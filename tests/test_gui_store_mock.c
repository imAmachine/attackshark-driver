#include <glib.h>
#include <stdio.h>

#define main gui_store_main
#include "test_gui_store.c"
#undef main

static GPtrArray *mock_devices;

void as_worker_start(void) {}
void as_worker_stop(void) {}
void as_store_scan(AsStore *self) { (void)self; }
void as_store_shutdown(AsStore *self) { (void)self; }
AsStore *as_store_get(void) { return NULL; }
GPtrArray *as_store_devices(AsStore *self) { (void)self; return mock_devices; }
AsDevice *as_store_lookup(AsStore *self, const char *path)
{
    (void)self;
    (void)path;
    return NULL;
}
gboolean as_store_apply(AsStore *self, const char *path, const AsOpRequest *req)
{
    (void)self;
    (void)path;
    return req->request_id != 99;
}

static void reset_state(void)
{
    g_hash_table_remove_all(expected_ids);
    g_hash_table_remove_all(completed_ids);
    expected_completions = 0;
    completed_completions = 0;
    successful_completions = 0;
    failed_completions = 0;
    timed_out = FALSE;
}

static void expect_completion(guint64 id, gboolean success)
{
    on_operation_completed(store, "/mock", AS_OP_KB_RATE, id, success, NULL);
}

static void test_completion_accounting(void)
{
    guint64 id1 = 1, id2 = 2, foreign = 77;

    reset_state();
    g_hash_table_add(expected_ids, g_memdup2(&id1, sizeof(id1)));
    g_hash_table_add(expected_ids, g_memdup2(&id2, sizeof(id2)));
    expected_completions = 2;
    expect_completion(id2, FALSE);
    expect_completion(foreign, TRUE);
    expect_completion(id2, TRUE);
    expect_completion(id1, TRUE);
    g_assert_cmpuint(completed_completions, ==, 2);
    g_assert_cmpuint(successful_completions, ==, 1);
    g_assert_cmpuint(failed_completions, ==, 1);
}

static void test_rejected_apply(void)
{
    AsDevice dev = { 0 };

    reset_state();
    store = (AsStore *)0x1;
    g_strlcpy(dev.path, "/mock", sizeof(dev.path));
    dev.rate = 1000;
    next_request_id = 97;
    g_assert_true(write_back(&dev));
    g_assert_false(write_back(&dev));
    g_assert_cmpuint(expected_completions, ==, 1);
}

static void test_no_devices(void)
{
    GPtrArray *devices = g_ptr_array_new();

    reset_state();
    scan_seen = FALSE;
    mock_devices = devices;
    on_changed(store, NULL);
    g_assert_true(scan_seen);
    g_assert_cmpuint(expected_completions, ==, 0);
    g_ptr_array_unref(devices);
}

static void test_timeout(void)
{
    guint64 id = 3;

    reset_state();
    g_hash_table_add(expected_ids, g_memdup2(&id, sizeof(id)));
    expected_completions = 1;
    on_timeout(NULL);
    g_assert_true(timed_out);
}

int main(void)
{
    loop = g_main_loop_new(NULL, FALSE);
    expected_ids = g_hash_table_new_full(g_int64_hash, g_int64_equal, g_free, NULL);
    completed_ids = g_hash_table_new_full(g_int64_hash, g_int64_equal, g_free, NULL);
    test_completion_accounting();
    test_rejected_apply();
    test_no_devices();
    test_timeout();
    g_hash_table_unref(expected_ids);
    g_hash_table_unref(completed_ids);
    g_main_loop_unref(loop);
    puts("mock GUI store regression: ok");
    return 0;
}
