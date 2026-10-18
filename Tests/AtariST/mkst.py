#!/usr/bin/env python3
"""Synthesize Atari ST test disks: a .st sector image plus matching flux (.scp or KryoFlux raw).

Usage: mkst.py OUTBASE [--cyls 80] [--sides 2] [--spt 9] [--heads-byte 0|1] [--seed 1]
                       [--copylock CYL] [--long CYL] [--fuzzy CYL]
Writes OUTBASE.st and OUTBASE.scp (Greaseweazle-style interleaved track table).
Protection options (track on side 0 of the given cylinder):
  --copylock CYL : sector 6 written with 5% longer cells, sector 7 with 5% shorter (Copylock-ST style)
  --long CYL     : 6600-byte track (long track, Macrodos-like) with an extra 10th sector squeezed in
  --fuzzy CYL    : sector 3 data area has random flux per revolution (weak bits)
"""
import argparse, random, struct

CELL_NS = 2000          # 250 kbit/s MFM -> 2us cells
TICK_NS = 25            # SCP sample clock
TRACK_CELLS = 100000    # 200 ms at 300 rpm

def crc16(data, crc=0xFFFF):
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else crc << 1
            crc &= 0xFFFF
    return crc

class Track:
    """Builds a list of (cell_bit, cell_ns, fuzzy) in MFM."""
    def __init__(self):
        self.cells = []
        self.prev = 0
        self.cell_ns = CELL_NS
        self.fuzzy = False
    def byte(self, b):
        for i in range(7, -1, -1):
            d = (b >> i) & 1
            c = 1 if (d == 0 and self.prev == 0) else 0
            self.cells.append((c, self.cell_ns, self.fuzzy))
            self.cells.append((d, self.cell_ns, self.fuzzy))
            self.prev = d
    def sync_a1(self):
        # A1 with missing clock : 0x4489
        w = 0x4489
        for i in range(15, -1, -1):
            self.cells.append(((w >> i) & 1, self.cell_ns, False))
        self.prev = 1
    def fill(self, b, n):
        for _ in range(n):
            self.byte(b)

def build_track(cyl, head, spt, data_for, prot):
    t = Track()
    long_track = prot.get('long') == (cyl, head)
    if spt >= 11 or long_track:
        gap1, pre_id, gap2, gap3 = 10, 3, 22, 2
    else:
        gap1, pre_id, gap2, gap3 = 60, 12, 22, 40
    nsec = spt + (1 if long_track else 0)
    t.fill(0x4E, gap1)
    for r in range(1, nsec + 1):
        t.fill(0x00, pre_id)
        for _ in range(3): t.sync_a1()
        idam = bytes([0xFE, cyl, head, r, 2])
        t.byte(0xFE); [t.byte(x) for x in idam[1:]]
        c = crc16(b'\xA1\xA1\xA1' + idam); t.byte(c >> 8); t.byte(c & 0xFF)
        t.fill(0x4E, gap2); t.fill(0x00, 12)
        if prot.get('copylock') == (cyl, head) and r == 6:
            t.cell_ns = CELL_NS * 105 // 100
        for _ in range(3): t.sync_a1()
        data = data_for(cyl, head, r)
        t.byte(0xFB)
        t.fuzzy = prot.get('fuzzy') == (cyl, head) and r == 3
        for x in data: t.byte(x)
        t.fuzzy = False
        c = crc16(b'\xA1\xA1\xA1\xFB' + data); t.byte(c >> 8); t.byte(c & 0xFF)
        t.cell_ns = CELL_NS
        t.fill(0x4E, gap3)
    total = TRACK_CELLS * (1056 if long_track else 1000) // 1000
    while len(t.cells) < total:
        t.byte(0x4E)
    return t.cells[:max(total, len(t.cells))]

def flux_rev(cells, rng, jitter_ns=60):
    """Return flux intervals in SCP ticks for one revolution."""
    out, acc = [], 0.0
    for bit, ns, fuzzy in cells:
        acc += ns
        if bit:
            # fuzzy cells : transitions written between cell boundaries, so they read differently each rev
            j = rng.uniform(-0.5, 0.5) * ns if fuzzy else rng.uniform(-jitter_ns, jitter_ns)
            v = acc + j
            out.append(max(1, int(round(v / TICK_NS))))
            acc = 0.0
    if acc > 0 and out:
        out[-1] += int(acc / TICK_NS)
    return out

def write_scp(path, tracks, nrev, heads_byte, rng, interleaved=True):
    # tracks: dict {(cyl,head): cells}
    entries = {}
    for (cyl, head), cells in tracks.items():
        idx = cyl * 2 + head if interleaved else cyl
        entries[idx] = cells
    hdr = bytearray(0x10 + 168 * 4)
    body = bytearray()
    base = len(hdr)
    for idx in sorted(entries):
        revs = [flux_rev(entries[idx], rng) for _ in range(nrev)]
        th = bytearray(b'TRK' + bytes([idx]))
        fluxdata = bytearray()
        offs = 4 + 12 * nrev
        for rv in revs:
            dur = sum(rv)
            th += struct.pack('<III', dur, len(rv), offs + len(fluxdata))
            for v in rv:
                while v > 0xFFFF:
                    fluxdata += b'\x00\x00'; v -= 0x10000
                fluxdata += struct.pack('>H', v)
        struct.pack_into('<I', hdr, 0x10 + idx * 4, base + len(body))
        body += th + fluxdata
    maxidx = max(entries)
    hdr[0:3] = b'SCP'; hdr[3] = 0x24; hdr[4] = 0x15  # Atari ST DD
    hdr[5] = nrev; hdr[6] = min(entries); hdr[7] = maxidx
    hdr[8] = 0x01 | 0x02  # index, 96tpi
    hdr[9] = 0; hdr[0x0A] = heads_byte; hdr[0x0B] = 0
    full = hdr + body
    struct.pack_into('<I', full, 0x0C, sum(full[0x10:]) & 0xFFFFFFFF)
    open(path, 'wb').write(full)

SCK = 24027428.5714285
ICK = SCK / 8

def write_kf(outdir, tracks, nrev, rng):
    import os
    os.makedirs(outdir, exist_ok=True)
    for (cyl, head), cells in tracks.items():
        out = bytearray()
        pos = 0
        def oob(t, payload):
            out.extend(bytes([0x0D, t]) + struct.pack('<H', len(payload)) + payload)
        info = b'sck=%.7f, ick=%.7f\x00' % (SCK, ICK)
        oob(4, info)
        icount = 0
        for r in range(nrev + 1):
            oob(2, struct.pack('<III', pos, 0, int(icount)))
            if r == nrev:
                break
            for t in flux_rev(cells, rng):
                v = int(round(t * TICK_NS * SCK / 1e9))
                icount += 0
                while v >= 0x10000:
                    out.append(0x0B); pos += 1; v -= 0x10000
                if v >= 0x0E and v <= 0xFF:
                    out.append(v); pos += 1
                elif v < 0x800:
                    out.extend(bytes([v >> 8, v & 0xFF])); pos += 2
                else:
                    out.extend(bytes([0x0C, v >> 8, v & 0xFF])); pos += 3
            icount += 0.2 * ICK
        oob(3, struct.pack('<II', pos, 0))
        out.extend(b'\x0D\x0D\x0D\x0D')
        open(f'{outdir}/track{cyl:02}.{head}.raw', 'wb').write(out)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--cyls', type=int, default=80)
    ap.add_argument('--sides', type=int, default=2)
    ap.add_argument('--spt', type=int, default=9)
    ap.add_argument('--heads-byte', type=int, default=None)
    ap.add_argument('--sequential', action='store_true', help='single-sided sequential track table')
    ap.add_argument('--revs', type=int, default=3)
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--kf', action='store_true', help='also write KryoFlux stream files in OUT_kf/')
    ap.add_argument('--copylock', type=int); ap.add_argument('--long', type=int); ap.add_argument('--fuzzy', type=int)
    a = ap.parse_args()
    rng = random.Random(a.seed)
    prot = {k: (getattr(a, k), 0) for k in ('copylock', 'long', 'fuzzy') if getattr(a, k) is not None}
    sectors = {}
    def data_for(c, h, r):
        key = (c, h, r)
        if key not in sectors:
            sectors[key] = bytes(rng.getrandbits(8) for _ in range(512))
        return sectors[key]
    tracks = {}
    for c in range(a.cyls):
        for h in range(a.sides):
            tracks[(c, h)] = build_track(c, h, a.spt, data_for, prot)
    heads_byte = a.heads_byte if a.heads_byte is not None else (0 if a.sides == 2 else 1)
    write_scp(a.out + '.scp', tracks, a.revs, heads_byte, rng, interleaved=not a.sequential)
    if a.kf:
        write_kf(a.out + '_kf', tracks, a.revs, rng)
    with open(a.out + '.st', 'wb') as f:
        for c in range(a.cyls):
            for h in range(a.sides):
                for r in range(1, a.spt + 1):
                    f.write(data_for(c, h, r))
    print(f'{a.out}: {a.cyls} cyl x {a.sides} side x {a.spt} spt, heads byte {heads_byte}, prot {prot}')

if __name__ == '__main__':
    main()
