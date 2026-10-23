"""Set the IMGE start bit (index position) of tracks. Usage: ipfstart.py IN OUT C:BIT [C:BIT ...] (head 0)"""
import struct, sys, zlib
sys.argv, args = sys.argv[:3], sys.argv[3:]
def records(d):
    p=0
    while p<len(d):
        t=d[p:p+4]; l=struct.unpack('>I',d[p+4:p+8])[0]; extra=0
        if t==b'DATA': extra=struct.unpack('>I',d[p+12:p+16])[0]
        yield t,bytearray(d[p:p+l]),bytearray(d[p+l:p+l+extra]); p+=l+extra
want={int(a.split(':')[0]):int(a.split(':')[1]) for a in args}
o=bytearray()
for t,rec,pl in records(open(sys.argv[1],'rb').read()):
    if t==b'IMGE':
        v=list(struct.unpack('>17I',rec[12:80]))
        if v[1]==0 and v[0] in want:
            v[6]=want[v[0]]; v[5]=v[6]//8
            rec[12:80]=struct.pack('>17I',*v); rec[8:12]=b'\0'*4; rec[8:12]=struct.pack('>I',zlib.crc32(bytes(rec))&0xFFFFFFFF)
    o+=rec+pl
open(sys.argv[2],'wb').write(o)
