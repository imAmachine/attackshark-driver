// SPDX-License-Identifier: MIT
#include <adwaita.h>
#include <cairo.h>
#include <string.h>

#include "as-key-dialog.h"
#include "as-store.h"
#include "as-window.h"
#include "as-worker.h"

/* Opened from the command line, before the window exists, so the layout is
 * only reachable once the store has found the keyboard. */
typedef struct {
    GWeakRef window;
    guint screenshot_id;
    guint layout_id;
} WindowSources;

static void window_sources_stop(WindowSources *sources)
{
    if (sources->screenshot_id) {
        g_source_remove(sources->screenshot_id);
        sources->screenshot_id = 0;
    }
    if (sources->layout_id) {
        g_source_remove(sources->layout_id);
        sources->layout_id = 0;
    }
}

static void window_sources_free(gpointer data)
{
    WindowSources *sources = data;

    window_sources_stop(sources);
    g_weak_ref_clear(&sources->window);
    g_free(sources);
}

static void on_shot_application_changed(GObject *window, GParamSpec *pspec,
                                         gpointer data)
{
    (void)pspec;
    if (!gtk_window_get_application(GTK_WINDOW(window))) {
        window_sources_stop(data);
    }
}

static gboolean open_key_layout(gpointer data)
{
    WindowSources *sources = data;
    GtkWindow *window = g_weak_ref_get(&sources->window);
    GPtrArray *devices;

    if (!window || !gtk_window_get_application(window)) {
        sources->layout_id = 0;
        g_clear_object(&window);
        return G_SOURCE_REMOVE;
    }
    devices = as_store_devices(as_store_get());
    for (guint i = 0; i < devices->len; i++) {
        AsDevice *dev = devices->pdata[i];

        if (dev->kind == AS_DEVICE_KEYBOARD && !dev->error[0]) {
            sources->layout_id = 0;
            as_key_dialog_open(window, as_store_get(), dev->path);
            g_object_unref(window);
            return G_SOURCE_REMOVE;
        }
    }
    g_object_unref(window);
    return G_SOURCE_CONTINUE;
}

/* Renders the whole window into a PNG. Useful for documentation and for
 * checking the layout without a visible session:
 *
 *     ATTACKSHARK_GUI_SHOT=/tmp/shot.png ./attackshark-gui
 */
static gboolean save_screenshot(gpointer data)
{
    WindowSources *sources = data;
    GtkWidget *window = g_weak_ref_get(&sources->window);
    const char *path = g_getenv("ATTACKSHARK_GUI_SHOT");
    int width, height;
    GdkPaintable *paintable;
    GtkSnapshot *snapshot;
    GskRenderNode *node;
    cairo_surface_t *surface;
    cairo_t *cr;

    if (!window || !gtk_window_get_application(GTK_WINDOW(window)) || !path) {
        sources->screenshot_id = 0;
        g_clear_object(&window);
        return G_SOURCE_REMOVE;
    }
    width = gtk_widget_get_width(window);
    height = gtk_widget_get_height(window);
    if (width <= 0 || height <= 0) {
        g_object_unref(window);
        return G_SOURCE_CONTINUE;
    }

    paintable = gtk_widget_paintable_new(window);
    snapshot = gtk_snapshot_new();
    gdk_paintable_snapshot(GDK_PAINTABLE(paintable), snapshot, width, height);
    node = gtk_snapshot_free_to_node(snapshot);
    g_object_unref(paintable);
    g_object_unref(window);
    if (!node) {
        return G_SOURCE_CONTINUE;
    }
    sources->screenshot_id = 0;
    surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cr = cairo_create(surface);
    gsk_render_node_draw(node, cr);
    cairo_destroy(cr);
    cairo_surface_flush(surface);
    cairo_status_t status = cairo_surface_write_to_png(surface, path);
    cairo_surface_destroy(surface);
    gsk_render_node_unref(node);

    if (status != CAIRO_STATUS_SUCCESS) {
        g_printerr("screenshot: %s\n", cairo_status_to_string(status));
        g_application_quit(g_application_get_default());
        return G_SOURCE_REMOVE;
    }
    g_print("screenshot: %s (%dx%d)\n", path, width, height);
    g_application_quit(g_application_get_default());
    return G_SOURCE_REMOVE;
}

static void on_activate(GtkApplication *app, gpointer user_data)
{
    GtkWindow *window = gtk_application_get_active_window(app);
    WindowSources *sources;

    (void)user_data;
    if (window) {
        gtk_window_present(window);
        return;
    }
    window = GTK_WINDOW(as_window_new(app));
    sources = g_new0(WindowSources, 1);
    g_weak_ref_init(&sources->window, window);
    g_object_set_data_full(G_OBJECT(window), "as-window-sources", sources,
                           window_sources_free);
    g_signal_connect(window, "notify::application",
                     G_CALLBACK(on_shot_application_changed), sources);
    gtk_window_present(window);
    if (g_getenv("ATTACKSHARK_GUI_SHOT")) {
        sources->screenshot_id = g_timeout_add(4000, save_screenshot, sources);
    }
    if (g_getenv("ATTACKSHARK_GUI_KEYDIALOG")) {
        sources->layout_id = g_timeout_add(2500, open_key_layout, sources);
    }
}

/* Privileged helper mode. The window runs itself through pkexec with this
 * flag when the hidraw nodes are root-owned, so the fix is one click and no
 * terminal or manual rule copying is involved. */
static int run_udev_install(void)
{
    char err[256] = "";
    int rc = as_udev_install(err, sizeof(err));

    if (rc < 0) {
        fprintf(stderr, "attackshark-gui: %s\n",
                err[0] ? err : as_strerror(rc));
        return 1;
    }
    printf("installed %s\n", as_udev_rule_path());
    return 0;
}

int main(int argc, char **argv)
{
    AdwApplication *app;
    int status;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--udev-install") == 0) {
            return run_udev_install();
        }
    }

    g_set_application_name("Attack Shark");
    app = adw_application_new("org.attackshark.Gui",
                              G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);

    as_worker_start();
    status = g_application_run(G_APPLICATION(app), argc, argv);

    /* The worker owns every hidraw file descriptor, so it has to stop before
     * the handles are closed and the store goes away. */
    as_store_analog_stop(as_store_get());
    as_worker_stop();
    as_store_shutdown(as_store_get());
    while (g_main_context_iteration(NULL, FALSE)) {
    }
    g_object_unref(app);
    return status;
}
