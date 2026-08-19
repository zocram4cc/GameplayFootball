"""Ties each goal celebration to the camerawork PES shot it with.

PES ships a goal cutscene as a matched set. For the celebration it calls "banzai":

    goal_2018_run_30_banzai.chor              who performs it, and what they play
    goal_2018_run_30_banzai_Z_fromL.camtrack  the camera, from the left
    goal_2018_run_30_banzai_Z_fromR.camtrack  and from the right

The choreography names the clip its primary actor performs, so the tie is in the
data: strip the angle suffix from a camera's name and what is left is the
choreography it belongs to. Over what we have imported, 38 camera tracks fall onto
19 celebrations and not one is left over.

That matters because the camerawork is only right for the celebration it was shot
for. A camera authored to catch a player wheeling away with his arms out is not the
camera for a knee-slide, and until now the engine picked a track by
`(teamID * 7 + score) % count` - any track for any celebration, which is how a
long-lens shot ended up jammed against a scorer's head.

    goal_cutscenes.py <dir of .chor and .camtrack> [--out <dir>/celebrations.txt]

writes one stanza per celebration:

    celebration goal_2018_run_30_banzai
    clip dml_goal_move3_0002.anim
    camera goal_2018_run_30_banzai_Z_fromL
    camera goal_2018_run_30_banzai_Z_fromR
"""

import argparse
import glob
import os
import re
import sys

# What separates one angle on a celebration from another. PES shoots most of them
# from the left and the right, and flips a couple.
# Measured across the full library rather than guessed: PES separates one angle from
# another with any of these.
ANGLE_SUFFIX = re.compile(r"_(Z_fromL|Z_fromR|Flip|cam\d+|appendCam|audi|cam_up|cam_default)$")


def slots(chor_text):
    """-> the choreography's actors, in file order: clip, role, phase, loop."""
    found = []
    for line in chor_text.splitlines():
        if not line.startswith("slot "):
            continue
        parts = line.split()
        entry = {"slot": int(parts[1]), "clip": os.path.basename(parts[2]),
                 "role": "", "phase": 0, "loop": 0}
        for key in ("role", "phase", "loop"):
            if key in parts:
                value = parts[parts.index(key) + 1]
                entry[key] = value if key == "role" else int(value)
        found.append(entry)
    return found


def primary_clip(chor_text):
    """-> the clip the celebration itself is, or None.

    The primary actor is the scorer; the rest are teammates arriving.
    """
    for entry in slots(chor_text):
        if entry["role"] == "primary":
            return entry["clip"]
    return None


def celebration_base(name):
    """-> the choreography a camera track belongs to."""
    return ANGLE_SUFFIX.sub("", name)


def build(clips_by_chor, camera_names):
    """-> {celebration: {clip, cameras}}.

    A celebration nobody filmed is kept with no cameras - PES ships eleven of
    those and they are perfectly good performances. A camera whose choreography is
    missing is an error: it would be a shot aimed at nothing.
    """
    built = {name: {"clip": clip, "cameras": []} for name, clip in clips_by_chor.items()}
    for camera in sorted(camera_names):
        base = celebration_base(camera)
        if base not in built:
            raise ValueError("camera %s has no choreography (%s)" % (camera, base))
        built[base]["cameras"].append(camera)
    return built


# PES's main celebration library is keyed by a four-digit number rather than by a
# choreography: goal_celebrate_0092_mayaL0x.camtrack films dml_goal_celebrate_0092.anim,
# and there is no .chor between them. 213 numbers have both, which is ten times what
# the .chor route finds.
CELEBRATION_ID = re.compile(r"(?:^goal|^dml_goal)_celebrate_(\d+)")


def celebration_id(name):
    """-> the four-digit celebration number in a camera or performance name, or None."""
    match = CELEBRATION_ID.match(os.path.basename(str(name)))
    return match.group(1) if match else None


def build_by_id(anim_names, camera_names):
    """-> {celebrate_NNNN: {clip, cameras}} for every number that has both.

    A number with a performance and no camera is left out: it would be a celebration
    with no camerawork, and the seeded draw must never land on one. A camera with no
    performance is left out for the same reason in reverse.
    """
    clips = {}
    for name in sorted(anim_names):
        number = celebration_id(name)
        if number and number not in clips:
            clips[number] = os.path.basename(str(name))
    cameras = {}
    for name in sorted(camera_names):
        number = celebration_id(name)
        if number:
            cameras.setdefault(number, []).append(os.path.basename(str(name)))
    built = {}
    for number in sorted(set(clips) & set(cameras)):
        built["celebrate_" + number] = {"clip": clips[number], "cameras": cameras[number]}
    return built


def sort_cameras(camera_names, chor_names):
    """Splits the goal camera pool into the families it actually holds.

    Of PES's 731 goal tracks, 355 are the numbered celebrations and some belong to
    staged cutscenes with choreographies; about 200 are neither - goal_st033_TV and
    friends are a ground's own goal cameras and have nothing to do with what the scorer
    does. Sweeping those in would tie a camera to a performance it never shot.
    """
    families = {"by_id": [], "by_chor": [], "other": []}
    for name in sorted(camera_names):
        if celebration_id(name):
            families["by_id"].append(name)
        elif celebration_base(name) in chor_names:
            families["by_chor"].append(name)
        else:
            families["other"].append(name)
    return families


def merge(*sets):
    """One manifest out of the families, which are named apart and cannot collide."""
    merged = {}
    for one in sets:
        merged.update(one)
    return merged


def assign_variables(built, base=100):
    """Numbers the filmed celebrations so the engine can ask for one by name.

    The engine picks a special animation by specialvar1/specialvar2
    (animcollection.cpp), so tying a camera to a performance means both sides
    agreeing on a number: the manifest carries it, install_anims.py stamps the clip
    with it, and the controller asks for it. Sorted by name so the numbering does not
    move when a celebration is added.

    An unfilmed celebration gets none - there is no camerawork to tie it to.
    """
    numbered = {name: dict(entry) for name, entry in built.items()}
    next_var = base
    for name in sorted(numbered):
        if numbered[name]["cameras"]:
            numbered[name]["var"] = next_var
            next_var += 1
        else:
            numbered[name]["var"] = None
    return numbered


def manifest_text(built):
    lines = [
        "# Which celebration PES shot with which camera, from its own .chor files",
        "# (tools/pes21_import/goal_cutscenes.py). The clip is what the scorer",
        "# performs; the cameras are the angles PES shot that performance from, and",
        "# a celebration with none is one PES never filmed.",
    ]
    for name in sorted(built):
        entry = built[name]
        lines.append("celebration %s" % name)
        lines.append("clip %s" % entry["clip"])
        if entry.get("var") is not None:
            lines.append("var %d" % entry["var"])
        for camera in entry["cameras"]:
            lines.append("camera %s" % camera)
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("dir", help="a directory of PES goal .chor and .camtrack files")
    parser.add_argument("--out", default=None,
                        help="where to write the manifest (default: <dir>/celebrations.txt)")
    args = parser.parse_args()

    clips = {}
    for path in sorted(glob.glob(os.path.join(args.dir, "*.chor"))):
        name = os.path.splitext(os.path.basename(path))[0]
        clip = primary_clip(open(path).read())
        if clip:
            clips[name] = clip
        else:
            print("  %-42s no primary actor, skipped" % name)
    cameras = [os.path.splitext(os.path.basename(p))[0]
               for p in sorted(glob.glob(os.path.join(args.dir, "*.camtrack")))]

    # Two families, named apart. The .chor route pairs a staged cutscene with the
    # cameras that shot it; the numbered route pairs goal_celebrate_NNNN cameras with
    # the dml_goal_celebrate_NNNN performance of the same number, with no .chor
    # between them - and that is where most of PES's celebrations live.
    performances = [os.path.basename(p)
                    for p in sorted(glob.glob(os.path.join(args.dir, "anims", "*.anim")))]
    families = sort_cameras(cameras, set(clips))
    by_id = build_by_id(performances, families["by_id"])
    by_chor = build(clips, families["by_chor"])

    built = assign_variables(merge(by_chor, by_id))
    out = args.out or os.path.join(args.dir, "celebrations.txt")
    open(out, "w").write(manifest_text(built))

    filmed = sum(1 for entry in built.values() if entry["cameras"])
    print("%d celebration(s), %d of them filmed, %d camera track(s) -> %s"
          % (len(built), filmed, sum(len(e["cameras"]) for e in built.values()), out))
    print("   %d from a choreography, %d keyed by number" % (len(by_chor), len(by_id)))
    if families["other"]:
        print("   %d camera(s) are not celebration camerawork (a ground's own goal "
              "cameras and the like), e.g. %s"
              % (len(families["other"]), ", ".join(families["other"][:3])))
    for name in sorted(built):
        if not built[name]["cameras"]:
            print("  %-42s performance only, no camerawork" % name)
    return 0


if __name__ == "__main__":
    sys.exit(main())
