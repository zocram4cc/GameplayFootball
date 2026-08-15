#!/usr/bin/env python3
"""PES 2021 WFNT bitmap fonts -> PNG atlas + glyphs.json.

The menu fonts live in dt11 under common/menu/font/*_fnt.bin.  Each bin is
a WESYS-wrapped named archive (see wesys_constant.py); every archived
payload (`<name>.o` / `<name>.obj`) is one WFNT face.  Most bins carry a
single face; edit_fnt.bin carries ten (e00..e09, the in-game edit-mode
letter styles).

WFNT layout (all little-endian)
-------------------------------
    +0x00  char[4] "WFNT"
    +0x04  u32     version (1)
    +0x08  u8      bpp           4 = paletted alpha, 32 = raw RGBA8888
    +0x09  u8      line_height   (== ascent + descent everywhere)
    +0x0a  u8      ascent        baseline distance from the top of the line
    +0x0b  u8      descent
    +0x0c  u32     glyph_count
    +0x10  u32     palette_offset (0x20; == table_offset when bpp == 32)
    +0x14  u32     table_offset
    +0x18  u32[2]  always 0 (unk_18 / unk_1c)

bpp == 4: 16 RGBA8 palette entries at palette_offset.  Every shipped font
uses white with a linear alpha ramp (FF FF FF 00/11/22/../FF), i.e. the
glyph bitmaps are 4-bit antialiased coverage.  bpp == 32 (ext only, the
U+E000.. private-use button/icon glyphs) has no palette and stores glyph
pixels as straight RGBA8888.

Glyph table: glyph_count * 16 bytes, sorted by charcode::

    +0x00  u32  charcode      UTF-8 bytes of the character packed into a
                              u32 (e.g. 0x20 ' ', 0xefbfa5 U+FFE5)
    +0x04  u8   cell_w        power-of-two cell (>= w, GPU cache cell;
    +0x05  u8   cell_h         exact rounding rule not pinned down)
    +0x06  u8   width         bitmap size in pixels
    +0x07  u8   height
    +0x08  u8   bearing_x     pen -> left edge of bitmap
    +0x09  u8   bearing_y     baseline -> top edge of bitmap (FreeType
                              horiBearingY; digits overshoot by 1)
    +0x0a  u8   advance
    +0x0b  u8   0 (unk_pad)
    +0x0c  u32  pixel data offset (from start of WFNT)

Pixel data: rows top-down.  bpp 4: two pixels per byte, high nibble
first, stride = ceil(width / 2); bpp 32: stride = width * 4.  Each
glyph's blob is padded to a 16-byte boundary; the blobs are consecutive
and the last one ends exactly at the payload end.

Usage::

    wfnt.py list    <font_fnt.bin> [...]
    wfnt.py extract <font_fnt.bin> [...] -o <outdir>

extract writes <outdir>/<font>/[<payload>/]atlas<N>.png + glyphs.json.
"""
import argparse
import json
import os
import struct
import sys

import wesys_constant

MAGIC = b"WFNT"
HEADER = struct.Struct("<4sI4B5I")
ENTRY = struct.Struct("<I8BI")
MAX_ATLAS = 4096
PAD = 1  # transparent pixels between packed glyphs


class Glyph(object):
    __slots__ = ("code", "char", "cell_w", "cell_h", "w", "h",
                 "bearing_x", "bearing_y", "advance", "offset",
                 "atlas", "x", "y")

    def __init__(self, code, cell_w, cell_h, w, h, bx, by, adv, offset):
        self.code = code
        self.char = decode_charcode(code)
        self.cell_w, self.cell_h = cell_w, cell_h
        self.w, self.h = w, h
        self.bearing_x, self.bearing_y = bx, by
        self.advance = adv
        self.offset = offset
        self.atlas = self.x = self.y = None


class Face(object):
    def __init__(self, payload_name, raw):
        magic, version, bpp, line_h, ascent, descent, count, pal_off, \
            tab_off, unk18, unk1c = HEADER.unpack_from(raw, 0)
        if magic != MAGIC:
            raise ValueError("not a WFNT payload: %r" % magic)
        if version != 1:
            raise ValueError("unhandled WFNT version %d" % version)
        if bpp not in (4, 32):
            raise ValueError("unhandled bpp %d" % bpp)
        if unk18 or unk1c:
            raise ValueError("unk_18/unk_1c not zero: %x %x" % (unk18, unk1c))
        self.name = payload_name
        self.raw = raw
        self.bpp = bpp
        self.line_height, self.ascent, self.descent = line_h, ascent, descent
        self.palette = None
        if bpp == 4:
            self.palette = [tuple(raw[pal_off + i * 4: pal_off + i * 4 + 4])
                            for i in range(16)]
        self.glyphs = []
        for i in range(count):
            f = ENTRY.unpack_from(raw, tab_off + i * 16)
            code, cw, ch, w, h, bx, by, adv, pad, doff = f
            if pad:
                raise ValueError("glyph %#x: pad byte %#x != 0" % (code, pad))
            self.glyphs.append(Glyph(code, cw, ch, w, h, bx, by, adv, doff))

    def glyph_rgba(self, g):
        """Glyph pixels as an RGBA byte string (w*h*4)."""
        if self.bpp == 32:
            stride = g.w * 4
            rows = [self.raw[g.offset + r * stride:
                             g.offset + (r + 1) * stride]
                    for r in range(g.h)]
            return b"".join(rows)
        lut = self._nibble_lut()
        stride = (g.w + 1) // 2
        out = []
        for r in range(g.h):
            row = self.raw[g.offset + r * stride: g.offset + (r + 1) * stride]
            out.append(b"".join(lut[b] for b in row)[:g.w * 4])
        return b"".join(out)

    def _nibble_lut(self):
        if not hasattr(self, "_lut"):
            pal = [bytes(c) for c in self.palette]
            self._lut = [pal[b >> 4] + pal[b & 0xF] for b in range(256)]
        return self._lut


def decode_charcode(code):
    """WFNT charcodes are the character's UTF-8 bytes packed into a u32."""
    n = max(1, (code.bit_length() + 7) // 8)
    return code.to_bytes(n, "big").decode("utf-8")


def load(path):
    """Returns [Face] for every WFNT payload in a *_fnt.bin."""
    faces = []
    for name, payload, _ in wesys_constant.parse(wesys_constant.unwrap(path)):
        faces.append(Face(name, payload))
    return faces


def pack(glyphs):
    """Shelf-packs glyphs; sets .atlas/.x/.y.  Returns [(w, h)] per page."""
    order = sorted(glyphs, key=lambda g: (-g.h, -g.w, g.code))
    area = sum((g.w + PAD) * (g.h + PAD) for g in order)
    max_w = max((g.w for g in order), default=1) + 2 * PAD
    width = 64
    while width < max_w or (width < MAX_ATLAS and width * width < area * 1.15):
        width *= 2
    pages, x, y, shelf = [], PAD, PAD, 0
    used_w = used_h = 0
    for g in order:
        if x + g.w + PAD > width:
            x, y = PAD, y + shelf + PAD
            shelf = 0
        if y + g.h + PAD > MAX_ATLAS:
            pages.append((used_w, used_h))
            x, y, shelf, used_w, used_h = PAD, PAD, 0, 0, 0
        g.atlas, g.x, g.y = len(pages), x, y
        shelf = max(shelf, g.h)
        used_w = max(used_w, x + g.w + PAD)
        used_h = max(used_h, y + g.h + PAD)
        x += g.w + PAD
    if used_w or used_h:
        pages.append((used_w, used_h))
    return pages


def extract(face, out_dir):
    from PIL import Image
    os.makedirs(out_dir, exist_ok=True)
    pages = pack(face.glyphs)
    images = [Image.new("RGBA", size, (0, 0, 0, 0)) for size in pages]
    for g in face.glyphs:
        if g.w and g.h:
            tile = Image.frombytes("RGBA", (g.w, g.h), face.glyph_rgba(g))
            images[g.atlas].paste(tile, (g.x, g.y))
    for i, img in enumerate(images):
        img.save(os.path.join(out_dir, "atlas%d.png" % i))
    meta = {
        "format": "WFNT v1",
        "payload": face.name,
        "bpp": face.bpp,
        "line_height": face.line_height,
        "ascent": face.ascent,
        "descent": face.descent,
        "palette": face.palette,
        "atlases": ["atlas%d.png" % i for i in range(len(images))],
        "glyphs": [{
            "char": g.char,
            "code": ord(g.char),
            "atlas": g.atlas, "x": g.x, "y": g.y, "w": g.w, "h": g.h,
            "bearing_x": g.bearing_x, "bearing_y": g.bearing_y,
            "advance": g.advance,
            "cell_w": g.cell_w, "cell_h": g.cell_h,
        } for g in face.glyphs],
    }
    with open(os.path.join(out_dir, "glyphs.json"), "w") as fh:
        json.dump(meta, fh, ensure_ascii=False, indent=1)
    return len(face.glyphs), len(images)


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("command", choices=("list", "extract"))
    ap.add_argument("bins", nargs="+", help="*_fnt.bin files")
    ap.add_argument("-o", "--out", default="wfnt_out",
                    help="output directory (extract)")
    args = ap.parse_args(argv)
    for path in args.bins:
        font = os.path.basename(path)
        for suffix in ("_fnt.bin", ".bin"):
            if font.endswith(suffix):
                font = font[:-len(suffix)]
                break
        faces = load(path)
        for face in faces:
            if args.command == "list":
                print("%-12s %-14s bpp=%-2d lh=%3d asc=%3d desc=%3d "
                      "glyphs=%5d range=U+%04X..U+%04X" % (
                          font, face.name, face.bpp, face.line_height,
                          face.ascent, face.descent, len(face.glyphs),
                          ord(face.glyphs[0].char),
                          ord(face.glyphs[-1].char)))
            else:
                sub = os.path.join(args.out, font)
                if len(faces) > 1:
                    sub = os.path.join(sub, os.path.splitext(face.name)[0])
                nglyphs, npages = extract(face, sub)
                print("%s: %d glyphs -> %d atlas page(s)" %
                      (sub, nglyphs, npages))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
