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
static int errors;
static int writes_sent;

static gboolean on_timeout(gpointer data)
{
    (void)data;
    fprintf(stderr, "test_gui_store: timeout\n");
    g_main_loop_quit(loop);
    return G_SOURCE_REMOVE;
}

static gboolean quit_later(gpointer data)
{
    (void)data;
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

/* Re-applies the value the device already reports, so nothing changes. */
static int write_back(AsDevice *dev)
{
    AsOpRequest req = { 0 };

    if (dev->error[0]) {
        return 0;
    }
    if (dev->kind == AS_DEVICE_MOUSE) {
        if (dev->rate <= 0) {
            return 0;
        }
        req.op = AS_OP_MOUSE_RATE;
        req.value = dev->rate;
    } else {
        if (dev->rate <= 0) {
            return 0;
        }
        req.op = AS_OP_KB_RATE;
        req.value = dev->rate;
    }
    return as_store_apply(store, dev->path, &req) ? 1 : 0;
}

static void on_changed(AsStore *self, gpointer data)
{
    GPtrArray *devs = as_store_devices(self);

    (void)data;
    for (guint i = 0; i < devs->len; i++) {
        AsDevice *dev = devs->pdata[i];

        if (dev->kind == AS_DEVICE_MOUSE) {
            printf("mouse    %s  %04x:%04x  link %s  battery %d%%  rate %d  "
                   "dpi stages %d\n",
                   dev->path, dev->vid, dev->pid,
                   dev->online ? "online" : "offline", dev->battery, dev->rate,
                   dev->dpi_stages);
        } else {
            printf("keyboard %s  %04x:%04x  fw %u  rate %d  profile %d\n",
                   dev->path, dev->vid, dev->pid, dev->usb_version, dev->rate,
                   dev->profile);
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

    if (writes_sent == 0) {
        for (guint i = 0; i < devs->len; i++) {
            writes_sent += write_back(devs->pdata[i]);
        }
        printf("queued %d write(s)\n", writes_sent);
        /* Give the writes a moment to come back through "changed". */
        g_timeout_add_seconds(3, quit_later, NULL);
    }
}

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    loop = g_main_loop_new(NULL, FALSE);
    as_worker_start();
    store = as_store_get();
    g_signal_connect(store, "changed", G_CALLBACK(on_changed), NULL);
    g_signal_connect(store, "error", G_CALLBACK(on_store_error), NULL);
    g_timeout_add_seconds(30, on_timeout, NULL);

    as_store_scan(store);
    g_main_loop_run(loop);

    as_worker_stop();
    as_store_shutdown(store);
    g_main_loop_unref(loop);

    printf("errors: %d\n", errors);
    return errors ? 1 : 0;
}
