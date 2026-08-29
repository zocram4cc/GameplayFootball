#!/usr/bin/env python3
"""Renders an imported player posed by one frame of an animation.

    pose_render.py <fullbody.ase> <clip.anim> <out.png> [--frame N] [--yaw D]

The instrument the rig work needed and did not have. `gfviewer` loads a static
object, so every judgement about skinning had to be made from numbers, and the
numbers lied twice: dual-quaternion skinning "measured worse" on a metric built
from 2.8 mm edges, and a distance ramp across the armpit cut the count of
stretched edges tenfold while visibly shredding both shoulders into spikes.

This skins offline exactly as `humanoidbase.cpp` does - linear blend, the same
joint walk (`skin_probe.joint_transforms` over `retarget.gf_world_bind`) - and
draws the result with `ase_render`, so a candidate can be looked at rather than
tabulated. No engine, no window, no dependencies.

The weights file sits beside the .ase, which is what `skin_probe.default_ase`
knows; the clip is any converted .anim, and --frame indexes its own keys.
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import ase_render
import retarget
import skin_probe


def posed_vertices(ase_path, weights_path, anim_path, frame):
    """-> [(name, {index: (x, y, z)}, faces)] skinned at `frame`."""
    positions, influences = skin_probe.read_weights(weights_path)
    anim = skin_probe.read_anim(anim_path)
    bind = retarget.gf_world_bind()
    parents = {n: retarget.GF_BIND[n][1] for n in retarget.GF_JOINT_ORDER}
    names = dict(enumerate(retarget.GF_JOINT_ORDER))

    local = {}
    for node, track in anim.items():
        if node == "player" or not isinstance(track, dict):
            continue
        local[node] = skin_probe.pose_at(track, frame)
    world = skin_probe.joint_transforms(bind, parents, local, names and
                                        retarget.GF_JOINT_ORDER)
    skinned = skin_probe.skin(positions, influences, bind, world, names)

    # The .ase lists faces over its own per-mesh vertex numbering; the weights
    # file lists vertices in one run. Matched by position, which is also how
    # skin_probe.mesh_edges welds the seams PES leaves unwelded.
    lookup = {}
    for index, p in enumerate(positions):
        lookup.setdefault((round(p[0], 4), round(p[1], 4), round(p[2], 4)), index)

    out = []
    for name, verts, faces in ase_render.read(ase_path):
        moved = {}
        for key, p in verts.items():
            index = lookup.get((round(p[0], 4), round(p[1], 4), round(p[2], 4)))
            moved[key] = skinned[index] if index is not None else p
        out.append((name, moved, faces))
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("ase")
    parser.add_argument("anim")
    parser.add_argument("out")
    parser.add_argument("--frame", type=int, default=0)
    parser.add_argument("--size", type=int, default=760)
    parser.add_argument("--yaw", type=float, default=25.0)
    parser.add_argument("--pitch", type=float, default=8.0)
    parser.add_argument("--wireframe", action="store_true")
    args = parser.parse_args()

    weights = os.path.splitext(args.ase)[0] + ".weights"
    if not os.path.exists(weights):
        raise SystemExit("no weights beside %s" % args.ase)

    meshes = posed_vertices(args.ase, weights, args.anim, args.frame)
    pixels = ase_render.render(meshes, args.size, args.yaw, args.pitch,
                               args.wireframe)
    if pixels is None:
        raise SystemExit("nothing to draw")
    ase_render.png(args.out, args.size, args.size, pixels)
    print("wrote %s (%d mesh(es), frame %d)"
          % (args.out, len(meshes), args.frame))


if __name__ == "__main__":
    main()
