"""Builds the scoreboard's bitmap font from an open typeface.

The PES-styled scoreboard draws its clock, score and shirt numbers from a bitmap
font: a PNG atlas plus a text metric file. The only one that existed was exported
from PES 2021's own UI, which is Konami's artwork and does not belong in this
repository - so this makes an equivalent from a font that does: Fira Sans
Condensed ExtraBold (SIL OFL, under data/media/fonts/firasanscondensed). Tall,
condensed and heavy, which is what a broadcast scoreboard numeral is.

    make_scoreboard_font.py <out dir> [--font <ttf>] [--size 51] [--name num_mid]

writes <out dir>/<name>.png and <name>.fnt in the format
src/utils/gui2/widgets/bitmaptext.cpp parses:

    atlas <png>
    line_height <px>
    ascent <px>
    glyph <codepoint> <x> <y> <w> <h> <bearingX> <bearingY> <advance>

Nothing here reads a PES file; the output is ours and is tracked.
"""

import argparse
import os
import sys

# What the scoreboard draws: the digits, the clock's colon, added time's plus, a
# minus for the goal difference, and a space. The same set PES's own export had.
GLYPHS = [32, 43, 45] + list(range(48, 59))

DEFAULT_FONT = "data/media/fonts/firasanscondensed/FiraSansCondensed-ExtraBold.ttf"


def pack(boxes, padding=1):
    """Lays glyph boxes out in one row per pass -> ({codepoint: (x, y)}, w, h).

    One row is enough for a dozen numerals and keeps the atlas trivially
    inspectable, which is the point of shipping it as a PNG.
    """
    placed = {}
    x = padding
    height = 0
    for codepoint in sorted(boxes):
        w, h = boxes[codepoint]
        placed[codepoint] = (x, padding)
        x += w + padding
        height = max(height, h)
    return placed, x, height + 2 * padding


def metrics_text(atlas_name, line_height, ascent, glyphs):
    """glyphs: {codepoint: (x, y, w, h, bearingX, bearingY, advance)}"""
    lines = [
        "# The scoreboard's bitmap font, built by tools/art/make_scoreboard_font.py",
        "# from Fira Sans Condensed ExtraBold (SIL OFL). Nothing here is Konami's.",
        "atlas %s" % atlas_name,
        "line_height %d" % line_height,
        "ascent %d" % ascent,
    ]
    for codepoint in sorted(glyphs):
        lines.append("glyph %d %d %d %d %d %d %d %d" % ((codepoint,) + tuple(glyphs[codepoint])))
    return "\n".join(lines) + "\n"


def build(out_dir, font_path, size, name):
    from PIL import Image, ImageDraw, ImageFont
    font = ImageFont.truetype(font_path, size)
    ascent, descent = font.getmetrics()

    rendered = {}
    boxes = {}
    for codepoint in GLYPHS:
        character = chr(codepoint)
        left, top, right, bottom = font.getbbox(character)
        width, height = max(0, right - left), max(0, bottom - top)
        advance = int(round(font.getlength(character)))
        if width == 0 or height == 0:
            boxes[codepoint] = (0, 0)
            rendered[codepoint] = (None, 0, 0, advance)
            continue
        glyph = Image.new("RGBA", (width, height), (0, 0, 0, 0))
        ImageDraw.Draw(glyph).text((-left, -top), character, font=font, fill=(255, 255, 255, 255))
        boxes[codepoint] = (width, height)
        # bearingY is measured from the baseline up, which is how the engine
        # places a glyph inside its line (see Gui2BitmapText::Redraw).
        rendered[codepoint] = (glyph, left, ascent - top, advance)

    placed, atlas_w, atlas_h = pack(boxes)
    atlas = Image.new("RGBA", (atlas_w, atlas_h), (0, 0, 0, 0))
    glyphs = {}
    for codepoint, (x, y) in placed.items():
        glyph, bearing_x, bearing_y, advance = rendered[codepoint]
        w, h = boxes[codepoint]
        if glyph is not None:
            atlas.paste(glyph, (x, y))
        glyphs[codepoint] = (x, y, w, h, bearing_x, bearing_y, advance)

    os.makedirs(out_dir, exist_ok=True)
    atlas_name = name + ".png"
    atlas.save(os.path.join(out_dir, atlas_name))
    open(os.path.join(out_dir, name + ".fnt"), "w").write(
        metrics_text(atlas_name, ascent + descent, ascent, glyphs))
    print("%s: %dx%d atlas, %d glyph(s), ascent %d" % (name, atlas_w, atlas_h, len(glyphs), ascent))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("out", help="where to write <name>.png and <name>.fnt")
    parser.add_argument("--font", default=DEFAULT_FONT)
    parser.add_argument("--size", type=int, default=51)
    parser.add_argument("--name", default="num_mid")
    args = parser.parse_args()
    if not os.path.isfile(args.font):
        print("no font at %s" % args.font)
        return 1
    build(args.out, args.font, args.size, args.name)
    return 0


if __name__ == "__main__":
    sys.exit(main())
