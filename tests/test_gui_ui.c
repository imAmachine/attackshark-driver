#include <glib.h>

#include "as-store.h"
#include "as-worker.h"
#include "../src/gui/as-ui.c"
#define on_store_changed dialog_on_store_changed
#include "../src/gui/as-key-dialog.c"
#undef on_store_changed
#define on_store_changed keyboard_on_store_changed
#include "../src/gui/as-keyboard-page.c"
#undef on_store_changed

static void test_key_lists(void)
{
    AsKeyboardPage page = { 0 };
    AsDevice dev = { .key_code_valid = 1 };

    dev.key_code[9] = 4;
    rebuild_key_lists(&page, &dev);
    g_assert_cmpint(page.key_count, ==, 1);
    g_assert_cmpint(page.key_positions[0], ==, 9);
    page.key_positions[0] = -2;
    dev.key_actuation[9] = 1.5;
    dev.key_option[9] = AS_KB_OPTION_RT;
    rebuild_key_lists(&page, &dev);
    g_assert_cmpint(page.key_positions[0], ==, -2);
    dev.key_code[15] = 22;
    rebuild_key_lists(&page, &dev);
    g_assert_cmpint(page.key_count, ==, 2);
    g_assert_cmpint(page.key_positions[0], ==, 9);
    dev.key_code_valid = 0;
    rebuild_key_lists(&page, &dev);
    g_assert_cmpint(page.key_count, ==, AS_KB_KEY_MAX);
}

static void test_rt_dirty(void)
{
    KeyDialog dialog = { 0 };

    dialog.updating = TRUE;
    on_rt_toggle(NULL, NULL, &dialog);
    g_assert_false(dialog.edited);
    g_assert_false(dialog.edited_while_pending);
    dialog.updating = FALSE;
    on_rt_toggle(NULL, NULL, &dialog);
    g_assert_true(dialog.edited);
    g_assert_true(dialog.edited_while_pending);
}

static gboolean have_display;

static void test_stale_completion(void)
{
    KeyDialog dialog = { .pending_request = 42, .edited = TRUE };

    g_strlcpy(dialog.path, "/nonexistent/gui-test", sizeof(dialog.path));
    on_operation_completed(NULL, dialog.path, AS_OP_KB_KEYS_TRAVEL, 41,
                           TRUE, &dialog);
    on_operation_completed(NULL, "/other", AS_OP_KB_KEYS_TRAVEL, 42,
                           TRUE, &dialog);
    on_operation_completed(NULL, dialog.path, AS_OP_KB_SNAP, 42,
                           TRUE, &dialog);
    g_assert_cmpuint(dialog.pending_request, ==, 42);
    g_assert_true(dialog.edited);
}

static void test_dirty_refresh(void)
{
    AsStore *store = as_store_get();
    AsDevice *dev;
    GListModel *windows;
    GtkWindow *window = NULL;
    KeyDialog *dialog = NULL;
    int key = keyboxes[0].pos;

    if (!have_display) {
        g_test_skip("GTK display unavailable");
        return;
    }
    dev = g_new0(AsDevice, 1);
    g_strlcpy(dev->path, "/nonexistent/gui-widget-test", sizeof(dev->path));
    dev->key_actuation[key] = 1.0;
    g_ptr_array_add(as_store_devices(store), dev);
    as_key_dialog_open(NULL, store, dev->path);
    windows = gtk_window_get_toplevels();
    for (guint i = 0; i < g_list_model_get_n_items(windows); i++) {
        GtkWindow *candidate = g_list_model_get_item(windows, i);

        dialog = g_object_get_data(G_OBJECT(candidate), "as-key-dialog");
        if (dialog) {
            window = candidate;
            break;
        }
        g_object_unref(candidate);
    }
    g_assert_nonnull(dialog);
    g_assert_cmpfloat(dialog->act, ==, 1.0);
    adw_switch_row_set_active(dialog->rt, TRUE);
    g_assert_true(dialog->edited);
    dev->key_actuation[key] = 0.5;
    dialog_on_store_changed(store, dialog);
    g_source_remove(dialog->refresh_id);
    refresh_later(dialog);
    g_assert_cmpfloat(dialog->act, ==, 1.0);
    g_assert_true(adw_switch_row_get_active(dialog->rt));
    dialog->pending_request = 42;
    dialog->pending_count = 1;
    dialog->edited_while_pending = TRUE;
    on_operation_completed(store, dev->path, AS_OP_KB_KEYS_TRAVEL, 42,
                           TRUE, dialog);
    g_assert_true(dialog->edited);
    g_assert_cmpfloat(dialog->act, ==, 1.0);
    dialog->pending_request = 43;
    gtk_label_set_text(GTK_LABEL(dialog->status), "write failed");
    on_operation_completed(store, dev->path, AS_OP_KB_KEYS_TRAVEL, 43,
                           FALSE, dialog);
    g_assert_true(dialog->edited);
    g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(dialog->status)), ==,
                    "write failed");
    dialog->pending_request = 44;
    dialog->edited_while_pending = FALSE;
    on_operation_completed(store, dev->path, AS_OP_KB_KEYS_TRAVEL, 44,
                           TRUE, dialog);
    g_assert_false(dialog->edited);
    g_assert_cmpfloat(dialog->act, ==, 0.5);
    gtk_window_destroy(window);
    g_object_unref(window);
    g_ptr_array_set_size(as_store_devices(store), 0);
}

static void test_queue(void)
{
    AsPage page = { 0 };
    AsOpRequest req = { .op = AS_OP_MOUSE_DPI_STAGE, .index = 0, .value = 800 };

    as_page_queue(&page, &req);
    req.index = 1;
    req.value = 1600;
    as_page_queue(&page, &req);
    req.index = 0;
    req.value = 1200;
    as_page_queue(&page, &req);
    g_assert_cmpint(page.pending_count, ==, 2);
    g_assert_cmpint(page.pending[0].value, ==, 1200);
    g_assert_cmpint(page.pending[1].value, ==, 1600);
    req.op = AS_OP_KB_SNAP;
    req.index = 9;
    as_page_queue(&page, &req);
    req.index = 15;
    as_page_queue(&page, &req);
    g_assert_cmpint(page.pending_count, ==, 4);
    req.op = AS_OP_KB_DEBOUNCE;
    as_page_queue(&page, &req);
    req.index = 0;
    req.value = 3;
    as_page_queue(&page, &req);
    g_assert_cmpint(page.pending_count, ==, 5);
    g_assert_cmpint(page.pending[4].value, ==, 3);
    g_source_remove(page.pending_id);
}

typedef struct {
    gboolean completed;
    int errors;
} Completion;

static void on_error(AsStore *store, const char *path, const char *message,
                     gpointer data)
{
    Completion *result = data;

    (void)store;
    g_assert_cmpstr(path, ==, "/nonexistent/gui-test");
    g_assert_nonnull(message);
    result->errors++;
}

static void on_completed(AsStore *store, const char *path, int op,
                         guint64 request_id, gboolean success, gpointer data)
{
    Completion *result = data;

    (void)store;
    g_assert_cmpstr(path, ==, "/nonexistent/gui-test");
    g_assert_cmpint(op, ==, AS_OP_KB_KEYS_TRAVEL);
    g_assert_cmpuint(request_id, ==, 42);
    g_assert_false(success);
    g_assert_cmpint(result->errors, ==, 1);
    result->completed = TRUE;
}

static void test_completion(void)
{
    AsStore *store = as_store_get();
    AsDevice *dev = g_new0(AsDevice, 1);
    AsOpRequest req = { .op = AS_OP_KB_KEYS_TRAVEL, .request_id = 42 };
    Completion result = { 0 };
    gint64 deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;

    g_assert_false(as_store_apply(store, "/nonexistent/gui-test", &req));
    g_strlcpy(dev->path, "/nonexistent/gui-test", sizeof(dev->path));
    g_ptr_array_add(as_store_devices(store), dev);
    g_signal_connect(store, "error", G_CALLBACK(on_error), &result);
    g_signal_connect(store, "operation-completed", G_CALLBACK(on_completed),
                     &result);
    as_worker_start();
    g_assert_true(as_store_apply(store, dev->path, &req));
    g_assert_false(result.completed);
    while (!result.completed && g_get_monotonic_time() < deadline) {
        g_main_context_iteration(NULL, FALSE);
        g_usleep(1000);
    }
    as_worker_stop();
    g_assert_true(result.completed);
    g_assert_cmpint(result.errors, ==, 1);
    g_signal_handlers_disconnect_by_data(store, &result);
    as_store_shutdown(store);
    g_ptr_array_set_size(as_store_devices(store), 0);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    have_display = gtk_init_check();
    g_test_add_func("/gui/dialog/stale-completion", test_stale_completion);
    g_test_add_func("/gui/dialog/dirty-refresh", test_dirty_refresh);
    g_test_add_func("/gui/keyboard/matrix-only-rebuild", test_key_lists);
    g_test_add_func("/gui/dialog/rt-dirty", test_rt_dirty);
    g_test_add_func("/gui/queue/indexed-coalescing", test_queue);
    g_test_add_func("/gui/store/failed-completion", test_completion);
    return g_test_run();
}
