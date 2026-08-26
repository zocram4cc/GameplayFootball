"""Installs converted pack animations into the live game's anim collection.

The engine scans data/media/animations recursively at startup, so installing
is copying the .anim in with the metadata the target class needs. Installed
files are named pes_<name>.anim (one .gitignore rule keeps generated content
out of the repo).

Two kinds of class live here.

*Presentation* classes (celebrations, entrances) only need a `<type>special`
tail and the specialvar pair the controller queries -- the clip plays start to
finish and nothing in it has to line up with the ball.

*Match* classes (sliding, keeper) have to survive the animation selector, and
that takes more than a type name: GF measures velocity and body angles off the
root curve, refuses to run any non-movement clip that has no ball keyframe,
and needs to know where the clip leaves the player standing or lying. All of
that is measured from the converted curves by anim_metrics -- see
docs/PES21_ANIMS.md for what the selector reads and why.

  python3 install_anims.py <src.anim> --class happy_normal [--game-dir data]
  python3 install_anims.py <dir> --class sliding --batch
  python3 install_anims.py <dir> --class keeper --batch --report
"""

import argparse
import math
import os

import anim_metrics as am

# --- presentation classes ---------------------------------------------------

# specialvar1/specialvar2 are how the engine asks for a particular special animation
# (animcollection.cpp matches on them), so they are the handle a celebration is
# selected by.
#
# A PES celebration is two clips: an intro, and a loop the scorer holds until the
# celebration is over. 66 loop clips come out of the import and none used to be
# installed, so a scorer played 1.2 seconds and then fell back to whatever his
# controller did next - which is why a celebration read as unfinished however long
# kCelebration_ms was. A loop goes in beside its intro and keeps specialvar1, so it is
# the same mood, with specialvar2 raised by ten so it can be asked for separately
# (GoalCelebration::Phase decides when).
LOOP_VAR_OFFSET = 10

CLASSES = {
    "happy_normal": ("celebration/happy_normal", {"specialvar1": 1, "specialvar2": 1}),
    "happy_extreme": ("celebration/happy_extreme", {"specialvar1": 1, "specialvar2": 2}),
    "sad_normal": ("celebration/sad_normal", {"specialvar1": 2, "specialvar2": 1}),
    "entrance_lineup": ("entrance/lineup", {"specialvar1": 3, "specialvar2": 1}),
}
for _name in ("happy_normal", "happy_extreme", "sad_normal"):
    _subdir, _vars = CLASSES[_name]
    CLASSES[_name + "_loop"] = (_subdir, {"specialvar1": _vars["specialvar1"],
                                          "specialvar2": _vars["specialvar2"] + LOOP_VAR_OFFSET})


def is_loop(path):
    """Whether a clip is the held part of a celebration rather than its opening."""
    return os.path.splitext(os.path.basename(str(path)))[0].endswith("_loop")


def intro_of(path):
    """-> the clip a loop belongs to, or None if it is not a loop."""
    name = os.path.basename(str(path))
    stem, ext = os.path.splitext(name)
    if not stem.endswith("_loop"):
        return None
    return stem[: -len("_loop")] + ext

# --- match classes ----------------------------------------------------------

MATCH_CLASSES = ("sliding", "interfere", "keeper", "trick", "movement", "trap")

# GF files match clips under <type>/<incoming velocity>/, and the selector
# leans on that reading being right, so the directory is derived, not chosen.
VELOCITY_DIRS = ("idle", "dribble", "walk", "sprint")

# A keeper who ends the clip holding the ball has it stuck to this node; GF
# reads the variable as a node name (Humanoid::Process, "superglue powers").
RETAIN_NODE = "right_elbow"

# How much clip to keep past the ball contact. GF's own sliding clips run
# about half a second past their touch; PES takes twice that before it lets
# go, and a player frozen in a follow-through cannot react.
FOLLOW_THROUGH_FRAMES = 45

# PES names its keeper clips by what they do. 'hold'/'catch' end with the ball
# in the gloves, everything else parries it away.
CATCH_TOKENS = ("catch", "hold")
# ...and these are not saves at all. PES keeps a keeper's whole repertoire in
# one family: distribution (throws, kicks, drop-balls), shuffling along the
# line, getting back up, set-up hops before a save, and clips that exist to be
# beaten. Selected as deflects they would have the keeper throwing the ball at
# a shot, or shuffling sideways while it goes past him.
KEEPER_SKIP_TOKENS = ("throw", "kick", "punt", "dropball", "goalkick", "pass",
                      "walk", "run", "idle", "wait", "ready", "reaction",
                      "keeperout", "trap", "dribble",
                      "move", "rise", "prejump", "collapsing", "cancel",
                      "fall", "series", "nutmeg", "miss", "cut", "keep",
                      "hold_", "carry", "wipe", "signal", "cheer")


def _classify_keeper(name):
    lowered = name.lower()
    for token in KEEPER_SKIP_TOKENS:
        if token in lowered:
            return None
    return "catch" if any(t in lowered for t in CATCH_TOKENS) else "deflect"


def _vector(v):
    return "%f,%f,%f" % tuple(v)


def prepare_match_anim(path, anim_class):
    """-> (subdirectory, filename stem, .anim text) or (None, None, reason).

    Everything the tail says is measured off the curves: which velocity
    bucket the clip lands in, where it leaves the player, where and when a
    limb is on the ball.
    """
    if anim_class == "keeper" and _classify_keeper(os.path.basename(path)) is None:
        return None, None, "not a save"

    anim = am.parse_anim(path)
    if "body" not in anim.nodes or len(anim.player) < 4:
        return None, None, "no usable curves"

    am.condition_root(anim)
    am.trim_to_rest(anim)
    if anim.last_frame < 12:
        return None, None, "too short after trimming"

    contact = None
    if anim_class != "movement":
        finder = {"keeper": am.keeper_contact,
                  "trick": am.touch_contact,
                  "trap": am.touch_contact}.get(anim_class, am.sliding_contact)
        contact = finder(anim)
        if contact is None or contact["frame"] >= anim.last_frame:
            return None, None, "no ball contact"

        # An action clip holds the player until it runs out; PES
        # follow-throughs run on far longer than GF's, so keep enough to read
        # as a follow-through and give the engine back its player.
        if anim.last_frame > contact["frame"] + FOLLOW_THROUGH_FRAMES:
            am.clip_to(anim, contact["frame"] + FOLLOW_THROUGH_FRAMES)

    _, speed = am.incoming_velocity(anim)
    velocity = am.VELOCITY_NAME[am.quantize_velocity(speed)]
    lie = am.lie_state(anim)
    variables = {}

    if anim_class == "movement":
        # movement is the one family GF forbids a ball keyframe on
        # (AnimCollection::_PrepareAnim complains about the reverse too)
        if lie is not None:
            return None, None, "ends on the ground: not locomotion"
        steps = am.step_count(anim)
        if steps:
            variables["steps"] = steps
        variables["type"] = "movement"
        # locomotion is blended from a hand-tuned template set and topped up
        # by thousands of autogenerated transitions; dropping mocap into the
        # middle of that is a playtest, not a smoke test
        return (os.path.join("movement", velocity, "experimental"),
                os.path.splitext(os.path.basename(path))[0],
                am.render_anim(anim, variables, []))

    if anim_class in ("sliding", "interfere"):
        # a slide ends on the deck and GF plays its own stand-up off
        # <outgoing_special_state>; an upright challenge must never claim to,
        # or the player drops to the grass he is standing on
        if anim_class == "sliding":
            if lie is None:
                return None, None, "stays on its feet: not a slide"
            variables["balldirection"] = _vector(contact["direction"])
            variables["outgoing_special_state"] = lie
            variables["type"] = "sliding"
            subdir = os.path.join("sliding", velocity, "pes")
        else:
            if lie is not None:
                return None, None, "goes to ground: not an interfere"
            variables["incomingballdirection"] = _vector(
                am.incoming_ball_direction(contact["position"],
                                           shot_distance=3.0,
                                           shot_height=am.BALL_RADIUS))
            variables["incomingballdirection_maxdeviation"] = "0.5"
            variables["type"] = "interfere"
    elif anim_class == "trick":
        if lie is not None:
            return None, None, "goes to ground: not a touch"
        # ballcontrol is the type the possession game runs through and GF
        # filters it strictly on direction, so an imported batch competes
        # directly with the tuned locomotion set. These ship dark: the
        # engine skips any path containing "experimental" unless
        # `anim_experimental` is set (AnimCollection::Load).
        variables["type"] = "ballcontrol"
        subdir = os.path.join("ballcontrol", velocity, "experimental")
    elif anim_class == "trap":
        if lie is not None:
            return None, None, "goes to ground: not a trap"
        # A trap receives a ball that is arriving, so the selector matches on
        # the incoming ball direction like interfere does; the touch keyframe
        # (appended below) is what lets GetBestCheatableAnimID check that the
        # clip can actually reach the ball before it wins first refusal.
        variables["incomingballdirection"] = _vector(
            am.incoming_ball_direction(contact["position"],
                                       shot_distance=3.0,
                                       shot_height=am.BALL_RADIUS))
        variables["incomingballdirection_maxdeviation"] = "0.5"
        variables["type"] = "trap"
        subdir = os.path.join("trap", velocity, "pes")
    else:
        kind = _classify_keeper(os.path.basename(path))
        if kind is None:
            return None, None, "not a save"
        variables["incomingballdirection"] = _vector(
            am.incoming_ball_direction(contact["position"]))
        # a dive covers a lot of goal, so let it answer for a wide spread of
        # shots rather than only the one angle it was mocapped against
        variables["incomingballdirection_maxdeviation"] = "0.4"
        if kind == "catch":
            variables["outgoing_retain_state"] = RETAIN_NODE
        if lie:
            variables["outgoing_special_state"] = lie
        variables["type"] = "deflect"
        subdir = os.path.join("deflect", velocity, "pes")

    touch = (contact["frame"],) + tuple(contact["position"])
    return subdir, os.path.splitext(os.path.basename(path))[0], \
        am.render_anim(anim, variables, [touch])


def install(src, anim_class, game_data_dir="data"):
    if anim_class in MATCH_CLASSES:
        subdir, stem, text = prepare_match_anim(src, anim_class)
        if subdir is None:
            return None, text            # text carries the reason
        dest = os.path.join(game_data_dir, "media", "animations", subdir,
                            "pes_" + stem + ".anim")
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        open(dest, "w").write(text)
        return dest, None

    subdir, variables = CLASSES[anim_class]
    lines = []
    for line in open(src):
        if line.startswith("<"):
            break
        lines.append(line.rstrip("\n"))
    for key, value in variables.items():
        lines.append("<%s>" % key)
        lines.append("\t%s" % value)
        lines.append("</%s>" % key)
    lines.append("<type>")
    lines.append("\tspecial")
    lines.append("</type>")

    name = "pes_" + os.path.splitext(os.path.basename(src))[0] + ".anim"
    dest = os.path.join(game_data_dir, "media", "animations", subdir, name)
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    open(dest, "w").write("\n".join(lines) + "\n")
    return dest, None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("src")
    parser.add_argument("--class", dest="anim_class", required=True,
                        choices=sorted(set(CLASSES) | set(MATCH_CLASSES)))
    parser.add_argument("--game-dir", default="data")
    parser.add_argument("--batch", action="store_true",
                        help="src is a directory of .anim files")
    parser.add_argument("--limit", type=int,
                        help="install at most this many (best first by name)")
    parser.add_argument("--report", action="store_true",
                        help="list what went where, and what was rejected")
    args = parser.parse_args()

    if not args.batch:
        dest, reason = install(args.src, args.anim_class, args.game_dir)
        print("installed %s" % dest if dest else "skipped: %s" % reason)
        return

    installed, skipped = [], []
    for name in sorted(os.listdir(args.src)):
        if not name.endswith(".anim"):
            continue
        if args.limit and len(installed) >= args.limit:
            break
        try:
            dest, reason = install(os.path.join(args.src, name),
                                   args.anim_class, args.game_dir)
        except Exception as exc:                      # a bad clip is not fatal
            dest, reason = None, "%s: %s" % (type(exc).__name__, exc)
        (installed if dest else skipped).append((name, dest or reason))

    if args.report:
        for name, dest in installed:
            print("  + %-52s %s" % (name[:52], os.path.dirname(dest)))
        for name, reason in skipped:
            print("  - %-52s %s" % (name[:52], reason))
    print("installed %d as %s, skipped %d"
          % (len(installed), args.anim_class, len(skipped)))


if __name__ == "__main__":
    main()
