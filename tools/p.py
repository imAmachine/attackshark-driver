import fcntl, os
G=0xC0000000|0x07
fd=os.open("/dev/hidraw6",os.O_RDWR)
for ln,rid in ((64,0),(65,0),(64,5)):
    b=bytearray(ln); b[0]=rid
    try:
        fcntl.ioctl(fd, G|(ln<<16)|(ord('H')<<8), b, True); print(ln,rid,"->",bytes(b)[:16].hex())
    except OSError as e: print(ln,rid,"err",e)
os.close(fd)
