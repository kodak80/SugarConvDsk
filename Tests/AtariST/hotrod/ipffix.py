"""Replace the fuzzy elements of one track's data stream by fixed data bytes.
Usage: ipffix.py IN OUT cyl head HEXBYTES"""
import struct, sys, zlib
def records(d):
    p=0
    while p<len(d):
        t=d[p:p+4]; l=struct.unpack('>I',d[p+4:p+8])[0]; extra=0
        if t==b'DATA': extra=struct.unpack('>I',d[p+12:p+16])[0]
        yield t,bytearray(d[p:p+l]),bytearray(d[p+l:p+l+extra]); p+=l+extra
def fix_crc(rec):
    rec[8:12]=b'\0'*4; rec[8:12]=struct.pack('>I',zlib.crc32(bytes(rec))&0xFFFFFFFF)
inp,out,cyl,head,fill=sys.argv[1],sys.argv[2],int(sys.argv[3]),int(sys.argv[4]),bytes.fromhex(sys.argv[5])
recs=list(records(open(inp,'rb').read())); key=None
for t,rec,_ in recs:
    if t==b'IMGE':
        v=struct.unpack('>17I',rec[12:80])
        if v[0]==cyl and v[1]==head: key=v[13]; nblk=v[10]
o=bytearray()
for t,rec,pl in recs:
    if t==b'DATA' and struct.unpack('>I',rec[24:28])[0]==key:
        blks=[list(struct.unpack('>8I',pl[i*32:i*32+32])) for i in range(nblk)]
        new=bytearray(); # rebuild data streams per block, keep everything else
        # copy gap streams and region before first data stream
        first=min(b[7] for b in blks)
        new+=pl[:first]
        for b in blks:
            off=b[7]; b[7]=len(new); inbits=b[5]&4
            while True:
                h=pl[off]; off+=1
                if h==0: new.append(0); break
                szw=h>>5; typ=h&31; sz=int.from_bytes(pl[off:off+szw],'big'); off+=szw
                if typ==5:
                    data=(fill*sz)[:sz]; new+=bytes([0x20|2|0]) if False else b''
                    new+=bytes([(1<<5)|2, sz])+data if sz<256 else bytes([(2<<5)|2])+sz.to_bytes(2,'big')+data
                    print('fuzzy',sz,'->',data.hex())
                else:
                    nb=(sz+7)//8 if inbits else sz
                    new+=bytes([h])+pl[off-szw:off]+pl[off:off+nb]; off+=nb
        for i,b in enumerate(blks): new[i*32:i*32+32]=struct.pack('>8I',*b)
        rec[12:16]=struct.pack('>I',len(new)); rec[16:20]=struct.pack('>I',len(new)*8)
        rec[20:24]=struct.pack('>I',zlib.crc32(bytes(new))&0xFFFFFFFF); fix_crc(rec)
        o+=rec+new
    else: o+=rec+pl
open(out,'wb').write(o)
