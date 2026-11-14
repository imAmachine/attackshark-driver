// SPDX-License-Identifier: MIT
#define _GNU_SOURCE

#include "as-store.h"
#include "as-worker.h"

#include <gio/gio.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

enum {
    SIGNAL_CHANGED,
    SIGNAL_ERROR,
    N_SIGNALS,
};

static guint signals[N_SIGNALS];

typedef struct {
    AsDeviceKind kind;
    int opened;
    unsigned vid;
    unsigned pid;
    int iface;
    char path[64];
    struct as_mouse mouse;
    struct as_keyboard kb;
} Handle;

struct _AsStore {
    GObject parent_instance;
    GPtrArray *devices;    /* AsDevice*, main thread only */
    GHashTable *handles;   /* path -> Handle*, worker thread only */
    gboolean busy;
    guint settle_id;
};

G_DEFINE_TYPE(AsStore, as_store, G_TYPE_OBJECT)

/* --------------------------- classification --------------------------- */

/* Same rule the CLI uses: the vendor HID collection inside the descriptor. */
static int classify(const struct as_hid *hid)
{
    if (hid->vid == 0x3151 &&
        memmem(hid->descriptor, hid->descriptor_len, "\x06\xff\xff\x09\x02", 5)) {
        return AS_DEVICE_KEYBOARD;
    }
    if (hid->vid == 0x3554 &&
        memmem(hid->descriptor, hid->descriptor_len, "\x06\x02\xff", 3)) {
        return AS_DEVICE_MOUSE;
    }
    return -1;
}

/* ------------------------------ handles ------------------------------- */

static Handle *handle_new(const struct as_hid *hid)
{
    Handle *h = g_new0(Handle, 1);

    h->kind = (AsDeviceKind)classify(hid);
    h->vid = hid->vid;
    h->pid = hid->pid;
    h->iface = hid->iface;
    h->mouse.hid.fd = -1;
    h->kb.hid.fd = -1;
    h->kb.analog.fd = -1;
    g_strlcpy(h->path, hid->path, sizeof(h->path));
    return h;
}

static void handle_close(Handle *h)
{
    if (!h->opened) {
        return;
    }
    if (h->kind == AS_DEVICE_MOUSE) {
        as_mouse_close(&h->mouse);
    } else {
        as_keyboard_close(&h->kb);
    }
    h->opened = 0;
}

static void handle_destroy(gpointer data)
{
    Handle *h = data;

    handle_close(h);
    g_free(h);
}

static int handle_open(Handle *h)
{
    int rc;

    if (h->opened) {
        return 0;
    }
    if (h->kind == AS_DEVICE_MOUSE) {
        rc = as_mouse_open(&h->mouse);
        if (rc < 0) {
            return rc;
        }
        if (strcmp(h->mouse.hid.path, h->path) != 0) {
            as_mouse_close(&h->mouse);
            return -ENODEV;
        }
    } else {
        rc = as_keyboard_open(&h->kb);
        if (rc < 0) {
            return rc;
        }
        if (strcmp(h->kb.hid.path, h->path) != 0) {
            as_keyboard_close(&h->kb);
            return -ENODEV;
        }
    }
    h->opened = 1;
    return 0;
}

/* Drops handles of devices that are gone; runs on the worker thread. */
static void handles_prune(GHashTable *handles, GHashTable *present)
{
    GHashTableIter iter;
    gpointer key, value;
    GPtrArray *gone = g_ptr_array_new();

    g_hash_table_iter_init(&iter, handles);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        if (!g_hash_table_contains(present, key)) {
            g_ptr_array_add(gone, g_strdup(key));
        }
    }
    for (guint i = 0; i < gone->len; i++) {
        g_hash_table_remove(handles, gone->pdata[i]);
    }
    g_ptr_array_free(gone, TRUE);
}

/* --------------------------- snapshot filling -------------------------- */

static void fill_identity(AsDevice *dev, const struct as_hid *hid, int kind)
{
    dev->kind = (AsDeviceKind)kind;
    dev->light_on = 1;   /* assumed on until the user flips the switch */
    g_strlcpy(dev->path, hid->path, sizeof(dev->path));
    dev->vid = hid->vid;
    dev->pid = hid->pid;
    dev->iface = hid->iface;
    if (kind == AS_DEVICE_MOUSE) {
        g_strlcpy(dev->product, as_mouse_model_name(hid->pid), sizeof(dev->product));
    } else {
        g_strlcpy(dev->product, "R85 HE", sizeof(dev->product));
    }
}

static void fill_mouse(Handle *h, AsDevice *dev)
{
    struct as_mouse_state st;
    int rc = as_mouse_read_state(&h->mouse, &st);

    if (rc < 0) {
        snprintf(dev->error, sizeof(dev->error), "%s", as_strerror(rc));
        if (rc == -ENODEV || rc == -EIO) {
            handle_close(h);
        }
        return;
    }

    dev->online = st.online;
    dev->config_valid = st.config_valid;
    dev->charging = st.charging;
    dev->battery = st.battery;
    dev->voltage = st.battery_voltage;
    dev->rate = st.config_valid ? st.report_rate : -1;
    dev->dpi_stages = st.dpi_stages;
    dev->active_stage = st.active_dpi_stage;
    for (int i = 0; i < st.dpi_stages && i < AS_MAX_DPI_STAGES; i++) {
        dev->dpi[i] = st.dpi[i][0];
    }
    dev->lod = st.config_valid ? st.lod : -1;
    dev->debounce = st.config_valid ? st.debounce : -1;
    dev->motion_sync = st.config_valid ? st.motion_sync : -1;
    dev->angle_snap = st.config_valid ? st.angle_snap : -1;
    dev->ripple = st.config_valid ? st.ripple : -1;
    dev->sleep_time = st.config_valid ? st.sleep_time : -1;
    dev->profile = st.config_valid ? st.profile : -1;
    dev->fw_valid = st.version_valid;
    dev->fw_major = st.version_major;
    dev->fw_minor = st.version_minor;
    dev->dongle_valid = st.dongle_version_valid;
    memcpy(dev->dongle, st.dongle_version, sizeof(dev->dongle));
    dev->light_mode = st.light_mode;
    dev->light_brightness = st.light_brightness;
    dev->light_speed = st.light_speed;
    dev->light_on = st.light_on;
    memcpy(dev->rgb, st.light_color, sizeof(dev->rgb));
    if (!st.config_valid && !st.online) {
        /* Not an error: the mouse is asleep or switched off. */
        dev->error[0] = 0;
    }
}

static void fill_keyboard(Handle *h, AsDevice *dev)
{
    struct as_kb_state st;
    int rc = as_keyboard_read_state(&h->kb, &st);

    if (rc < 0) {
        snprintf(dev->error, sizeof(dev->error), "%s", as_strerror(rc));
        if (rc == -ENODEV || rc == -EIO) {
            handle_close(h);
        }
        return;
    }

    dev->usb_version_valid = st.usb_version_valid;
    dev->usb_version = st.usb_version;
    dev->revision_valid = st.revision_valid;
    dev->revision = st.revision;
    dev->profile = st.profile;
    dev->rate = st.report_rate;
    dev->debounce = st.debounce;
    dev->mag_mode = st.mag_mode;
    if (st.side_light.valid) {
        dev->side_light_valid = 1;
        dev->side_light_mode = st.side_light.mode;
        dev->side_light_speed = st.side_light.speed;
        dev->side_light_brightness = st.side_light.brightness;
        dev->side_light_dazzle = st.side_light.dazzle;
        dev->side_rgb[0] = st.side_light.r;
        dev->side_rgb[1] = st.side_light.g;
        dev->side_rgb[2] = st.side_light.b;
    }
    if (st.light_on >= 0) {
        dev->light_on = st.light_on;
        dev->light_on_known = 1;
    }
    if (st.light.valid) {
        dev->light_mode = st.light.mode;
        dev->light_speed = st.light.speed;
        dev->light_brightness = st.light.brightness;
        dev->light_dazzle = st.light.dazzle;
        dev->light_opt = st.light.option;
        dev->rgb[0] = st.light.r;
        dev->rgb[1] = st.light.g;
        dev->rgb[2] = st.light.b;
    }
}

/* ------------------------------ scanning ------------------------------ */

typedef struct {
    AsStore *store;
    char *only_path;
    GPtrArray *devices;
} RefreshJob;

static void refresh_job_free(gpointer data)
{
    RefreshJob *job = data;

    if (job->devices) {
        g_ptr_array_free(job->devices, TRUE);
    }
    g_free(job->only_path);
    g_free(job);
}

static void refresh_one(AsStore *self, const struct as_hid *hid, int kind,
                        GPtrArray *out)
{
    Handle *h = g_hash_table_lookup(self->handles, hid->path);
    AsDevice *dev = g_new0(AsDevice, 1);
    int rc;

    fill_identity(dev, hid, kind);
    if (!h) {
        h = handle_new(hid);
        g_hash_table_insert(self->handles, g_strdup(hid->path), h);
    }
    rc = handle_open(h);
    if (rc < 0) {
        snprintf(dev->error, sizeof(dev->error), "%s", as_strerror(rc));
    } else if (h->kind == AS_DEVICE_MOUSE) {
        fill_mouse(h, dev);
    } else {
        fill_keyboard(h, dev);
    }
    g_ptr_array_add(out, dev);
}

static void refresh_job_run(gpointer data, GError **error)
{
    RefreshJob *job = data;
    GPtrArray *out = g_ptr_array_new_with_free_func(g_free);

    if (job->only_path) {
        Handle *h = g_hash_table_lookup(job->store->handles, job->only_path);
        if (!h) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                        "device %s is not connected", job->only_path);
        } else {
            struct as_hid hid;
            memset(&hid, 0, sizeof(hid));
            hid.fd = -1;
            g_strlcpy(hid.path, h->path, sizeof(hid.path));
            hid.vid = h->vid;
            hid.pid = h->pid;
            hid.iface = h->iface;
            refresh_one(job->store, &hid, h->kind, out);
        }
    } else {
        struct as_hid found[16];
        GHashTable *present = g_hash_table_new(g_str_hash, g_str_equal);
        int n = as_hid_find(NULL, 0, found, 16);

        for (int i = 0; i < n; i++) {
            int kind = classify(&found[i]);
            if (kind < 0) {
                continue;
            }
            g_hash_table_add(present, found[i].path);
            refresh_one(job->store, &found[i], kind, out);
        }
        handles_prune(job->store->handles, present);
        g_hash_table_destroy(present);
    }
    job->devices = out;
}

static void store_merge(AsStore *self, GPtrArray *fresh)
{
    GPtrArray *next = g_ptr_array_new_with_free_func(g_free);

    for (guint i = 0; i < fresh->len; i++) {
        AsDevice *dev = fresh->pdata[i];
        AsDevice *old = as_store_lookup(self, dev->path);

        if (old) {
            int light_on = old->light_on;
            int light_on_known = old->light_on_known;

            *old = *dev;
            /* The backlight switch is not readable on every firmware, so the
             * value the user set must survive a refresh. */
            if (!dev->light_on_known) {
                old->light_on = light_on;
                old->light_on_known = light_on_known;
            }
            g_free(dev);
            dev = old;
        }
        g_ptr_array_add(next, dev);
    }
    /* Devices that disappeared are not referenced by the new array anymore. */
    for (guint i = 0; i < self->devices->len; i++) {
        AsDevice *old = self->devices->pdata[i];
        if (!g_ptr_array_find(next, old, NULL)) {
            g_free(old);
        }
    }
    g_ptr_array_free(self->devices, FALSE);
    self->devices = next;
}

static gboolean settle_cb(gpointer data)
{
    AsStore *self = data;

    self->settle_id = 0;
    as_store_scan(self);
    return G_SOURCE_REMOVE;
}

static void refresh_job_done(gpointer data, GError *error, gpointer user_data)
{
    RefreshJob *job = data;
    AsStore *self = job->store;

    (void)user_data;
    self->busy = FALSE;
    if (job->devices) {
        store_merge(self, job->devices);
        g_ptr_array_free(job->devices, FALSE);
        job->devices = NULL;
    }
    if (error) {
        g_signal_emit(self, signals[SIGNAL_ERROR], 0, (const char *)NULL,
                      error->message);
    }
    g_signal_emit(self, signals[SIGNAL_CHANGED], 0);

    /* Values can be clamped by the firmware (DPI steps, for example), so the
     * optimistic snapshot is confirmed against the hardware once the user
     * stops fiddling with the controls. */
    if (!self->settle_id) {
        self->settle_id = g_timeout_add_seconds(2, settle_cb, self);
    }
}

/* ------------------------------- writes ------------------------------- */

typedef struct {
    AsStore *store;
    char *path;
    AsOpRequest req;
} ApplyJob;

static void apply_job_free(gpointer data)
{
    ApplyJob *job = data;

    g_free(job->path);
    g_free(job);
}

static int apply_mouse(struct as_mouse *mouse, const AsOpRequest *req)
{
    switch (req->op) {
    case AS_OP_MOUSE_RATE:
        return as_mouse_set_report_rate(mouse, req->value);
    case AS_OP_MOUSE_DPI_STAGE:
        return as_mouse_set_dpi_stage(mouse, req->index, (unsigned)req->value);
    case AS_OP_MOUSE_LOD:
        return as_mouse_set_u8(mouse, AS_M_LOD, (uint8_t)req->value);
    case AS_OP_MOUSE_DEBOUNCE:
        return as_mouse_set_u8(mouse, AS_M_DEBOUNCE, (uint8_t)req->value);
    case AS_OP_MOUSE_MOTION_SYNC:
        return as_mouse_set_u8(mouse, AS_M_MOTION_SYNC, req->value ? 1 : 0);
    case AS_OP_MOUSE_ANGLE_SNAP:
        return as_mouse_set_u8(mouse, AS_M_ANGLE_SNAP, req->value ? 1 : 0);
    case AS_OP_MOUSE_RIPPLE:
        return as_mouse_set_u8(mouse, AS_M_RIPPLE, req->value ? 1 : 0);
    case AS_OP_MOUSE_SLEEP:
        return as_mouse_set_u8(mouse, AS_M_SLEEP_TIME, (uint8_t)req->value);
    case AS_OP_MOUSE_LIGHT:
        return as_mouse_set_light(mouse, (uint8_t)req->light_mode, req->rgb[0],
                                  req->rgb[1], req->rgb[2],
                                  (uint8_t)req->light_speed,
                                  (uint8_t)req->light_brightness,
                                  req->light_on);
    default:
        return -EINVAL;
    }
}

static int apply_keyboard(struct as_keyboard *kb, const AsOpRequest *req)
{
    struct as_kb_light light;

    switch (req->op) {
    case AS_OP_KB_RATE:
        return as_keyboard_set_report_rate(kb, req->value);
    case AS_OP_KB_PROFILE:
        return as_keyboard_set_profile(kb, req->value);
    case AS_OP_KB_DEBOUNCE:
        return as_keyboard_set_debounce(kb, req->value);
    case AS_OP_KB_MAG_MODE:
        return as_keyboard_set_mag_mode(kb, req->value);
    case AS_OP_KB_LIGHT_POWER:
        return as_keyboard_set_light_power(kb, req->value ? 1 : 0);
    case AS_OP_KB_SIDE_LIGHT:
        memset(&light, 0, sizeof(light));
        light.valid = 1;
        light.mode = (uint8_t)req->light_mode;
        light.speed = req->light_speed;
        light.brightness = req->light_brightness;
        light.option = req->light_option;
        light.dazzle = req->light_dazzle;
        light.r = req->rgb[0];
        light.g = req->rgb[1];
        light.b = req->rgb[2];
        return as_keyboard_set_sidelight(kb, &light);
    case AS_OP_KB_LIGHT:
        memset(&light, 0, sizeof(light));
        light.valid = 1;
        light.mode = (uint8_t)req->light_mode;
        light.speed = req->light_speed;
        light.brightness = req->light_brightness;
        light.option = req->light_option;
        light.dazzle = req->light_dazzle;
        light.r = req->rgb[0];
        light.g = req->rgb[1];
        light.b = req->rgb[2];
        return as_keyboard_set_light(kb, &light);
    default:
        return -EINVAL;
    }
}

static void apply_job_run(gpointer data, GError **error)
{
    ApplyJob *job = data;
    Handle *h = g_hash_table_lookup(job->store->handles, job->path);
    int rc;

    if (!h) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                    "device %s is not connected", job->path);
        return;
    }
    rc = handle_open(h);
    if (rc < 0) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s", as_strerror(rc));
        return;
    }
    rc = h->kind == AS_DEVICE_MOUSE ? apply_mouse(&h->mouse, &job->req)
                                    : apply_keyboard(&h->kb, &job->req);
    if (rc < 0) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s", as_strerror(rc));
        if (rc == -ENODEV || rc == -EIO) {
            handle_close(h);
        }
    }
}

/* Mirrors the request into the snapshot so the UI reacts immediately. */
static void apply_optimistic(AsDevice *dev, const AsOpRequest *req)
{
    switch (req->op) {
    case AS_OP_MOUSE_RATE:
    case AS_OP_KB_RATE:
        dev->rate = req->value;
        break;
    case AS_OP_MOUSE_DPI_STAGE:
        if (req->index >= 0 && req->index < AS_MAX_DPI_STAGES) {
            dev->dpi[req->index] = (unsigned)req->value;
        }
        break;
    case AS_OP_MOUSE_LOD:
        dev->lod = req->value;
        break;
    case AS_OP_MOUSE_DEBOUNCE:
    case AS_OP_KB_DEBOUNCE:
        dev->debounce = req->value;
        break;
    case AS_OP_MOUSE_MOTION_SYNC:
        dev->motion_sync = req->value;
        break;
    case AS_OP_MOUSE_ANGLE_SNAP:
        dev->angle_snap = req->value;
        break;
    case AS_OP_MOUSE_RIPPLE:
        dev->ripple = req->value;
        break;
    case AS_OP_MOUSE_SLEEP:
        dev->sleep_time = req->value;
        break;
    case AS_OP_MOUSE_LIGHT:
    case AS_OP_KB_LIGHT:
        dev->light_mode = req->light_mode;
        dev->light_speed = req->light_speed;
        dev->light_brightness = req->light_brightness;
        memcpy(dev->rgb, req->rgb, sizeof(dev->rgb));
        if (req->op == AS_OP_MOUSE_LIGHT) {
            dev->light_on = req->light_on;
        } else {
            dev->light_opt = req->light_option;
            dev->light_dazzle = req->light_dazzle;
        }
        break;
    case AS_OP_KB_SIDE_LIGHT:
        dev->side_light_mode = req->light_mode;
        dev->side_light_speed = req->light_speed;
        dev->side_light_brightness = req->light_brightness;
        dev->side_light_dazzle = req->light_dazzle;
        memcpy(dev->side_rgb, req->rgb, sizeof(dev->side_rgb));
        dev->side_light_valid = 1;
        break;
    case AS_OP_KB_LIGHT_POWER:
        dev->light_on = req->value ? 1 : 0;
        dev->light_on_known = 1;
        break;
    case AS_OP_KB_PROFILE:
        dev->profile = req->value;
        break;
    case AS_OP_KB_MAG_MODE:
        dev->mag_mode = req->value;
        break;
    default:
        break;
    }
}

static void apply_job_done(gpointer data, GError *error, gpointer user_data)
{
    ApplyJob *job = data;

    (void)user_data;
    if (error) {
        g_signal_emit(job->store, signals[SIGNAL_ERROR], 0, job->path,
                      error->message);
        return;
    }
    AsDevice *dev = as_store_lookup(job->store, job->path);
    if (dev) {
        apply_optimistic(dev, &job->req);
    }
    g_signal_emit(job->store, signals[SIGNAL_CHANGED], 0);
}

/* -------------------------------- API --------------------------------- */

AsStore *as_store_get(void)
{
    static AsStore *instance;

    if (!instance) {
        instance = g_object_new(AS_TYPE_STORE, NULL);
        g_object_ref_sink(instance);
    }
    return instance;
}

void as_store_scan(AsStore *self)
{
    RefreshJob *job;

    g_return_if_fail(AS_IS_STORE(self));
    if (self->busy) {
        return;
    }
    self->busy = TRUE;
    job = g_new0(RefreshJob, 1);
    job->store = self;
    as_job_run(refresh_job_run, job, refresh_job_free, refresh_job_done, NULL,
               NULL);
}

void as_store_refresh(AsStore *self, const char *path)
{
    RefreshJob *job;

    g_return_if_fail(AS_IS_STORE(self));
    if (self->busy) {
        return;
    }
    self->busy = TRUE;
    job = g_new0(RefreshJob, 1);
    job->store = self;
    job->only_path = g_strdup(path);
    as_job_run(refresh_job_run, job, refresh_job_free, refresh_job_done, NULL,
               NULL);
}

gboolean as_store_busy(AsStore *self)
{
    return self->busy;
}

GPtrArray *as_store_devices(AsStore *self)
{
    return self->devices;
}

AsDevice *as_store_lookup(AsStore *self, const char *path)
{
    for (guint i = 0; i < self->devices->len; i++) {
        AsDevice *dev = self->devices->pdata[i];
        if (strcmp(dev->path, path) == 0) {
            return dev;
        }
    }
    return NULL;
}

guint as_store_count(AsStore *self)
{
    return self->devices->len;
}

gboolean as_store_apply(AsStore *self, const char *path, const AsOpRequest *req)
{
    ApplyJob *job;

    g_return_val_if_fail(AS_IS_STORE(self), FALSE);
    if (!as_store_lookup(self, path)) {
        return FALSE;
    }
    job = g_new0(ApplyJob, 1);
    job->store = self;
    job->path = g_strdup(path);
    job->req = *req;
    as_job_run(apply_job_run, job, apply_job_free, apply_job_done, NULL, NULL);
    return TRUE;
}

void as_store_shutdown(AsStore *self)
{
    if (self->settle_id) {
        g_source_remove(self->settle_id);
        self->settle_id = 0;
    }
    g_hash_table_remove_all(self->handles);
}

static void as_store_finalize(GObject *object)
{
    AsStore *self = AS_STORE(object);

    g_hash_table_destroy(self->handles);
    g_ptr_array_free(self->devices, TRUE);
    G_OBJECT_CLASS(as_store_parent_class)->finalize(object);
}

static void as_store_class_init(AsStoreClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->finalize = as_store_finalize;

    signals[SIGNAL_CHANGED] =
        g_signal_new("changed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                     0, NULL, NULL, NULL, G_TYPE_NONE, 0);
    signals[SIGNAL_ERROR] =
        g_signal_new("error", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
                     NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_STRING,
                     G_TYPE_STRING);
}

static void as_store_init(AsStore *self)
{
    self->devices = g_ptr_array_new_with_free_func(g_free);
    self->handles = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                          handle_destroy);
}
