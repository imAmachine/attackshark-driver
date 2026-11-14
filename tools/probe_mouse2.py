import os, select, time
REPORT_ID = 8
def checksum(p):
    at = (85 - (sum(p[0:15]) & 0xFF)) & 0xFF
    p[15] = (at - REPORT_ID) & 0xFF
def build(cmd, addr=0, length=0, payload=b""):
    p = bytearray(16); p[0]=cmd; p[2]=(addr>>8)&0xFF; p[3]=addr&0xFF; p[4]=length
    p[5:5+len(payload)]=payload; checksum(p); return bytes(p)
def drain(fd):
    while select.select([fd],[],[],0.03)[0]:
        try: os.read(fd,64)
        except OSError: return
def exchange(fd, pkt, tries=5, window=0.25, echo=3):
    for t in range(tries):
        os.write(fd, bytes([REPORT_ID])+pkt)
        end=time.time()+window
        while time.time()<end:
            if select.select([fd],[],[],0.03)[0]:
                d=os.read(fd,64)
                if len(d)>=echo+1 and d[0]==REPORT_ID and d[1:1+echo]==pkt[:echo]:
                    return d, t+1
    return None, tries
fd=os.open("/dev/hidraw2", os.O_RDWR); drain(fd)
r,n=exchange(fd, build(1,0,8,bytes([0x4b,0xb3,0x7c,0xde,0,0,0,0])))
print("handshake:", r.hex() if r else None, "tries", n)
tests = [("ver",build(18)),("battery",build(4)),("read0_10",build(8,0,10)),("read0_16",build(8,0,16)),("getcfg",build(14)),("online",build(3)),("dongle",build(29))]
for name,pkt in tests:
    r,n=exchange(fd,pkt)
    print(f"{name:10s} tx={pkt.hex()} rx={r.hex() if r else None} tries={n}")
os.close(fd)
