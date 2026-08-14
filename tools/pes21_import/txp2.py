"""PES 2021 menu texture packages -> PNG + regions.json.

The Flash-derived UI (dt11 common/menu) stores its art in *_tex.bin files:
a WESYS wrapper (see wesys_constant.py) around a one-entry archive whose
payload is a Konami TXP2 texture package, all big-endian:

  0x00  4s  "TXP2"
  0x0c  u32 total size
  0x18  u32 texture count      0x1c u32 texture table offset (0x6c)
  0x24  u32 region count       0x28 u32 region table offset

  texture entry (12 bytes): u32 name_offset, u32 blob_size, u32 blob_offset
  region entry  (10 bytes): u16 texture_no, u16 left, u16 top,
                            u16 right, u16 bottom   (pixel rect)

Texture blob: u32 uncompressed, u32 compressed, then Okumura LZSS
(0x1000 zero window, write pos 0xFEE, LSB-first flags, set bit = literal).
Decompressed: "TXDT" header, u16 width/height at 0x10, raw 32-bit RGBA
pixels from 0x40.

  python3 txp2.py <file_tex.bin> <out_dir>
  python3 txp2.py --batch <dir> <out_dir>     # mirrors the tree
"""

import json
import os
import struct
import sys

import wesys_constant


def lzss(data, out_size):
    win = bytearray(0x1000)
    wpos = 0xFEE
    out = bytearray()
    i = 0
    flags = 0
    bits = 0
    while len(out) < out_size and i < len(data):
        if bits == 0:
            flags = data[i]
            i += 1
            bits = 8
        if flags & 1:
            b = data[i]
            i += 1
            out.append(b)
            win[wpos] = b
            wpos = (wpos + 1) & 0xFFF
        else:
            if i + 1 >= len(data):
                break
            b1, b2 = data[i], data[i + 1]
            i += 2
            off = ((b2 & 0xF0) << 4) | b1
            length = (b2 & 0x0F) + 3
            for _ in range(length):
                b = win[off]
                off = (off + 1) & 0xFFF
                out.append(b)
                win[wpos] = b
                wpos = (wpos + 1) & 0xFFF
        flags >>= 1
        bits -= 1
    return bytes(out)


def _name(raw, offset):
    return raw[offset:raw.index(b"\0", offset)].decode("ascii", "replace")


def _dxt_image(fourcc, width, height, pixels):
    # wrap in a minimal DDS so Pillow's DDS plugin does the block decode
    import io
    from PIL import Image
    header = struct.pack("<4sIIIIIII44x", b"DDS ", 124, 0x1 | 0x2 | 0x4 | 0x1000 | 0x80000,
                         height, width, len(pixels), 0, 0)
    header += struct.pack("<II4sIIIII", 32, 0x4, fourcc, 0, 0, 0, 0, 0)
    header += struct.pack("<IIIII", 0x1000, 0, 0, 0, 0)
    return Image.open(io.BytesIO(header + pixels)).convert("RGBA")


def parse(raw):
    """Returns ([(name, width, height, rgba_bytes)], [region dicts])."""
    if raw[:4] != b"TXP2":
        raise ValueError("not a TXP2 package")
    tex_count, tex_table = struct.unpack_from(">II", raw, 0x18)
    reg_count, reg_table = struct.unpack_from(">II", raw, 0x24)

    textures = []
    for i in range(tex_count):
        name_off, size, off = struct.unpack_from(">3I", raw, tex_table + i * 12)
        usize, csize = struct.unpack_from(">II", raw, off)
        blob = raw[off + 8:off + 8 + csize]
        dec = blob if csize == usize else lzss(blob, usize)
        if dec[:4] != b"TXDT":
            raise ValueError("texture %d: no TXDT header" % i)
        width, height = struct.unpack_from(">HH", dec, 0x10)
        payload = len(dec) - 0x40
        fmt = dec[0x17]
        # pixel layout by TXDT format byte
        if fmt == 0x15:    # raw RGBA8888 (4 bytes/px)
            mode = "RGBA"
            pixels = dec[0x40:0x40 + width * height * 4]
        elif fmt == 0x16:  # DXT1 (0.5 bytes/px)
            mode = "DXT1"
            pixels = dec[0x40:0x40 + width * height // 2]
        elif fmt == 0x1a:  # DXT5 (1 byte/px)
            mode = "DXT5"
            pixels = dec[0x40:0x40 + width * height]
        else:
            raise ValueError("texture %d: unknown format %02x (%dx%d, %d bytes)"
                             % (i, fmt, width, height, payload))
        textures.append((_name(raw, name_off), mode, width, height, pixels))

    regions = []
    for i in range(reg_count):
        texno, left, top, right, bottom = \
            struct.unpack_from(">5H", raw, reg_table + i * 10)
        regions.append({"texture": texno, "rect": [left, top, right, bottom]})
    return textures, regions


def extract(bin_path, out_dir):
    entries = wesys_constant.parse(wesys_constant.unwrap(bin_path))
    from PIL import Image
    total = 0
    for entry_name, payload, _off in entries:
        if payload[:4] != b"TXP2":
            continue
        os.makedirs(out_dir, exist_ok=True)
        textures, regions = parse(payload)
        for name, mode, width, height, pixels in textures:
            if mode in ("DXT1", "DXT5"):
                img = _dxt_image(mode.encode(), width, height, pixels)
            else:
                img = Image.frombytes(mode, (width, height), pixels)
            img.save(os.path.join(out_dir, name + ".png"))
            total += 1
        if regions:
            json.dump(regions, open(os.path.join(out_dir, "regions.json"), "w"),
                      indent=1)
    return total


def main():
    argv = [a for a in sys.argv[1:] if a != "--batch"]
    batch = "--batch" in sys.argv
    src, out = argv
    if not batch:
        print("extracted %d textures" % extract(src, out))
        return
    files = failed = 0
    for root, _dirs, names in os.walk(src):
        for name in sorted(names):
            if not name.endswith(".bin"):
                continue
            rel = os.path.relpath(root, src)
            stem = name[:-len("_tex.bin")] if name.endswith("_tex.bin") \
                else name[:-len(".bin")]
            dest = os.path.join(out, rel, stem)
            try:
                if extract(os.path.join(root, name), dest) > 0:
                    files += 1
            except Exception as exc:
                failed += 1
                print("FAIL %s: %s" % (os.path.join(rel, name), exc))
    print("extracted %d packages, %d failed" % (files, failed))


if __name__ == "__main__":
    main()
