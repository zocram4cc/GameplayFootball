"""Points every installed kit-slot mesh at the engine's kit slot.

Run once after the importer stopped baking the kit in (fmdl_to_fullbody's
kit-slot routing, 21-09): the models imported before that were written with
the team's first kit baked into the mesh that, in PES, wears whatever kit the
team is wearing. In a match that means they stand in kit 1 no matter what
Team::SetKitNumber is told, because HumanoidBase::SetKit only swaps a material
whose diffuse is the engine's kit slot (kit_template.png).

The source packs are not available every time - and do not need to be: the
defect is in the installed `.ase` itself, and the fix is one *BITMAP per
material, so this repairs what is already on disk and needs nothing else.
Idempotent: a mesh already on the kit slot is left alone.

A kit-slot mesh is the one the importer names after the PES kit map:
`fullbody_dummy_kit`, or the uniform-map name when the pack's mesh is already
named that way (`fullbody_u0821p0`). The engine matches by BASENAME, so
kit_template.png is the one shared file the whole engine swaps into.

  python3 repoint_kit_slot_installed.py [--data ../../data] [--dry-run]
"""

import argparse
import glob
import os
import re
import sys

# The engine's kit slot, and the name HumanoidBase::SetKit swaps it out for.
# The ident string is the basename (ResourceManager::Fetch adapts the filename),
# which is why the match here is on the file name, not the path.
KIT_SLOT_TEXTURE = "media/objects/players/textures/kit_template.png"

# A mesh named after a PES kit map is the kit slot even when the pack named
# its geometry after the map instead of `dummy_kit` (smg_2581's u0821p0).
UNIFORM_MAP_NAME = re.compile(r"u[0-9a-z]{4}p[0-9]")

GEOM_HEADER = re.compile(r'\*GEOMOBJECT \{\n\t\*NODE_NAME "([^"]*)"')
MATERIAL_HEADER = re.compile(r'\*MATERIAL (\d+) \{')
MATERIAL_REF = re.compile(r'\*MATERIAL_REF (\d+)')
BITMAP = re.compile(r'(\*BITMAP ")([^"]*)(")')


def is_kit_geom(name):
    base = name[len("fullbody_"):] if name.startswith("fullbody_") else name
    return base == "dummy_kit" or bool(UNIFORM_MAP_NAME.fullmatch(base))


def repair(text):
    """-> (new text, how many meshes moved to the kit slot).

    Splices the changed blocks back-to-front so earlier offsets stay valid.
    """
    # Which material each geom samples, so a kit's own material is found by
    # reference rather than by hunting a texture name the broken importer
    # happily baked in twice.
    refs = {}
    for m in GEOM_HEADER.finditer(text):
        end = text.find("\n}", m.start())
        ref = MATERIAL_REF.search(text, m.end(), end)
        if ref:
            refs.setdefault(int(ref.group(1)), []).append(m.group(1))

    spans = []
    moved = 0
    for m in MATERIAL_HEADER.finditer(text):
        kit = [n for n in (refs.get(int(m.group(1))) or []) if is_kit_geom(n)]
        if not kit:
            continue
        stop = text.find("\n\t}", m.start())
        block = text[m.start():stop]
        # Only the diffuse map changes. A kit material can carry a bump map
        # too, and rewriting the whole block would repaint that as well.
        diffuse = re.search(r"\*MAP_DIFFUSE \{.*?\n\t\t\}", block, re.S)
        if not diffuse:
            continue
        old = block[diffuse.start():diffuse.end()]
        new_diffuse, count = BITMAP.subn(
            lambda b: b.group(1) + KIT_SLOT_TEXTURE + b.group(3), old)
        if count and new_diffuse != old:
            moved += len(kit)
            spans.append((m.start() + diffuse.start(),
                          m.start() + diffuse.end(), new_diffuse))

    for start, stop, new_block in sorted(spans, reverse=True):
        text = text[:start] + new_block + text[stop:]

    return text, moved


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--data", default=os.path.join(
        os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
        "data"))
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    paths = sorted(glob.glob(os.path.join(
        args.data, "media", "players", "custom", "*", "fullbody_*.ase")))
    if not paths:
        print("no installed models under %s" % args.data)
        return 1

    total = 0
    touched = 0
    for path in paths:
        model = os.path.basename(os.path.dirname(path))
        with open(path, encoding="latin-1") as file:
            text = file.read()
        new, moved = repair(text)
        if not moved:
            continue
        total += moved
        touched += 1
        print("%s: %d mesh(es) -> %s" % (model, moved, KIT_SLOT_TEXTURE))
        if not args.dry_run and new != text:
            with open(path, "w", encoding="latin-1") as file:
                file.write(new)
    print("%d mesh(es) across %d model(s)%s" %
          (total, touched, " (dry run)" if args.dry_run else ""))


if __name__ == "__main__":
    sys.exit(main())
