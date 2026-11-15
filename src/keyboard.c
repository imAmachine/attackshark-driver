// SPDX-License-Identifier: MIT
/*
 * Attack Shark R85 HE (Royuan RY5088 / GearHub) keyboard protocol.
 *
 * Transport: 64-byte feature report without report number + a 64-byte
 * interrupt IN endpoint.  Payload layout:
 *   [0]     command
 *   [1..]   arguments
 *   [7]     checksum 0xFF - sum([0..6])   (bit7 variant)
 *   [8]     checksum 0xFF - sum([0..7])   (bit8 variant, lighting)
 */
#include "attackshark.h"

#include <errno.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define REPORT_ID 0
#define REPORT_LEN AS_K_REPORT_LEN
#define HS_TIMEOUT_MS 400

/* Usage page 0xFFFF / usage 0x02 collection with a 64-byte feature report. */
static const unsigned char keyboard_probe[] = {
    0x06, 0xff, 0xff, 0x09, 0x02, 0xa1, 0x01
};

/* Analog travel stream: usage page 0xFFFF / usage 0x01, report ID 5. */
static const unsigned char analog_probe[] = {
    0x06, 0xff, 0xff, 0x09, 0x01, 0xa1, 0x01, 0x85, 0x05
};

static void put_bit7(uint8_t *p)
{
    unsigned sum = 0;
    for (int i = 0; i < 7; i++) {
        sum += p[i];
    }
    p[7] = (uint8_t)(0xff - (sum & 0xff));
}

static void put_bit8(uint8_t *p)
{
    unsigned sum = 0;
    for (int i = 0; i < 8; i++) {
        sum += p[i];
    }
    p[8] = (uint8_t)(0xff - (sum & 0xff));
}

int as_keyboard_find_bluetooth(struct as_hid *out)
{
    struct as_hid found[16];
    int n = as_hid_find(NULL, 0, found, 16);

    for (int i = 0; i < n; i++) {
        if (!as_hid_is_keyboard(&found[i]) ||
            !as_hid_is_bluetooth(&found[i])) {
            continue;
        }
        if (out) {
            *out = found[i];
        }
        return 1;
    }
    return 0;
}

int as_keyboard_open(struct as_keyboard *kb)
{
    struct as_hid found[8];
    int n, err;

    memset(kb, 0, sizeof(*kb));
    kb->hid.fd = -1;
    kb->analog.fd = -1;

    n = as_hid_find(keyboard_probe, sizeof(keyboard_probe), found, 8);
    if (n <= 0) {
        /* A Bluetooth link publishes standard HID collections only: say so
         * instead of pretending the keyboard is absent. */
        return as_keyboard_find_bluetooth(NULL) ? -ENOTSUP : -ENODEV;
    }
    for (int i = 0; i < n; i++) {
        if (found[i].vid == 0x3151) {
            kb->hid = found[i];
            break;
        }
    }
    if (kb->hid.path[0] == 0) {
        kb->hid = found[0];
    }

    err = as_hid_open(&kb->hid);
    if (err < 0) {
        return err;
    }
    return 0;
}

void as_keyboard_close(struct as_keyboard *kb)
{
    as_hid_close(&kb->hid);
    if (kb->analog_open) {
        as_hid_close(&kb->analog);
        kb->analog_open = 0;
    }
}

static int keyboard_cmd(struct as_keyboard *kb, uint8_t cmd,
                        const uint8_t *args, size_t args_len, int bit8,
                        uint8_t *out, int timeout_ms, int validate)
{
    uint8_t payload[REPORT_LEN];
    uint8_t frame[REPORT_LEN + 1];
    int ret;

    if (args_len > REPORT_LEN - 1) {
        return -EINVAL;
    }

    memset(payload, 0, sizeof(payload));
    payload[0] = cmd;
    if (args && args_len) {
        memcpy(payload + 1, args, args_len);
    }
    if (bit8) {
        put_bit8(payload);
    } else {
        put_bit7(payload);
    }

    memset(frame, 0, sizeof(frame));
    frame[0] = REPORT_ID;
    memcpy(frame + 1, payload, REPORT_LEN);

    usleep(10000);
    ret = as_feature_set(&kb->hid, frame, sizeof(frame));
    if (ret < 0) {
        return ret;
    }
    if (!out) {
        return 0;
    }

    usleep(10000);
    memset(out, 0, REPORT_LEN);
    ret = as_feature_get(&kb->hid, frame, sizeof(frame), timeout_ms);
    if (ret < 0) {
        return ret;
    }
    memcpy(out, frame + 1, REPORT_LEN);
    if (validate && out[0] != cmd) {
        return -ENODATA;
    }
    return 0;
}

int as_keyboard_cmd(struct as_keyboard *kb, uint8_t cmd, const uint8_t *args,
                    size_t args_len, int bit8, uint8_t *out, int timeout_ms)
{
    return keyboard_cmd(kb, cmd, args, args_len, bit8, out, timeout_ms, 1);
}

int as_keyboard_cmd_raw(struct as_keyboard *kb, uint8_t cmd, const uint8_t *args,
                        size_t args_len, int bit8, uint8_t *out,
                        int timeout_ms)
{
    return keyboard_cmd(kb, cmd, args, args_len, bit8, out, timeout_ms, 0);
}

int as_kb_rate_code(int hz)
{
    switch (hz) {
    case 8000: return 0;
    case 4000: return 1;
    case 2000: return 2;
    case 1000: return 3;
    case 500: return 4;
    case 250: return 5;
    case 125: return 6;
    default: return -1;
    }
}

int as_kb_rate_from_code(int code)
{
    switch (code) {
    case 0: return 8000;
    case 1: return 4000;
    case 2: return 2000;
    case 3: return 1000;
    case 4: return 500;
    case 5: return 250;
    case 6: return 125;
    default: return -1;
    }
}

uint8_t as_kb_mm_to_raw(double mm)
{
    double raw = mm * 10.0;
    if (raw < 0) {
        raw = 0;
    }
    if (raw > 255) {
        raw = 255;
    }
    return (uint8_t)((int)(raw + 0.5) - 1);
}

double as_kb_raw_to_mm(uint8_t raw)
{
    return (raw + 1) / 10.0;
}

uint8_t as_kb_deadzone_to_raw(double mm)
{
    double raw = 40.0 - mm * 10.0 - 1.0;
    if (raw < 0) {
        raw = 0;
    }
    if (raw > 255) {
        raw = 255;
    }
    return (uint8_t)(raw + 0.5);
}

int as_keyboard_open_analog(struct as_keyboard *kb)
{
    struct as_hid found[8];
    int n;

    if (kb->analog_open) {
        return 0;
    }
    n = as_hid_find(analog_probe, sizeof(analog_probe), found, 8);
    for (int i = 0; i < n; i++) {
        if (found[i].vid == 0x3151) {
            kb->analog = found[i];
            if (as_hid_open(&kb->analog) < 0) {
                return -errno;
            }
            kb->analog_open = 1;
            return 0;
        }
    }
    return -ENODEV;
}

int as_keyboard_enable_stream(struct as_keyboard *kb, int on)
{
    uint8_t arg = on ? 1 : 0;
    return as_keyboard_cmd(kb, AS_K_SET_MAGNETISM_STREAM, &arg, 1, 0, NULL, 0);
}

/* ---------- Hall-effect value encoding ---------- */

int as_keyboard_precision(struct as_keyboard *kb)
{
    uint8_t out[REPORT_LEN];

    if (kb->mag_mult) {
        return kb->mag_mult;
    }
    if (as_keyboard_cmd(kb, AS_K_GET_USB_VERSION, NULL, 0, 0, out,
                        HS_TIMEOUT_MS) != 0) {
        return -ENODATA;
    }
    kb->usb_version = (uint32_t)out[1] | ((uint32_t)out[2] << 8) |
                      ((uint32_t)out[3] << 16) | ((uint32_t)out[4] << 24);
    kb->usb_version_known = 1;
    if (kb->usb_version >= 1280) {
        kb->mag_mult = 200;   /* 0.005 mm */
    } else if (kb->usb_version >= 768) {
        kb->mag_mult = 100;   /* 0.01 mm */
    } else {
        kb->mag_mult = 10;    /* 0.1 mm */
    }
    return kb->mag_mult;
}

static int param_width(struct as_keyboard *kb, int param)
{
    switch (param) {
    case AS_K_MAG_OPTION:
    case AS_K_MAG_SOCD_PAIR:
        return 1;
    case AS_K_MAG_DKS_FLAGS:
        return 4;
    default:
        return as_keyboard_precision(kb) >= 100 ? 2 : 1;
    }
}

static int32_t encode_value(struct as_keyboard *kb, int param, double mm)
{
    int mult = as_keyboard_precision(kb);

    if (mult < 0) {
        return -1;
    }
    if (mult < 100) {
        return param == AS_K_MAG_DEADZONE ? as_kb_deadzone_to_raw(mm)
                                          : as_kb_mm_to_raw(mm);
    }
    return (int32_t)(mm * mult + 0.5);
}

double as_kb_value_to_mm(struct as_keyboard *kb, int param, int32_t raw)
{
    int mult = kb->mag_mult ? kb->mag_mult : 10;

    if (mult < 100) {
        return param == AS_K_MAG_DEADZONE ? (40 - (raw + 1)) / 10.0
                                          : (raw + 1) / 10.0;
    }
    return (double)raw / mult;
}

int as_keyboard_get_param(struct as_keyboard *kb, int param, int32_t *values,
                          size_t count)
{
    int width = param_width(kb, param);
    size_t per_page, pages;

    if (width < 0) {
        return width;
    }
    per_page = 64 / (size_t)width;
    pages = (count + per_page - 1) / per_page;

    for (size_t page = 0; page < pages; page++) {
        uint8_t args[3] = { (uint8_t)param, 1, (uint8_t)page };
        uint8_t out[REPORT_LEN];
        size_t offset = page * per_page;
        size_t n = count - offset < per_page ? count - offset : per_page;
        int ret = as_keyboard_cmd_raw(kb, AS_K_GET_MULTI_MAGNETISM, args, 3, 0,
                                      out, HS_TIMEOUT_MS);
        if (ret < 0) {
            return ret;
        }
        for (size_t i = 0; i < n; i++) {
            const uint8_t *p = out + i * (size_t)width;
            int32_t v = 0;
            for (int b = 0; b < width; b++) {
                v |= (int32_t)p[b] << (8 * b);
            }
            values[offset + i] = v;
        }
    }
    return 0;
}

int as_keyboard_set_param(struct as_keyboard *kb, int param,
                          const int32_t *values, size_t count, int save)
{
    int width = param_width(kb, param);
    size_t per_page, pages;

    if (width < 0) {
        return width;
    }
    per_page = AS_K_MAG_WRITE_PAGE / (size_t)width;
    pages = (count + per_page - 1) / per_page;

    for (size_t page = 0; page < pages; page++) {
        uint8_t args[7 + AS_K_MAG_WRITE_PAGE];
        size_t offset = page * per_page;
        size_t n = count - offset < per_page ? count - offset : per_page;
        int ret;

        memset(args, 0, sizeof(args));
        args[0] = (uint8_t)param;
        args[1] = 1; /* all keys */
        args[2] = (uint8_t)page;
        args[3] = (uint8_t)(save && page + 1 == pages ? 1 : 0);
        for (size_t i = 0; i < n; i++) {
            int32_t v = values[offset + i];
            for (int b = 0; b < width; b++) {
                args[7 + i * (size_t)width + b] = (uint8_t)(v >> (8 * b));
            }
        }
        ret = as_keyboard_cmd(kb, AS_K_SET_MULTI_MAGNETISM, args,
                              7 + n * (size_t)width, 0, NULL, 0);
        if (ret < 0) {
            return ret;
        }
        usleep(20000);
    }
    return 0;
}

int as_keyboard_set_light(struct as_keyboard *kb, const struct as_kb_light *l)
{
    uint8_t args[8];

    args[0] = l->mode;
    args[1] = (uint8_t)(4 - (l->speed < 0 ? 0 : (l->speed > 4 ? 4 : l->speed)));
    args[2] = (uint8_t)(l->brightness < 0 ? 0 :
                        (l->brightness > 4 ? 4 : l->brightness));
    args[3] = (uint8_t)(((l->option & 0x0f) << 4) | (l->dazzle ? 8 : 7));
    args[4] = l->r;
    args[5] = l->g;
    args[6] = l->b;

    return as_keyboard_cmd(kb, AS_K_SET_LED, args, 7, 1, NULL, 0);
}

int as_keyboard_get_light_power(struct as_keyboard *kb)
{
    uint8_t out[REPORT_LEN];
    int ret;

    ret = as_keyboard_cmd(kb, AS_K_GET_LED_POWER, NULL, 0, 0, out,
                          HS_TIMEOUT_MS);
    if (ret < 0) {
        return ret;
    }
    /* Both layouts the vendor software handles put the flag in byte 1 or 3;
     * 1 means the backlight is in low-power (off) mode. Some firmwares just
     * echo the request, so "all zeroes" is "unknown", not "on". */
    if (out[1] == 1 || out[3] == 1) {
        return 0;
    }
    return -1;
}

int as_keyboard_set_light_power(struct as_keyboard *kb, int on)
{
    uint8_t args[3] = { 0, 0, on ? 0 : 1 };

    return as_keyboard_cmd(kb, AS_K_SET_LED_POWER, args, sizeof(args), 0, NULL,
                           0);
}

int as_keyboard_get_sidelight(struct as_keyboard *kb, struct as_kb_light *l)
{
    uint8_t out[REPORT_LEN];
    int ret;

    ret = as_keyboard_cmd(kb, AS_K_GET_SIDELED, NULL, 0, 0, out,
                          HS_TIMEOUT_MS);
    if (ret < 0) {
        return ret;
    }
    memset(l, 0, sizeof(*l));
    l->valid = 1;
    l->mode = out[1];
    l->speed = out[2];
    l->brightness = out[3];
    l->option = out[4] >> 4;
    l->dazzle = (out[4] & 0x0f) == 8;
    l->r = out[5];
    l->g = out[6];
    l->b = out[7];
    return 0;
}

int as_keyboard_set_sidelight(struct as_keyboard *kb,
                              const struct as_kb_light *l)
{
    uint8_t args[7];

    args[0] = l->mode;
    args[1] = (uint8_t)(l->speed < 0 ? 0 : (l->speed > 4 ? 4 : l->speed));
    args[2] = (uint8_t)(l->brightness < 0 ? 0 :
                        (l->brightness > 4 ? 4 : l->brightness));
    args[3] = (uint8_t)(((l->option & 0x0f) << 4) | (l->dazzle ? 8 : 7));
    args[4] = l->r;
    args[5] = l->g;
    args[6] = l->b;
    return as_keyboard_cmd(kb, AS_K_SET_SIDELED, args, sizeof(args), 1, NULL, 0);
}

int as_keyboard_set_report_rate(struct as_keyboard *kb, int hz)
{
    uint8_t args[2];
    int code = as_kb_rate_code(hz);

    if (code < 0) {
        return -EINVAL;
    }
    args[0] = 0;
    args[1] = (uint8_t)code;
    return as_keyboard_cmd(kb, AS_K_SET_REPORT_RATE, args, 2, 0, NULL, 0);
}

int as_keyboard_set_profile(struct as_keyboard *kb, int profile)
{
    uint8_t arg = (uint8_t)profile;
    return as_keyboard_cmd(kb, AS_K_SET_PROFILE, &arg, 1, 0, NULL, 0);
}

int as_keyboard_set_debounce(struct as_keyboard *kb, int ms)
{
    uint8_t arg = (uint8_t)(ms < 0 ? 0 : ms);
    return as_keyboard_cmd(kb, AS_K_SET_DEBOUNCE, &arg, 1, 0, NULL, 0);
}

int as_keyboard_set_mag_mode(struct as_keyboard *kb, int mode)
{
    uint8_t arg = (uint8_t)mode;
    return as_keyboard_cmd(kb, AS_K_SET_MAGNETISM_MODE, &arg, 1, 0, NULL, 0);
}

int as_keyboard_set_travel(struct as_keyboard *kb, int rapid_trigger,
                           double actuation, double release, double rt_press,
                           double rt_release, double deadzone)
{
    int32_t values[128];
    int32_t act, rel, dz, rtp, rtr;
    int ret;

    act = encode_value(kb, AS_K_MAG_ACTUATION, actuation);
    rel = encode_value(kb, AS_K_MAG_RELEASE, release);
    dz = encode_value(kb, AS_K_MAG_DEADZONE, deadzone);
    rtp = encode_value(kb, AS_K_MAG_RT_PRESS, rt_press);
    rtr = encode_value(kb, AS_K_MAG_RT_RELEASE, rt_release);
    if (act < 0 || rel < 0 || dz < 0 || rtp < 0 || rtr < 0) {
        return -ENODATA;
    }

    for (int i = 0; i < 128; i++) {
        values[i] = act;
    }
    if ((ret = as_keyboard_set_param(kb, AS_K_MAG_ACTUATION, values, 128, 0))) {
        return ret;
    }
    for (int i = 0; i < 128; i++) {
        values[i] = rel;
    }
    if ((ret = as_keyboard_set_param(kb, AS_K_MAG_RELEASE, values, 128, 0))) {
        return ret;
    }
    for (int i = 0; i < 128; i++) {
        values[i] = dz;
    }
    if ((ret = as_keyboard_set_param(kb, AS_K_MAG_DEADZONE, values, 128, 0))) {
        return ret;
    }
    if (rapid_trigger) {
        for (int i = 0; i < 128; i++) {
            values[i] = rtp;
        }
        if ((ret = as_keyboard_set_param(kb, AS_K_MAG_RT_PRESS, values, 128, 0))) {
            return ret;
        }
        for (int i = 0; i < 128; i++) {
            values[i] = rtr;
        }
        if ((ret = as_keyboard_set_param(kb, AS_K_MAG_RT_RELEASE, values, 128, 0))) {
            return ret;
        }
    }
    /* The option page enables Rapid Trigger - write it last and save it. */
    for (int i = 0; i < 128; i++) {
        values[i] = rapid_trigger ? 0x80 : 0x00;
    }
    return as_keyboard_set_param(kb, AS_K_MAG_OPTION, values, 128, 1);
}

int as_keyboard_read_state(struct as_keyboard *kb, struct as_kb_state *st)
{
    uint8_t out[REPORT_LEN];
    int ret;
    int dead = 0;

    memset(st, 0, sizeof(*st));
    st->light_on = -1;
    st->report_rate = -1;
    st->profile = -1;
    st->debounce = -1;
    st->revision = -1;
    st->mag_mode = -1;

    ret = as_keyboard_cmd(kb, AS_K_GET_USB_VERSION, NULL, 0, 0, out,
                          HS_TIMEOUT_MS);
    if (ret == -ETIMEDOUT) {
        dead = 1;
    } else if (ret == 0) {
        kb->usb_version = (uint32_t)out[1] | ((uint32_t)out[2] << 8) |
                          ((uint32_t)out[3] << 16) | ((uint32_t)out[4] << 24);
        kb->usb_version_known = 1;
        st->usb_version = kb->usb_version;
        st->usb_version_valid = kb->usb_version != 0xffffffffu;
        kb->mag_mult = kb->usb_version >= 1280 ? 200 :
                       kb->usb_version >= 768 ? 100 : 10;
    }

    if (!dead) {
        ret = as_keyboard_cmd(kb, AS_K_GET_REV, NULL, 0, 0, out, HS_TIMEOUT_MS);
        if (ret == -ETIMEDOUT) {
            dead = 1;
        } else if (ret == 0) {
            st->revision = (out[2] << 8) | out[1];
            st->revision_valid = 1;
        }
    }

    if (!dead) {
        ret = as_keyboard_cmd(kb, AS_K_GET_PROFILE, NULL, 0, 0, out, HS_TIMEOUT_MS);
        if (ret == -ETIMEDOUT) {
            dead = 1;
        } else if (ret == 0) {
            st->profile = out[1];
        }
    }

    if (!dead) {
        uint8_t arg = 0;
        ret = as_keyboard_cmd(kb, AS_K_GET_REPORT_RATE, &arg, 1, 0, out,
                              HS_TIMEOUT_MS);
        if (ret == -ETIMEDOUT) {
            dead = 1;
        } else if (ret == 0) {
            st->report_rate = as_kb_rate_from_code(out[2] ? out[2] : out[1]);
        }
    }

    if (!dead) {
        ret = as_keyboard_cmd(kb, AS_K_GET_DEBOUNCE, NULL, 0, 0, out,
                              HS_TIMEOUT_MS);
        if (ret == -ETIMEDOUT) {
            dead = 1;
        } else if (ret == 0) {
            st->debounce = out[1];
        }
    }

    if (!dead) {
        ret = as_keyboard_cmd(kb, AS_K_GET_LED, NULL, 0, 0, out, HS_TIMEOUT_MS);
        if (ret == -ETIMEDOUT) {
            dead = 1;
        } else if (ret == 0) {
            struct as_kb_light *l = &st->light;
            l->valid = 1;
            l->mode = out[1];
            l->speed = 4 - out[2];
            l->brightness = out[3];
            l->option = out[4] >> 4;
            l->dazzle = (out[4] & 0x0f) == 8;
            l->r = out[5];
            l->g = out[6];
            l->b = out[7];
        }
    }

    if (!dead) {
        if (as_keyboard_get_sidelight(kb, &st->side_light) < 0) {
            st->side_light.valid = 0;
        }
    }

    if (!dead) {
        int power = as_keyboard_get_light_power(kb);

        if (power >= 0) {
            st->light_on = power;
        }
    }

    if (!dead) {
        ret = as_keyboard_cmd(kb, AS_K_GET_MAGNETISM_MODE, NULL, 0, 0, out,
                              HS_TIMEOUT_MS);
        if (ret == 0) {
            st->mag_mode = out[1];
        }
    }
    return dead ? -ETIMEDOUT : 0;
}
