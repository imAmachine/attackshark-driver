#define _GNU_SOURCE
#include "as-store.h"
#include "as-worker.h"
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

static GMutex gate;
static GCond wake;
static gboolean mouse_entered;
static gboolean mouse_release;
static gint enumeration_mode;
static gint keyboard_reads;
static gint keyboard_writes;
static gint stream_starts;
static gint stream_stops;
static gint mouse_closes;
static gint keyboard_rate = 1000;
static int sample_pipe[2];

static int mock_list(struct as_hid *out, int max)
{
    int mode = g_atomic_int_get(&enumeration_mode);
    g_assert_cmpint(max, >=, 2);
    memset(out, 0, 2 * sizeof(*out));
    out[0].vid = 0x3151;
    out[0].pid = 1;
    g_strlcpy(out[0].path, "/mock/keyboard", sizeof(out[0].path));
    out[1].vid = 0x3554;
    out[1].pid = mode == 2 ? 2 : 1;
    g_strlcpy(out[1].path, "/mock/mouse", sizeof(out[1].path));
    return mode == 1 ? 1 : 2;
}

static int mock_mouse_open(struct as_mouse *mouse, const char *path)
{
    (void)mouse;
    (void)path;
    return 0;
}

static void mock_mouse_close(struct as_mouse *mouse)
{
    (void)mouse;
    g_atomic_int_inc(&mouse_closes);
}

static int mock_mouse_read(struct as_mouse *mouse, struct as_mouse_state *st)
{
    (void)mouse;
    g_mutex_lock(&gate);
    mouse_entered = TRUE;
    g_cond_broadcast(&wake);
    while (!mouse_release) {
        g_cond_wait(&wake, &gate);
    }
    g_mutex_unlock(&gate);
    memset(st, 0, sizeof(*st));
    st->config_valid = 1;
    st->online = 1;
    st->report_rate = 125;
    return 0;
}

static int mock_keyboard_open(struct as_keyboard *kb, const char *path)
{
    (void)path;
    kb->hid.fd = -1;
    kb->analog.fd = -1;
    return 0;
}

static int mock_keyboard_read(struct as_keyboard *kb, struct as_kb_state *st)
{
    (void)kb;
    memset(st, 0, sizeof(*st));
    st->profile = -1;
    st->mag_mode = -1;
    st->report_rate = g_atomic_int_get(&keyboard_rate);
    g_atomic_int_inc(&keyboard_reads);
    return 0;
}

static int mock_keyboard_write(struct as_keyboard *kb, int rate)
{
    (void)kb;
    g_atomic_int_set(&keyboard_rate, rate);
    g_atomic_int_inc(&keyboard_writes);
    return 0;
}

static int mock_stream(struct as_keyboard *kb, int on)
{
    (void)kb;
    if (on) {
        g_atomic_int_inc(&stream_starts);
    } else {
        g_atomic_int_inc(&stream_stops);
    }
    return 0;
}

static int mock_analog_open(struct as_keyboard *kb)
{
    kb->analog.fd = dup(sample_pipe[0]);
    g_assert_cmpint(kb->analog.fd, >=, 0);
    kb->analog_open = 1;
    return 0;
}

static int mock_read(struct as_hid *hid, uint8_t *buf, size_t size, int timeout)
{
    (void)timeout;
    int n = read(hid->fd, buf, size);
    return n < 0 ? -errno : n;
}

static int mock_parse(const uint8_t *buf, size_t size, int32_t *values)
{
    if (!size) {
        return 0;
    }
    values[4] = buf[size - 1];
    return 1;
}

#define as_hid_list_devices mock_list
#define as_mouse_open_path mock_mouse_open
#define as_mouse_close mock_mouse_close
#define as_mouse_read_state mock_mouse_read
#define as_keyboard_open_path mock_keyboard_open
#define as_keyboard_read_state mock_keyboard_read
#define as_keyboard_set_report_rate mock_keyboard_write
#define as_keyboard_enable_stream mock_stream
#define as_keyboard_open_analog mock_analog_open
#define as_hid_read mock_read
#define as_kb_parse_analog mock_parse
#include "../src/gui/as-store.c"

static gboolean completed;
static guint analog_signals;

static void on_completed(AsStore *store, const char *path, int op, guint64 id,
                         gboolean success, gpointer data)
{
    (void)store;
    (void)path;
    (void)op;
    (void)data;
    g_assert_cmpuint(id, ==, 77);
    g_assert_true(success);
    completed = TRUE;
}

static void on_analog(AsStore *store, gpointer data)
{
    (void)store;
    (void)data;
    analog_signals++;
}

#define WAIT_FOR(expression) do { \
    gint64 deadline = g_get_monotonic_time() + 8 * G_TIME_SPAN_SECOND; \
    while (!(expression) && g_get_monotonic_time() < deadline) { \
        g_main_context_iteration(NULL, FALSE); \
        g_usleep(1000); \
    } \
    g_assert_true(expression); \
} while (0)

static gboolean mouse_blocked(void)
{
    gboolean result;
    g_mutex_lock(&gate);
    result = mouse_entered && !mouse_release;
    g_mutex_unlock(&gate);
    return result;
}

static void send_sample(AsStore *store, uint8_t value)
{
    guint before = analog_signals;
    g_assert_cmpint(write(sample_pipe[1], &value, 1), ==, 1);
    WAIT_FOR(analog_signals > before);
    g_assert_cmpint(as_store_travel(store, "/mock/keyboard", 4), ==, value);
}

static void test_threaded_isolation(void)
{
    AsStore *store = g_object_new(AS_TYPE_STORE, NULL);
    AsOpRequest req = { .op = AS_OP_KB_RATE, .value = 2000, .request_id = 77 };
    g_assert_cmpint(pipe2(sample_pipe, O_NONBLOCK | O_CLOEXEC), ==, 0);
    as_worker_start();
    g_signal_connect(store, "operation-completed", G_CALLBACK(on_completed), NULL);
    g_signal_connect(store, "analog", G_CALLBACK(on_analog), NULL);
    as_store_scan(store);
    WAIT_FOR(mouse_blocked());
    WAIT_FOR(g_atomic_int_get(&keyboard_reads) >= 1);
    WAIT_FOR(!as_store_busy(store));
    WAIT_FOR(!((Handle *)g_hash_table_lookup(store->handles, "/mock/keyboard"))->refreshing);
    g_assert_true(as_store_analog_start(store, "/mock/keyboard"));
    WAIT_FOR(store->analog_fd >= 0);
    send_sample(store, 11);
    g_assert_true(mouse_blocked());
    g_assert_true(as_store_apply(store, "/mock/keyboard", &req));
    WAIT_FOR(completed);
    WAIT_FOR(store->analog_fd >= 0);
    g_assert_cmpint(g_atomic_int_get(&keyboard_writes), ==, 1);
    send_sample(store, 22);
    int reads_before = g_atomic_int_get(&keyboard_reads);
    WAIT_FOR(g_atomic_int_get(&keyboard_reads) > reads_before);
    WAIT_FOR(store->analog_fd >= 0);
    g_assert_cmpint(as_store_lookup(store, "/mock/keyboard")->rate, ==, 2000);
    send_sample(store, 33);
    g_assert_true(mouse_blocked());
    Handle *old = handle_ref(g_hash_table_lookup(store->handles, "/mock/mouse"));
    g_atomic_int_set(&enumeration_mode, 2);
    as_store_scan(store);
    WAIT_FOR(g_hash_table_lookup(store->handles, "/mock/mouse") != old);
    g_assert_cmpuint(as_store_lookup(store, "/mock/mouse")->pid, ==, 2);
    g_atomic_int_set(&enumeration_mode, 1);
    as_store_scan(store);
    WAIT_FOR(as_store_lookup(store, "/mock/mouse") == NULL);
    WAIT_FOR(store->analog_fd >= 0);
    send_sample(store, 44);
    g_assert_true(mouse_blocked());
    g_assert_cmpint(g_atomic_int_get(&mouse_closes), ==, 0);
    g_mutex_lock(&gate);
    mouse_release = TRUE;
    g_cond_broadcast(&wake);
    g_mutex_unlock(&gate);
    WAIT_FOR(g_atomic_int_get(&old->refs) == 1);
    handle_destroy(old);
    WAIT_FOR(g_atomic_int_get(&mouse_closes) == 1);
    g_assert_null(as_store_lookup(store, "/mock/mouse"));
    g_assert_cmpuint(as_store_devices(store)->len, ==, 1);
    g_assert_cmpint(g_atomic_int_get(&stream_starts), >=, 3);
    g_assert_cmpint(g_atomic_int_get(&stream_stops), >=, 2);
    as_store_analog_stop(store);
    as_worker_stop();
    as_store_shutdown(store);
    while (g_main_context_iteration(NULL, FALSE)) {}
    g_object_unref(store);
    close(sample_pipe[0]);
    close(sample_pipe[1]);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/store/threads/blocked-mouse", test_threaded_isolation);
    return g_test_run();
}
