// SPDX-License-Identifier: MIT
/*
 * The driver owns the udev rules it needs.
 *
 * hidraw nodes are root-only by default, so a desktop user cannot talk to the
 * vendor collection until a rule grants access. Shipping the rules in the
 * source tree means every user has to find them, copy them to /etc/udev and
 * reload udev by hand; instead the library generates the file, installs it
 * when it is allowed to (root, or via pkexec from the GUI) and refreshes the
 * permissions of the nodes that are already plugged in.
 *
 * The rules match the vendor id rather than single product ids: a new model of
 * the same family, or a firmware update that renumbers the product id, keeps
 * working without a new rules file.
 */
#include "attackshark.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

#define RULE_FILE "99-attackshark.rules"
#define RULE_PATH "/etc/udev/rules.d/" RULE_FILE
/* Present in the file name and in the header comment, so a renamed copy of
 * the rules file is still recognised. */
#define RULE_MARKER "attackshark"

static const struct {
    const char *vid;
    const char *comment;
} families[] = {
    { "3151", "Royuan keyboards: R85 HE and relatives" },
    { "3554", "Compx mice: R11 Ultra, X11 Ultra and relatives" },
};

static const char *const rules_dirs[] = {
    "/etc/udev/rules.d",
    "/run/udev/rules.d",
    "/usr/local/lib/udev/rules.d",
    "/usr/lib/udev/rules.d",
    "/lib/udev/rules.d",
};

const char *as_udev_rule_path(void)
{
    return RULE_PATH;
}

char *as_udev_rule_text(void)
{
    size_t cap = 1024 + sizeof(families) / sizeof(families[0]) * 256;
    char *text = malloc(cap);
    int n;

    if (!text) {
        return NULL;
    }
    n = snprintf(text, cap,
                 "# Attack Shark peripherals: let the logged-in user talk to\n"
                 "# the hidraw nodes. Written by \"attackshark permissions"
                 " install\";\n"
                 "# regenerating it is safe, local edits are overwritten.\n"
                 "#\n"
                 "# Rules match the vendor id, not a single product id, so a new\n"
                 "# model of the same family works without a new rules file.\n");
    for (size_t i = 0; i < sizeof(families) / sizeof(families[0]); i++) {
        int written = snprintf(text + n, cap - (size_t)n,
                               "\n# %s\n"
                               "SUBSYSTEM==\"hidraw\", ATTRS{idVendor}==\"%s\", "
                               "TAG+=\"uaccess\", MODE=\"0660\"\n",
                               families[i].comment, families[i].vid);

        if (written < 0 || (size_t)written >= cap - (size_t)n) {
            free(text);
            return NULL;
        }
        n += written;
    }
    return text;
}

static int file_has_marker(const char *path)
{
    char buf[4096];
    size_t n;
    FILE *f = fopen(path, "r");

    if (!f) {
        return 0;
    }
    n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = 0;
    return strstr(buf, RULE_MARKER) != NULL;
}

static int has_rules_suffix(const char *name)
{
    size_t len = strlen(name);

    return len > 6 && strcmp(name + len - 6, ".rules") == 0;
}

int as_udev_rules_installed(void)
{
    for (size_t i = 0; i < sizeof(rules_dirs) / sizeof(rules_dirs[0]); i++) {
        DIR *dir = opendir(rules_dirs[i]);
        struct dirent *de;

        if (!dir) {
            continue;
        }
        while ((de = readdir(dir)) != NULL) {
            char path[512];
            int found;

            if (!has_rules_suffix(de->d_name)) {
                continue;
            }
            if (strstr(de->d_name, RULE_MARKER)) {
                closedir(dir);
                return 1;
            }
            snprintf(path, sizeof(path), "%s/%s", rules_dirs[i], de->d_name);
            found = file_has_marker(path);
            if (found) {
                closedir(dir);
                return 1;
            }
        }
        closedir(dir);
    }
    return 0;
}

int as_udev_can_install(void)
{
    return geteuid() == 0;
}

static int write_rules(const char *text, char *err, size_t errlen)
{
    char tmp[256];
    size_t len = strlen(text), done = 0;
    struct stat st;
    int fd, rc = 0;

    if (stat("/etc/udev/rules.d", &st) != 0) {
        if (mkdir("/etc/udev/rules.d", 0755) != 0 && errno != EEXIST) {
            snprintf(err, errlen, "cannot create /etc/udev/rules.d: %s",
                     strerror(errno));
            return -errno;
        }
    }
    snprintf(tmp, sizeof(tmp), "%s.tmp", RULE_PATH);
    fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        snprintf(err, errlen, "cannot write %s: %s", tmp, strerror(errno));
        return -errno;
    }
    while (done < len) {
        ssize_t n = write(fd, text + done, len - done);

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            rc = -errno;
            snprintf(err, errlen, "cannot write %s: %s", tmp, strerror(errno));
            break;
        }
        done += (size_t)n;
    }
    if (close(fd) != 0 && rc == 0) {
        rc = -errno;
        snprintf(err, errlen, "cannot write %s: %s", tmp, strerror(errno));
    }
    if (rc == 0 && rename(tmp, RULE_PATH) != 0) {
        rc = -errno;
        snprintf(err, errlen, "cannot rename %s to %s: %s", tmp, RULE_PATH,
                 strerror(errno));
    }
    if (rc != 0) {
        unlink(tmp);
    }
    return rc;
}

static int run_tool(char *const argv[])
{
    pid_t pid;
    int status;
    int rc = posix_spawnp(&pid, argv[0], NULL, NULL, argv, environ);

    if (rc != 0) {
        return -rc;
    }
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            return -errno;
        }
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return -EIO;
    }
    return 0;
}

int as_udev_install(char *err, size_t errlen)
{
    char *text;
    int rc;

    if (err && errlen) {
        err[0] = 0;
    }
    if (!as_udev_can_install()) {
        snprintf(err, errlen, "%s needs root privileges", RULE_PATH);
        return -EPERM;
    }
    text = as_udev_rule_text();
    if (!text) {
        snprintf(err, errlen, "out of memory");
        return -ENOMEM;
    }
    rc = write_rules(text, err, errlen);
    free(text);
    if (rc < 0) {
        return rc;
    }

    /* udev only applies the tags to nodes that appear after the rule is
     * loaded, so reload and re-trigger the hidraw subsystem: a keyboard that
     * is already plugged in gains its ACL without a replug. */
    char *const reload[] = { "udevadm", "control", "--reload-rules", NULL };
    char *const trigger[] = { "udevadm", "trigger", "--subsystem-match=hidraw",
                              NULL };

    rc = run_tool(reload);
    if (rc == -ENOENT) {
        /* No udevadm: the rules will be picked up on the next plug or boot. */
        return 0;
    }
    if (rc < 0) {
        snprintf(err, errlen, "the rules file is installed, but \"udevadm "
                              "control --reload-rules\" failed: %s",
                 strerror(-rc));
        return rc;
    }
    rc = run_tool(trigger);
    if (rc == -ENOENT) {
        return 0;
    }
    if (rc < 0) {
        snprintf(err, errlen, "the rules file is installed, but \"udevadm "
                              "trigger\" failed: %s; replug the device",
                 strerror(-rc));
        return rc;
    }
    return 0;
}
