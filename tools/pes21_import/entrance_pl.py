"""Exports PES match-entrance player choreography (_pl packs) for the engine.

The ent_* fixdemo families stage their players with tag-0x04 actor records
(see camera_cut.ActorCut): one record per actor slot (0-10 home XI, 11-21
away XI, 22-24 officials) holding a spawn transform, a clip reference into
``common/demo/anime/FoxAnim/FixDemo/Animations/*.gani`` and a phase offset.
The clips are near-in-place (their RIG_ROOT motion spans at most ~3 m); the
walk-out reads as authored because of where actors are placed and how the
camera cuts between the packs.

This tool converts one family's _pl packs into two open text artefacts the
engine plays back directly:

  <out>/<id>/<pack>.chor            per-slot placement + world root track
  <out>/<id>/anims/<clip>.anim      the clip, root-stripped (in place), in
                                    the engine's own .anim format

.chor format (text, one file per _pl fdc):

  chor 1
  source <fdc basename>
  # one block per actor:
  slot <n> anims/<clip>.anim phase <gf-frames> loop 1
  k <gf-frame> <x> <y> <yaw-rad>     # world root track, GF coords (Z up,
  ...                                # metres, pitch centre origin), covering
                                     # one clip cycle; the engine loops it

The root track is the composition of the record's spawn transform with the
clip's own RIG_ROOT motion, sampled on the engine's 10 ms frame grid with the
phase offset already applied. Yaw values are unwrapped (continuous), GF
convention: a player at yaw a faces (sin a, -cos a).

  python3 entrance_pl.py <cut_data-dir> <animations-dir> <out-dir>
                         [--ids 020,009] [--stadiums st000]
"""

import argparse
import math
import os
import re

import camera_cut
import gani
import gani_to_anim

FAMILY_RE = re.compile(r"^(ent_\d{3})_")

# GF pitch half sizes (src/gametypes.hpp) over the Fox 105 x 68 m pitch
SCALE_X = 55.0 / camera_cut.FOX_PITCH_HALF_LENGTH
SCALE_Y = 36.0 / camera_cut.FOX_PITCH_HALF_WIDTH

GF_FRAME_MS = gani_to_anim.GF_FRAME_MS
PES_FRAME_MS = gani_to_anim.PES_FRAME_MS


def is_player_pack(name):
    stem = os.path.splitext(name)[0]
    return any(part == "pl" or part.startswith("pl") and part[2:3] in ("", "_")
               for part in stem.split("_")) and "_pl" in stem


def collect(directory, ids=None):
    """{family: [fdc path]} of the _pl packs."""
    found = {}
    for name in sorted(os.listdir(directory)):
        if not name.endswith(".fdc") or not is_player_pack(name):
            continue
        match = FAMILY_RE.match(name)
        if not match:
            continue
        family = match.group(1)
        if ids and family[-3:] not in ids:
            continue
        found.setdefault(family, []).append(os.path.join(directory, name))
    return found


def actor_role(slot, stem):
    """Who this staged actor is meant to be.

    PES puts the parts in the clip name and the team in the slot number: a
    "_judge" clip is the official, slots 22+ are the match officials, slots
    0-10 the home XI and 11-21 the away XI. In an incident pack the base clip
    (no _sub suffix) is the player the moment is about - the booked man, the
    scorer - and an actor from the other side is his counterpart, the one he
    fouled. Anyone else is an extra.
    """
    lowered = stem.lower()
    if slot >= 22 or "judge" in lowered or "referee" in lowered:
        return "official"
    if "_sub" in lowered:
        # a supporting actor: on the opposite side he is the other party
        return "opponent" if slot >= 11 else "extra"
    return "primary" if slot <= 10 else "opponent"


def convert_clip(gani_path, dest):
    """Root-stripped clip conversion; returns the clip's GF frame count."""
    blob = open(gani_path, "rb").read()
    text, g = gani_to_anim.convert(blob, anim_type="cutscene", strip_root=True)
    with open(dest, "w") as out:
        out.write(text)
    duration_ms = g.frame_count * PES_FRAME_MS
    return max(2, int(round(duration_ms / GF_FRAME_MS))), g


def bake_track(actor, g, key_step=2):
    """[(gf_frame, x, y, yaw)] world root track over one clip cycle, GF space."""
    sample = gani_to_anim.root_sampler(g)
    theta = math.radians(actor.yaw_deg)
    cos_t, sin_t = math.cos(theta), math.sin(theta)
    spawn_x, _, spawn_z = actor.position

    cycle = max(2, int(round(g.frame_count * PES_FRAME_MS / GF_FRAME_MS)))
    keys = []
    prev_yaw = None
    for f in range(0, cycle + 1, key_step):
        t = (actor.phase_ticks + f * GF_FRAME_MS / PES_FRAME_MS) % g.frame_count
        rx, rz, ryaw = sample(t)
        # spawn transform: rotate the clip's root by the spawn yaw, then offset
        wx = spawn_x + cos_t * rx + sin_t * rz
        wz = spawn_z - sin_t * rx + cos_t * rz
        yaw = theta + ryaw
        if prev_yaw is not None:  # unwrap for safe lerping
            while yaw - prev_yaw > math.pi:
                yaw -= 2.0 * math.pi
            while yaw - prev_yaw < -math.pi:
                yaw += 2.0 * math.pi
        prev_yaw = yaw
        keys.append((f, wx * SCALE_X, -wz * SCALE_Y, yaw))
    return keys, cycle


def export_pack(fdc_path, anims_dir, out_dir, clip_cache):
    fdc = camera_cut.load(fdc_path)
    if not fdc.actors:
        raise ValueError("no actor records")

    lines = ["chor 1", "source %s" % os.path.basename(fdc_path)]
    exported = 0
    for actor in sorted(fdc.actors, key=lambda a: a.slot):
        stem = os.path.splitext(actor.gani_name)[0]
        gani_path = os.path.join(anims_dir, actor.gani_name)
        if not os.path.isfile(gani_path):
            print("  MISSING %s (slot %d)" % (actor.gani_name, actor.slot))
            continue
        if stem not in clip_cache:
            dest = os.path.join(out_dir, "anims", stem + ".anim")
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            cycle, g = convert_clip(gani_path, dest)
            clip_cache[stem] = (cycle, g)
        cycle, g = clip_cache[stem]

        phase_frames = int(round(actor.phase_ticks * PES_FRAME_MS / GF_FRAME_MS))
        phase_frames %= cycle
        keys, cycle = bake_track(actor, g)
        lines.append("slot %d anims/%s.anim role %s phase %d loop 1"
                     % (actor.slot, stem, actor_role(actor.slot, stem), phase_frames))
        for k in keys:
            lines.append("k %d %.4f %.4f %.5f" % k)
        exported += 1

    if exported == 0:
        raise ValueError("no exportable actors")
    dest = os.path.join(out_dir,
                        os.path.splitext(os.path.basename(fdc_path))[0] + ".chor")
    with open(dest, "w") as out:
        out.write("\n".join(lines) + "\n")
    return dest, exported


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("cut_data", help="fixdemo ent cut_data directory")
    parser.add_argument("animations", help="FoxAnim/FixDemo/Animations directory")
    parser.add_argument("out", help="output root (media/cutscenes/ent)")
    parser.add_argument("--ids", default="",
                        help="comma-separated entrance ids (default: all)")
    args = parser.parse_args()

    ids = set(i.strip() for i in args.ids.split(",") if i.strip())
    families = collect(args.cut_data, ids)
    written = 0
    for family in sorted(families):
        out_dir = os.path.join(args.out, family[-3:])
        os.makedirs(out_dir, exist_ok=True)
        clip_cache = {}
        for path in families[family]:
            try:
                dest, actors = export_pack(path, args.animations, out_dir,
                                           clip_cache)
            except Exception as exc:
                print("SKIP %s: %s" % (os.path.basename(path), exc))
                continue
            written += 1
            print("  %s (%d actors)" % (dest, actors))
    print("exported %d choreography packs" % written)


if __name__ == "__main__":
    main()
