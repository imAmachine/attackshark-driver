#define _GNU_SOURCE
#include "../src/internal.h"
#include <stdio.h>
#define main cli_main
#define as_keyboard_open test_keyboard_open
#define as_keyboard_close test_keyboard_close
#define as_keyboard_enable_stream test_keyboard_enable_stream
#define as_keyboard_open_analog test_keyboard_open_analog
#define as_hid_read test_hid_read
int test_keyboard_open(struct as_keyboard *kb);
void test_keyboard_close(struct as_keyboard *kb);
int test_keyboard_enable_stream(struct as_keyboard *kb, int on);
int test_keyboard_open_analog(struct as_keyboard *kb);
int test_hid_read(struct as_hid *dev, uint8_t *buf, size_t len, int timeout_ms);
int test_mouse_open(struct as_mouse *m);
void test_mouse_close(struct as_mouse *m);
int test_mouse_read_state(struct as_mouse *m, struct as_mouse_state *st);
int test_mouse_set_u8(struct as_mouse *m, unsigned offset, uint8_t value);
int test_mouse_set_light(struct as_mouse *m, uint8_t mode, uint8_t r, uint8_t g, uint8_t b, uint8_t speed, uint8_t bright, int on);
int test_mouse_read_block(struct as_mouse *m, unsigned addr, uint8_t *buf, unsigned len);
int test_keyboard_read_state(struct as_keyboard *kb, struct as_kb_state *st);
int test_keyboard_set_light(struct as_keyboard *kb, const struct as_kb_light *l);
int test_keyboard_set_sidelight(struct as_keyboard *kb, const struct as_kb_light *l);
int test_keyboard_get_param(struct as_keyboard *kb, int param, int32_t *values, unsigned count);
int test_keyboard_get_keymatrix(struct as_keyboard *kb, uint32_t *codes);
int test_keyboard_get_light_power_zones(struct as_keyboard *kb, int *main_on, int *side_on);
int test_keyboard_set_light_power_zones(struct as_keyboard *kb, int main_on, int side_on);
#define as_mouse_open test_mouse_open
#define as_mouse_close test_mouse_close
#define as_mouse_read_state test_mouse_read_state
#define as_mouse_set_u8 test_mouse_set_u8
#define as_mouse_set_light test_mouse_set_light
#define as_mouse_read_block test_mouse_read_block
#define as_keyboard_read_state test_keyboard_read_state
#define as_keyboard_set_light test_keyboard_set_light
#define as_keyboard_set_sidelight test_keyboard_set_sidelight
#define as_keyboard_get_param test_keyboard_get_param
#define as_keyboard_get_keymatrix test_keyboard_get_keymatrix
#define as_keyboard_get_light_power_zones test_keyboard_get_light_power_zones
#define as_keyboard_set_light_power_zones test_keyboard_set_light_power_zones
FILE *test_fopen(const char *path, const char *mode);
size_t test_fwrite(const void *buf, size_t size, size_t count, FILE *f);
int test_fclose(FILE *f);
#define fopen test_fopen
#define fwrite test_fwrite
#define fclose test_fclose
#include "../src/main.c"
#undef fopen
#undef fwrite
#undef fclose
#undef as_hid_read
#undef as_keyboard_open_analog
#undef as_keyboard_enable_stream
#undef as_keyboard_close
#undef as_keyboard_open
#undef as_mouse_open
#undef as_mouse_close
#undef as_mouse_read_state
#undef as_mouse_set_u8
#undef as_mouse_set_light
#undef as_mouse_read_block
#undef as_keyboard_read_state
#undef as_keyboard_set_light
#undef as_keyboard_set_sidelight
#undef as_keyboard_get_param
#undef as_keyboard_get_keymatrix
#undef as_keyboard_get_light_power_zones
#undef as_keyboard_set_light_power_zones
#undef main

#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

static int fail_write, fail_close;
static size_t cookie_left;
static int cookie_eof, cookie_terminal_reads;

static ssize_t error_read(void *cookie, char *buf, size_t size)
{
    (void)cookie;
    if (!cookie_left) {
        cookie_terminal_reads++;
        if (cookie_eof) { return 0; }
        errno = EIO;
        return -1;
    }
    if (size > cookie_left) { size = cookie_left; }
    memset(buf, 'a', size);
    cookie_left -= size;
    return (ssize_t)size;
}

FILE *test_fopen(const char *path, const char *mode)
{
    if (!strcmp(path, "/mock/read-error") || !strcmp(path, "/mock/drain-error") || !strcmp(path, "/mock/drain-eof")) {
        cookie_io_functions_t io = { .read = error_read };
        cookie_eof = !strcmp(path, "/mock/drain-eof");
        cookie_terminal_reads = 0;
        cookie_left = strcmp(path, "/mock/read-error") ? 700 : 0;
        return fopencookie(NULL, "r", io);
    }
    return fopen(path, mode);
}

size_t test_fwrite(const void *buf, size_t size, size_t count, FILE *f)
{
    if (fail_write) { errno = EIO; return 0; }
    return fwrite(buf, size, count, f);
}

int test_fclose(FILE *f)
{
    int ret = fclose(f);
    if (fail_close) { errno = EIO; return EOF; }
    return ret;
}

static int device_open_result, state_result, writes, power_calls, state_calls;
static int main_power, side_power;
static struct as_kb_light written_light;
static struct as_mouse_state mouse_state;
static struct as_kb_state keyboard_state;
static unsigned byte_offset;
static uint8_t byte_value;

int test_mouse_open(struct as_mouse *m) { memset(m, 0, sizeof(*m)); return device_open_result; }
void test_mouse_close(struct as_mouse *m) { (void)m; }
int test_mouse_read_state(struct as_mouse *m, struct as_mouse_state *st)
{ (void)m; state_calls++; *st = mouse_state; return state_result; }
int test_mouse_set_u8(struct as_mouse *m, unsigned offset, uint8_t value)
{ (void)m; writes++; byte_offset = offset; byte_value = value; return 0; }
int test_mouse_set_light(struct as_mouse *m, uint8_t mode, uint8_t r, uint8_t g, uint8_t b, uint8_t speed, uint8_t bright, int on)
{
    (void)m; writes++; written_light = (struct as_kb_light){ .mode = mode, .r = r, .g = g, .b = b, .speed = speed, .brightness = bright };
    main_power = on; return 0;
}
int test_mouse_read_block(struct as_mouse *m, unsigned addr, uint8_t *buf, unsigned len)
{ (void)m; (void)addr; memset(buf, 0x55, len); return 0; }
int test_keyboard_read_state(struct as_keyboard *kb, struct as_kb_state *st)
{ (void)kb; state_calls++; *st = keyboard_state; return state_result; }
int test_keyboard_set_light(struct as_keyboard *kb, const struct as_kb_light *l)
{ (void)kb; writes++; written_light = *l; return 0; }
int test_keyboard_set_sidelight(struct as_keyboard *kb, const struct as_kb_light *l)
{ return test_keyboard_set_light(kb, l); }
int test_keyboard_get_light_power_zones(struct as_keyboard *kb, int *main_on, int *side_on)
{ (void)kb; if (main_on) { *main_on = main_power; } if (side_on) { *side_on = side_power; } return state_result; }
int test_keyboard_set_light_power_zones(struct as_keyboard *kb, int main_on, int side_on)
{ (void)kb; power_calls++; main_power = main_on; side_power = side_on; return 0; }
int test_keyboard_get_param(struct as_keyboard *kb, int param, int32_t *values, unsigned count)
{
    (void)kb;
    for (unsigned i = 0; i < count; i++) {
        values[i] = param == AS_K_MAG_OPTION ? (i == 10 ? AS_KB_MODE_SNAP_PRIORITY : i == 20 ? AS_KB_MODE_SNAP_OVERRIDDEN : 0)
                  : param == AS_K_MAG_SOCD_PAIR ? (i == 10 ? 20 : i == 20 ? 10 : 0) : 100;
    }
    return 0;
}
int test_keyboard_get_keymatrix(struct as_keyboard *kb, uint32_t *codes)
{ (void)kb; memset(codes, 0, AS_KB_KEY_MAX * sizeof(*codes)); codes[10] = 4; return 0; }

static int enable_result;
static int open_analog_result;
static int read_result;
static int enable_calls;
static int close_calls;
static int read_calls;
static int disable_result;

int test_keyboard_open(struct as_keyboard *kb)
{
    memset(kb, 0, sizeof(*kb));
    kb->analog.fd = -1;
    return device_open_result;
}

void test_keyboard_close(struct as_keyboard *kb)
{
    (void)kb;
    close_calls++;
}

int test_keyboard_enable_stream(struct as_keyboard *kb, int on)
{
    (void)kb;
    enable_calls++;
    if (!on && disable_result) {
        return disable_result;
    }
    return enable_result;
}

int test_keyboard_open_analog(struct as_keyboard *kb)
{
    (void)kb;
    return open_analog_result;
}

int test_hid_read(struct as_hid *dev, uint8_t *buf, size_t len, int timeout_ms)
{
    (void)dev;
    (void)timeout_ms;
    read_calls++;
    if (read_result) {
        return read_result;
    }
    if (len >= 6) {
        buf[0] = 0x05;
        buf[1] = 0x1b;
        buf[2] = 0x20;
        buf[3] = 0x00;
        buf[4] = 0x09;
    }
    usleep(10000);
    return 5;
}

static void reset_state(void)
{
    fail_write = fail_close = 0;
    device_open_result = state_result = writes = power_calls = state_calls = 0;
    main_power = side_power = 0;
    memset(&mouse_state, 0, sizeof(mouse_state));
    mouse_state.config_valid = 1;
    mouse_state.light_mode = 7;
    mouse_state.light_brightness = 8;
    mouse_state.light_speed = 9;
    mouse_state.light_color[0] = 11;
    keyboard_state = (struct as_kb_state){ .light = { .valid = 1, .mode = 7, .brightness = 3, .speed = 2, .option = 6, .dazzle = 1, .r = 11 } };
    keyboard_state.side_light = keyboard_state.light;
    json_mode = 0;
    optional_mode = 0;
    enable_result = 0;
    open_analog_result = 0;
    read_result = 0;
    enable_calls = 0;
    close_calls = 0;
    read_calls = 0;
    disable_result = 0;
}

static char *capture_file(void (*fn)(void *), void *arg)
{
    FILE *f = tmpfile();
    int saved = dup(STDOUT_FILENO);
    long size;
    char *out;

    assert(f && saved >= 0);
    assert(dup2(fileno(f), STDOUT_FILENO) >= 0);
    fn(arg);
    fflush(stdout);
    assert(fseek(f, 0, SEEK_END) == 0);
    size = ftell(f);
    assert(size >= 0);
    assert(fseek(f, 0, SEEK_SET) == 0);
    out = calloc((size_t)size + 1, 1);
    assert(out);
    assert(fread(out, 1, (size_t)size, f) == (size_t)size);
    assert(dup2(saved, STDOUT_FILENO) >= 0);
    close(saved);
    fclose(f);
    return out;
}

static void escape_case(void *arg)
{
    char out[128];
    (void)arg;
    assert(!strcmp(json_escape("\001\n\"/", out, sizeof(out)),
                  "\\u0001\\u000a\\\"/"));
    assert(json_escape("x", out, 0) == out);
}

static void long_file_case(void *arg)
{
    const char *path = arg;
    json_file_result(path, 37);
}

static int saved_stdout;
static int saved_stderr;

static void silence_output(void)
{
    int fd = open("/dev/null", O_WRONLY);

    saved_stdout = dup(STDOUT_FILENO);
    saved_stderr = dup(STDERR_FILENO);
    assert(fd >= 0 && saved_stdout >= 0 && saved_stderr >= 0);
    fflush(stdout);
    fflush(stderr);
    assert(dup2(fd, STDOUT_FILENO) >= 0);
    assert(dup2(fd, STDERR_FILENO) >= 0);
    close(fd);
}

static void restore_output(void)
{
    fflush(stdout);
    fflush(stderr);
    assert(dup2(saved_stdout, STDOUT_FILENO) >= 0);
    assert(dup2(saved_stderr, STDERR_FILENO) >= 0);
    close(saved_stdout);
    close(saved_stderr);
}

static void test_json(void)
{
    char *out;
    char path[1201];
    out = capture_file(escape_case, NULL);
    free(out);
    memset(path, 'a', sizeof(path) - 1);
    path[sizeof(path) - 1] = 0;
    out = capture_file(long_file_case, path);
    assert(strlen(out) == strlen("{\"file\":\"\",\"bytes\":37}\n") + 1200);
    assert(!strncmp(out, "{\"file\":\"", 9));
    assert(!strcmp(out + strlen(out) - strlen("\",\"bytes\":37}\n"),
                  "\",\"bytes\":37}\n"));
    free(out);
}

static void test_separator(void)
{
    char dir[] = "/tmp/attackshark-cli-XXXXXX";
    char *argv1[] = { (char *)"attackshark", (char *)"--json", (char *)"help", NULL };
    char *argv2[] = { (char *)"attackshark", (char *)"apply", (char *)"--", (char *)"--json", NULL };
    int fd;
    int cwd;

    cwd = open(".", O_RDONLY | O_DIRECTORY);
    assert(cwd >= 0);
    assert(mkdtemp(dir));
    assert(chdir(dir) == 0);
    fd = open("--json", O_WRONLY | O_CREAT | O_TRUNC, 0600);
    assert(fd >= 0);
    assert(write(fd, "help\n", 5) == 5);
    close(fd);
    silence_output();
    reset_state();
    assert(cli_main(3, argv1) == 0);
    reset_state();
    assert(cli_main(4, argv2) == 0);
    restore_output();
    assert(json_mode == 0);
    unlink("--json");
    assert(fchdir(cwd) == 0);
    close(cwd);
    assert(rmdir(dir) == 0);
}

static int run_stream(void)
{
    char *argv[] = { (char *)"attackshark", (char *)"keyboard", (char *)"stream",
                     (char *)"--seconds", (char *)"1", NULL };
    return cli_main(5, argv);
}

static void test_stream(void)
{
    struct timeval start, end;
    double elapsed;

    reset_state();
    silence_output();
    gettimeofday(&start, NULL);
    assert(run_stream() == 0);
    gettimeofday(&end, NULL);
    elapsed = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1e6;
    assert(elapsed < 2.0);
    assert(enable_calls == 2 && read_calls > 0 && close_calls == 1);
    reset_state();
    enable_result = -EIO;
    assert(run_stream() == 1);
    assert(enable_calls == 1 && close_calls == 1);

    reset_state();
    open_analog_result = -ENODEV;
    assert(run_stream() == 1);
    assert(enable_calls == 2 && close_calls == 1);

    reset_state();
    read_result = -EIO;
    assert(run_stream() == 1);
    assert(enable_calls == 2 && close_calls == 1);

    reset_state();
    disable_result = -EIO;
    assert(run_stream() == 1);
    assert(enable_calls == 2 && close_calls == 1);
    restore_output();
}

static int run_case(const char *command)
{
    char text[1024], *argv[64], *save, *tok;
    int argc = 0;

    assert(strlen(command) < sizeof(text));
    strcpy(text, command);
    for (tok = strtok_r(text, " ", &save); tok; tok = strtok_r(NULL, " ", &save)) {
        assert(argc < 64);
        argv[argc++] = tok;
    }
    return cli_main(argc, argv);
}

static void test_parsing(void)
{
    const char *bad[] = {
        "mouse dpi junk", "mouse rate junk", "keyboard light junk",
        "keyboard light --no-dazzle=yes", "mouse light --option 1",
        "keyboard stream --brightness 2", "keyboard snap 1 2 first junk",
        "mouse dpi set 1x 800", "mouse dpi set 4294967297 800",
        "mouse rate set 1000junk", "mouse lod 256", "mouse sleep -1",
        "mouse debounce 999999999999999999999999", "mouse effect mode 256",
        "mouse dump xyz", "mouse dump ffff 100", "mouse dump 0 101",
        "mouse raw xyz", "mouse raw 100", "mouse raw 1 10000",
        "mouse raw 1 0 b", "mouse raw 1 0 1 100", "mouse raw 1 0 1 zz",
        "mouse light --mode 256", "mouse light --color 12345z",
        "mouse light --color +12345", "mouse light --brightness garbage",
        "keyboard light --brightness 5", "keyboard light --speed -1",
        "keyboard light --option 16", "keyboard light --mode 4294967297",
        "keyboard light --color 0x1234", "keyboard light --mode",
        "keyboard rate 1000x", "keyboard profile 256", "keyboard debounce -1",
        "keyboard magmode garbage", "keyboard key 1x", "keyboard snap 1 2x",
        "keyboard snap off 1x", "keyboard multi 1 -1", "keyboard multi 256",
        "keyboard multi-set 1 2147483648", "keyboard multi-set 1 -2147483649",
        "keyboard multi-set 1 1 129", "keyboard stream --seconds -1",
        "keyboard travel --actuation nan", "keyboard travel --rt-press inf",
        "keyboard travel --deadzone 0junk", "keyboard key 1 --rt-release 1e999"
    };
    char command[1024];
    uint8_t r = 0, g = 0, b = 0;

    silence_output();
    for (size_t i = 0; i < AS_N(bad); i++) {
        reset_state();
        snprintf(command, sizeof(command), "attackshark %s", bad[i]);
        assert(run_case(command) == 2);
        assert(writes == 0 && state_calls == 0);
        device_open_result = -ENODEV;
        snprintf(command, sizeof(command), "attackshark --optional %s", bad[i]);
        assert(run_case(command) == 2);
    }
    char *sparse[] = { (char *)"light", NULL, (char *)"--no-dazzle", (char *)"--mode=wave" };
    assert(opt_flag(4, sparse, "--no-dazzle") == 1);
    assert(!strcmp(opt_value(4, sparse, "--mode"), "wave"));
    assert(parse_color("#aB12fF", &r, &g, &b) == 0 && r == 0xab && g == 0x12 && b == 0xff);
    reset_state();
    assert(run_case("attackshark --json help") == 0 && json_mode);
    assert(run_case("attackshark help") == 0 && !json_mode && !optional_mode);
    restore_output();
}

static void test_lighting(void)
{
    silence_output();
    reset_state();
    assert(run_case("attackshark mouse light --color 112233") == 0);
    assert(writes == 1 && written_light.mode == 7 && written_light.brightness == 8 && written_light.speed == 9);
    assert(written_light.r == 0x11 && written_light.g == 0x22 && written_light.b == 0x33 && main_power == 0);
    reset_state();
    assert(run_case("attackshark mouse light on") == 0);
    assert(writes == 1 && state_calls == 0 && byte_offset == AS_M_LIGHT + 7 && byte_value == 1);
    reset_state();
    assert(run_case("attackshark keyboard light --color 112233") == 0);
    assert(writes == 1 && written_light.mode == 7 && written_light.brightness == 3 && written_light.option == 6 && written_light.dazzle == 1 && power_calls == 0);
    reset_state();
    assert(run_case("attackshark keyboard light on") == 0);
    assert(writes == 0 && state_calls == 0 && power_calls == 1 && main_power == 1 && side_power == 0);
    reset_state();
    assert(run_case("attackshark keyboard light --no-dazzle") == 0);
    assert(writes == 1 && written_light.dazzle == 0 && written_light.mode == 7);
    reset_state();
    assert(run_case("attackshark keyboard side-light --no-dazzle") == 0);
    assert(writes == 1 && written_light.dazzle == 0 && written_light.option == 6);
    reset_state();
    state_result = -EIO;
    assert(run_case("attackshark keyboard light --color 112233") == 1 && writes == 0);
    assert(run_case("attackshark mouse light --color 112233") == 1 && writes == 0);
    restore_output();
}

static void snap_case(void *arg)
{
    assert(run_case(arg) == 0);
}

static void test_snap_output(void)
{
    char *out;
    reset_state();
    out = capture_file(snap_case, "attackshark --json keyboard keys");
    assert(strstr(out, "\"snap_mode\":\"first\",\"snap_partner\":20"));
    free(out);
    out = capture_file(snap_case, "attackshark --json keyboard key 10");
    assert(strstr(out, "\"snap_mode\":\"first\",\"snap_partner\":20"));
    free(out);
    out = capture_file(snap_case, "attackshark keyboard key 20");
    assert(strstr(out, "second (paired with key 10)"));
    free(out);
}

static void test_apply_errors(void)
{
    char path[] = "/tmp/attackshark-apply-XXXXXX", command[1024];
    int fd = mkstemp(path);
    const char *lines = "--optional keyboard info\n--json help\nhelp\n";

    assert(fd >= 0);
    assert(write(fd, lines, strlen(lines)) == (ssize_t)strlen(lines));
    close(fd);
    snprintf(command, sizeof(command), "attackshark --optional apply %s", path);
    silence_output();
    reset_state();
    device_open_result = -ENODEV;
    assert(run_case(command) == 0 && !json_mode && optional_mode);
    device_open_result = -EIO;
    assert(run_case(command) == 1);
    device_open_result = -EACCES;
    assert(run_case(command) == 1);
    fd = open(path, O_WRONLY | O_TRUNC);
    assert(fd >= 0 && write(fd, "keyboard invalid\n", 17) == 17);
    close(fd);
    device_open_result = -ENODEV;
    assert(run_case(command) == 1);
    fd = open(path, O_WRONLY | O_TRUNC);
    assert(fd >= 0 && write(fd, "--json apply nope\n", 18) == 18);
    close(fd);
    assert(run_case(command) == 1);
    assert(run_case("attackshark --optional apply /tmp") == 1);
    reset_state();
    assert(run_case("attackshark --optional mouse backup /dev/full") == 1);
    assert(run_case("attackshark --optional apply /mock/read-error") == 1);
    assert(run_case("attackshark --optional apply /mock/drain-error") == 1);
    assert(cookie_terminal_reads == 1);
    assert(run_case("attackshark --optional apply /mock/drain-eof") == 1);
    assert(cookie_terminal_reads == 1);
    fail_write = 1;
    snprintf(command, sizeof(command), "attackshark mouse backup %s", path);
    assert(run_case(command) == 1);
    fail_write = 0;
    fail_close = 1;
    assert(run_case(command) == 1);
    fail_close = 0;
    restore_output();
    unlink(path);
}

int main(void)
{
    test_json();
    test_separator();
    test_stream();
    test_parsing();
    test_lighting();
    test_snap_output();
    test_apply_errors();
    puts("cli regression ok");
    return 0;
}
