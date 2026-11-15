// SPDX-License-Identifier: MIT
#include <adwaita.h>
#include <cairo.h>
#include <string.h>

#include "as-store.h"
#include "as-window.h"
#include "as-worker.h"

/* Renders the whole window into a PNG. Useful for documentation and for
 * checking the layout without a visible session:
 *
 *     ATTACKSHARK_GUI_SHOT=/tmp/shot.png ./attackshark-gui
 */
static gboolean save_screenshot(gpointer data)
{
    GtkWidget *window = data;
    const char *path = g_getenv("ATTACKSHARK_GUI_SHOT");
    int width = gtk_widget_get_width(window);
    int height = gtk_widget_get_height(window);
    GdkPaintable *paintable;
    GtkSnapshot *snapshot;
    GskRenderNode *node;
    cairo_surface_t *surface;
    cairo_t *cr;

    if (width <= 0 || height <= 0) {
        g_timeout_add(500, save_screenshot, window);
        return G_SOURCE_REMOVE;
    }

    paintable = gtk_widget_paintable_new(window);
    snapshot = gtk_snapshot_new();
    gdk_paintable_snapshot(GDK_PAINTABLE(paintable), snapshot, width, height);
    node = gtk_snapshot_free_to_node(snapshot);
    surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cr = cairo_create(surface);
    gsk_render_node_draw(node, cr);
    cairo_destroy(cr);
    cairo_surface_flush(surface);
    cairo_surface_write_to_png(surface, path);
    cairo_surface_destroy(surface);
    gsk_render_node_unref(node);
    g_object_unref(paintable);

    g_print("screenshot: %s (%dx%d)\n", path, width, height);
    g_application_quit(g_application_get_default());
    return G_SOURCE_REMOVE;
}

static void on_activate(GtkApplication *app, gpointer user_data)
{
    GtkWindow *window = GTK_WINDOW(as_window_new(app));

    (void)user_data;
    gtk_window_present(window);
    if (g_getenv("ATTACKSHARK_GUI_SHOT")) {
        g_timeout_add(4000, save_screenshot, window);
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
    as_worker_stop();
    as_store_shutdown(as_store_get());
    g_object_unref(app);
    return status;
}
