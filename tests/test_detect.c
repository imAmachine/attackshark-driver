// SPDX-License-Identifier: MIT
/* Detection rules: which hidraw nodes belong to which family, and whether they
 * carry the vendor settings collection. The Bluetooth report map below is the
 * one an R11 Ultra really publishes over HOGP (202 bytes, standard HID
 * collections only), so a Bluetooth link must be recognised without it. */
#include "attackshark.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    assert(strstr(text, "ATTRS{idVendor}==\"3151\"") != NULL);
    assert(strstr(text, "ATTRS{idVendor}==\"3554\"") != NULL);
    assert(strstr(text, "TAG+=\"uaccess\"") != NULL);
    /* Vendor-wide: a new product id of the same family keeps working. */
    assert(strstr(text, "idProduct") == NULL);
    assert(strcmp(as_udev_rule_path(),
                  "/etc/udev/rules.d/99-attackshark.rules") == 0);
    free(text);
    printf("udev rule text ok\n");
}

/* The file shipped for "make install" must stay byte-identical to what the
 * driver writes itself, otherwise packaging and the built-in installer would
 * drift apart. Skipped when the test is run outside the source tree. */
static void test_udev_rule_file(void)
{
    char file[4096];
    char *text = as_udev_rule_text();
    size_t n;
    FILE *f = fopen("udev/99-attackshark.rules", "rb");

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
    test_udev_rule_file();
    printf("all tests passed\n");
    return 0;
}
