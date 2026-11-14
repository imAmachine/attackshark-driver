import os, select, time
R=8
def pkt(cmd,addr=0,length=0,payload=b""):
    p=bytearray(16); p[0]=cmd; p[2]=(addr>>8)&0xFF; p[3]=addr&0xFF; p[4]=length
    p[5:5+len(payload)]=payload; p[15]=((85-(sum(p[:15])&0xFF))-R)&0xFF; return bytes(p)
def drain(fd):
    while select.select([fd],[],[],0.02)[0]:
        try: os.read(fd,64)
        except OSError: return
def x(fd,p,tries=3,window=0.3):
    got=[]
    for _ in range(tries):
        os.write(fd,bytes([R])+p)
        end=time.time()+window
        while time.time()<end:
            if select.select([fd],[],[],0.02)[0]:
                got.append(os.read(fd,64).hex())
    return got
fds={n:os.open(n,os.O_RDWR|os.O_NONBLOCK) for n in ["/dev/hidraw1","/dev/hidraw2","/dev/hidraw3"]}
for fd in fds.values(): drain(fd)
print("hs:", x(fds["/dev/hidraw2"], pkt(1,0,8,bytes([1,2,3,4,0,0,0,0])),2,0.4))
# wake attempts
for val in (1,0):
    print(f"PCDriverStatus({val}):", x(fds["/dev/hidraw2"], pkt(2,0,1,bytes([val])),1,0.3))
print("online:", x(fds["/dev/hidraw2"], pkt(3)))
print("battery:", x(fds["/dev/hidraw2"], pkt(4),5,0.4))
print("ver:", x(fds["/dev/hidraw2"], pkt(18),5,0.4))
print("read0:", x(fds["/dev/hidraw2"], pkt(8,0,10),5,0.4))
# check for any traffic on other interfaces during a 1.5s window
end=time.time()+1.5; hits={n:[] for n in fds}
while time.time()<end:
    for n,fd in fds.items():
        if select.select([fd],[],[],0.01)[0]:
            hits[n].append(os.read(fd,64).hex())
for n,v in hits.items(): print(n, "traffic:", v[:5], "count", len(v))
os.close(fds["/dev/hidraw2"])
