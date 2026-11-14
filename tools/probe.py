#!/usr/bin/env python3
"""Read-only HID feature-report probe for Attack Shark devices."""
import ctypes, fcntl, os, sys

HIDIOCGFEATURE = 0xC0000000

def getfeature(fd, length, rid):
    buf = bytearray(length)
    buf[0] = rid
    req = HIDIOCGFEATURE | (length << 16) | (ord('H') << 8) | 0x07
    fcntl.ioctl(fd, req, buf, True)
    return bytes(buf)

def main():
    dev = sys.argv[1]
    fd = os.open(dev, os.O_RDWR)
    try:
        print("=== report id 0, 64 bytes")
        for _ in range(1):
            print(getfeature(fd, 64, 0).hex())
        for rid in range(0, 17):
            try:
                print(f"rid {rid:02x}:", getfeature(fd, 64, rid)[:32].hex())
            except OSError as e:
                print(f"rid {rid:02x}: err {e}")
    finally:
        os.close(fd)

main()
