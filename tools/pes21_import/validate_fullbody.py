"""Offline sanity checker for GF fullbody ASE models (vertex-color skinning).

Decodes the engine's skin encoding exactly like HumanoidBase::
PrepareFullbodyModel: each color channel (0..255) holds one influence as
jointID*10 + weight*9; jointID = floor(channel/10); influences with
weight > 0.01 count. Joint IDs are the player.object DFS order (retarget.GF_JOINT_ORDER)
(retarget.GF_BIND).

Checks per GEOMOBJECT and per model:
  * joint IDs within [0, njoints): the engine indexes joints[] UNCHECKED,
    so an out-of-range ID reads garbage memory -> exploded vertices.
  * channel-0 weight nonzero for every referenced color (engine assert).
  * anchor distance: |vertex - weighted joint anchor|. A vertex far from
    the joints that drive it swings on a huge lever arm during animation
    ("exploded" bodies). Reported as p99/max, failed over --max-anchor.
  * bbox sanity: a bind-pose player must fit a human-sized box.
  * joint coverage: a full-height mesh (z extent > 1.2m) that never
    references leg joints lost its body (e.g. triangle-budget drop).

Usage:
  python3 validate_fullbody.py <model_dir_or_ase> [more...] [--verbose]
  python3 validate_fullbody.py data/media/players/custom/*   # all models
"""

import argparse
import glob
import math
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import retarget

GF_JOINT_ORDER = list(retarget.GF_JOINT_ORDER)
LEG_JOINTS = {retarget.JOINT_ID[n] for n in (
    "left_thigh", "left_knee", "left_ankle",
    "right_thigh", "right_knee", "right_ankle")}
ARM_JOINTS = {retarget.JOINT_ID[n] for n in (
    "left_clavicle", "left_shoulder", "left_elbow", "left_hand",
    "right_clavicle", "right_shoulder", "right_elbow", "right_hand")}


def joint_anchors():
    """joint id -> world bind position (accumulated player.object offsets)."""
    out = {}
    pos = {}
    for i, name in enumerate(GF_JOINT_ORDER):
        offset, parent = retarget.GF_BIND[name]
        if parent is None:
            pos[name] = offset
        else:
            p = pos[parent]
            pos[name] = (p[0] + offset[0], p[1] + offset[1], p[2] + offset[2])
        out[i] = pos[name]
    return out


ANCHORS = joint_anchors()

GEOM_RE = re.compile(r"\*GEOMOBJECT\s*\{")
VERT_RE = re.compile(r"\*MESH_VERTEX\s+(\d+)\s+(-?[\d.]+)\s+(-?[\d.]+)\s+(-?[\d.]+)")
COL_RE = re.compile(r"\*MESH_VERTCOL\s+(\d+)\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)")
FACE_RE = re.compile(r"\*MESH_FACE\s+(\d+):?\s+A:\s+(\d+)\s+B:\s+(\d+)\s+C:\s+(\d+)")
CFACE_RE = re.compile(r"\*MESH_CFACE\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)")


def parse_geoms(path):
    """-> list of dicts {verts: [(x,y,z)], colors: [(r,g,b)], faces, cfaces}."""
    geoms = []
    cur = None
    for line in open(path):
        if GEOM_RE.search(line):
            cur = {"verts": [], "colors": [], "faces": [], "cfaces": []}
            geoms.append(cur)
            continue
        if cur is None:
            continue
        m = VERT_RE.search(line)
        if m:
            cur["verts"].append(tuple(float(v) for v in m.groups()[1:]))
            continue
        m = COL_RE.search(line)
        if m:
            cur["colors"].append(tuple(float(v) for v in m.groups()[1:]))
            continue
        m = FACE_RE.search(line)
        if m:
            cur["faces"].append(tuple(int(v) for v in m.groups()[1:]))
            continue
        m = CFACE_RE.search(line)
        if m:
            cur["cfaces"].append(tuple(int(v) for v in m.groups()[1:]))
    return geoms


def decode(channel):
    """ASE 0..1 color channel -> (jointID, weight 0..1) per the engine."""
    v = channel * 255.0
    joint = int(math.floor(v * 0.1))
    weight = (v - joint * 10.0) / 9.0
    return joint, weight


def validate_geom(geom, njoints=len(GF_JOINT_ORDER), max_anchor=0.85):
    """-> (errors, warnings, stats) for one GEOMOBJECT."""
    errors, warnings = [], []
    verts, colors = geom["verts"], geom["colors"]
    stats = {"nverts": len(verts), "hist": {}, "anchor_p99": 0.0,
             "anchor_max": 0.0, "far": 0}
    if not verts:
        errors.append("no vertices")
        return errors, warnings, stats
    if not colors:
        errors.append("no vertex colors (unskinned mesh)")
        return errors, warnings, stats

    # vertex -> color via the face/cface pairing (like the engine loader)
    vert_color = {}
    if geom["faces"] and len(geom["cfaces"]) == len(geom["faces"]):
        for face, cface in zip(geom["faces"], geom["cfaces"]):
            for vid, cid in zip(face, cface):
                vert_color[vid] = cid
    else:
        for i in range(min(len(verts), len(colors))):
            vert_color[i] = i

    out_of_range = 0
    zero_first = 0
    dists = []          # multi-influence (blend) vertices only
    far_single = 0
    for vid, cid in vert_color.items():
        if cid >= len(colors):
            errors.append("color index %d out of range" % cid)
            continue
        influences = []
        for c, channel in enumerate(colors[cid]):
            joint, weight = decode(channel)
            if c == 0 and weight <= 0.0:
                zero_first += 1
            if weight <= 0.01:
                continue
            if joint < 0 or joint >= njoints:
                out_of_range += 1
                continue
            influences.append((joint, weight))
            stats["hist"][joint] = stats["hist"].get(joint, 0) + 1
        if not influences:
            continue
        total = sum(w for _, w in influences)
        anchor = [0.0, 0.0, 0.0]
        for joint, weight in influences:
            a = ANCHORS[joint]
            for k in range(3):
                anchor[k] += a[k] * weight / total
        p = verts[vid]
        d = math.sqrt(sum((p[k] - anchor[k]) ** 2 for k in range(3)))
        # a single-influence vertex rides its joint rigidly however far it
        # is (props, plates); tearing needs a BLEND across distant joints
        if len(influences) > 1:
            dists.append(d)
            if d > max_anchor:
                stats["far"] += 1
        elif d > max_anchor:
            far_single += 1

    if out_of_range:
        errors.append("%d influences with joint ID out of range 0..%d "
                      "(engine reads unchecked joints[] -> explodes)"
                      % (out_of_range, njoints - 1))
    if zero_first:
        errors.append("%d vertices with zero channel-0 weight (engine assert)"
                      % zero_first)
    if dists:
        dists.sort()
        stats["anchor_p99"] = dists[int(len(dists) * 0.99) - 1]
        stats["anchor_max"] = dists[-1]
        if stats["anchor_p99"] > max_anchor:
            errors.append("blend-vertex anchor-distance p99 %.2fm > %.2fm "
                          "(blends across distant joints: tearing/exploded "
                          "animation)" % (stats["anchor_p99"], max_anchor))
        elif stats["far"]:
            warnings.append("%d/%d blend vertices beyond %.2fm of their "
                            "anchor" % (stats["far"], len(dists), max_anchor))
    if far_single:
        warnings.append("%d rigid (single-influence) vertices beyond %.2fm "
                        "of their joint" % (far_single, max_anchor))
    return errors, warnings, stats


def validate_model(ase_path, max_anchor=0.85, verbose=False):
    """-> True if the model passes. Prints a per-model report line."""
    geoms = parse_geoms(ase_path)
    errors, warnings = [], []
    hist = {}
    nverts = 0
    xs, ys, zs = [], [], []
    for gi, geom in enumerate(geoms):
        g_err, g_warn, stats = validate_geom(geom, max_anchor=max_anchor)
        errors += ["geom%d: %s" % (gi, e) for e in g_err]
        warnings += ["geom%d: %s" % (gi, w) for w in g_warn]
        for j, n in stats["hist"].items():
            hist[j] = hist.get(j, 0) + n
        nverts += stats["nverts"]
        for x, y, z in geom["verts"]:
            xs.append(x); ys.append(y); zs.append(z)

    if zs:
        # 4cc aesthetics are deliberately odd shapes (giant hats, blobs,
        # reclining figures), so the box is generous; true explosions and
        # scale blowups overshoot these limits by multiples
        bbox = (min(xs), max(xs), min(ys), max(ys), min(zs), max(zs))
        if bbox[4] < -0.5 or bbox[5] > 3.2 or \
           max(abs(bbox[0]), abs(bbox[1]), abs(bbox[2]), abs(bbox[3])) > 1.8:
            errors.append("bbox not player-sized: x[%.2f,%.2f] y[%.2f,%.2f] "
                          "z[%.2f,%.2f]" % bbox)
        if bbox[4] > 0.5:
            # nothing anywhere near the ground: a floating fragment (the
            # triangle budget dropped the body and kept a hat/prop)
            errors.append("floating fragment: lowest vertex at z=%.2fm "
                          "(no geometry below mid-body)" % bbox[4])
        z_extent = bbox[5] - bbox[4]
        if z_extent > 1.2 and not (set(hist) & LEG_JOINTS) \
                and len(set(hist)) > 1:
            errors.append("full-height multi-joint mesh (%.2fm) with no "
                          "leg-joint influences (body meshes dropped?)"
                          % z_extent)
        if z_extent <= 1.2:
            warnings.append("mesh spans only %.2fm of height (z %.2f..%.2f)"
                            % (z_extent, bbox[4], bbox[5]))
    if len(set(hist)) <= 2 and nverts > 1000:
        warnings.append("only %d distinct joints referenced (%s): rigid mesh"
                        % (len(set(hist)), sorted(hist)))

    ok = not errors
    name = os.path.basename(ase_path)
    print("%-42s %s" % (name, "PASS" if ok else "FAIL"))
    if verbose or not ok:
        print("    verts=%d joints=%s" % (nverts, sorted(hist)))
        for e in errors:
            print("    ERROR: %s" % e)
        for w in warnings:
            print("    warn:  %s" % w)
    elif warnings and verbose:
        for w in warnings:
            print("    warn:  %s" % w)
    return ok


def find_ase(target):
    if os.path.isfile(target):
        return [target]
    hits = sorted(glob.glob(os.path.join(target, "fullbody_*.ase")))
    return hits or sorted(glob.glob(os.path.join(target, "*.ase")))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("targets", nargs="+",
                        help="model dirs or .ase files")
    parser.add_argument("--max-anchor", type=float, default=0.85,
                        help="p99 vertex-to-anchor distance limit in metres")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()
    failed = 0
    for target in args.targets:
        for ase in find_ase(target):
            if not validate_model(ase, args.max_anchor, args.verbose):
                failed += 1
    sys.exit(1 if failed else 0)
