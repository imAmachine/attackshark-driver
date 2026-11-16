#define as_keyboard_cmd_raw test_keyboard_cmd_raw
#define as_feature_set test_feature_set
#define as_feature_get test_feature_get
#include "../src/keyboard.c"
#undef as_feature_set
#undef as_feature_get
#undef as_keyboard_cmd_raw

#include <assert.h>
#include <stdio.h>

static int32_t model[10][AS_KB_KEY_MAX];
static int pending_param;
static int pending_page;
static int set_calls;
static int get_calls;
static int save_calls;
static int ignored_param = -1;

int test_feature_set(struct as_hid *dev, const uint8_t *buf, size_t len)
{
    const uint8_t *p = buf + 1;
    int param = p[1];
    int page = p[3];
    int width = param == AS_K_MAG_OPTION ? 1 : 2;

    (void)dev;
    assert(len == AS_K_REPORT_LEN + 1);
    if (p[0] == AS_K_GET_MULTI_MAGNETISM) {
        pending_param = p[1];
        pending_page = p[3];
        return 0;
    }
    if (p[0] != AS_K_SET_MULTI_MAGNETISM) {
        return -EINVAL;
    }
    set_calls++;
    if (p[4]) {
        save_calls++;
    }
    for (int i = 0; i < AS_K_MAG_WRITE_PAGE / width; i++) {
        int key = page * (AS_K_MAG_WRITE_PAGE / width) + i;
        if (key >= AS_KB_KEY_MAX) {
            break;
        }
        if (param == ignored_param) {
            continue;
        }
        model[param][key] = width == 1 ? p[8 + i] :
            p[8 + i * 2] | ((int32_t)p[9 + i * 2] << 8);
    }
    return 0;
}

int test_feature_get(struct as_hid *dev, uint8_t *buf, size_t len, int timeout_ms)
{
    uint8_t *p = buf + 1;
    int width = pending_param == AS_K_MAG_OPTION ? 1 : 2;
    int per_page = AS_K_MAG_READ_PAGE / width;

    (void)dev;
    (void)timeout_ms;
    assert(len == AS_K_REPORT_LEN + 1);
    memset(buf, 0, len);
    p[0] = AS_K_GET_MULTI_MAGNETISM;
    p[1] = pending_param;
    p[2] = 1;
    p[3] = pending_page;
    for (int i = 0; i < per_page; i++) {
        int key = pending_page * per_page + i;
        if (key >= AS_KB_KEY_MAX) {
            break;
        }
        if (width == 1) {
            p[i] = (uint8_t)model[pending_param][key];
        } else {
            p[i * 2] = (uint8_t)model[pending_param][key];
            p[1 + i * 2] = (uint8_t)(model[pending_param][key] >> 8);
        }
    }
    get_calls++;
    return 0;
}

int as_keyboard_cmd_raw(struct as_keyboard *kb, uint8_t cmd, const uint8_t *args,
                        size_t args_len, int bit8, uint8_t *out, int timeout_ms)
{
    uint8_t frame[AS_K_REPORT_LEN + 1] = { 0 };

    (void)kb; (void)bit8; (void)timeout_ms;
    assert(cmd == AS_K_GET_MULTI_MAGNETISM && args_len == 3);
    pending_param = args[0];
    pending_page = args[2];
    assert(test_feature_get(kb ? &kb->hid : NULL, frame, sizeof(frame),
                          timeout_ms) == 0);
    memcpy(out, frame + 1, AS_K_REPORT_LEN);
    return 0;
}

static void reset_counters(void)
{
    set_calls = get_calls = save_calls = 0;
}

int main(void)
{
    struct as_keyboard kb = { 0 };
    uint8_t selected[AS_KB_KEY_MAX] = { 0 };
    int32_t before;

    kb.mag_mult = 100;
    for (int param = 0; param < 10; param++) {
        for (int key = 0; key < AS_KB_KEY_MAX; key++) {
            model[param][key] = 100 + param * 10 + key;
        }
    }
    selected[3] = 1;
    selected[7] = 1;
    before = model[AS_K_MAG_ACTUATION][4];

    ignored_param = AS_K_MAG_RT_PRESS;
    assert(as_keyboard_set_keys_travel(&kb, selected, 1, 2.0, 2.5,
                                      0.2, 0.3, 0.4) == -EIO);
    reset_counters();
    model[AS_K_MAG_RT_PRESS][3] = 20;
    model[AS_K_MAG_RT_PRESS][7] = 20;
    model[AS_K_MAG_RT_RELEASE][3] = 103;
    model[AS_K_MAG_RT_RELEASE][7] = 107;
    ignored_param = AS_K_MAG_RT_RELEASE;
    assert(as_keyboard_set_keys_travel(&kb, selected, 1, 2.0, 2.5,
                                      0.2, 0.3, 0.4) == -EIO);
    reset_counters();
    ignored_param = -1;
    assert(as_keyboard_set_keys_travel(&kb, selected, 1, 2.0, 2.5,
                                      0.2, 0.3, 0.4) == 0);
    assert(set_calls > 0 && get_calls > 0);
    assert(model[AS_K_MAG_ACTUATION][3] == 200);
    assert(model[AS_K_MAG_RELEASE][7] == 250);
    assert(model[AS_K_MAG_ACTUATION][4] == before);
    assert(model[AS_K_MAG_RT_PRESS][3] == 20);
    assert(model[AS_K_MAG_RT_RELEASE][7] == 30);
    assert(save_calls == 1);

    reset_counters();
    ignored_param = -1;
    memset(selected, 0, sizeof(selected));
    assert(as_keyboard_set_keys_travel(&kb, selected, 1, 2.0, 2.5,
                                      0.2, 0.3, 0.4) == 0);
    assert(set_calls == 0 && get_calls == 0);
    puts("keyboard transport regression ok");
    return 0;
}
