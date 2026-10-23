#!/usr/bin/env python3
"""Build an IPF from BASE with some tracks taken from DONOR (record CRCs recomputed).
Usage: ipfsplice.py BASE DONOR OUT C.H [C.H ...]
Track records are IMGE (track description) + DATA (blocks); the donor's data keys are renumbered
to the base's so the keys stay unique."""
import struct, sys, zlib

def records(d):
    p = 0
    while p < len(d):
        t = d[p:p + 4]
        l = struct.unpack('>I', d[p + 4:p + 8])[0]
        extra = 0
        if t == b'DATA':
            extra = struct.unpack('>I', d[p + 12:p + 16])[0]
        yield t, bytearray(d[p:p + l]), bytearray(d[p + l:p + l + extra])
        p += l + extra

def fix_crc(rec):
    rec[8:12] = b'\0\0\0\0'
    rec[8:12] = struct.pack('>I', zlib.crc32(bytes(rec)) & 0xFFFFFFFF)

def tracks(path):
    head, imge, data = [], {}, {}
    for t, rec, payload in records(open(path, 'rb').read()):
        if t == b'IMGE':
            v = struct.unpack('>17I', rec[12:80])
            imge[(v[0], v[1])] = rec
        elif t == b'DATA':
            key = struct.unpack('>I', rec[24:28])[0]
            data[key] = (rec, payload)
        else:
            head.append(rec)
    return head, imge, data

base, donor, out = sys.argv[1], sys.argv[2], sys.argv[3]
take = {tuple(int(x) for x in a.split('.')) for a in sys.argv[4:]}
bh, bi, bd = tracks(base)
dh, di, dd = tracks(donor)
outb = bytearray()
for rec in bh:
    if bytes(rec[:4]) in (b'CAPS', b'INFO'):
        outb += rec
order = sorted(bi)
for key in order:
    if key in take and key in di:
        rec = bytearray(di[key])
        dkey = struct.unpack('>I', rec[12 + 13 * 4:12 + 14 * 4])[0]
        bkey = struct.unpack('>I', bi[key][12 + 13 * 4:12 + 14 * 4])[0]
        rec[12 + 13 * 4:12 + 14 * 4] = struct.pack('>I', bkey)
        fix_crc(rec)
        outb += rec
    else:
        outb += bi[key]
for key in order:
    bkey = struct.unpack('>I', bi[key][12 + 13 * 4:12 + 14 * 4])[0]
    if key in take and key in di:
        dkey = struct.unpack('>I', di[key][12 + 13 * 4:12 + 14 * 4])[0]
        rec, payload = dd[dkey]
        rec = bytearray(rec)
        rec[24:28] = struct.pack('>I', bkey)
        fix_crc(rec)
        outb += rec + payload
    else:
        rec, payload = bd[bkey]
        outb += rec + payload
open(out, 'wb').write(outb)
print(f'{out}: {len(take)} track(s) from {donor}')
