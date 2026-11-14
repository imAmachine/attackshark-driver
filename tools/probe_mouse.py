#!/usr/bin/env python3
"""Probe Attack Shark R11 Ultra (3554:fb44) ControlHub 16-byte report-8 protocol."""
import os, select, sys, time

REPORT_ID = 8

def checksum(p):
    at = (85 - (sum(p[0:15]) & 0xFF)) & 0xFF
    p[15] = (at - REPORT_ID) & 0xFF

def build(cmd, addr=0, length=0, payload=b""):
    p = bytearray(16)
    p[0] = cmd
    p[1] = 0
    p[2] = (addr >> 8) & 0xFF
    p[3] = addr & 0xFF
    p[4] = length
    p[5:5 + len(payload)] = payload
    checksum(p)
    return bytes(p)

def drain(fd):
    while True:
        r, _, _ = select.select([fd], [], [], 0.05)
        if not r:
            return
        try:
            os.read(fd, 64)
        except OSError:
            return

def send_read(fd, pkt, timeout=0.5):
    os.write(fd, bytes([REPORT_ID]) + pkt)
    end = time.time() + timeout
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.05)
        if not r:
            continue
        data = os.read(fd, 64)
        if len(data) >= 2 and data[0] == REPORT_ID and data[1] == pkt[0]:
            return data
    return None

def main():
    fd = os.open("/dev/hidraw2", os.O_RDWR)
    drain(fd)
    # handshake: cmd 1, 8-byte payload (4 random + 4 zeros)
    pkt = build(1, 0, 8, bytes([0x4b, 0xb3, 0x7c, 0xde, 0, 0, 0, 0]))
    r = send_read(fd, pkt)
    print("handshake tx:", pkt.hex())
    print("handshake rx:", r.hex() if r else None)
    if r:
        r2 = r[1:]
        print("status=%d cid=%#x mid=%#x type=%d" % (r2[1], r2[9], r2[10], r2[11]))
    # read flash address 0, 16 bytes
    for cmd, name in ((8, "ReadFlash"), (18, "VersionID"), (4, "Battery"), (14, "GetConfig"), (3, "OnLine"), (29, "DongleVer")):
        pkt = build(cmd, 0, 16 if cmd == 8 else 0)
        r = send_read(fd, pkt)
        print(f"{name:10s} tx={pkt.hex()} rx={r.hex() if r else None}")
    os.close(fd)

main()
