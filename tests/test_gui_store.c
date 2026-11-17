/* SPDX-License-Identifier: MIT
 *
 * Smoke test for the GUI data path: enumeration, state read and a harmless
 * write round-trip. Needs real hardware; without devices it exits 0 and says
 * so, which keeps it usable in a build container.
 */
#include <glib.h>
#include <stdio.h>

#include "as-store.h"
#include "as-worker.h"

static GMainLoop *loop;
static AsStore *store;
static GHashTable *expected_ids;
static GHashTable *completed_ids;
static guint expected_completions;
static guint completed_completions;
static guint successful_completions;
static guint failed_completions;
static guint64 next_request_id;
static int errors;
static gboolean scan_seen;
static gboolean timed_out;

static gboolean on_timeout(gpointer data)
{
    (void)data;
    timed_out = TRUE;
    fprintf(stderr, "test_gui_store: timeout waiting for %u/%u write completion(s)\n",
            completed_completions, expected_completions);
    g_main_loop_quit(loop);
    return G_SOURCE_REMOVE;
}

static void on_store_error(AsStore *self, const char *path, const char *message,
                           gpointer data)
{
    (void)self;
    (void)data;
    fprintf(stderr, "error for %s: %s\n", path ? path : "?", message);
    errors++;
}

static void on_operation_completed(AsStore *self, const char *path, int op,
                                   guint64 request_id, gboolean success,
                                   gpointer data)
{
    (void)self;
    (void)path;
    (void)op;
    (void)data;

    if (!g_hash_table_contains(expected_ids, &request_id) ||
        g_hash_table_contains(completed_ids, &request_id)) {
        return;
    }
    g_hash_table_add(completed_ids, g_memdup2(&request_id, sizeof(request_id)));
    completed_completions++;
    if (success) {
        successful_completions++;
    } else {
        failed_completions++;
    }
    if (completed_completions == expected_completions) {
        g_main_loop_quit(loop);
    }
}

static gboolean write_back(AsDevice *dev)
{
    AsOpRequest req = { 0 };
    guint64 *id;

    if (dev->error[0] || dev->rate <= 0) {
        return FALSE;
    }
    req.op = dev->kind == AS_DEVICE_MOUSE ? AS_OP_MOUSE_RATE : AS_OP_KB_RATE;
    req.value = dev->rate;
    req.request_id = ++next_request_id;
    if (!as_store_apply(store, dev->path, &req)) {
        return FALSE;
    }
    id = g_memdup2(&req.request_id, sizeof(req.request_id));
    g_hash_table_add(expected_ids, id);
    expected_completions++;
    return TRUE;
}

static void on_changed(AsStore *self, gpointer data)
{
    GPtrArray *devs = as_store_devices(self);

    (void)data;
    if (scan_seen) {
        return;
    }
    scan_seen = TRUE;
    for (guint i = 0; i < devs->len; i++) {
        AsDevice *dev = devs->pdata[i];

        if (dev->kind == AS_DEVICE_MOUSE) {
            printf("mouse    %s  %04x:%04x  link %s  battery %d%%  rate %d  "
                   "dpi stages %d\n",
                   dev->path, dev->vid, dev->pid,
                   dev->online ? "online" : "offline", dev->battery, dev->rate,
                   dev->dpi_stages);
        } else {
            printf("keyboard %s  %04x:%04x  fw %u  rate %d  profile %d  "
                   "side light %s\n",
                   dev->path, dev->vid, dev->pid, dev->usb_version, dev->rate,
                   dev->profile, dev->side_light_valid ? "yes" : "no");
        }
        if (dev->error[0]) {
            printf("         note: %s\n", dev->error);
        }
    }

    if (devs->len == 0) {
        printf("no devices found - nothing to test\n");
        g_main_loop_quit(loop);
        return;
    }

    for (guint i = 0; i < devs->len; i++) {
        write_back(devs->pdata[i]);
    }
    printf("queued %u write(s)\n", expected_completions);
    if (expected_completions == 0) {
        g_main_loop_quit(loop);
    }
}

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    loop = g_main_loop_new(NULL, FALSE);
    expected_ids = g_hash_table_new_full(g_int64_hash, g_int64_equal, g_free, NULL);
    completed_ids = g_hash_table_new_full(g_int64_hash, g_int64_equal, g_free, NULL);
    as_worker_start();
    store = as_store_get();
    g_signal_connect(store, "changed", G_CALLBACK(on_changed), NULL);
    g_signal_connect(store, "error", G_CALLBACK(on_store_error), NULL);
    g_signal_connect(store, "operation-completed",
                     G_CALLBACK(on_operation_completed), NULL);
    g_timeout_add_seconds(30, on_timeout, NULL);

    as_store_scan(store);
    g_main_loop_run(loop);

    as_worker_stop();
    as_store_shutdown(store);
    g_main_loop_unref(loop);
    g_hash_table_unref(expected_ids);
    g_hash_table_unref(completed_ids);

    printf("completions: %u success, %u failure; errors: %d\n",
           successful_completions, failed_completions, errors);
    return timed_out || failed_completions || errors ? 1 : 0;
}
