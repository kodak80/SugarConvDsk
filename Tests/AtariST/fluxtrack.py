#!/usr/bin/env python3
"""Decode one revolution of a KryoFlux stream file into MFM, list syncs/sectors and per-byte cell timing.
Usage: fluxtrack.py track.raw [--rev N] [--from BYTE --to BYTE]"""
import sys, argparse, bisect
sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from kfparse import parse, SCK

def crc16(data, crc=0xFFFF):
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else crc << 1
            crc &= 0xFFFF
    return crc

def decode(us):
    """Simple PLL: returns cells (list of 0/1) and the time (us) of each cell."""
    cells, ctime = [], []
    clock = 2.0
    for v in us:
        n = max(1, round(v / clock))
        n = min(n, 8)
        # PLL: nudge clock towards observed
        if 2 <= n <= 4:
            clock += (v / n - clock) * 0.05
            clock = min(max(clock, 1.8), 2.2)
        per = v / n
        for i in range(n - 1):
            cells.append(0); ctime.append(per)
        cells.append(1); ctime.append(per)
    return cells, ctime

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('raw'); ap.add_argument('--rev', type=int, default=0)
    ap.add_argument('--dump', nargs=2, type=int, metavar=('FROM', 'TO'))
    a = ap.parse_args()
    flux, idx, fpos = parse(a.raw)
    us = [f / SCK * 1e6 for f in flux]
    # flux index range of the revolution
    ks = [bisect.bisect_left(fpos, sp) for sp, sc in idx]
    k0, k1 = ks[a.rev], ks[a.rev + 1]
    seg = us[k0:k1]
    cells, ctime = decode(seg)
    print(f'revolution {a.rev}: {sum(seg)/1000:.3f} ms, {len(cells)} cells, mean cell {sum(seg)/len(cells)*1000:.1f} ns')
    s = ''.join(map(str, cells))
    SYNC = '0100010010001001'
    def rd(p, n):
        out = []
        for i in range(n):
            q = p + i * 16
            if q + 16 > len(s): break
            out.append(int(''.join(s[q + 2*j + 1] for j in range(8)), 2))
        return bytes(out)
    def tim(p, n):
        seg_t = ctime[p:p + n * 16]
        return sum(seg_t) / len(seg_t) * 1000 if seg_t else 0
    pos = 0; idr = None
    while True:
        i = s.find(SYNC * 3, pos)
        if i < 0: break
        p = i + 48; m = rd(p, 1)[0] if p + 16 <= len(s) else None
        if m == 0xFE:
            idr = rd(p + 16, 6)
            ok = crc16(b'\xA1\xA1\xA1\xFE' + idr[:4]) == (idr[4] << 8 | idr[5]) if len(idr) == 6 else False
            print(f'  cell {i:6d} (byte {i//16:5d}) ID  C{idr[0]} H{idr[1]} R{idr[2]} N{idr[3]} crc {"ok" if ok else "BAD"}  {tim(p,7):.0f} ns/cell')
        elif m is not None and 0xF8 <= m <= 0xFB and idr and len(idr) == 6:
            sz = 128 << (idr[3] & 3); d = rd(p + 16, sz + 2)
            ok = len(d) == sz + 2 and crc16(bytes([0xA1]*3 + [m]) + d[:sz]) == (d[sz] << 8 | d[sz+1])
            # timing per 64-byte chunk
            chunks = [tim(p + 16 + c*16, 64) for c in range(0, sz, 64)]
            print(f'  cell {i:6d} (byte {i//16:5d}) DATA R{idr[2]} mark {m:02X} size {sz} crc {"ok" if ok else "BAD"}  timing/64B: ' + ' '.join(f'{x:.0f}' for x in chunks))
            idr = None
        else:
            print(f'  cell {i:6d} (byte {i//16:5d}) sync then {m if m is None else hex(m)}')
        pos = i + 48
    if a.dump:
        f, t = a.dump
        for b in range(f, t, 16):
            row = rd(b * 16, 16)
            print(f'  byte {b:5d}: ' + ' '.join(f'{x:02X}' for x in row) + '   ' + ' '.join(f'{tim(b*16 + k*16, 1):.0f}' for k in range(0, 16, 4)))

main()
