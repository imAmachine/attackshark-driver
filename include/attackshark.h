/* SPDX-License-Identifier: MIT */
#ifndef ATTACKSHARK_H
#define ATTACKSHARK_H

#include <stddef.h>
#include <stdint.h>

#define AS_MAX_DPI_STAGES 8
#define AS_MOUSE_CONFIG_LEN 256

/* ---------- HID transport ---------- */

/* Value of the bus field in the HID_ID line of the hidraw uevent. */
#define AS_BUS_BLUETOOTH 0x0005

struct as_hid {
    char path[64];
    /* HID_PHYS of the node, for example "usb-0000:00:14.0-6/input2" or a
     * Bluetooth address. All interfaces of one physical device share the part
     * before "/input", which is what ties a composite device together. */
    char phys[96];
    unsigned bus;
    unsigned vid;
    unsigned pid;
    int iface;
    unsigned char descriptor[512];
    size_t descriptor_len;
    char product[64];
    int fd;
};

/* Collapses the hidraw nodes of one composite device into one entry.
 * A keyboard such as the R85 HE is a composite USB device: the firmware
 * publishes the boot keyboard, the consumer/system keys and the vendor
 * settings collection as three interfaces, so /sys/class/hidraw holds three
 * nodes with the same vendor and product id. Only one of them can carry
 * settings, so the node with the vendor collection wins and the others are
 * dropped. Entries are sorted by path, so listings stay stable. Returns the
 * number of entries left in nodes. */
int as_hid_dedupe(struct as_hid *nodes, int count);

/* Every supported Attack Shark device, one entry per physical device.
 * Returns the number of entries written to out (max max). */
int as_hid_list_devices(struct as_hid *out, int max);

/* Transport and family of a hidraw node, shared by the CLI and the GUI. */
int as_hid_is_bluetooth(const struct as_hid *dev);
int as_hid_is_mouse(const struct as_hid *dev);
int as_hid_is_keyboard(const struct as_hid *dev);
/* True when the node exposes the vendor configuration collection. Bluetooth
 * HID links carry only standard HID collections, so they have no settings. */
int as_hid_has_vendor(const struct as_hid *dev);

int as_hid_open(struct as_hid *dev);
void as_hid_close(struct as_hid *dev);

/* Feature report helpers. len includes the one-byte report number. */
int as_feature_set(struct as_hid *dev, const uint8_t *buf, size_t len);
int as_feature_get(struct as_hid *dev, uint8_t *buf, size_t len, int timeout_ms);

/* Interrupt IN / OUT report helpers, buffer includes report number. */
int as_hid_write(struct as_hid *dev, const uint8_t *buf, size_t len, int timeout_ms);
int as_hid_read(struct as_hid *dev, uint8_t *buf, size_t len, int timeout_ms);

/* ---------- Attack Shark R11 Ultra (Compx / Control HUB) ---------- */

struct as_mouse {
    struct as_hid hid;
    uint8_t cid;
    uint8_t mid;
    uint8_t type;
    uint8_t version_major;
    uint8_t version_minor;
    uint8_t dongle_version[3];
};

enum as_mouse_cmd {
    AS_M_CMD_HANDSHAKE = 1,
    AS_M_CMD_PC_DRIVER_STATUS = 2,
    AS_M_CMD_ONLINE = 3,
    AS_M_CMD_BATTERY = 4,
    AS_M_CMD_WRITE_FLASH = 7,
    AS_M_CMD_READ_FLASH = 8,
    AS_M_CMD_GET_CONFIG = 14,
    AS_M_CMD_SET_CONFIG = 15,
    AS_M_CMD_VERSION = 18,
    AS_M_CMD_DONGLE_VERSION = 29,
};

/* EEPROM offsets of the per-profile mouse configuration block. */
enum as_mouse_offset {
    AS_M_REPORT_RATE = 0,
    AS_M_MAX_DPI_STAGE = 2,
    AS_M_CURRENT_DPI = 4,
    AS_M_KEY_OPERATION = 8,
    AS_M_LOD = 10,
    AS_M_DPI_VALUE = 12,
    AS_M_DPI_COLOR = 44,
    AS_M_DPI_EFFECT_MODE = 76,
    AS_M_DPI_EFFECT_BRIGHTNESS = 78,
    AS_M_DPI_EFFECT_SPEED = 80,
    AS_M_DPI_EFFECT_STATE = 82,
    AS_M_KEY_FUNCTION = 96,
    AS_M_LIGHT = 160,
    AS_M_DEBOUNCE = 169,
    AS_M_MOTION_SYNC = 171,
    AS_M_SLEEP_TIME = 173,
    AS_M_ANGLE_SNAP = 175,
    AS_M_RIPPLE = 177,
    AS_M_PERFORMANCE_STATE = 181,
    AS_M_PERFORMANCE = 183,
    AS_M_SENSOR_MODE = 185,
    AS_M_SENSOR_FPS_20K = 225,
    AS_M_WHEEL_DEBOUNCE = 227,
    AS_M_DEBOUNCE_RELEASE = 229,
    AS_M_SENSOR_3955_DPI = 6912,
    AS_M_END_EEPROM = 6987,
};

struct as_mouse_state {
    int online;
    int battery;
    int charging;
    int battery_voltage;
    int report_rate;
    int dpi_stages;
    int active_dpi_stage;
    unsigned dpi[AS_MAX_DPI_STAGES];
    int lod;
    int debounce;
    int motion_sync;
    int angle_snap;
    int ripple;
    int sleep_time;
    int profile;
    int version_valid;
    int version_major;
    int version_minor;
    int dongle_version_valid;
    int dongle_version[3];
    int config_valid;
    int light_mode;
    int light_speed;
    int light_brightness;
    int light_on;
    uint8_t light_color[3];
    uint8_t raw[AS_MOUSE_CONFIG_LEN];
};

int as_mouse_open(struct as_mouse *m);
void as_mouse_close(struct as_mouse *m);
/* Fills out with a Bluetooth link of the mouse family, which the firmware
 * exposes without the vendor collection. Returns 1 when such a link exists,
 * so callers can explain why settings are unavailable instead of reporting
 * the device as missing. */
int as_mouse_find_bluetooth(struct as_hid *out);
int as_mouse_handshake(struct as_mouse *m);
int as_mouse_read_block(struct as_mouse *m, unsigned addr, uint8_t *buf, unsigned len);
int as_mouse_write_block(struct as_mouse *m, unsigned addr, const uint8_t *buf, unsigned len);
int as_mouse_read_state(struct as_mouse *m, struct as_mouse_state *st);
int as_mouse_set_u8(struct as_mouse *m, unsigned offset, uint8_t value);
int as_mouse_set_dpi_stage(struct as_mouse *m, int stage, unsigned dpi);
int as_mouse_set_report_rate(struct as_mouse *m, int hz);
int as_mouse_raw(struct as_mouse *m, uint8_t cmd, unsigned addr,
                 const uint8_t *payload, unsigned len, uint8_t reply[16]);
int as_mouse_set_light(struct as_mouse *m, uint8_t mode, uint8_t r, uint8_t g,
                       uint8_t b, uint8_t speed, uint8_t brightness, int on);

int as_mouse_rate_code(int hz);
int as_mouse_rate_from_code(int code);
int as_mouse_dpi_valid(unsigned dpi);
int as_mouse_encode_dpi(unsigned dpi, uint8_t out[4]);
unsigned as_mouse_decode_dpi(const uint8_t rec[4]);

/* ---------- Attack Shark R85 HE (Royuan / GearHub) ---------- */

struct as_keyboard {
    struct as_hid hid;
    struct as_hid analog;
    int analog_open;
    int mag_mult;            /* 10, 100 or 200 (0.1 / 0.01 / 0.005 mm) */
    uint32_t usb_version;
    int usb_version_known;
};

enum as_kb_cmd {
    /* RY5088 (GearHub "Et") command set used by the R85 HE. */
    AS_K_RESET = 0x01,
    AS_K_SET_REPORT_RATE = 0x03,
    AS_K_SET_PROFILE = 0x04,
    AS_K_SET_DEBOUNCE = 0x06,
    AS_K_SET_LED = 0x07,
    AS_K_SET_SIDELED = 0x08,
    AS_K_SET_LED_POWER = 0x05,
    AS_K_SET_KBOPTION = 0x09,
    AS_K_SET_KEYMATRIX = 0x0A,
    AS_K_SET_FNKEYMATRIX = 0x10,
    AS_K_SET_SLEEPTIME = 0x11,
    AS_K_SET_MAGNETISM_STREAM = 0x1B,
    AS_K_SET_MAGNETISM_MODE = 0x1D,
    AS_K_SET_MULTI_MAGNETISM = 0x65,
    AS_K_GET_RF_VERSION = 0x80,
    AS_K_GET_REPORT_RATE = 0x83,
    AS_K_GET_PROFILE = 0x84,
    AS_K_GET_DEBOUNCE = 0x86,
    AS_K_GET_LED = 0x87,
    AS_K_GET_SIDELED = 0x88,
    AS_K_GET_LED_POWER = 0x85,
    AS_K_GET_KEYMATRIX = 0x8A,
    AS_K_GET_USB_VERSION = 0x8F,
    AS_K_GET_MAGNETISM_MODE = 0x9D,
    AS_K_GET_MULTI_MAGNETISM = 0xE5,
    AS_K_GET_REV = 0xF0,
};

enum as_kb_mag_param {
    AS_K_MAG_ACTUATION = 0,
    AS_K_MAG_RELEASE = 1,
    AS_K_MAG_RT_PRESS = 2,
    AS_K_MAG_RT_RELEASE = 3,
    AS_K_MAG_DKS_TOP = 4,
    AS_K_MAG_MT_TIME = 5,
    AS_K_MAG_DEADZONE = 6,
    AS_K_MAG_OPTION = 7,
    AS_K_MAG_DKS_FLAGS = 8,
    AS_K_MAG_SOCD_PAIR = 9,
};

#define AS_K_REPORT_LEN 64
#define AS_K_MAG_READ_PAGE 64
#define AS_K_MAG_WRITE_PAGE 56

struct as_kb_light {
    int valid;
    uint8_t mode;
    int speed;
    int brightness;
    int option;
    int dazzle;
    uint8_t r, g, b;
};

struct as_kb_state {
    int light_on;            /* 1/0, -1 when the firmware does not answer */
    struct as_kb_light side_light;
    int usb_version_valid;
    uint32_t usb_version;
    int revision_valid;
    int revision;
    int profile;
    int report_rate;
    int debounce;
    struct as_kb_light light;
    int mag_mode;
};

int as_keyboard_open(struct as_keyboard *kb);
void as_keyboard_close(struct as_keyboard *kb);
/* Fills out with a Bluetooth link of the keyboard family, which carries no
 * vendor collection. Returns 1 when such a link exists, so callers can say why
 * settings are unavailable instead of reporting the keyboard as missing. */
int as_keyboard_find_bluetooth(struct as_hid *out);

/* Send a command and return the 64-byte answer in out (may be NULL). */
int as_keyboard_cmd(struct as_keyboard *kb, uint8_t cmd, const uint8_t *args,
                    size_t args_len, int bit8, uint8_t *out, int timeout_ms);
/* Same, but accepts a response that does not echo the command byte. */
int as_keyboard_cmd_raw(struct as_keyboard *kb, uint8_t cmd, const uint8_t *args,
                        size_t args_len, int bit8, uint8_t *out,
                        int timeout_ms);

int as_keyboard_read_state(struct as_keyboard *kb, struct as_kb_state *st);
int as_keyboard_open_analog(struct as_keyboard *kb);
int as_keyboard_enable_stream(struct as_keyboard *kb, int on);
int as_keyboard_set_light(struct as_keyboard *kb, const struct as_kb_light *l);
/* Master switch for the whole backlight ("low power mode" in the vendor
 * software): mode 0 only changes the effect, not the power state. */
int as_keyboard_get_light_power(struct as_keyboard *kb);
int as_keyboard_get_light_power_zones(struct as_keyboard *kb, int *main_on,
                                      int *side_on);
int as_keyboard_set_light_power_zones(struct as_keyboard *kb, int main_on,
                                      int side_on);
int as_keyboard_set_light_power(struct as_keyboard *kb, int on);
/* Side (edge) lighting of the R85 HE. Unlike the main light, its speed byte
 * is written as-is instead of being inverted. */
int as_keyboard_get_sidelight(struct as_keyboard *kb, struct as_kb_light *l);
int as_keyboard_set_sidelight(struct as_keyboard *kb,
                              const struct as_kb_light *l);
int as_keyboard_set_report_rate(struct as_keyboard *kb, int hz);
int as_keyboard_set_profile(struct as_keyboard *kb, int profile);
int as_keyboard_set_debounce(struct as_keyboard *kb, int ms);
int as_keyboard_set_mag_mode(struct as_keyboard *kb, int mode);
/* Hall-effect parameters: width (u8/u16) is chosen from the firmware
 * version, exactly like the vendor driver. */
int as_keyboard_precision(struct as_keyboard *kb);
int as_keyboard_get_param(struct as_keyboard *kb, int param, int32_t *values,
                          size_t count);
int as_keyboard_set_param(struct as_keyboard *kb, int param,
                          const int32_t *values, size_t count, int save);
double as_kb_value_to_mm(struct as_keyboard *kb, int param, int32_t raw);
int as_keyboard_set_travel(struct as_keyboard *kb, int rapid_trigger,
                           double actuation, double release, double rt_press,
                           double rt_release, double deadzone);

int as_kb_rate_code(int hz);
int as_kb_rate_from_code(int code);
uint8_t as_kb_mm_to_raw(double mm);
double as_kb_raw_to_mm(uint8_t raw);
uint8_t as_kb_deadzone_to_raw(double mm);

/* ---------- udev / permissions ---------- */

/* Where the driver installs its own rules file; nothing has to be copied by
 * hand. */
const char *as_udev_rule_path(void);
/* Body of the rules file. Matches whole vendor families instead of single
 * product ids, so a new model of the same family works without a new file. */
char *as_udev_rule_text(void);
/* 1 when an Attack Shark rules file is present in any udev rules directory. */
int as_udev_rules_installed(void);
/* Writes the rules file (needs root), then reloads and re-triggers udev so the
 * nodes that are already plugged in gain their ACLs without a replug. Returns
 * 0 on success, a negative errno otherwise with a description in err. */
int as_udev_install(char *err, size_t errlen);
/* 1 when this process may install the rules itself (running as root). */
int as_udev_can_install(void);

/* ---------- misc ---------- */
const char *as_strerror(int err);

/* Human-readable names shared by the CLI and the GUI. */
const char *as_mouse_model_name(unsigned pid);
int as_light_mode_count(void);
const char *as_light_mode_name(int mode);
int as_light_mode_by_name(const char *name);

#endif
