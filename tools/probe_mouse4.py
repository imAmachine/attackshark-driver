import os, select, time
R=8
def pkt(cmd,addr=0,length=0,payload=b""):
    p=bytearray(16); p[0]=cmd; p[2]=(addr>>8)&0xFF; p[3]=addr&0xFF; p[4]=length
    p[5:5+len(payload)]=payload; p[15]=((85-(sum(p[:15])&0xFF))-R)&0xFF; return bytes(p)
def drain(fd):
    while select.select([fd],[],[],0.03)[0]:
        try: os.read(fd,64)
        except OSError: return
def retry(fd,p,tries=20,window=0.25):
    got=[]
    for _ in range(tries):
        os.write(fd,bytes([R])+p)
        end=time.time()+window
        while time.time()<end:
            if select.select([fd],[],[],0.02)[0]:
                d=os.read(fd,64)
                got.append(d.hex())
                if d[1:2]==p[:1]: return got
    return got
fd=os.open("/dev/hidraw2",os.O_RDWR); drain(fd)
print("hs", retry(fd,pkt(1,0,8,bytes([1,2,3,4,0,0,0,0])),3,0.4))
for name,p in (("ver",pkt(18)),("battery",pkt(4)),("read0",pkt(8,0,10)),("getcfg",pkt(14))):
    print(name, retry(fd,p))
os.close(fd)
