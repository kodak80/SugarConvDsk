#!/usr/bin/env python3
"""Dump IPF record structure: IMGE fields, block descriptors and data/gap stream elements.
Usage: ipfdump.py FILE.ipf [--track C.H] [--sizes]"""
import struct, sys, argparse
from collections import Counter

def records(d):
    p = 0
    while p < len(d):
        t = d[p:p+4].decode('latin-1'); l = struct.unpack('>I', d[p+4:p+8])[0]
        if t == 'DATA':
            dl, bits, crc, key = struct.unpack('>4I', d[p+12:p+28])
            yield t, p, d[p:p+l], d[p+l:p+l+dl], key
            p += l + dl
        else:
            yield t, p, d[p:p+l], None, None
            p += l

def stream(buf, off, is_gap):
    els = []
    while off < len(buf):
        h = buf[off]; off += 1
        if h == 0: break
        szw = h >> 5; typ = h & 0x1f
        sz = int.from_bytes(buf[off:off+szw], 'big'); off += szw
        if is_gap:
            if typ == 2:   # sample
                n = (sz + 7) // 8
                els.append(('gap-sample', sz, buf[off:off+n].hex())); off += n
            else:
                els.append(('gap-len' if typ == 1 else f'gap{typ}', sz, ''))
        else:
            names = {1: 'sync', 2: 'data', 3: 'gap', 4: 'raw', 5: 'fuzzy'}
            if typ == 5:
                els.append(('fuzzy', sz, ''))
            else:
                els.append((names.get(typ, f't{typ}'), sz, buf[off:off+sz][:16].hex())); off += sz
    return els, off

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('ipf'); ap.add_argument('--track'); ap.add_argument('--sizes', action='store_true')
    a = ap.parse_args()
    d = open(a.ipf, 'rb').read()
    imges = {}
    tot = Counter()
    for t, p, rec, payload, key in records(d):
        tot[t] += len(rec) + (len(payload) if payload else 0)
        if t == 'INFO':
            v = struct.unpack('>21I', rec[12:96]); print('INFO encoder', v[1], 'rev', v[2], 'platform', v[12])
        if t == 'IMGE':
            v = struct.unpack('>17I', rec[12:80])
            imges[v[13]] = dict(cyl=v[0], head=v[1], density=v[2], signal=v[3], trackbytes=v[4], startbyte=v[5], startbit=v[6],
                                databits=v[7], gapbits=v[8], trackbits=v[9], blocks=v[10], enc=v[11], flags=v[12])
        if t == 'DATA':
            im = imges.get(key)
            tag = f"{im['cyl']}.{im['head']}" if im else '?'
            if a.track and tag != a.track:
                continue
            if a.track:
                print('IMGE', im); print('DATA payload bytes', len(payload))
                for b in range(im['blocks']):
                    bd = struct.unpack('>8I', payload[b*32:b*32+32])
                    print(f' block {b}: databits={bd[0]} gapbits={bd[1]} gapoff={bd[2]} cell={bd[3]} enc={bd[4]} flags={bd[5]} gapdef={bd[6]:#x} dataoff={bd[7]}')
                    ds, _ = stream(payload, bd[7], False)
                    for e in ds: print('    data', e)
                    if bd[5] & 3:
                        off = bd[2]
                        for direction, bit in (('fwd', 1), ('bwd', 2)):
                            if bd[5] & bit:
                                gs, off = stream(payload, off, True)
                                for e in gs: print('    gap', direction, e)
    if a.sizes or not a.track:
        print('bytes per record type', dict(tot), 'total', len(d))

main()
