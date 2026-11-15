// SPDX-License-Identifier: MIT
#include "attackshark.h"

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
    sa.sa_flags = 0;
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

static int ioctl_timeout(struct as_hid *dev, unsigned long req, uint8_t *buf,
                         size_t len, int timeout_ms)
{
    struct ioctl_arg a = { dev->fd, req, buf };
    (void)len;
    return with_timeout(timeout_ms, do_ioctl, &a);
}

/* Extract "inputN" interface number from HID_PHYS=usb-...:1.2 */
static int parse_iface(const char *phys)
{
    const char *p = strstr(phys, "/input");
    if (!p) {
        return -1;
    }
    return atoi(p + 6);
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

int as_hid_find(const unsigned char *probe, size_t probe_len,
                struct as_hid *out, int max)
{
    DIR *dir = opendir("/sys/class/hidraw");
    struct dirent *de;
    int found = 0;

    if (!dir) {
        return 0;
    }

    while ((de = readdir(dir)) != NULL && found < max) {
        char base[512], path[600], line[256];
        unsigned char desc[512];
        FILE *f;
        struct as_hid dev;
        int desc_len;
        size_t i;

        if (strncmp(de->d_name, "hidraw", 6) != 0) {
            continue;
        }

        memset(&dev, 0, sizeof(dev));
        dev.fd = -1;
        if (strlen(de->d_name) + 6 > sizeof(dev.path)) {
            continue;
        }
        memcpy(dev.path, "/dev/", 5);
        memcpy(dev.path + 5, de->d_name, strlen(de->d_name) + 1);

        snprintf(base, sizeof(base), "/sys/class/hidraw/%s/device", de->d_name);
        snprintf(path, sizeof(path), "%s/uevent", base);

        f = fopen(path, "r");
        if (!f) {
            continue;
        }
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "HID_ID=", 7) == 0) {
                unsigned bus = 0, vid = 0, pid = 0;
                if (sscanf(line + 7, "%x:%x:%x", &bus, &vid, &pid) == 3) {
                    dev.bus = bus;
                    dev.vid = vid;
                    dev.pid = pid;
                }
            } else if (strncmp(line, "HID_NAME=", 9) == 0) {
                size_t n = strcspn(line + 9, "\n");
                if (n > sizeof(dev.product) - 1) {
                    n = sizeof(dev.product) - 1;
                }
                memcpy(dev.product, line + 9, n);
                dev.product[n] = 0;
            } else if (strncmp(line, "HID_PHYS=", 9) == 0) {
                size_t n = strcspn(line + 9, "\n");
                if (n > sizeof(dev.phys) - 1) {
                    n = sizeof(dev.phys) - 1;
                }
                memcpy(dev.phys, line + 9, n);
                dev.phys[n] = 0;
                dev.iface = parse_iface(line + 9);
            }
        }
        fclose(f);

        snprintf(path, sizeof(path), "%s/report_descriptor", base);
        desc_len = read_file(path, desc, sizeof(desc));
        if (desc_len <= 0) {
            continue;
        }
        dev.descriptor_len = (size_t)desc_len;
        memcpy(dev.descriptor, desc, (size_t)desc_len);

        if (probe && probe_len > 0) {
            int match = 0;
            for (i = 0; i + probe_len <= (size_t)desc_len; i++) {
                if (memcmp(desc + i, probe, probe_len) == 0) {
                    match = 1;
                    break;
                }
            }
            if (!match) {
                continue;
            }
        }
        out[found++] = dev;
    }
    closedir(dir);
    return found;
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
        return;
    }
    snprintf(key, len, "%u:%.*s", dev->bus, (int)n, dev->phys);
}

/* The settings node is the one the driver talks to, so it represents the
 * device; without vendor collections (Bluetooth) the first node is kept. */
static int node_rank(const struct as_hid *dev)
{
    if (as_hid_has_vendor(dev)) {
        return 2;
    }
    return as_hid_is_bluetooth(dev) ? 1 : 0;
}

static int compare_nodes(const void *a, const void *b)
{
    const struct as_hid *left = a;
    const struct as_hid *right = b;

    return strcmp(left->path, right->path);
}

int as_hid_dedupe(struct as_hid *nodes, int count)
{
    struct as_hid *keys;
    int kept = 0;

    if (!nodes || count <= 0) {
        return 0;
    }
    keys = calloc((size_t)count, sizeof(*keys));
    if (!keys) {
        return count;
    }

    for (int i = 0; i < count; i++) {
        char key[128], known[128];
        int slot = -1;

        device_key(&nodes[i], key, sizeof(key));
        for (int j = 0; j < kept; j++) {
            device_key(&keys[j], known, sizeof(known));
            if (strcmp(key, known) == 0) {
                slot = j;
                break;
            }
        }
        if (slot < 0) {
            keys[kept] = nodes[i];
            nodes[kept++] = nodes[i];
        } else if (node_rank(&nodes[i]) > node_rank(&keys[slot])) {
            keys[slot] = nodes[i];
            nodes[slot] = nodes[i];
        }
    }
    free(keys);

    qsort(nodes, (size_t)kept, sizeof(*nodes), compare_nodes);
    return kept;
}

int as_hid_list_devices(struct as_hid *out, int max)
{
    struct as_hid found[32];
    int n = as_hid_find(NULL, 0, found, 32);
    int kept = 0;

    for (int i = 0; i < n; i++) {
        if (!as_hid_is_keyboard(&found[i]) && !as_hid_is_mouse(&found[i])) {
            continue;
        }
        if (kept < max) {
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

int as_feature_set(struct as_hid *dev, const uint8_t *buf, size_t len)
{
    int ret = ioctl_timeout(dev, HIDIOCSFEATURE(len), (uint8_t *)buf, len, 3000);
    if (ret < 0) {
        return -errno;
    }
    return 0;
}

int as_feature_get(struct as_hid *dev, uint8_t *buf, size_t len, int timeout_ms)
{
    int ret = ioctl_timeout(dev, HIDIOCGFEATURE(len), buf, len, timeout_ms);
    if (ret < 0) {
        return -errno;
    }
    return 0;
}

int as_hid_write(struct as_hid *dev, const uint8_t *buf, size_t len, int timeout_ms)
{
    struct pollfd p = { dev->fd, POLLOUT, 0 };
    int ret;

    ret = poll(&p, 1, timeout_ms);
    if (ret < 0) {
        return -errno;
    }
    if (ret == 0) {
        return -ETIMEDOUT;
    }
    ret = (int)write(dev->fd, buf, len);
    if (ret < 0) {
        return -errno;
    }
    return ret;
}

int as_hid_read(struct as_hid *dev, uint8_t *buf, size_t len, int timeout_ms)
{
    struct pollfd p = { dev->fd, POLLIN, 0 };
    int ret;

    ret = poll(&p, 1, timeout_ms);
    if (ret < 0) {
        return -errno;
    }
    if (ret == 0) {
        return -ETIMEDOUT;
    }
    ret = (int)read(dev->fd, buf, len);
    if (ret < 0) {
        return -errno;
    }
    return ret;
}
