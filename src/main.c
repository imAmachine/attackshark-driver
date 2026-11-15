// SPDX-License-Identifier: MIT
#define _GNU_SOURCE
#include "attackshark.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

static int json_mode;
static int optional_mode;

static const char *mag_mode_names[] = { "comfort", "sensitive", "gaming", "custom" };

static int parse_bool(const char *s, int *value)
{
    if (!s) {
        return -1;
    }
    if (!strcmp(s, "on") || !strcmp(s, "1") || !strcmp(s, "true") || !strcmp(s, "yes")) {
        *value = 1;
        return 0;
    }
    if (!strcmp(s, "off") || !strcmp(s, "0") || !strcmp(s, "false") || !strcmp(s, "no")) {
        *value = 0;
        return 0;
    }
    return -1;
}

static int parse_color(const char *s, uint8_t *r, uint8_t *g, uint8_t *b)
{
    unsigned v;
    size_t len;

    if (!s) {
        return -1;
    }
    if (s[0] == '#') {
        s++;
    }
    len = strlen(s);
    if (len != 6 || sscanf(s, "%6x", &v) != 1) {
        return -1;
    }
    *r = (uint8_t)(v >> 16);
    *g = (uint8_t)(v >> 8);
    *b = (uint8_t)v;
    return 0;
}

/* Similar to getopt: look up "--name value" or "--name=value". */
static const char *opt_value(int argc, char **argv, const char *name)
{
    size_t len = strlen(name);

    for (int i = 0; i < argc; i++) {
        if (!strncmp(argv[i], name, len)) {
            if (argv[i][len] == '=') {
                return argv[i] + len + 1;
            }
            if (argv[i][len] == 0 && i + 1 < argc) {
                return argv[i + 1];
            }
        }
    }
    return NULL;
}

static int opt_flag(int argc, char **argv, const char *name)
{
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], name)) {
            return 1;
        }
    }
    return 0;
}

/* ------------------------------ list ------------------------------ */

static int cmd_list(void)
{
    struct as_hid found[16];
    int n = as_hid_find(NULL, 0, found, 16);
    int first = 1;
    int bt_hint = 0;
    int configurable_mouse = 0;

    if (json_mode) {
        printf("[");
    }
    for (int i = 0; i < n; i++) {
        const char *kind = NULL;
        const char *product = "unknown";
        int bluetooth = as_hid_is_bluetooth(&found[i]);
        int configurable;

        if (as_hid_is_keyboard(&found[i])) {
            kind = "keyboard";
            product = "R85 HE";
        } else if (as_hid_is_mouse(&found[i])) {
            kind = "mouse";
            product = as_mouse_model_name(found[i].pid);
        } else {
            continue;
        }
        configurable = as_hid_has_vendor(&found[i]);
        if (!configurable && bluetooth) {
            bt_hint = 1;
        }
        if (configurable && !strcmp(kind, "mouse")) {
            configurable_mouse = 1;
        }
        if (json_mode) {
            printf("%s{\"kind\":\"%s\",\"path\":\"%s\",\"vid\":%u,"
                   "\"pid\":%u,\"interface\":%d,\"product\":\"%s\","
                   "\"transport\":\"%s\",\"configurable\":%s}",
                   first ? "" : ",", kind, found[i].path, found[i].vid,
                   found[i].pid, found[i].iface, product,
                   bluetooth ? "bluetooth" : "usb",
                   configurable ? "true" : "false");
        } else {
            printf("%-8s %s  %04x:%04x  iface %d  %-3s %s%s\n", kind,
                   found[i].path, found[i].vid, found[i].pid, found[i].iface,
                   bluetooth ? "bt" : "usb", product,
                   configurable ? "" : "  (settings unavailable)");
        }
        first = 0;
    }
    if (json_mode) {
        printf("]\n");
    } else if (first) {
        printf("no supported Attack Shark device found\n");
        return 1;
    } else if (bt_hint && !configurable_mouse) {
        fprintf(stderr, "note: a Bluetooth link exposes only the standard "
                        "HID collections;\n"
                        "      DPI, polling rate and lighting need the "
                        "2.4 GHz receiver or a cable.\n");
    }
    return 0;
}

/* ------------------------------ mouse ------------------------------ */

static int cmd_mouse(int argc, char **argv)
{
    struct as_mouse m;
    struct as_mouse_state st;
    const char *sub = argc > 0 ? argv[0] : "info";
    int ret;

    ret = as_mouse_open(&m);
    if (ret < 0) {
        struct as_hid bt;

        if (ret == -ENOTSUP && as_mouse_find_bluetooth(&bt)) {
            fprintf(stderr, "attackshark: %s at %s, but %s\n",
                    as_mouse_model_name(bt.pid), bt.path, as_strerror(ret));
        } else if (ret == -ENODEV) {
            fprintf(stderr, "attackshark: mouse not found\n");
        } else {
            fprintf(stderr, "attackshark: %s\n", as_strerror(ret));
        }
        as_mouse_close(&m);
        return 1;
    }

    if (!strcmp(sub, "info")) {
        ret = as_mouse_read_state(&m, &st);
        if (ret < 0) {
            fprintf(stderr, "attackshark: %s\n", as_strerror(ret));
            as_mouse_close(&m);
            return 1;
        }
        if (json_mode) {
            printf("{\"product\":\"%s\",\"vid\":%u,\"pid\":%u,\"cid\":%u,"
                   "\"mid\":%u,\"type\":%u,\"online\":%d,\"config_valid\":%d,"
                   "\"profile\":%d,\"version\":\"%d.%02x\","
                   "\"dongle_version\":\"%d.%d.%d\","
                   "\"battery\":%d,\"charging\":%d,\"battery_voltage\":%d,"
                   "\"report_rate\":%d,\"dpi_stages\":%d,\"active_stage\":%d,"
                   "\"lod\":%d,\"debounce\":%d,\"motion_sync\":%d,"
                   "\"angle_snap\":%d,\"ripple\":%d,\"sleep\":%d,"
                   "\"light\":{\"mode\":%d,\"brightness\":%d,\"speed\":%d,"
                   "\"color\":\"%02x%02x%02x\",\"on\":%d},\"dpi\":[",
                   as_mouse_model_name(m.hid.pid), m.hid.vid, m.hid.pid, m.cid,
                   m.mid, m.type, st.online, st.config_valid, st.profile,
                   st.version_major, st.version_minor, st.dongle_version[0],
                   st.dongle_version[1], st.dongle_version[2], st.battery,
                   st.charging, st.battery_voltage, st.report_rate,
                   st.dpi_stages, st.active_dpi_stage, st.lod, st.debounce,
                   st.motion_sync, st.angle_snap, st.ripple, st.sleep_time,
                   st.light_mode, st.light_brightness, st.light_speed,
                   st.light_color[0], st.light_color[1], st.light_color[2],
                   st.light_on);
            for (int i = 0; i < st.dpi_stages; i++) {
                printf("%s%u", i ? "," : "", st.dpi[i][0]);
            }
            printf("]}\n");
        } else {
            printf("device         %s\n", as_mouse_model_name(m.hid.pid));
            printf("usb            %04x:%04x  %s\n", m.hid.vid, m.hid.pid, m.hid.path);
            printf("device id      cid %u mid %u type %u\n", m.cid, m.mid, m.type);
            if (st.version_valid) {
                printf("firmware       v%d.%02x\n", st.version_major,
                       st.version_minor);
            }
            if (st.dongle_version_valid) {
                printf("dongle         v%d.%d.%d\n", st.dongle_version[0],
                       st.dongle_version[1], st.dongle_version[2]);
            }
            printf("link           %s\n", st.online ? "online" : "offline");
            if (st.battery) {
                printf("battery        %d%%%s", st.battery,
                       st.charging ? " (charging)" : "");
                if (st.battery_voltage) {
                    printf("  %d mV", st.battery_voltage);
                }
                printf("\n");
            }
            if (!st.config_valid) {
                printf("settings       unavailable - turn the mouse on and wake it\n");
            } else {
                printf("profile        %d\n", st.profile + 1);
                printf("report rate    %d Hz\n", st.report_rate);
                printf("dpi stages     %d (active %d)\n", st.dpi_stages,
                       st.active_dpi_stage + 1);
                for (int i = 0; i < st.dpi_stages; i++) {
                    printf("  stage %d      %u dpi%s\n", i + 1, st.dpi[i][0],
                           i == st.active_dpi_stage ? "  <- current" : "");
                }
                printf("lod            %d\n", st.lod);
                printf("debounce       %d ms\n", st.debounce);
                printf("motion sync    %s\n", st.motion_sync ? "on" : "off");
                printf("angle snap     %s\n", st.angle_snap ? "on" : "off");
                printf("ripple         %s\n", st.ripple ? "on" : "off");
                printf("sleep          %d\n", st.sleep_time);
                printf("light          mode %d  %s  %02x%02x%02x  "
                       "brightness %d  speed %d\n", st.light_mode,
                       st.light_on ? "on" : "off", st.light_color[0],
                       st.light_color[1], st.light_color[2],
                       st.light_brightness, st.light_speed);
            }
        }
    } else if (!strcmp(sub, "battery")) {
        ret = as_mouse_read_state(&m, &st);
        if (ret < 0) {
            fprintf(stderr, "attackshark: %s\n", as_strerror(ret));
        } else if (json_mode) {
            printf("{\"battery\":%d,\"charging\":%d,\"voltage\":%d}\n",
                   st.battery, st.charging, st.battery_voltage);
        } else {
            printf("%d%%%s\n", st.battery, st.charging ? " (charging)" : "");
        }
    } else if (!strcmp(sub, "dpi")) {
        ret = as_mouse_read_state(&m, &st);
        if (ret < 0) {
            fprintf(stderr, "attackshark: %s\n", as_strerror(ret));
        } else if (argc > 1 && !strcmp(argv[1], "set")) {
            unsigned stage = 0, dpi = 0;
            if (argc < 4 || sscanf(argv[2], "%u", &stage) != 1 ||
                sscanf(argv[3], "%u", &dpi) != 1) {
                fprintf(stderr, "usage: attackshark mouse dpi set STAGE DPI\n");
                as_mouse_close(&m);
                return 2;
            }
            ret = as_mouse_set_dpi_stage(&m, (int)stage - 1, dpi);
            if (ret < 0) {
                fprintf(stderr, "attackshark: %s\n",
                        ret == -EINVAL ? "dpi must be 50..30000 step 50 or "
                                         "30100..60000 step 100" : as_strerror(ret));
            } else if (!json_mode) {
                printf("stage %u set to %u dpi\n", stage, dpi);
            }
        } else if (!st.config_valid) {
            fprintf(stderr, "attackshark: mouse is offline\n");
            ret = -ENODEV;
        } else {
            for (int i = 0; i < st.dpi_stages; i++) {
                printf("stage %d  %u dpi\n", i + 1, st.dpi[i][0]);
            }
        }
    } else if (!strcmp(sub, "rate")) {
        if (argc > 1 && !strcmp(argv[1], "set")) {
            int hz;
            if (argc < 3 || sscanf(argv[2], "%d", &hz) != 1) {
                fprintf(stderr, "usage: attackshark mouse rate set HZ\n");
                as_mouse_close(&m);
                return 2;
            }
            ret = as_mouse_set_report_rate(&m, hz);
            if (ret < 0) {
                fprintf(stderr, "attackshark: unsupported rate (125/250/500/"
                                "1000/2000/4000/8000)\n");
            } else if (!json_mode) {
                printf("report rate set to %d Hz\n", hz);
            }
        } else {
            ret = as_mouse_read_state(&m, &st);
            if (ret < 0) {
                fprintf(stderr, "attackshark: %s\n", as_strerror(ret));
            } else if (!st.config_valid) {
                fprintf(stderr, "attackshark: mouse is offline\n");
                ret = -ENODEV;
            } else {
                printf("%d Hz\n", st.report_rate);
            }
        }
    } else if (!strcmp(sub, "lod") || !strcmp(sub, "debounce") ||
               !strcmp(sub, "sleep")) {
        unsigned offset = !strcmp(sub, "lod") ? AS_M_LOD :
                          !strcmp(sub, "debounce") ? AS_M_DEBOUNCE :
                          AS_M_SLEEP_TIME;
        int value;
        if (argc < 2 || sscanf(argv[1], "%d", &value) != 1) {
            fprintf(stderr, "usage: attackshark mouse %s VALUE\n", sub);
            as_mouse_close(&m);
            return 2;
        }
        ret = as_mouse_set_u8(&m, offset, (uint8_t)value);
        if (ret < 0) {
            fprintf(stderr, "attackshark: %s\n", as_strerror(ret));
        }
    } else if (!strcmp(sub, "motion-sync") || !strcmp(sub, "angle-snap") ||
               !strcmp(sub, "ripple")) {
        unsigned offset = !strcmp(sub, "motion-sync") ? AS_M_MOTION_SYNC :
                          !strcmp(sub, "angle-snap") ? AS_M_ANGLE_SNAP :
                          AS_M_RIPPLE;
        int value;
        if (argc < 2 || parse_bool(argv[1], &value) < 0) {
            fprintf(stderr, "usage: attackshark mouse %s on|off\n", sub);
            as_mouse_close(&m);
            return 2;
        }
        ret = as_mouse_set_u8(&m, offset, (uint8_t)value);
        if (ret < 0) {
            fprintf(stderr, "attackshark: %s\n", as_strerror(ret));
        }
    } else if (!strcmp(sub, "light")) {
        const char *mode = opt_value(argc, argv, "--mode");
        const char *color = opt_value(argc, argv, "--color");
        const char *bright = opt_value(argc, argv, "--brightness");
        const char *speed = opt_value(argc, argv, "--speed");
        int on = 1, have_on = 0;

        for (int i = 1; i < argc; i++) {
            if (!strcmp(argv[i], "on") || !strcmp(argv[i], "off")) {
                have_on = 1;
                on = argv[i][1] == 'n';
            }
        }
        if (!mode && !color && !bright && !speed && !have_on) {
            ret = as_mouse_read_state(&m, &st);
            if (ret < 0) {
                fprintf(stderr, "attackshark: %s\n", as_strerror(ret));
            } else if (!st.config_valid) {
                fprintf(stderr, "attackshark: mouse is offline\n");
                ret = -ENODEV;
            } else {
                printf("mode        %d\n", st.light_mode);
                printf("color       %02x%02x%02x\n", st.light_color[0],
                       st.light_color[1], st.light_color[2]);
                printf("brightness  %d\n", st.light_brightness);
                printf("speed       %d\n", st.light_speed);
                printf("state       %s\n", st.light_on ? "on" : "off");
            }
        } else {
            uint8_t c[3] = { 0xff, 0, 0 };
            int lm = 1, lb = 5, ls = 3;
            if (color && parse_color(color, &c[0], &c[1], &c[2]) < 0) {
                fprintf(stderr, "attackshark: color must be RRGGBB\n");
                as_mouse_close(&m);
                return 2;
            }
            if (mode) {
                lm = atoi(mode);
            }
            if (bright) {
                lb = atoi(bright);
            }
            if (speed) {
                ls = atoi(speed);
            }
            ret = as_mouse_set_light(&m, (uint8_t)lm, c[0], c[1], c[2],
                                     (uint8_t)ls, (uint8_t)lb, on);
            if (ret < 0) {
                fprintf(stderr, "attackshark: %s\n", as_strerror(ret));
            } else if (!json_mode) {
                printf("light updated\n");
            }
        }
    } else if (!strcmp(sub, "effect")) {
        const char *what = argc > 1 ? argv[1] : NULL;
        unsigned offset;
        int value;
        if (!what) {
            ret = as_mouse_read_state(&m, &st);
            if (ret < 0) {
                fprintf(stderr, "attackshark: %s\n", as_strerror(ret));
            } else if (!st.config_valid) {
                fprintf(stderr, "attackshark: mouse is offline\n");
                ret = -ENODEV;
            } else {
                printf("mode        %u\n", st.raw[AS_M_DPI_EFFECT_MODE]);
                printf("brightness  %u\n", st.raw[AS_M_DPI_EFFECT_BRIGHTNESS]);
                printf("speed       %u\n", st.raw[AS_M_DPI_EFFECT_SPEED]);
                printf("state       %s\n", st.raw[AS_M_DPI_EFFECT_STATE] ? "on" : "off");
            }
        } else if (!strcmp(what, "on") || !strcmp(what, "off")) {
            ret = as_mouse_set_u8(&m, AS_M_DPI_EFFECT_STATE,
                                  (uint8_t)(what[1] == 'n'));
            if (ret < 0) {
                fprintf(stderr, "attackshark: %s\n", as_strerror(ret));
            }
        } else {
            if (argc < 3 || sscanf(argv[2], "%d", &value) != 1) {
                fprintf(stderr, "usage: attackshark mouse effect mode|brightness|"
                                "speed VALUE\n");
                as_mouse_close(&m);
                return 2;
            }
            offset = !strcmp(what, "mode") ? AS_M_DPI_EFFECT_MODE :
                     !strcmp(what, "brightness") ? AS_M_DPI_EFFECT_BRIGHTNESS :
                     !strcmp(what, "speed") ? AS_M_DPI_EFFECT_SPEED : 0;
            if (!offset) {
                fprintf(stderr, "unknown effect field\n");
                as_mouse_close(&m);
                return 2;
            }
            ret = as_mouse_set_u8(&m, offset, (uint8_t)value);
            if (ret < 0) {
                fprintf(stderr, "attackshark: %s\n", as_strerror(ret));
            }
        }
    } else if (!strcmp(sub, "dump")) {
        unsigned addr = 0, len = AS_MOUSE_CONFIG_LEN;
        uint8_t buf[AS_MOUSE_CONFIG_LEN];
        if (argc > 1) {
            sscanf(argv[1], "%x", &addr);
        }
        if (argc > 2) {
            sscanf(argv[2], "%x", &len);
        }
        if (len > sizeof(buf)) {
            len = sizeof(buf);
        }
        ret = as_mouse_read_block(&m, addr, buf, len);
        if (ret < 0) {
            fprintf(stderr, "attackshark: %s\n", as_strerror(ret));
        } else {
            for (unsigned i = 0; i < len; i += 16) {
                printf("%04x:", addr + i);
                for (unsigned j = 0; j < 16 && i + j < len; j++) {
                    printf(" %02x", buf[i + j]);
                }
                printf("\n");
            }
        }
    } else if (!strcmp(sub, "backup") || !strcmp(sub, "restore")) {
        const char *file = argc > 1 ? argv[1] : NULL;
        uint8_t buf[AS_MOUSE_CONFIG_LEN];
        FILE *f;
        if (!file) {
            fprintf(stderr, "usage: attackshark mouse %s FILE\n", sub);
            as_mouse_close(&m);
            return 2;
        }
        if (!strcmp(sub, "backup")) {
            ret = as_mouse_read_block(&m, 0, buf, sizeof(buf));
            if (ret < 0) {
                fprintf(stderr, "attackshark: %s\n", as_strerror(ret));
                as_mouse_close(&m);
                return 1;
            }
            f = fopen(file, "wb");
            if (!f) {
                perror("attackshark");
                as_mouse_close(&m);
                return 1;
            }
            fwrite(buf, 1, sizeof(buf), f);
            fclose(f);
            printf("saved %zu bytes to %s\n", sizeof(buf), file);
        } else {
            f = fopen(file, "rb");
            if (!f) {
                perror("attackshark");
                as_mouse_close(&m);
                return 1;
            }
            if (fread(buf, 1, sizeof(buf), f) != sizeof(buf)) {
                fprintf(stderr, "attackshark: %s must hold %d bytes\n", file,
                        AS_MOUSE_CONFIG_LEN);
                fclose(f);
                as_mouse_close(&m);
                return 1;
            }
            fclose(f);
            ret = as_mouse_write_block(&m, 0, buf, sizeof(buf));
            if (ret < 0) {
                fprintf(stderr, "attackshark: %s\n", as_strerror(ret));
            } else {
                printf("restored %zu bytes from %s\n", sizeof(buf), file);
            }
        }
    } else if (!strcmp(sub, "raw")) {
        /* raw CMD [ADDR] [LEN] [BYTE...] */
        unsigned cmd, addr = 0, len = 0;
        uint8_t payload[10] = { 0 };
        uint8_t reply[16];
        if (argc < 2) {
            fprintf(stderr, "usage: attackshark mouse raw CMD [ADDR] [LEN] "
                            "[BYTES...]\n");
            as_mouse_close(&m);
            return 2;
        }
        sscanf(argv[1], "%x", &cmd);
        if (argc > 2) sscanf(argv[2], "%x", &addr);
        if (argc > 3) sscanf(argv[3], "%x", &len);
        for (int i = 4; i < argc && i - 4 < 10; i++) {
            unsigned b;
            sscanf(argv[i], "%x", &b);
            payload[i - 4] = (uint8_t)b;
        }
        ret = as_mouse_raw(&m, (uint8_t)cmd, addr, payload, len, reply);
        if (ret < 0) {
            fprintf(stderr, "attackshark: %s\n", as_strerror(ret));
        } else {
            for (int i = 0; i < 16; i++) {
                printf("%02x ", reply[i]);
            }
            printf("\n");
        }
    } else {
        fprintf(stderr, "unknown mouse command: %s\n", sub);
        as_mouse_close(&m);
        return 2;
    }

    as_mouse_close(&m);
    return ret < 0 ? 1 : 0;
}

/* ---------------------------- keyboard ---------------------------- */

static void print_kb_light(const struct as_kb_state *st)
{
    const struct as_kb_light *l = &st->light;

    printf("state       %s\n", st->light_on == 0 ? "off" :
                               st->light_on > 0 ? "on" : "unknown");
    printf("mode        %d (%s)\n", l->mode, as_light_mode_name(l->mode));
    printf("brightness  %d\n", l->brightness);
    printf("speed       %d\n", l->speed);
    printf("color       %02x%02x%02x\n", l->r, l->g, l->b);
    printf("option      %d\n", l->option);
    printf("dazzle      %s\n", l->dazzle ? "on" : "off");
}

static void print_kb_side_light(const struct as_kb_light *l)
{
    printf("side mode   %d (%s)\n", l->mode, as_light_mode_name(l->mode));
    printf("side bright %d\n", l->brightness);
    printf("side speed  %d\n", l->speed);
    printf("side color  %02x%02x%02x\n", l->r, l->g, l->b);
}

static volatile sig_atomic_t stop_stream;

static void on_sigint(int sig)
{
    (void)sig;
    stop_stream = 1;
}

static void stream_analog(struct as_keyboard *kb, int seconds)
{
    struct sigaction sa = { 0 };
    unsigned char buf[64];
    long deadline = 0;

    sa.sa_handler = on_sigint;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    if (as_keyboard_enable_stream(kb, 1) < 0) {
        fprintf(stderr, "attackshark: cannot enable the analog stream\n");
        return;
    }
    if (as_keyboard_open_analog(kb) < 0) {
        fprintf(stderr, "attackshark: analog interface not found\n");
        as_keyboard_enable_stream(kb, 0);
        return;
    }

    if (seconds > 0) {
        deadline = (long)seconds * 1000;
    }
    printf("watching key travel, press Ctrl-C to stop\n");
    fflush(stdout);

    while (!stop_stream) {
        int ret = as_hid_read(&kb->analog, buf, sizeof(buf), 500);
        if (ret == -ETIMEDOUT) {
            if (deadline && (deadline -= 500) <= 0) {
                break;
            }
            continue;
        }
        if (ret < 0) {
            break;
        }
        if (ret < 2 || buf[0] != 0x05) {
            continue;
        }
        for (int i = 1; i + 3 < ret; i++) {
            if (buf[i] == 0x1b) {
                int raw = buf[i + 1] | (buf[i + 2] << 8);
                int key = buf[i + 3];
                printf("key %3d  raw %4d  %.2f mm\n", key, raw,
                       raw / 350.0 * 3.4);
                fflush(stdout);
                i += 3;
            }
        }
    }
    as_keyboard_enable_stream(kb, 0);
}

static int cmd_keyboard(int argc, char **argv)
{
    struct as_keyboard kb;
    const char *sub = argc > 0 ? argv[0] : "info";
    struct as_kb_state st;
    int ret = 0;

    if (as_keyboard_open(&kb) < 0) {
        fprintf(stderr, "attackshark: keyboard not found\n");
        return 1;
    }

    if (!strcmp(sub, "info")) {
        ret = as_keyboard_read_state(&kb, &st);
        if (ret == -ETIMEDOUT && !json_mode) {
            printf("device         R85 HE\n");
            printf("usb            %04x:%04x  %s\n", kb.hid.vid, kb.hid.pid,
                   kb.hid.path);
            printf("settings       no answer from the config interface\n");
            fprintf(stderr, "attackshark: the keyboard's configuration endpoint "
                            "did not answer.\n"
                            "            Re-plug the keyboard (or close apps "
                            "that keep it open) and retry.\n");
            as_keyboard_close(&kb);
            return 1;
        }
        if (json_mode) {
            printf("{\"product\":\"R85 HE\",\"vid\":%u,\"pid\":%u,"
                   "\"usb_version\":%u,\"revision\":%d,\"profile\":%d,"
                   "\"report_rate\":%d,\"debounce\":%d,\"mag_mode\":%d,"
                   "\"light\":{",
                   kb.hid.vid, kb.hid.pid,
                   st.usb_version_valid ? st.usb_version : 0,
                   st.revision, st.profile, st.report_rate, st.debounce,
                   st.mag_mode);
            if (st.light.valid) {
                printf("\"on\":%d,\"mode\":%d,\"brightness\":%d,\"speed\":%d,"
                       "\"color\":\"%02x%02x%02x\",\"dazzle\":%d",
                       st.light_on, st.light.mode, st.light.brightness,
                       st.light.speed, st.light.r, st.light.g, st.light.b,
                       st.light.dazzle);
            }
            if (st.side_light.valid) {
                printf("},\"side_light\":{\"mode\":%d,\"brightness\":%d,"
                       "\"speed\":%d,\"color\":\"%02x%02x%02x\","
                       "\"dazzle\":%d",
                       st.side_light.mode, st.side_light.brightness,
                       st.side_light.speed, st.side_light.r, st.side_light.g,
                       st.side_light.b, st.side_light.dazzle);
            }
            printf("}}\n");
        } else {
            printf("device         R85 HE\n");
            printf("usb            %04x:%04x  %s\n", kb.hid.vid, kb.hid.pid,
                   kb.hid.path);
            if (st.usb_version_valid) {
                printf("firmware       %u\n", st.usb_version);
            }
            if (st.revision_valid) {
                printf("revision       %d\n", st.revision);
            }
            printf("profile        %d\n", st.profile);
            printf("report rate    %d Hz\n", st.report_rate);
            printf("debounce       %d ms\n", st.debounce);
            if (st.mag_mode >= 0) {
                printf("magnet mode    %s\n", mag_mode_names[st.mag_mode & 3]);
            }
            if (st.light.valid) {
                printf("light\n");
                printf("  state        %s\n", st.light_on == 0 ? "off" :
                                              st.light_on > 0 ? "on" :
                                                               "unknown");
                printf("  mode         %d (%s)\n", st.light.mode,
                       as_light_mode_name(st.light.mode));
                printf("  brightness   %d\n", st.light.brightness);
                printf("  speed        %d\n", st.light.speed);
                printf("  color        %02x%02x%02x\n", st.light.r, st.light.g,
                       st.light.b);
                if (st.side_light.valid) {
                    printf("side light\n");
                    printf("  mode         %d (%s)\n", st.side_light.mode,
                           as_light_mode_name(st.side_light.mode));
                    printf("  brightness   %d\n", st.side_light.brightness);
                    printf("  speed        %d\n", st.side_light.speed);
                    printf("  color        %02x%02x%02x\n", st.side_light.r,
                           st.side_light.g, st.side_light.b);
                }
            }
        }
    } else if (!strcmp(sub, "light")) {
        const char *mode = opt_value(argc, argv, "--mode");
        const char *color = opt_value(argc, argv, "--color");
        const char *brightness = opt_value(argc, argv, "--brightness");
        const char *speed = opt_value(argc, argv, "--speed");
        const char *option = opt_value(argc, argv, "--option");
        int have_power = 0, power_on = 0;

        for (int i = 1; i < argc; i++) {
            if (!strcmp(argv[i], "on") || !strcmp(argv[i], "off")) {
                have_power = 1;
                power_on = argv[i][1] == 'n';
            }
        }

        if (!mode && !color && !brightness && !speed && !option && !have_power) {
            ret = as_keyboard_read_state(&kb, &st);
            if (ret == 0 && st.light.valid) {
                if (json_mode) {
                    printf("{\"on\":%d,\"mode\":%d,\"name\":\"%s\","
                           "\"brightness\":%d,\"speed\":%d,"
                           "\"color\":\"%02x%02x%02x\",\"option\":%d,"
                           "\"dazzle\":%d}\n",
                           st.light_on, st.light.mode,
                           as_light_mode_name(st.light.mode),
                           st.light.brightness, st.light.speed, st.light.r,
                           st.light.g, st.light.b, st.light.option,
                           st.light.dazzle);
                } else {
                    print_kb_light(&st);
                }
            } else {
                fprintf(stderr, "attackshark: %s\n", as_strerror(ret));
            }
        } else {
            struct as_kb_light l = { .valid = 1, .mode = 1, .speed = 2,
                                     .brightness = 4, .option = 0, .dazzle = 1,
                                     .r = 0xff, .g = 0, .b = 0 };
            if (mode) {
                int m = as_light_mode_by_name(mode);
                if (m < 0) {
                    fprintf(stderr, "attackshark: unknown light mode '%s'\n", mode);
                    as_keyboard_close(&kb);
                    return 2;
                }
                l.mode = (uint8_t)m;
            }
            if (color && parse_color(color, &l.r, &l.g, &l.b) < 0) {
                fprintf(stderr, "attackshark: color must be RRGGBB\n");
                as_keyboard_close(&kb);
                return 2;
            }
            if (brightness) {
                l.brightness = atoi(brightness);
            }
            if (speed) {
                l.speed = atoi(speed);
            }
            if (option) {
                l.option = atoi(option);
            }
            if (opt_flag(argc, argv, "--no-dazzle")) {
                l.dazzle = 0;
            }
            ret = as_keyboard_set_light(&kb, &l);
            if (ret == 0 && have_power) {
                ret = as_keyboard_set_light_power(&kb, power_on);
            }
            if (ret < 0) {
                fprintf(stderr, "attackshark: %s\n", as_strerror(ret));
            } else if (!json_mode) {
                printf("light updated\n");
            }
        }
    } else if (!strcmp(sub, "side-light")) {
        const char *mode = opt_value(argc, argv, "--mode");
        const char *color = opt_value(argc, argv, "--color");
        const char *brightness = opt_value(argc, argv, "--brightness");
        const char *speed = opt_value(argc, argv, "--speed");

        ret = as_keyboard_read_state(&kb, &st);
        if (ret < 0 || !st.side_light.valid) {
            fprintf(stderr, "attackshark: side light is not available\n");
            as_keyboard_close(&kb);
            return 1;
        }
        if (!mode && !color && !brightness && !speed) {
            if (json_mode) {
                printf("{\"mode\":%d,\"name\":\"%s\",\"brightness\":%d,"
                       "\"speed\":%d,\"color\":\"%02x%02x%02x\","
                       "\"dazzle\":%d}\n",
                       st.side_light.mode,
                       as_light_mode_name(st.side_light.mode),
                       st.side_light.brightness, st.side_light.speed,
                       st.side_light.r, st.side_light.g, st.side_light.b,
                       st.side_light.dazzle);
            } else {
                print_kb_side_light(&st.side_light);
            }
        } else {
            struct as_kb_light l = st.side_light;

            if (mode) {
                int m = as_light_mode_by_name(mode);
                if (m < 0) {
                    fprintf(stderr, "attackshark: unknown light mode '%s'\n",
                            mode);
                    as_keyboard_close(&kb);
                    return 2;
                }
                l.mode = (uint8_t)m;
            }
            if (color && parse_color(color, &l.r, &l.g, &l.b) < 0) {
                fprintf(stderr, "attackshark: color must be RRGGBB\n");
                as_keyboard_close(&kb);
                return 2;
            }
            if (brightness) {
                l.brightness = atoi(brightness);
            }
            if (speed) {
                l.speed = atoi(speed);
            }
            if (opt_flag(argc, argv, "--no-dazzle")) {
                l.dazzle = 0;
            }
            ret = as_keyboard_set_sidelight(&kb, &l);
            if (ret < 0) {
                fprintf(stderr, "attackshark: %s\n", as_strerror(ret));
            } else if (!json_mode) {
                printf("side light updated\n");
            }
        }
    } else if (!strcmp(sub, "rate")) {
        int hz;
        if (argc < 2 || sscanf(argv[1], "%d", &hz) != 1) {
            fprintf(stderr, "usage: attackshark keyboard rate HZ\n");
            as_keyboard_close(&kb);
            return 2;
        }
        ret = as_keyboard_set_report_rate(&kb, hz);
        if (ret < 0) {
            fprintf(stderr, "attackshark: unsupported rate\n");
        } else if (!json_mode) {
            printf("report rate set to %d Hz\n", hz);
        }
    } else if (!strcmp(sub, "profile")) {
        int profile;
        if (argc < 2 || sscanf(argv[1], "%d", &profile) != 1) {
            fprintf(stderr, "usage: attackshark keyboard profile N\n");
            as_keyboard_close(&kb);
            return 2;
        }
        ret = as_keyboard_set_profile(&kb, profile);
        if (ret < 0) {
            fprintf(stderr, "attackshark: %s\n", as_strerror(ret));
        }
    } else if (!strcmp(sub, "debounce")) {
        int ms;
        if (argc < 2 || sscanf(argv[1], "%d", &ms) != 1) {
            fprintf(stderr, "usage: attackshark keyboard debounce MS\n");
            as_keyboard_close(&kb);
            return 2;
        }
        ret = as_keyboard_set_debounce(&kb, ms);
        if (ret < 0) {
            fprintf(stderr, "attackshark: %s\n", as_strerror(ret));
        }
    } else if (!strcmp(sub, "magmode")) {
        int mode = -1;
        if (argc > 1) {
            for (int i = 0; i < 4; i++) {
                if (!strcmp(argv[1], mag_mode_names[i])) {
                    mode = i;
                }
            }
            if (mode < 0) {
                mode = atoi(argv[1]);
            }
        }
        if (mode < 0 || mode > 3) {
            fprintf(stderr, "usage: attackshark keyboard magmode "
                            "comfort|sensitive|gaming|custom\n");
            as_keyboard_close(&kb);
            return 2;
        }
        ret = as_keyboard_set_mag_mode(&kb, mode);
        if (ret < 0) {
            fprintf(stderr, "attackshark: %s\n", as_strerror(ret));
        }
    } else if (!strcmp(sub, "travel")) {
        const char *act = opt_value(argc, argv, "--actuation");
        const char *rel = opt_value(argc, argv, "--release");
        const char *rtp = opt_value(argc, argv, "--rt-press");
        const char *rtr = opt_value(argc, argv, "--rt-release");
        const char *dz = opt_value(argc, argv, "--deadzone");
        int rt = opt_flag(argc, argv, "--rt") || opt_flag(argc, argv, "--rapid-trigger");
        double actuation = act ? atof(act) : 1.5;
        double release = rel ? atof(rel) : actuation;
        double rt_press = rtp ? atof(rtp) : 0.2;
        double rt_release = rtr ? atof(rtr) : rt_press;
        double deadzone = dz ? atof(dz) : 0.0;

        if (!act && !rel && !rtp && !rtr && !dz && !rt) {
            int32_t act_v[128], rel_v[128], dz_v[128], opt_v[128];
            int r0 = as_keyboard_get_param(&kb, AS_K_MAG_ACTUATION, act_v, 128);
            int r1 = as_keyboard_get_param(&kb, AS_K_MAG_RELEASE, rel_v, 128);
            int r2 = as_keyboard_get_param(&kb, AS_K_MAG_DEADZONE, dz_v, 128);
            int r3 = as_keyboard_get_param(&kb, AS_K_MAG_OPTION, opt_v, 128);
            if (r0 || r1 || r2 || r3) {
                fprintf(stderr, "attackshark: cannot read travel settings\n");
                as_keyboard_close(&kb);
                return 1;
            }
            printf("actuation   %.2f mm\n",
                   as_kb_value_to_mm(&kb, AS_K_MAG_ACTUATION, act_v[0]));
            printf("release     %.2f mm\n",
                   as_kb_value_to_mm(&kb, AS_K_MAG_RELEASE, rel_v[0]));
            printf("deadzone    %.2f mm\n",
                   as_kb_value_to_mm(&kb, AS_K_MAG_DEADZONE, dz_v[0]));
            printf("rapid trig  %s\n", (opt_v[0] & 0x80) ? "on" : "off");
            as_keyboard_close(&kb);
            return 0;
        }
        if (actuation < 0.1 || actuation > 4.0 || release < 0.1 ||
            release > 4.0 || deadzone < 0.0 || deadzone > 1.0) {
            fprintf(stderr, "attackshark: travel values out of range\n");
            as_keyboard_close(&kb);
            return 2;
        }
        ret = as_keyboard_set_travel(&kb, rt, actuation, release, rt_press,
                                     rt_release, deadzone);
        if (ret < 0) {
            fprintf(stderr, "attackshark: %s\n", as_strerror(ret));
        } else if (!json_mode) {
            printf("travel applied to all keys (actuation %.2f mm, "
                   "rapid trigger %s)\n", actuation, rt ? "on" : "off");
        }
    } else if (!strcmp(sub, "multi-set")) {
        int param;
        long value;
        int32_t values[128];
        if (argc < 3 || sscanf(argv[1], "%d", &param) != 1 ||
            sscanf(argv[2], "%ld", &value) != 1) {
            fprintf(stderr, "usage: attackshark keyboard multi-set PARAM VALUE "
                            "[COUNT]\n");
            as_keyboard_close(&kb);
            return 2;
        }
        unsigned count = argc > 3 ? (unsigned)atoi(argv[3]) : 128;
        if (count > sizeof(values)) {
            count = sizeof(values);
        }
        for (unsigned i = 0; i < count; i++) {
            values[i] = (int32_t)value;
        }
        ret = as_keyboard_set_param(&kb, param, values, count, 1);
        if (ret < 0) {
            fprintf(stderr, "attackshark: %s\n", as_strerror(ret));
        } else if (!json_mode) {
            printf("param %d set to %ld for %u keys\n", param, value, count);
        }
    } else if (!strcmp(sub, "multi")) {
        int param;
        unsigned count = 128;
        int32_t values[128];
        if (argc < 2 || sscanf(argv[1], "%d", &param) != 1) {
            fprintf(stderr, "usage: attackshark keyboard multi PARAM [COUNT]\n");
            as_keyboard_close(&kb);
            return 2;
        }
        if (argc > 2) {
            count = (unsigned)atoi(argv[2]);
        }
        if (count > sizeof(values)) {
            count = sizeof(values);
        }
        ret = as_keyboard_get_param(&kb, param, values, count);
        if (ret < 0) {
            fprintf(stderr, "attackshark: %s\n", as_strerror(ret));
        } else {
            int is_option = param == AS_K_MAG_OPTION ||
                            param == AS_K_MAG_SOCD_PAIR;
            for (unsigned i = 0; i < count; i++) {
                if (is_option) {
                    printf("%3u: %3d\n", i, values[i]);
                } else {
                    printf("%3u: %5d (%.2f mm)\n", i, values[i],
                           as_kb_value_to_mm(&kb, param, values[i]));
                }
            }
        }
    } else if (!strcmp(sub, "stream")) {
        const char *secs = opt_value(argc, argv, "--seconds");
        stream_analog(&kb, secs ? atoi(secs) : 0);
    } else if (!strcmp(sub, "reset")) {
        ret = as_keyboard_cmd(&kb, AS_K_RESET, NULL, 0, 0, NULL, 0);
        if (ret == 0) {
            sleep(3);
        }
        if (ret < 0) {
            fprintf(stderr, "attackshark: %s\n", as_strerror(ret));
        } else {
            printf("factory reset sent\n");
        }
    } else {
        fprintf(stderr, "unknown keyboard command: %s\n", sub);
        as_keyboard_close(&kb);
        return 2;
    }

    as_keyboard_close(&kb);
    return ret < 0 ? 1 : 0;
}

/* ------------------------------ apply ------------------------------ */

static int run_argv(int argc, char **argv);

static int cmd_apply(const char *file)
{
    FILE *f = fopen(file, "r");
    char line[512];
    int ret = 0;

    if (!f) {
        perror("attackshark");
        return 1;
    }
    while (fgets(line, sizeof(line), f)) {
        char *argv[16];
        int argc = 0;
        char *save = NULL;
        char *tok;

        line[strcspn(line, "#\n")] = 0;
        for (tok = strtok_r(line, " \t\r\n", &save);
             tok && argc < 16;
             tok = strtok_r(NULL, " \t\r\n", &save)) {
            argv[argc++] = tok;
        }
        if (argc == 0) {
            continue;
        }
        if (!strcmp(argv[0], "apply")) {
            fprintf(stderr, "attackshark: nested apply is not allowed\n");
            ret = 1;
            continue;
        }
        if (run_argv(argc, argv) != 0) {
            if (optional_mode) {
                fprintf(stderr, "attackshark: warning: '%s' failed\n", argv[0]);
            } else {
                ret = 1;
            }
        }
    }
    fclose(f);
    return ret;
}

/* ------------------------------ main ------------------------------ */

static void usage(void)
{
    printf(
        "usage: attackshark [--json] [--optional] COMMAND\n"
        "\n"
        "  list                        show detected devices\n"
        "\n"
        "  mouse info                  device, battery, dpi, settings\n"
        "  mouse battery               battery level\n"
        "  mouse dpi [set STAGE DPI]   list or set dpi stages\n"
        "  mouse rate [set HZ]         polling rate\n"
        "  mouse lod N                 lift-off distance\n"
        "  mouse debounce N            debounce time (ms)\n"
        "  mouse motion-sync on|off    motion sync\n"
        "  mouse angle-snap on|off     angle snapping\n"
        "  mouse ripple on|off         ripple control\n"
        "  mouse sleep N               sleep time\n"
        "  mouse light [--mode N] [--color RRGGBB] [--brightness N]\n"
        "              [--speed N] [on|off]\n"
        "  mouse effect [FIELD N]      dpi indicator effect\n"
        "  mouse dump [ADDR LEN]       hex dump of the config block\n"
        "  mouse backup FILE           save the 256-byte config block\n"
        "  mouse restore FILE          restore the config block\n"
        "\n"
        "  keyboard info               firmware, profile, light, rate\n"
        "  keyboard light [--mode M] [--brightness 0-4] [--speed 0-4]\n"
        "                [--color RRGGBB] [--option N] [--no-dazzle] [on|off]\n"
        "  keyboard side-light [--mode M] [--brightness 0-4] [--speed 0-4]\n"
        "                      [--color RRGGBB] [--no-dazzle]\n"
        "  keyboard rate HZ            polling rate (125..8000)\n"
        "  keyboard profile N          active profile\n"
        "  keyboard debounce MS        debounce time\n"
        "  keyboard magmode MODE       comfort|sensitive|gaming|custom\n"
        "  keyboard travel [--actuation MM] [--release MM] [--rt]\n"
        "                  [--rt-press MM] [--rt-release MM] [--deadzone MM]\n"
        "  keyboard multi PARAM [N]    raw actuation table read\n"
        "  keyboard stream [--seconds N]  live key travel monitor\n"
        "  keyboard reset              factory reset\n"
        "\n"
        "  apply FILE                  run commands from FILE\n"
        "\n"
        "Light modes: ");
    for (int i = 0; i < as_light_mode_count(); i++) {
        printf("%s%s", i ? ", " : "", as_light_mode_name(i));
    }
    printf("\n");
}

static int run_argv(int argc, char **argv)
{
    if (argc == 0) {
        usage();
        return 2;
    }
    if (!strcmp(argv[0], "list")) {
        return cmd_list();
    }
    if (!strcmp(argv[0], "mouse")) {
        return cmd_mouse(argc - 1, argv + 1);
    }
    if (!strcmp(argv[0], "keyboard")) {
        return cmd_keyboard(argc - 1, argv + 1);
    }
    if (!strcmp(argv[0], "apply")) {
        if (argc < 2) {
            fprintf(stderr, "usage: attackshark apply FILE\n");
            return 2;
        }
        return cmd_apply(argv[1]);
    }
    if (!strcmp(argv[0], "help") || !strcmp(argv[0], "--help") ||
        !strcmp(argv[0], "-h")) {
        usage();
        return 0;
    }
    fprintf(stderr, "attackshark: unknown command '%s'\n", argv[0]);
    return 2;
}

int main(int argc, char **argv)
{
    int i = 1;

    while (i < argc) {
        if (!strcmp(argv[i], "--json")) {
            json_mode = 1;
            i++;
        } else if (!strcmp(argv[i], "--optional")) {
            optional_mode = 1;
            i++;
        } else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            usage();
            return 0;
        } else {
            break;
        }
    }
    return run_argv(argc - i, argv + i);
}
