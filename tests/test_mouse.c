#include "attackshark.h"

#include <assert.h>
#include <errno.h>
#include <string.h>

static uint8_t request[16];
static int writes;
static int reads;
static int scenario;

int mock_as_hid_write(struct as_hid *dev, const uint8_t *buf, size_t len,
                      int timeout_ms)
{
    (void)dev;
    (void)timeout_ms;
    assert(len == 17);
    memcpy(request, buf + 1, 16);
    writes++;
    return (int)len;
}

int mock_as_hid_read(struct as_hid *dev, uint8_t *buf, size_t len,
                     int timeout_ms)
{
    uint8_t cmd;
    int status = 0;

    (void)dev;
    (void)timeout_ms;
    assert(len >= 17);
    cmd = request[0];
    memset(buf, 0, len);
    buf[0] = 8;
    memcpy(buf + 1, request, 16);

    if (scenario == 1 || scenario == 2)
        status = 1;
    else if (scenario == 3 && reads++ == 0)
        status = 1;
    else if (scenario == 4 && writes == 2)
        status = 1;
    else if (scenario == 5 && cmd == AS_M_CMD_ONLINE) {
        buf[6] = 0;
        reads++;
        return 17;
    } else if (scenario == 5 && cmd == AS_M_CMD_BATTERY) {
        status = 1;
    } else if (scenario == 5) {
    }

    if (status) {
        buf[1] = cmd;
        buf[2] = 1;
    } else {
        buf[1 + 1] = 0;
        if (cmd == AS_M_CMD_READ_FLASH) {
            buf[1 + 4] = request[4];
            for (unsigned i = 0; i < (request[4] & 0x0f); i++)
                buf[1 + 5 + i] = (uint8_t)(0xa0 + i);
        }
    }
    reads++;
    return 17;
}

#define as_hid_write mock_as_hid_write
#define as_hid_read mock_as_hid_read
#include "../src/mouse.c"
#undef as_hid_write
#undef as_hid_read

static void reset_test(int which)
{
    memset(request, 0, sizeof(request));
    writes = 0;
    reads = 0;
    scenario = which;
}

static void test_flash_status1(void)
{
    struct as_mouse m = { 0 };
    uint8_t data[15] = { 0 };

    reset_test(1);
    assert(as_mouse_read_block(&m, 0x1234, data, sizeof(data)) == -EREMOTEIO);
    assert(writes == 1);

    reset_test(2);
    assert(as_mouse_write_block(&m, 0x1234, data, sizeof(data)) == -EREMOTEIO);
    assert(writes == 1);
}

static void test_flash_echo_and_chunks(void)
{
    struct as_mouse m = { 0 };
    uint8_t data[5] = { 0 };

    reset_test(0);
    assert(as_mouse_read_block(&m, 0x1234, data, sizeof(data)) == 0);
    assert(writes == 1);
    for (unsigned i = 0; i < sizeof(data); i++)
        assert(data[i] == (uint8_t)(0xa0 + i));
}

static void test_setters_propagate_status(void)
{
    struct as_mouse m = { 0 };
    uint8_t data[2] = { 0 };

    reset_test(1);
    assert(as_mouse_set_u8(&m, 20, 0x37) == -EREMOTEIO);
    assert(writes == 1);

    reset_test(4);
    assert(as_mouse_set_light(&m, 1, 2, 3, 4, 5, 6, 1) == -EREMOTEIO);
    assert(writes == 2);
    (void)data;
}

static void test_non_flash_status1_is_ignored(void)
{
    struct as_mouse m = { 0 };
    uint8_t reply[16];

    reset_test(3);
    assert(as_mouse_raw(&m, AS_M_CMD_VERSION, 0, NULL, 0, reply) == 0);
    assert(writes == 1);
    assert(reads == 2);
    assert(reply[0] == AS_M_CMD_VERSION);
    assert(reply[1] == 1);
}

static void test_battery_status1_is_not_parsed(void)
{
    struct as_mouse m = { 0 };
    struct as_mouse_state st;

    reset_test(5);
    assert(as_mouse_read_state(&m, &st) == 0);
    assert(st.online == 0);
    assert(st.battery == 0);
    assert(st.charging == 0);
}

int main(void)
{
    test_flash_status1();
    test_flash_echo_and_chunks();
    test_setters_propagate_status();
    test_non_flash_status1_is_ignored();
    test_battery_status1_is_not_parsed();
    return 0;
}
