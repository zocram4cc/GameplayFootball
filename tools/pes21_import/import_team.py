"""Imports a whole 4cc team - models, squad, tactics - in one command.

    python3 import_team.py path/to/AET/ path/to/team.ted

and that is the whole procedure. No follow-up script, no manual install step,
no hand-written playermodels.cfg lines. Nothing PES-derived is ever committed
to this repo, so this command is how a user gets a team at all; anything it
does not do is something they cannot have.

It does, in order:

  1. reads the .ted - team name, abbreviation, squad, per-player stats, name
     colours, formation and tactical sliders
  2. writes the team, its players and its tactics into the database, and keeps
     the row id each player landed on
  3. converts every boots.fmdl in the pack into the engine's fullbody format
  4. binds each model to its player's row in playermodels.cfg

Step 4 is why 2 has to come first. A 4cc pack names its exports by shirt
number - <k2411 - Name> is number 11 - so each model can be bound to the row
its player actually got. Renumbering the database and re-keying the models by
hand is what silently unbound both squads before.

The exports all share one layout - the one the 2HUG and HDG packs use:

    <pack>/Boots/<kNNNN - Player Name>/boots.fmdl      full-body model
    <pack>/Gloves/<gNNNN - Player Name>/glove_*.fmdl   (keepers, optional)
    <pack>/Faces/<fNNNN - Player Name>/face.fmdl       (optional)

The models land in data/media/players/custom/<prefix>_<export id>/, the prefix
taken from the team's own abbreviation. Nothing is overwritten without --force.

Without a .ted it still imports models alone, but then --prefix is yours to
give and the database is left untouched (see docs/PES21_IMPORT.md).
"""

import argparse
import os
import re
import subprocess
import sys

import body_coverage
import install_team
import retarget
import ted

EXPORT_DIR_RE = re.compile(r"^([kgf])(\d+)\s*-\s*(.+)$")

# The engine's own skinned body, put under a pack that ships none. Relative to
# the game directory, and the repository's own asset rather than PES's, so an
# import composited onto it stays distributable.
STOCK_BODY_REL = "media/objects/players/models/fullbody.ase"


def find_players(pack_dir, kind="Boots", model_name="boots.fmdl"):
    """-> [(export_id, player_name, fmdl_path)] in export order."""
    root = os.path.join(pack_dir, kind)
    if not os.path.isdir(root):
        return []
    found = []
    for entry in sorted(os.listdir(root)):
        match = EXPORT_DIR_RE.match(entry)
        if not match:
            continue
        fmdl = os.path.join(root, entry, model_name)
        if not os.path.isfile(fmdl):
            continue
        found.append((match.group(2), match.group(3).strip(), fmdl))
    return found


def find_gloves(pack_dir, export_id):
    """-> [fmdl] of this player's hand/forearm slot, l before r.

    A 4cc body export does not always reach the wrist. DBG's pack keeps every
    player's forearms, hands and all nineteen finger joints per side under
    Gloves/gNNNN - 20,770 vertices a side - while Boots/kNNNN stops at the
    elbow. Imported on its own that is a character with no hands and no
    forearms, which is how DBG has looked. The slot letter differs (g against
    k) and the number is shared, which is what ties the two together.
    """
    root = os.path.join(pack_dir, "Gloves")
    if not os.path.isdir(root):
        return []
    for entry in sorted(os.listdir(root)):
        match = EXPORT_DIR_RE.match(entry)
        if not match or match.group(2) != export_id:
            continue
        return [path for path in
                (os.path.join(root, entry, side)
                 for side in ("glove_l.fmdl", "glove_r.fmdl"))
                if os.path.isfile(path)]
    return []


# The verdicts body_coverage.py returns for an export that is not a whole body.
# PES draws a boots or glove export *over* its own body wearing the team's kit - the
# packs' kit textures are DXT1 and carry no alpha, so nothing is hiding that body -
# and binding the prop in its place leaves the prop and nothing else. lcg_2702 is
# `boots` plus `wings`; 2hug_1851 is one `medical_c` mesh.
NOT_A_BODY = ("needs base", "carries scenery")


def may_bind_as_body(verdict, composited=False):
    """Whether an export may replace a player's body.

    `composited` is for --base, which puts the stock skinned body underneath: the
    result clothes the rig whatever the export alone measured.
    """
    if composited:
        return True
    return verdict == "whole"


def describe_import(dest, prefix, export_id):
    """-> body_coverage's verdict on the .ase the import just wrote."""
    ase = os.path.join(dest, "fullbody_%s_%s.ase" % (prefix, export_id))
    if not os.path.isfile(ase):
        return "missing"
    vertices, _ = body_coverage.read_vertices(ase)
    return body_coverage.verdict(vertices, retarget.gf_world_bind())[0]


# A 4cc export names its portraits by team slot rather than by player, because the
# slot a team gets is not known when the pack is built: "XXX07 - Rodya.png". The
# models carry the same slot in their directory name, and their bindings already
# resolve a slot to a database ID - so that is what the portraits ride on.
PORTRAIT_SLOT_RE = re.compile(r"(\d{2})(?:\D|$)")


def portrait_slot(filename):
    """The slot a portrait file names, or None when it names none."""
    base = os.path.basename(filename)
    match = PORTRAIT_SLOT_RE.search(base)
    if not match:
        return None
    return int(match.group(1))


def model_number(model_dir):
    """The export number a model directory carries: .../lcg_2707 -> 2707."""
    base = os.path.basename(model_dir.rstrip("/"))
    digits = re.search(r"_(\d+)$", base)
    return int(digits.group(1)) if digits else None


def portrait_name(filename):
    """The nickname a portrait file carries: "XXX09 - Dante.png" -> "dante"."""
    base = os.path.splitext(os.path.basename(filename))[0]
    trimmed = re.sub(r"^\S+\s*-\s*", "", base).strip().lower()
    return re.sub(r"[^a-z0-9]+", "", trimmed)


def bind_portraits(model_bindings, portrait_files, prefix, names=None):
    """Maps database ID -> portrait path, for players this pack has a model for.

    model_bindings is {database ID: model directory} as playermodels.cfg holds it, and
    names is {database ID: player name} out of the roster - which is the authority on
    who a player is. A 4cc export names its portraits for the player ("XXX09 -
    Dante.png"), so the roster name is what binds them; the slot in the filename is a
    fallback for a pack that numbers its portraits and nothing more.
    """
    # Each pack numbers its own players from a base of its choosing - lcg from 2701,
    # 2hug from 1851, ink from 2426 - while the portraits always count from 01. So the
    # slot is the offset from the pack's lowest number, not the digits themselves.
    numbered = {}
    for database_id, model_dir in model_bindings.items():
        if ("/%s_" % prefix) not in model_dir:
            continue
        number = model_number(model_dir)
        if number is not None:
            numbered[number] = database_id
    base = min(numbered) if numbered else 0
    by_slot = {number - base + 1: database_id for number, database_id in numbered.items()}
    # Both sides usually carry the player's nickname, and that beats the numbering:
    # LCG's portraits run one ahead of its boots from slot 8 on, so binding by slot
    # gives ten players somebody else's face. Fall back to the slot only for a name
    # that appears on neither side or on both.
    # A portrait needs no model: any rostered player can have a face on the game plan,
    # so the name pass runs over the whole roster it was given.
    by_name = {}
    for database_id, player in (names or {}).items():
        nickname = portrait_name("x - %s" % player)
        if not nickname:
            continue
        # A name two players share is no evidence at all.
        by_name[nickname] = None if nickname in by_name else database_id

    # Names first, across the whole set, and only then slots for what is left. One
    # pass would let an unnamed portrait take a player by slot before the portrait
    # that names him is reached - which is exactly LCG's Papa Don and Dante.
    bound = {}
    taken = set()
    unmatched = []
    for name in portrait_files:
        nickname = portrait_name(name)
        database_id = by_name.get(nickname) if nickname else None
        if database_id is None or database_id in taken:
            unmatched.append(name)
            continue
        taken.add(database_id)
        bound[database_id] = "imports/%s/portraits/%s" % (prefix, name)
    # A pack whose portraits name their players has already shown its numbering to be
    # unreliable wherever a name failed to match - LCG's run one ahead from slot 8 -
    # so no guessing from there. Slots are for a pack that offers nothing else.
    if bound:
        return bound
    for name in unmatched:
        slot = portrait_slot(name)
        database_id = by_slot.get(slot) if slot is not None else None
        if database_id is None or database_id in taken:
            continue
        taken.add(database_id)
        bound[database_id] = "imports/%s/portraits/%s" % (prefix, name)
    return bound


# The stock body's parts, and which of them a composited import replaces.
#
# A face-slot import brings its own head: left in place, the stock face, eyes and
# scalp sit inside it and fight it for depth, which reads as a dark doubled head. But
# the 4cc packs ship plenty of hair-only exports (fcl_hair.fmdl) and accessory packs,
# and dropping the stock face for one of those leaves a player with no head at all -
# which is what shipped for hdg_stims and face_100117.
FACE_WORDS = ("face", "head", "visage")
HAIR_WORDS = ("hair", "scalp")


# The GF head joint's bind height, and how much geometry above it counts as a
# head. Measured over the packs: "k2402 - Helldiver Headless" reaches y 1.59 and
# puts *nothing* above the joint, while every export with a head of its own puts
# thousands there - k2411 2,451 and dbg_2004 67,476. Eight is far below any real
# head and far above the nothing a headless pack leaves.
HEAD_JOINT_Y = 1.64
HEAD_PRESENT_VERTICES = 8


def headless(fmdl_path, fmdl_lib, head_top=HEAD_JOINT_Y,
             present=HEAD_PRESENT_VERTICES):
    """Whether an export carries nothing where a head goes.

    The one case worth compositing the engine's own body under: "k2402 -
    Helldiver Headless" ships six meshes and an empty neck ring, so on its own it
    is a suit of armour with no head and no hands.

    Asked of the geometry rather than of `body_coverage.verdict`, which answers
    "needs base" for almost everything - it counts bare finger joints, and its
    32-vertex head threshold was calibrated on PES-resolution heads, so the
    engine's own fullbody.ase fails the same check at 29-31. Compositing on that
    verdict buried most of 2HUG under the stock body, and the stock body has no
    face: a squad of characters became a squad of faceless mannequins in kit.

    False on any read error: guessing wrong here costs a character its face.
    """
    try:
        sys.path.insert(0, fmdl_lib)
        import FmdlFile
        fmdl = FmdlFile.FmdlFile()
        fmdl.readFile(fmdl_path)
        above = 0
        for mesh in fmdl.meshes:
            for v in mesh.vertices:
                if v.position.y > head_top:
                    above += 1
                    if above >= present:
                        return False
        return True
    except Exception:
        return False


def mesh_names(fmdl_path, fmdl_lib):
    """The mesh names an fmdl carries, by their base texture - which is how the 4cc
    exports say what a mesh is. Returns [] when the file cannot be read, so a
    composite falls back to dropping nothing rather than guessing."""
    try:
        sys.path.insert(0, fmdl_lib)
        import FmdlFile
        import fmdl_to_fullbody
        fmdl = FmdlFile.FmdlFile()
        fmdl.readFile(fmdl_path)
        names = []
        for mesh in fmdl.meshes:
            base = fmdl_to_fullbody.mesh_base_texture(mesh)
            if base:
                names.append(base)
        return names
    except Exception:
        return []


def base_parts_to_drop(import_mesh_names):
    """-> the set of stock body parts this import stands in for."""
    drop = set()
    for name in import_mesh_names:
        words = re.split(r"[^a-z0-9]+", (name or "").lower())
        if any(word in FACE_WORDS for word in words):
            drop.update(("face", "eyes", "scalp", "hair"))
        elif any(word in HAIR_WORDS for word in words):
            # The stock body has no hair mesh of its own, so this drops nothing
            # today; naming it keeps the rule honest if one is ever added.
            drop.update(("scalp", "hair"))
    return drop


def install_dir(game_dir, prefix, export_id):
    return os.path.join(game_dir, "media", "players", "custom",
                        "%s_%s" % (prefix, export_id))


def install_kit_texture(pack_dir, dest):
    """Puts the team's outfield kit in the model directory as body.png.

    A 4cc character model carries its own textures for hair, dress and so on,
    but the mesh wearing the actual football kit still points at the shared
    PES kit map (u0XXXp0), which no pack ships - the kit itself lives under
    Kit Textures as u0<team>p1. Without it that one mesh has nothing to
    sample and the loader stops on a missing file.
    """
    kit_dir = os.path.join(pack_dir, "Kit Textures")
    if not os.path.isdir(kit_dir):
        return False
    names = [n for n in sorted(os.listdir(kit_dir)) if n.lower().endswith((".dds", ".png"))]
    outfield = [n for n in names if "p1" in n.lower()] or names
    if not outfield:
        return False
    try:
        from PIL import Image
        image = Image.open(os.path.join(kit_dir, outfield[0]))
        image.load()
        if image.mode != "RGBA":
            image = image.convert("RGBA")
        os.makedirs(dest, exist_ok=True)
        image.save(os.path.join(dest, "body.png"))
        return True
    except Exception as error:
        print("  kit texture failed: %s" % error)
        return False


def import_player(fmdl, dest, fmdl_lib, max_tris, texture_rel, force=False, max_edge=0.15,
                  base_ase=None, extra_fmdls=None):
    ase = os.path.join(dest, "fullbody_%s.ase" % os.path.basename(dest))
    if os.path.exists(ase) and not force:
        return "exists"
    os.makedirs(dest, exist_ok=True)
    command = [sys.executable,
               os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            "fmdl_to_fullbody.py"),
               fmdl, dest, "--fmdl-lib", fmdl_lib,
               "--texture", texture_rel, "--max-tris", str(max_tris),
               "--max-edge", str(max_edge)]
    # The rest of this character, where the pack keeps it in another slot.
    if extra_fmdls:
        command += ["--extra", ",".join(extra_fmdls)]
    if base_ase:
        # A face-slot model is a head and hair, nothing else. Imported on its
        # own it is a head floating where the body should be; it has to be
        # composited over a skinned body.
        command += ["--base", base_ase]
        # And only the stock parts this import actually stands in for are dropped:
        # a hair-only or accessory export brings no head, and dropping the stock
        # face for one of those leaves the player without one.
        drop = base_parts_to_drop(mesh_names(fmdl, fmdl_lib))
        if drop:
            command += ["--drop-base-parts", ",".join(sorted(drop))]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        return "FAILED: " + (result.stderr.strip().splitlines() or ["?"])[-1]
    return "imported"


def install_art(pack_dir, game_dir, tag, dry_run=False):
    """Writes the team's logo and kits where the engine looks for them.

    -> [what was written]. The database rows point at these files, and nothing
    else creates them: `Team::FetchKit` silently substitutes flat white or flat
    black for a kit that is not on disk (team.cpp:142), and a missing logo is
    worse than silent - the scoreboard hands the empty path to the resource
    manager and the match dies with "There is no loader for
    databases/default/", which took a whole showcase run down.

    The pack ships the logo as PNG and the kits as DDS, both of which PIL reads
    directly. Kit `p1`..`p3` are the three outfield kits the engine offers;
    `g1` is the keeper's and currently has nowhere to go, because GF dresses
    every keeper in one global `goalie_kit.png` rather than the team's own.
    """
    from PIL import Image

    out_dir = os.path.join(game_dir, "databases", "default", "images_teams", tag)
    written = []

    logo_dir = os.path.join(pack_dir, "Logo")
    if os.path.isdir(logo_dir):
        # emblem_0XXX_r.png is the full-size emblem; _l and _ll are its
        # smaller mips and would install a blurry logo over the good one.
        emblems = sorted(f for f in os.listdir(logo_dir)
                         if re.match(r"^emblem_.*_r\.(png|dds)$", f, re.I))
        if emblems:
            dest = os.path.join(out_dir, "%s_logo.png" % tag)
            if not dry_run:
                os.makedirs(out_dir, exist_ok=True)
                Image.open(os.path.join(logo_dir, emblems[0])).convert("RGBA").save(dest)
            written.append(os.path.basename(dest))

    # p1..p3 are the outfield kits the engine offers on the options screen;
    # g1 is the keeper's own, which PES gives every team and GF used to have
    # nowhere to put. Installing it as _kit_03 - which is what a hand install
    # did here once - dresses an outfield player in the keeper's shirt.
    kit_dir = os.path.join(pack_dir, "Kit Textures")
    if os.path.isdir(kit_dir):
        for pattern, name in ((r"^u.*p1\.(dds|png)$", "%s_kit_01.png" % tag),
                              (r"^u.*p2\.(dds|png)$", "%s_kit_02.png" % tag),
                              (r"^u.*p3\.(dds|png)$", "%s_kit_03.png" % tag),
                              (r"^u.*g1\.(dds|png)$", "%s_gk.png" % tag)):
            source = [f for f in os.listdir(kit_dir) if re.match(pattern, f, re.I)]
            if not source:
                continue
            dest = os.path.join(out_dir, name)
            if not dry_run:
                os.makedirs(out_dir, exist_ok=True)
                Image.open(os.path.join(kit_dir, source[0])).convert("RGBA").save(dest)
            written.append(name)

    return written


def read_pack_colours(pack_dir):
    """-> (first, second) as the database stores them, or (None, None).

    Every pack states its identity in Note.txt:

        Team Colours:
        - 1st: 255 232 0
        - 2nd: 54 52 50

    and the import used to drop it, leaving color1/color2 NULL. They are not
    decoration: the scoreboard, the crowd banners and the stats overlay all
    read them, and TeamData falls back to black on white for a team without
    them, so /hdg/ played in yellow and appeared in the HUD in black.
    """
    # Every pack writes the note, none agree on the name: "Note.txt",
    # "2hug note.txt", "DBG note.txt".
    notes = [f for f in os.listdir(pack_dir) if re.match(r"^.*note\.txt$", f, re.I)] \
        if os.path.isdir(pack_dir) else []
    if not notes:
        return (None, None)
    found = {}
    for line in open(os.path.join(pack_dir, sorted(notes)[0]), "r", errors="replace"):
        match = re.match(r"^\s*-\s*(1st|2nd)\s*:\s*(\d+)\s+(\d+)\s+(\d+)\s*$", line, re.I)
        if match:
            # DBG zero-pads its channels ("041 081 156"); the database and
            # GetVectorFromString both want plain numbers.
            found[match.group(1).lower()] = ", ".join(
                str(int(channel)) for channel in match.group(2, 3, 4))
    return (found.get("1st"), found.get("2nd"))


def install_portraits(pack_dir, game_dir, tag, by_shirt, dry_run=False):
    """Converts the pack's portraits and binds them to their players.

    -> [(database id, path written)]. The menu reads
    media/players/playerportraits.cfg, one "<databaseID> <png path>" per line,
    and shows a plain card for anyone missing.

    Packs name portraits two ways - 2HUG ships `player_78301.dds` with the full
    PES id, HDG ships `player_XXX21.dds` with the team left as a placeholder -
    but both end in the shirt number, which is the same rule the model exports
    follow, so both resolve through the squad the ted just installed.
    """
    from PIL import Image

    source_dir = os.path.join(pack_dir, "Portraits")
    if not os.path.isdir(source_dir):
        return []

    out_dir = os.path.join(game_dir, "imports", tag, "portraits")
    rel_dir = "imports/%s/portraits" % tag
    written = []
    for name in sorted(os.listdir(source_dir)):
        match = re.match(r"^player_.*?(\d{2})\.(dds|png)$", name, re.I)
        if not match:
            continue
        db_id = by_shirt.get(int(match.group(1)))
        if db_id is None:
            continue
        rel = "%s/player_%d.png" % (rel_dir, db_id)
        if not dry_run:
            os.makedirs(out_dir, exist_ok=True)
            Image.open(os.path.join(source_dir, name)).convert("RGBA").save(
                os.path.join(game_dir, rel))
        written.append((db_id, rel))
    return written


def portrait_shirt(filename):
    """-> the shirt a portrait file belongs to, or None.

    Packs write the name three ways and all three put the shirt last in the
    leading token: "player_78301.png" carries the full PES id, "XXX01 - Bullet
    Sponge.png" leaves the team a placeholder and adds the player's name, and
    an already-installed "player_604.png" carries the database id it was bound
    to. The last two digits of that token are the shirt in every case.
    """
    token = os.path.splitext(os.path.basename(filename))[0].split(" - ")[0].strip()
    match = re.search(r"(\d{2})$", token)
    return int(match.group(1)) if match else None


def relink_portraits(game_dir, database):
    """Rewrites playerportraits.cfg from the portraits on disk. -> lines written.

    The config binds a portrait to a database id, and those ids move: every
    re-import deletes and re-inserts the squad. All 75 entries in this repo's
    config had come adrift that way - the files were all there and every path
    resolved, but not one id still belonged to the player it was written for.
    They had landed on the stock teams, so the first thing to actually draw a
    portrait would have put 2HUG's faces on Masterdam.

    Rebuilt rather than appended: an entry that cannot be regenerated from a
    file on disk and a player in the database is stale by definition.
    """
    import sqlite3

    conn = sqlite3.connect(database)
    try:
        teams = conn.execute("select id, name from teams").fetchall()
        squads = {}
        for team_id, name in teams:
            squads[install_team.art_tag(name)] = {
                order + 1: row[0] for order, row in enumerate(conn.execute(
                    "select id from players where team_id = ? order by formationorder",
                    (team_id,)))}
    finally:
        conn.close()

    lines = []
    root = os.path.join(game_dir, "imports")
    for tag in sorted(os.listdir(root)) if os.path.isdir(root) else []:
        by_shirt = squads.get(tag)
        portraits = os.path.join(root, tag, "portraits")
        if not by_shirt or not os.path.isdir(portraits):
            continue
        for name in sorted(os.listdir(portraits)):
            if not name.lower().endswith(".png"):
                continue
            shirt = portrait_shirt(name)
            if shirt is None or shirt not in by_shirt:
                continue
            lines.append("%d imports/%s/portraits/%s" % (by_shirt[shirt], tag, name))

    path = os.path.join(game_dir, "media", "players", "playerportraits.cfg")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as out:
        out.write("# imported portraits: \"<databaseID> <png path>\"\n")
        out.write("# Rebuilt by import_team.py: database ids move on every\n")
        out.write("# re-import, so this is regenerated rather than appended to.\n")
        out.write("\n".join(lines) + ("\n" if lines else ""))
    return lines


def append_config(path, lines):
    """Appends "<id> <path>" lines, skipping ids the file already binds."""
    existing = open(path).read() if os.path.exists(path) else ""
    fresh = [line for line in lines if line.split()[0] + " " not in existing]
    if not fresh:
        return 0
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "a") as out:
        if existing and not existing.endswith("\n"):
            out.write("\n")
        out.write("\n".join(fresh) + "\n")
    return len(fresh)


def find_fmdl_lib(hint=""):
    """-> the pes-fmdl directory holding FmdlFile.py, or "".

    A tool dependency, not per-team data, so it has no business being on the
    command line of every import. Explicit argument first, then PES_FMDL_LIB,
    then a search of the tree for the 4cc Blender Starter Pack the packs are
    distributed alongside.
    """
    for candidate in (hint, os.environ.get("PES_FMDL_LIB", "")):
        if candidate and os.path.exists(os.path.join(candidate, "FmdlFile.py")):
            return candidate
    here = os.path.dirname(os.path.abspath(__file__))
    for base, dirs, files in os.walk(os.path.normpath(os.path.join(here, "..", ".."))):
        if "FmdlFile.py" in files:
            return base
        # the game's own data and build trees cannot hold it and are large
        dirs[:] = [d for d in dirs if d not in (".git", "build", "data")]
    return ""


def shirt_number(export_id):
    """-> the shirt a <kNNNN - Name> export belongs to.

    4cc packs number their exports by shirt in the last two digits: HDG ships
    k2402, k2411 and k2421 for numbers 2, 11 and 21 against a squad numbered
    1..23. That is the only link between a model on disk and a player in the
    database, so it is what binds them.
    """
    return int(export_id) % 100


def main():
    parser = argparse.ArgumentParser(
        description=__doc__.split("\n")[0],
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("pack_dir", help="the AET pack directory")
    parser.add_argument("ted", nargs="?", default="",
                        help="the team's .ted export; without it no database "
                             "work is done and --prefix is required")
    parser.add_argument("--prefix", default="",
                        help="directory prefix, e.g. 2hug / hdg / a "
                             "(default: the team's own abbreviation)")
    parser.add_argument("--fmdl-lib", default="",
                        help="pes-fmdl directory (default: PES_FMDL_LIB, or found "
                             "in the 4cc Blender Starter Pack)")
    parser.add_argument("--game-dir", default="data")
    parser.add_argument("--database", default="",
                        help="default: <game-dir>/databases/default/database.sqlite")
    parser.add_argument("--tactics", default="",
                        help="sliders to write, k=v,k=v; see install_team")
    parser.add_argument("--first-db-id", type=int, default=0,
                        help="assign consecutive database ids from here; only for "
                             "a pack imported without its .ted")
    parser.add_argument("--db-ids", default="",
                        help="explicit comma-separated database ids, in export order")
    # A ceiling, enforced by refusal rather than by cutting limbs off. Two
    # earlier settings were both wrong: 20,000 threw lcg_2709's head away whole
    # and 100,000 amputated dbg_2014's legs (154,799 faces over 11 meshes), so
    # select_meshes no longer trims a visible character at all. That alone left
    # the import unbounded, which is its own fault - the engine CPU-skins every
    # unique vertex of every player each body tick and has no LOD, so nothing
    # downstream absorbs an oversized model.
    #
    # Measured over the DBG pack: the largest single character is 212,417
    # triangles and a starting eleven is 1.83M. 250,000 admits every real 4cc
    # character whole while still catching a pathological one, and an
    # over-budget model now fails the import loudly - so raising this is a
    # deliberate decision with the cost in view, not a silent default.
    parser.add_argument("--max-tris", type=int, default=250000)
    parser.add_argument("--max-edge", type=float, default=0.15,
                        help="drop triangles with an edge longer than this "
                             "(metres); see fmdl_to_fullbody")
    parser.add_argument("--base", default="",
                        help="stock fullbody .ase to composite the import over; "
                             "required for face-slot models, which carry no body")
    parser.add_argument("--force", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    args.fmdl_lib = find_fmdl_lib(args.fmdl_lib)
    if not args.fmdl_lib:
        print("no pes-fmdl library found; pass --fmdl-lib or set PES_FMDL_LIB")
        return 1

    players = find_players(args.pack_dir)
    if not players:
        print("no <kNNNN - Name>/boots.fmdl exports under", args.pack_dir)
        return 1

    # The database first, so each model can be bound to the row its player
    # actually landed on rather than to a number guessed ahead of time.
    by_shirt = {}
    if args.ted:
        export, _ = ted.read_export(args.ted)
        if not args.prefix:
            args.prefix = re.sub(r"[^a-z0-9]", "", export["team"].lower()) or \
                export["abbreviation"].lower()
        database = args.database or os.path.join(
            args.game_dir, "databases", "default", "database.sqlite")
        export["colour1"], export["colour2"] = read_pack_colours(args.pack_dir)
        tactics = install_team.parse_tactics(args.tactics) or {
            "team_pressure": 0.5, "counter_attack": 0.5, "support_distance": 0.5}
        team_row, by_shirt = install_team.install(database, export, tactics,
                                                  args.dry_run)
        print("%s -> team row %d, %d player(s), %d slider(s)%s"
              % (export["team"], team_row, len(export["squad"]), len(tactics),
                 "  (dry run, rolled back)" if args.dry_run else ""))
        tag = install_team.art_tag(export["team"])
        art = install_art(args.pack_dir, args.game_dir, tag, args.dry_run)
        print("   art: %s" % (", ".join(art) if art else "none found in the pack"))
        portraits = install_portraits(args.pack_dir, args.game_dir, tag,
                                      by_shirt, args.dry_run)
        if not args.dry_run:
            # Rebuilt across every team, not appended for this one: the ids of
            # every squad already installed move whenever any of them is
            # re-imported.
            bound = relink_portraits(args.game_dir, database)
            print("   portraits: %d converted, %d bound across all teams"
                  % (len(portraits), len(bound)))
        else:
            print("   portraits: %d" % len(portraits))
    elif not args.prefix:
        print("give the team's .ted, or --prefix if you only want the models")
        return 1

    explicit = [int(x) for x in args.db_ids.split(",") if x.strip()]
    lines = []
    for index, (export_id, name, fmdl) in enumerate(players):
        dest = install_dir(args.game_dir, args.prefix, export_id)
        db_id = (by_shirt.get(shirt_number(export_id))
                 or (explicit[index] if index < len(explicit) else None)
                 or (args.first_db_id + index if args.first_db_id else None))
        rel = os.path.relpath(dest, args.game_dir).replace(os.sep, "/")
        status = "dry-run"
        if not args.dry_run:
            # Only a whole-character pack needs the kit dropped in beside it:
            # its own mesh wears the kit. A face-slot import is a head, and the
            # body it is composited onto keeps the engine's kit slot, which the
            # team's kit is swapped into at run time (Team::FetchKit).
            if not args.base:
                install_kit_texture(args.pack_dir, dest)
            status = import_player(fmdl, dest, args.fmdl_lib, args.max_tris,
                                   rel + "/body.png", args.force, args.max_edge,
                                   args.base or None,
                                   extra_fmdls=find_gloves(args.pack_dir, export_id))
        # What the import actually produced decides whether it may stand in for a
        # body. An export that leaves the rig's joints bare is a prop PES draws over
        # its own body, and binding it in place of that body leaves only the prop.
        verdict = "whole" if args.dry_run else describe_import(dest, args.prefix, export_id)
        composited = bool(args.base)

        # A pack that ships no head is drawn over PES's base body, and it is the
        # importer's job to put that body under it rather than the user's.
        # Measured on HDG: 2402 and 2421 have no head of their own (0 and 2
        # vertices above the head joint) while 2411 is whole - one command cannot
        # ask which is which. Thirty of the ninety-three installed bodies are in
        # the same position (docs/PES21_IMPORT.md).
        stock_body = os.path.join(args.game_dir, STOCK_BODY_REL)
        # Only when the head is genuinely absent.
        #
        # `verdict` says "needs base" for almost every export, because
        # bare_joints counts finger joints and HEAD_MIN_VERTICES of 32 was
        # calibrated on PES-resolution heads - the engine's own fullbody.ase
        # fails the same check at 29-31. Compositing on that verdict put the
        # stock body under most of 2HUG, and the stock body has no face, so a
        # squad of characters became a squad of faceless mannequins wearing the
        # kit. A pack that ships no head at all - "k2402 - Helldiver Headless"
        # has nothing above y=1.59 - is the case this is for, and it is the only
        # case worth guessing at.
        if (not args.dry_run and not composited and headless(fmdl, args.fmdl_lib)
                and os.path.isfile(stock_body)):
            status = import_player(fmdl, dest, args.fmdl_lib, args.max_tris,
                                   rel + "/body.png", True, args.max_edge, stock_body)
            verdict = describe_import(dest, args.prefix, export_id)
            composited = True
            print("       %s ships no body of its own; composited over %s"
                  % (export_id, os.path.basename(stock_body)))

        bindable = may_bind_as_body(verdict, composited=composited)
        print("%-6s %-28s %-34s %s%s" % (export_id, name[:28], rel, status,
                                         "" if bindable else "  NOT BOUND: " + verdict))
        if db_id is not None and bindable:
            lines.append("%d %s" % (db_id, rel))

    if lines and not args.dry_run:
        added = append_config(
            os.path.join(args.game_dir, "media", "players", "playermodels.cfg"), lines)
        print("wrote %d playermodels.cfg entries" % added)
    return 0


if __name__ == "__main__":
    sys.exit(main())
