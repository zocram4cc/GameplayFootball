"""Turns the 4cc replay wipe into frames the engine can draw.

The mod ships its replay transition as a movie - 4cc_20_swipe.cpk, PES19's download
folder - and each wipe is a CRI USM holding *two* video streams of equal length:
stream 0 the colour, stream 1 the matte. That second stream is the point. The
picture is the /vg/ Football League crest on black, and used as an opaque frame the
black blacks the screen out; the matte is what makes it a wipe. It rises from
nothing over about ten frames, holds fully opaque while the cut happens underneath,
and falls away again.

ffmpeg reads the container without a key (the mod re-encoded it), so the conversion
is a merge of the two streams into ordinary RGBA PNGs, plus a text sidecar for the
timing - simple editable formats, and the engine has no video path anyway:

    import_wipe.py <4cc_20_swipe.cpk or an extraction of it> [--out data/media/textures/wipe]

writes, per wipe:

    <out>/<name>/f_001.png ...      RGBA, the tail of fully transparent frames dropped
    <out>/<name>/wipe.txt           fps, frames, fadestart, and where it came from

settings.json inside the cpk says which competition gets which wipe and when the
cut happens:

    { "id": -1, "file": "WEPESwipe_hd.usm", "fade": 0, "fadestart": 6 }
    { "id":  7, "file": "ACLwipe_hd.usm",   "fade": 0, "fadestart": 8 }
"""

import argparse
import glob
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

# A frame whose matte is below this everywhere has nothing to draw.
ALPHA_FLOOR = 0.004


def read_settings(text):
    """-> the wipes settings.json lists, in order, as dicts with numbers for numbers.

    PES closes the list with a bare {}; entries without a file are not wipes.
    """
    try:
        parsed = json.loads(text)
    except ValueError as exc:
        raise ValueError("settings.json is not readable JSON: %s" % exc)
    wipes = []
    for entry in parsed.get("wipes", []):
        if not isinstance(entry, dict) or not entry.get("file"):
            continue
        wipes.append({
            "id": int(entry.get("id", -1)),
            "file": str(entry["file"]),
            "fade": int(entry.get("fade", 0)),
            "fadestart": int(entry.get("fadestart", 0)),
        })
    return wipes


def wipe_for(wipes, competition_id):
    """-> the wipe a competition uses: its own, else the default, else the first."""
    if not wipes:
        return None
    for wipe in wipes:
        if wipe["id"] == competition_id:
            return wipe
    for wipe in wipes:
        if wipe["id"] == -1:
            return wipe
    return wipes[0]


def useful_frames(alpha_maxima):
    """-> how many frames are worth keeping, given each frame's greatest alpha.

    Everything past the last frame that carries any matte is 1280 x 720 of nothing:
    the ACL wipe runs 145 frames and goes fully transparent at 93.
    """
    last = 0
    for index, value in enumerate(alpha_maxima):
        if value > ALPHA_FLOOR:
            last = index + 1
    return last


# How opaque the least opaque pixel has to be before a cut can hide behind the frame.
# The real matte tops out at 0.996.
COVER_ALPHA = 0.99


def cover_frame(alpha_minima):
    """-> the first frame whose *least* opaque pixel is opaque, or None.

    PES's own "fadestart" is not that frame. It is 6 on the default wipe and 8 on the
    ACL one, and at frame 6 the matte still has pixels at zero: the crest has swung
    most of the way across but there are gaps, and a cut made there is a cut you can
    see through. Full cover arrives at frame 9 on both.
    """
    for index, value in enumerate(alpha_minima):
        if value >= COVER_ALPHA:
            return index
    return None


# How far into the cover the cut is held. The matte first covers everything at frame
# 9; cutting exactly there leaves nothing to spare, since the wipe is 60 fps and the
# game may not be. Frame 15 is a quarter of a second in, well inside the hold.
CUT_HOLD_FRAME = 15


def cut_frame(cover, frames):
    """-> the frame the engine cuts on: held past the cover, inside the wipe."""
    last = max(0, int(frames) - 1)
    held = CUT_HOLD_FRAME if cover is None else max(int(cover), CUT_HOLD_FRAME)
    return min(held, last)


def sidecar_text(fps, frames, fadestart, source, cover=None):
    """The timing, as the engine reads it (src/onthepitch/replaywipe.hpp)."""
    last = max(0, int(frames) - 1)
    cut = max(0, min(int(fadestart), last))
    covered = cut if cover is None else max(0, min(int(cover), last))
    return ("# The 4cc replay wipe, from %s by tools/pes21_import/import_wipe.py.\n"
            "# RGBA frames: the colour is PES's stream 0, the alpha its stream 1 -\n"
            "# without that matte the crest's black background blacks out the screen.\n"
            "fps %g\n"
            "frames %d\n"
            "# PES's own \"fadestart\", kept for reference.\n"
            "fadestart %d\n"
            "# The frame the matte first covers every pixel, measured rather than taken\n"
            "# from fadestart, which still has gaps in it.\n"
            "cover %d\n"
            "# The frame the engine cuts on: held past the cover, so a game running at\n"
            "# some other rate than the wipe's 60 fps cannot land the cut in the gaps.\n"
            "cut %d\n"
            % (source, fps, int(frames), cut, covered, cut_frame(cover, frames)))


def _probe_fps(path):
    out = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0",
                          "-show_entries", "stream=r_frame_rate", "-of",
                          "default=nw=1:nk=1", path],
                         check=True, capture_output=True, text=True).stdout.strip()
    match = re.match(r"(\d+)/(\d+)", out)
    if match and int(match.group(2)):
        return float(match.group(1)) / float(match.group(2))
    return 60.0


def _decode(path, out_dir, width=None):
    """Merges the colour and matte streams into RGBA PNGs. -> how many were written.

    The file is opened twice because a filtergraph takes its inputs by file index,
    not by stream: [0:v:0] is the colour of the first, [1:v:1] the matte of the
    second.

    width scales the result down. A second and a half of 1280 x 720 at 60 fps is 92
    frames and 320 MB of texture, which is not a sensible price for a transition; at
    640 wide it is a quarter of that, and the crest is large and moving while it
    plays. Pass 0 to keep PES's own size.
    """
    os.makedirs(out_dir, exist_ok=True)
    for stale in glob.glob(os.path.join(out_dir, "f_*.png")):
        os.remove(stale)
    graph = "[0:v:0][1:v:1]alphamerge"
    if width:
        graph += ",scale=%d:-2:flags=lanczos" % int(width)
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", path, "-i", path,
                    "-filter_complex", graph,
                    "-pix_fmt", "rgba",
                    os.path.join(out_dir, "f_%03d.png")], check=True)
    return len(glob.glob(os.path.join(out_dir, "f_*.png")))


def _trim(out_dir):
    """Drops the fully transparent tail. -> (frames kept, the frame that covers)."""
    from PIL import Image
    import numpy

    frames = sorted(glob.glob(os.path.join(out_dir, "f_*.png")))
    maxima = []
    minima = []
    for path in frames:
        with Image.open(path) as image:
            alpha = numpy.asarray(image.convert("RGBA"), dtype=numpy.float32)[..., 3] / 255.0
        maxima.append(float(alpha.max()))
        minima.append(float(alpha.min()))
    keep = useful_frames(maxima)
    for path in frames[keep:]:
        os.remove(path)
    return keep, cover_frame(minima[:keep])


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("source", help="4cc_20_swipe.cpk, or a directory holding movie/fade")
    parser.add_argument("--out", default="data/media/textures/wipe")
    parser.add_argument("--width", type=int, default=640,
                        help="scale the frames to this width (0 keeps PES's 1280); "
                             "92 frames of 1280x720 RGBA is 320 MB of texture for a "
                             "second and a half of transition")
    args = parser.parse_args()

    root = args.source
    if root.lower().endswith(".cpk"):
        import cpk
        extracted = os.path.join(os.path.dirname(os.path.abspath(args.out)), "pes_wipe_extract")
        print("extracting movie/fade from %s" % os.path.basename(root))
        cpk.extract(root, extracted, pattern="movie/fade")
        root = extracted

    settings_paths = glob.glob(os.path.join(root, "**", "settings.json"), recursive=True)
    if not settings_paths:
        raise SystemExit("no movie/fade/settings.json under %s" % root)
    wipes = read_settings(open(settings_paths[0]).read())
    print("settings: %d wipe(s)" % len(wipes))

    written = 0
    for wipe in wipes:
        matches = glob.glob(os.path.join(root, "**", wipe["file"]), recursive=True)
        if not matches:
            print("  %-22s missing" % wipe["file"])
            continue
        name = os.path.splitext(wipe["file"])[0].lower()
        for suffix in ("_hd", "wipe"):
            name = name.replace(suffix, "")
        name = name or "wipe"
        out_dir = os.path.join(args.out, name)
        fps = _probe_fps(matches[0])
        decoded = _decode(matches[0], out_dir, args.width)
        kept, covers = _trim(out_dir)
        open(os.path.join(out_dir, "wipe.txt"), "w").write(
            sidecar_text(fps, kept, wipe["fadestart"], wipe["file"], covers))
        print("  %-22s id %3d  %d frame(s) at %g fps, %d kept, fadestart %d, covers at %s -> %s"
              % (wipe["file"], wipe["id"], decoded, fps, kept, wipe["fadestart"],
                 covers, out_dir))
        written += 1
    print("wrote %d wipe(s) under %s" % (written, args.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
