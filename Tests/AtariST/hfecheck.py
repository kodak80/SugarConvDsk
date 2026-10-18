#!/usr/bin/env python3
"""Decode an HFE (v1 or v3) file the way FlashFloppy reads it and check its sectors.

Usage: hfecheck.py FILE.hfe [--reads N] [--cmp-ipf LIBCAPS.so OTHER.ipf]
For every track : sectors found, CRC state, sectors that change between reads (fuzzy),
and cell-rate changes (v3 OP_Bitrate, e.g. Copylock ST). With --cmp-ipf, sector contents
are compared with an IPF decoded by the SPS capsimage library (stable sectors only).
"""
import argparse, random, struct, sys, os

OP_NOP, OP_INDEX, OP_BITRATE, OP_SKIPBITS, OP_RAND = 0x0F, 0x8F, 0x4F, 0xCF, 0x2F

def rbit(x):
    return int('{:08b}'.format(x)[::-1], 2)

def crc16(data, crc=0xFFFF):
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else crc << 1
            crc &= 0xFFFF
    return crc

def load(path):
    d = open(path, 'rb').read()
    sig = d[:8]
    if sig not in (b'HXCPICFE', b'HXCHFEV3'):
        raise SystemExit('not an HFE file')
    hdr = dict(v3=(sig == b'HXCHFEV3'), rev=d[8], tracks=d[9], sides=d[10], enc=d[11],
               rate=struct.unpack('<H', d[12:14])[0], ifm=d[16], lut=struct.unpack('<H', d[18:20])[0])
    tracks = {}
    for t in range(hdr['tracks']):
        off, ln = struct.unpack('<HH', d[hdr['lut'] * 512 + t * 4: hdr['lut'] * 512 + t * 4 + 4])
        side = [bytearray(), bytearray()]
        for b in range(0, ln // 2, 256):
            blk = d[off * 512 + b * 2: off * 512 + b * 2 + 512]
            n = min(256, ln // 2 - b)
            side[0] += blk[:n]; side[1] += blk[256:256 + n]
        tracks[t] = side
    return hdr, tracks

def cells(raw, v3, rng):
    """Expand one side to (cell list, rate list) following FlashFloppy's v3 rules."""
    out, rates = [], []
    rate = 72
    i = 0
    while i < len(raw):
        x = raw[i]; i += 1
        skip = 0
        if v3 and (x & 0xF) == 0xF:
            if x == OP_BITRATE:
                rate = rbit(raw[i]); i += 1; continue
            if x == OP_SKIPBITS:
                skip = rbit(raw[i]) & 7; i += 1
                x = raw[i]; i += 1
                if x == OP_RAND:
                    x = rng.getrandbits(8)
            elif x == OP_RAND:
                x = rng.getrandbits(8)
            else:
                continue
        for b in range(skip, 8):
            out.append((x >> b) & 1); rates.append(rate)
    return out, rates

SYNC = '0100010010001001' * 3

def sectors(bits, rates):
    s = ''.join('1' if b else '0' for b in bits)
    def rd(p, c):
        return bytes(int(''.join(s[p + i * 16 + j * 2 + 1] for j in range(8)), 2) for i in range(c))
    out, pos, idr = {}, 0, None
    while True:
        i = s.find(SYNC, pos)
        if i < 0 or i + 48 + 32 > len(s):
            break
        p = i + 48; m = rd(p, 1)[0]
        if m == 0xFE:
            idr = rd(p + 16, 6)
            idok = crc16(b'\xA1\xA1\xA1\xFE' + idr[:4]) == (idr[4] << 8 | idr[5])
            idr = (idr, idok)
        elif m in (0xFB, 0xF8) and idr:
            sz = 128 << (idr[0][3] & 3)
            if p + 16 + (sz + 2) * 16 <= len(s):
                dd = rd(p + 16, sz + 2)
                ok = crc16(bytes([0xA1] * 3 + [m]) + dd[:sz]) == (dd[sz] << 8 | dd[sz + 1])
                rate = sum(rates[p:p + sz * 16]) / (sz * 16)
                r = idr[0][2]
                if r not in out:
                    out[r] = (idr[1], ok, dd[:sz], rate)
            idr = None
        pos = i + 48
    return out

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('hfe'); ap.add_argument('--reads', type=int, default=3)
    ap.add_argument('--cmp-ipf', nargs=2, metavar=('LIBCAPS', 'IPF'))
    ap.add_argument('--quiet', action='store_true')
    a = ap.parse_args()
    hdr, tracks = load(a.hfe)
    print('header', {k: v for k, v in hdr.items()})
    ref = None
    if a.cmp_ipf:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import ctypes as C
        import ipfcheck
        lib = C.CDLL(a.cmp_ipf[0]); lib.CAPSInit()
        ref, _ = ipfcheck.load_all(lib, a.cmp_ipf[1])
    rng = random.Random(1)
    nsec = nbad = nfuzzy = ndiff = 0
    for t, sides in tracks.items():
        for h in range(hdr['sides']):
            reads = []
            for _ in range(a.reads):
                c, r = cells(sides[h], hdr['v3'], rng)
                reads.append(sectors(c, r))
            first = reads[0]
            notes = []
            fuzzy = [r for r in first if any(rd.get(r, (0, 0, None))[2] != first[r][2] for rd in reads[1:])]
            bad = [r for r in first if not (first[r][0] and first[r][1]) and r not in fuzzy]
            dens = [f'R{r}:{v[3]:.0f}' for r, v in sorted(first.items()) if abs(v[3] - 72) > 0.5]
            nsec += len(first); nbad += len(bad); nfuzzy += len(fuzzy)
            if fuzzy: notes.append('fuzzy ' + ','.join(f'R{r}' for r in fuzzy))
            if bad: notes.append('CRC bad ' + ','.join(f'R{r}' for r in bad))
            if dens: notes.append('rate ' + ' '.join(dens))
            if ref is not None:
                rs = ref.get((t, h), ({}, 0, 0))[0]
                diffs = [r for r in set(first) | set(rs)
                         if r not in fuzzy and (r not in first or r not in rs or first[r][2] != rs[r][2])]
                if diffs:
                    ndiff += 1; notes.append('DIFF vs IPF ' + ','.join(f'R{r}' for r in sorted(diffs)))
            if notes or not a.quiet:
                print(f'{t:02}.{h} sectors={len(first)} ' + ' '.join(notes))
    print(f'SUMMARY sectors={nsec} crc_bad={nbad} fuzzy={nfuzzy}' + (f' tracks_differing_from_ipf={ndiff}' if ref is not None else ''))

main()
