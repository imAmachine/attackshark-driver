import fcntl, os, time
S=0xC0000000|0x06; G=0xC0000000|0x07
def feat(fd,req,buf):
    fcntl.ioctl(fd, req|(len(buf)<<16)|(ord('H')<<8), buf, True); return bytes(buf)
def try_set(fd,payload,size,off):
    buf=bytearray(size); buf[off:off+len(payload)]=payload
    try:
        feat(fd,S,buf); return "ok"
    except OSError as e: return str(e)
def try_get(fd,size,rid):
    buf=bytearray(size); buf[0]=rid
    try: return feat(fd,G,buf).hex()
    except OSError as e: return str(e)
fd=os.open("/dev/hidraw6",os.O_RDWR|os.O_NONBLOCK)
print("GET len65 rid0:", try_get(fd,65,0))
print("GET len64 rid0:", try_get(fd,64,0))
print("GET len64 rid8:", try_get(fd,64,8))
for size,off in ((64,0),(65,1),(9,0),(9,1),(65,0)):
    print(f"SET size={size} off={off}:", try_set(fd,bytes([0x8f]),size,off))
    time.sleep(0.1)
print("GET len65 rid0 after:", try_get(fd,65,0))
os.close(fd)
