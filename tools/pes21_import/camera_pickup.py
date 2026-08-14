"""PES 2021 CameraPickupInfo — which frames of an animation the replay camera frames.

Source data, both in PES21/download/dt13_all.cpk:

  common/anime/Mbinfo/bin/CameraPickupInfo.bin    980 bytes, md5 8a4f48d1..
  common/anime/Mbinfo/json/anim_infos.json        15 702 852 bytes

The .bin is the runtime form of the "camera_pickup_info" member of the animation
metadata table. Konami also ship the whole table as PLAIN JSON in the same cpk,
one object per animation keyed by animation id, so the JSON is the practical
decode path and this module reads it. What the table means:

  "camera_pickup_info": [ { "start_frame": 80, "end_frame": 120 } ]

= the frame window of that animation which is worth putting a camera on. 240 of
the 4907 animations carry exactly one such window (no animation has two).
234 of the 240 are ``dm_*`` demo/reaction animations (dm_oop_* out-of-play,
dm_miss_*, dm_tu_* time-up, dm_change_*), the rest are 4 ``run_*``, 1 ``idle_*``
and 1 ``kick_long_*``. Windows run 12..494 frames (0.4 s .. 16.5 s at 30 fps),
mean 65 frames, and 239 of 240 lie inside the animation's own start_frame..
end_frame span — i.e. this is a "the interesting part happens here" marker the
highlight/replay director uses to decide when to cut to a reaction shot and how
long to hold it.

--------------------------------------------------------------------------
On the .bin's byte layout — what is established, and what is not
--------------------------------------------------------------------------
Not cracked. It is NOT a plain array of fixed 32-bit records, which is worth
recording because that is the obvious first guess. Established facts:

  * 980 bytes = 245 u32 words for 240 entries, i.e. 32.67 bits per entry.
    Every sibling table in Mbinfo/bin is likewise a whole number of u32 words,
    so the stream is u32-aligned.
  * The family cannot be fixed-record: PersonalizedData.bin holds 97 entries in
    284 bytes = 71 words, fewer words than entries. So Mbinfo bins are
    bit-packed streams with variable-width (probably delta/varint) keys.
    Consistent with that, no sibling table's size divides evenly by its entry
    count: RotData.bin is 7.084 bytes/entry over 4907 entries, BallData.bin
    17.709, DemoConnectData.bin 4.004.
  * Ruled out by exhaustive search over every contiguous bitfield (offsets
    0..27, widths 4..19, both LE and BE word order):
      - no field reproduces the JSON animation ids (best set overlap 14 of 240)
      - no field is monotone across records, so the table is not a sorted
        id-keyed array
      - no field equals the entry's start_frame or end_frame positionally for
        any word alignment, so it is not a positional array either
      - no non-overlapping (start, end) field pair, with or without a /5 scale,
        reproduces even 180 of the 240 known (start_frame, end_frame) pairs
    Cracking it would need the bit-reader in PES2021.exe; the JSON makes that
    unnecessary for asset import.
"""

import json
import os
import struct
import sys

MBINFO_JSON = "common/anime/Mbinfo/json/anim_infos.json"
MBINFO_BIN = "common/anime/Mbinfo/bin/CameraPickupInfo.bin"


class PickupWindow:
    """One camera-pickup window: frames [start, end] of one animation."""

    def __init__(self, anim_id, name, start_frame, end_frame,
                 anim_start=None, anim_end=None):
        self.anim_id = anim_id
        self.name = name
        self.start_frame = start_frame
        self.end_frame = end_frame
        self.anim_start = anim_start
        self.anim_end = anim_end

    @property
    def length(self):
        return self.end_frame - self.start_frame

    def seconds(self, frame_rate=30.0):
        return (self.start_frame / frame_rate, self.end_frame / frame_rate)

    def __repr__(self):
        return "PickupWindow(%d, %r, %d..%d)" % (
            self.anim_id, self.name, self.start_frame, self.end_frame)


def load_pickups(anim_infos_path):
    """Read every camera_pickup_info window out of anim_infos.json."""
    with open(anim_infos_path, "r", encoding="utf-8", errors="replace") as fp:
        animations = json.load(fp)["animations"]
    out = []
    for key, info in animations.items():
        windows = info.get("camera_pickup_info")
        if not windows:
            continue
        for w in windows:
            out.append(PickupWindow(int(key), info.get("file_name", ""),
                                    w["start_frame"], w["end_frame"],
                                    info.get("start_frame"), info.get("end_frame")))
    out.sort(key=lambda p: p.anim_id)
    return out


def bin_report(path):
    """Report the framing facts for CameraPickupInfo.bin (see module docstring)."""
    with open(path, "rb") as fp:
        blob = fp.read()
    words = [struct.unpack_from("<I", blob, i)[0] for i in range(0, len(blob) - 3, 4)]
    return {
        "bytes": len(blob),
        "u32_words": len(words),
        "u32_aligned": len(blob) % 4 == 0,
        "distinct_low_bytes": len(set(w & 0xFF for w in words)),
        "first_words": words[:4],
    }


def main(argv):
    import argparse
    ap = argparse.ArgumentParser(
        description="Dump PES 2021 camera-pickup windows (Mbinfo camera_pickup_info).")
    ap.add_argument("path", help="anim_infos.json, CameraPickupInfo.bin, or the "
                                 "directory the cpk was extracted to")
    ap.add_argument("-n", "--limit", type=int, default=25,
                    help="rows to print (0 = all)")
    ap.add_argument("--fps", type=float, default=30.0)
    args = ap.parse_args(argv)

    target = args.path
    if os.path.isdir(target):
        cand = os.path.join(target, MBINFO_JSON)
        target = cand if os.path.exists(cand) else os.path.join(target, MBINFO_BIN)

    if target.endswith(".bin"):
        info = bin_report(target)
        print("== %s" % target)
        for k, v in info.items():
            print("   %-20s %s" % (k, v))
        print("\n   The byte layout is not decoded — see the module docstring for what")
        print("   is ruled out. Point this tool at anim_infos.json for the real data.")
        return 0

    pickups = load_pickups(target)
    print("== %s" % target)
    print("   %d camera-pickup windows" % len(pickups))
    lengths = [p.length for p in pickups]
    print("   window length: min %d, max %d, mean %.1f frames (%.2f..%.2f s at %g fps)"
          % (min(lengths), max(lengths), sum(lengths) / float(len(lengths)),
             min(lengths) / args.fps, max(lengths) / args.fps, args.fps))
    inside = sum(1 for p in pickups
                 if p.anim_start is not None and p.anim_end is not None
                 and p.anim_start <= p.start_frame and p.end_frame <= p.anim_end)
    print("   %d of %d lie inside the animation's own frame range" % (inside, len(pickups)))
    print()
    print("   %-8s %-50s %-14s %-14s %s"
          % ("anim id", "animation", "pickup frames", "seconds", "animation span"))
    rows = pickups if args.limit == 0 else pickups[:args.limit]
    for p in rows:
        a, b = p.seconds(args.fps)
        print("   %-8d %-50s %-14s %-14s %s"
              % (p.anim_id, p.name, "%d..%d" % (p.start_frame, p.end_frame),
                 "%.2f..%.2f" % (a, b),
                 "-" if p.anim_start is None else "%s..%s" % (p.anim_start, p.anim_end)))
    if args.limit and len(pickups) > args.limit:
        print("   ... %d more (pass -n 0)" % (len(pickups) - args.limit))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
