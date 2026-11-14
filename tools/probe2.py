import fcntl, os, sys
HIDIOCGFEATURE = 0xC0000000
def getfeature(fd, length, rid):
    buf = bytearray(length); buf[0] = rid
    req = HIDIOCGFEATURE | (length << 16) | (ord('H') << 8) | 0x07
    fcntl.ioctl(fd, req, buf, True)
    return bytes(buf)
for dev in sys.argv[1:]:
    print("==", dev)
    fd = os.open(dev, os.O_RDWR)
    for ln in (8, 9, 16, 32, 63, 64, 65, 128):
        for rid in (0, 2, 5, 8, 16):
            try:
                print(f"  len={ln} rid={rid:#04x}: {getfeature(fd, ln, rid)[:24].hex()}")
            except OSError as e:
                pass
    os.close(fd)
