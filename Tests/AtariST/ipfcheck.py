#!/usr/bin/env python3
"""Check an IPF with the SPS capsimage library (independent of SugarConvDsk).

Usage: ipfcheck.py LIB.so FILE.ipf [REFERENCE.st --spt N --sides S]
Prints platform, geometry, per-track sector decode results, weak areas and density variation,
and compares sector contents with REFERENCE.st if given.
"""
import ctypes as C, sys, argparse

U = C.c_uint32
class DT(C.Structure):
    _pack_ = 1
    _fields_ = [(n, U) for n in ('year','month','day','hour','min','sec','tick')]
class ImageInfo(C.Structure):
    _pack_ = 1
    _fields_ = [('type',U),('release',U),('revision',U),('mincyl',U),('maxcyl',U),('minhead',U),('maxhead',U),
                ('crdt',DT),('platform',U*4)]
class TI2(C.Structure):
    _pack_ = 1  # capsimage headers use #pragma pack(1)
    _fields_ = [('type',U),('cylinder',U),('head',U),('sectorcnt',U),('sectorsize',U),
                ('trackbuf',C.POINTER(C.c_uint8)),('tracklen',U),('timelen',U),('timebuf',C.POINTER(U)),
                ('overlap',C.c_int32),('startbit',U),('wseed',U),('weakcnt',U)]

PLAT = {0:'none',1:'Amiga',2:'Atari ST',3:'PC',4:'Amstrad CPC',5:'Spectrum'}
F_INDEX, F_DENVAR, F_UPDATEFD, F_TYPE = 1<<0, 1<<2, 1<<8, 1<<9

def crc16(data, crc=0xFFFF):
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else crc << 1
            crc &= 0xFFFF
    return crc

def decode(bits, timebuf):
    """Find IBM MFM sectors in a cell bit list. Returns {R: (ok_id, ok_data, data, mean_density)}"""
    n = len(bits)
    s = ''.join('1' if b else '0' for b in bits)
    sync = '0100010010001001'
    found = {}
    pending_id = None
    pos = 0
    def rd(p, cnt):
        out = bytearray()
        for i in range(cnt):
            v = 0
            for j in range(8):
                q = p + i*16 + j*2 + 1
                v = (v << 1) | (bits[q % n])
            out.append(v)
        return bytes(out)
    while True:
        i = s.find(sync*3, pos)
        if i < 0: break
        p = i + 48
        mark = rd(p, 1)[0]
        if mark == 0xFE:
            chrn = rd(p+16, 6)
            ok = crc16(b'\xA1\xA1\xA1\xFE' + chrn[:4]) == (chrn[4] << 8 | chrn[5])
            pending_id = (chrn[2], chrn[3], ok)
        elif mark in (0xFB, 0xF8) and pending_id:
            r, nsz, okid = pending_id
            size = 128 << (nsz & 3)
            d = rd(p+16, size + 2)
            okd = crc16(bytes([0xA1,0xA1,0xA1,mark]) + d[:size]) == (d[size] << 8 | d[size+1])
            dens = None
            if timebuf:
                b0, b1 = (p+16)//8, (p+16+size*16)//8
                seg = timebuf[b0:b1]
                dens = sum(seg)/len(seg) if seg else None
            if r not in found or (okd and not found[r][1]):
                found[r] = (okid, okd, d[:size], dens)
            pending_id = None
        pos = i + 48
    return found

def load_all(lib, path):
    cid = lib.CAPSAddImage()
    if lib.CAPSLockImage(cid, path.encode()) != 0:
        raise SystemExit('CAPSLockImage FAILED on ' + path)
    fl = F_INDEX | F_DENVAR | F_UPDATEFD | F_TYPE
    lib.CAPSLoadImage(cid, fl)
    ii = ImageInfo(); lib.CAPSGetImageInfo(C.byref(ii), cid)
    out = {}
    for cyl in range(ii.mincyl, ii.maxcyl + 1):
        for head in range(ii.minhead, ii.maxhead + 1):
            ti = TI2(); ti.type = 2
            if lib.CAPSLockTrack(C.byref(ti), cid, cyl, head, fl) != 0:
                continue
            n = ti.tracklen
            buf = bytes(ti.trackbuf[:n]) if n else b''
            bits = [(buf[k >> 3] >> (7 - (k & 7))) & 1 for k in range(n * 8)]
            tb = [ti.timebuf[k] for k in range(ti.timelen)] if ti.timelen and ti.timebuf else []
            out[(cyl, head)] = (decode(bits, tb) if bits else {}, n, ti.weakcnt)
    plats = [PLAT.get(p, p) for p in ii.platform if p]
    return out, plats

def compare(lib, mine, ref):
    a, pa = load_all(lib, mine)
    b, pb = load_all(lib, ref)
    print(f'mine platform={pa} tracks={len(a)}   ref platform={pb} tracks={len(b)}')
    same = diff = 0
    for key in sorted(set(a) | set(b)):
        sa, na, wa = a.get(key, ({}, 0, 0)); sb, nb, wb = b.get(key, ({}, 0, 0))
        if not sa and not sb:
            continue
        probs = []
        for r in sorted(set(sa) | set(sb)):
            x, y = sa.get(r), sb.get(r)
            if x is None: probs.append(f'R{r} missing'); continue
            if y is None: probs.append(f'R{r} extra'); continue
            if x[2] != y[2]:
                probs.append(f'R{r} data{"" if x[1] else "(mine CRC bad)"}{"" if y[1] else "(ref CRC bad)"}')
            dx = round(x[3]) if x[3] else 1000; dy = round(y[3]) if y[3] else 1000
            if abs(dx - dy) > 20: probs.append(f'R{r} density mine={dx} ref={dy}')
        if wa or wb:
            if bool(wa) != bool(wb): probs.append(f'weak mine={wa} ref={wb}')
        if probs:
            diff += 1
            print(f'{key[0]:02}.{key[1]} bytes mine={na} ref={nb} : ' + ', '.join(probs))
        else:
            same += 1
    print(f'SUMMARY identical_tracks={same} differing_tracks={diff}')

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('lib'); ap.add_argument('ipf'); ap.add_argument('ref', nargs='?')
    ap.add_argument('--spt', type=int, default=9); ap.add_argument('--sides', type=int, default=2)
    ap.add_argument('--quiet', action='store_true')
    ap.add_argument('--cmp', help='compare sector contents/density with another IPF (e.g. an official SPS one)')
    a = ap.parse_args()
    lib = C.CDLL(a.lib)
    lib.CAPSInit()
    if a.cmp:
        compare(lib, a.ipf, a.cmp); return
    cid = lib.CAPSAddImage()
    if lib.CAPSLockImage(cid, a.ipf.encode()) != 0:
        print('CAPSLockImage FAILED'); sys.exit(2)
    lib.CAPSLoadImage(cid, F_INDEX | F_DENVAR | F_UPDATEFD | F_TYPE)  # timing is only generated if requested at load
    ii = ImageInfo(); lib.CAPSGetImageInfo(C.byref(ii), cid)
    plats = [PLAT.get(p, p) for p in ii.platform if p]
    print(f'platform={plats} cyl={ii.mincyl}-{ii.maxcyl} head={ii.minhead}-{ii.maxhead}')
    ref = open(a.ref, 'rb').read() if a.ref else None
    bad = 0; total = 0
    for cyl in range(ii.mincyl, ii.maxcyl + 1):
        for head in range(ii.minhead, ii.maxhead + 1):
            ti = TI2(); ti.type = 2
            rc = lib.CAPSLockTrack(C.byref(ti), cid, cyl, head, F_INDEX | F_DENVAR | F_UPDATEFD | F_TYPE)
            if rc != 0:
                print(f'{cyl:02}.{head} lock error {rc}'); bad += 1; continue
            nbytes = ti.tracklen
            buf = bytes(ti.trackbuf[:nbytes]) if nbytes else b''
            bits = [(buf[k >> 3] >> (7 - (k & 7))) & 1 for k in range(nbytes * 8)]
            tb = [ti.timebuf[k] for k in range(ti.timelen)] if ti.timelen and ti.timebuf else []
            secs = decode(bits, tb) if bits else {}
            notes = []
            if ti.weakcnt: notes.append(f'weak={ti.weakcnt}')
            vary = [f'R{r}:{v[3]:.0f}' for r, v in sorted(secs.items()) if v[3] and abs(v[3]-1000) > 20]
            if vary: notes.append('density ' + ' '.join(vary))
            if nbytes * 8 > 101000: notes.append(f'long track {nbytes} bytes')
            errs = []
            if ref is not None and head < a.sides:
                for r in range(1, a.spt + 1):
                    total += 1
                    off = ((cyl * a.sides + head) * a.spt + r - 1) * 512
                    exp = ref[off:off+512]
                    got = secs.get(r)
                    if not got: errs.append(f'R{r} missing'); continue
                    if not got[0]: errs.append(f'R{r} IDCRC')
                    if got[2] != exp: errs.append(f'R{r} data{"(CRC bad)" if not got[1] else ""}')
                bad += bool(errs)
            line = f'{cyl:02}.{head} bytes={nbytes} sectors={len(secs)} ' + ' '.join(notes) + (' ERR ' + ','.join(errs) if errs else '')
            if not a.quiet or errs or notes:
                print(line)
    if ref is not None:
        print(f'SUMMARY tracks_with_errors={bad} sectors_checked={total}')

if __name__ == '__main__':
    main()
