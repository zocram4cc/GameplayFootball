# What this repository does not ship, and how to build it

Nothing derived from PES 2021 or from the 4cc community packs is kept in this
repository. The camerawork, the choreography, the celebration clips, the player
body, the kits and badges, the scoreboard theme and the stadiums are Konami's
work or the pack authors', and they are not ours to redistribute.

What is here instead is everything needed to build them yourself, from your own
installation: `tools/pes21_import/`. The formats are documented in
[PES21_IMPORT.md](PES21_IMPORT.md), which is the reference for why each step
does what it does; this file is the running order.

**Step 2 is currently required.** The engine's default player body is
`fullbody_pes.ase`, and it stops with a fatal error if that file is missing
rather than falling back to the legacy `fullbody.ase` beside it:

    [FATAL ERROR] in [tree_load::]: could not open
        media/objects/players/models/fullbody_pes.ase

So a fresh clone does not run until you have built the body. That is a bug, not
a design — the fallback the `player_body` config key implies is not actually
wired up — and it is worth fixing so the repository is playable out of the box.
Everything else below is genuinely optional: each step adds one layer of PES
fidelity and is independent of the others.

## What you need

* A PES 2021 installation (its `Data/` directory).
* The 4cc packs, if you want 4cc teams: `download/4cc_*.cpk` from the mod.
* The `pes-fmdl` Blender addon, for anything that reads `.fmdl`. Point
  `--fmdl-lib` at the directory holding `FmdlFile.py`.
* `ffmpeg`, for the chants.
* Python 3 with Pillow.

## 1. The bulk pack — portraits, animations, adboards, chants, faces

    python3 tools/pes21_import/build_pes21_pack.py "/path/to/PES21/Data" \
        --pack data/imports/pes21

Converts `body_anime_file*.mtar` to the engine's `.anim`, player portraits and
advertising boards to PNG, and `CHANT.awb` to ogg. Add
`--faces <id>,<id>,...` for face models; there are about nine thousand players,
so it takes a list rather than all of them.

## 2. The default player body

PES composes its match player from parts, so this assembles one:

    python3 tools/pes21_import/pes_base_body.py data/media/objects/players/models \
        --common   <common_package>/Assets/pes16/model/character/common \
        --undershirt <dt32>/Asset/model/character/parts/undershirt/scenes/#Win/undershirt.fmdl \
        --shirt-body <dt32>/Asset/model/character/parts/bibs/scenes/#Win/bibs.fmdl \
        --boots <a boots.fmdl> --boots-skl <its boots.skl> \
        --face  <a face_high.fmdl> \
        --stock data/media/objects/players/models/fullbody.ase \
        --fmdl-lib <pes-fmdl dir> --kit-uv native

Note `--shirt-body`: the uniform-mapped torso is the part whose material is
`mod_latest_uni_shirts`, and the sleeves come from the undershirt's `torso_mat`.
`--kit-uv native` keeps PES's own uniform mapping, which is what makes a team's
`u0<team>p<n>` kit sheet land on the right panels. Extract `dt00_x64.cpk.bak`
rather than the live `dt00`, which the 4cc mod edits.

## 3. Cutscenes: camerawork, choreography, celebrations

    python3 tools/pes21_import/export_cutscenes.py "/path/to/PES21/Data" \
        --out data/media/cutscenes

Camera tracks come from `dt12_g4`'s `.fdc`/`.canm` (see `canm_to_camtrack.py`),
the entrance choreography from the `_pl` packs (`entrance_pl.py`,
`export_entrances.py`), and the goal celebrations from the animation archive.
Skip it and the pre-match presentation has no camerawork or choreography to
play; set `entrance_id none` to go straight to kickoff.

## 4. Stadiums

    python3 tools/pes21_import/stadium_to_gf.py <stadium fmdl> \
        data/media/objects/stadiums/pes_st002

Then point `stadium_object` at it in your config.

## 5. Scoreboard and formation-panel theme

    python3 tools/pes21_import/export_scoreboard_theme.py "/path/to/PES21/Data" \
        --out data/media/ui/pes
    python3 tools/pes21_import/export_formation_theme.py "/path/to/PES21/Data" \
        --out data/media/ui/pes

Only needed for `scoreboard_theme pes`; the engine's own scoreboard is the
default and needs nothing.

## 6. Teams: squads, kits and badges

For a 4cc team, the roster mapping lives in the mod's base archive and the
models in the faces archive:

    python3 tools/pes21_import/cpk.py <...>/download/4cc_01_base.cpk /tmp/pesdb \
        --filter=pesdb
    python3 tools/pes21_import/cpk.py <...>/download/4cc_40_faces.cpk /tmp/team \
        --filter=face/real/<teamId>
    python3 tools/pes21_import/import_team.py <staged pack> --prefix <name> \
        --fmdl-lib <pes-fmdl dir> --game-dir data \
        --base data/media/objects/players/models/fullbody_pes.ase

Kits and badges come out of the uniform archive: the badge is already a PNG at
`common/render/symbol/flag/e_<teamId padded>_r.png`, and the kits are
`u0<teamId>p1`..`p8` `.ftex` beside it — decode with `ftex.py` and install as
`<kit_url>_kit_0<n>.png`, with `kit_url` set on the team's row.
PES21_IMPORT.md covers the staging layout and the `EDIT` decryption for real
player names.
