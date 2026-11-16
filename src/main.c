// SPDX-License-Identifier: MIT
#define _GNU_SOURCE
#include "internal.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int json_mode;
static int optional_mode;

/* The driver installs its own udev rules; point the user at that instead of
 * asking them to copy a file into /etc by hand. */
static void note_udev(void)
{
    fprintf(stderr,
            "            run \"sudo attackshark permissions install\" to grant "
            "access,\n"
            "            or press \"Исправить\" in attackshark-gui\n");
}

static void report(int ret)
{
    fprintf(stderr, "attackshark: %s\n", as_strerror(ret));
}

static const char *jbool(int value)
{
    return value ? "true" : "false";
}

/* Key names contain a backslash, which JSON would swallow. */
static const char *json_escape(const char *in, char *buf, size_t len)
{
    size_t o = 0;

    if (!len) {
        return buf;
    }
    for (; *in; in++) {
        unsigned char ch = (unsigned char)*in;
        size_t need = ch < 0x20 ? 6 : (ch == '\\' || ch == '"') ? 2 : 1;

        if (need >= len - o) {
            break;
        }
        if (ch < 0x20) {
            snprintf(buf + o, len - o, "\\u%04x", ch);
            o += 6;
        } else {
            if (need == 2) {
                buf[o++] = '\\';
            }
            buf[o++] = (char)ch;
        }
    }
    buf[o] = 0;
    return buf;
}

static void json_file_result(const char *file, size_t bytes)
{
    printf("{\"file\":\"");
    for (; *file; file++) {
        char input[2] = { *file, 0 }, escaped[7];

        fputs(json_escape(input, escaped, sizeof(escaped)), stdout);
    }
    printf("\",\"bytes\":%zu}\n", bytes);
}

static int parse_bool(const char *s, int *value)
{
    if (!s) {
        return -1;
    }
    if (!strcmp(s, "on") || !strcmp(s, "1") || !strcmp(s, "true") ||
        !strcmp(s, "yes")) {
        *value = 1;
        return 0;
    }
    if (!strcmp(s, "off") || !strcmp(s, "0") || !strcmp(s, "false") ||
        !strcmp(s, "no")) {
        *value = 0;
        return 0;
    }
    return -1;
}

static int parse_color(const char *s, uint8_t *r, uint8_t *g, uint8_t *b)
{
    unsigned v;

    if (!s) {
        return -1;
    }
    if (*s == '#') {
        s++;
    }
    if (strlen(s) != 6 || sscanf(s, "%6x", &v) != 1) {
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

/* "on"/"off" is a positional argument, unlike the --options. */
static int light_power_arg(int argc, char **argv, int *on)
{
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "on") || !strcmp(argv[i], "off")) {
            *on = argv[i][1] == 'n';
            return 1;
        }
    }
    return 0;
}

/* ------------------------------ list ------------------------------ */

static int cmd_list(void)
{
    struct as_hid found[16];
    /* One entry per physical device: a composite keyboard such as the R85 HE
     * publishes three hidraw nodes, and only one of them has settings. */
    int n = as_hid_list_devices(found, 16);
    int first = 1, bt_hint = 0, configurable_mouse = 0, denied = 0;

    if (json_mode) {
        printf("[");
    }
    for (int i = 0; i < n; i++) {
        const struct as_hid *dev = &found[i];
        int bluetooth = as_hid_is_bluetooth(dev);
        int configurable = as_hid_has_vendor(dev);
        int accessible = access(dev->path, R_OK | W_OK) == 0;
        const char *kind, *product;

        if (as_hid_is_keyboard(dev)) {
            kind = "keyboard";
            product = "R85 HE";
        } else if (as_hid_is_mouse(dev)) {
            kind = "mouse";
            product = as_mouse_model_name(dev->pid);
        } else {
            continue;
        }
        bt_hint |= !configurable && bluetooth;
        configurable_mouse |= configurable && !strcmp(kind, "mouse");
        denied += !accessible;
        if (json_mode) {
            printf("%s{\"kind\":\"%s\",\"path\":\"%s\",\"vid\":%u,"
                   "\"pid\":%u,\"interface\":%d,\"product\":\"%s\","
                   "\"transport\":\"%s\",\"configurable\":%s,"
                   "\"accessible\":%s}",
                   first ? "" : ",", kind, dev->path, dev->vid, dev->pid,
                   dev->iface, product, bluetooth ? "bluetooth" : "usb",
                   jbool(configurable), jbool(accessible));
        } else {
            printf("%-8s %s  %04x:%04x  iface %d  %-3s %s%s%s\n", kind,
                   dev->path, dev->vid, dev->pid, dev->iface,
                   bluetooth ? "bt" : "usb", product,
                   configurable ? "" : "  (settings unavailable)",
                   accessible ? "" : "  (no access)");
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
    if (denied) {
        fprintf(stderr, "note: %d device node(s) are not readable by this "
                        "user;\n", denied);
        note_udev();
    }
    return 0;
}

/* --------------------------- permissions --------------------------- */

static int cmd_permissions(int argc, char **argv)
{
    const char *sub = argc > 0 ? argv[0] : "status";
    struct as_hid found[16];
    char err[256] = "";
    int n, denied = 0, ret;

    if (!strcmp(sub, "install")) {
        ret = as_udev_install(err, sizeof(err));
        if (ret < 0) {
            fprintf(stderr, "attackshark: %s\n",
                    err[0] ? err : as_strerror(ret));
            if (ret == -EPERM) {
                fprintf(stderr, "            run \"sudo attackshark "
                                "permissions install\"\n");
            }
            return 1;
        }
        if (json_mode) {
            char escaped[512];

            printf("{\"installed\":\"%s\"}\n",
                   json_escape(as_udev_rule_path(), escaped, sizeof(escaped)));
        } else {
            printf("installed %s\n", as_udev_rule_path());
        }
        return 0;
    }
    if (strcmp(sub, "status")) {
        fprintf(stderr, "usage: attackshark permissions [status|install]\n");
        return 2;
    }

    n = as_hid_list_devices(found, 16);
    for (int i = 0; i < n; i++) {
        denied += access(found[i].path, R_OK | W_OK) != 0;
    }
    if (json_mode) {
        printf("{\"rules_file\":\"%s\",\"installed\":%s,\"root\":%s,"
               "\"devices\":[", as_udev_rule_path(),
               jbool(as_udev_rules_installed()), jbool(as_udev_can_install()));
        for (int i = 0; i < n; i++) {
            printf("%s{\"path\":\"%s\",\"accessible\":%s}", i ? "," : "",
                   found[i].path,
                   jbool(access(found[i].path, R_OK | W_OK) == 0));
        }
        printf("]}\n");
    } else {
        printf("rules file  %s\n", as_udev_rule_path());
        printf("installed   %s\n", as_udev_rules_installed() ? "yes" : "no");
        if (!n) {
            printf("devices     none\n");
        }
        for (int i = 0; i < n; i++) {
            printf("device      %s  %s\n", found[i].path,
                   access(found[i].path, R_OK | W_OK) == 0 ? "ok"
                                                           : "no access");
        }
    }
    if (denied) {
        fprintf(stderr, "note: %d device node(s) are not readable by this "
                        "user;\n", denied);
        note_udev();
    }
    return 0;
}

/* ------------------------------ mouse ------------------------------ */

static int mouse_usage(struct as_mouse *m, const char *what, const char *args)
{
    fprintf(stderr, "usage: attackshark mouse %s %s\n", what, args);
    as_mouse_close(m);
    return 2;
}

/* Reads the settings block; a read error and a sleeping mouse are both
 * reported here, so callers only have to test for 0. */
static int mouse_read_settings(struct as_mouse *m, struct as_mouse_state *st)
{
    int ret = as_mouse_read_state(m, st);

    if (ret < 0) {
        report(ret);
    } else if (!st->config_valid) {
        fprintf(stderr, "attackshark: mouse is offline\n");
        ret = -ENODEV;
    }
    return ret;
}

static void mouse_open_failed(struct as_hid *bluetooth, int ret)
{
    if (ret == -ENOTSUP && as_mouse_find_bluetooth(bluetooth)) {
        fprintf(stderr, "attackshark: %s at %s, but %s\n",
                as_mouse_model_name(bluetooth->pid), bluetooth->path,
                as_strerror(ret));
    } else if (ret == -ENODEV) {
        fprintf(stderr, "attackshark: mouse not found\n");
    } else {
        report(ret);
        if (ret == -EACCES) {
            note_udev();
        }
    }
}

/* Raw settings that are a single byte in the config block. */
static const struct {
    const char *name;
    unsigned offset;
    const char *arg;
    int boolean;
} mouse_u8[] = {
    { "lod", AS_M_LOD, "VALUE", 0 },
    { "debounce", AS_M_DEBOUNCE, "VALUE", 0 },
    { "sleep", AS_M_SLEEP_TIME, "VALUE", 0 },
    { "motion-sync", AS_M_MOTION_SYNC, "on|off", 1 },
    { "angle-snap", AS_M_ANGLE_SNAP, "on|off", 1 },
    { "ripple", AS_M_RIPPLE, "on|off", 1 },
};

static int mouse_u8_index(const char *name)
{
    for (size_t i = 0; i < AS_N(mouse_u8); i++) {
        if (!strcmp(name, mouse_u8[i].name)) {
            return (int)i;
        }
    }
    return -1;
}

static void mouse_info_json(const struct as_mouse *m,
                            const struct as_mouse_state *st)
{
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
           as_mouse_model_name(m->hid.pid), m->hid.vid, m->hid.pid, m->cid,
           m->mid, m->type, st->online, st->config_valid, st->profile,
           st->version_major, st->version_minor, st->dongle_version[0],
           st->dongle_version[1], st->dongle_version[2], st->battery,
           st->charging, st->battery_voltage, st->report_rate, st->dpi_stages,
           st->active_dpi_stage, st->lod, st->debounce, st->motion_sync,
           st->angle_snap, st->ripple, st->sleep_time, st->light_mode,
           st->light_brightness, st->light_speed, st->light_color[0],
           st->light_color[1], st->light_color[2], st->light_on);
    for (int i = 0; i < st->dpi_stages; i++) {
        printf("%s%u", i ? "," : "", st->dpi[i]);
    }
    printf("]}\n");
}

static void mouse_info_text(const struct as_mouse *m,
                            const struct as_mouse_state *st)
{
    printf("device         %s\n", as_mouse_model_name(m->hid.pid));
    printf("usb            %04x:%04x  %s\n", m->hid.vid, m->hid.pid,
           m->hid.path);
    printf("device id      cid %u mid %u type %u\n", m->cid, m->mid, m->type);
    if (st->version_valid) {
        printf("firmware       v%d.%02x\n", st->version_major,
               st->version_minor);
    }
    if (st->dongle_version_valid) {
        printf("dongle         v%d.%d.%d\n", st->dongle_version[0],
               st->dongle_version[1], st->dongle_version[2]);
    }
    printf("link           %s\n", st->online ? "online" : "offline");
    if (st->battery) {
        printf("battery        %d%%%s", st->battery,
               st->charging ? " (charging)" : "");
        if (st->battery_voltage) {
            printf("  %d mV", st->battery_voltage);
        }
        printf("\n");
    }
    if (!st->config_valid) {
        printf("settings       unavailable - turn the mouse on and wake it\n");
        return;
    }
    printf("profile        %d\n", st->profile + 1);
    printf("report rate    %d Hz\n", st->report_rate);
    printf("dpi stages     %d (active %d)\n", st->dpi_stages,
           st->active_dpi_stage + 1);
    for (int i = 0; i < st->dpi_stages; i++) {
        printf("  stage %d      %u dpi%s\n", i + 1, st->dpi[i],
               i == st->active_dpi_stage ? "  <- current" : "");
    }
    printf("lod            %d\n", st->lod);
    printf("debounce       %d ms\n", st->debounce);
    printf("motion sync    %s\n", st->motion_sync ? "on" : "off");
    printf("angle snap     %s\n", st->angle_snap ? "on" : "off");
    printf("ripple         %s\n", st->ripple ? "on" : "off");
    printf("sleep          %d\n", st->sleep_time);
    printf("light          mode %d  %s  %02x%02x%02x  "
           "brightness %d  speed %d\n", st->light_mode,
           st->light_on ? "on" : "off", st->light_color[0],
           st->light_color[1], st->light_color[2], st->light_brightness,
           st->light_speed);
}

static int cmd_mouse(int argc, char **argv)
{
    struct as_mouse m;
    struct as_mouse_state st;
    const char *sub = argc > 0 ? argv[0] : "info";
    int ret, u8;

    ret = as_mouse_open(&m);
    if (ret < 0) {
        struct as_hid bluetooth;

        mouse_open_failed(&bluetooth, ret);
        as_mouse_close(&m);
        return 1;
    }

    if (!strcmp(sub, "info")) {
        ret = as_mouse_read_state(&m, &st);
        if (ret < 0) {
            report(ret);
            as_mouse_close(&m);
            return 1;
        }
        if (json_mode) {
            mouse_info_json(&m, &st);
        } else {
            mouse_info_text(&m, &st);
        }
    } else if (!strcmp(sub, "battery")) {
        ret = as_mouse_read_state(&m, &st);
        if (ret < 0) {
            report(ret);
        } else if (json_mode) {
            printf("{\"battery\":%d,\"charging\":%d,\"voltage\":%d}\n",
                   st.battery, st.charging, st.battery_voltage);
        } else {
            printf("%d%%%s\n", st.battery, st.charging ? " (charging)" : "");
        }
    } else if (!strcmp(sub, "dpi")) {
        if (argc > 1 && !strcmp(argv[1], "set")) {
            unsigned stage, dpi;

            if (argc < 4 || sscanf(argv[2], "%u", &stage) != 1 ||
                sscanf(argv[3], "%u", &dpi) != 1) {
                return mouse_usage(&m, "dpi set", "STAGE DPI");
            }
            ret = as_mouse_set_dpi_stage(&m, (int)stage - 1, dpi);
            if (ret < 0) {
                fprintf(stderr, "attackshark: %s\n",
                        ret == -EINVAL ? "dpi must be 50..30000 step 50 or "
                                         "30100..60000 step 100"
                                       : as_strerror(ret));
            } else if (!json_mode) {
                printf("stage %u set to %u dpi\n", stage, dpi);
            }
        } else if ((ret = mouse_read_settings(&m, &st)) == 0) {
            if (json_mode) {
                printf("{\"dpi_stages\":%d,\"active_stage\":%d,\"dpi\":[",
                       st.dpi_stages, st.active_dpi_stage);
                for (int i = 0; i < st.dpi_stages; i++) {
                    printf("%s%u", i ? "," : "", st.dpi[i]);
                }
                printf("]}\n");
            } else {
                for (int i = 0; i < st.dpi_stages; i++) {
                    printf("stage %d  %u dpi\n", i + 1, st.dpi[i]);
                }
            }
        }
    } else if (!strcmp(sub, "rate")) {
        if (argc > 1 && !strcmp(argv[1], "set")) {
            int hz;

            if (argc < 3 || sscanf(argv[2], "%d", &hz) != 1) {
                return mouse_usage(&m, "rate set", "HZ");
            }
            ret = as_mouse_set_report_rate(&m, hz);
            if (ret < 0) {
                fprintf(stderr, "attackshark: unsupported rate (125/250/500/"
                                "1000/2000/4000/8000)\n");
            } else if (!json_mode) {
                printf("report rate set to %d Hz\n", hz);
            }
        } else if ((ret = mouse_read_settings(&m, &st)) == 0) {
            if (json_mode) {
                printf("{\"report_rate\":%d}\n", st.report_rate);
            } else {
                printf("%d Hz\n", st.report_rate);
            }
        }
    } else if ((u8 = mouse_u8_index(sub)) >= 0) {
        int value = 0;
        int ok = argc > 1 && (mouse_u8[u8].boolean
                                  ? parse_bool(argv[1], &value) == 0
                                  : sscanf(argv[1], "%d", &value) == 1);

        if (!ok) {
            return mouse_usage(&m, sub, mouse_u8[u8].arg);
        }
        ret = as_mouse_set_u8(&m, mouse_u8[u8].offset, (uint8_t)value);
        if (ret < 0) {
            report(ret);
        }
    } else if (!strcmp(sub, "light")) {
        const char *mode = opt_value(argc, argv, "--mode");
        const char *color = opt_value(argc, argv, "--color");
        const char *bright = opt_value(argc, argv, "--brightness");
        const char *speed = opt_value(argc, argv, "--speed");
        int on = 1, have_on = light_power_arg(argc, argv, &on);

        if (!mode && !color && !bright && !speed && !have_on) {
            if ((ret = mouse_read_settings(&m, &st)) == 0) {
                if (json_mode) {
                    printf("{\"mode\":%d,\"color\":\"%02x%02x%02x\","
                           "\"brightness\":%d,\"speed\":%d,\"on\":%d}\n",
                           st.light_mode, st.light_color[0], st.light_color[1],
                           st.light_color[2], st.light_brightness,
                           st.light_speed, st.light_on);
                } else {
                    printf("mode        %d\n", st.light_mode);
                    printf("color       %02x%02x%02x\n", st.light_color[0],
                           st.light_color[1], st.light_color[2]);
                    printf("brightness  %d\n", st.light_brightness);
                    printf("speed       %d\n", st.light_speed);
                    printf("state       %s\n", st.light_on ? "on" : "off");
                }
            }
        } else {
            uint8_t c[3] = { 0xff, 0, 0 };
            int lm = mode ? atoi(mode) : 1;
            int lb = bright ? atoi(bright) : 5;
            int ls = speed ? atoi(speed) : 3;

            if (color && parse_color(color, &c[0], &c[1], &c[2]) < 0) {
                fprintf(stderr, "attackshark: color must be RRGGBB\n");
                as_mouse_close(&m);
                return 2;
            }
            ret = as_mouse_set_light(&m, (uint8_t)lm, c[0], c[1], c[2],
                                     (uint8_t)ls, (uint8_t)lb, on);
            if (ret < 0) {
                report(ret);
            } else if (!json_mode) {
                printf("light updated\n");
            }
        }
    } else if (!strcmp(sub, "effect")) {
        const char *what = argc > 1 ? argv[1] : NULL;

        if (!what) {
            if ((ret = mouse_read_settings(&m, &st)) == 0) {
                if (json_mode) {
                    printf("{\"mode\":%u,\"brightness\":%u,\"speed\":%u,"
                           "\"state\":%s}\n",
                           st.raw[AS_M_DPI_EFFECT_MODE],
                           st.raw[AS_M_DPI_EFFECT_BRIGHTNESS],
                           st.raw[AS_M_DPI_EFFECT_SPEED],
                           jbool(st.raw[AS_M_DPI_EFFECT_STATE]));
                } else {
                    printf("mode        %u\n", st.raw[AS_M_DPI_EFFECT_MODE]);
                    printf("brightness  %u\n",
                           st.raw[AS_M_DPI_EFFECT_BRIGHTNESS]);
                    printf("speed       %u\n", st.raw[AS_M_DPI_EFFECT_SPEED]);
                    printf("state       %s\n",
                           st.raw[AS_M_DPI_EFFECT_STATE] ? "on" : "off");
                }
            }
        } else if (!strcmp(what, "on") || !strcmp(what, "off")) {
            ret = as_mouse_set_u8(&m, AS_M_DPI_EFFECT_STATE,
                                  (uint8_t)(what[1] == 'n'));
            if (ret < 0) {
                report(ret);
            }
        } else {
            int value;
            unsigned offset = !strcmp(what, "mode") ? AS_M_DPI_EFFECT_MODE
                            : !strcmp(what, "brightness")
                                ? AS_M_DPI_EFFECT_BRIGHTNESS
                            : !strcmp(what, "speed") ? AS_M_DPI_EFFECT_SPEED : 0;

            if (argc < 3 || sscanf(argv[2], "%d", &value) != 1) {
                return mouse_usage(&m, "effect",
                                   "mode|brightness|speed VALUE");
            }
            if (!offset) {
                fprintf(stderr, "unknown effect field\n");
                as_mouse_close(&m);
                return 2;
            }
            ret = as_mouse_set_u8(&m, offset, (uint8_t)value);
            if (ret < 0) {
                report(ret);
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
            report(ret);
        } else if (json_mode) {
            printf("{\"addr\":%u,\"len\":%u,\"data\":\"", addr, len);
            for (unsigned i = 0; i < len; i++) {
                printf("%02x", buf[i]);
            }
            printf("\"}\n");
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
        int backup = !strcmp(sub, "backup");
        FILE *f;

        if (!file) {
            return mouse_usage(&m, sub, "FILE");
        }
        if (backup &&
            (ret = as_mouse_read_block(&m, 0, buf, sizeof(buf))) < 0) {
            report(ret);
            as_mouse_close(&m);
            return 1;
        }
        f = fopen(file, backup ? "wb" : "rb");
        if (!f) {
            perror("attackshark");
            as_mouse_close(&m);
            return 1;
        }
        if (backup) {
            fwrite(buf, 1, sizeof(buf), f);
        } else if (fread(buf, 1, sizeof(buf), f) != sizeof(buf)) {
            fprintf(stderr, "attackshark: %s must hold %d bytes\n", file,
                    AS_MOUSE_CONFIG_LEN);
            fclose(f);
            as_mouse_close(&m);
            return 1;
        }
        fclose(f);
        if (backup) {
            if (json_mode) {
                json_file_result(file, sizeof(buf));
            } else {
                printf("saved %zu bytes to %s\n", sizeof(buf), file);
            }
        } else if ((ret = as_mouse_write_block(&m, 0, buf, sizeof(buf))) < 0) {
            report(ret);
        } else if (json_mode) {
            json_file_result(file, sizeof(buf));
        } else {
            printf("restored %zu bytes from %s\n", sizeof(buf), file);
        }
    } else if (!strcmp(sub, "raw")) {
        /* raw CMD [ADDR] [LEN] [BYTE...] */
        unsigned cmd, addr = 0, len = 0;
        uint8_t payload[10] = { 0 }, reply[16];

        if (argc < 2) {
            return mouse_usage(&m, "raw", "CMD [ADDR] [LEN] [BYTES...]");
        }
        sscanf(argv[1], "%x", &cmd);
        if (argc > 2) {
            sscanf(argv[2], "%x", &addr);
        }
        if (argc > 3) {
            sscanf(argv[3], "%x", &len);
        }
        for (int i = 4; i < argc && i - 4 < 10; i++) {
            unsigned b;

            sscanf(argv[i], "%x", &b);
            payload[i - 4] = (uint8_t)b;
        }
        ret = as_mouse_raw(&m, (uint8_t)cmd, addr, payload, len, reply);
        if (ret < 0) {
            report(ret);
        } else if (json_mode) {
            printf("{\"reply\":\"");
            for (int i = 0; i < 16; i++) {
                printf("%02x", reply[i]);
            }
            printf("\"}\n");
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

static int kb_usage(struct as_keyboard *kb, const char *what, const char *args)
{
    fprintf(stderr, "usage: attackshark keyboard %s %s\n", what, args);
    as_keyboard_close(kb);
    return 2;
}

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

static int light_has_opts(int argc, char **argv)
{
    return opt_value(argc, argv, "--mode") ||
           opt_value(argc, argv, "--color") ||
           opt_value(argc, argv, "--brightness") ||
           opt_value(argc, argv, "--speed") ||
           opt_value(argc, argv, "--option");
}

/* Applies the --light options to l; returns -1 and complains on bad input. */
static int parse_light_opts(int argc, char **argv, struct as_kb_light *l)
{
    const char *mode = opt_value(argc, argv, "--mode");
    const char *color = opt_value(argc, argv, "--color");
    const char *brightness = opt_value(argc, argv, "--brightness");
    const char *speed = opt_value(argc, argv, "--speed");
    const char *option = opt_value(argc, argv, "--option");

    if (mode) {
        int m = as_light_mode_by_name(mode);

        if (m < 0) {
            fprintf(stderr, "attackshark: unknown light mode '%s'\n", mode);
            return -1;
        }
        l->mode = (uint8_t)m;
    }
    if (color && parse_color(color, &l->r, &l->g, &l->b) < 0) {
        fprintf(stderr, "attackshark: color must be RRGGBB\n");
        return -1;
    }
    if (brightness) {
        l->brightness = atoi(brightness);
    }
    if (speed) {
        l->speed = atoi(speed);
    }
    if (option) {
        l->option = atoi(option);
    }
    if (opt_flag(argc, argv, "--no-dazzle")) {
        l->dazzle = 0;
    }
    return 0;
}

static void print_kb_light_json(const struct as_kb_state *st)
{
    printf("{\"on\":%d,\"mode\":%d,\"name\":\"%s\",\"brightness\":%d,"
           "\"speed\":%d,\"color\":\"%02x%02x%02x\",\"option\":%d,"
           "\"dazzle\":%d}\n", st->light_on, st->light.mode,
           as_light_mode_name(st->light.mode), st->light.brightness,
           st->light.speed, st->light.r, st->light.g, st->light.b,
           st->light.option, st->light.dazzle);
}

static void kb_info_json(const struct as_keyboard *kb,
                         const struct as_kb_state *st)
{
    printf("{\"product\":\"R85 HE\",\"vid\":%u,\"pid\":%u,"
           "\"usb_version\":%u,\"revision\":%d,\"profile\":%d,"
           "\"report_rate\":%d,\"debounce\":%d,\"mag_mode\":%d,"
           "\"light\":{", kb->hid.vid, kb->hid.pid,
           st->usb_version_valid ? st->usb_version : 0, st->revision,
           st->profile, st->report_rate, st->debounce, st->mag_mode);
    if (st->light.valid) {
        printf("\"on\":%d,\"mode\":%d,\"brightness\":%d,\"speed\":%d,"
               "\"color\":\"%02x%02x%02x\",\"dazzle\":%d", st->light_on,
               st->light.mode, st->light.brightness, st->light.speed,
               st->light.r, st->light.g, st->light.b, st->light.dazzle);
    }
    if (st->side_light.valid) {
        printf("},\"side_light\":{\"mode\":%d,\"brightness\":%d,"
               "\"speed\":%d,\"color\":\"%02x%02x%02x\",\"dazzle\":%d",
               st->side_light.mode, st->side_light.brightness,
               st->side_light.speed, st->side_light.r, st->side_light.g,
               st->side_light.b, st->side_light.dazzle);
    }
    printf("}}\n");
}

static void kb_info_text(const struct as_keyboard *kb,
                         const struct as_kb_state *st)
{
    printf("device         R85 HE\n");
    printf("usb            %04x:%04x  %s\n", kb->hid.vid, kb->hid.pid,
           kb->hid.path);
    if (st->usb_version_valid) {
        printf("firmware       %u\n", st->usb_version);
    }
    if (st->revision_valid) {
        printf("revision       %d\n", st->revision);
    }
    printf("profile        %d\n", st->profile);
    printf("report rate    %d Hz\n", st->report_rate);
    printf("debounce       %d ms\n", st->debounce);
    if (st->mag_mode >= 0) {
        printf("magnet mode    %s\n", as_kb_mag_mode_name(st->mag_mode & 3));
    }
    if (!st->light.valid) {
        return;
    }
    printf("light\n");
    printf("  state        %s\n", st->light_on == 0 ? "off" :
                                  st->light_on > 0 ? "on" : "unknown");
    printf("  mode         %d (%s)\n", st->light.mode,
           as_light_mode_name(st->light.mode));
    printf("  brightness   %d\n", st->light.brightness);
    printf("  speed        %d\n", st->light.speed);
    printf("  color        %02x%02x%02x\n", st->light.r, st->light.g,
           st->light.b);
    if (!st->side_light.valid) {
        return;
    }
    printf("side light\n");
    printf("  mode         %d (%s)\n", st->side_light.mode,
           as_light_mode_name(st->side_light.mode));
    printf("  brightness   %d\n", st->side_light.brightness);
    printf("  speed        %d\n", st->side_light.speed);
    printf("  color        %02x%02x%02x\n", st->side_light.r,
           st->side_light.g, st->side_light.b);
}

static volatile sig_atomic_t stop_stream;

static void on_sigint(int sig)
{
    (void)sig;
    stop_stream = 1;
}

static long long stream_now_ms(void)
{
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);
    return (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static int stream_analog(struct as_keyboard *kb, int seconds)
{
    struct sigaction sa = { 0 }, old_int, old_term;
    unsigned char buf[64];
    long long deadline = seconds > 0 ? stream_now_ms() + (long long)seconds * 1000 : 0;
    int result, cleanup;

    stop_stream = 0;
    sa.sa_handler = on_sigint;
    sigaction(SIGINT, &sa, &old_int);
    sigaction(SIGTERM, &sa, &old_term);

    result = as_keyboard_enable_stream(kb, 1);
    if (result < 0) {
        goto restore_signals;
    }
    result = as_keyboard_open_analog(kb);
    if (result < 0) {
        goto disable_stream;
    }

    if (!json_mode) {
        printf("watching key travel, press Ctrl-C to stop\n");
        fflush(stdout);
    }
    while (!stop_stream) {
        long long left = deadline ? deadline - stream_now_ms() : 500;
        int ret;

        if (left <= 0) {
            break;
        }
        ret = as_hid_read(&kb->analog, buf, sizeof(buf), left < 500 ? (int)left : 500);
        if (ret == -ETIMEDOUT) {
            continue;
        }
        if (ret < 0) {
            if (!stop_stream) {
                result = ret;
            }
            break;
        }
        if (ret < 2 || buf[0] != 0x05) {
            continue;
        }
        for (int i = 1; i + 3 < ret; i++) {
            if (buf[i] == 0x1b) {
                int raw = buf[i + 1] | (buf[i + 2] << 8);

                if (json_mode) {
                    printf("{\"key\":%d,\"raw\":%d,\"mm\":%.2f}\n",
                           buf[i + 3], raw, raw / 200.0);
                } else {
                    printf("key %3d  raw %4d  %.2f mm\n", buf[i + 3], raw,
                           raw / 200.0);
                }
                fflush(stdout);
                i += 3;
            }
        }
    }
disable_stream:
    cleanup = as_keyboard_enable_stream(kb, 0);
    if (result >= 0 && cleanup < 0) {
        result = cleanup;
    }
restore_signals:
    sigaction(SIGINT, &old_int, NULL);
    sigaction(SIGTERM, &old_term, NULL);
    return result < 0 ? result : 0;
}

/* Clamped key count for the raw Hall-effect table commands. */
static unsigned kb_key_count(int argc, char **argv, int index)
{
    unsigned count = argc > index ? (unsigned)atoi(argv[index]) : 128;

    return count > 128 ? 128 : count;
}

static const char *snap_mode_name(int mode)
{
    switch (mode) {
    case AS_KB_SNAP_LAST:
        return "last";
    case AS_KB_SNAP_NEUTRAL:
        return "neutral";
    case AS_KB_SNAP_FIRST:
        return "first";
    case AS_KB_SNAP_SECOND:
        return "second";
    default:
        return "none";
    }
}

static int snap_mode_by_name(const char *name)
{
    if (!strcmp(name, "last") || !strcmp(name, "late")) {
        return AS_KB_SNAP_LAST;
    }
    if (!strcmp(name, "neutral")) {
        return AS_KB_SNAP_NEUTRAL;
    }
    if (!strcmp(name, "first") || !strcmp(name, "key1")) {
        return AS_KB_SNAP_FIRST;
    }
    if (!strcmp(name, "second") || !strcmp(name, "key2")) {
        return AS_KB_SNAP_SECOND;
    }
    return -1;
}

/* Reads the parameters the per-key commands work with. */
static int read_key_tables(struct as_keyboard *kb, int32_t act[AS_KB_KEY_MAX],
                           int32_t rel[AS_KB_KEY_MAX],
                           int32_t rtp[AS_KB_KEY_MAX],
                           int32_t rtr[AS_KB_KEY_MAX],
                           int32_t dz[AS_KB_KEY_MAX],
                           int32_t opt[AS_KB_KEY_MAX],
                           int32_t pair[AS_KB_KEY_MAX])
{
    if (as_keyboard_get_param(kb, AS_K_MAG_ACTUATION, act, AS_KB_KEY_MAX) ||
        as_keyboard_get_param(kb, AS_K_MAG_RELEASE, rel, AS_KB_KEY_MAX) ||
        as_keyboard_get_param(kb, AS_K_MAG_RT_PRESS, rtp, AS_KB_KEY_MAX) ||
        as_keyboard_get_param(kb, AS_K_MAG_RT_RELEASE, rtr, AS_KB_KEY_MAX) ||
        as_keyboard_get_param(kb, AS_K_MAG_DEADZONE, dz, AS_KB_KEY_MAX) ||
        as_keyboard_get_param(kb, AS_K_MAG_OPTION, opt, AS_KB_KEY_MAX) ||
        as_keyboard_get_param(kb, AS_K_MAG_SOCD_PAIR, pair, AS_KB_KEY_MAX)) {
        return -ENODATA;
    }
    return 0;
}

static void print_key_travel(struct as_keyboard *kb, int key,
                             const int32_t *act, const int32_t *rel,
                             const int32_t *rtp, const int32_t *rtr,
                             const int32_t *dz, const uint8_t *opt,
                             const uint8_t *pair)
{
    int mode;

    mode = as_kb_snap_mode_of(opt, pair, key);
    printf("key          %d\n", key);
    printf("actuation    %.2f mm\n",
           as_kb_value_to_mm(kb, AS_K_MAG_ACTUATION, act[key]));
    printf("release      %.2f mm\n",
           as_kb_value_to_mm(kb, AS_K_MAG_RELEASE, rel[key]));
    printf("rt press     %.2f mm\n",
           as_kb_value_to_mm(kb, AS_K_MAG_RT_PRESS, rtp[key]));
    printf("rt release   %.2f mm\n",
           as_kb_value_to_mm(kb, AS_K_MAG_RT_RELEASE, rtr[key]));
    printf("deadzone     %.2f mm\n",
           as_kb_value_to_mm(kb, AS_K_MAG_DEADZONE, dz[key]));
    printf("rapid trig   %s\n",
           (opt[key] & AS_KB_OPTION_RT) ? "on" : "off");
    printf("snap tap     %s", snap_mode_name(mode));
    if (mode >= 0) {
        printf(" (paired with key %d)", pair[key]);
    }
    printf("\n");
}

static int cmd_keyboard(int argc, char **argv)
{
    struct as_keyboard kb;
    const char *sub = argc > 0 ? argv[0] : "info";
    struct as_kb_state st;
    int ret;

    ret = as_keyboard_open(&kb);
    if (ret < 0) {
        struct as_hid bluetooth;

        if (ret == -ENOTSUP && as_keyboard_find_bluetooth(&bluetooth)) {
            fprintf(stderr, "attackshark: R85 HE at %s, but %s\n",
                    bluetooth.path, as_strerror(ret));
        } else if (ret == -ENODEV) {
            fprintf(stderr, "attackshark: keyboard not found\n");
        } else {
            report(ret);
            if (ret == -EACCES) {
                note_udev();
            }
        }
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
            kb_info_json(&kb, &st);
        } else {
            kb_info_text(&kb, &st);
        }
    } else if (!strcmp(sub, "light")) {
        int on = 1;
        int have_power = light_power_arg(argc, argv, &on);

        if (!light_has_opts(argc, argv) && !have_power) {
            ret = as_keyboard_read_state(&kb, &st);
            if (ret == 0 && st.light.valid) {
                if (json_mode) {
                    print_kb_light_json(&st);
                } else {
                    print_kb_light(&st);
                }
            } else {
                report(ret);
            }
        } else {
            struct as_kb_light l = { .valid = 1, .mode = 1, .speed = 2,
                                     .brightness = 4, .dazzle = 1, .r = 0xff };

            if (parse_light_opts(argc, argv, &l) < 0) {
                as_keyboard_close(&kb);
                return 2;
            }
            ret = as_keyboard_set_light(&kb, &l);
            if (ret == 0 && have_power) {
                ret = as_keyboard_set_light_power(&kb, on);
            }
            if (ret < 0) {
                report(ret);
            } else if (!json_mode) {
                printf("light updated\n");
            }
        }
    } else if (!strcmp(sub, "side-light")) {
        ret = as_keyboard_read_state(&kb, &st);
        if (ret < 0 || !st.side_light.valid) {
            fprintf(stderr, "attackshark: side light is not available\n");
            as_keyboard_close(&kb);
            return 1;
        }
        if (!light_has_opts(argc, argv)) {
            if (json_mode) {
                printf("{\"mode\":%d,\"name\":\"%s\",\"brightness\":%d,"
                       "\"speed\":%d,\"color\":\"%02x%02x%02x\","
                       "\"dazzle\":%d}\n", st.side_light.mode,
                       as_light_mode_name(st.side_light.mode),
                       st.side_light.brightness, st.side_light.speed,
                       st.side_light.r, st.side_light.g, st.side_light.b,
                       st.side_light.dazzle);
            } else {
                print_kb_side_light(&st.side_light);
            }
        } else {
            struct as_kb_light l = st.side_light;

            if (parse_light_opts(argc, argv, &l) < 0) {
                as_keyboard_close(&kb);
                return 2;
            }
            ret = as_keyboard_set_sidelight(&kb, &l);
            if (ret < 0) {
                report(ret);
            } else if (!json_mode) {
                printf("side light updated\n");
            }
        }
    } else if (!strcmp(sub, "rate") || !strcmp(sub, "profile") ||
               !strcmp(sub, "debounce")) {
        int value;

        if (argc < 2 || sscanf(argv[1], "%d", &value) != 1) {
            return kb_usage(&kb, sub, sub[0] == 'r' ? "HZ" :
                                        sub[0] == 'p' ? "N" : "MS");
        }
        if (sub[0] == 'r') {
            ret = as_keyboard_set_report_rate(&kb, value);
            if (ret < 0) {
                fprintf(stderr, "attackshark: unsupported rate\n");
            } else if (!json_mode) {
                printf("report rate set to %d Hz\n", value);
            }
        } else {
            ret = sub[0] == 'p' ? as_keyboard_set_profile(&kb, value)
                                : as_keyboard_set_debounce(&kb, value);
            if (ret < 0) {
                report(ret);
            }
        }
    } else if (!strcmp(sub, "magmode")) {
        int mode = -1;

        if (argc > 1) {
            for (int i = 0; i < 4; i++) {
                if (!strcmp(argv[1], as_kb_mag_mode_name(i))) {
                    mode = i;
                }
            }
            if (mode < 0) {
                mode = atoi(argv[1]);
            }
        }
        if (mode < 0 || mode > 3) {
            return kb_usage(&kb, "magmode",
                            "comfort|sensitive|gaming|custom");
        }
        ret = as_keyboard_set_mag_mode(&kb, mode);
        if (ret < 0) {
            report(ret);
        }
    } else if (!strcmp(sub, "travel")) {
        const char *act = opt_value(argc, argv, "--actuation");
        const char *rel = opt_value(argc, argv, "--release");
        const char *rtp = opt_value(argc, argv, "--rt-press");
        const char *rtr = opt_value(argc, argv, "--rt-release");
        const char *dz = opt_value(argc, argv, "--deadzone");
        int rt = opt_flag(argc, argv, "--rt") ||
                 opt_flag(argc, argv, "--rapid-trigger");
        double actuation = act ? atof(act) : AS_KB_DEFAULT_ACTUATION;
        double release = rel ? atof(rel) : actuation;
        double rt_press = rtp ? atof(rtp) : AS_KB_DEFAULT_RT;
        double rt_release = rtr ? atof(rtr) : rt_press;
        double deadzone = dz ? atof(dz) : 0.0;

        if (!act && !rel && !rtp && !rtr && !dz && !rt) {
            int32_t act_v[128], rel_v[128], dz_v[128], opt_v[128];
            int rc = as_keyboard_get_param(&kb, AS_K_MAG_ACTUATION, act_v, 128) ||
                     as_keyboard_get_param(&kb, AS_K_MAG_RELEASE, rel_v, 128) ||
                     as_keyboard_get_param(&kb, AS_K_MAG_DEADZONE, dz_v, 128) ||
                     as_keyboard_get_param(&kb, AS_K_MAG_OPTION, opt_v, 128);

            if (rc) {
                fprintf(stderr, "attackshark: cannot read travel settings\n");
                as_keyboard_close(&kb);
                return 1;
            }
            int uniform = 1;

            for (int key = 1; key < AS_KB_KEY_MAX; key++) {
                if (act_v[key] != act_v[0] || rel_v[key] != rel_v[0] ||
                    dz_v[key] != dz_v[0] ||
                    (opt_v[key] & AS_KB_OPTION_RT) != (opt_v[0] & AS_KB_OPTION_RT)) {
                    uniform = 0;
                    break;
                }
            }
            if (json_mode) {
                printf("{\"slot\":0,\"uniform\":%s,\"actuation\":%.2f,\"release\":%.2f,"
                       "\"deadzone\":%.2f,\"rapid_trigger\":%s}\n",
                       jbool(uniform),
                       as_kb_value_to_mm(&kb, AS_K_MAG_ACTUATION, act_v[0]),
                       as_kb_value_to_mm(&kb, AS_K_MAG_RELEASE, rel_v[0]),
                       as_kb_value_to_mm(&kb, AS_K_MAG_DEADZONE, dz_v[0]),
                       jbool(opt_v[0] & 0x80));
            } else {
                printf("slot        0\n");
                if (!uniform) {
                    printf("settings differ between slots; use keyboard keys for per-key values\n");
                }
                printf("actuation   %.2f mm\n",
                       as_kb_value_to_mm(&kb, AS_K_MAG_ACTUATION, act_v[0]));
                printf("release     %.2f mm\n",
                       as_kb_value_to_mm(&kb, AS_K_MAG_RELEASE, rel_v[0]));
                printf("deadzone    %.2f mm\n",
                       as_kb_value_to_mm(&kb, AS_K_MAG_DEADZONE, dz_v[0]));
                printf("rapid trig  %s\n", (opt_v[0] & 0x80) ? "on" : "off");
            }
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
            report(ret);
        } else if (!json_mode) {
            printf("travel applied to all keys (actuation %.2f mm, "
                   "rapid trigger %s)\n", actuation, rt ? "on" : "off");
        }
    } else if (!strcmp(sub, "keys")) {
        int32_t act[AS_KB_KEY_MAX], rel[AS_KB_KEY_MAX], rtp[AS_KB_KEY_MAX];
        int32_t rtr[AS_KB_KEY_MAX], dz[AS_KB_KEY_MAX], opt[AS_KB_KEY_MAX];
        int32_t pair[AS_KB_KEY_MAX];
        uint8_t uopt[AS_KB_KEY_MAX] = { 0 }, upair[AS_KB_KEY_MAX] = { 0 };
        uint32_t codes[AS_KB_KEY_MAX];
        int have_matrix, first = 1;

        ret = read_key_tables(&kb, act, rel, rtp, rtr, dz, opt, pair);
        if (ret < 0) {
            report(ret);
            as_keyboard_close(&kb);
            return 1;
        }
        have_matrix = as_keyboard_get_keymatrix(&kb, codes) == 0;
        if (json_mode) {
            printf("{\"keys\":[");
        }
        for (int i = 0; i < AS_KB_KEY_MAX; i++) {
            char name[32];
            int mode;

            if (have_matrix) {
                as_kb_key_name(codes[i], name, sizeof(name));
                if (!name[0]) {
                    continue;
                }
            } else {
                snprintf(name, sizeof(name), "поз.%d", i + 1);
            }
            uopt[i] = (uint8_t)opt[i];
            upair[i] = pair[i] >= 0 && pair[i] < AS_KB_KEY_MAX
                           ? (uint8_t)pair[i]
                           : 0;
            mode = as_kb_snap_mode_of(uopt, upair, i);
            if (json_mode) {
                char escaped[128];

                printf("%s{\"index\":%d,\"name\":\"%s\",\"actuation\":%.2f,"
                       "\"release\":%.2f,\"deadzone\":%.2f,\"rt_press\":%.2f,"
                       "\"rt_release\":%.2f,\"rapid_trigger\":%s,"
                       "\"snap_mode\":\"%s\",\"snap_partner\":",
                       first ? "" : ",", i,
                       json_escape(name, escaped, sizeof(escaped)),
                       as_kb_value_to_mm(&kb, AS_K_MAG_ACTUATION, act[i]),
                       as_kb_value_to_mm(&kb, AS_K_MAG_RELEASE, rel[i]),
                       as_kb_value_to_mm(&kb, AS_K_MAG_DEADZONE, dz[i]),
                       as_kb_value_to_mm(&kb, AS_K_MAG_RT_PRESS, rtp[i]),
                       as_kb_value_to_mm(&kb, AS_K_MAG_RT_RELEASE, rtr[i]),
                       jbool(opt[i] & AS_KB_OPTION_RT), snap_mode_name(mode));
                if (mode >= 0) {
                    printf("%d}", pair[i]);
                } else {
                    printf("null}");
                }
                first = 0;
                continue;
            }
            printf("%3d  %-10s act %.2f  rel %.2f  rt %-3s  %s", i, name,
                   as_kb_value_to_mm(&kb, AS_K_MAG_ACTUATION, act[i]),
                   as_kb_value_to_mm(&kb, AS_K_MAG_RELEASE, rel[i]),
                   (opt[i] & AS_KB_OPTION_RT) ? "on" : "off",
                   snap_mode_name(mode));
            if (mode >= 0) {
                printf(" with %d", pair[i]);
            }
            printf("\n");
        }
        if (json_mode) {
            printf("]}\n");
        }
    } else if (!strcmp(sub, "key")) {
        int32_t act[AS_KB_KEY_MAX], rel[AS_KB_KEY_MAX], rtp[AS_KB_KEY_MAX];
        int32_t rtr[AS_KB_KEY_MAX], dz[AS_KB_KEY_MAX], opt[AS_KB_KEY_MAX];
        int32_t pair[AS_KB_KEY_MAX];
        uint8_t uopt[AS_KB_KEY_MAX] = { 0 }, upair[AS_KB_KEY_MAX] = { 0 };
        const char *act_s = opt_value(argc, argv, "--actuation");
        const char *rel_s = opt_value(argc, argv, "--release");
        const char *rtp_s = opt_value(argc, argv, "--rt-press");
        const char *rtr_s = opt_value(argc, argv, "--rt-release");
        const char *dz_s = opt_value(argc, argv, "--deadzone");
        int rt_flag = opt_flag(argc, argv, "--rt") ||
                      opt_flag(argc, argv, "--rapid-trigger");
        int no_rt = opt_flag(argc, argv, "--no-rt");
        double act_mm, rel_mm, rt_press, rt_release, deadzone;
        int key, rt;

        if (argc < 2 || sscanf(argv[1], "%d", &key) != 1 || key < 0 ||
            key >= AS_KB_KEY_MAX) {
            return kb_usage(&kb, "key",
                            "KEY [--actuation MM] [--release MM] [--rt|--no-rt] "
                            "[--rt-press MM] [--rt-release MM] [--deadzone MM]");
        }
        ret = read_key_tables(&kb, act, rel, rtp, rtr, dz, opt, pair);
        if (ret < 0) {
            report(ret);
            as_keyboard_close(&kb);
            return 1;
        }
        act_mm = as_kb_value_to_mm(&kb, AS_K_MAG_ACTUATION, act[key]);
        rel_mm = as_kb_value_to_mm(&kb, AS_K_MAG_RELEASE, rel[key]);
        rt_press = as_kb_value_to_mm(&kb, AS_K_MAG_RT_PRESS, rtp[key]);
        rt_release = as_kb_value_to_mm(&kb, AS_K_MAG_RT_RELEASE, rtr[key]);
        deadzone = as_kb_value_to_mm(&kb, AS_K_MAG_DEADZONE, dz[key]);
        rt = (opt[key] & AS_KB_OPTION_RT) != 0;
        if (rt_flag) {
            rt = 1;
        }
        if (no_rt) {
            rt = 0;
        }
        if (!act_s && !rel_s && !rtp_s && !rtr_s && !dz_s && !rt_flag &&
            !no_rt) {
            uopt[key] = (uint8_t)opt[key];
            upair[key] = pair[key] >= 0 && pair[key] < AS_KB_KEY_MAX
                             ? (uint8_t)pair[key]
                             : 0;
            if (json_mode) {
                int mode = as_kb_snap_mode_of(uopt, upair, key);

                printf("{\"index\":%d,\"actuation\":%.2f,\"release\":%.2f,"
                       "\"deadzone\":%.2f,\"rt_press\":%.2f,"
                       "\"rt_release\":%.2f,\"rapid_trigger\":%s,"
                       "\"snap_mode\":\"%s\",\"snap_partner\":",
                       key, act_mm, rel_mm, deadzone, rt_press, rt_release,
                       jbool(rt), snap_mode_name(mode));
                if (mode >= 0) {
                    printf("%d}\n", pair[key]);
                } else {
                    printf("null}\n");
                }
            } else {
                print_key_travel(&kb, key, act, rel, rtp, rtr, dz, uopt,
                                 upair);
            }
            as_keyboard_close(&kb);
            return 0;
        }
        if (act_s) {
            act_mm = atof(act_s);
        }
        if (rel_s) {
            rel_mm = atof(rel_s);
        }
        if (rtp_s) {
            rt_press = atof(rtp_s);
        }
        if (rtr_s) {
            rt_release = atof(rtr_s);
        }
        if (dz_s) {
            deadzone = atof(dz_s);
        }
        if (act_mm < 0.1 || act_mm > 4.0 || rel_mm < 0.1 || rel_mm > 4.0 ||
            deadzone < 0.0 || deadzone > 1.0) {
            fprintf(stderr, "attackshark: travel values out of range\n");
            as_keyboard_close(&kb);
            return 2;
        }
        {
            uint8_t selected[AS_KB_KEY_MAX] = { 0 };

            selected[key] = 1;
            ret = as_keyboard_set_keys_travel(&kb, selected, rt, act_mm,
                                              rel_mm, rt_press, rt_release,
                                              deadzone);
        }
        if (ret < 0) {
            report(ret);
        } else if (!json_mode) {
            printf("key %d: actuation %.2f mm, rapid trigger %s\n", key,
                   act_mm, rt ? "on" : "off");
        }
    } else if (!strcmp(sub, "snap")) {
        int key1, key2, mode = AS_KB_SNAP_LAST;

        if (argc >= 3 && !strcmp(argv[1], "off")) {
            if (sscanf(argv[2], "%d", &key1) != 1 || key1 < 0 ||
                key1 >= AS_KB_KEY_MAX) {
                return kb_usage(&kb, "snap", "off KEY");
            }
            ret = as_keyboard_clear_snap(&kb, key1);
            if (ret < 0) {
                report(ret);
            } else if (json_mode) {
                printf("{\"key\":%d,\"snap\":false}\n", key1);
            } else {
                printf("snap tap removed from key %d\n", key1);
            }
        } else {
            if (argc < 3 || sscanf(argv[1], "%d", &key1) != 1 ||
                sscanf(argv[2], "%d", &key2) != 1 || key1 < 0 ||
                key1 >= AS_KB_KEY_MAX || key2 < 0 || key2 >= AS_KB_KEY_MAX ||
                key1 == key2) {
                return kb_usage(&kb, "snap",
                                "KEY1 KEY2 [last|neutral|first|second] "
                                "[--full-bottom] | off KEY");
            }
            if (argc > 3 && argv[3][0] != '-') {
                mode = snap_mode_by_name(argv[3]);
                if (mode < 0) {
                    return kb_usage(&kb, "snap",
                                    "KEY1 KEY2 [last|neutral|first|second] "
                                    "[--full-bottom] | off KEY");
                }
            }
            ret = as_keyboard_set_snap(&kb, key1, key2, mode,
                                       opt_flag(argc, argv, "--full-bottom"));
            if (ret < 0) {
                report(ret);
            } else if (json_mode) {
                printf("{\"key1\":%d,\"key2\":%d,\"mode\":\"%s\","
                       "\"full_bottom\":%s}\n", key1, key2,
                       snap_mode_name(mode),
                       jbool(opt_flag(argc, argv, "--full-bottom")));
            } else {
                printf("snap tap: keys %d and %d, %s\n", key1, key2,
                       snap_mode_name(mode));
            }
        }
    } else if (!strcmp(sub, "multi-set")) {
        int32_t values[128];
        long value;
        int param;
        unsigned count;

        if (argc < 3 || sscanf(argv[1], "%d", &param) != 1 ||
            sscanf(argv[2], "%ld", &value) != 1) {
            return kb_usage(&kb, "multi-set", "PARAM VALUE [COUNT]");
        }
        count = kb_key_count(argc, argv, 3);
        for (unsigned i = 0; i < count; i++) {
            values[i] = (int32_t)value;
        }
        ret = as_keyboard_set_param(&kb, param, values, count, 1);
        if (ret < 0) {
            report(ret);
        } else if (!json_mode) {
            printf("param %d set to %ld for %u keys\n", param, value, count);
        }
    } else if (!strcmp(sub, "multi")) {
        int32_t values[128];
        int param, is_option;
        unsigned count;

        if (argc < 2 || sscanf(argv[1], "%d", &param) != 1) {
            return kb_usage(&kb, "multi", "PARAM [COUNT]");
        }
        count = kb_key_count(argc, argv, 2);
        ret = as_keyboard_get_param(&kb, param, values, count);
        if (ret < 0) {
            report(ret);
        } else if (json_mode) {
            printf("{\"param\":%d,\"values\":[", param);
            for (unsigned i = 0; i < count; i++) {
                printf("%s%d", i ? "," : "", values[i]);
            }
            printf("]}\n");
        } else {
            is_option = param == AS_K_MAG_OPTION ||
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
        const char *seconds = opt_value(argc, argv, "--seconds");

        ret = stream_analog(&kb, seconds ? atoi(seconds) : 0);
        if (ret < 0) {
            report(ret);
        }
    } else if (!strcmp(sub, "reset")) {
        ret = as_keyboard_cmd(&kb, AS_K_RESET, NULL, 0, 0, NULL, 0);
        if (ret == 0) {
            sleep(3);
            if (json_mode) {
                printf("{\"reset\":true}\n");
            } else {
                printf("factory reset sent\n");
            }
        } else {
            report(ret);
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
        char *argv[16], *save = NULL, *tok;
        int argc = 0;

        if (!strchr(line, '\n') && !feof(f)) {
            int ch;

            fprintf(stderr, "attackshark: configuration line is too long\n");
            ret = 1;
            while ((ch = fgetc(f)) != '\n' && ch != EOF) {
            }
            continue;
        }
        line[strcspn(line, "#\n")] = 0;
        for (tok = strtok_r(line, " \t\r\n", &save); tok && argc < 16;
             tok = strtok_r(NULL, " \t\r\n", &save)) {
            argv[argc++] = tok;
        }
        if (tok) {
            fprintf(stderr, "attackshark: configuration line has too many arguments\n");
            ret = 1;
            continue;
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
                fprintf(stderr, "attackshark: warning: '%s' failed\n",
                        argv[0]);
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
        "  mouse raw CMD [ADDR] [LEN] [BYTES...]\n"
        "                              send a raw protocol packet\n"
        "\n"
        "  keyboard info               firmware, profile, light, rate\n"
        "  keyboard light [--mode M] [--brightness 0-4] [--speed 0-4]\n"
        "                [--color RRGGBB] [--option N] [--no-dazzle] [on|off]\n"
        "  keyboard side-light [--mode M] [--brightness 0-4] [--speed 0-4]\n"
        "                      [--color RRGGBB] [--no-dazzle]\n"
        "  keyboard rate HZ            polling rate (125..8000)\n"
        "  keyboard profile N          active profile\n"
        "  keyboard debounce MS        debounce time\n"
        "  keyboard magmode MODE       ");
    for (int i = 0; i < as_kb_mag_mode_count(); i++) {
        printf("%s%s", i ? "|" : "", as_kb_mag_mode_name(i));
    }
    printf("\n"
        "  keyboard travel [--actuation MM] [--release MM] [--rt]\n"
        "                  [--rt-press MM] [--rt-release MM] [--deadzone MM]\n"
        "  keyboard keys               list every key with its travel\n"
        "  keyboard key KEY [--actuation MM] [--release MM] [--rt|--no-rt]\n"
        "                  [--rt-press MM] [--rt-release MM] [--deadzone MM]\n"
        "  keyboard snap KEY1 KEY2 [last|neutral|first|second] [--full-bottom]\n"
        "  keyboard snap off KEY       remove a Snap Tap pair\n"
        "  keyboard multi PARAM [N]    raw actuation table read\n"
        "  keyboard multi-set PARAM VALUE [COUNT]\n"
        "                              raw actuation table write\n"
        "  keyboard stream [--seconds N]  live key travel monitor\n"
        "  keyboard reset              factory reset\n"
        "\n"
        "  apply FILE                  run commands from FILE\n"
        "\n"
        "  permissions [status|install]\n"
        "                              show or install the udev rules that\n"
        "                              give the user access to hidraw\n"
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
    if (!strcmp(argv[0], "permissions")) {
        return cmd_permissions(argc - 1, argv + 1);
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
    int n = 1;

    int positional = 0;

    for (int i = 1; i < argc; i++) {
        if (!positional && !strcmp(argv[i], "--")) {
            positional = 1;
            continue;
        }
        if (!positional && !strcmp(argv[i], "--json")) {
            json_mode = 1;
        } else if (!positional && !strcmp(argv[i], "--optional")) {
            optional_mode = 1;
        } else {
            argv[n++] = argv[i];
        }
    }
    return run_argv(n - 1, argv + 1);
}
