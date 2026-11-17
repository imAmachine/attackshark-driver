#include <glib.h>
#include "as-worker.h"

static guint queued;
static AsJobFunc queued_func;
static gpointer queued_data;
static AsJobFreeFunc queued_free;
static char *queued_path;
static GPtrArray *pending_jobs;

typedef struct {
    AsJobFunc func;
    gpointer data;
    AsJobFreeFunc free_data;
    AsJobDoneFunc done;
    gpointer user_data;
    AsJobFreeFunc free_user_data;
} CapturedJob;

static void capture_job(AsJobFunc func, gpointer data, AsJobFreeFunc free_data,
                        AsJobDoneFunc done, gpointer user_data,
                        AsJobFreeFunc free_user_data)
{
    CapturedJob *job = g_new0(CapturedJob, 1);
    *job = (CapturedJob){ func, data, free_data, done, user_data, free_user_data };
    if (!pending_jobs) {
        pending_jobs = g_ptr_array_new();
    }
    g_ptr_array_add(pending_jobs, job);
    queued++;
    queued_func = func;
    queued_data = data;
    queued_free = free_data;
}

static void capture_device_job(const char *path, AsJobFunc func, gpointer data,
                               AsJobFreeFunc free_data, AsJobDoneFunc done,
                               gpointer user_data, AsJobFreeFunc free_user_data)
{
    g_free(queued_path);
    queued_path = g_strdup(path);
    capture_job(func, data, free_data, done, user_data, free_user_data);
}

#include "as-store.h"
#include <errno.h>

static guint keyboard_open_calls;
static gboolean keyboard_identity_mismatch;

static int mock_mouse_open_path(struct as_mouse *mouse, const char *path)
{
    (void)mouse;
    (void)path;
    return -ENODEV;
}

static int mock_keyboard_open_path(struct as_keyboard *kb, const char *path)
{
    (void)path;
    keyboard_open_calls++;
    if (keyboard_identity_mismatch) {
        kb->hid.vid = 0x9999;
        return 0;
    }
    return -ENODEV;
}

static uint8_t incoming_reports[16][5];
static guint incoming_count;
static guint incoming_index;

static int mock_hid_read(struct as_hid *hid, void *buf, size_t len, int timeout)
{
    (void)hid;
    (void)timeout;
    g_assert_cmpuint(len, >=, 5);
    if (incoming_index == incoming_count) {
        return -EAGAIN;
    }
    memcpy(buf, incoming_reports[incoming_index++], 5);
    return 5;
}

static void incoming(int key, int depth)
{
    int raw = depth / 5;
    incoming_reports[incoming_count][0] = 0x05;
    incoming_reports[incoming_count][1] = 0x1b;
    incoming_reports[incoming_count][2] = raw & 255;
    incoming_reports[incoming_count][3] = raw >> 8;
    incoming_reports[incoming_count++][4] = key;
}

#define as_hid_read mock_hid_read
#define as_mouse_open_path mock_mouse_open_path
#define as_keyboard_open_path mock_keyboard_open_path
#define as_job_run capture_job
#define as_job_run_for_device capture_device_job
#include "../src/gui/as-store.c"
#undef as_hid_read
#undef as_job_run_for_device
#undef as_job_run
#undef as_mouse_open_path
#undef as_keyboard_open_path

static void finish_job(gpointer data, gboolean run)
{
    for (guint i = 0; i < pending_jobs->len; i++) {
        CapturedJob *job = pending_jobs->pdata[i];
        if (job->data != data) {
            continue;
        }
        g_ptr_array_remove_index(pending_jobs, i);
        GError *error = NULL;
        if (run) {
            job->func(job->data, &error);
            if (job->done) {
                job->done(job->data, error, job->user_data);
            }
        }
        g_clear_error(&error);
        if (job->free_data) {
            job->free_data(job->data);
        }
        if (job->free_user_data) {
            job->free_user_data(job->user_data);
        }
        g_free(job);
        return;
    }
    g_assert_not_reached();
}

static void release_job(void)
{
    finish_job(queued_data, FALSE);
    queued_free = NULL;
    queued_data = NULL;
}

static Handle *publish_handle(AsStore *store, AsDevice *dev)
{
    struct as_hid hid = { .vid = dev->vid, .pid = dev->pid };
    g_strlcpy(hid.path, dev->path, sizeof(hid.path));
    Handle *h = handle_new(&hid);
    h->kind = dev->kind;
    g_hash_table_insert(store->handles, g_strdup(dev->path), h);
    return h;
}

static AsDevice *device_new(void)
{
    AsDevice *dev = g_new0(AsDevice, 1);

    dev->kind = AS_DEVICE_KEYBOARD;
    dev->vid = 0x3151;
    dev->pid = 1;
    dev->profile = 2;
    g_strlcpy(dev->path, "/nonexistent/store-state", sizeof(dev->path));
    return dev;
}

static void merge_one(AsStore *store, AsDevice *dev)
{
    GPtrArray *fresh = g_ptr_array_new_with_free_func(g_free);

    g_ptr_array_add(fresh, dev);
    store_merge(store, fresh);
    g_free(g_ptr_array_free(fresh, FALSE));
}

static void test_merge(void)
{
    AsStore *store = g_object_new(AS_TYPE_STORE, NULL);
    AsDevice *old = device_new();
    AsDevice *fresh;

    old->key_travel_valid = 1;
    old->key_actuation[4] = 1.7;
    old->key_option[4] = AS_KB_OPTION_RT;
    old->side_light_mode_saved = 3;
    g_ptr_array_add(store->devices, old);
    merge_one(store, device_new());
    g_assert_false(old->key_travel_valid);
    g_assert_cmpfloat(old->key_actuation[4], ==, 1.7);
    g_assert_cmpint(old->key_option[4], ==, AS_KB_OPTION_RT);
    g_assert_cmpint(old->side_light_mode_saved, ==, 3);
    merge_one(store, device_new());
    g_assert_cmpfloat(old->key_actuation[4], ==, 1.7);
    fresh = device_new();
    fresh->profile++;
    merge_one(store, fresh);
    g_assert_cmpfloat(old->key_actuation[4], ==, 0.0);
    g_assert_cmpint(old->side_light_mode_saved, ==, 0);
    g_object_unref(store);
}

static void test_merge_identity_replacement(void)
{
    for (int change = 0; change < 4; change++) {
        AsStore *store = g_object_new(AS_TYPE_STORE, NULL);
        AsDevice *old = device_new();
        AsDevice *fresh = device_new();
        AsDevice expected;

        old->key_travel_valid = 1;
        old->key_actuation[4] = 1.7;
        old->key_option[4] = AS_KB_OPTION_RT;
        old->key_pair[4] = 5;
        old->side_light_mode_saved = 3;
        old->usb_version_valid = 1;
        old->usb_version = 1280;
        old->revision_valid = 1;
        old->revision = 7;
        if (change == 0) {
            g_strlcpy(fresh->phys, "usb/replacement", sizeof(fresh->phys));
        } else if (change == 1) {
            fresh->vid++;
        } else if (change == 2) {
            fresh->kind = AS_DEVICE_MOUSE;
        } else {
            fresh->pid++;
        }
        expected = *fresh;
        g_ptr_array_add(store->devices, old);
        merge_one(store, fresh);
        g_assert_true(as_store_lookup(store, old->path) == old);
        g_assert_cmpint(old->kind, ==, expected.kind);
        g_assert_cmpuint(old->vid, ==, expected.vid);
        g_assert_cmpuint(old->pid, ==, expected.pid);
        g_assert_cmpstr(old->phys, ==, expected.phys);
        g_assert_false(old->key_travel_valid);
        g_assert_cmpfloat(old->key_actuation[4], ==, 0.0);
        g_assert_cmpint(old->key_option[4], ==, 0);
        g_assert_cmpint(old->key_pair[4], ==, 0);
        g_assert_cmpint(old->side_light_mode_saved, ==, 0);
        g_assert_false(old->usb_version_valid);
        g_assert_false(old->revision_valid);
        g_object_unref(store);
    }
}

static void test_snap_mask(void)
{
    AsDevice dev = { 0 };
    AsOpRequest req = { .op = AS_OP_KB_SNAP, .index = 4, .value = 5 };

    dev.key_option[4] = AS_KB_OPTION_RT | AS_KB_OPTION_FULL_BOTTOM;
    dev.key_option[5] = AS_KB_OPTION_RT | AS_KB_OPTION_FULL_BOTTOM;
    apply_optimistic(&dev, &req);
    g_assert_false(dev.key_option[4] & AS_KB_OPTION_FULL_BOTTOM);
    g_assert_false(dev.key_option[5] & AS_KB_OPTION_FULL_BOTTOM);
    g_assert_true(dev.key_option[4] & AS_KB_OPTION_RT);
    req.snap_full_bottom = 1;
    apply_optimistic(&dev, &req);
    req.snap_disable = 1;
    apply_optimistic(&dev, &req);
    g_assert_cmpint(dev.key_option[4], ==, AS_KB_OPTION_RT);
    g_assert_cmpint(dev.key_option[5], ==, AS_KB_OPTION_RT);
}

static void test_stream_pause(void)
{
    AsStore *store = g_object_new(AS_TYPE_STORE, NULL);
    AnalogJob stale = { .store = store, .path = "/nonexistent/store-state",
                        .fd = -1 };
    GError *error = NULL;
    guint before;

    AsDevice *dev = device_new();
    g_ptr_array_add(store->devices, dev);
    Handle *h = publish_handle(store, dev);
    config_begin(store, h);
    config_begin(store, h);
    before = queued;
    g_assert_true(as_store_analog_start(store, stale.path));
    g_assert_cmpuint(queued, ==, before);
    g_assert_true(store->analog_running);
    config_end(store, h);
    g_assert_cmpuint(queued, ==, before);
    config_end(store, h);
    g_assert_cmpuint(queued, ==, before + 1);
    g_assert_true(queued_func == analog_job_run);
    g_assert_cmpstr(queued_path, ==, dev->path);
    g_assert_cmpint(((AnalogJob *)queued_data)->generation, ==,
                    store->analog_generation);
    release_job();
    config_begin(store, h);
    as_store_analog_stop(store);
    release_job();
    before = queued;
    config_end(store, h);
    g_assert_cmpuint(queued, ==, before);
    analog_job_run(&stale, &error);
    g_assert_no_error(error);
    g_object_unref(store);
}

static void test_shutdown_completion(void)
{
    AsStore *store = g_object_new(AS_TYPE_STORE, NULL);
    RefreshJob refresh = { .store = store };
    ApplyJob apply = { .store = store };
    guint before = queued;

    store->analog_running = TRUE;
    as_store_shutdown(store);
    refresh_job_done(&refresh, NULL, NULL);
    apply_job_done(&apply, NULL, NULL);
    g_assert_cmpuint(queued, ==, before);
    g_assert_false(as_store_analog_start(store, "/nonexistent/store-state"));
    as_store_scan(store);
    g_assert_cmpuint(queued, ==, before);
    g_object_unref(store);
}

static void test_operation_kind(void)
{
    AsStore *store = g_object_new(AS_TYPE_STORE, NULL);
    AsDevice *dev = device_new();
    AsOpRequest req = { .op = AS_OP_MOUSE_RATE };
    guint before = queued;

    g_ptr_array_add(store->devices, dev);
    g_assert_false(as_store_apply(store, dev->path, &req));
    req.op = -1;
    g_assert_false(as_store_apply(store, dev->path, &req));
    req.op = AS_OP_KB_SIDE_LIGHT + 1;
    g_assert_false(as_store_apply(store, dev->path, &req));
    g_assert_cmpuint(queued, ==, before);
    g_assert_true(operation_matches_kind(AS_OP_KB_RATE, dev->kind));
    g_object_unref(store);
}

static void test_cross_device(void)
{
    AsStore *store = g_object_new(AS_TYPE_STORE, NULL);
    AsDevice *kb = device_new();
    AsDevice *mouse = device_new();
    AsOpRequest req = { .op = AS_OP_MOUSE_RATE, .value = 500 };
    g_strlcpy(mouse->path, "/nonexistent/mouse", sizeof(mouse->path));
    mouse->kind = AS_DEVICE_MOUSE;
    g_ptr_array_add(store->devices, kb);
    g_ptr_array_add(store->devices, mouse);
    Handle *kh = publish_handle(store, kb);
    Handle *mh = publish_handle(store, mouse);
    g_assert_true(as_store_analog_start(store, kb->path));
    release_job();
    int generation = store->analog_generation;
    g_assert_true(as_store_apply(store, mouse->path, &req));
    ApplyJob *write = queued_data;
    g_assert_cmpstr(queued_path, ==, mouse->path);
    g_assert_cmpint(store->analog_generation, ==, generation);
    g_assert_cmpuint(kh->config_pending, ==, 0);
    store_request(store, kb->path);
    RefreshJob *read = queued_data;
    g_assert_true(queued_func == refresh_job_run);
    g_assert_cmpstr(queued_path, ==, kb->path);
    g_assert_cmpuint(kh->config_pending, ==, 1);
    finish_job(read, TRUE);
    g_assert_cmpuint(kh->config_pending, ==, 0);
    g_assert_cmpuint(mh->config_pending, ==, 1);
    g_assert_true(queued_func == analog_job_run);
    g_assert_cmpuint(store->devices->len, ==, 2);
    release_job();
    finish_job(write, TRUE);
    g_assert_cmpuint(mh->config_pending, ==, 0);
    as_store_analog_stop(store);
    release_job();
    as_store_shutdown(store);
    g_object_unref(store);
}

static void record_completion(AsStore *store, const char *path, int op,
                              guint64 id, gboolean success, gpointer data)
{
    (void)store;
    (void)path;
    (void)op;
    (void)id;
    *(int *)data = success ? 1 : -1;
}

static void test_stale_apply(void)
{
    AsStore *store = g_object_new(AS_TYPE_STORE, NULL);
    AsDevice *dev = device_new();
    AsOpRequest req = { .op = AS_OP_KB_RATE, .value = 8000 };
    int completion = 0;
    g_ptr_array_add(store->devices, dev);
    Handle *old = publish_handle(store, dev);
    g_signal_connect(store, "operation-completed", G_CALLBACK(record_completion),
                     &completion);
    g_assert_true(as_store_apply(store, dev->path, &req));
    ApplyJob *job = queued_data;
    Handle *replacement = publish_handle(store, dev);
    dev->rate = 1000;
    store->analog_running = TRUE;
    g_strlcpy(store->analog_path, dev->path, sizeof(store->analog_path));
    guint before = queued;
    apply_job_done(job, NULL, NULL);
    g_assert_cmpint(completion, ==, -1);
    g_assert_cmpint(dev->rate, ==, 1000);
    g_assert_cmpuint(old->config_pending, ==, 0);
    g_assert_cmpuint(replacement->settle_id, ==, 0);
    g_assert_cmpuint(old->settle_id, ==, 0);
    g_assert_cmpuint(queued, ==, before);
    release_job();
    as_store_shutdown(store);
    g_object_unref(store);
}

static void count_error(AsStore *store, const char *path, const char *message,
                        gpointer data)
{
    (void)store;
    (void)path;
    (void)message;
    (*(guint *)data)++;
}

static void test_retired_execution(void)
{
    AsStore *store = g_object_new(AS_TYPE_STORE, NULL);
    AsDevice *dev = device_new();
    AsOpRequest req = { .op = AS_OP_KB_RATE, .value = 8000 };
    guint errors = 0;
    GError *error = NULL;
    g_ptr_array_add(store->devices, dev);
    Handle *old = publish_handle(store, dev);
    g_assert_true(as_store_apply(store, dev->path, &req));
    ApplyJob *write = queued_data;
    store_request(store, dev->path);
    RefreshJob *read = queued_data;
    g_atomic_int_set(&old->retired, TRUE);
    publish_handle(store, dev);
    guint before = keyboard_open_calls;
    apply_job_run(write, &error);
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
    g_clear_error(&error);
    refresh_job_run(read, &error);
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
    g_signal_connect(store, "error", G_CALLBACK(count_error), &errors);
    refresh_job_done(read, error, NULL);
    g_assert_cmpuint(errors, ==, 0);
    g_assert_cmpuint(keyboard_open_calls, ==, before);
    g_assert_cmpint(handle_open(old), ==, -ENODEV);
    g_assert_cmpuint(keyboard_open_calls, ==, before);
    g_clear_error(&error);
    finish_job(read, FALSE);
    finish_job(write, FALSE);
    Handle *current = g_hash_table_lookup(store->handles, dev->path);
    keyboard_identity_mismatch = TRUE;
    g_assert_cmpint(handle_open(current), ==, -ENODEV);
    g_assert_false(current->opened);
    keyboard_identity_mismatch = FALSE;
    as_store_shutdown(store);
    g_object_unref(store);
}

static void test_revision_isolation(void)
{
    AsStore *store = g_object_new(AS_TYPE_STORE, NULL);
    AsDevice *kb = device_new();
    AsDevice *mouse = device_new();
    g_strlcpy(mouse->path, "/nonexistent/mouse", sizeof(mouse->path));
    mouse->kind = AS_DEVICE_MOUSE;
    g_ptr_array_add(store->devices, kb);
    g_ptr_array_add(store->devices, mouse);
    Handle *kh = publish_handle(store, kb);
    Handle *mh = publish_handle(store, mouse);
    store_request(store, kb->path);
    RefreshJob *read = queued_data;
    read->devices = g_ptr_array_new_with_free_func(g_free);
    AsDevice *fresh = device_new();
    fresh->rate = 2000;
    g_ptr_array_add(read->devices, fresh);
    mh->revision++;
    refresh_job_done(read, NULL, NULL);
    g_assert_cmpint(kb->rate, ==, 2000);
    g_assert_cmpuint(store->devices->len, ==, 2);
    release_job();
    store_request(store, kb->path);
    read = queued_data;
    read->devices = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(read->devices, device_new());
    kh->revision++;
    refresh_job_done(read, NULL, NULL);
    g_assert_cmpint(kb->rate, ==, 2000);
    g_assert_true(queued_data != read);
    release_job();
    finish_job(read, FALSE);
    as_store_shutdown(store);
    g_object_unref(store);
}

static void test_enumeration_publication(void)
{
    AsStore *store = g_object_new(AS_TYPE_STORE, NULL);
    RefreshJob scan = { .store = store, .count = 1 };
    AsDevice *dev = device_new();
    scan.found[0].vid = dev->vid;
    scan.found[0].pid = dev->pid;
    g_strlcpy(scan.found[0].path, dev->path, sizeof(scan.found[0].path));
    g_free(dev);
    scan.found[0].vid = 0x3151;
    scan.found[0].pid = 0x5030;
    if (classify(&scan.found[0]) < 0) {
        scan.found[0].vid = 0x258a;
        scan.found[0].pid = 0x0027;
    }
    g_assert_cmpint(classify(&scan.found[0]), >=, 0);
    refresh_job_done(&scan, NULL, NULL);
    g_assert_cmpuint(g_hash_table_size(store->handles), ==, 1);
    g_assert_cmpuint(store->devices->len, ==, 1);
    RefreshJob *read = queued_data;
    Handle *h = read->handle;
    g_assert_cmpint(g_atomic_int_get(&h->refs), ==, 2);
    scan.count = 0;
    refresh_job_done(&scan, NULL, NULL);
    g_assert_cmpuint(store->devices->len, ==, 0);
    g_assert_cmpint(g_atomic_int_get(&h->refs), ==, 1);
    finish_job(read, TRUE);
    g_assert_cmpuint(store->devices->len, ==, 0);
    as_store_shutdown(store);
    g_object_unref(store);
}

static void test_active_scan(void)
{
    AsStore *store = g_object_new(AS_TYPE_STORE, NULL);
    RefreshJob scan = { .store = store, .count = 1 };
    scan.found[0].vid = 0x3151;
    scan.found[0].pid = 0x5030;
    g_strlcpy(scan.found[0].path, "/nonexistent/active-scan",
              sizeof(scan.found[0].path));
    g_assert_cmpint(classify(&scan.found[0]), >=, 0);
    refresh_job_done(&scan, NULL, NULL);
    release_job();
    Handle *h = g_hash_table_lookup(store->handles, scan.found[0].path);
    h->refreshing = FALSE;
    h->config_pending = 0;
    store->analog_running = TRUE;
    g_strlcpy(store->analog_path, h->path, sizeof(store->analog_path));
    store->analog_fd = open("/dev/null", O_RDONLY);
    int fd = store->analog_fd;
    int generation = store->analog_generation;
    guint before = queued;
    refresh_job_done(&scan, NULL, NULL);
    g_assert_cmpuint(queued, ==, before);
    g_assert_cmpint(store->analog_fd, ==, fd);
    g_assert_cmpint(fcntl(fd, F_GETFD), >=, 0);
    g_assert_cmpint(store->analog_generation, ==, generation);
    g_assert_cmpuint(h->config_pending, ==, 0);
    g_assert_true(store->analog_running);
    scan.count = 0;
    refresh_job_done(&scan, NULL, NULL);
    g_assert_false(store->analog_running);
    g_assert_cmpint(store->analog_fd, ==, -1);
    g_assert_cmpuint(store->devices->len, ==, 0);
    release_job();
    as_store_shutdown(store);
    g_object_unref(store);
}

static void test_analog_freshness(void)
{
    AsStore *store = g_object_new(AS_TYPE_STORE, NULL);
    AsDevice *dev = device_new();
    g_ptr_array_add(store->devices, dev);
    Handle *h = publish_handle(store, dev);
    h->config_pending = 1;
    g_assert_true(as_store_analog_start(store, dev->path));
    store->analog_fd = open("/dev/null", O_RDONLY);
    g_assert_cmpint(store->analog_fd, >=, 0);
    incoming(9, 2000);
    incoming(1, 1500);
    incoming(9, 1995);
    incoming(1, 1500);
    analog_tick(store);
    guint64 a = as_store_travel_sequence(store, dev->path, 9);
    guint64 b = as_store_travel_sequence(store, dev->path, 1);
    g_assert_cmpuint(a, >, b);
    g_assert_cmpuint(b, >, 0);
    g_assert_cmpint(as_store_travel(store, dev->path, 9), ==, 1995);
    incoming_count = incoming_index = 0;
    incoming(1, 1500);
    incoming(9, 1995);
    incoming(2, 0);
    analog_tick(store);
    g_assert_cmpuint(as_store_travel_sequence(store, dev->path, 9), ==, a);
    g_assert_cmpuint(as_store_travel_sequence(store, dev->path, 1), ==, b);
    g_assert_cmpuint(as_store_travel_sequence(store, dev->path, 2), ==, 0);
    incoming_count = incoming_index = 0;
    incoming(9, 0);
    analog_tick(store);
    g_assert_cmpuint(as_store_travel_sequence(store, dev->path, 9), >, a);
    g_assert_cmpint(as_store_travel(store, dev->path, 9), ==, 0);
    config_begin(store, h);
    g_assert_cmpuint(as_store_travel_sequence(store, dev->path, 1), ==, b);
    as_store_analog_stop(store);
    g_assert_cmpuint(as_store_travel_sequence(store, dev->path, 1), ==, 0);
    release_job();
    g_assert_true(as_store_analog_start(store, dev->path));
    g_assert_cmpuint(as_store_travel_sequence(store, dev->path, 1), ==, 0);
    store->analog_fd = open("/dev/null", O_RDONLY);
    incoming_count = incoming_index = 0;
    incoming(1, 1500);
    analog_tick(store);
    g_assert_cmpuint(as_store_travel_sequence(store, dev->path, 1), >, a);
    as_store_analog_stop(store);
    release_job();
    as_store_shutdown(store);
    g_object_unref(store);
    incoming_count = incoming_index = 0;
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/store/analog-freshness", test_analog_freshness);
    g_test_add_func("/store/active-scan", test_active_scan);
    g_test_add_func("/store/retired-execution", test_retired_execution);
    g_test_add_func("/store/stale-apply", test_stale_apply);
    g_test_add_func("/store/cross-device", test_cross_device);
    g_test_add_func("/store/revision-isolation", test_revision_isolation);
    g_test_add_func("/store/enumeration-publication", test_enumeration_publication);
    g_test_add_func("/store/merge", test_merge);
    g_test_add_func("/store/merge-identity-replacement",
                    test_merge_identity_replacement);
    g_test_add_func("/store/snap-mask", test_snap_mask);
    g_test_add_func("/store/stream-pause", test_stream_pause);
    g_test_add_func("/store/shutdown-completion", test_shutdown_completion);
    g_test_add_func("/store/operation-kind", test_operation_kind);
    return g_test_run();
}
