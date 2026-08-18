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

## What is in the repository, and how to check

Nothing derived from PES 2021 or from the 4cc packs is tracked. Two tracked files
have "pes" in the name and neither is artwork: `fullbody_pes.object` is a
ten-line descriptor pointing at a model you build, and
`menu_smoke_pes_stadium.config` is a config. The check is one command:

    git ls-files | grep -iE 'fullbody_pes\.(ase|png)|pes_st|/ent/|\.camtrack$|\.chor$|_bsm|_alp\.'

It should print nothing. The import outputs are covered by `.gitignore` -
stadiums, adboards, LUTs, banners, nets, the player body, the entrance packs, the
PES UI theme - so a build that has been through `tools/pes21_import` still has a
clean `git status`.

The one thing that used to need Konami's artwork even in a fresh checkout was the
scoreboard's bitmap font, because the code named `media/ui/pes/num_mid.fnt`
directly. The shipped default is now built from Fira Sans Condensed ExtraBold
(SIL OFL, licence included) by `tools/art/make_scoreboard_font.py`, and
`scoreboard_font_dir` points at the PES export for anyone who wants the exact
glyphs.

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

A 4cc stadium pack is a directory named `<slot> - <name>`, with the scene model
under `#Win/st<slot>_fpk_extracted/` and the textures under
`sourceimages/tga/#windx11/`:

    python3 tools/pes21_import/stadium_to_gf.py \
        "017 - planet namek/#Win/st017_fpk_extracted/center1.fmdl" \
        data/media/objects/stadiums/pes_st017 \
        --fmdl-lib "4cc Blender Starter Pack/scripts/addons/pes-fmdl" \
        --textures "017 - planet namek/sourceimages/tga/#windx11" \
        --name pes_st017 --max-extent 1300

Then point `stadium_object` at the `.object` it writes.

`--max-extent` drops meshes spanning more than that many metres. The default of
260 keeps the bowl and throws away the surrounding apron, which is right for a
real stadium; a pack whose setting *is* the view - Namek's terrain is 276 m and
its sky dome 1154 m across, benuldys has backdrop pieces 4.7 km across - needs
it raised or the vista disappears. `convert_stadiums.sh` passes 6000, at which
the pieces that would have been dropped are recognised as backdrop and go to the
sky object rather than into the scene.

Three things about these conversions are worth knowing, because each one produced
a stadium that looked broken in a way that had nothing to do with the pack:

* **Outlines.** PES draws cel-shaded outlines as an inverted hull: the mesh again,
  pushed out along its normals a few centimetres, front faces culled. Half of
  Namek's materials are that pass. Written with the source winding into an engine
  that culls back faces, each shell covers the mesh it was meant to outline, and
  the whole stadium reads as flat grey. The converter reverses their winding, and
  their normals with it, so they render as the outlines they are.
* **The pitch.** Its colour is generated at match start by `proceduralpitch.cpp`,
  which overwrites the texture resources named `pitch_0N.png` - so a stadium
  cannot point its pitch materials at its own turf without killing the match. The
  converter drops the pack's turf beside the `.object` as `turf.png`, which the
  engine prefers over its own grass, and takes it at its own colour
  (`src/onthepitch/pitchturf.hpp`). GF still draws the lines and the mow striping.
* **Adboards.** Not in the stadium pack at all. PES keeps them in a common
  package, `4cc_15_billboard.cpk` (`Asset/model/bg/common/bill/`): one shared
  board model plus a few hundred `bill_NNN_bsm` panels. The engine has its own
  randomiser, which replaces the diffuse of any stadium mesh whose texture ident
  begins with `ad_placeholder` with a random PNG from
  `media/textures/adboards/`.
* **The sky.** A pack's sky dome is culled by default, because the camera is
  inside it and its faces point outward, and forcing it visible blows it out
  because it is lit like any other surface. Both are fixed on import - reversed
  winding, constant normals via `ase_util.write_mesh_normals` - but the dome
  still has to leave the stadium node: `SplitGeometry` buckets stadium meshes
  into 24 m grid cells, and a 1154 m dome never rasterises there. The converter
  writes it to `sky/sky.ase` + `sky.object` instead, and `Match` loads that
  through the engine's own `skydome_object` path (which also keeps it out of the
  shadow map and floors the far plane), so the clouds and moons arrive with it.
  The colours are sampled into `sky.txt` as well, for the postprocess gradient
  behind everything the dome does not cover.

### The people beside the pitch

A stadium pack does not carry them. PES keeps one copy in its common package and
hands it to every ground - and Planet Namek's own staff pack is one of the
48-byte empty overrides, so without this there is nobody on the touchline:

    python3 tools/pes21_import/stadium_staff.py \
        <extracted>/Asset/model/bg/st002/staff \
        data/media/objects/stadiums/pes_st017/staff \
        --fmdl-lib "4cc Blender Starter Pack/scripts/addons/pes-fmdl" \
        --textures <extracted 4cc_30_stadiums.cpk>/Asset/model/bg/st002 \
        --asset-dir pes_st017/staff

The models are in the *stadium* packs' staff directories and their skins are in
the stadium pack's own sourceimages - which is why they are named after teams
(`staff_doomyuri1`, `staff_lizard`, `staff_jkraptor`): the 4cc mod replaces them,
and that is what the reference broadcast shows on the touchline. They are rigged
but stand still, so they import as static geometry in their bind pose, already
placed in the technical areas, facing the pitch and set out by their own depth so
nothing reaches over the line. `Match` loads `staff/staff.object` from beside the
stadium the same way it finds the sky. The `.ase` is text, so the marks can be
moved by hand.

### The crowd in the stands

The pack says where they sit and PES supplies the people. Extract its shared crowd
once and every ground can be filled:

    python3 tools/pes21_import/cpk.py <PES>/Data/dt00_x64.cpk audi --filter=bg/common/audi
    bash tools/pes21_import/extract_stadium_packs.sh audi
    python3 tools/pes21_import/stadium_crowd.py <pack> \
        --out data/media/objects/stadiums/pes_st011 \
        --models audi/Asset/model/bg/common/audi \
        --flags <props>/common/demo/prop \
        --fmdl-lib "4cc Blender Starter Pack/scripts/addons/pes-fmdl" \
        --asset-dir pes_st011/crowd

`audi/audiarea.bin` in the pack gives the stands as sloped quads with the row
spacing the game itself uses (1.9 m in st060, 2.1 in st002, 0.7-0.9 in st011);
`dt00_x64.cpk` gives the spectators (`au00`..`au17`, each with a `mouthOpen`
version, plus the cheap `au_Low`) and `chair.fmdl`, the seat. st041 comes to about
13,800 seats, st011 11,900, st060 5,120 - far too much to merge into one mesh, so
each model is written once and its seats beside it as a placement list, and the
renderer draws one mesh many times (`src/utils/instancelist.hpp`). Measured on
st011 with 23,944 copies in the stands: 60 fps, the same as without them. PES's
stand flags (`mob_prop_teamflag_*`) go up one seat in sixty.

### The furniture round the pitch, and the walkout

Neither the packs nor this engine had any of it - there was not even a corner flag
in `media/objects`. It is all in `dt12_g4.cpk`:

    python3 tools/pes21_import/cpk.py <PES>/Data/dt12_g4.cpk props --filter=common/demo/prop
    python3 tools/pes21_import/cpk.py <PES>/Data/dt12_g4.cpk props --filter=fixdemoobj
    bash tools/pes21_import/extract_stadium_packs.sh props
    python3 tools/pes21_import/stadium_props.py props \
        data/media/objects/stadiums/pes_st011/props \
        --fmdl-lib "4cc Blender Starter Pack/scripts/addons/pes-fmdl" \
        --asset-dir pes_st011/props
    python3 tools/pes21_import/stadium_props.py props \
        data/media/objects/stadiums/pes_st011/entrance --name entrance --set entrance \
        --fmdl-lib "4cc Blender Starter Pack/scripts/addons/pes-fmdl" \
        --asset-dir pes_st011/entrance

The first set stays out all match - corner flags, the fourth official's board,
three television cameras outside the perimeter, the barrier at the tunnel mouth,
the paramedics in the technical areas. The second is the walkout's own - the flag
bearers, their banners, the arch over the mouth, the tunnel behind it - which the
engine drops the moment the presentation ends. `--competition` adds the ring of
pennant holders PES sets on the centre circle for a continental tie.

PES's stock-kit humans are deliberately left out: the stewards, the press row and
the television crew ask for clothing textures (`st_shirt2018_non_bsm`,
`gu_generic2018_bsm`, `tv_parka_bib_2018A_bsm`) that are in none of the archives -
checked every `dt` and 4cc cpk - and an undressed figure is a white shape beside
the pitch. The 4cc mod replaces those skins with its own characters anyway, which
is what the packs' own staff import gives you.

### The ground's own lighting

A pack also ships how PES lights it, as readable XML:

    python3 tools/pes21_import/stadium_lighting.py "017 - planet namek" \
        --out data/media/objects/stadiums/pes_st017

light/#Win/light_st<slot>_af_fpkd_extracted/*.fox2.xml gives a place, a date and
a time - Planet Namek is lit as Buenos Aires on 8 April 2019 at noon, with the
ground turned 96 degrees off north, under a 150000 lux sun - which fixes the sun
to the degree. The engine otherwise rolls dice for it at every kickoff, so its
shadows fall a different way in every match. The converter does the astronomy
and writes `lighting.txt` beside the stadium; the engine reads a direction.

### The assets every ground shares

PES keeps one copy of what is the same everywhere in `Asset/model/bg/common`, and
the fork follows the same pattern - these install into the engine's shared media
folders, so every converted stadium gets them:

    python3 tools/pes21_import/import_common_stadium_assets.py \
        /path/to/4cc_15_billboard.cpk --out data --net x_netPat04

That brings in the hoarding faces (into the randomiser's pool), the net
patterns, the pitch detail maps, the crowd's banner art, and the colour grading
tables. The grading is the one that changes the whole picture: PES runs every
frame through a 33-cubed lookup table chosen by time of day and weather, and
without it an imported stadium comes out flat. Measured against the VGL26
broadcast our midtones sat 1.68x low (median 74 against 124) while our
highlights were already hotter and our shadows 13x more crushed - a missing tone
curve, not a missing light. The tables are half-float volumes rather than
pictures, so `lut_strip.py` unrolls the set into one ordinary PNG
(`media/textures/lut/grade.png`: 33 blue slices across, one band per condition
down) and `postprocess.frag` samples it. `graphics_lut_strength` mixes it in;
`0` turns it off.

**Which table a band uses is decided by looking at it, not by its name.** PES ships
sixteen: an `h_` and an `s_` for each condition, each in a version for its own
presentation (`demo`) and one for gameplay (`game`). Eleven of them stop climbing
around 0.69 - `lut_s_day_game` maps grey 0.5 to 0.616 and grey 1.0 to 0.689, so half
its input range lands inside a 0.07 band - and applied as a display transfer that
costs the frame its whole top end: st011 measured p98 0.659 graded against 0.918
ungraded and 0.918 in the reference broadcast, with the spread down to 0.087 from
the reference's 0.132. That is the washed-out, milky look, and it is a contrast
error rather than a level one, which is why keying the exposure never touched it.
The decode was never at fault - the ftex read matches PES bit for bit; the table
selection was.

So `lut_strip.py` reads every candidate, measures what each does to grey
(`grey_response`, `spans_display_range`), and takes the first that reaches white.
Only `lut_h_day_demo` and the four night tables do; `cloudy` and `evening` have
nothing usable of their own and borrow the day table, which the importer prints as
it goes. Distance from the reference's whole ladder afterwards, over all nine converted
grounds: 3.44 graded against 3.49 ungraded, closer on five of the nine. It helps
most where a ground is dark (st002 0.92 -> 0.82, st043 0.74 -> 0.59) and costs a
little where one is already bright (st019 0.15 -> 0.27).

The 1.68x midtone gap quoted above was measured before each ground's own sun was
imported and the exposure keyed; those are where that gap actually was.

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
How the imported art then reaches the screen - the grade, the exposure, the
sun, and the defects that look like art - is [PICTURE.md](PICTURE.md).

PES21_IMPORT.md covers the staging layout and the `EDIT` decryption for real
player names.

## Worked example: importing a /vg/ League team

The league's exports page lists, per team, a tactical export (`.ted`), an
aesthetics export, audio, and sometimes a stadium. `/lcg/` and `/ink/` were
imported from the VGL 26 page as follows.

The tactical export gives the roster:

    python3 tools/pes21_import/ted.py lcg.ted

`.ted` is not a PES save - pesXdecrypter segfaults on one - so `ted.py` handles
it: plaintext header, payload under a repeating 32-byte key that each file
chooses for itself and that is recoverable from the file's own zero padding.
Note that many roster slots read `PLACEHOLDER`; the names that matter are the
ones on the models, which the aesthetics pack supplies.

The aesthetics export is an ordinary AET pack - `Boots/`, `Faces/`,
`Kit Textures/`, `Logo/`, and a note giving the team's colours and sometimes its
id. Whole characters live in `Boots/<kNNNN - Name>/boots.fmdl`, so:

    python3 tools/pes21_import/import_team.py "<pack>" --prefix lcg \
        --fmdl-lib <pes-fmdl dir> --game-dir data --max-edge 0.15

Kits and badge come from the pack itself - `Kit Textures/u0XXXp1.dds` and
`Logo/emblem_0NNN_r.png`. Install them as `<prefix>_kit_0N.png` and
`<prefix>_logo.png` under `data/databases/default/images_teams/<prefix>/`, and
set the team's `kit_url` to `images_teams/<prefix>/<prefix>`. Many packs leave
the id as `XXX` for the organisers to fill in, so do not rely on it; the
filename is a placeholder but the contents are the kit.

Two things to get right when adding the team rows by hand:

* **`profile_xml` must not be empty.** `PlayerData::GetStat` asserts on a
  missing stat, and the match dies during `Team::Team` with
  `Assertion 'exists' failed`.  Copy an existing player's profile and vary it.
* **`philosophy` and `formation`** go in `tactics_xml` as their own tags; with
  neither present a team falls back to balanced and 4-4-2. The per-player stats
  and tactics inside a `.ted` are bit-packed and are not decoded, so anything
  tactical you set is your choice, not theirs.

A community stadium export converts the same way as a Konami one, but pass
`--max-extent` generously: the default 260 m drops distant scenery, which on a
stadium whose setting *is* the distance leaves an empty white backdrop.
