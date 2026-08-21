#!/usr/bin/env python3
"""Bake the pre-match "TV-style" formation graphic theme (PRESENTATION_SPEC.md
section 1.1) into plain PNG assets.

Reuses the exact technique export_scoreboard_theme.py already established for
the PES21 licence-skin panels (regions.json rects are in 2x space; panels
encode shape in the R channel, 0 = outside, 224 = fill plateau; see that
script's docstring for the source layout). No new PES source assets were
usable for the jersey icon itself (the closest sprite sheets found under
common/menu/general/gamePlan were plain sort-direction arrows and unlabeled
noise-compressed pitch line art, not a jersey silhouette - see the session
notes), so the jersey icon is drawn procedurally here instead: still a
"simple editable format" PNG, just not sourced from PES.

Outputs (data/media/ui/pes/, committed to the repo):
  formation_panel.png   - tall navy-to-blue gradient portrait panel (body)
  formation_header.png  - short brighter-blue gradient strip (team tag header)
  jersey_icon.png       - light-blue jersey silhouette (starter icon)
  plan_pitch.png        - portrait pitch diagram for the game plan's formation map
  banner_panel.png      - dark rounded rect, the in-match lower-third banner
                          (PRESENTATION_SPEC.md section 4)
"""

import argparse
import os
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(__file__))
from export_scoreboard_theme import bake_panel, load_region, widen  # noqa: E402

DEFAULT_EXTRACTED = "/run/media/z/Dati III/PES21/extracted/ui"
DEFAULT_OUT = os.path.join(os.path.dirname(__file__), "..", "..",
                           "data", "media", "ui", "pes")

# top_rgb, bottom_rgb, alpha - darker/richer than the scoreboard's NAVY so the
# live 3D scene stays dimly visible through the panel at its edges, per spec.
PANEL_NAVY = ((22, 30, 64), (46, 78, 150), 202)
HEADER_BLUE = ((58, 108, 210), (30, 64, 150), 235)
BANNER_DARK = ((14, 16, 22), (6, 7, 11), 205)

JERSEY_FILL = (150, 205, 245, 255)
JERSEY_OUTLINE = (235, 245, 255, 255)

# The panel and its header are baked at exactly the proportions the widget
# lays out at, so Gui2Image's scale-to-fit never has to distort a rounded
# corner. Keep these in step with kPanelPixelAspect / kHeaderFraction in
# src/menu/ingame/formationgraphiclayout.hpp:
#   panel  = 1.16 (width / height)
#   header = panel width / (panel height * 0.085) = 13.65
PANEL_PX = (928, 800)
HEADER_PX = (1092, 80)


def heighten(img, target_h, cap_h):
    """Vertical analogue of widen(): stretch a rounded panel taller, keeping
    the top/bottom caps crisp."""
    if target_h <= img.size[1]:
        return img
    rotated = img.transpose(Image.ROTATE_90)
    widened = widen(rotated, target_h, cap_h)
    return widened.transpose(Image.ROTATE_270)


def draw_jersey_icon(size=64):
    """A small light-blue jersey/shirt silhouette: rounded body, a V-neck
    collar notch, and shoulder/sleeve steps - simple enough to read at icon
    scale, no PES source asset was usable for this (see module docstring)."""
    scale = 4  # supersample, then downscale for cheap antialiasing
    s = size * scale
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    body_top = int(s * 0.22)
    body = [
        (int(s * 0.12), body_top),
        (int(s * 0.28), int(s * 0.06)),   # left shoulder up to collar
        (int(s * 0.40), int(s * 0.06)),
        (int(s * 0.50), int(s * 0.20)),   # collar notch (V-neck)
        (int(s * 0.60), int(s * 0.06)),
        (int(s * 0.72), int(s * 0.06)),   # right shoulder
        (int(s * 0.88), body_top),
        (int(s * 0.80), int(s * 0.34)),   # right sleeve underarm
        (int(s * 0.80), int(s * 0.92)),
        (int(s * 0.20), int(s * 0.92)),
        (int(s * 0.20), int(s * 0.34)),   # left sleeve underarm
    ]
    draw.polygon(body, fill=JERSEY_FILL, outline=JERSEY_OUTLINE)
    img = img.resize((size, size), Image.LANCZOS)
    return img


# The game plan's pitch, portrait, as the broadcast draws it: mown bands running
# across, white markings, the goal at the bottom and the attack at the top. Drawn
# rather than sourced - PES's own gamePlan pitch art is noise-compressed line work
# that does not survive extraction (see the note above about the jersey icon).
PITCH_PX = (420, 580)
GRASS_DARK = (38, 92, 44, 236)
GRASS_LIGHT = (46, 108, 52, 236)
PITCH_LINE = (232, 240, 232, 190)


def draw_plan_pitch(size=PITCH_PX, bands=9):
    pitch = Image.new("RGBA", size, GRASS_DARK)
    draw = ImageDraw.Draw(pitch)
    w, h = size
    for i in range(bands):
        if i % 2:
            continue
        top = int(h * i / float(bands))
        bottom = int(h * (i + 1) / float(bands))
        draw.rectangle([0, top, w, bottom], fill=GRASS_LIGHT)

    line = max(1, w // 210)
    inset = int(w * 0.045)
    draw.rectangle([inset, inset, w - inset, h - inset], outline=PITCH_LINE, width=line)
    # halfway line and centre circle
    draw.line([inset, h // 2, w - inset, h // 2], fill=PITCH_LINE, width=line)
    circle = int(w * 0.155)
    draw.ellipse([w // 2 - circle, h // 2 - circle, w // 2 + circle, h // 2 + circle],
                 outline=PITCH_LINE, width=line)
    # penalty areas, top and bottom
    boxW, boxH = int(w * 0.60), int(h * 0.135)
    sixW, sixH = int(w * 0.30), int(h * 0.058)
    for near in (True, False):
        y0 = h - inset - boxH if near else inset
        draw.rectangle([(w - boxW) // 2, y0, (w + boxW) // 2, y0 + boxH], outline=PITCH_LINE,
                       width=line)
        y1 = h - inset - sixH if near else inset
        draw.rectangle([(w - sixW) // 2, y1, (w + sixW) // 2, y1 + sixH], outline=PITCH_LINE,
                       width=line)
    return pitch


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--extracted", default=DEFAULT_EXTRACTED,
                    help="extracted dt11 ui root")
    ap.add_argument("-o", "--out", default=DEFAULT_OUT)
    args = ap.parse_args()

    out_dir = os.path.abspath(args.out)
    os.makedirs(out_dir, exist_ok=True)
    skin = os.path.join(args.extracted, "common", "menu", "licence", "game2dPes")

    # region 14: ~755x185 rounded bar. Rotated 90 deg it becomes a portrait
    # pill (rounded top/bottom, straight sides); heighten() then stretches it
    # tall while keeping those caps crisp.
    panel = bake_panel(load_region(skin, 14), PANEL_NAVY)
    panel = panel.transpose(Image.ROTATE_90)
    panel = heighten(panel, PANEL_PX[1], 90)
    panel = panel.resize(PANEL_PX, Image.LANCZOS)
    panel.save(os.path.join(out_dir, "formation_panel.png"))
    print("wrote formation_panel.png %dx%d" % panel.size)

    # Same source region, unrotated and re-paletted brighter: the header
    # strip the team tag sits on. widen()'d rather than plain-resized so the
    # rounded ends keep their radius across a strip this wide.
    header = bake_panel(load_region(skin, 14), HEADER_BLUE)
    header = widen(header, HEADER_PX[0], 90)
    header = header.resize(HEADER_PX, Image.LANCZOS)
    header.save(os.path.join(out_dir, "formation_header.png"))
    print("wrote formation_header.png %dx%d" % header.size)

    pitch = draw_plan_pitch()
    pitch.save(os.path.join(out_dir, "plan_pitch.png"))
    print("wrote plan_pitch.png %dx%d" % pitch.size)

    jersey = draw_jersey_icon(64)
    jersey.save(os.path.join(out_dir, "jersey_icon.png"))
    print("wrote jersey_icon.png %dx%d" % jersey.size)

    # region 15: ~473x137 rounded rect - a lower-third-shaped panel as-is,
    # just downscaled to the final banner size.
    banner = bake_panel(load_region(skin, 15), BANNER_DARK)
    banner = banner.resize((620, 150), Image.LANCZOS)
    banner.save(os.path.join(out_dir, "banner_panel.png"))
    print("wrote banner_panel.png %dx%d" % banner.size)


if __name__ == "__main__":
    main()
