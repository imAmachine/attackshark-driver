#include <adwaita.h>
#include "as-store.h"

static void mock_scan(AsStore *store)
{
    (void)store;
    g_assert_not_reached();
}

#define as_store_scan mock_scan
#include "../src/gui/as-window.c"
#define main gui_main
#include "../src/gui/main.c"
#undef main

static gboolean completed;

static void helper_done(GObject *source, GAsyncResult *result, gpointer data)
{
    on_fix_helper_done(source, result, data);
    completed = TRUE;
}

static void test_expired_helper(gconstpointer data)
{
    GObject *window = g_object_new(G_TYPE_OBJECT, NULL);
    GWeakRef *ref = g_new0(GWeakRef, 1);
    GCancellable *cancel = g_cancellable_new();
    GError *error = NULL;
    GSubprocess *proc;

    g_weak_ref_init(ref, window);
    proc = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_SILENCE |
                               G_SUBPROCESS_FLAGS_STDERR_PIPE,
                           &error, "/bin/true", NULL);
    g_assert_no_error(error);
    completed = FALSE;
    g_subprocess_communicate_utf8_async(proc, NULL, cancel, helper_done, ref);
    g_object_unref(window);
    if (GPOINTER_TO_INT(data)) {
        g_cancellable_cancel(cancel);
    }
    while (!completed) {
        g_main_context_iteration(NULL, TRUE);
    }
    g_object_unref(cancel);
    g_object_unref(proc);
}

static gboolean unexpected_timeout(gpointer data)
{
    (void)data;
    g_assert_not_reached();
    return G_SOURCE_REMOVE;
}

static void test_source_cleanup(void)
{
    WindowSources *sources = g_new0(WindowSources, 1);
    guint shot, layout;

    g_weak_ref_init(&sources->window, NULL);
    sources->screenshot_id = shot = g_timeout_add(10000, unexpected_timeout,
                                                  NULL);
    sources->layout_id = layout = g_timeout_add(10000, unexpected_timeout,
                                               NULL);
    window_sources_stop(sources);
    g_assert_cmpuint(sources->screenshot_id, ==, 0);
    g_assert_cmpuint(sources->layout_id, ==, 0);
    g_assert_null(g_main_context_find_source_by_id(NULL, shot));
    g_assert_null(g_main_context_find_source_by_id(NULL, layout));
    window_sources_free(sources);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_data_func("/window/helper-expired", GINT_TO_POINTER(0),
                         test_expired_helper);
    g_test_add_data_func("/window/helper-cancelled", GINT_TO_POINTER(1),
                         test_expired_helper);
    g_test_add_func("/window/source-cleanup", test_source_cleanup);
    return g_test_run();
}
