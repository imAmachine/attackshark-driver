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
 *
 * Two details make the difference between a rules file that works and one that
 * only looks installed:
 *
 * - The file has to be read before "73-seat-late.rules". That rule runs the
 *   uaccess builtin for the tags set so far, so a file that sets the tag later
 *   (the driver used to be called "99-attackshark.rules") changes the mode of
 *   the node but no ACL is ever granted: the node stays root-only. The rules
 *   also ask for the builtin themselves, so a renamed copy keeps working.
 * - ATTRS{id/vendor} is the HID bus attribute, present on the parent of every
 *   hidraw node, so it matches the USB cable, the 2.4 GHz receiver and the
 *   Bluetooth link of the same device. ATTRS{idVendor} only matches USB.
 */
#define _GNU_SOURCE
#include "internal.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

/* Numbered before 73-seat-late.rules: see the comment at the top of the file. */
#define RULE_FILE "70-attackshark.rules"
/* The name the driver used before the ordering bug was fixed. It is still
 * recognised so an upgrade replaces it instead of reporting it as installed. */
#define LEGACY_RULE_FILE "99-attackshark.rules"
#define RULE_DIR "/etc/udev/rules.d"
#define RULE_PATH RULE_DIR "/" RULE_FILE
#define LEGACY_RULE_PATH RULE_DIR "/" LEGACY_RULE_FILE
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
    RULE_DIR,
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
    static const char header[] =
        "# Attack Shark peripherals: let the logged-in user talk to\n"
        "# the hidraw nodes. Written by \"attackshark permissions install\";\n"
        "# regenerating it is safe, local edits are overwritten.\n"
        "#\n"
        "# Rules match the vendor id, not a single product id, so a new\n"
        "# model of the same family works without a new rules file.\n"
        "#\n"
        "# The file name has to sort before 73-seat-late.rules: that rule\n"
        "# runs the uaccess builtin for the tags set by the files read so\n"
        "# far, so a file with a higher number grants no ACL at all. For the\n"
        "# same reason the rules run the builtin themselves, which keeps a\n"
        "# renamed copy working.\n";
    char *text = NULL;
    size_t len = 0;
    FILE *f = open_memstream(&text, &len);

    if (!f) {
        return NULL;
    }
    fputs(header, f);
    for (size_t i = 0; i < AS_N(families); i++) {
        /* id/vendor is the HID bus attribute: USB, 2.4 GHz and Bluetooth
         * links of the same vendor all carry it on the parent of the node. */
        fprintf(f, "\n# %s\n"
                   "SUBSYSTEM==\"hidraw\", ATTRS{id/vendor}==\"%s\", "
                   "TAG+=\"uaccess\", MODE=\"0660\", "
                   "RUN{builtin}+=\"uaccess\"\n",
                families[i].comment, families[i].vid);
    }
    if (fclose(f) != 0) {
        free(text);
        return NULL;
    }
    return text;
}

/* The rules file has to be recognised even when it was renamed by the
 * distribution, so the name and the content are both checked. The 99-prefixed
 * file is ours too, but it grants nothing, so it does not count as installed:
 * that keeps "permissions status" honest and makes the fix run again. */
static int is_rules_file(const char *dir, const char *name)
{
    char buf[4096], path[512];
    size_t len = strlen(name), n;
    FILE *f;

    if (len <= 6 || strcmp(name + len - 6, ".rules") != 0) {
        return 0;
    }
    if (strcmp(name, LEGACY_RULE_FILE) == 0) {
        return 0;
    }
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    f = fopen(path, "r");
    if (!f) {
        return 0;
    }
    n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = 0;
    return strstr(buf, "SUBSYSTEM=\"hidraw\"") != NULL &&
           strstr(buf, "ATTRS{id/vendor}==\"3151\"") != NULL &&
           strstr(buf, "ATTRS{id/vendor}==\"3554\"") != NULL &&
           strstr(buf, RULE_MARKER) != NULL;
}

int as_udev_rules_installed(void)
{
    for (size_t i = 0; i < AS_N(rules_dirs); i++) {
        DIR *dir = opendir(rules_dirs[i]);
        struct dirent *de;

        if (!dir) {
            continue;
        }
        while ((de = readdir(dir)) != NULL) {
            if (is_rules_file(rules_dirs[i], de->d_name)) {
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

static int write_all(int fd, const char *text, size_t len)
{
    while (len > 0) {
        ssize_t n = write(fd, text, len);

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -errno;
        }
        text += n;
        len -= (size_t)n;
    }
    return 0;
}

static int write_rules(const char *text, char *err, size_t errlen)
{
    char tmp[256];
    int fd, rc;

    if (mkdir(RULE_DIR, 0755) != 0 && errno != EEXIST) {
        snprintf(err, errlen, "cannot create %s: %s", RULE_DIR,
                 strerror(errno));
        return -errno;
    }
    snprintf(tmp, sizeof(tmp), "%s.tmp", RULE_PATH);
    fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        snprintf(err, errlen, "cannot write %s: %s", tmp, strerror(errno));
        return -errno;
    }
    rc = write_all(fd, text, strlen(text));
    if (close(fd) != 0 && rc == 0) {
        rc = -errno;
    }
    if (rc == 0 && rename(tmp, RULE_PATH) != 0) {
        rc = -errno;
    }
    if (rc < 0) {
        snprintf(err, errlen, "cannot write %s: %s", tmp, strerror(-rc));
        unlink(tmp);
    }
    return rc;
}

static int run_tool(char *const argv[])
{
    pid_t pid;
    int status;
    int rc = posix_spawn(&pid, argv[0], NULL, NULL, argv, environ);

    if (rc != 0) {
        return -rc;
    }
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            return -errno;
        }
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -EIO;
}

int as_udev_install(char *err, size_t errlen)
{
    char *const reload[] = { "/usr/bin/udevadm", "udevadm", "control",
                             "--reload-rules", NULL };
    char *const trigger[] = { "/usr/bin/udevadm", "udevadm", "trigger",
                              "--subsystem-match=hidraw", NULL };
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
    /* An older install may still carry the 99-prefixed file. It changes the
     * mode of the node but grants no access, so drop it before the reload
     * instead of leaving a misleading second copy behind. */
    (void)unlink(LEGACY_RULE_PATH);

    /* udev only applies the tags to nodes that appear after the rule is
     * loaded, so reload and re-trigger the hidraw subsystem: a keyboard that
     * is already plugged in gains its ACL without a replug. */
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
    }
    return rc;
}
