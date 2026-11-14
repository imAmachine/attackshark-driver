// SPDX-License-Identifier: MIT
#include "attackshark.h"

#include <errno.h>
#include <string.h>

const char *as_strerror(int err)
{
    switch (-err) {
    case ENODEV: return "device not found";
    case EACCES: return "permission denied (install the udev rules)";
    case ETIMEDOUT: return "device did not answer (is it awake / plugged in?)";
    case EPROTO: return "unexpected reply from device";
    case ENODATA: return "device returned no data";
    case EINVAL: return "invalid argument";
    default: return strerror(-err);
    }
}
