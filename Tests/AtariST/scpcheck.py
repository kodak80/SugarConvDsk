#!/usr/bin/env python3
"""Validate an .scp file against the SuperCard Pro image specification v2.5.

Usage: scpcheck.py FILE.scp [--quiet]
Checks the header (signature, disk type, revolutions, track range, flags, cell width, heads,
resolution, checksum), the track table, every Track Data Header (TRK signature, track number,
offsets relative to the TDH, flux data inside the file, lengths counting 0x0000 overflow words),
and reports index time against the flux sum of each revolution.
"""
import struct, sys

MANUFACTURERS = {0x0: 'Commodore', 0x1: 'Atari', 0x2: 'Apple', 0x3: 'PC', 0x4: 'Tandy', 0x5: 'TI',
                 0x6: 'Roland', 0x7: 'Amstrad', 0x8: 'Other', 0xE: 'Tape drive', 0xF: 'Hard drive'}
DISK_TYPES = {0x00: 'C64', 0x04: 'Amiga', 0x08: 'Amiga HD', 0x10: 'Atari FM SS', 0x11: 'Atari FM DS',
              0x12: 'Atari FM Ex', 0x14: 'Atari ST SS', 0x15: 'Atari ST DS', 0x16: 'Atari ST SS HD',
              0x17: 'Atari ST DS HD', 0x20: 'Apple II', 0x21: 'Apple II Pro', 0x24: 'Apple 400K',
              0x25: 'Apple 800K', 0x26: 'Apple 1.44', 0x30: 'PC 360K', 0x31: 'PC 720K', 0x32: 'PC 1.2M',
              0x33: 'PC 1.44M', 0x40: 'TRS80 SSSD', 0x41: 'TRS80 SSDD', 0x42: 'TRS80 DSSD',
              0x43: 'TRS80 DSDD', 0x50: 'TI 99/4A', 0x60: 'Roland D20', 0x70: 'Amstrad CPC',
              0x80: 'Other 360', 0x81: 'Other 1.2M', 0x84: 'Other 720', 0x85: 'Other 1.44M'}
FLAG_NAMES = ['INDEX', 'TPI96', 'RPM360', 'NORMALISED', 'READWRITE', 'FOOTER', 'EXTENDED', 'THIRDPARTY']


def check(path, quiet=False):
    d = open(path, 'rb').read()
    errors, notes = [], []
    if d[:3] != b'SCP':
        return ['not an SCP file'], []
    ver, dtype, revs, start, end, flags, width, heads, res = d[3], d[4], d[5], d[6], d[7], d[8], d[9], d[10], d[11]
    flag_txt = ','.join(n for b, n in enumerate(FLAG_NAMES) if flags >> b & 1) or 'none'
    notes.append(f'version {ver >> 4}.{ver & 15}, disk type {dtype:#04x} ({DISK_TYPES.get(dtype, "unknown")}, '
                 f'{MANUFACTURERS.get(dtype >> 4, "?")}), revolutions {revs}, tracks {start}-{end}, '
                 f'flags {flags:#04x} [{flag_txt}], width {width or 16}, heads {heads}, resolution {25 * (res + 1)} ns')
    if dtype not in DISK_TYPES:
        errors.append(f'unknown disk type {dtype:#04x}')
    if not 1 <= revs <= 5:
        errors.append(f'revolutions {revs} outside 1-5')
    if start > end or end > 167:
        errors.append(f'bad track range {start}-{end}')
    if heads not in (0, 1, 2):
        errors.append(f'heads byte {heads} not 0/1/2')
    if width not in (0, 8, 16):
        errors.append(f'cell width {width} unusual')
    if flags & 0x10 and revs != 1:
        errors.append('read/write image with more than one revolution')
    if not flags & 0x80:
        notes.append('FLUX CREATOR bit clear : claims SuperCard Pro hardware made it')
    cks = struct.unpack('<I', d[12:16])[0]
    calc = sum(d[0x10:]) & 0xFFFFFFFF
    if flags & 0x10:
        if cks != 0:
            errors.append('read/write image must store checksum 0')
    elif cks != calc:
        errors.append(f'checksum {cks:#010x} != computed {calc:#010x}')
    base = 0x80 if flags & 0x40 else 0x10
    nb_entries = 168
    bpe = 1 if width == 8 else 2
    used = []
    # Images from SCP software before spec v1.5 have a 166-entry table (first TDH at 0x2A8) : the table
    # ends where the first track data starts
    offs = [struct.unpack('<I', d[base + 4 * e: base + 4 * e + 4])[0] for e in range(nb_entries)]
    first = min([o for o in offs if 0 < o < len(d)] or [len(d)])
    if first < base + 4 * nb_entries:
        nb_entries = (first - base) // 4
        notes.append(f'short track table ({nb_entries} entries, pre-v1.5 layout)')
    for e in range(nb_entries):
        off = offs[e]
        if off == 0:
            continue
        used.append(e)
        if off + 4 + 12 * revs > len(d):
            errors.append(f'entry {e}: TDH outside the file'); continue
        if d[off:off + 3] != b'TRK':
            errors.append(f'entry {e}: no TRK signature'); continue
        if d[off + 3] != e:
            errors.append(f'entry {e}: TRK track number {d[off + 3]}')
        if e < start or e > end:
            errors.append(f'entry {e}: outside the header track range')
        if heads == 1 and e & 1:
            errors.append(f'entry {e}: odd entry in a side-0-only image')
        if heads == 2 and not e & 1:
            errors.append(f'entry {e}: even entry in a side-1-only image')
        prev_end = None
        for r in range(revs):
            dur, length, doff = struct.unpack('<III', d[off + 4 + 12 * r: off + 16 + 12 * r])
            if doff < 4 + 12 * revs:
                errors.append(f'entry {e} rev {r}: data offset {doff} inside the TDH')
            if off + doff + length * bpe > len(d):
                errors.append(f'entry {e} rev {r}: flux data outside the file'); continue
            if prev_end is not None and off + doff != prev_end:
                notes.append(f'entry {e} rev {r}: revolutions not stored sequentially')
            prev_end = off + doff + length * bpe
            raw = d[off + doff: off + doff + length * bpe]
            vals = struct.unpack('>%dH' % length, raw) if bpe == 2 else raw
            total, acc = 0, 0
            for v in vals:
                if v == 0:
                    acc += 0x10000 if bpe == 2 else 0x100
                else:
                    total += acc + v; acc = 0
            if acc:
                errors.append(f'entry {e} rev {r}: ends with an overflow word')
            ms = dur * 25 * (res + 1) / 1e6
            if not (150 <= ms <= 230):
                errors.append(f'entry {e} rev {r}: index time {ms:.2f} ms')
            if abs(total - dur) > 0.002 * dur:
                notes.append(f'entry {e} rev {r}: flux sum {total} vs index time {dur}')
    exp_heads = {0: None, 1: 0, 2: 1}[heads]
    if used and heads == 0 and not any(e & 1 for e in used):
        notes.append('heads byte says both sides but only even entries are present')
    notes.append(f'{len(used)} tracks present, first TDH at {min(struct.unpack("<I", d[base+4*e:base+4*e+4])[0] for e in used):#x}' if used else 'no tracks')
    if not quiet:
        for n in notes:
            print('  note :', n)
    return errors, notes


if __name__ == '__main__':
    quiet = '--quiet' in sys.argv
    bad = 0
    for p in [a for a in sys.argv[1:] if not a.startswith('--')]:
        errs, _ = check(p, quiet)
        print(f'{p}: {"OK" if not errs else "%d ERROR(S)" % len(errs)}')
        for e in errs[:20]:
            print('  ERROR:', e)
        bad += bool(errs)
    sys.exit(1 if bad else 0)
