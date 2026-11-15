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
#include "internal.h"

#include <errno.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define REPORT_ID 0
#define REPORT_LEN AS_K_REPORT_LEN
#define HS_TIMEOUT_MS 400
#define IO_DELAY_US 10000

/* Analog travel stream: usage page 0xFFFF / usage 0x01, report ID 5. */
static const unsigned char analog_probe[] = {
    0x06, 0xff, 0xff, 0x09, 0x01, 0xa1, 0x01, 0x85, 0x05
};

static const int kb_rates[] = { 8000, 4000, 2000, 1000, 500, 250, 125 };

static int is_settings_keyboard(const struct as_hid *dev)
{
    return as_hid_is_keyboard(dev) && as_hid_has_vendor(dev);
}

static int is_bluetooth_keyboard(const struct as_hid *dev)
{
    return as_hid_is_keyboard(dev) && as_hid_is_bluetooth(dev);
}

static int is_analog(const struct as_hid *dev)
{
    return as_hid_is_keyboard(dev) &&
           as_hid_desc_has(dev, analog_probe, sizeof(analog_probe));
}

int as_keyboard_find_bluetooth(struct as_hid *out)
{
    return as_hid_find_first(is_bluetooth_keyboard, out);
}

int as_keyboard_open(struct as_keyboard *kb)
{
    int ret;

    memset(kb, 0, sizeof(*kb));
    kb->hid.fd = -1;
    kb->analog.fd = -1;

    ret = as_hid_open_first(is_settings_keyboard, 0x3151, &kb->hid);
    if (ret == -ENODEV) {
        /* A Bluetooth link publishes standard HID collections only: say so
         * instead of pretending the keyboard is absent. */
        return as_keyboard_find_bluetooth(NULL) ? -ENOTSUP : -ENODEV;
    }
    return ret;
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
    uint8_t payload[REPORT_LEN] = { 0 };
    uint8_t frame[REPORT_LEN + 1] = { 0 };
    unsigned sum = 0;
    int ret;

    if (args_len > REPORT_LEN - 1) {
        return -EINVAL;
    }
    payload[0] = cmd;
    if (args && args_len) {
        memcpy(payload + 1, args, args_len);
    }
    for (size_t i = 0; i < (size_t)(bit8 ? 8 : 7); i++) {
        sum += payload[i];
    }
    payload[bit8 ? 8 : 7] = (uint8_t)(0xff - (sum & 0xff));

    frame[0] = REPORT_ID;
    memcpy(frame + 1, payload, REPORT_LEN);
    usleep(IO_DELAY_US);
    ret = as_feature_set(&kb->hid, frame, sizeof(frame));
    if (ret < 0 || !out) {
        return ret;
    }

    usleep(IO_DELAY_US);
    ret = as_feature_get(&kb->hid, frame, sizeof(frame), timeout_ms);
    if (ret < 0) {
        return ret;
    }
    memcpy(out, frame + 1, REPORT_LEN);
    return validate && out[0] != cmd ? -ENODATA : 0;
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
    for (size_t i = 0; i < AS_N(kb_rates); i++) {
        if (kb_rates[i] == hz) {
            return (int)i;
        }
    }
    return -1;
}

int as_kb_rate_from_code(int code)
{
    return code >= 0 && (size_t)code < AS_N(kb_rates) ? kb_rates[code] : -1;
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
    if (raw < 1) {
        raw = 1;
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
    int ret;

    if (kb->analog_open) {
        return 0;
    }
    ret = as_hid_open_first(is_analog, 0x3151, &kb->analog);
    if (ret < 0) {
        return ret;
    }
    kb->analog_open = 1;
    return 0;
}

int as_keyboard_enable_stream(struct as_keyboard *kb, int on)
{
    uint8_t arg = on ? 1 : 0;

    return as_keyboard_cmd(kb, AS_K_SET_MAGNETISM_STREAM, &arg, 1, 0, NULL, 0);
}

/* ---------- Hall-effect value encoding ---------- */

static void read_usb_version(struct as_keyboard *kb, const uint8_t *out)
{
    kb->usb_version = (uint32_t)out[1] | ((uint32_t)out[2] << 8) |
                      ((uint32_t)out[3] << 16) | ((uint32_t)out[4] << 24);
    kb->usb_version_known = 1;
    kb->mag_mult = kb->usb_version >= 1280 ? 200   /* 0.005 mm */
                 : kb->usb_version >= 768  ? 100   /* 0.01 mm */
                                           : 10;   /* 0.1 mm */
}

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
    read_usb_version(kb, out);
    return kb->mag_mult;
}

static int param_width(struct as_keyboard *kb, int param)
{
    int precision;

    switch (param) {
    case AS_K_MAG_OPTION:
    case AS_K_MAG_SOCD_PAIR:
        return 1;
    case AS_K_MAG_DKS_FLAGS:
        return 4;
    default:
        precision = as_keyboard_precision(kb);
        return precision < 0 ? precision : precision >= 100 ? 2 : 1;
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
        uint8_t args[7 + AS_K_MAG_WRITE_PAGE] = { 0 };
        size_t offset = page * per_page;
        size_t n = count - offset < per_page ? count - offset : per_page;
        int ret;

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

/* ---------- lighting ---------- */

static void light_args(const struct as_kb_light *l, uint8_t args[7],
                       int invert_speed)
{
    int speed = l->speed < 0 ? 0 : l->speed > 4 ? 4 : l->speed;
    int bright = l->brightness < 0 ? 0 : l->brightness > 4 ? 4 : l->brightness;

    args[0] = l->mode;
    args[1] = (uint8_t)(invert_speed ? 4 - speed : speed);
    args[2] = (uint8_t)bright;
    args[3] = (uint8_t)(((l->option & 0x0f) << 4) | (l->dazzle ? 8 : 7));
    args[4] = l->r;
    args[5] = l->g;
    args[6] = l->b;
}

static void parse_light(const uint8_t *out, struct as_kb_light *l,
                        int invert_speed)
{
    memset(l, 0, sizeof(*l));
    l->valid = 1;
    l->mode = out[1];
    l->speed = invert_speed ? 4 - out[2] : out[2];
    l->brightness = out[3];
    l->option = out[4] >> 4;
    l->dazzle = (out[4] & 0x0f) == 8;
    l->r = out[5];
    l->g = out[6];
    l->b = out[7];
}

static int set_light_cmd(struct as_keyboard *kb, uint8_t cmd,
                         const struct as_kb_light *l, int invert_speed)
{
    uint8_t args[7];

    light_args(l, args, invert_speed);
    return as_keyboard_cmd(kb, cmd, args, sizeof(args), 1, NULL, 0);
}

int as_keyboard_set_light(struct as_keyboard *kb, const struct as_kb_light *l)
{
    return set_light_cmd(kb, AS_K_SET_LED, l, 1);
}

int as_keyboard_set_sidelight(struct as_keyboard *kb,
                              const struct as_kb_light *l)
{
    return set_light_cmd(kb, AS_K_SET_SIDELED, l, 0);
}

int as_keyboard_get_sidelight(struct as_keyboard *kb, struct as_kb_light *l)
{
    uint8_t out[REPORT_LEN];
    int ret = as_keyboard_cmd(kb, AS_K_GET_SIDELED, NULL, 0, 0, out,
                              HS_TIMEOUT_MS);

    if (ret < 0) {
        return ret;
    }
    parse_light(out, l, 0);
    return 0;
}

int as_keyboard_get_light_power(struct as_keyboard *kb)
{
    uint8_t out[REPORT_LEN];
    int ret = as_keyboard_cmd(kb, AS_K_GET_LED_POWER, NULL, 0, 0, out,
                              HS_TIMEOUT_MS);

    if (ret < 0) {
        return ret;
    }
    /* Both layouts the vendor software handles put the flag in byte 1 or 3;
     * 1 means the backlight is in low-power (off) mode. Some firmwares just
     * echo the request, so "all zeroes" is "unknown", not "on". */
    return out[1] == 1 || out[3] == 1 ? 0 : -1;
}

int as_keyboard_set_light_power(struct as_keyboard *kb, int on)
{
    uint8_t args[3] = { 0, 0, on ? 0 : 1 };

    return as_keyboard_cmd(kb, AS_K_SET_LED_POWER, args, sizeof(args), 0, NULL,
                           0);
}

/* ---------- settings ---------- */

static int set_u8(struct as_keyboard *kb, uint8_t cmd, uint8_t arg)
{
    return as_keyboard_cmd(kb, cmd, &arg, 1, 0, NULL, 0);
}

int as_keyboard_set_report_rate(struct as_keyboard *kb, int hz)
{
    int code = as_kb_rate_code(hz);
    uint8_t args[2] = { 0, (uint8_t)code };

    if (code < 0) {
        return -EINVAL;
    }
    return as_keyboard_cmd(kb, AS_K_SET_REPORT_RATE, args, 2, 0, NULL, 0);
}

int as_keyboard_set_profile(struct as_keyboard *kb, int profile)
{
    return set_u8(kb, AS_K_SET_PROFILE, (uint8_t)profile);
}

int as_keyboard_set_debounce(struct as_keyboard *kb, int ms)
{
    return set_u8(kb, AS_K_SET_DEBOUNCE, (uint8_t)(ms < 0 ? 0 : ms));
}

int as_keyboard_set_mag_mode(struct as_keyboard *kb, int mode)
{
    return set_u8(kb, AS_K_SET_MAGNETISM_MODE, (uint8_t)mode);
}

/* Writes one value to every key of a Hall-effect parameter page. */
static int set_all(struct as_keyboard *kb, int param, int32_t value, int save)
{
    int32_t values[128];

    for (size_t i = 0; i < AS_N(values); i++) {
        values[i] = value;
    }
    return as_keyboard_set_param(kb, param, values, AS_N(values), save);
}

int as_keyboard_set_travel(struct as_keyboard *kb, int rapid_trigger,
                           double actuation, double release, double rt_press,
                           double rt_release, double deadzone)
{
    int32_t act = encode_value(kb, AS_K_MAG_ACTUATION, actuation);
    int32_t rel = encode_value(kb, AS_K_MAG_RELEASE, release);
    int32_t dz = encode_value(kb, AS_K_MAG_DEADZONE, deadzone);
    int32_t rtp = encode_value(kb, AS_K_MAG_RT_PRESS, rt_press);
    int32_t rtr = encode_value(kb, AS_K_MAG_RT_RELEASE, rt_release);
    int ret;

    if (act < 0 || rel < 0 || dz < 0 || rtp < 0 || rtr < 0) {
        return -ENODATA;
    }
    if ((ret = set_all(kb, AS_K_MAG_ACTUATION, act, 0)) ||
        (ret = set_all(kb, AS_K_MAG_RELEASE, rel, 0)) ||
        (ret = set_all(kb, AS_K_MAG_DEADZONE, dz, 0))) {
        return ret;
    }
    if (rapid_trigger &&
        ((ret = set_all(kb, AS_K_MAG_RT_PRESS, rtp, 0)) ||
         (ret = set_all(kb, AS_K_MAG_RT_RELEASE, rtr, 0)))) {
        return ret;
    }
    /* The option page enables Rapid Trigger - write it last and save it. */
    return set_all(kb, AS_K_MAG_OPTION, rapid_trigger ? 0x80 : 0, 1);
}

static int kb_query(struct as_keyboard *kb, uint8_t cmd, const uint8_t *args,
                    size_t args_len, uint8_t *out, int *dead)
{
    int ret = as_keyboard_cmd(kb, cmd, args, args_len, 0, out, HS_TIMEOUT_MS);

    if (ret == -ETIMEDOUT) {
        *dead = 1;
    }
    return ret;
}

int as_keyboard_read_state(struct as_keyboard *kb, struct as_kb_state *st)
{
    uint8_t out[REPORT_LEN], arg = 0;
    int dead = 0;

    memset(st, 0, sizeof(*st));
    st->light_on = st->report_rate = st->profile = st->debounce =
        st->revision = st->mag_mode = -1;

    if (kb_query(kb, AS_K_GET_USB_VERSION, NULL, 0, out, &dead) == 0) {
        read_usb_version(kb, out);
        st->usb_version = kb->usb_version;
        st->usb_version_valid = kb->usb_version != 0xffffffffu;
    }
    if (!dead && kb_query(kb, AS_K_GET_REV, NULL, 0, out, &dead) == 0) {
        st->revision = (out[2] << 8) | out[1];
        st->revision_valid = 1;
    }
    if (!dead && kb_query(kb, AS_K_GET_PROFILE, NULL, 0, out, &dead) == 0) {
        st->profile = out[1];
    }
    if (!dead && kb_query(kb, AS_K_GET_REPORT_RATE, &arg, 1, out, &dead) == 0) {
        st->report_rate = as_kb_rate_from_code(out[2] ? out[2] : out[1]);
    }
    if (!dead && kb_query(kb, AS_K_GET_DEBOUNCE, NULL, 0, out, &dead) == 0) {
        st->debounce = out[1];
    }
    if (!dead && kb_query(kb, AS_K_GET_LED, NULL, 0, out, &dead) == 0) {
        parse_light(out, &st->light, 1);
    }
    if (!dead && as_keyboard_get_sidelight(kb, &st->side_light) >= 0) {
        st->side_light.valid = 1;
    }
    if (!dead) {
        int power = as_keyboard_get_light_power(kb);

        if (power >= 0) {
            st->light_on = power;
        }
    }
    if (!dead && as_keyboard_cmd(kb, AS_K_GET_MAGNETISM_MODE, NULL, 0, 0, out,
                                 HS_TIMEOUT_MS) == 0) {
        st->mag_mode = out[1];
    }
    return dead ? -ETIMEDOUT : 0;
}
