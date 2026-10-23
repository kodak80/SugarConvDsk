"""Shorten a trailing fuzzy element: fuzzy N -> fuzzy N-K + K data bytes of 0x00 (all tracks).
Usage: ipfsplitfuzzy.py IN OUT K"""
import struct, sys, zlib
def records(d):
    p=0
    while p<len(d):
        t=d[p:p+4]; l=struct.unpack('>I',d[p+4:p+8])[0]; extra=0
        if t==b'DATA': extra=struct.unpack('>I',d[p+12:p+16])[0]
        yield t,bytearray(d[p:p+l]),bytearray(d[p+l:p+l+extra]); p+=l+extra
def fix_crc(rec):
    rec[8:12]=b'\0'*4; rec[8:12]=struct.pack('>I',zlib.crc32(bytes(rec))&0xFFFFFFFF)
K=int(sys.argv[3]); recs=list(records(open(sys.argv[1],'rb').read())); nblk={}
for t,rec,_ in recs:
    if t==b'IMGE':
        v=struct.unpack('>17I',rec[12:80]); nblk[v[13]]=v[10]
o=bytearray(); changed=0
for t,rec,pl in recs:
    if t==b'DATA':
        key=struct.unpack(">I",rec[24:28])[0]; nb=nblk.get(key,0)
        if nb==0 or not pl: o+=rec+pl; continue
        blks=[list(struct.unpack('>8I',pl[i*32:i*32+32])) for i in range(nb)]
        first=min(b[7] for b in blks); new=bytearray(pl[:first])
        for b in blks:
            off=b[7]; b[7]=len(new); inbits=b[5]&4; els=[]
            while True:
                h=pl[off]; off+=1
                if h==0: break
                szw=h>>5; typ=h&31; sz=int.from_bytes(pl[off:off+szw],'big'); off+=szw
                nbytes=0 if typ==5 else ((sz+7)//8 if inbits else sz)
                els.append([typ,sz,bytes(pl[off:off+nbytes])]); off+=nbytes
            if not inbits and els and els[-1][0]==5 and els[-1][1]>K+8:
                els[-1][1]-=K; els.append([2,K,b'\0'*K]); changed+=1
            for typ,sz,data in els:
                szw=1 if sz<256 else 2 if sz<65536 else 3
                new+=bytes([(szw<<5)|typ])+sz.to_bytes(szw,'big')+data
            new.append(0)
        for i,b in enumerate(blks): new[i*32:i*32+32]=struct.pack('>8I',*b)
        rec[12:16]=struct.pack('>I',len(new)); rec[16:20]=struct.pack('>I',len(new)*8)
        rec[20:24]=struct.pack('>I',zlib.crc32(bytes(new))&0xFFFFFFFF); fix_crc(rec); pl=new
    o+=rec+pl
open(sys.argv[2],'wb').write(o); print('tracks changed',changed)
