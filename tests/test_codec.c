// SPDX-License-Identifier: MIT
#include "attackshark.h"

#include <assert.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

static void test_dpi(void)
{
    struct { unsigned dpi; uint8_t rec[4]; } cases[] = {
        { 800, { 0x0f, 0x0f, 0x00, 0x37 } },
        { 1600, { 0x1f, 0x1f, 0x00, 0x17 } },
        { 3200, { 0x3f, 0x3f, 0x00, 0xd7 } },
        { 5600, { 0x6f, 0x6f, 0x00, 0x77 } },
        { 8000, { 0x9f, 0x9f, 0x00, 0x17 } },
        { 42000, { 0xa3, 0xa3, 0x55, 0xba } },
    };
    uint8_t rec[4];

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        assert(as_mouse_encode_dpi(cases[i].dpi, rec) == 0);
        for (int j = 0; j < 4; j++) {
            if (rec[j] != cases[i].rec[j]) {
                fprintf(stderr, "dpi %u byte %d: %02x != %02x\n", cases[i].dpi,
                        j, rec[j], cases[i].rec[j]);
            }
            assert(rec[j] == cases[i].rec[j]);
        }
        assert(as_mouse_decode_dpi(rec) == cases[i].dpi);
    }

    assert(!as_mouse_dpi_valid(25));
    assert(!as_mouse_dpi_valid(30150));
    assert(as_mouse_dpi_valid(50));
    assert(as_mouse_dpi_valid(30000));
    assert(as_mouse_dpi_valid(30100));

    for (unsigned dpi = 50; dpi <= 30000; dpi += 50) {
        assert(as_mouse_encode_dpi(dpi, rec) == 0);
        assert(as_mouse_decode_dpi(rec) == dpi);
    }
    for (unsigned dpi = 30100; dpi <= 60000; dpi += 100) {
        assert(as_mouse_encode_dpi(dpi, rec) == 0);
        assert(as_mouse_decode_dpi(rec) == dpi);
    }
    printf("dpi codec ok\n");
}

static void test_rates(void)
{
    struct { int hz; int code; } cases[] = {
        { 125, 0x08 }, { 250, 0x04 }, { 500, 0x02 }, { 1000, 0x01 },
        { 2000, 0x10 }, { 4000, 0x20 }, { 8000, 0x40 },
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        assert(as_mouse_rate_code(cases[i].hz) == cases[i].code);
        assert(as_mouse_rate_from_code(cases[i].code) == cases[i].hz);
    }
    assert(as_mouse_rate_code(3000) == -1);
    printf("mouse rate codec ok\n");

    assert(as_kb_rate_code(8000) == 0);
    assert(as_kb_rate_code(1000) == 3);
    assert(as_kb_rate_code(125) == 6);
    assert(as_kb_rate_code(750) == -1);
    assert(as_kb_rate_from_code(0) == 8000);
    assert(as_kb_rate_from_code(3) == 1000);
    assert(as_kb_rate_from_code(6) == 125);
    printf("keyboard rate codec ok\n");
}

static void test_travel(void)
{
    assert(as_kb_mm_to_raw(0.1) == 0);
    assert(as_kb_mm_to_raw(1.5) == 14);
    assert(as_kb_mm_to_raw(4.0) == 39);
    assert(fabs(as_kb_raw_to_mm(14) - 1.5) < 1e-9);
    assert(as_kb_deadzone_to_raw(0.0) == 39);
    assert(as_kb_deadzone_to_raw(0.3) == 36);
    assert(as_kb_deadzone_to_raw(1.0) == 29);
    printf("travel codec ok\n");
}

static void test_travel_precision(void)
{
    struct as_keyboard kb;

    memset(&kb, 0, sizeof(kb));

    kb.mag_mult = 200;   /* firmware >= 1280: 0.005 mm */
    assert(fabs(as_kb_value_to_mm(&kb, AS_K_MAG_ACTUATION, 400) - 2.0) < 1e-9);
    assert(fabs(as_kb_value_to_mm(&kb, AS_K_MAG_DEADZONE, 60) - 0.3) < 1e-9);

    kb.mag_mult = 100;   /* firmware >= 768: 0.01 mm */
    assert(fabs(as_kb_value_to_mm(&kb, AS_K_MAG_ACTUATION, 150) - 1.5) < 1e-9);

    kb.mag_mult = 10;    /* old firmware: 0.1 mm */
    assert(fabs(as_kb_value_to_mm(&kb, AS_K_MAG_ACTUATION, 14) - 1.5) < 1e-9);
    assert(fabs(as_kb_value_to_mm(&kb, AS_K_MAG_DEADZONE, 37) - 0.2) < 1e-9);
    printf("travel precision ok\n");
}

int main(void)
{
    test_dpi();
    test_rates();
    test_travel();
    test_travel_precision();
    printf("all tests passed\n");
    return 0;
}
