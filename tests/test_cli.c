#define _GNU_SOURCE
#include "../src/internal.h"
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
#include "../src/main.c"
#undef as_hid_read
#undef as_keyboard_open_analog
#undef as_keyboard_enable_stream
#undef as_keyboard_close
#undef as_keyboard_open
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
    return 0;
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

int main(void)
{
    test_json();
    test_separator();
    test_stream();
    puts("cli regression ok");
    return 0;
}
