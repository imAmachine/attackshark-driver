import fcntl, os, select, time
S=0xC0000000|0x06; G=0xC0000000|0x07
def feat(fd,req,buf):
    fcntl.ioctl(fd, req|(len(buf)<<16)|(ord('H')<<8), buf, True); return bytes(buf)
def drain(fd):
    while select.select([fd],[],[],0.05)[0]:
        try: os.read(fd,64)
        except OSError: return
fd=os.open("/dev/hidraw6",os.O_RDWR|os.O_NONBLOCK)
drain(fd)
print("idle poll (no cmd):")
end=time.time()+0.5
while time.time()<end:
    if select.select([fd],[],[],0.05)[0]:
        print("  idle rx:", os.read(fd,64).hex())
def send(payload,bit8=False):
    p=bytearray(payload)+bytearray(64-len(payload))
    if bit8: p[8]=(0xFF-(sum(p[0:8])&0xFF))&0xFF
    else: p[7]=(0xFF-(sum(p[0:7])&0xFF))&0xFF
    buf=bytearray(65); buf[1:]=p; feat(fd,S,buf); return p
for name,payload,bit8 in (("USB_VERSION",[0x8f],False),("REV",[0x80],False),("LED",[0x87],False),("PROFILE",[0x85],False),("REPORT",[0x84,0],False),("MAGMODE",[0x9d],False)):
    p=send(payload,bit8)
    print(f"sent {name} {p[:9].hex()}")
    end=time.time()+0.6; got=[]
    while time.time()<end:
        if select.select([fd],[],[],0.05)[0]:
            got.append(os.read(fd,64).hex())
    print("  rx:", got)
os.close(fd)
