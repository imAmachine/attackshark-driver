import fcntl, os, sys
HIDIOCGFEATURE = 0xC0000000
def getfeature(fd, length, rid):
    buf = bytearray(length); buf[0] = rid
    req = HIDIOCGFEATURE | (length << 16) | (ord('H') << 8) | 0x07
    fcntl.ioctl(fd, req, buf, True)
    return bytes(buf)
fd = os.open("/dev/hidraw2", os.O_RDWR)
for ln in (7, 8, 16, 64):
    for rid in (6, 8, 0x10):
        try: print(f"hidraw2 len={ln} rid={rid:#04x}: {getfeature(fd,ln,rid)[:16].hex()}")
        except OSError as e: print(f"hidraw2 len={ln} rid={rid:#04x}: err {e}")
os.close(fd)
fd = os.open("/dev/hidraw6", os.O_RDWR)
for rid in (0,1,2,3,4,5,6,7,8,9,0x0a,0x10,0x11,0x18,0x20,0x55,0xaa,0xff):
    try:
        a = getfeature(fd, 64, rid)
        print(f"hidraw6 rid={rid:#04x}: {a[:20].hex()}")
    except OSError as e: print(f"hidraw6 rid={rid:#04x}: err {e}")
os.close(fd)
