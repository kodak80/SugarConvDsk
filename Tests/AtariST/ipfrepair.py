#!/usr/bin/env python3
"""Repair IPFs written by SugarConvDsk before commit 48daf8a, keeping every track as it is.

Usage: ipfrepair.py IN.ipf OUT.ipf

Two things made Aufit report "IPF File Error: inconsistent track content" (and made capsimg, used by
Hatari and others, read weak areas twice as long as they are):
- IMGE track bytes : must be the sum of each block (data + gap bits) rounded up to whole bytes.
- Weak (fuzzy) elements in bit-sized blocks : their size counts data bits (2 MFM cells each), but was
  written in cells. Such blocks are recognised (their elements only add up to the block size when weak
  sizes are taken as cells) and their weak sizes halved ; an odd weak run keeps its last cell as a
  plain 0 cell, so the block keeps its exact length.
Nothing else is changed : sector data, timing (density), gaps and other blocks stay byte for byte.
"""
import struct, sys, zlib


def records(d):
    p = 0
    while p < len(d):
        t = d[p:p + 4]
        l = struct.unpack('>I', d[p + 4:p + 8])[0]
        extra = struct.unpack('>I', d[p + 12:p + 16])[0] if t == b'DATA' else 0
        yield t, bytearray(d[p:p + l]), bytearray(d[p + l:p + l + extra])
        p += l + extra


def fix_crc(rec):
    rec[8:12] = b'\0' * 4
    rec[8:12] = struct.pack('>I', zlib.crc32(bytes(rec)) & 0xFFFFFFFF)


def parse_elements(buf, off, inbits=True):
    """Elements of a data stream (sizes in bits or in bytes) and the offset after its end mark"""
    els = []
    while True:
        h = buf[off]
        off += 1
        if h == 0:
            break
        w, ty = h >> 5, h & 31
        sz = int.from_bytes(buf[off:off + w], 'big')
        off += w
        payload = b''
        if ty != 5:
            n = (sz + 7) // 8 if inbits else sz
            payload = bytes(buf[off:off + n])
            off += n
        els.append([ty, sz, payload])
    return els, off


def element(ty, sz, payload=b''):
    w = 1
    while w < 4 and (sz >> (8 * w)) != 0:
        w += 1
    return bytes([(w << 5) | ty]) + sz.to_bytes(w, 'big') + payload


def cells(els, weak_unit):
    total = 0
    for ty, sz, _ in els:
        if ty == 5:
            total += sz * weak_unit
        elif ty == 1:
            total += sz
        else:
            total += 2 * sz
    return total


def repair(src, dst):
    recs = list(records(open(src, 'rb').read()))
    imge = {}
    for i, (t, rec, _) in enumerate(recs):
        if t == b'IMGE':
            imge[struct.unpack('>I', rec[12 + 13 * 4:12 + 14 * 4])[0]] = i
    n_blocks = n_tracks = 0
    for t, rec, pl in recs:
        if t != b'DATA' or not pl:
            continue
        key = struct.unpack('>I', rec[24:28])[0]
        irec = recs[imge[key]][1]
        v = list(struct.unpack('>17I', irec[12:80]))
        nb = v[10]
        descs = [list(struct.unpack('>8I', pl[i * 32:i * 32 + 32])) for i in range(nb)]
        # data streams are stored after the descriptors and the gap streams : rebuild them in order
        data_start = min(d[7] for d in descs)
        new_data = bytearray()
        changed = False
        for d in sorted(descs, key=lambda x: x[7]):
            inbits = (d[5] & 4) != 0
            els, end = parse_elements(pl, d[7], inbits)
            if not inbits or not any(e[0] == 5 for e in els) or cells(els, 2) == d[0] or cells(els, 1) != d[0]:
                stream = bytes(pl[d[7]:end])   # unchanged : copied as it is
                d[7] = data_start + len(new_data)
                new_data += stream
                continue
            # weak sizes written in cells : halve them. An odd run grows by one cell taken from the raw
            # cells next to it (so even a 1-cell weak spot stays weak) ; with no raw neighbour its
            # last cell becomes a plain 0 cell.
            fixed = [list(e) for e in els]
            i = 0
            while i < len(fixed):
                ty, sz, payload = fixed[i]
                if ty != 5:
                    i += 1
                    continue
                if sz & 1:
                    nxt = fixed[i + 1] if i + 1 < len(fixed) else None
                    prv = fixed[i - 1] if i > 0 else None
                    if nxt is not None and nxt[0] == 1 and nxt[1] >= 1:
                        bits = ''.join(f'{x:08b}' for x in nxt[2])[1:nxt[1]]
                        nxt[1] -= 1
                        nxt[2] = int(bits.ljust(((len(bits) + 7) // 8) * 8, '0') or '0', 2).to_bytes((len(bits) + 7) // 8, 'big') if bits else b''
                        fixed[i][1] = (sz + 1) // 2
                        if nxt[1] == 0:
                            del fixed[i + 1]
                    elif prv is not None and prv[0] == 1 and prv[1] >= 1:
                        bits = ''.join(f'{x:08b}' for x in prv[2])[:prv[1] - 1]
                        prv[1] -= 1
                        prv[2] = int(bits.ljust(((len(bits) + 7) // 8) * 8, '0') or '0', 2).to_bytes((len(bits) + 7) // 8, 'big') if bits else b''
                        fixed[i][1] = (sz + 1) // 2
                        if prv[1] == 0:
                            del fixed[i - 1]
                            i -= 1
                    else:
                        fixed[i][1] = sz // 2
                        fixed.insert(i + 1, [1, 1, b'\x00'])
                        if fixed[i][1] == 0:
                            del fixed[i]
                            continue
                else:
                    fixed[i][1] = sz // 2
                i += 1
            assert cells(fixed, 2) == d[0]
            els = fixed
            changed = True
            n_blocks += 1
            d[7] = data_start + len(new_data)
            for ty, sz, payload in els:
                new_data += element(ty, sz, payload)
            new_data += b'\0'
        if changed:
            for i, d in enumerate(descs):
                pl[i * 32:i * 32 + 32] = struct.pack('>8I', *d)
            del pl[data_start:]
            pl += new_data
            rec[12:16] = struct.pack('>I', len(pl))
            rec[16:20] = struct.pack('>I', len(pl) * 8)
        rec[20:24] = struct.pack('>I', zlib.crc32(bytes(pl)) & 0xFFFFFFFF)
        fix_crc(rec)
        track_bytes = sum((d[0] + d[1] + 7) // 8 for d in descs)
        if track_bytes != v[4] or changed:
            n_tracks += 1
        v[4] = track_bytes
        irec[12:80] = struct.pack('>17I', *v)
        fix_crc(irec)
    open(dst, 'wb').write(b''.join(bytes(r) + bytes(p) for _, r, p in recs))
    return n_tracks, n_blocks


if __name__ == '__main__':
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    tracks, blocks = repair(sys.argv[1], sys.argv[2])
    print(f'{sys.argv[2]}: {tracks} track(s) corrected, {blocks} block(s) with weak sizes fixed')
