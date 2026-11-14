#!/usr/bin/env python3
"""Probe Attack Shark R85 HE (3151:5029) using the GearHub 64-byte feature protocol."""
import fcntl, os, sys, time

HIDIOCSFEATURE = 0xC0000000 | 0x06
HIDIOCGFEATURE = 0xC0000000 | 0x07
REPORT_SIZE = 64

def ioctl_feature(fd, req, buf):
    fcntl.ioctl(fd, req | (len(buf) << 16) | (ord('H') << 8), buf, True)
    return bytes(buf)

def checksum_bit7(payload):
    p = bytearray(payload) + bytearray(9 - len(payload))
    p[7] = (0xFF - (sum(p[0:7]) & 0xFF)) & 0xFF
    return p

def send(fd, payload, bit8=False):
    p = bytearray(payload) + bytearray(64 - len(payload))
    if bit8:
        p[8] = (0xFF - (sum(p[0:8]) & 0xFF)) & 0xFF
    else:
        p[7] = (0xFF - (sum(p[0:7]) & 0xFF)) & 0xFF
    buf = bytearray(65)
    buf[1:] = p
    ioctl_feature(fd, HIDIOCSFEATURE, buf)
    return bytes(p)

def recv(fd):
    buf = bytearray(65)
    ioctl_feature(fd, HIDIOCGFEATURE, buf)
    return bytes(buf[1:])

def main():
    fd = os.open("/dev/hidraw6", os.O_RDWR)
    cmds = {
        "USB_VERSION": [0x8F],
        "REV": [0x80],
        "PROFILE": [0x85],
        "REPORT": [0x84, 0],
        "DEBOUNCE": [0x86],
        "LED": [0x87],
        "MAG_MODE": [0x9D],
        "MULTI_MAG_ACT": [0xE5, 0, 1, 0],
    }
    for name, payload in cmds.items():
        try:
            sent = send(fd, payload)
            time.sleep(0.03)
            r = recv(fd)
            print(f"{name:16s} tx={sent[:9].hex()} rx={r[:24].hex()}")
        except OSError as e:
            print(f"{name:16s} err {e}")
    os.close(fd)

main()
