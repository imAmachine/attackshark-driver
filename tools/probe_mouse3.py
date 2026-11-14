import os, select, time
REPORT_ID=8
def checksum(p):
    at=(85-(sum(p[0:15])&0xFF))&0xFF; p[15]=(at-REPORT_ID)&0xFF
def build(cmd, addr=0, length=0, payload=b""):
    p=bytearray(16); p[0]=cmd; p[2]=(addr>>8)&0xFF; p[3]=addr&0xFF; p[4]=length
    p[5:5+len(payload)]=payload; checksum(p); return bytes(p)
def drain(fd):
    while select.select([fd],[],[],0.03)[0]:
        try: os.read(fd,64)
        except OSError: return
def dump(fd, pkt, window=0.8, label=""):
    os.write(fd, bytes([REPORT_ID])+pkt)
    got=[]; end=time.time()+window
    while time.time()<end:
        if select.select([fd],[],[],0.05)[0]:
            got.append(os.read(fd,64).hex())
    print(f"{label:22s} tx={pkt.hex()} rx={got}")
fd=os.open("/dev/hidraw2", os.O_RDWR); drain(fd)
dump(fd, build(1,0,8,bytes([0x4b,0xb3,0x7c,0xde,0,0,0,0])), 1.0, "handshake")
time.sleep(0.3)
for cmd,label in ((2,"PCDriverStatus"),(18,"VersionID"),(4,"Battery"),(14,"GetConfig"),(8,"ReadFlash0-10")):
    pkt = build(8,0,10) if cmd==8 else build(cmd)
    dump(fd,pkt,0.8,label); time.sleep(0.2)
os.close(fd)
