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

    <pack>/Boots/<kNNNN - Player Name>/boots.fmdl        body, often to the elbow
    <pack>/Gloves/<gNNNN - Player Name>/glove_[lr].fmdl  forearms, hands, fingers
    <pack>/Faces/<XXXnn - Player Name>/face_high.fmdl    head
    <pack>/Faces/<XXXnn - Player Name>/fcl_hair.fmdl     hair

All three are one character and all three are imported. A body export is not
always a whole body: DBG's stops at the elbow and keeps 20,770 vertices of
forearm, hand and finger joints per side under Gloves, and HDG keeps its heads
under Faces - 27 models that had never been read, which is why its "Helldiver
Headless" pack looked headless and was getting the engine's faceless body
composited under it instead of its own skull.

Two naming traps. Gloves shares Boots' numbering; **Faces is keyed by shirt
number** - k2402 and XXX02 are the same player. And the face models are
face_high.fmdl and fcl_hair.fmdl, not face.fmdl.

The models land in data/media/players/custom/<prefix>_<export id>/, the prefix
taken from the team's own abbreviation. Nothing is overwritten without --force.

Without a .ted it still imports models alone, but then --prefix is yours to
give and the database is left untouched (see docs/PES21_IMPORT.md).
"""

import argparse
import os
import re
import shutil
import subprocess
import sys

import body_coverage
import install_team
import retarget
import ted

EXPORT_DIR_RE = re.compile(r"^([kgf])(\d+)\s*-\s*(.+)$")

# The body put under a pack that ships none: PES's own, which is also what the
# engine loads for a player with no model at all (PlayerBody::kDefaultBody).
# It used to be the repository's legacy GF body on a distribution argument that
# does not hold - an imported model is never committed either - and it put GF's
# 453-vertex body inside every composited character: its head inside the
# character's head, and its kit_template shirt for the engine to repaint. That
# is "the default body is back to the GF one" (owner, 05-09). PES's plates
# expect PES's body under them, at PES's kit UVs; the legacy body is kept as
# the fallback for a data directory where PES's has not been generated yet.
STOCK_BODY_REL = "media/objects/players/models/fullbody_pes.ase"
LEGACY_BODY_REL = "media/objects/players/models/fullbody.ase"


def stock_body(game_dir):
    """-> the base body to composite onto, PES's if it has been generated."""
    pes = os.path.join(game_dir, STOCK_BODY_REL)
    return pes if os.path.isfile(pes) else os.path.join(game_dir, LEGACY_BODY_REL)


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


def find_face_players(pack_dir, taken_shirts):
    """-> [(export_id, player_name, fmdl_path)] for the players a pack ships as a
    head and nothing else.

    SMBG keeps five of its characters entirely in Faces/ - K Rool (a whole
    character in the face slot, with his gloves under Gloves/g3117), Miyamoto,
    Yoshit, BUP Toad and Fawful - and HDG keeps 22 heads there for players whose
    body is PES's own. Only the folders whose shirt no boots export claims: a
    head that belongs to a body was merged into it by find_face. The export id
    is the folder's own token (XXX08), so the model lands in <prefix>_XXX08.
    """
    root = os.path.join(pack_dir, "Faces")
    if not os.path.isdir(root):
        return []
    found = []
    for entry in sorted(os.listdir(root)):
        head, _, rest = entry.partition("-")
        digits = "".join(c for c in head if c.isdigit())
        if not digits or int(digits) in taken_shirts:
            continue
        models = [os.path.join(root, entry, model) for model in FACE_MODELS
                  if os.path.isfile(os.path.join(root, entry, model))]
        if models:
            found.append((head.strip(), rest.strip(), models[0]))
    return found


def find_gloves(pack_dir, export_id, name=None):
    """-> [fmdl] of this player's hand/forearm slot, l before r.

    A 4cc body export does not always reach the wrist. DBG's pack keeps every
    player's forearms, hands and all nineteen finger joints per side under
    Gloves/gNNNN - 20,770 vertices a side - while Boots/kNNNN stops at the
    elbow. Imported on its own that is a character with no hands and no
    forearms, which is how DBG has looked. The slot letter differs (g against
    k) and the number is shared, which is what ties the two together - or the
    name, for a character whose body is in the face slot (SMBG's K Rool wears
    g3117 with a face in XXX08).
    """
    root = os.path.join(pack_dir, "Gloves")
    if not os.path.isdir(root):
        return []
    wanted = set(_words(name))
    for entry in sorted(os.listdir(root)):
        match = EXPORT_DIR_RE.match(entry)
        if not match:
            continue
        if match.group(2) != export_id and not (wanted and set(_words(match.group(3))) == wanted):
            continue
        return [path for path in
                (os.path.join(root, entry, side)
                 for side in ("glove_l.fmdl", "glove_r.fmdl"))
                if os.path.isfile(path)]
    return []


# What a Faces directory calls its models. PES splits a head across the face
# itself and the hair, and a 4cc export may ship either or both, under either
# spelling of the hair.
FACE_MODELS = ("face_high.fmdl", "face.fmdl", "fcl_hair.fmdl", "hair_high.fmdl")


def _words(text):
    return [t for t in re.split(r"[^a-z0-9]+", (text or "").lower()) if t]


def face_folder(pack_dir, export_id, name=None):
    """-> (Faces/<XXXnn - Name> directory, shirt) for a boots export, or (None, None).

    The Faces slot is keyed by *shirt number* where Boots is keyed by export id,
    and the two are tied by the player's name first and the digits second. The
    name has to win: LCG's boots run one behind its shirts from 8 on (k2708 is
    Dante, XXX08 is Papa Don and XXX09 is Dante), and by digits alone every LCG
    model from Dante down was on the player before him; SMBG numbers its boots
    k2576..k2593, which is nobody's shirt at all. HDG's `k2402 - Helldiver
    Headless` and `XXX02 - Lobby doko` share no word, and there the digits are
    all there is.
    """
    root = os.path.join(pack_dir, "Faces")
    if not os.path.isdir(root):
        return None, None
    folders = []
    for entry in sorted(os.listdir(root)):
        head, _, rest = entry.partition("-")
        digits = "".join(c for c in head if c.isdigit())
        if digits:
            folders.append((entry, int(digits), set(_words(rest))))
    wanted = set(_words(name))
    if wanted:
        named = [f for f in folders if f[2] == wanted]
        if len(named) == 1:
            return os.path.join(root, named[0][0]), named[0][1]
    shirt = shirt_number(export_id)
    for entry, digits, _ in folders:
        if digits == shirt:
            return os.path.join(root, entry), shirt
    return None, None


def export_shirt(pack_dir, export_id, name=None):
    """-> the shirt a boots export belongs to: the Faces folder that names him,
    else the last two digits of the export id."""
    _folder, shirt = face_folder(pack_dir, export_id, name)
    return shirt if shirt is not None else shirt_number(export_id)


def export_shirts(pack_dir, players):
    """-> {export id: shirt} for a pack's boots exports, each shirt claimed once.

    The exports a Faces folder names take their shirts first; the rest fall back
    to their digits, and only onto a shirt nobody named. LCG's k2713 "KYS" carries
    the digits of Gregor's shirt (XXX13, k2712 by name): letting the digits win
    would put two bodies on one player and leave Gregor's own man in PES's kit.
    An export whose digits are already spoken for is left unbound rather than
    guessed.
    """
    shirts = {}
    named = set()
    for export_id, name, _ in players:
        _folder, shirt = face_folder(pack_dir, export_id, name)
        if shirt is not None and set(_words(name)):
            folder_name = os.path.basename(_folder).partition("-")[2]
            if set(_words(folder_name)) == set(_words(name)):
                shirts[export_id] = shirt
                named.add(shirt)
    for export_id, name, _ in players:
        if export_id in shirts:
            continue
        shirt = shirt_number(export_id)
        if shirt not in named:
            shirts[export_id] = shirt
    return shirts


def find_face(pack_dir, export_id, name=None):
    """-> [fmdl] of this player's head, face before hair.

    A filename the first version did not expect - `face_high.fmdl` and
    `fcl_hair.fmdl` rather than `face.fmdl` - is why 27 head and hair models in
    the HDG pack had never been imported, and why the "Headless" boots pack looked
    headless: its head was in the other slot all along.
    """
    folder, _shirt = face_folder(pack_dir, export_id, name)
    if not folder:
        return []
    return [path for path in (os.path.join(folder, model) for model in FACE_MODELS)
            if os.path.isfile(path)]


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

    Everything else binds unless the export is scenery. It used to require
    `verdict == "whole"`, and that verdict is not a completeness signal - it
    counts bare finger joints and its head threshold rejects the engine's own
    fullbody.ase - so a real character with real geometry was being refused and
    left unused. "carries scenery" is the one verdict that means what it says:
    while a backdrop is in the file the model's bounds are the backdrop's and
    nothing about it can be judged, so those stay out.
    """
    if composited:
        return True
    return verdict != "carries scenery"


def describe_import(dest, prefix, export_id):
    """-> body_coverage's verdict on the .ase the import just wrote."""
    ase = os.path.join(dest, "fullbody_%s_%s.ase" % (prefix, export_id))
    if not os.path.isfile(ase):
        return "missing"
    vertices, _ = body_coverage.read_vertices(ase)
    return body_coverage.verdict(vertices, retarget.gf_world_render_bind())[0]


def teams_bound_to_prefix(conn, game_dir, prefix):
    """-> [team id] whose players playermodels.cfg already binds to this prefix."""
    path = os.path.join(game_dir, "media", "players", "playermodels.cfg")
    if not os.path.isfile(path):
        return []
    ids = []
    for line in open(path).read().splitlines():
        fields = line.split()
        if len(fields) > 1 and ("/%s_" % prefix) in fields[1]:
            try:
                ids.append(int(fields[0]))
            except ValueError:
                continue
    if not ids:
        return []
    placeholders = ",".join("?" for _ in ids)
    rows = conn.execute("select distinct team_id from players where id in (%s)" % placeholders,
                        ids).fetchall()
    return [row[0] for row in rows if row[0] is not None]


def team_for_prefix(game_dir, database, prefix):
    """-> (team id, team name) for the team whose art tag is `prefix`, or None.

    A team's art tag and its model prefix are not always the same word: SMBG's art
    is images_teams/smbg/ while its models are smg_25xx, because the tag comes from
    the team name and the prefix from whoever ran the import. Either being a prefix
    of the other identifies the team; failing that, the bindings that already exist
    say which team this prefix belongs to. Only an unambiguous single match counts.
    """
    path = database or os.path.join(game_dir, "databases", "default", "database.sqlite")
    if not os.path.isfile(path):
        return None
    import sqlite3
    try:
        conn = sqlite3.connect(path)
        try:
            teams = conn.execute(
                "select id, name, kit_url from teams where kit_url is not null").fetchall()
            wanted = []
            for team_id, _name, kit_url in teams:
                tag = kit_url.strip("/").split("/")[1] if "/" in kit_url else kit_url
                if tag and (tag.startswith(prefix) or prefix.startswith(tag)):
                    wanted.append(team_id)
            if len(wanted) != 1:
                wanted = teams_bound_to_prefix(conn, game_dir, prefix)
            if len(wanted) != 1:
                return None
            names = {team_id: name for team_id, name, _ in teams}
            return wanted[0], names.get(wanted[0], "")
        finally:
            conn.close()
    except sqlite3.Error:
        return None


def read_roster(game_dir, database, prefix):
    """-> {database id: player name} for the team whose art tag is `prefix`.

    Read from the database rather than the .ted so a models-only re-import can
    bind by name too. An unknown prefix or a missing database is an empty
    roster, and the caller falls back to what it did before.
    """
    team = team_for_prefix(game_dir, database, prefix)
    if team is None:
        return {}
    import sqlite3
    path = database or os.path.join(game_dir, "databases", "default", "database.sqlite")
    try:
        conn = sqlite3.connect(path)
        rows = conn.execute(
            "select id, lastname from players where team_id = ?", (team[0],)).fetchall()
        conn.close()
    except sqlite3.Error:
        return {}
    return {int(row[0]): row[1] or "" for row in rows}


def bind_by_name(exports, roster):
    """-> {export id: database id}, matching each export to the player it names.

    A pack folder carries the player's name - "k2580 - Shiddy" - and so does the
    roster, in the team's own style: "Shiddy" against "SHIDDY", "Wario Land 4"
    against "MY GREATEST ACHIEVEMENT WARIO LAND 4", "Pianta Chuckster" against
    "I'M A CHUCKSTER". When the export numbering says nothing about the squad,
    that name is the only honest link.

    SMBG numbers its exports k2576..k2593 against shirts 1..23, so
    shirt_number() finds nobody and the import fell back to the pack's directory
    order - which is not the roster's. Measured on the installed squad: twelve of
    fourteen players wore another player's model, two places out, and SHIDDY had
    FUCK LUIGI's.

    Best match first, over the whole set at once, rather than in directory
    order: "Luigi" shares a word with "FUCK LUIGI" and would take his row before
    the export that names him in full is even reached. Words only, on letters and
    digits, so a substring cannot claim a row; a tie claims nothing.
    """
    def tokens(text):
        return [t for t in re.split(r"[^a-z0-9]+", text.lower()) if t]

    scored = []
    for export_id, export_name in exports:
        wanted = tokens(export_name)
        if not wanted:
            continue
        for database_id, player in roster.items():
            have = set(tokens(player))
            hits = sum(1 for t in wanted if t in have)
            if hits:
                # a name matched in full is worth more than one matched in part
                scored.append((hits, hits / float(len(wanted)), export_id, database_id))
    scored.sort(reverse=True)

    bound, taken = {}, set()
    for hits, share, export_id, database_id in scored:
        if export_id in bound or database_id in taken:
            continue
        # an export whose best is a tie between two players names neither
        rivals = [row for row in scored
                  if row[2] == export_id and row[3] not in taken and row[:2] == (hits, share)]
        if len(rivals) > 1:
            continue
        bound[export_id] = database_id
        taken.add(database_id)
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


# What "a whole body" means, in bands of the GF bind: a head above the joint, a
# torso through the middle, and feet near the ground. Measured over the packs:
# HDG's "Helldiver Headless" has *nothing* above the joint, LCG's k2701 is a face
# and a shark prop with no legs, and every complete character - k2411, dbg_2004 -
# clears every band by thousands of vertices. Eight and sixty are far below what
# any real band holds and far above what an absent one leaves.
HEAD_JOINT_Y = 1.64
HEAD_PRESENT_VERTICES = 8
TORSO_PRESENT_VERTICES = 60
FEET_PRESENT_VERTICES = 60
# A character need not have feet to be whole: a ghost has none, and giving it
# PES's legs is worse than giving it nothing. What it does have is one
# continuous body from at least hip height up to its head, which a face or a
# prop set never has - measured in 10 cm slices, each of which must hold more
# than a strap's worth of geometry.
HIP_JOINT_Y = 0.95
BODY_SLICE_M = 0.10
BODY_SLICE_VERTICES = 20
# And it has to be the body on the rig. HDG's k2421 is a headless helldiver at
# the origin with Alexus standing two metres behind him, gun and speech bubble to
# 2.2 m; counted whole, Alexus's head passed for the helldiver's and the player
# went out with no head. A mesh whose centre is this far off the rig's axis is a
# sidekick or a prop, not the body. Measured over the 89 exports on disk: every
# glove sits at 0.6-0.7 m, Wart's widest piece at 0.83, and Alexus's own pieces
# start at 1.11 - Gregor (lcg k2712) and Alexus are the only characters entirely
# off the axis, which is exactly a body PES draws its own player beside.
SIDEKICK_RADIUS_M = 1.0

# A body has mass on the rig's own axis at chest height. Measured over HDG's 22
# exports (vertices between 0.95 m and 1.55 m within 0.25 m of the axis):
#
#   John Helldiver 14996   Lobby doko 16384   I FUCKING HATE SWEDEN 16841
#   Mechwarrior     5756   SEAF-chan   3155   Ragdolled              4487
#   ...
#   Mothdiver        118   Brapdiver    116   Bullet Sponge            19
#   Malicious Code     0   Cuck Throne    0   Not gonna sugarcoat it    0
#
# Everything under a couple of hundred is a prop set standing where a player
# should be - a corpse on the ground, a gas column, a stim scatter, a throne -
# and those are the ones the owner sees as a player-shaped hole on the pitch.
# is_continuous_body passes them because a pill or a launcher does span hip to
# head with no gap; this is the test it was missing.
TORSO_AXIS_RADIUS_M = 0.25
TORSO_AXIAL_VERTICES = 200


# The chest band in rig metres, stretched only for a character taller than the
# rig: 0.95-1.55 is where a PES chest is, and a 2.5 m 4cc character's chest is
# proportionally higher. Measuring it as a pure fraction of the model's own
# height was wrong for a mesh that IS only a chest - there the height is the
# chest's own and the band slid off it.
CHEST_LOW_M = 0.95
CHEST_HIGH_M = 1.55
RIG_HEIGHT_M = 1.81

# Per 10 cm slice of that band, on the axis. A total was not enough: SMBG's
# "Shiddy" is legs, arms and a large head with NOTHING between - 1 axial vertex
# at 1.0-1.1 m and 0 at 1.1-1.2 - and his head alone put 800 into a band that
# reached 1.55, so the gate called him a whole body and he was drawn with no
# torso, his legs hanging under a floating head (owner, 05-09).
TORSO_AXIAL_SLICE_VERTICES = 20


def axial_torso_slices(meshes, slice_m=BODY_SLICE_M):
    """-> (total, thinnest slice) of axial chest vertices.

    Measured over the character's own chest band, one slice at a time: a torso
    is continuous on the axis, and a head sitting above a gap is not a torso.
    """
    heights = [v.position.y for mesh in meshes if mesh.vertices and not _off_the_rig(mesh)
               for v in mesh.vertices]
    if not heights:
        return 0, 0
    scale = max(1.0, max(heights) / RIG_HEIGHT_M)
    low, high = CHEST_LOW_M * scale, CHEST_HIGH_M * scale
    if high - low < slice_m:
        return 0, 0
    slices = [0] * max(1, int((high - low) / slice_m))
    for mesh in meshes:
        if not mesh.vertices or _off_the_rig(mesh):
            continue
        for v in mesh.vertices:
            y = v.position.y
            if not (low <= y < high):
                continue
            if v.position.x ** 2 + v.position.z ** 2 >= TORSO_AXIS_RADIUS_M ** 2:
                continue
            slices[min(len(slices) - 1, int((y - low) / slice_m))] += 1
    return sum(slices), min(slices)


def axial_torso_count(meshes):
    """-> vertices at chest height within TORSO_AXIS_RADIUS_M of the rig axis.

    Split out from whole_body so it can be measured on its own: it is the test
    that tells a player from a prop standing in his place.
    """
    return axial_torso_slices(meshes)[0]


def whole_body(fmdl_paths, fmdl_lib):
    """Whether these slots together dress the whole rig.

    Head, torso and feet, measured on the geometry the character will actually
    wear - Boots, Gloves and Faces merged, because a pack legitimately splits one
    player across them.

    This is the composite gate, and it has to be a whole-body test rather than a
    head test. LCG's k2701 "Clapped" is a face and a shark prop: it has a head,
    so a head-only gate skipped the composite and the player rendered as a
    floating shark. PES draws such an export over its own body, and that is what
    the stock body under it is for.

    The feet are not required when the export is a body in its own right, which
    is the second test below. SMBG's k2582 is a Boo: it stops at 0.83 m because a
    ghost has no legs, and the band test read that as a partial export and gave
    it PES's - which is the "legs where there shouldn't be" the owner reported.
    Across the five packs on disk exactly two exports take this path: that Boo,
    and a whole K Rool shipped in the Faces slot.

    True on any read error: a needless composite costs nothing, a missing one
    costs the character.
    """
    bands = {"head": 0, "torso": 0, "feet": 0}
    heights = []
    meshes = []
    try:
        sys.path.insert(0, fmdl_lib)
        import FmdlFile
        for path in fmdl_paths:
            fmdl = FmdlFile.FmdlFile()
            fmdl.readFile(path)
            for mesh in fmdl.meshes:
                if not mesh.vertices or _off_the_rig(mesh):
                    continue
                meshes.append(mesh)
                for v in mesh.vertices:
                    y = v.position.y
                    heights.append(y)
                    if y > HEAD_JOINT_Y:
                        bands["head"] += 1
                    elif y < 0.35:
                        bands["feet"] += 1
                    elif 0.95 < y < 1.55:
                        bands["torso"] += 1
    except Exception:
        return True
    # No chest on the axis, no body: whatever else these vertices are, a player
    # is not standing here and PES's own body has to go underneath. Continuous
    # on the axis, not merely numerous: a head above a hole is not a torso.
    axial_torso, thinnest = axial_torso_slices(meshes)
    if axial_torso < TORSO_AXIAL_VERTICES or thinnest < TORSO_AXIAL_SLICE_VERTICES:
        return False
    if (bands["head"] >= HEAD_PRESENT_VERTICES
            and bands["torso"] >= TORSO_PRESENT_VERTICES
            and bands["feet"] >= FEET_PRESENT_VERTICES):
        return True
    return is_continuous_body(heights)


def _off_the_rig(mesh):
    """Whether a mesh's centre stands too far from the rig's axis to be the body."""
    xs = [v.position.x for v in mesh.vertices]
    zs = [v.position.z for v in mesh.vertices]
    cx = 0.5 * (min(xs) + max(xs))
    cz = 0.5 * (min(zs) + max(zs))
    return cx * cx + cz * cz > SIDEKICK_RADIUS_M ** 2


def is_continuous_body(heights):
    """Whether these vertex heights are one body from the hip up to the head.

    Reaching the head is what rules out a pair of boots, starting at or below the
    hip is what rules out a face, and no empty slice in between is what rules out
    a prop set - LCG's k2583 has geometry from the floor to 2.18 m and nine holes
    through it, because it is a blaster and a tail rather than a character.
    """
    if not heights:
        return False
    low, high = min(heights), max(heights)
    if low > HIP_JOINT_Y or high < HEAD_JOINT_Y:
        return False
    slices = [0] * (int((high - low) / BODY_SLICE_M) + 1)
    for y in heights:
        slices[min(len(slices) - 1, int((y - low) / BODY_SLICE_M))] += 1
    return all(count >= BODY_SLICE_VERTICES for count in slices)


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


def kit_texture_name(dest):
    """The kit texture's file name inside a model directory.

    Unique per model, because the engine's resource manager keys surfaces by
    BASENAME (ResourceManager::Fetch): every model directory used to hold this
    file as plain "body.png", so all 186 installed models resolved to whichever
    one loaded first and every player in the match wore the first team's kit.
    Owner, 05-09: "Wario is mapping on HDG's kit", HDG being team 1 and Wario
    an SMBG player. The pack's own textures were already prefixed for exactly
    this reason; this one was not.
    """
    return "%s_kit.png" % os.path.basename(os.path.normpath(dest))


def install_kit_texture(pack_dir, dest):
    """Puts the team's outfield kit in the model directory as <model>_kit.png.

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
        image.save(os.path.join(dest, kit_texture_name(dest)))
        return True
    except Exception as error:
        print("  kit texture failed: %s" % error)
        return False


def import_player(fmdl, dest, fmdl_lib, max_tris, texture_rel, force=False, max_edge=0.15,
                  base_ase=None, extra_fmdls=None, drop_stray=False):
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
    if base_ase:
        # A face-slot model is a head and hair, nothing else. Imported on its
        # own it is a head floating where the body should be; it has to be
        # composited over a skinned body.
        command += ["--base", base_ase]
    if extra_fmdls:
        # The same character's other slots - hands under Gloves, head under Faces
        # - merged in as more meshes. This list was gathered and never handed
        # over, which is why HDG's "Helldiver Headless" stayed headless with its
        # skull sitting in Faces/XXX02, and DBG's forearms stayed in Gloves.
        command += ["--extra", ",".join(extra_fmdls)]
    if drop_stray:
        command += ["--drop-stray"]
    if base_ase:
        # And only the stock parts this import actually stands in for are dropped:
        # a hair-only or accessory export brings no head, and dropping the stock
        # face for one of those leaves the player without one. The head may be in
        # any of the slots, so every one of them is asked.
        names = []
        for path in [fmdl] + list(extra_fmdls or []):
            names += mesh_names(path, fmdl_lib)
        drop = base_parts_to_drop(names)
        if drop:
            command += ["--drop-base-parts", ",".join(sorted(drop))]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        return "FAILED: " + (result.stderr.strip().splitlines() or ["?"])[-1]
    # The converter's own measurements belong in this run's log. Swallowing them
    # is how "no seam was welded in four teams" came to be believed: the weld
    # was working (783 vertices on one SMBG player) and its line was going into
    # a captured pipe nobody read.
    for line in (getattr(result, "stdout", "") or "").splitlines():
        stripped = line.strip()
        if stripped.startswith(("seams:", "dropped", "texture ")) and "NOT FOUND" not in stripped:
            print("       " + stripped)
        elif "NOT FOUND" in stripped:
            print("       " + stripped)
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
        # Two ways a pack writes a colour: DBG's zero-padded decimal channels
        # ("041 081 156") and SMBG's CSS hex ("#b30000"). The database and
        # GetVectorFromString both want plain decimal numbers. Only the team
        # colours are read; the "Kit Colours" block below them pairs two per
        # line and is not what the HUD is asking for.
        match = re.match(r"^\s*-\s*(1st|2nd)\s*:\s*(\d+)\s+(\d+)\s+(\d+)\s*$", line, re.I)
        if match:
            found[match.group(1).lower()] = ", ".join(
                str(int(channel)) for channel in match.group(2, 3, 4))
            continue
        match = re.match(r"^\s*-\s*(1st|2nd)\s*:\s*#([0-9a-f]{6})\s*$", line, re.I)
        if match:
            rgb = match.group(2)
            found[match.group(1).lower()] = ", ".join(
                str(int(rgb[i:i + 2], 16)) for i in (0, 2, 4))
    return (found.get("1st"), found.get("2nd"))


def portrait_sources(pack_dir):
    """-> [(png name to write, source file)] for every portrait a pack ships.

    Two places, and a pack may use either or both: a `Portraits/` folder of
    `player_78301.dds` / `player_XXX21.dds`, and a `portrait.dds` inside each
    `Faces/<XXXnn - Name>/` folder - which is where LCG, SMBG, HDG and DBG keep
    theirs, and which the import never read, so those squads played with no faces
    on the game plan. The written name keeps the token that carries the shirt
    (portrait_shirt), and for a face folder that is the folder's own name.
    """
    out = []
    seen = set()

    def take(name, source):
        shirt = portrait_shirt(name)
        if shirt is None or shirt in seen:
            return
        seen.add(shirt)
        out.append((name, source))

    portraits = os.path.join(pack_dir, "Portraits")
    if os.path.isdir(portraits):
        for name in sorted(os.listdir(portraits)):
            if re.match(r"^player_.*?\d{2}\.(dds|png)$", name, re.I):
                take(os.path.splitext(name)[0] + ".png", os.path.join(portraits, name))
    faces = os.path.join(pack_dir, "Faces")
    if os.path.isdir(faces):
        for folder in sorted(os.listdir(faces)):
            for name in ("portrait.dds", "portrait.png"):
                source = os.path.join(faces, folder, name)
                if os.path.isfile(source):
                    take(folder + ".png", source)
                    break
    return out


def install_portraits(pack_dir, game_dir, tag, dry_run=False):
    """Converts the pack's portraits to imports/<tag>/portraits/. -> [paths written].

    The directory is rebuilt from the pack: an earlier version named the files by
    database id, and those ids move on every re-import, so a leftover
    `player_814.png` was later read as shirt 14 and bound a second face to a player
    who already had one. relink_portraits binds what is written here by shirt.
    """
    from PIL import Image

    sources = portrait_sources(pack_dir)
    if not sources:
        return []
    out_dir = os.path.join(game_dir, "imports", tag, "portraits")
    written = []
    if not dry_run:
        if os.path.isdir(out_dir):
            shutil.rmtree(out_dir)
        os.makedirs(out_dir)
    for name, source in sources:
        rel = "imports/%s/portraits/%s" % (tag, name)
        if not dry_run:
            Image.open(source).convert("RGBA").save(os.path.join(game_dir, rel))
        written.append(rel)
    return written


def portrait_shirt(filename):
    """-> the shirt a portrait file belongs to, or None.

    Packs write the name three ways and all three put the shirt last in the
    leading token: "player_78301.png" carries the full PES id, "XXX01 - Bullet
    Sponge.png" leaves the team a placeholder and adds the player's name, and
    "XXX06- Miyamoto" (SMBG) forgets the space before the dash. The last two
    digits of that token are the shirt in every case.
    """
    token = os.path.splitext(os.path.basename(filename))[0].split("-")[0].strip()
    match = re.search(r"(\d{2})$", token)
    return int(match.group(1)) if match else None


def squad_by_shirt(database, team_id):
    """-> {shirt: database id} for one team, shirts counted in formation order -
    the same order install_team writes a .ted's squad in."""
    import sqlite3

    conn = sqlite3.connect(database)
    try:
        return {order + 1: row[0] for order, row in enumerate(conn.execute(
            "select id from players where team_id = ? order by formationorder", (team_id,)))}
    finally:
        conn.close()


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
    finally:
        conn.close()
    squads = {install_team.art_tag(name): squad_by_shirt(database, team_id)
              for team_id, name in teams}

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


def write_config(path, lines):
    """Writes "<id> <path>" lines, replacing whatever those ids were bound to.

    Appending and skipping ids the file already mentioned is what let a wrong
    binding survive a re-import: SMBG's squad was bound by position on its first
    import, and every later run saw the ids "already there" and left twelve
    players wearing another player's model. A binding is a fact about one id, so
    a fresh one replaces it and every other line is kept as it stands.

    -> (written, replaced).
    """
    kept, replaced = [], 0
    ids = {line.split()[0] for line in lines}
    models = {line.split()[1] for line in lines if len(line.split()) > 1}
    if os.path.exists(path):
        for existing in open(path).read().splitlines():
            fields = existing.split()
            # Both directions: one player has one model, and one model belongs to
            # one player. Replacing only by id left the model bound twice when it
            # moved to another player, which is how a squad ends up with two men
            # in the same body.
            if fields and (fields[0] in ids or (len(fields) > 1 and fields[1] in models)):
                if existing.strip() not in [l.strip() for l in lines]:
                    replaced += 1
                continue
            kept.append(existing)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as out:
        out.write("\n".join(kept + lines).rstrip("\n") + "\n")
    return len(lines), replaced


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
    digits = re.search(r"(\d+)$", str(export_id))
    return int(digits.group(1)) % 100 if digits else -1


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
    # And the players the pack ships only as a head - their body is PES's own, so
    # the head goes on the stock body; a whole character in the face slot (SMBG's
    # K Rool) stands on its own by whole_body.
    shirts = export_shirts(args.pack_dir, players)
    for export_id, name, fmdl in find_face_players(args.pack_dir, set(shirts.values())):
        players.append((export_id, name, fmdl))
        shirts[export_id] = shirt_number(export_id)
    if not players:
        print("no <kNNNN - Name>/boots.fmdl or Faces exports under", args.pack_dir)
        return 1

    # The database first, so each model can be bound to the row its player
    # actually landed on rather than to a number guessed ahead of time.
    by_shirt = {}
    database = args.database or os.path.join(
        args.game_dir, "databases", "default", "database.sqlite")
    tag = None
    if args.ted:
        export, _ = ted.read_export(args.ted)
        if not args.prefix:
            args.prefix = re.sub(r"[^a-z0-9]", "", export["team"].lower()) or \
                export["abbreviation"].lower()
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
    elif not args.prefix:
        print("give the team's .ted, or --prefix if you only want the models")
        return 1
    else:
        # A models-only re-import still owes the squad its faces, and its exports
        # still bind by shirt first: the team is already in the database, so both
        # its art tag and its shirt order are known.
        team = team_for_prefix(args.game_dir, args.database, args.prefix)
        tag = install_team.art_tag(team[1]) if team else None
        if team:
            by_shirt = squad_by_shirt(database, team[0])
    if tag:
        portraits = install_portraits(args.pack_dir, args.game_dir, tag, args.dry_run)
        if not args.dry_run:
            # Rebuilt across every team, not appended for this one: the ids of
            # every squad already installed move whenever any of them is
            # re-imported.
            bound = relink_portraits(args.game_dir, database)
            print("   portraits: %d converted, %d bound across all teams"
                  % (len(portraits), len(bound)))
        else:
            print("   portraits: %d" % len(portraits))

    explicit = [int(x) for x in args.db_ids.split(",") if x.strip()]
    # Who each export belongs to, by the name it carries, before anything falls
    # back to counting. The roster is the database's own, so a re-import with no
    # .ted can still bind correctly - which is what the pack's own folder names
    # are for (model_owner).
    roster = read_roster(args.game_dir, args.database, args.prefix) if not by_shirt else {}
    by_name = bind_by_name([(export_id, name) for export_id, name, _ in players], roster)
    if roster:
        print("   named: %d of %d export(s) matched a squad name" % (len(by_name), len(players)))
    lines = []
    for index, (export_id, name, fmdl) in enumerate(players):
        dest = install_dir(args.game_dir, args.prefix, export_id)
        db_id = (by_shirt.get(shirts.get(export_id))
                 or by_name.get(export_id)
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
            # Every other slot of the same character: the hands, and the head.
            # A pack splits one player across Boots, Gloves and Faces, and only
            # the first was ever read.
            rest_of_him = [path for path in
                           find_gloves(args.pack_dir, export_id, name)
                           + find_face(args.pack_dir, export_id, name)
                           if path != fmdl]
            # A scenery export needs its backdrop gone and a body under it, or
            # every view frames the pair down to a dot. The verdict is only
            # known after a first import, so this is a second pass over the same
            # output: force=True, strays dropped, stock body composited.
            status = import_player(fmdl, dest, args.fmdl_lib, args.max_tris,
                                   rel + "/" + kit_texture_name(dest), args.force, args.max_edge,
                                   args.base or None, extra_fmdls=rest_of_him)
            verdict = "whole" if args.dry_run else describe_import(dest, args.prefix, export_id)
            if verdict == "carries scenery" and os.path.isfile(
                    stock_body(args.game_dir)):
                status = import_player(fmdl, dest, args.fmdl_lib, args.max_tris,
                                       rel + "/" + kit_texture_name(dest), True, args.max_edge,
                                       stock_body(args.game_dir),
                                       extra_fmdls=rest_of_him, drop_stray=True)
                verdict = describe_import(dest, args.prefix, export_id)
                composited = True
                print("       %s carries scenery; strays dropped, body composited"
                      % export_id)
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
        base_body = stock_body(args.game_dir)
        # Asked of every slot together, and as a whole-body test: HDG's
        # "Helldiver Headless" boots stop at the neck but its head is in
        # Faces/XXX02, so a head-only gate skips the composite it needs - while
        # LCG's k2701 has a head and no legs, so a head-only gate wrongly skips
        # the composite it also needs. What decides it is whether the character
        # dresses the whole rig once every slot is on.
        if (not args.dry_run and not composited and os.path.isfile(base_body)
                and not whole_body([fmdl] + rest_of_him, args.fmdl_lib)):
            status = import_player(fmdl, dest, args.fmdl_lib, args.max_tris,
                                   rel + "/" + kit_texture_name(dest), True, args.max_edge,
                                   base_body, extra_fmdls=rest_of_him)
            verdict = describe_import(dest, args.prefix, export_id)
            composited = True
            print("       %s ships no body of its own; composited over %s"
                  % (export_id, os.path.basename(base_body)))

        bindable = may_bind_as_body(verdict, composited=composited)
        print("%-6s %-28s %-34s %s%s" % (export_id, name[:28], rel, status,
                                         "" if bindable else "  NOT BOUND: " + verdict))
        if db_id is not None and bindable:
            lines.append("%d %s" % (db_id, rel))

    if lines and not args.dry_run:
        written, replaced = write_config(
            os.path.join(args.game_dir, "media", "players", "playermodels.cfg"), lines)
        print("wrote %d playermodels.cfg entries (%d replaced)" % (written, replaced))
    return 0


if __name__ == "__main__":
    sys.exit(main())
