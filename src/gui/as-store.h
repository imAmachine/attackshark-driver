/* SPDX-License-Identifier: MIT
 *
 * Device registry for the GUI. All hidraw access happens on the worker thread;
 * the main thread only ever sees immutable snapshots that are replaced as a
 * whole, so widgets can read them without locking.
 */
#ifndef AS_STORE_H
#define AS_STORE_H

#include <glib-object.h>
#include <stdint.h>

#include "attackshark.h"

G_BEGIN_DECLS

#define AS_TYPE_STORE (as_store_get_type())
G_DECLARE_FINAL_TYPE(AsStore, as_store, AS, STORE, GObject)

typedef enum {
    AS_DEVICE_MOUSE,
    AS_DEVICE_KEYBOARD,
} AsDeviceKind;

typedef struct {
    AsDeviceKind kind;
    char path[64];
    char product[64];
    unsigned vid;
    unsigned pid;
    int iface;
    char error[160];

    /* mouse */
    int online;
    int config_valid;
    int charging;
    int battery;
    int voltage;
    int rate;
    int dpi_stages;
    int active_stage;
    unsigned dpi[AS_MAX_DPI_STAGES];
    int lod;
    int debounce;
    int motion_sync;
    int angle_snap;
    int ripple;
    int sleep_time;
    int profile;
    int fw_valid;
    int fw_major;
    int fw_minor;
    int dongle_valid;
    int dongle[3];
    int light_mode;
    int light_brightness;
    int light_speed;
    int light_on;
    uint8_t rgb[3];

    /* keyboard */
    int usb_version_valid;
    uint32_t usb_version;
    int revision_valid;
    int revision;
    int mag_mode;
    int light_opt;
    int light_dazzle;
    int light_on_known;   /* the hardware does not always report the switch */
} AsDevice;

/* Settings the GUI can write. Everything is a single call into the driver. */
enum {
    AS_OP_MOUSE_RATE,
    AS_OP_MOUSE_DPI_STAGE,
    AS_OP_MOUSE_LOD,
    AS_OP_MOUSE_DEBOUNCE,
    AS_OP_MOUSE_MOTION_SYNC,
    AS_OP_MOUSE_ANGLE_SNAP,
    AS_OP_MOUSE_RIPPLE,
    AS_OP_MOUSE_SLEEP,
    AS_OP_MOUSE_LIGHT,
    AS_OP_KB_RATE,
    AS_OP_KB_PROFILE,
    AS_OP_KB_DEBOUNCE,
    AS_OP_KB_MAG_MODE,
    AS_OP_KB_LIGHT,
    AS_OP_KB_LIGHT_POWER,
};

typedef struct {
    int op;
    int index;              /* dpi stage, 0-based */
    int value;
    uint8_t rgb[3];
    int light_mode;
    int light_speed;
    int light_brightness;
    int light_on;
    int light_option;
    int light_dazzle;
} AsOpRequest;

AsStore *as_store_get(void);
/* Runs in the worker thread; the UI gets "changed" when the data is ready. */
void as_store_scan(AsStore *self);
void as_store_refresh(AsStore *self, const char *path);
gboolean as_store_busy(AsStore *self);

GPtrArray *as_store_devices(AsStore *self);
AsDevice *as_store_lookup(AsStore *self, const char *path);
guint as_store_count(AsStore *self);

/* Queues a write. Returns FALSE when the device is unknown. */
gboolean as_store_apply(AsStore *self, const char *path, const AsOpRequest *req);
/* Closes every handle; call after as_worker_stop(). */
void as_store_shutdown(AsStore *self);

G_END_DECLS

#endif
