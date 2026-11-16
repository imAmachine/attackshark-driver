// SPDX-License-Identifier: MIT
#include "internal.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <unistd.h>

#define HIDIOCSFEATURE(len) _IOC(_IOC_READ | _IOC_WRITE, 'H', 0x06, len)
#define HIDIOCGFEATURE(len) _IOC(_IOC_READ | _IOC_WRITE, 'H', 0x07, len)

static volatile sig_atomic_t timed_out;

static void on_alarm(int sig)
{
    (void)sig;
    timed_out = 1;
}

/* Run fn(arg) with a wall-clock limit; a blocking hidraw ioctl cannot be
 * interrupted any other way. */
static int with_timeout(int timeout_ms, int (*fn)(void *), void *arg)
{
    struct sigaction sa = { 0 }, old;
    struct itimerval it = { { 0, 0 }, { 0, 0 } };
    int ret;

    if (timeout_ms <= 0) {
        return fn(arg);
    }
    sa.sa_handler = on_alarm;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGALRM, &sa, &old);
    timed_out = 0;
    it.it_value.tv_sec = timeout_ms / 1000;
    it.it_value.tv_usec = (timeout_ms % 1000) * 1000;
    setitimer(ITIMER_REAL, &it, NULL);

    ret = fn(arg);

    memset(&it, 0, sizeof(it));
    setitimer(ITIMER_REAL, &it, NULL);
    sigaction(SIGALRM, &old, NULL);
    if (ret < 0 && errno == EINTR && timed_out) {
        errno = ETIMEDOUT;
    }
    return ret;
}

struct ioctl_arg {
    int fd;
    unsigned long req;
    uint8_t *buf;
};

static int do_ioctl(void *p)
{
    struct ioctl_arg *a = p;
    return ioctl(a->fd, a->req, a->buf);
}

/* Extract "inputN" interface number from HID_PHYS=usb-...:1.2 */
static int parse_iface(const char *phys)
{
    const char *p = strstr(phys, "/input");
    return p ? atoi(p + 6) : -1;
}

static int read_file(const char *path, unsigned char *buf, size_t max)
{
    int fd = open(path, O_RDONLY);
    ssize_t n;

    if (fd < 0) {
        return -1;
    }
    n = read(fd, buf, max);
    close(fd);
    return (int)n;
}

/* Copy a uevent value without its newline. */
static void copy_line(char *dst, size_t cap, const char *src)
{
    size_t n = strcspn(src, "\n");

    if (n > cap - 1) {
        n = cap - 1;
    }
    memcpy(dst, src, n);
    dst[n] = 0;
}

int as_hid_scan(struct as_hid *out, int max)
{
    DIR *dir = opendir("/sys/class/hidraw");
    struct dirent *de;
    int found = 0;

    if (!dir) {
        return 0;
    }
    while (found < max && (de = readdir(dir)) != NULL) {
        struct as_hid dev = { .fd = -1 };
        char base[512], path[600], line[256];
        FILE *f;
        int len;

        if (strncmp(de->d_name, "hidraw", 6) != 0) {
            continue;
        }
        snprintf(dev.path, sizeof(dev.path), "/dev/%.*s",
                 (int)sizeof(dev.path) - 6, de->d_name);
        snprintf(base, sizeof(base), "/sys/class/hidraw/%s/device", de->d_name);
        snprintf(path, sizeof(path), "%s/uevent", base);

        f = fopen(path, "r");
        if (!f) {
            continue;
        }
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "HID_ID=", 7) == 0) {
                sscanf(line + 7, "%x:%x:%x", &dev.bus, &dev.vid, &dev.pid);
            } else if (strncmp(line, "HID_PHYS=", 9) == 0) {
                copy_line(dev.phys, sizeof(dev.phys), line + 9);
                dev.iface = parse_iface(line + 9);
            }
        }
        fclose(f);

        snprintf(path, sizeof(path), "%s/report_descriptor", base);
        len = read_file(path, dev.descriptor, sizeof(dev.descriptor));
        if (len <= 0) {
            continue;
        }
        dev.descriptor_len = (size_t)len;
        out[found++] = dev;
    }
    closedir(dir);
    return found;
}

static int collect(int (*match)(const struct as_hid *), struct as_hid *out,
                   int max)
{
    struct as_hid all[AS_HID_MAX];
    int n = as_hid_scan(all, AS_HID_MAX);
    int kept = 0;

    for (int i = 0; i < n && kept < max; i++) {
        if (match(&all[i])) {
            out[kept++] = all[i];
        }
    }
    return kept;
}

int as_hid_find_first(int (*match)(const struct as_hid *), struct as_hid *out)
{
    struct as_hid one;

    if (collect(match, &one, 1) == 0) {
        return 0;
    }
    if (out) {
        *out = one;
    }
    return 1;
}

int as_hid_open_first(int (*match)(const struct as_hid *), unsigned vid,
                      struct as_hid *out)
{
    struct as_hid found[AS_HID_MAX];
    int n = collect(match, found, AS_HID_MAX);
    int best = 0;

    if (n == 0) {
        return -ENODEV;
    }
    for (int i = 0; i < n; i++) {
        if (found[i].vid == vid) {
            best = i;
            break;
        }
    }
    *out = found[best];
    return as_hid_open(out);
}

/* Identity of the physical device a node belongs to. USB interfaces append
 * "/inputN" to HID_PHYS, so the part before the slash is shared by all of
 * them; a Bluetooth link has no suffix and is its own device. A node without
 * HID_PHYS keeps its own path, so nothing is merged on a guess. */
static void device_key(const struct as_hid *dev, char *key, size_t len)
{
    size_t n = strcspn(dev->phys, "/");

    if (n == 0) {
        snprintf(key, len, "%u:%s", dev->bus, dev->path);
    } else {
        snprintf(key, len, "%u:%.*s", dev->bus, (int)n, dev->phys);
    }
}

/* The settings node is the one the driver talks to, so it represents the
 * device; without vendor collections (Bluetooth) the first node is kept. */
static int node_rank(const struct as_hid *dev)
{
    return as_hid_has_vendor(dev) ? 2 : as_hid_is_bluetooth(dev) ? 1 : 0;
}

static int key_rank_cmp(const void *a, const void *b)
{
    char ka[128], kb[128];
    int c;

    device_key(a, ka, sizeof(ka));
    device_key(b, kb, sizeof(kb));
    c = strcmp(ka, kb);
    if (c == 0) {
        c = node_rank(b) - node_rank(a);
    }
    return c ? c : strcmp(((const struct as_hid *)a)->path,
                          ((const struct as_hid *)b)->path);
}

static int path_cmp(const void *a, const void *b)
{
    return strcmp(((const struct as_hid *)a)->path,
                  ((const struct as_hid *)b)->path);
}

int as_hid_dedupe(struct as_hid *nodes, int count)
{
    int kept = 0;

    if (!nodes || count <= 0) {
        return 0;
    }
    /* Sorted by device key, best node of each device first. */
    qsort(nodes, (size_t)count, sizeof(*nodes), key_rank_cmp);
    for (int i = 0; i < count; i++) {
        char key[128], prev[128];

        device_key(&nodes[i], key, sizeof(key));
        if (kept > 0) {
            device_key(&nodes[kept - 1], prev, sizeof(prev));
            if (strcmp(key, prev) == 0) {
                continue;
            }
        }
        nodes[kept++] = nodes[i];
    }
    qsort(nodes, (size_t)kept, sizeof(*nodes), path_cmp);
    return kept;
}

int as_hid_list_devices(struct as_hid *out, int max)
{
    struct as_hid found[AS_HID_MAX];
    int n = as_hid_scan(found, AS_HID_MAX);
    int kept = 0;

    for (int i = 0; i < n && kept < max; i++) {
        if (as_hid_is_keyboard(&found[i]) || as_hid_is_mouse(&found[i])) {
            out[kept++] = found[i];
        }
    }
    return as_hid_dedupe(out, kept);
}

int as_hid_open(struct as_hid *dev)
{
    if (dev->fd >= 0) {
        return 0;
    }
    dev->fd = open(dev->path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    return dev->fd < 0 ? -errno : 0;
}

void as_hid_close(struct as_hid *dev)
{
    if (dev->fd >= 0) {
        close(dev->fd);
        dev->fd = -1;
    }
}

static int ioctl_feature(struct as_hid *dev, unsigned long req, uint8_t *buf,
                         size_t len, int timeout_ms)
{
    struct ioctl_arg a = { dev->fd, req, buf };
    int ret = with_timeout(timeout_ms, do_ioctl, &a);

    if (ret < 0) {
        return -errno;
    }
    return (size_t)ret == len ? 0 : -EPROTO;
}

int as_feature_set(struct as_hid *dev, const uint8_t *buf, size_t len)
{
    return ioctl_feature(dev, HIDIOCSFEATURE(len), (uint8_t *)buf, len, 3000);
}

int as_feature_get(struct as_hid *dev, uint8_t *buf, size_t len, int timeout_ms)
{
    return ioctl_feature(dev, HIDIOCGFEATURE(len), buf, len, timeout_ms);
}

/* Wait until the node is ready for events, -ETIMEDOUT when it never is. */
static int wait_fd(struct as_hid *dev, short events, int timeout_ms)
{
    struct pollfd p = { dev->fd, events, 0 };
    int ret = poll(&p, 1, timeout_ms);

    if (ret < 0) {
        return -errno;
    }
    return ret ? 0 : -ETIMEDOUT;
}

int as_hid_write(struct as_hid *dev, const uint8_t *buf, size_t len,
                 int timeout_ms)
{
    int ret = wait_fd(dev, POLLOUT, timeout_ms);

    if (ret < 0) {
        return ret;
    }
    ret = (int)write(dev->fd, buf, len);
    return ret < 0 ? -errno : ret;
}

int as_hid_read(struct as_hid *dev, uint8_t *buf, size_t len, int timeout_ms)
{
    int ret = wait_fd(dev, POLLIN, timeout_ms);

    if (ret < 0) {
        return ret;
    }
    ret = (int)read(dev->fd, buf, len);
    return ret < 0 ? -errno : ret;
}
