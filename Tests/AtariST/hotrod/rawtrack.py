"""Replace one track of an IPF with a single raw-cell block (like Aufit writes stream tracks).
Usage: rawtrack.py IN OUT cyl head BITSFILE   (BITSFILE: text of 0/1 cells, index at cell 0; length multiple of 8)"""
import struct, sys, zlib
def records(d):
    p=0
    while p<len(d):
        t=d[p:p+4]; l=struct.unpack('>I',d[p+4:p+8])[0]; extra=0
        if t==b'DATA': extra=struct.unpack('>I',d[p+12:p+16])[0]
        yield t,bytearray(d[p:p+l]),bytearray(d[p+l:p+l+extra]); p+=l+extra
def fix_crc(rec):
    rec[8:12]=b'\0'*4; rec[8:12]=struct.pack('>I',zlib.crc32(bytes(rec))&0xFFFFFFFF)
def main(inp,out,cyl,head,bits):
    assert len(bits)%8==0
    raw=int(bits,2).to_bytes(len(bits)//8,'big'); n=len(raw)
    recs=list(records(open(inp,'rb').read())); key=None; o=bytearray()
    for t,rec,pl in recs:
        if t==b'IMGE':
            v=list(struct.unpack('>17I',rec[12:80]))
            if v[0]==cyl and v[1]==head:
                key=v[13]; v[2]=2; v[4]=n; v[5]=0; v[6]=0; v[7]=len(bits); v[8]=0; v[9]=len(bits); v[10]=1
                rec[12:80]=struct.pack('>17I',*v); fix_crc(rec)
        if t==b'DATA' and struct.unpack('>I',rec[24:28])[0]==key:
            szw=1 if n<256 else 2 if n<65536 else 3
            pl=bytearray(struct.pack('>8I',len(bits),0,0,1,1,0,0x4e,32))+bytes([(szw<<5)|1])+n.to_bytes(szw,'big')+raw+b'\0'
            rec[12:16]=struct.pack('>I',len(pl)); rec[16:20]=struct.pack('>I',len(pl)*8)
            rec[20:24]=struct.pack('>I',zlib.crc32(bytes(pl))&0xFFFFFFFF); fix_crc(rec)
        o+=rec+pl
    open(out,'wb').write(o)
if __name__=='__main__':
    main(sys.argv[1],sys.argv[2],int(sys.argv[3]),int(sys.argv[4]),open(sys.argv[5]).read().strip())
