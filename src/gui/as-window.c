// SPDX-License-Identifier: MIT
#include "as-window.h"

#include <glib/gstdio.h>
#include <string.h>

#include "as-keyboard-page.h"
#include "as-mouse-page.h"
#include "as-store.h"
#include "as-ui.h"

/* Battery and link state change slowly, but the mouse state read is expensive
 * (256-byte EEPROM block), so the dashboard refreshes on a relaxed schedule. */
#define AUTO_REFRESH_SECONDS 15

typedef struct {
    AsStore *store;
    GtkWidget *window;
    GtkWidget *list;
    GtkWidget *stack;
    GtkWidget *toast_overlay;
    AdwBanner *banner;
    AdwWindowTitle *title;
    GHashTable *rows;      /* path -> GtkListBoxRow* */
    GHashTable *pages;     /* path -> page widget */
    char *selected;
    int prefer_kind;   /* AS_DEVICE_* to preselect, or -1 */
    guint refresh_id;
    gboolean fixing;        /* an install attempt is running */
    gboolean auto_fixed;    /* the silent root path was already tried */
} AsWindow;

static AsWindow *window_of(gpointer widget)
{
    return g_object_get_data(G_OBJECT(widget), "as-window");
}

/* ------------------------------- pages -------------------------------- */

static GtkWidget *page_for(AsWindow *w, const AsDevice *dev)
{
    GtkWidget *page = g_hash_table_lookup(w->pages, dev->path);

    if (page) {
        return page;
    }
    page = dev->kind == AS_DEVICE_MOUSE
               ? as_mouse_page_new(w->store, dev->path)
               : as_keyboard_page_new(w->store, dev->path);
    gtk_stack_add_named(GTK_STACK(w->stack), page, dev->path);
    g_hash_table_insert(w->pages, g_strdup(dev->path), page);
    return page;
}

static void drop_page(AsWindow *w, const char *path)
{
    GtkWidget *page = g_hash_table_lookup(w->pages, path);

    if (!page) {
        return;
    }
    gtk_stack_remove(GTK_STACK(w->stack), page);
    g_hash_table_remove(w->pages, path);
}

static void select_path(AsWindow *w, const char *path)
{
    if (!path) {
        g_clear_pointer(&w->selected, g_free);
        gtk_stack_set_visible_child_name(GTK_STACK(w->stack), "empty");
        adw_window_title_set_title(w->title, "Attack Shark");
        adw_window_title_set_subtitle(w->title, NULL);
        return;
    }

    AsDevice *dev = as_store_lookup(w->store, path);
    if (!dev) {
        return;
    }
    page_for(w, dev);
    gtk_stack_set_visible_child_name(GTK_STACK(w->stack), dev->path);

    g_free(w->selected);
    w->selected = g_strdup(path);

    char subtitle[96];
    snprintf(subtitle, sizeof(subtitle), "%04x:%04x · %s", dev->vid, dev->pid,
             dev->path);
    adw_window_title_set_title(w->title, dev->product);
    adw_window_title_set_subtitle(w->title, subtitle);
}

/* -------------------------------- rows -------------------------------- */

static const char *row_subtitle(const AsDevice *dev)
{
    static char buf[192];

    char *text = dev->kind == AS_DEVICE_MOUSE ? as_ui_mouse_summary(dev)
                                              : as_ui_keyboard_summary(dev);
    g_strlcpy(buf, text, sizeof(buf));
    g_free(text);
    return buf;
}

static GtkWidget *row_new(AsWindow *w, const AsDevice *dev)
{
    /* AdwActionRow is itself a GtkListBoxRow: it must be added to the list
     * box directly, without a wrapper row. */
    GtkWidget *row = adw_action_row_new();
    GtkWidget *icon = gtk_image_new_from_icon_name(
        dev->kind == AS_DEVICE_MOUSE ? "input-mouse-symbolic"
                                     : "input-keyboard-symbolic");

    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), dev->product);
    adw_action_row_set_subtitle(ADW_ACTION_ROW(row), row_subtitle(dev));
    adw_action_row_add_prefix(ADW_ACTION_ROW(row), icon);
    g_object_set_data_full(G_OBJECT(row), "as-path", g_strdup(dev->path),
                           g_free);
    gtk_list_box_append(GTK_LIST_BOX(w->list), row);
    return row;
}

static void sync_rows(AsWindow *w)
{
    GPtrArray *devs = as_store_devices(w->store);
    GHashTable *seen = g_hash_table_new(g_str_hash, g_str_equal);

    for (guint i = 0; i < devs->len; i++) {
        AsDevice *dev = devs->pdata[i];
        GtkWidget *row = g_hash_table_lookup(w->rows, dev->path);

        g_hash_table_add(seen, dev->path);
        if (!row) {
            row = row_new(w, dev);
            g_hash_table_insert(w->rows, g_strdup(dev->path), row);
        } else {
            adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row),
                                          dev->product);
            adw_action_row_set_subtitle(ADW_ACTION_ROW(row),
                                        row_subtitle(dev));
        }
    }

    /* Devices that are gone must not leave dead rows or pages behind. */
    GList *stale = NULL;
    GHashTableIter iter;
    gpointer key;
    g_hash_table_iter_init(&iter, w->rows);
    while (g_hash_table_iter_next(&iter, &key, NULL)) {
        if (!g_hash_table_contains(seen, key)) {
            stale = g_list_prepend(stale, g_strdup(key));
        }
    }
    for (GList *l = stale; l; l = l->next) {
        const char *path = l->data;
        GtkWidget *row = g_hash_table_lookup(w->rows, path);

        drop_page(w, path);
        if (row) {
            gtk_list_box_remove(GTK_LIST_BOX(w->list), row);
        }
        g_hash_table_remove(w->rows, path);
        if (w->selected && strcmp(w->selected, path) == 0) {
            g_clear_pointer(&w->selected, g_free);
        }
    }
    g_list_free_full(stale, g_free);
    g_hash_table_destroy(seen);

    if (w->selected) {
        GtkWidget *row = g_hash_table_lookup(w->rows, w->selected);
        if (row) {
            gtk_list_box_select_row(GTK_LIST_BOX(w->list),
                                    GTK_LIST_BOX_ROW(row));
        }
    } else if (as_store_count(w->store) > 0) {
        AsDevice *dev = devs->pdata[0];

        for (guint i = 0; i < devs->len; i++) {
            AsDevice *candidate = devs->pdata[i];
            if ((int)candidate->kind == w->prefer_kind) {
                dev = candidate;
                break;
            }
        }
        select_path(w, dev->path);
        GtkWidget *row = g_hash_table_lookup(w->rows, dev->path);
        if (row) {
            gtk_list_box_select_row(GTK_LIST_BOX(w->list),
                                    GTK_LIST_BOX_ROW(row));
        }
    } else {
        select_path(w, NULL);
    }
}

/* --------------------------- permissions ------------------------------ */

static void show_toast(AsWindow *w, const char *message)
{
    adw_toast_overlay_add_toast(ADW_TOAST_OVERLAY(w->toast_overlay),
                                adw_toast_new(message));
}

/* The fix is one click: the helper mode of this very binary writes the rules
 * file and re-triggers udev. pkexec asks for the password; when the app is
 * already running as root no prompt is needed and the driver just does it. */
static void on_fix_helper_done(GObject *source, GAsyncResult *res,
                               gpointer data)
{
    AsWindow *w = data;
    char *out = NULL, *errtext = NULL;
    GError *error = NULL;

    w->fixing = FALSE;
    if (!g_subprocess_communicate_utf8_finish(G_SUBPROCESS(source), res, &out,
                                              &errtext, &error)) {
        show_toast(w, error->message);
        g_clear_error(&error);
        g_free(out);
        g_free(errtext);
        return;
    }
    if (!g_subprocess_get_successful(G_SUBPROCESS(source))) {
        char *message = errtext && errtext[0]
                            ? g_strstrip(errtext)
                            : g_strdup("Не удалось установить правила udev");
        show_toast(w, message);
        g_free(message);
        g_free(out);
        g_free(errtext);
        return;
    }
    g_free(out);
    g_free(errtext);
    adw_banner_set_revealed(w->banner, FALSE);
    show_toast(w, "Правила udev установлены");
    as_store_scan(w->store);
}

static void on_fix_clicked(AdwBanner *banner, gpointer widget)
{
    AsWindow *w = window_of(widget);
    GError *error = NULL;
    GSubprocess *proc;
    char *self = g_file_read_link("/proc/self/exe", NULL);

    (void)banner;
    if (w->fixing || as_store_busy(w->store)) {
        return;
    }
    if (as_udev_can_install()) {
        w->fixing = TRUE;
        as_store_fix_permissions(w->store);
        return;
    }
    if (!self) {
        show_toast(w, "Не удалось определить путь к программе");
        return;
    }
    proc = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_SILENCE |
                                G_SUBPROCESS_FLAGS_STDERR_PIPE,
                            &error, "pkexec", self, "--udev-install", NULL);
    if (!proc) {
        char message[256];
        snprintf(message, sizeof(message),
                 "%s — выполните «sudo attackshark permissions install»",
                 error->message);
        show_toast(w, message);
        g_clear_error(&error);
        g_free(self);
        return;
    }
    w->fixing = TRUE;
    g_subprocess_communicate_utf8_async(proc, NULL, NULL, on_fix_helper_done, w);
    g_object_unref(proc);
    g_free(self);
}

static void on_permissions_fixed(AsStore *store, gpointer widget)
{
    AsWindow *w = window_of(widget);

    (void)store;
    w->fixing = FALSE;
    adw_banner_set_revealed(w->banner, FALSE);
    show_toast(w, "Правила udev установлены");
}

static void sync_permissions(AsWindow *w)
{
    gboolean denied = as_store_permissions_denied(w->store);

    adw_banner_set_revealed(w->banner, denied);
    if (!denied || w->fixing || w->auto_fixed || as_store_busy(w->store)) {
        return;
    }
    /* Already root: there is nobody to ask, so fix it instead of nagging. */
    if (as_udev_can_install()) {
        w->auto_fixed = TRUE;
        w->fixing = TRUE;
        as_store_fix_permissions(w->store);
    }
}

/* ------------------------------ callbacks ----------------------------- */

static void on_store_changed(AsStore *store, gpointer widget)
{
    AsWindow *w = window_of(widget);

    (void)store;
    sync_rows(w);
    sync_permissions(w);
}

static void on_store_error(AsStore *store, const char *path,
                           const char *message, gpointer widget)
{
    AsWindow *w = window_of(widget);
    AdwToast *toast = adw_toast_new(message);

    (void)store;
    /* A failed install must not leave the fix button latched off. */
    if (!path && w->fixing) {
        w->fixing = FALSE;
    }
    adw_toast_overlay_add_toast(ADW_TOAST_OVERLAY(w->toast_overlay), toast);
}

static void on_row_selected(GtkListBox *list, GtkListBoxRow *row,
                            gpointer widget)
{
    AsWindow *w = window_of(widget);

    (void)list;
    if (!row) {
        return;
    }
    const char *path = g_object_get_data(G_OBJECT(row), "as-path");

    if (path) {
        select_path(w, path);
    }
}

static void on_refresh_clicked(GtkButton *button, gpointer widget)
{
    AsWindow *w = window_of(widget);

    (void)button;
    as_store_scan(w->store);
}

static gboolean on_auto_refresh(gpointer widget)
{
    AsWindow *w = window_of(widget);

    if (!as_store_busy(w->store)) {
        as_store_scan(w->store);
    }
    return G_SOURCE_CONTINUE;
}

static void on_window_map(GtkWidget *widget, gpointer data)
{
    AsWindow *w = window_of(widget);

    (void)data;
    if (!as_store_busy(w->store)) {
        as_store_scan(w->store);
    }
}

static void window_free(gpointer data)
{
    AsWindow *w = data;

    if (w->refresh_id) {
        g_source_remove(w->refresh_id);
    }
    g_hash_table_destroy(w->rows);
    g_hash_table_destroy(w->pages);
    g_free(w->selected);
    g_free(w);
}

/* ------------------------------ construction --------------------------- */

static GtkWidget *build_sidebar(AsWindow *w)
{
    GtkWidget *view = adw_toolbar_view_new();
    GtkWidget *header = adw_header_bar_new();
    GtkWidget *title = adw_window_title_new("Attack Shark", "Устройства");
    GtkWidget *refresh = gtk_button_new_from_icon_name("view-refresh-symbolic");
    GtkWidget *scroll = gtk_scrolled_window_new();
    GtkWidget *placeholder = adw_status_page_new();

    gtk_widget_set_tooltip_text(refresh, "Обновить состояние устройств");
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(header), title);
    adw_header_bar_pack_end(ADW_HEADER_BAR(header), refresh);

    adw_status_page_set_title(ADW_STATUS_PAGE(placeholder),
                              "Устройства не найдены");
    adw_status_page_set_description(
        ADW_STATUS_PAGE(placeholder),
        "Подключите мышь или клавиатуру Attack Shark по USB. Если устройство "
        "подключено, нажмите «Исправить» в шапке окна: драйвер сам установит "
        "правила udev и откроет доступ к hidraw.");

    w->list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(w->list), GTK_SELECTION_SINGLE);
    gtk_list_box_set_show_separators(GTK_LIST_BOX(w->list), TRUE);
    gtk_list_box_set_placeholder(GTK_LIST_BOX(w->list), placeholder);
    gtk_widget_add_css_class(w->list, "navigation-sidebar");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), w->list);
    gtk_widget_set_vexpand(scroll, TRUE);

    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(view), header);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(view), scroll);

    g_signal_connect_object(refresh, "clicked", G_CALLBACK(on_refresh_clicked),
                            w->window, 0);
    g_signal_connect_object(w->list, "row-selected",
                            G_CALLBACK(on_row_selected), w->window, 0);
    return view;
}

static GtkWidget *build_content(AsWindow *w)
{
    GtkWidget *view = adw_toolbar_view_new();
    GtkWidget *header = adw_header_bar_new();
    GtkWidget *empty = adw_status_page_new();
    GtkWidget *clamp;

    w->title = ADW_WINDOW_TITLE(adw_window_title_new("Attack Shark", NULL));
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(header), GTK_WIDGET(w->title));

    adw_status_page_set_icon_name(ADW_STATUS_PAGE(empty), "input-mouse-symbolic");
    adw_status_page_set_title(ADW_STATUS_PAGE(empty), "Выберите устройство");
    adw_status_page_set_description(
        ADW_STATUS_PAGE(empty),
        "Слева перечислены подключённые мыши и клавиатуры Attack Shark. "
        "Настройки применяются сразу и сохраняются в памяти устройства.");

    w->stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(w->stack),
                                  GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_add_named(GTK_STACK(w->stack), empty, "empty");

    clamp = adw_clamp_new();
    adw_clamp_set_child(ADW_CLAMP(clamp), w->stack);
    adw_clamp_set_maximum_size(ADW_CLAMP(clamp), 720);

    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(view), header);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(view), clamp);
    return view;
}

GtkWidget *as_window_new(GtkApplication *app)
{
    AsWindow *w = g_new0(AsWindow, 1);
    AdwNavigationSplitView *split;
    AdwNavigationPage *sidebar_page;
    AdwNavigationPage *content_page;
    GtkWidget *window;

    w->store = as_store_get();
    w->prefer_kind = -1;
    if (g_strcmp0(g_getenv("ATTACKSHARK_GUI_SELECT"), "mouse") == 0) {
        w->prefer_kind = AS_DEVICE_MOUSE;
    } else if (g_strcmp0(g_getenv("ATTACKSHARK_GUI_SELECT"), "keyboard") == 0) {
        w->prefer_kind = AS_DEVICE_KEYBOARD;
    }
    w->rows = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    w->pages = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    window = adw_application_window_new(app);
    w->window = window;
    g_object_set_data_full(G_OBJECT(window), "as-window", w, window_free);

    gtk_window_set_title(GTK_WINDOW(window), "Attack Shark");
    gtk_window_set_default_size(GTK_WINDOW(window), 980, 720);

    split = ADW_NAVIGATION_SPLIT_VIEW(adw_navigation_split_view_new());
    adw_navigation_split_view_set_collapsed(split, FALSE);
    adw_navigation_split_view_set_min_sidebar_width(split, 260);
    adw_navigation_split_view_set_max_sidebar_width(split, 340);

    sidebar_page = adw_navigation_page_new(build_sidebar(w), "Устройства");
    adw_navigation_split_view_set_sidebar(split, sidebar_page);
    content_page = adw_navigation_page_new(build_content(w), "Настройки");
    adw_navigation_split_view_set_content(split, content_page);

    /* Root-owned hidraw nodes are the one problem the app can solve for the
     * user, so it says so in one line and offers a single button. */
    w->banner = ADW_BANNER(
        adw_banner_new("Нет доступа к устройствам: нужны правила udev"));
    adw_banner_set_button_label(w->banner, "Исправить");
    adw_banner_set_revealed(w->banner, FALSE);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(box), GTK_WIDGET(w->banner));
    gtk_box_append(GTK_BOX(box), GTK_WIDGET(split));
    gtk_widget_set_vexpand(GTK_WIDGET(split), TRUE);

    w->toast_overlay = adw_toast_overlay_new();
    adw_toast_overlay_set_child(ADW_TOAST_OVERLAY(w->toast_overlay), box);
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(window),
                                       w->toast_overlay);

    g_signal_connect_object(w->store, "changed", G_CALLBACK(on_store_changed),
                            window, 0);
    g_signal_connect_object(w->store, "error", G_CALLBACK(on_store_error),
                            window, 0);
    g_signal_connect_object(w->store, "permissions-fixed",
                            G_CALLBACK(on_permissions_fixed), window, 0);
    g_signal_connect_object(w->banner, "button-clicked",
                            G_CALLBACK(on_fix_clicked), window, 0);
    g_signal_connect_object(window, "map", G_CALLBACK(on_window_map), NULL, 0);

    w->refresh_id = g_timeout_add_seconds(AUTO_REFRESH_SECONDS, on_auto_refresh,
                                          window);

    as_store_scan(w->store);
    sync_rows(w);
    return window;
}
