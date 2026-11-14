import fcntl, os, select, time
S=0xC0000000|0x06; G=0xC0000000|0x07
def io(fd,req,buf):
    t=time.time()
    try:
        fcntl.ioctl(fd, req|(len(buf)<<16)|(ord('H')<<8), buf, True); return bytes(buf), time.time()-t
    except OSError as e: return e, time.time()-t
def poll(fd,secs=0.4):
    got=[]; end=time.time()+secs
    while time.time()<end:
        if select.select([fd],[],[],0.05)[0]:
            got.append(os.read(fd,64).hex())
    return got
fd=os.open("/dev/hidraw6",os.O_RDWR|os.O_NONBLOCK)
poll(fd,0.2)
variants={}
# (payload builder, set_len, get_len)
def mk(cmd,pos,cs_pos,size=64,extra=None):
    p=bytearray(size); p[pos]=cmd
    if extra: p[pos+1]=extra
    if cs_pos is not None:
        p[cs_pos]=(0xFF-(sum(p[:cs_pos])&0xFF))&0xFF
    return p
for name,(p,sl,gl) in {
 "A cmd0 cs7 set65 get65": (mk(0x8f,0,7),65,65),
 "B cmd0 cs8 set65 get65": (mk(0x8f,0,8),65,65),
 "C cmd1 cs8 set65 get65": (mk(0x8f,1,8),65,65),
 "D cmd0 cs7 set64 get64": (mk(0x8f,0,7),64,64),
 "E cmd1 cs7 set65 get64": (mk(0x8f,1,7),65,64),
 "F cmd0 noc cs0 set65 get65": (mk(0x8f,0,None),65,65),
 "G cmd1 cs8 arg0 set65 get65": (mk(0x8f,1,8,extra=0),65,65),
}.items():
    b=bytearray(sl)
    if sl==65: b[1:]=p[:64]
    else: b[:]=p[:64]
    r,t=io(fd,S,b)
    print(f"{name}: set={'ok' if not isinstance(r,OSError) else r} {t:.2f}s", end="")
    ra=poll(fd,0.2)
    gb=bytearray(gl); gb[0]=0
    r2,t2=io(fd,G,gb)
    if isinstance(r2,OSError): print(f"  get=ERR({r2.errno}) {t2:.2f}s int={ra}")
    else: print(f"  get={bytes(r2)[:12].hex()} int={ra}")
os.close(fd)
