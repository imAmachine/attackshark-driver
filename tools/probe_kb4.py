import os, select, fcntl, time
S=0xC0000000|0x06; G=0xC0000000|0x07
def feat(fd,req,buf):
    fcntl.ioctl(fd, req|(len(buf)<<16)|(ord('H')<<8), buf, True); return bytes(buf)
def cs(payload, pos=7):
    p=bytearray(payload)+bytearray(64-len(payload))
    p[pos]=(0xFF-(sum(p[:pos])&0xFF))&0xFF
    return p
def poll(fd,secs=0.7):
    got=[]; end=time.time()+secs
    while time.time()<end:
        if select.select([fd],[],[],0.05)[0]:
            got.append(os.read(fd,64).hex())
    return got
fd=os.open("/dev/hidraw6",os.O_RDWR|os.O_NONBLOCK)
poll(fd,0.2)
print("A) feature SET + read:")
p=cs([0x8f]); b=bytearray(65); b[1:]=p
try: feat(fd,S,b); print("  set ok")
except OSError as e: print("  set err",e)
print("  poll:",poll(fd))
print("B) output write 65 ([0]+payload):")
b=bytearray(65); b[1:]=cs([0x8f])
try: print("  wrote",os.write(fd,bytes(b)))
except OSError as e: print("  write err",e)
print("  poll:",poll(fd))
print("C) output write 64 (cmd first):")
try: print("  wrote",os.write(fd,bytes(cs([0x8f]))))
except OSError as e: print("  write err",e)
print("  poll:",poll(fd))
print("D) feature GET 65:")
buf=bytearray(65)
try: print("  ",feat(fd,G,buf).hex())
except OSError as e: print("  err",e)
print("E) feature SET then GET 80ms (cmd 0x87):")
b=bytearray(65); b[1:]=cs([0x87])
try:
    feat(fd,S,b); time.sleep(0.08); buf=bytearray(65); print("  ",feat(fd,G,buf).hex())
except OSError as e: print("  err",e)
os.close(fd)
