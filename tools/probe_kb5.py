import fcntl, os, time
S=0xC0000000|0x06; G=0xC0000000|0x07
def io(fd,req,buf):
    fcntl.ioctl(fd, req|(len(buf)<<16)|(ord('H')<<8), buf, True); return bytes(buf)
def mk(cmd,pos=0,size=64):
    p=bytearray(size); p[pos]=cmd; p[pos+7]=(0xFF-(sum(p[0:pos+7])&0xFF))&0xFF
    return p
fd=os.open("/dev/hidraw6",os.O_RDWR|os.O_NONBLOCK)
for rid in [0,1,2,3,4,5,6,7,8,9,0x10]:
    # GET only
    b=bytearray(65); b[0]=rid
    try: r=io(fd,G,b); got=r[1:17].hex()
    except OSError as e: got="err:%s"%e
    print(f"GET rid={rid:#04x} -> {got}")
print("--- set 0x8f then GET each rid")
b=bytearray(65); b[1:]=mk(0x8f); io(fd,S,b); time.sleep(0.1)
for rid in [0,2,5,6,8]:
    b=bytearray(65); b[0]=rid
    try: r=io(fd,G,b); got=r[1:17].hex()
    except OSError as e: got="err:%s"%e
    print(f"GET rid={rid:#04x} -> {got}")
os.close(fd)
