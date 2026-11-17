// SPDX-License-Identifier: MIT
/* Detection rules: which hidraw nodes belong to which family, and whether they
 * carry the vendor settings collection. The Bluetooth report map below is the
 * one an R11 Ultra really publishes over HOGP (202 bytes, standard HID
 * collections only), so a Bluetooth link must be recognised without it. */
#define _GNU_SOURCE
#include "attackshark.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>

static FILE *install_file;
static int spawn_calls;
static int spawn_error;
static int wait_calls;
static int wait_interrupts;
static int wait_error;
static int wait_status;
static const char *open_path;

static uid_t mock_geteuid(void)
{
    return 0;
}

static int mock_mkdir(const char *path, mode_t mode)
{
    assert(strcmp(path, "/etc/udev/rules.d") == 0);
    assert(mode == 0755);
    return 0;
}

static int mock_open(const char *path, int flags, ...)
{
    assert(strcmp(path, "/etc/udev/rules.d/70-attackshark.rules.tmp") == 0);
    assert(flags == (O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC));
    if (open_path) {
        return open(open_path, flags, 0644);
    }
    assert(install_file != NULL);
    return dup(fileno(install_file));
}

static int mock_rename(const char *oldpath, const char *newpath)
{
    assert(strcmp(oldpath, "/etc/udev/rules.d/70-attackshark.rules.tmp") == 0);
    assert(strcmp(newpath, "/etc/udev/rules.d/70-attackshark.rules") == 0);
    return 0;
}

static int mock_unlink(const char *path)
{
    assert(strcmp(path, "/etc/udev/rules.d/99-attackshark.rules") == 0);
    return 0;
}

static int mock_posix_spawn(pid_t *pid, const char *path,
                            const posix_spawn_file_actions_t *actions,
                            const posix_spawnattr_t *attr,
                            char *const argv[], char *const envp[])
{
    assert(strcmp(path, "/usr/bin/udevadm") == 0);
    assert(actions == NULL);
    assert(attr == NULL);
    assert(envp != NULL);
    assert(spawn_calls < 2);
    assert(strcmp(argv[0], path) == 0);
    assert(strcmp(argv[1], spawn_calls == 0 ? "control" : "trigger") == 0);
    assert(strcmp(argv[2], spawn_calls == 0 ? "--reload-rules" :
                                           "--subsystem-match=hidraw") == 0);
    assert(argv[3] == NULL);
    spawn_calls++;
    if (spawn_error) {
        errno = EINVAL;
        return spawn_error;
    }
    *pid = 123;
    return 0;
}

static pid_t mock_waitpid(pid_t pid, int *status, int options)
{
    assert(pid == 123);
    assert(options == 0);
    wait_calls++;
    if (wait_interrupts > 0) {
        wait_interrupts--;
        errno = EINTR;
        return -1;
    }
    if (wait_error) {
        errno = wait_error;
        return -1;
    }
    *status = wait_status;
    return pid;
}

#define as_udev_rule_path mock_udev_rule_path
#define as_udev_rule_text mock_udev_rule_text
#define as_udev_rules_installed mock_udev_rules_installed
#define as_udev_can_install mock_udev_can_install
#define as_udev_install mock_udev_install
#define geteuid mock_geteuid
#define mkdir mock_mkdir
#define open mock_open
#define rename mock_rename
#define unlink mock_unlink
#define posix_spawn mock_posix_spawn
#define waitpid mock_waitpid
#include "../src/udev.c"
#undef waitpid
#undef posix_spawn
#undef unlink
#undef rename
#undef open
#undef mkdir
#undef geteuid
#undef as_udev_install
#undef as_udev_can_install
#undef as_udev_rules_installed
#undef as_udev_rule_text
#undef as_udev_rule_path

/* Attack Shark R11 Ultra over Bluetooth (VID 3554, PID f533). */
static const unsigned char bt_map[] = {
    0x05, 0x01, 0x09, 0x06, 0xa1, 0x01, 0x85, 0x01, 0x05, 0x08, 0x19, 0x01,
    0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x03, 0x91, 0x02,
    0x95, 0x05, 0x91, 0x01, 0x05, 0x07, 0x19, 0xe0, 0x29, 0xe7, 0x95, 0x08,
    0x81, 0x02, 0x75, 0x08, 0x95, 0x01, 0x81, 0x01, 0x05, 0x07, 0x19, 0x00,
    0x2a, 0xff, 0x00, 0x26, 0xff, 0x00, 0x95, 0x06, 0x81, 0x00, 0xc0, 0x05,
    0x0c, 0x09, 0x01, 0xa1, 0x01, 0x85, 0x02, 0x15, 0x00, 0x26, 0x3c, 0x02,
    0x19, 0x00, 0x2a, 0x3c, 0x02, 0x75, 0x10, 0x95, 0x01, 0x81, 0x00, 0xc0,
    0x05, 0x01, 0x09, 0x80, 0xa1, 0x01, 0x85, 0x03, 0x19, 0x81, 0x29, 0x83,
    0x15, 0x00, 0x25, 0x01, 0x95, 0x03, 0x75, 0x01, 0x81, 0x02, 0x95, 0x01,
    0x75, 0x05, 0x81, 0x01, 0xc0, 0x05, 0x01, 0x09, 0x02, 0xa1, 0x01, 0x09,
    0x01, 0xa1, 0x00, 0x85, 0x04, 0x05, 0x09, 0x19, 0x01, 0x29, 0x05, 0x15,
    0x00, 0x25, 0x01, 0x95, 0x05, 0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75,
    0x03, 0x81, 0x01, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x16, 0x00, 0x80,
    0x26, 0xff, 0x7f, 0x75, 0x10, 0x95, 0x02, 0x81, 0x06, 0xc0, 0xa1, 0x00,
    0x05, 0x01, 0x09, 0x38, 0x15, 0x81, 0x25, 0x7f, 0x75, 0x08, 0x95, 0x01,
    0x81, 0x06, 0xc0, 0xa1, 0x00, 0x05, 0x0c, 0x0a, 0x38, 0x02, 0x95, 0x01,
    0x75, 0x08, 0x15, 0x81, 0x25, 0x7f, 0x81, 0x06, 0xc0, 0xc0,
};

/* 2.4 GHz receiver: mouse map plus the Compx vendor collection (usage page
 * 0xFF02, report ID 8) the driver actually talks to. */
static const unsigned char dongle_map[] = {
    0x05, 0x01, 0x09, 0x02, 0xa1, 0x01, 0x85, 0x04, 0x05, 0x09, 0x19, 0x01,
    0x29, 0x05, 0x15, 0x00, 0x25, 0x01, 0x95, 0x05, 0x75, 0x01, 0x81, 0x02,
    0xc0,
    0x06, 0x02, 0xff, 0x09, 0x02, 0xa1, 0x01, 0x85, 0x08, 0x75, 0x08, 0x95,
    0x10, 0x15, 0x00, 0x26, 0xff, 0x00, 0x81, 0x02, 0x91, 0x02, 0xc0,
};

/* R85 HE keyboard: Royuan vendor collection (usage page 0xFFFF, usage 2). */
static const unsigned char keyboard_map[] = {
    0x05, 0x01, 0x09, 0x06, 0xa1, 0x01, 0x85, 0x01, 0x05, 0x07, 0x19, 0xe0,
    0x29, 0xe7, 0x95, 0x08, 0x75, 0x01, 0x81, 0x02, 0xc0,
    0x06, 0xff, 0xff, 0x09, 0x02, 0xa1, 0x01, 0x85, 0x06, 0x75, 0x08, 0x95,
    0x38, 0x81, 0x02, 0xc0,
};

static struct as_hid make(unsigned vid, unsigned pid, unsigned bus,
                          const unsigned char *desc, size_t len)
{
    struct as_hid dev;

    memset(&dev, 0, sizeof(dev));
    dev.fd = -1;
    dev.vid = vid;
    dev.pid = pid;
    dev.bus = bus;
    memcpy(dev.descriptor, desc, len);
    dev.descriptor_len = len;
    return dev;
}

static void test_bluetooth_link(void)
{
    struct as_hid dev = make(0x3554, 0xf533, AS_BUS_BLUETOOTH, bt_map,
                             sizeof(bt_map));

    assert(as_hid_is_bluetooth(&dev));
    assert(as_hid_is_mouse(&dev));
    assert(!as_hid_is_keyboard(&dev));
    /* The whole point: nothing to configure over Bluetooth. */
    assert(!as_hid_has_vendor(&dev));
    assert(strcmp(as_mouse_model_name(dev.pid), "R11 Ultra (Bluetooth)") == 0);
    assert(strstr(as_strerror(-ENOTSUP), "Bluetooth") != NULL);
    printf("bluetooth link ok\n");
}

static void test_dongle_link(void)
{
    struct as_hid dev = make(0x3554, 0xfb44, 0, dongle_map,
                             sizeof(dongle_map));

    assert(!as_hid_is_bluetooth(&dev));
    assert(as_hid_is_mouse(&dev));
    assert(as_hid_has_vendor(&dev));
    assert(strcmp(as_mouse_model_name(dev.pid), "R11 Ultra (2.4 GHz)") == 0);
    assert(strstr(as_strerror(-EREMOTEIO), "refused") != NULL);
    assert(strstr(as_strerror(-EIO), "input/output") != NULL);
    assert(strstr(as_strerror(-EIO), "after the write") == NULL);
    printf("dongle link ok\n");
}

static void test_keyboard_link(void)
{
    struct as_hid dev = make(0x3151, 0x5029, 0, keyboard_map,
                             sizeof(keyboard_map));

    assert(as_hid_is_keyboard(&dev));
    assert(!as_hid_is_mouse(&dev));
    assert(as_hid_has_vendor(&dev));
    printf("keyboard link ok\n");
}

static void test_unrelated_node(void)
{
    struct as_hid dev = make(0x347d, 0x7853, 0, bt_map, sizeof(bt_map));

    assert(!as_hid_is_mouse(&dev));
    assert(!as_hid_is_keyboard(&dev));
    assert(!as_hid_has_vendor(&dev));
    assert(!as_hid_is_bluetooth(&dev));
    printf("unrelated node ok\n");
}

/* A boot keyboard map, as published by the first interface of the R85 HE. */
static const unsigned char boot_map[] = {
    0x05, 0x01, 0x09, 0x06, 0xa1, 0x01, 0x05, 0x07, 0x19, 0xe0, 0x29, 0xe7,
    0x95, 0x08, 0x75, 0x01, 0x81, 0x02, 0xc0,
};

static struct as_hid node(unsigned vid, unsigned pid, const char *path,
                          const char *phys, int iface,
                          const unsigned char *desc, size_t len)
{
    struct as_hid dev = make(vid, pid, 0, desc, len);

    snprintf(dev.path, sizeof(dev.path), "%s", path);
    snprintf(dev.phys, sizeof(dev.phys), "%s", phys);
    dev.iface = iface;
    return dev;
}

/* The R85 HE publishes three hidraw nodes: the boot keyboard, the consumer and
 * system key collections and the vendor settings collection. They are one
 * device, so the list must show one entry, and that entry has to be the node
 * with the settings collection. This was the source of three "R85 HE" rows. */
static void test_dedupe_composite_keyboard(void)
{
    struct as_hid nodes[3];
    int n;

    nodes[0] = node(0x3151, 0x5029, "/dev/hidraw2",
                    "usb-0000:00:14.0-6/input0", 0, boot_map,
                    sizeof(boot_map));
    nodes[1] = node(0x3151, 0x5029, "/dev/hidraw3",
                    "usb-0000:00:14.0-6/input1", 1, boot_map,
                    sizeof(boot_map));
    nodes[2] = node(0x3151, 0x5029, "/dev/hidraw4",
                    "usb-0000:00:14.0-6/input2", 2, keyboard_map,
                    sizeof(keyboard_map));

    n = as_hid_dedupe(nodes, 3);
    assert(n == 1);
    assert(strcmp(nodes[0].path, "/dev/hidraw4") == 0);
    assert(nodes[0].iface == 2);
    assert(as_hid_has_vendor(&nodes[0]));
    printf("composite keyboard dedupe ok\n");
}

/* Two keyboards on two cables are two devices, even with equal ids. */
static void test_dedupe_separate_devices(void)
{
    struct as_hid nodes[2];

    nodes[0] = node(0x3151, 0x5029, "/dev/hidraw2",
                    "usb-0000:00:14.0-6/input2", 2, keyboard_map,
                    sizeof(keyboard_map));
    nodes[1] = node(0x3151, 0x5029, "/dev/hidraw5",
                    "usb-0000:00:14.0-7/input2", 2, keyboard_map,
                    sizeof(keyboard_map));

    assert(as_hid_dedupe(nodes, 2) == 2);
    /* Sorted by path, so "list" does not depend on readdir order. */
    assert(strcmp(nodes[0].path, "/dev/hidraw2") == 0);
    assert(strcmp(nodes[1].path, "/dev/hidraw5") == 0);
    printf("separate devices ok\n");
}

/* Without HID_PHYS nothing is merged on a guess. */
static void test_dedupe_without_phys(void)
{
    struct as_hid nodes[2];

    nodes[0] = node(0x3151, 0x5029, "/dev/hidraw2", "", 0, boot_map,
                    sizeof(boot_map));
    nodes[1] = node(0x3151, 0x5029, "/dev/hidraw3", "", 1, boot_map,
                    sizeof(boot_map));

    assert(as_hid_dedupe(nodes, 2) == 2);
    printf("no phys no merge ok\n");
}

/* The driver writes the rules file itself, so it has to be complete: both
 * families, the hidraw subsystem and a vendor-wide match. */
static void test_udev_rule_text(void)
{
    char *text = as_udev_rule_text();

    assert(text != NULL);
    assert(strstr(text, "SUBSYSTEM==\"hidraw\"") != NULL);
    /* id/vendor is the HID bus attribute, so the USB, the receiver and the
     * Bluetooth link of a device are all covered by one rule. */
    assert(strstr(text, "ATTRS{id/vendor}==\"3151\"") != NULL);
    assert(strstr(text, "ATTRS{id/vendor}==\"3554\"") != NULL);
    assert(strstr(text, "TAG+=\"uaccess\"") != NULL);
    assert(strstr(text, "RUN{builtin}+=\"uaccess\"") != NULL);
    /* Vendor-wide: a new product id of the same family keeps working. */
    assert(strstr(text, "idProduct") == NULL);
    assert(strcmp(as_udev_rule_path(),
                  "/etc/udev/rules.d/70-attackshark.rules") == 0);
    free(text);
    printf("udev rule text ok\n");
}

/* udev reads the rules in file name order and 73-seat-late.rules only runs
 * the uaccess builtin for the tags set by the files before it. A higher
 * number therefore installs a file that changes the node mode but grants no
 * access: the node stays root-only and the fix looks like it did nothing. */
static void test_udev_rule_order(void)
{
    const char *base = strrchr(as_udev_rule_path(), '/');

    assert(base != NULL);
    assert(strcmp(base + 1, "73-seat-late.rules") < 0);
    printf("udev rules are read before the seat rules\n");
}

/* The file shipped for "make install" must stay byte-identical to what the
 * driver writes itself, otherwise packaging and the built-in installer would
 * drift apart. Skipped when the test is run outside the source tree. */
static void test_udev_rule_file(void)
{
    char file[4096];
    char *text = as_udev_rule_text();
    size_t n;
    FILE *f = fopen("udev/70-attackshark.rules", "rb");

    if (!f) {
        printf("udev rule file not found, skipped\n");
        free(text);
        return;
    }
    n = fread(file, 1, sizeof(file) - 1, f);
    fclose(f);
    file[n] = 0;
    assert(strcmp(file, text) == 0);
    free(text);
    printf("udev rule file matches the driver output\n");
}

/* The error buffer is optional: callers that only need the code pass NULL. */
static void test_udev_install_null_err(void)
{
    if (geteuid() == 0) {
        printf("udev install as root, skipped\n");
        return;
    }
    assert(as_udev_install(NULL, 0) == -EPERM);
    printf("udev install without an error buffer ok\n");
}

static void test_udev_install_argv(void)
{
    char err[256];
    char actual[4096];
    char *expected = as_udev_rule_text();
    size_t n;

    assert(expected != NULL);
    install_file = tmpfile();
    assert(install_file != NULL);
    spawn_calls = 0;
    assert(mock_udev_install(err, sizeof(err)) == 0);
    assert(err[0] == 0);
    assert(spawn_calls == 2);
    rewind(install_file);
    n = fread(actual, 1, sizeof(actual) - 1, install_file);
    actual[n] = 0;
    assert(strcmp(actual, expected) == 0);
    assert(fclose(install_file) == 0);
    install_file = NULL;
    free(expected);
    printf("udev install spawned argv ok\n");
}

static void test_udev_rule_roundtrip(void)
{
    char dir[] = "/tmp/attackshark-rules-XXXXXX";
    char path[256];
    char *text = as_udev_rule_text();
    FILE *f;

    assert(text != NULL);
    assert(mkdtemp(dir) != NULL);
    snprintf(path, sizeof(path), "%s/%s", dir, RULE_FILE);
    f = fopen(path, "w");
    assert(f != NULL);
    assert(fputs(text, f) >= 0);
    assert(fclose(f) == 0);
    assert(is_rules_file(dir, RULE_FILE));
    snprintf(path, sizeof(path), "%s/60-renamed.rules", dir);
    {
        char original[256];
        snprintf(original, sizeof(original), "%s/%s", dir, RULE_FILE);
        assert(rename(original, path) == 0);
    }
    assert(is_rules_file(dir, "60-renamed.rules"));
    {
        char legacy[256];
        snprintf(legacy, sizeof(legacy), "%s/%s", dir, LEGACY_RULE_FILE);
        assert(rename(path, legacy) == 0);
        assert(!is_rules_file(dir, LEGACY_RULE_FILE));
        assert(unlink(legacy) == 0);
    }
    assert(rmdir(dir) == 0);
    free(text);
    printf("udev generated rule detection roundtrip ok\n");
}

static void test_udev_tool_errors(void)
{
    char *const argv[] = { "/usr/bin/udevadm", "control", "--reload-rules",
                           NULL };

    spawn_calls = 0;
    wait_calls = 0;
    spawn_error = ENOENT;
    assert(run_tool(argv) == -ENOENT);
    assert(wait_calls == 0);
    spawn_error = EACCES;
    spawn_calls = 0;
    assert(run_tool(argv) == -EACCES);
    assert(wait_calls == 0);
    spawn_error = 0;
    spawn_calls = 0;
    wait_interrupts = 2;
    assert(run_tool(argv) == 0);
    assert(wait_calls == 3);
    spawn_calls = 0;
    wait_error = ECHILD;
    assert(run_tool(argv) == -ECHILD);
    wait_error = 0;
    spawn_calls = 0;
    wait_status = 1 << 8;
    assert(run_tool(argv) == -EIO);
    spawn_calls = 0;
    wait_status = SIGTERM;
    assert(run_tool(argv) == -EIO);
    wait_status = 0;
    printf("udev spawn and wait errors ok\n");
}

static void test_udev_stale_symlink(void)
{
    char dir[] = "/tmp/attackshark-symlink-XXXXXX";
    char target[256], link[256], content[32], err[256];
    FILE *f;

    assert(mkdtemp(dir) != NULL);
    snprintf(target, sizeof(target), "%s/target", dir);
    snprintf(link, sizeof(link), "%s/rules.tmp", dir);
    f = fopen(target, "w");
    assert(f != NULL);
    assert(fputs("preserve me", f) >= 0);
    assert(fclose(f) == 0);
    assert(symlink(target, link) == 0);
    open_path = link;
    spawn_calls = 0;
    assert(mock_udev_install(err, sizeof(err)) == -EEXIST);
    assert(spawn_calls == 0);
    open_path = NULL;
    f = fopen(target, "r");
    assert(f != NULL);
    assert(fgets(content, sizeof(content), f) != NULL);
    assert(strcmp(content, "preserve me") == 0);
    assert(fclose(f) == 0);
    assert(unlink(link) == 0);
    assert(unlink(target) == 0);
    assert(rmdir(dir) == 0);
    printf("udev stale temporary symlink rejected safely\n");
}

int main(void)
{
    test_bluetooth_link();
    test_dongle_link();
    test_keyboard_link();
    test_unrelated_node();
    test_dedupe_composite_keyboard();
    test_dedupe_separate_devices();
    test_dedupe_without_phys();
    test_udev_rule_text();
    test_udev_rule_order();
    test_udev_rule_file();
    test_udev_install_null_err();
    test_udev_install_argv();
    test_udev_rule_roundtrip();
    test_udev_tool_errors();
    test_udev_stale_symlink();
    printf("all tests passed\n");
    return 0;
}
