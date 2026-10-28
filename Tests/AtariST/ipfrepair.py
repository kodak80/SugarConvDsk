"""Repair IPFs written by SugarConvDsk before 48daf8a: weak elements in bit-sized blocks (cells ->
data bits) and IMGE track bytes (sum of blocks rounded up). Usage: ipfrepair.py IN OUT"""
import struct, sys, zlib
def records(d):
    p=0
    while p<len(d):
        t=d[p:p+4]; l=struct.unpack('>I',d[p+4:p+8])[0]; extra=0
        if t==b'DATA': extra=struct.unpack('>I',d[p+12:p+16])[0]
        yield t,bytearray(d[p:p+l]),bytearray(d[p+l:p+l+extra]); p+=l+extra
def fix_crc(rec):
    rec[8:12]=b'\0'*4; rec[8:12]=struct.pack('>I',zlib.crc32(bytes(rec))&0xFFFFFFFF)
recs=list(records(open(sys.argv[1],'rb').read())); imge={}
for i,(t,rec,pl) in enumerate(recs):
    if t==b'IMGE': imge[struct.unpack('>I',rec[12+13*4:12+14*4])[0]]=i
fixed_weak=0
for t,rec,pl in recs:
    if t!=b'DATA' or not pl: continue
    key=struct.unpack('>I',rec[24:28])[0]; irec=recs[imge[key]][1]
    v=list(struct.unpack('>17I',irec[12:80])); nb=v[10]
    total=0
    for i in range(nb):
        b=struct.unpack('>8I',pl[i*32:i*32+32]); total+=(b[0]+b[1]+7)//8
        if b[5]&4:
            off=b[7]
            while True:
                h=pl[off]; 
                if h==0: break
                szw=h>>5; typ=h&31; sz=int.from_bytes(pl[off+1:off+1+szw],'big')
                if typ==5:
                    pl[off+1:off+1+szw]=(sz//2).to_bytes(szw,'big'); fixed_weak+=1
                    off+=1+szw
                else:
                    off+=1+szw+((sz+7)//8)
    v[4]=total; irec[12:80]=struct.pack('>17I',*v); fix_crc(irec)
    rec[20:24]=struct.pack('>I',zlib.crc32(bytes(pl))&0xFFFFFFFF); fix_crc(rec)
open(sys.argv[2],'wb').write(b''.join(bytes(r)+bytes(p) for _,r,p in recs))
print(sys.argv[2],'weak elements fixed',fixed_weak)
