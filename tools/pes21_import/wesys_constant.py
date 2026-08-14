#!/usr/bin/env python3
"""Reader for PES2021 `constant_*.bin` gameplay/tactics constant archives.

These live in dt18_all.cpk under common/match/constant/ and are overridden by
community "gameplay" CPK mods (e.g. 4cc_06_gameplay.cpk).

Container layout
----------------
Outer wrapper (16 bytes) then zlib::

    +0x00  u8[3]  tag bytes ("\\xff\\x10Q" stock, "\\x00\\x10\\x01" repacked)
    +0x03  char[5] "WESYS"
    +0x08  u32    compressed size (zlib stream length)
    +0x0c  u32    uncompressed size
    +0x10  zlib deflate stream

Inflated payload is a flat archive of named ".o" parameter objects::

    +0x00  u32    entry count
    +0x04  u32    offset of the entry table (always 8)
    +0x08  count * { u32 data_offset; u32 data_size; u32 name_offset }
           ... NUL-terminated names ...
           ... object payloads (arrays of little-endian i32/f32/i16) ...
           trailer "v4.0"

Usage::

    wesys_constant.py list   <constant_x.bin>
    wesys_constant.py dump   <constant_x.bin> <outdir>       # one file per .o
    wesys_constant.py show   <constant_x.bin> <objname>      # decode as i32/f32
    wesys_constant.py diff   <stock.bin> <mod.bin>           # per-object, by name
"""
import os
import struct
import sys
import zlib

TRAILER = b"v4.0"


def unwrap(path):
    """Strips the WESYS wrapper (if any) and inflates."""
    blob = open(path, "rb").read()
    if blob[3:8] == b"WESYS":
        csize, usize = struct.unpack("<II", blob[8:16])
        raw = zlib.decompress(blob[16:16 + csize])
        if len(raw) != usize:
            raise ValueError("size mismatch: %d != %d" % (len(raw), usize))
        return raw
    if blob[:2] == b"\x78\xda":
        return zlib.decompress(blob)
    return blob


def parse(raw):
    """Returns [(name, data_bytes, data_offset)] in table order."""
    count, table = struct.unpack("<II", raw[:8])
    out = []
    for i in range(count):
        pos = table + i * 12
        doff, dsize, noff = struct.unpack("<III", raw[pos:pos + 12])
        end = raw.find(b"\0", noff)
        name = raw[noff:end].decode("ascii", "replace")
        out.append((name, raw[doff:doff + dsize], doff))
    return out


def load(path):
    return parse(unwrap(path))


def words(data):
    """Decodes a payload as parallel i32 / f32 views."""
    n = len(data) // 4
    ints = struct.unpack("<%di" % n, data[:n * 4])
    flts = struct.unpack("<%df" % n, data[:n * 4])
    return ints, flts


def plausible_float(f):
    return f == 0.0 or 1e-4 <= abs(f) <= 1e6


def best(i, f):
    """Most likely interpretation of one dword.

    Payloads mix f32 and i32 with no type table. A dword whose i32 view is
    huge but whose f32 view is a tame number is a float; everything else
    reads better as an int (small ints denormalise as ~1e-44 floats).
    """
    if f != 0.0 and 1e-3 <= abs(f) <= 1e7 and abs(i) > 0x100000:
        return "%g" % f
    return "%d" % i


def render(name, data, indices=None):
    ints, flts = words(data)
    print("--- %s  (%d bytes, %d dwords)" % (name, len(data), len(ints)))
    for i in range(len(ints)):
        if indices is not None and i not in indices:
            continue
        guess = best(ints[i], flts[i])
        print("  [%3d] +0x%04x  i32=%-12d f32=%-14.6g  %s"
              % (i, i * 4, ints[i], flts[i], guess))


def cmd_list(path):
    total = 0
    for name, data, off in load(path):
        print("%-34s %6d bytes  @0x%06x" % (name, len(data), off))
        total += len(data)
    print("# %d objects, %d payload bytes" % (len(load(path)), total))


def cmd_dump(path, outdir):
    os.makedirs(outdir, exist_ok=True)
    for name, data, _ in load(path):
        with open(os.path.join(outdir, name), "wb") as f:
            f.write(data)
    print("wrote %d objects to %s" % (len(load(path)), outdir))


def cmd_show(path, target):
    for name, data, _ in load(path):
        if name == target or name == target + ".o":
            render(name, data)
            return
    raise SystemExit("no such object: %s" % target)


def cmd_diff(a_path, b_path):
    a = {n: d for n, d, _ in load(a_path)}
    b = {n: d for n, d, _ in load(b_path)}
    only_a = sorted(set(a) - set(b))
    only_b = sorted(set(b) - set(a))
    if only_a:
        print("# only in %s: %s" % (os.path.basename(a_path), ", ".join(only_a)))
    if only_b:
        print("# only in %s: %s" % (os.path.basename(b_path), ", ".join(only_b)))
    for name in sorted(set(a) & set(b)):
        da, db = a[name], b[name]
        if da == db:
            continue
        print("=== %s   %d -> %d bytes" % (name, len(da), len(db)))
        ia, fa = words(da)
        ib, fb = words(db)
        for i in range(min(len(ia), len(ib))):
            if ia[i] == ib[i]:
                continue
            print("  [%3d] +0x%04x  %-14s -> %-14s   (i32 %d -> %d, f32 %g -> %g)"
                  % (i, i * 4, best(ia[i], fa[i]), best(ib[i], fb[i]),
                     ia[i], ib[i], fa[i], fb[i]))
        if len(ia) != len(ib):
            print("  (length differs: %d vs %d dwords)" % (len(ia), len(ib)))


def main(argv):
    if len(argv) < 3:
        raise SystemExit(__doc__)
    cmd = argv[1]
    if cmd == "list":
        cmd_list(argv[2])
    elif cmd == "dump":
        cmd_dump(argv[2], argv[3])
    elif cmd == "show":
        cmd_show(argv[2], argv[3])
    elif cmd == "diff":
        cmd_diff(argv[2], argv[3])
    elif cmd == "cat":
        sys.stdout.buffer.write(unwrap(argv[2]))
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main(sys.argv)
