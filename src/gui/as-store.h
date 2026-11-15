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
    char error[160];
    /* The node exists but this user may not open it: installing the udev
     * rules is the fix, so the window offers it instead of a dead end. */
    int permission_denied;

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
    int profile;
    int fw_valid;
    int fw_major;
    int fw_minor;
    int dongle_valid;
    int dongle[3];

    /* keyboard */
    int usb_version_valid;
    uint32_t usb_version;
    int revision_valid;
    int revision;
    int mag_mode;
    int rapid_trigger;
    double actuation;
    double release;
    double rt_press;
    double rt_release;
    double deadzone;
    /* Every matrix position, so the page can configure one key at a time. */
    int key_code_valid;
    uint32_t key_code[AS_KB_KEY_MAX];
    uint8_t key_option[AS_KB_KEY_MAX];
    uint8_t key_pair[AS_KB_KEY_MAX];
    double key_actuation[AS_KB_KEY_MAX];
    double key_release[AS_KB_KEY_MAX];
    double key_rt_press[AS_KB_KEY_MAX];
    double key_rt_release[AS_KB_KEY_MAX];
    double key_deadzone[AS_KB_KEY_MAX];
    int light_valid;
    int light_on;
    int light_mode;
    int light_speed;
    int light_brightness;
    int light_dazzle;
    uint8_t light_rgb[3];
    int side_light_valid;
    int side_light_mode;
    int side_light_speed;
    int side_light_brightness;
    int side_light_dazzle;
    int side_light_mode_saved;   /* last non-zero mode, restored by the switch */
    uint8_t side_rgb[3];
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
    AS_OP_KB_TRAVEL,
    AS_OP_KB_KEY_TRAVEL,
    AS_OP_KB_KEYS_TRAVEL,
    AS_OP_KB_SNAP,
    AS_OP_KB_LIGHT,
    AS_OP_KB_LIGHT_POWER,
    AS_OP_KB_SIDE_LIGHT,
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
    double actuation;
    double release;
    double rt_press;
    double rt_release;
    double deadzone;
    int rapid_trigger;
    /* AS_OP_KB_KEYS_TRAVEL: matrix positions to change, one byte each. */
    uint8_t keys[AS_KB_KEY_MAX];
    /* AS_OP_KB_SNAP: index is the first key, value the second, snap_mode one
     * of as_kb_snap_mode. snap_disable only clears the pair. */
    int snap_mode;
    int snap_full_bottom;
    int snap_disable;
} AsOpRequest;

AsStore *as_store_get(void);
/* Runs in the worker thread; the UI gets "changed" when the data is ready. */
void as_store_scan(AsStore *self);
gboolean as_store_busy(AsStore *self);

GPtrArray *as_store_devices(AsStore *self);
AsDevice *as_store_lookup(AsStore *self, const char *path);
/* TRUE when at least one device is present but not readable by this user. */
gboolean as_store_permissions_denied(AsStore *self);
/* Installs the udev rules on the worker thread and rescans afterwards. Only
 * works when the process may write /etc/udev/rules.d (root); otherwise the
 * window runs its own helper through pkexec. */
void as_store_fix_permissions(AsStore *self);

/* Analog travel stream of one keyboard, used by the switch demo. It is polled
 * on the worker thread and the "analog" signal fires when values arrived. */
gboolean as_store_analog_start(AsStore *self, const char *path);
void as_store_analog_stop(AsStore *self);
/* Travel of one matrix position in thousandths of a millimetre, 0 when the
 * stream is off or that key is released. */
int as_store_travel(AsStore *self, const char *path, int key);

/* Queues a write. Returns FALSE when the device is unknown. */
gboolean as_store_apply(AsStore *self, const char *path, const AsOpRequest *req);
/* Closes every handle; call after as_worker_stop(). */
void as_store_shutdown(AsStore *self);

G_END_DECLS

#endif
