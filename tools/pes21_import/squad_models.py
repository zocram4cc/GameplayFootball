"""Assigns a 4cc pack's models to a squad, which takes the team's tactical export.

A pack does not name its files after players. It names them after shirt numbers,
because that is what PES's slots are keyed by:

    Faces/XXX02 - Lobby doko          the face and hair for whoever wears 2
    Boots/k2411 - Helldiver           the body in the boots slot for shirt 11

Nothing in the pack says which engine player wears which shirt, so a squad model
import *requires* the .ted: `ted.read_squad` is the only thing that pairs a
player with a number. Importing /hdg/ without it bound bodies to the three
players whose shirt number happened to have its own boots folder and left the
other twenty on the stock body, which is what a showcase then showed.

A pack also shares bodies - PES lets one boots model serve any number of players.
/hdg/ ships three for twenty-three, and which one a player takes follows from his
own face folder: PES draws the boots model and the face model together, so a
player whose folder carries a face mesh takes the headless body and a player with
only hair takes the complete one. That is why the pack ships "Helldiver" and
"Helldiver Headless" as a pair, and why the two of them are shirts 11 and 02
while shirt 21, Alexus, is a character of his own.

  python3 squad_models.py <team.ted> <pack dir>

prints the assignment it would import.
"""

import argparse
import os
import re
import sys

import ted

FACE_DIR = re.compile(r"^XXX(\d{2})\b")
BOOTS_DIR = re.compile(r"^k\d*?(\d{2})\b")

# What marks a body as drawn without a head, so it pairs with a face mesh.
HEADLESS = re.compile(r"headless", re.IGNORECASE)


def shirt_of(name):
    """-> the shirt number a pack folder is named for, or None."""
    match = FACE_DIR.match(name) or BOOTS_DIR.match(name)
    return int(match.group(1)) if match else None


def read_faces(faces_dir):
    """-> {shirt: {dir, face, hair}} for a pack's Faces folder.

    `face_high.fmdl` is a face mesh and `fcl_hair.fmdl` is hair; a folder may
    ship either, both, or only textures.
    """
    found = {}
    if not os.path.isdir(faces_dir):
        return found
    for name in sorted(os.listdir(faces_dir)):
        shirt = shirt_of(name)
        if shirt is None:
            continue
        files = os.listdir(os.path.join(faces_dir, name))
        found[shirt] = {"dir": name,
                        "face": any(f.startswith("face_high") for f in files),
                        "hair": any(f.startswith("fcl_hair") for f in files)}
    return found


def read_boots(boots_dir):
    """-> {shirt: dir} for a pack's Boots folder."""
    found = {}
    if not os.path.isdir(boots_dir):
        return found
    for name in sorted(os.listdir(boots_dir)):
        shirt = shirt_of(name)
        if shirt is not None:
            found[shirt] = name
    return found


def _shared(boots, want_headless):
    """-> the body a player without his own takes, or None."""
    headless = [d for d in boots.values() if HEADLESS.search(d)]
    complete = [d for d in boots.values() if not HEADLESS.search(d)]
    wanted = headless if want_headless else complete
    pool = wanted or complete or headless
    return sorted(pool)[0] if pool else None


def assign(squad, faces, boots):
    """-> {player id: {body, face_dir, hair, why}}.

    An empty squad is refused rather than guessed at: without the tactical export
    there is nothing to key the pack's shirt numbers against.
    """
    if not squad:
        raise ValueError("no squad: a squad model import needs the team's .ted "
                         "to pair players with shirt numbers")
    out = {}
    for entry in squad:
        shirt = entry["number"]
        face = faces.get(shirt)
        if shirt in boots:
            body, why = boots[shirt], "own boots folder for shirt %d" % shirt
        else:
            wants_headless = bool(face and face["face"])
            body = _shared(boots, wants_headless)
            why = ("shared %s body (his folder %s a face mesh)"
                   % ("headless" if wants_headless else "complete",
                      "carries" if wants_headless else "ships no")) if body else "no body in the pack"
        out[entry["id"]] = {"body": body,
                            "face_dir": face["dir"] if face else None,
                            "hair": bool(face and face["hair"]),
                            "why": why}
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("ted", help="the team's tactical export")
    parser.add_argument("pack", help="the aesthetics pack directory")
    args = parser.parse_args()

    export, plain = ted.read_export(args.ted)
    faces = read_faces(os.path.join(args.pack, "Faces"))
    boots = read_boots(os.path.join(args.pack, "Boots"))
    assignment = assign(export["squad"], faces, boots)

    print("%s: %d players, %d face folder(s), %d body/bodies"
          % (export["team"], len(assignment), len(faces), len(boots)))
    numbers = {e["id"]: e["number"] for e in export["squad"]}
    for pid in sorted(assignment):
        got = assignment[pid]
        print("  %-7d #%-3d %-28s %-22s %s"
              % (pid, numbers[pid], got["body"] or "-",
                 got["face_dir"] or "-", got["why"]))
    bodies = {}
    for got in assignment.values():
        bodies[got["body"]] = bodies.get(got["body"], 0) + 1
    print("\n" + ", ".join("%d x %s" % (n, b or "no body")
                           for b, n in sorted(bodies.items(), key=lambda kv: -kv[1])))
    return 0


if __name__ == "__main__":
    sys.exit(main())
