// SPDX-License-Identifier: MIT
/*
 * Attack Shark R11 Ultra (Compx / "Control HUB WEB" 16-byte protocol).
 *
 * Transport: interrupt/control report ID 8, 16-byte packet.
 *   [0]     command
 *   [1]     0
 *   [2..3]  flash address, big endian
 *   [4]     payload length (+ 128 for keyboard-class devices)
 *   [5..14] payload
 *   [15]    checksum: (85 - sum([0..14] & 0xff)) & 0xff, minus the report ID
 *
 * Responses carry the same header, the payload starts at byte 6 and the
 * responder echoes the first 3 bytes (5 for flash commands) of the request.
 */
#include "internal.h"

#include <errno.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define REPORT_ID 8
#define PACKET_LEN 16
#define RETRIES 5
#define REPLY_WINDOW_MS 250
#define FLASH_CHUNK 10

static const struct {
    int hz;
    uint8_t code;
} mouse_rates[] = {
    { 125, 0x08 }, { 250, 0x04 }, { 500, 0x02 }, { 1000, 0x01 },
    { 2000, 0x10 }, { 4000, 0x20 }, { 8000, 0x40 },
};

static long now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void build_packet(uint8_t *p, uint8_t cmd, unsigned addr,
                         const uint8_t *payload, unsigned len)
{
    unsigned sum = 0;

    memset(p, 0, PACKET_LEN);
    p[0] = cmd;
    p[2] = (uint8_t)(addr >> 8);
    p[3] = (uint8_t)addr;
    p[4] = (uint8_t)len;
    if (payload && len) {
        memcpy(p + 5, payload, len < 10 ? len : 10);
    }
    for (int i = 0; i < 15; i++) {
        sum += p[i];
    }
    p[15] = (uint8_t)(85 - REPORT_ID - (sum & 0xff));
}

/*
 * Send a packet and wait for the reply.  Returns 0 and fills reply (16 bytes,
 * report ID stripped) on success, -ETIMEDOUT if the device never answered.
 */
static int exchange(struct as_mouse *m, const uint8_t *pkt, uint8_t *reply,
                    int echo_len)
{
    uint8_t frame[PACKET_LEN + 1];

    frame[0] = REPORT_ID;
    memcpy(frame + 1, pkt, PACKET_LEN);

    for (int attempt = 0; attempt < RETRIES; attempt++) {
        long deadline;
        int ret = as_hid_write(&m->hid, frame, sizeof(frame), 500);

        if (ret < 0) {
            return ret;
        }
        deadline = now_ms() + REPLY_WINDOW_MS;
        for (;;) {
            uint8_t buf[64];
            long left = deadline - now_ms();

            if (left <= 0) {
                break;
            }
            ret = as_hid_read(&m->hid, buf, sizeof(buf), (int)left);
            if (ret == -ETIMEDOUT) {
                break;
            }
            if (ret < 0) {
                return ret;
            }
            if (ret < PACKET_LEN + 1 || buf[0] != REPORT_ID) {
                continue;
            }
            /* The echo starts at buf[1]; a reply must repeat it. A device
             * that understands but refuses the command answers with the
             * command byte and status 1 instead of the echo, and callers
             * need that answer rather than a timeout. */
            if (memcmp(buf + 1, pkt, (size_t)echo_len) == 0 ||
                (buf[1] == pkt[0] && buf[2] == 1)) {
                if (reply) {
                    size_t n = (size_t)ret - 1;

                    memset(reply, 0, PACKET_LEN);
                    memcpy(reply, buf + 1, n < PACKET_LEN ? n : PACKET_LEN);
                }
                return 0;
            }
        }
    }
    return -ETIMEDOUT;
}

static int is_settings_mouse(const struct as_hid *dev)
{
    return as_hid_is_mouse(dev) && as_hid_has_vendor(dev);
}

static int is_bluetooth_mouse(const struct as_hid *dev)
{
    return as_hid_is_mouse(dev) && as_hid_is_bluetooth(dev) &&
           !as_hid_has_vendor(dev);
}

int as_mouse_find_bluetooth(struct as_hid *out)
{
    return as_hid_find_first(is_bluetooth_mouse, out);
}

int as_mouse_open_path(struct as_mouse *m, const char *path)
{
    struct as_hid found[AS_HID_MAX];
    int n = as_hid_scan(found, AS_HID_MAX);

    memset(m, 0, sizeof(*m));
    m->hid.fd = -1;
    for (int i = 0; i < n; i++) {
        if (!strcmp(found[i].path, path) && is_settings_mouse(&found[i])) {
            m->hid = found[i];
            return as_hid_open(&m->hid);
        }
    }
    return -ENODEV;
}

int as_mouse_open(struct as_mouse *m)
{
    int ret;

    memset(m, 0, sizeof(*m));
    m->hid.fd = -1;

    ret = as_hid_open_first(is_settings_mouse, 0x3554, &m->hid);
    if (ret == -ENODEV) {
        return as_mouse_find_bluetooth(NULL) ? -ENOTSUP : -ENODEV;
    }
    if (ret < 0) {
        return ret;
    }
    ret = as_mouse_handshake(m);
    if (ret < 0) {
        as_mouse_close(m);
    }
    return ret;
}

void as_mouse_close(struct as_mouse *m)
{
    as_hid_close(&m->hid);
}

int as_mouse_handshake(struct as_mouse *m)
{
    uint8_t pkt[PACKET_LEN], reply[PACKET_LEN], payload[8];
    unsigned seed = 0x4b3c2d1e;
    int ret;

    for (int i = 0; i < 4; i++) {
        seed = seed * 1103515245u + 12345u;
        payload[i] = (uint8_t)(seed >> 16);
    }
    memset(payload + 4, 0, 4);

    build_packet(pkt, AS_M_CMD_HANDSHAKE, 0, payload, 8);
    ret = exchange(m, pkt, reply, 3);
    if (ret < 0) {
        return ret;
    }
    if (reply[1] != 0) {
        return -EREMOTEIO;
    }
    /* reply[9] = cid, reply[10] = mid, reply[11] = type. */
    m->cid = reply[9];
    m->mid = reply[10];
    m->type = reply[11];
    return 0;
}

int as_mouse_raw(struct as_mouse *m, uint8_t cmd, unsigned addr,
                 const uint8_t *payload, unsigned len, uint8_t reply[16])
{
    uint8_t pkt[PACKET_LEN];

    if (len > 10 || (len && !payload)) {
        return -EINVAL;
    }
    build_packet(pkt, cmd, addr, payload, len);
    return exchange(m, pkt, reply, 3);
}

/* Chunked flash transfer: the device takes ten bytes per packet. */
static int flash_io(struct as_mouse *m, uint8_t cmd, unsigned addr, uint8_t *buf,
                    unsigned len)
{
    unsigned done = 0;

    while (done < len) {
        unsigned chunk = len - done < FLASH_CHUNK ? len - done : FLASH_CHUNK;
        int write_cmd = cmd == AS_M_CMD_WRITE_FLASH;
        uint8_t pkt[PACKET_LEN], reply[PACKET_LEN];
        int ret;

        build_packet(pkt, cmd, addr + done, write_cmd ? buf + done : NULL,
                     chunk);
        ret = exchange(m, pkt, reply, 5);
        if (ret < 0) {
            return ret;
        }
        /* reply[1]: 0 - data, 1 - understood but not supported. Without this
         * check a refused EEPROM write looks like a successful one. */
        if (reply[1] != 0) {
            return reply[1] == 1 ? -EREMOTEIO : -EIO;
        }
        if (!write_cmd) {
            if ((reply[4] & 0x0f) != chunk) {
                return -EPROTO;
            }
            memcpy(buf + done, reply + 5, chunk);
        }
        done += chunk;
        if (write_cmd) {
            usleep(2000);
        }
    }
    return 0;
}

int as_mouse_read_block(struct as_mouse *m, unsigned addr, uint8_t *buf,
                        unsigned len)
{
    if (addr > 0xffffu || len > 0x10000u - addr) {
        return -EINVAL;
    }
    return flash_io(m, AS_M_CMD_READ_FLASH, addr, buf, len);
}

int as_mouse_write_block(struct as_mouse *m, unsigned addr, const uint8_t *buf,
                         unsigned len)
{
    if (addr > 0xffffu || len > 0x10000u - addr) {
        return -EINVAL;
    }
    return flash_io(m, AS_M_CMD_WRITE_FLASH, addr, (uint8_t *)buf, len);
}

int as_mouse_set_u8(struct as_mouse *m, unsigned offset, uint8_t value)
{
    uint8_t pair[2] = { value, (uint8_t)(85 - value) };

    return as_mouse_write_block(m, offset, pair, 2);
}

int as_mouse_rate_code(int hz)
{
    for (size_t i = 0; i < AS_N(mouse_rates); i++) {
        if (mouse_rates[i].hz == hz) {
            return mouse_rates[i].code;
        }
    }
    return -1;
}

int as_mouse_rate_from_code(int code)
{
    for (size_t i = 0; i < AS_N(mouse_rates); i++) {
        if (mouse_rates[i].code == code) {
            return mouse_rates[i].hz;
        }
    }
    return -1;
}

int as_mouse_set_report_rate(struct as_mouse *m, int hz)
{
    int code = as_mouse_rate_code(hz);

    return code < 0 ? -EINVAL : as_mouse_set_u8(m, AS_M_REPORT_RATE, code);
}

int as_mouse_dpi_valid(unsigned dpi)
{
    if (dpi < 50) {
        return 0;
    }
    if (dpi <= 30000) {
        return (dpi - 50) % 50 == 0;
    }
    if (dpi <= 60000) {
        return dpi % 100 == 0;
    }
    return 0;
}

int as_mouse_encode_dpi(unsigned dpi, uint8_t out[4])
{
    unsigned val, hi, dpiex = 0;

    if (!as_mouse_dpi_valid(dpi)) {
        return -EINVAL;
    }
    if (dpi > 30000) {
        val = dpi / 2 / 50 - 1;
        dpiex = 17;
    } else {
        val = dpi / 50 - 1;
    }
    hi = val >> 8;
    out[0] = (uint8_t)(val & 0xff);
    out[1] = (uint8_t)(val & 0xff);
    out[2] = (uint8_t)(((hi << 2) | (hi << 6)) | dpiex | (dpiex << 4));
    out[3] = (uint8_t)(85 - (out[0] + out[1] + out[2]));
    return 0;
}

unsigned as_mouse_decode_dpi(const uint8_t rec[4])
{
    /* The high bits are replicated in bits 2-3 and 6-7; take one copy. */
    unsigned hi = (rec[2] >> 2) & 0x03;
    unsigned dpi = (((rec[0] | (hi << 8)) + 1) * 50);

    return rec[2] & 0x01 ? dpi * 2 : dpi;
}

int as_mouse_set_dpi_stage(struct as_mouse *m, int stage, unsigned dpi)
{
    uint8_t rec[4];
    int ret;

    if (stage < 0 || stage >= AS_MAX_DPI_STAGES) {
        return -EINVAL;
    }
    ret = as_mouse_encode_dpi(dpi, rec);
    if (ret < 0) {
        return ret;
    }
    return as_mouse_write_block(m, AS_M_DPI_VALUE + (unsigned)stage * 4, rec, 4);
}

int as_mouse_set_light(struct as_mouse *m, uint8_t mode, uint8_t r, uint8_t g,
                       uint8_t b, uint8_t speed, uint8_t brightness, int on)
{
    /*
     * 160 mode | 161 R | 162 G | 163 B | 164 speed | 165 brightness |
     * 166 checksum (85 - sum) | 167 on/off (written as its own pair).
     */
    uint8_t rec[7] = { mode, r, g, b, speed, brightness,
                       (uint8_t)(85 - (mode + r + g + b + speed + brightness)) };
    int ret = as_mouse_write_block(m, AS_M_LIGHT, rec, sizeof(rec));

    return ret < 0 ? ret : as_mouse_set_u8(m, AS_M_LIGHT + 7, on ? 1 : 0);
}

int as_mouse_read_state(struct as_mouse *m, struct as_mouse_state *st)
{
    uint8_t reply[PACKET_LEN];
    const uint8_t *raw;
    int ret;

    memset(st, 0, sizeof(*st));

    /* Link status and battery are answered by the dongle itself. */
    ret = as_mouse_raw(m, AS_M_CMD_ONLINE, 0, NULL, 0, reply);
    if (ret < 0) {
        return ret;
    }
    /* reply[1] == 1 means "understood but unsupported". */
    st->online = reply[1] == 0 ? reply[5] : 0;

    if (as_mouse_raw(m, AS_M_CMD_VERSION, 0, NULL, 0, reply) == 0 &&
        reply[1] == 0) {
        st->version_valid = 1;
        st->version_major = reply[5];
        st->version_minor = reply[6];
    }
    if (as_mouse_raw(m, AS_M_CMD_DONGLE_VERSION, 0, NULL, 0, reply) == 0 &&
        reply[1] == 0) {
        st->dongle_version_valid = 1;
        st->dongle_version[0] = reply[5];
        st->dongle_version[1] = reply[6];
        st->dongle_version[2] = reply[7];
    }
    if (as_mouse_raw(m, AS_M_CMD_GET_CONFIG, 0, NULL, 0, reply) == 0 &&
        reply[1] == 0) {
        st->profile = reply[5];
    }
    if (as_mouse_raw(m, AS_M_CMD_BATTERY, 0, NULL, 0, reply) == 0 &&
        reply[1] == 0) {
        st->charging = reply[6] == 1;
        if (reply[9] == 1) {
            st->battery = reply[10];
        } else {
            st->battery = reply[5];
            st->battery_voltage = (reply[7] << 8) | reply[8];
        }
    }

    /* Everything below lives in the mouse and needs the RF link up. */
    if (!st->online) {
        st->report_rate = st->lod = st->debounce = st->motion_sync =
            st->angle_snap = st->ripple = st->sleep_time = st->profile = -1;
        return 0;
    }

    ret = as_mouse_read_block(m, 0, st->raw, AS_MOUSE_CONFIG_LEN);
    if (ret < 0) {
        return ret;
    }
    st->config_valid = 1;
    raw = st->raw;

    st->report_rate = as_mouse_rate_from_code(raw[AS_M_REPORT_RATE]);
    st->dpi_stages = raw[AS_M_MAX_DPI_STAGE];
    if (st->dpi_stages > AS_MAX_DPI_STAGES) {
        st->dpi_stages = AS_MAX_DPI_STAGES;
    }
    st->active_dpi_stage = raw[AS_M_CURRENT_DPI];
    for (int i = 0; i < st->dpi_stages; i++) {
        st->dpi[i] = as_mouse_decode_dpi(raw + AS_M_DPI_VALUE + i * 4);
    }
    st->lod = raw[AS_M_LOD];
    st->debounce = raw[AS_M_DEBOUNCE];
    st->motion_sync = raw[AS_M_MOTION_SYNC];
    st->angle_snap = raw[AS_M_ANGLE_SNAP];
    st->ripple = raw[AS_M_RIPPLE];
    st->sleep_time = raw[AS_M_SLEEP_TIME];
    st->light_mode = raw[AS_M_LIGHT];
    memcpy(st->light_color, raw + AS_M_LIGHT + 1, sizeof(st->light_color));
    st->light_speed = raw[AS_M_LIGHT + 4];
    st->light_brightness = raw[AS_M_LIGHT + 5];
    st->light_on = raw[AS_M_LIGHT + 7] == 1;
    return 0;
}
