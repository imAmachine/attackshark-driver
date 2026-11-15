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
#include "attackshark.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define REPORT_ID 8
#define PACKET_LEN 16
#define RETRIES 5
#define REPLY_WINDOW_MS 250
#define FLASH_CHUNK 10

/* Vendor collection: usage page 0xFF02, report ID 8. */
static const unsigned char mouse_probe[] = { 0x06, 0x02, 0xff };

static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void put_checksum(uint8_t *p)
{
    unsigned sum = 0;
    for (int i = 0; i < 15; i++) {
        sum += p[i];
    }
    p[15] = (uint8_t)(((85 - (sum & 0xff)) & 0xff) - REPORT_ID);
}

static void build_packet(uint8_t *p, uint8_t cmd, unsigned addr,
                         const uint8_t *payload, unsigned len)
{
    memset(p, 0, PACKET_LEN);
    p[0] = cmd;
    p[2] = (uint8_t)(addr >> 8);
    p[3] = (uint8_t)(addr & 0xff);
    p[4] = (uint8_t)(len + 0);
    if (payload && len) {
        memcpy(p + 5, payload, len > 10 ? 10 : len);
    }
    put_checksum(p);
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
            if (ret < 2 || buf[0] != REPORT_ID) {
                continue;
            }
            /* Le[i] lives at buf[i + 1]; compare the echo. */
            if (ret >= echo_len + 1 &&
                memcmp(buf + 1, pkt, (size_t)echo_len) == 0) {
                if (reply) {
                    size_t n = (size_t)ret - 1;
                    memset(reply, 0, PACKET_LEN);
                    memcpy(reply, buf + 1, n > PACKET_LEN ? PACKET_LEN : n);
                }
                return 0;
            }
        }
    }
    return -ETIMEDOUT;
}

int as_mouse_find_bluetooth(struct as_hid *out)
{
    struct as_hid found[16];
    int n = as_hid_find(NULL, 0, found, 16);

    for (int i = 0; i < n; i++) {
        if (!as_hid_is_mouse(&found[i]) || !as_hid_is_bluetooth(&found[i]) ||
            as_hid_has_vendor(&found[i])) {
            continue;
        }
        if (out) {
            *out = found[i];
        }
        return 1;
    }
    return 0;
}

int as_mouse_open(struct as_mouse *m)
{
    struct as_hid found[8];
    int n, err;

    memset(m, 0, sizeof(*m));
    m->hid.fd = -1;

    n = as_hid_find(mouse_probe, sizeof(mouse_probe), found, 8);
    if (n <= 0) {
        return as_mouse_find_bluetooth(NULL) ? -ENOTSUP : -ENODEV;
    }
    /* Prefer the vendor interface on a Compx device. */
    for (int i = 0; i < n; i++) {
        if (found[i].vid == 0x3554) {
            m->hid = found[i];
            break;
        }
    }
    if (m->hid.fd == -1 && m->hid.path[0] == 0) {
        m->hid = found[0];
    }

    err = as_hid_open(&m->hid);
    if (err < 0) {
        return err;
    }
    return as_mouse_handshake(m);
}

void as_mouse_close(struct as_mouse *m)
{
    as_hid_close(&m->hid);
}

int as_mouse_handshake(struct as_mouse *m)
{
    uint8_t pkt[PACKET_LEN], reply[PACKET_LEN];
    uint8_t payload[8];
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
    /* Le[9] = reply[9] = cid, Le[10] = mid, Le[11] = type. */
    m->cid = reply[9];
    m->mid = reply[10];
    m->type = reply[11];
    /* Keyboards enumerate with a 128-byte offset on their payload length. */
    m->type_offset = 0;
    return 0;
}

int as_mouse_read_block(struct as_mouse *m, unsigned addr, uint8_t *buf,
                        unsigned len)
{
    unsigned done = 0;

    while (done < len) {
        unsigned chunk = len - done > FLASH_CHUNK ? FLASH_CHUNK : len - done;
        uint8_t pkt[PACKET_LEN], reply[PACKET_LEN];
        int ret;

        build_packet(pkt, AS_M_CMD_READ_FLASH, addr + done, NULL, chunk);
        ret = exchange(m, pkt, reply, 5);
        if (ret < 0) {
            return ret;
        }
        if ((reply[4] & 0x0f) != chunk) {
            return -EPROTO;
        }
        memcpy(buf + done, reply + 5, chunk);
        done += chunk;
    }
    return 0;
}

int as_mouse_write_block(struct as_mouse *m, unsigned addr, const uint8_t *buf,
                         unsigned len)
{
    unsigned done = 0;

    while (done < len) {
        unsigned chunk = len - done > FLASH_CHUNK ? FLASH_CHUNK : len - done;
        uint8_t pkt[PACKET_LEN], reply[PACKET_LEN];
        int ret;

        build_packet(pkt, AS_M_CMD_WRITE_FLASH, addr + done, buf + done, chunk);
        ret = exchange(m, pkt, reply, 5);
        if (ret < 0) {
            return ret;
        }
        done += chunk;
        usleep(2000);
    }
    return 0;
}

int as_mouse_set_u8(struct as_mouse *m, unsigned offset, uint8_t value)
{
    uint8_t pair[2] = { value, (uint8_t)(85 - value) };
    return as_mouse_write_block(m, offset, pair, 2);
}

int as_mouse_rate_code(int hz)
{
    /* 1 kHz and below use 1000/hz, higher rates use hz/125. */
    switch (hz) {
    case 125: return 0x08;
    case 250: return 0x04;
    case 500: return 0x02;
    case 1000: return 0x01;
    case 2000: return 0x10;
    case 4000: return 0x20;
    case 8000: return 0x40;
    default: return -1;
    }
}

int as_mouse_rate_from_code(int code)
{
    switch (code) {
    case 0x08: return 125;
    case 0x04: return 250;
    case 0x02: return 500;
    case 0x01: return 1000;
    case 0x10: return 2000;
    case 0x20: return 4000;
    case 0x40: return 8000;
    default: return -1;
    }
}

int as_mouse_set_report_rate(struct as_mouse *m, int hz)
{
    int code = as_mouse_rate_code(hz);
    if (code < 0) {
        return -EINVAL;
    }
    return as_mouse_set_u8(m, AS_M_REPORT_RATE, (uint8_t)code);
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
    unsigned val = rec[0] | (hi << 8);
    unsigned dpi = (val + 1) * 50;
    if (rec[2] & 0x01) {
        dpi *= 2;
    }
    return dpi;
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

int as_mouse_raw(struct as_mouse *m, uint8_t cmd, unsigned addr,
                 const uint8_t *payload, unsigned len, uint8_t reply[16])
{
    uint8_t pkt[PACKET_LEN];

    build_packet(pkt, cmd, addr, payload, len);
    return exchange(m, pkt, reply, 3);
}

int as_mouse_set_light(struct as_mouse *m, uint8_t mode, uint8_t r, uint8_t g,
                       uint8_t b, uint8_t speed, uint8_t brightness, int on)
{
    uint8_t rec[7];
    int ret;

    /*
     * 160 mode | 161 R | 162 G | 163 B | 164 speed | 165 brightness |
     * 166 checksum (85 - sum) | 167 on/off (written as its own pair).
     */
    rec[0] = mode;
    rec[1] = r;
    rec[2] = g;
    rec[3] = b;
    rec[4] = speed;
    rec[5] = brightness;
    rec[6] = (uint8_t)(85 - (mode + r + g + b + speed + brightness));

    ret = as_mouse_write_block(m, AS_M_LIGHT, rec, sizeof(rec));
    if (ret < 0) {
        return ret;
    }
    return as_mouse_set_u8(m, AS_M_LIGHT + 7, on ? 1 : 0);
}

int as_mouse_read_state(struct as_mouse *m, struct as_mouse_state *st)
{
    uint8_t pkt[PACKET_LEN], reply[PACKET_LEN];
    int ret;

    memset(st, 0, sizeof(*st));

    /* Link status and battery are answered by the dongle itself. */
    build_packet(pkt, AS_M_CMD_ONLINE, 0, NULL, 0);
    ret = exchange(m, pkt, reply, 3);
    if (ret < 0) {
        return ret;
    }
    /* Le[1] == 1 means "understood but unsupported". */
    st->online = reply[1] == 0 ? reply[5] : 0;

    build_packet(pkt, AS_M_CMD_VERSION, 0, NULL, 0);
    if (exchange(m, pkt, reply, 3) == 0 && reply[1] == 0) {
        st->version_valid = 1;
        st->version_major = reply[5];
        st->version_minor = reply[6];
    }

    build_packet(pkt, AS_M_CMD_DONGLE_VERSION, 0, NULL, 0);
    if (exchange(m, pkt, reply, 3) == 0 && reply[1] == 0) {
        st->dongle_version_valid = 1;
        st->dongle_version[0] = reply[5];
        st->dongle_version[1] = reply[6];
        st->dongle_version[2] = reply[7];
    }

    build_packet(pkt, AS_M_CMD_GET_CONFIG, 0, NULL, 0);
    if (exchange(m, pkt, reply, 3) == 0 && reply[1] == 0) {
        st->profile = reply[5];
    }
    build_packet(pkt, AS_M_CMD_BATTERY, 0, NULL, 0);
    if (exchange(m, pkt, reply, 3) == 0) {
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
        st->dpi_stages = 0;
        st->report_rate = -1;
        st->lod = -1;
        st->debounce = -1;
        st->motion_sync = -1;
        st->angle_snap = -1;
        st->ripple = -1;
        st->sleep_time = -1;
        st->profile = -1;
        return 0;
    }

    ret = as_mouse_read_block(m, 0, st->raw, AS_MOUSE_CONFIG_LEN);
    if (ret < 0) {
        return ret;
    }
    st->config_valid = 1;

    st->report_rate = as_mouse_rate_from_code(st->raw[AS_M_REPORT_RATE]);
    st->dpi_stages = st->raw[AS_M_MAX_DPI_STAGE];
    if (st->dpi_stages > AS_MAX_DPI_STAGES) {
        st->dpi_stages = AS_MAX_DPI_STAGES;
    }
    st->active_dpi_stage = st->raw[AS_M_CURRENT_DPI];
    for (int i = 0; i < st->dpi_stages; i++) {
        st->dpi[i][0] = as_mouse_decode_dpi(st->raw + AS_M_DPI_VALUE + i * 4);
        st->dpi[i][1] = st->dpi[i][0];
    }
    st->lod = st->raw[AS_M_LOD];
    st->debounce = st->raw[AS_M_DEBOUNCE];
    st->motion_sync = st->raw[AS_M_MOTION_SYNC];
    st->angle_snap = st->raw[AS_M_ANGLE_SNAP];
    st->ripple = st->raw[AS_M_RIPPLE];
    st->sleep_time = st->raw[AS_M_SLEEP_TIME];
    st->sensor_mode = st->raw[AS_M_SENSOR_MODE];
    memcpy(st->light, st->raw + AS_M_LIGHT, 9);
    st->light_mode = st->light[0];
    st->light_color[0] = st->light[1];
    st->light_color[1] = st->light[2];
    st->light_color[2] = st->light[3];
    st->light_speed = st->light[4];
    st->light_brightness = st->light[5];
    st->light_on = st->light[7] == 1;
    return 0;
}
