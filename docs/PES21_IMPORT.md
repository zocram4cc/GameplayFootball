# PES 2021 bulk import

The goal: bring PES 2021 content — player models, animations (match play,
celebrations, entrances), portraits, billboards, chants, and eventually
stadiums — into GameplayFootball's own formats, so packs of imported content
work on this engine. PES 2021 is abandonware, no longer for sale; the content
sources here are a local install and 4cc aesthetic exports.

Everything lives in `tools/pes21_import/`; converted content is written to
`data/imports/<pack>/` (git-ignored — packs are generated, not source).

**Ground rules:**
- when import fidelity forces a choice between the PES asset and the engine,
  the PES side wins — extend the open-source engine rather than downgrade the
  asset;
- packs hold **simple, editable formats through and through**: PNG, OGG
  Vorbis (the engine has an stb_vorbis loader), plain-text `.anim`, ASE.
  Proprietary formats are tool inputs — and, for gani, a tool *output* too:
  edited `.anim` files re-encode to valid `.gani` (see below).

## The two sides

### GameplayFootball (target)

| Thing | Format | Where |
|---|---|---|
| Player model | 3ds Max ASE, one `GEOMOBJECT` per body part (`pelvis`, `trunk`, `head`, `upperarm_left`…) referenced by an `.object` XML | `data/media/objects/players/` |
| Skeleton | 14 nodes: `player` (root motion) + body, middle, neck, L/R shoulder/elbow/thigh/knee/ankle, bind pose in `player.object` (Z up, faces −Y, 10 ms frames) | `data/media/objects/players/player.object` |
| Animation | plain text: one CSV line per node — `name,frame,qx,qy,qz,qw,…`; the `player` line carries `frame,x,y,z` root positions; optional `extension,football,…` ball keys; XML metadata tail (`<type>`…) | `data/media/animations/**/*.anim`, loader `src/utils/animation.cpp` |
| Billboards | PNG textures | `data/media/textures/adboards/` |
| Audio | 44.1 kHz WAV | `data/media/sounds/` |
| Portraits | none in-engine yet; packs store PNG, menu wiring is future work | `data/imports/<pack>/portraits/` |

### PES 2021 (source) — all formats DECODED

| Thing | Format | Notes |
|---|---|---|
| Containers | CriPak `.cpk` (`@UTF` tables + CRILAYLA) | `cpk.py`, pure Python — verified on `dt13_all.cpk` (416 files) |
| Body animations | `.mtar` motion archives, 16-byte entries keyed by a 48-bit GZ path hash | `mtar.py` — 10 archives, **4,389 animations** |
| Animation | Fox Engine `.gani` v1 (GZ generation): per-file TrackHeader, 15 units / 27 segments for the body rig, quantized-quaternion bitstream + AnimHalf vector curves | `gani.py` — **full curve decoder, 4,389/4,389 decode clean** |
| Rig | `.frig`: unit records + a bone table of `StrCode32` hashes; `body_skel.frig` maps the 27 gani tracks onto 20 bones (`RIG_ROOT`, motion node, `dsk_hip`, `sk_*`) | decoded; the map is baked into `retarget.py` |
| Name hashes | Fox `StrCode` = CityHash **1.0** (`CityHash64WithSeeds(str+'\0', K2, (sbyte)str[0]*0x10000+len)`); pip `cityhash` is CityHash 1.1 and gives wrong values | `strcode.py` — own CityHash 1.0 port, verified: `strcode32("MOTION")` = gani node id `0x08908348`, `strcode32("RIG_ROOT")` = unit-0 hash, all 27 frig bone hashes resolve |
| Player model | Fox Engine `.fmdl` + `.dds` texture set | `fmdl_to_ase.py` via the 4cc `pes-fmdl` parser |
| Face expressions | `face_skel.frig` (59 tracks) + `FHSequence` tables + face `.gani`s | decodable with the same code; **needs an engine-side face rig** (see below) |
| Audio | `.adx`/`.hca` in sound cpks | ffmpeg decodes ADX directly |

## Gani format details (as implemented)

- Positions are IEEE half-floats with the exponent rebased +7 (×128), in
  millimetres: metres = raw/128000. Root motion is split: `RIG_ROOT` carries
  XZ (+yaw quat), the motion node carries Y, bind-relative.
- Frames are ~1/59.94 s; `frame_scale` (byte at TrackHeader+0x10) is a
  per-delta tick multiplier.
- Chain units (legs seg0, arms seg1) hold auxiliary IK vec3 channels, often
  0xFF-filled — skipped by the converter.
- Track→bone order comes from `body_skel.frig` and is constant across all
  body ganis (`retarget.PES_TRACK_MAP`).

## What works today

```sh
# 1. Unpack a cpk (no wine, no .NET)
python3 tools/pes21_import/cpk.py dt13_all.cpk out/

# 2. List / extract a motion archive
python3 tools/pes21_import/mtar.py out/.../body_anime_file0.mtar ganis/

# 3. Decode + validate a .gani (curves, all 27 tracks)
python3 tools/pes21_import/gani.py ganis/anim_xxxxxxxx.gani

# 4. Convert one gani or a whole directory to GF .anim
python3 tools/pes21_import/gani_to_anim.py ganis/anim_x.gani out.anim
python3 tools/pes21_import/gani_to_anim.py --batch ganis/ data/imports/pes21/animations/body_anime_file0/
# verified: 4,389/4,389 converted, 0 failures

# 5. Visual check: render an .anim as a stick-figure filmstrip (GF FK)
python3 tools/pes21_import/anim_preview.py out.anim strip.png

# 5b. Round trip: edit the text .anim, re-encode it as a valid .gani
python3 tools/pes21_import/anim_to_gani.py edited.anim out.gani --template original.gani
# verified: edited clip re-decodes cleanly and keeps the edit (arms-raised POC)

# 5c. Build the whole pack in one shot (portraits/anims/adboards/chants/faces)
python3 tools/pes21_import/build_pes21_pack.py "/path/to/PES21/Data" \
    --faces 100117 --fmdl-lib "<pes-fmdl dir>"
# current pack: 14,672 portraits, 4,389 anims, 336 adboards, 333 chants (ogg)

# 6. Convert a full-body .fmdl into a GameplayFootball ASE
python3 tools/pes21_import/fmdl_to_ase.py model.fmdl out.ase \
    --fmdl-lib "<4cc Blender pack>/scripts/addons/pes-fmdl"

# 7. Package portraits / adboards / chants into a pack
python3 tools/pes21_import/package_assets.py --export "HDG VGL26 AET" --pack data/imports/hdg
```

The retarget (in `gani_to_anim.py`, data in `retarget.py`): decoded PES locals
are FK'd over the PES bind skeleton (world-aligned bind frames), mapped
Fox→GF coordinates `(x,y,z)→(x,−z,y)`, then GF nodes are solved — direct
orientation match for body/middle/neck/ankles, aim-plus-hinge-roll for
thighs/shoulders, pure hinge angles for knees (+X) and elbows (−X).

## Installed content (engine wiring)

Converted animations join the live game via
`tools/pes21_import/install_anims.py`, which copies a pack `.anim` into the
anim collection with the metadata the target class needs. First wired class:
celebrations (`--class happy_normal|happy_extreme|sad_normal` →
`<type>special` + the specialvar pair the controller queries). Installed
files are named `pes_*.anim` and git-ignored. Verified: the full-match
headless smoke loads all installed PES anims into the collection and plays a
match with a goal, no errors.

## Face expressions (decoded; engine rig designed)

- `face_skel.frig` parses with `frig.py` (works for both rigs — magic is
  `strcode32("Face")`/`strcode32("HumanBody")`): 32 units / 59 tracks over
  31 face bones, 29 resolved to `skf_*` muscle names from fmdl bone tables.
- Expressions are **named** loose ganis in dt13
  (`FoxAnim/Face/Animations/{Base,Add}`): 166 base + 15 additive, from
  `smil_soft` and `angr_brwnit_*` to context poses (`base_g_shoot`,
  `base_d_end_cup_warai`). All 181 decode with the standard gani decoder.
- `face_to_anim.py` exports them to an open text format
  (`data/imports/pes21/faces_anim/*.faceanim`): `skf_bone,frame,qx,qy,qz,qw`
  rotation lines and `skf_bone_pos,...` translation lines, metres.
- FHSequence bins (`Facebase.bin`/`Faceadd.bin`) sequence those expressions
  in-game; the expression names are already self-describing, so an engine
  implementation can key off contexts directly and FHSequence decode is
  optional.

**Engine face rig design** (per the fidelity ground rule): GF renders rigid
per-node meshes, so faithful facial animation needs a skinned head — the
plan is a `FaceRig` attached to the `neck` node: (1) load the per-player
face `.ase` plus an `skf_*` weight map exported by `fmdl_to_ase.py`, (2) CPU
skinning of the head mesh only (~2k verts — cheap), (3) a `FaceState`
choosing `.faceanim` poses from match context (shoot/tackle/goal/loss — the
same contexts the PES names encode), nlerp-blended base + additive layers.
Until that lands, imported heads render statically.

## Stadiums, crowds, referees (imported and editable)

- `stadium_to_gf.py`: PES stadium scene fmdl → multi-material ASE + PNG
  textures + `.object`; the engine's `stadium_object` config key selects any
  stadium directory. Verified: the 4CC st060 (31 geoms, 31 textures), st002,
  st011, st019 and st041 (load-probed).
  **Generated ASEs must include MESH_NORMALS** (the loader fatals without) —
  `ase_util.py` provides them for all generators. The engine's text ASE
  parser is slow on big scenes (a 146MB stadium did not finish in 14 min),
  so `--max-tris` caps the budget (60k ≈ 26MB ≈ ~2 min) until the parser is
  optimized — an engine-side TODO. `--max-verts-per-geom` splits an oversized
  fmdl mesh across several GEOMOBJECTs sharing one material, keeping single
  geometries within what the renderer likes. `--textures` takes the ftex
  directory or any parent of it: packs disagree on where sourceimages sit
  (`sourceimages/#windx11` vs `sourceimages/tga/#windx11`), so every
  ftex-holding directory underneath is searched and indexed by texture stem.
  A material whose ftex the pack does not ship (20 of st041's 21) still gets a
  `MAP_DIFFUSE`, pointing at `media/objects/stadiums/white.png`: the ASE
  loader's own fallback is a stock `orange.jpg` that does not ship, and a
  missing image file is fatal to `ImageLoader`.
- `crowd_gen.py`: flat-outline crowds from the stadium's own `audiarea.bin`
  stand quads — silhouette texture + billboard strips hooked into the stadium
  `.object`. audiarea.bin is a set of stand blocks (magic `0x0001xxxx`, its
  own AABB offset, record count, then 96-byte records of row step + scale +
  four corners); row spacing is per stand and per stadium (1.9m in st060,
  2.1 in st002, 0.7-0.9 in st011), so the records are found structurally.
  Coverage: st002 (2 stands), st011 (30), st060 (5); st019 and st041 ship no
  audi fpk at all, so they get no crowd. An empty crowd is never written —
  a zero-vertex geometry used to abort the renderer's buffer upload.
- Referees: the `referee_kit` config key points at any PNG; PES referee kit
  textures (13) live in `data/imports/pes21/referees/`.

## Per-player models (model_url milestone)

The engine's fullbody format IS a skinned mesh: vertex colors pack up to
three bone influences per vertex (channel = jointID·10 + weight·9, /255 in
the ASE; joint IDs = `player.object` DFS order). `fmdl_to_fullbody.py`
converts a skinned PES player fmdl into that format with real PES weights.
Assignment is the editable `media/players/playermodels.cfg`
(`<databaseID> <directory>` lines); `Team::InitPlayers` loads the override
fullbody + its own vertex-color map per player.

## Animation names

mtar entry hashes are `StrCode32` of the bare animation name — and the
dictionary is **complete: 4,389/4,389 resolve** (`anim_names.txt`, mined by
`mine_anim_names.py`). The canonical source is
`common/anime/Mbinfo/json/anim_infos.json` (plain JSON, `file_name` fields);
`PES2021.exe` strings cover the remainder. Names are self-classifying:
`dm_goal_*` goal celebrations (archer, ivoryCoastDance, Head_sliding),
`dm_miss_*` dejection, `dm_oop_*` cutscene one-offs, `assistant_*` linesman
flags, plus the full grammar
`<action>_<startSpeed>_<endSpeed>_<height>_<angle>_<modifiers>`. The pack's
converted anims carry these real names, and the installed celebration set
uses genuine `dm_goal_*` clips.

## Cutscene camerawork (TRACED — the earlier "no data" verdict was wrong)

PES 2021 ships **1,274,880 frames (~11h48m at 30fps) of hand-authored
camera animation** in `dt12_g4.cpk` under
`common/demo/fixdemo/<category>/cut_data/*.fdc` (4,804 files; goal, ent,
mode, end, change, timeup, pk, result, foul categories). The `.canm`
camera streams are embedded inside the `.fdc` containers (never loose,
which is why early scans missed them): raw float32 quaternion, position,
vertical FOV, near, far, aspect — metres, Y-up, origin at the centre spot,
triple-confirmed (penalty cameras at exactly x=52.5, Maya-exact FOVs,
unit-norm quats). `tools/pes21_import/camera_cut.py` parses all 4,804
files with zero errors; `docs/PES21_CAMERA_TRACE.md` documents the
formats and the GF mapping (one +90° X basis rotation; FOV copies
unchanged; clamp near to ≥0.1). Next milestone: play imported .canm
tracks in the engine's cutscene camera.

## Menu / in-match UI art (REVERSED — dt11_x64.cpk)

The Flash-derived UI stores its art in `common/menu/**/*.bin`. Chain of
containers, all decoded by `tools/pes21_import/txp2.py`:

1. **WESYS wrapper** (same as constants: `wesys_constant.unwrap`) — tag
   bytes + "WESYS", u32 compressed/uncompressed, zlib.
2. **Named archive** (same `.o` layout as constants) holding one
   `afp_<name>.apk` payload.
3. **TXP2 texture package** (big-endian): texture count/table at
   0x18/0x1c (12-byte entries `{name_off, size, data_off}`), sprite region
   count/table at 0x24/0x28 (10-byte entries `{texture_no, l, t, r, b}`,
   exported as `regions.json`).
4. Each texture blob: u32 uncompressed + u32 compressed + **Okumura LZSS**
   (0x1000 zero-primed window, write pos 0xFEE, LSB-first flags, set
   bit = literal) around a **TXDT** payload: u16 width/height at 0x10,
   pixels from 0x40 — fmt 0x15 = raw RGBA8888, fmt 0x1a = 8bpp
   alpha/gray (fonts, masks).

`python3 txp2.py --batch <dt11_dir> <out>` mirrors the tree and emits
PNGs. Key packages: `general/game2d.bin` (in-match HUD), `general/
gamePlan*.bin` (tactics screens), `licence/game2d*.bin` (competition
scoreboard skins), `general/topModeSelect*Bg_*` (menu backgrounds).
Fonts (`font/*_fnt.bin`) are a separate `WFNT` format — see the next
section.

## Fonts (WFNT — REVERSED)

The menu/match fonts (`common/menu/font/*_fnt.bin` in dt11) are
pre-rasterized bitmap fonts, decoded end-to-end by
`tools/pes21_import/wfnt.py`. Same outer containers as everything else:
WESYS wrapper + named `.o` archive; each archived payload is one `WFNT`
face. Most bins hold a single face; `edit_fnt.bin` holds ten
(`e00.obj`..`e09.obj`, the edit-mode lettering styles). No TXP2/TXDT/LZSS
involved — glyph bitmaps are stored raw.

WFNT payload (little-endian):

```
+0x00  "WFNT", u32 version = 1
+0x08  u8  bpp          4 = paletted alpha, 32 = raw RGBA8888 (ext only)
+0x09  u8  line_height  == ascent + descent in every shipped font
+0x0a  u8  ascent       top of line -> baseline
+0x0b  u8  descent
+0x0c  u32 glyph_count
+0x10  u32 palette_offset  (0x20; == table_offset when bpp == 32)
+0x14  u32 table_offset
+0x18  u32[2] zero (unk_18/unk_1c — zero in all 23 shipped faces)
```

bpp 4 palette: 16 RGBA8 entries; every shipped font uses white with a
linear alpha ramp (`FF FF FF 00/11/../FF`), i.e. 4-bit antialiased
coverage. `ext_fnt` (bpp 32, U+E000..U+E0F6 private-use area) is the
color icon face — controller buttons, arrows, HUD pictograms — with no
palette and straight RGBA8888 pixels.

Glyph table: `glyph_count` × 16 bytes, sorted by charcode:

```
+0x00  u32 charcode   the character's UTF-8 bytes packed into a u32
                      (0x41 'A', 0xefbfa5 U+FFE5 fullwidth yen)
+0x04  u8  cell_w     power-of-two cell >= bitmap (GPU glyph-cache cell;
+0x05  u8  cell_h      exact rounding rule not pinned down — only unknown)
+0x06  u8  width      bitmap size in pixels
+0x07  u8  height
+0x08  u8  bearing_x  pen -> left edge
+0x09  u8  bearing_y  baseline -> top edge (FreeType horiBearingY;
                      digits overshoot the baseline by 1px as expected)
+0x0a  u8  advance
+0x0b  u8  zero
+0x0c  u32 pixel data offset from start of WFNT
```

Pixels: rows top-down; bpp 4 packs two pixels per byte, high nibble
first, stride `ceil(w/2)`; bpp 32 is `w*4` bytes per row. Each glyph
blob is 16-byte aligned; blobs are consecutive and the last ends exactly
at the payload end (verified on all 23 faces).

```
python3 wfnt.py list    <font_fnt.bin>...
python3 wfnt.py extract <font_fnt.bin>... -o <out>
```

`extract` shelf-packs each face into `<out>/<font>/[<face>/]atlasN.png`
(white-on-transparent RGBA, max 4096²) + `glyphs.json` (per glyph: char,
codepoint, atlas page + rect, bearings, advance, cell dims; plus face
line metrics and palette). Faces: `match`/`matchBold` (in-match HUD),
`numMatch`/`numMid`/`numSml`/`numCard`/`numXl`/`numExt` (scoreboard/
clock digit strips, `+ - 0-9 %`), `pes` (psm, base menu face), `LPsm`/
`XLPsm` (large menu), `plname` (player-name face, 8,030 glyphs), `ext`
(icons), `edit` (10 styles). The JP faces carry the full charset:
14,734 glyphs covering ASCII, kana, JIS kanji, cyrillic, latin extended.

## Open problems, in priority order

1. **Animation naming/classification.** mtar entries are keyed by 48-bit GZ
   path hashes (`strcode.gz_hash` — full `/Assets/...` path, extension
   dropped). A dictionary of PES anime paths (or `AnimeTable/bin/*.bin`)
   would separate celebrations / entrances / play and give real names;
   until then output is `anim_<hash>.anim` grouped per source mtar.
2. **Engine-side hookup.** Converted anims need `<type>`/touch metadata to
   join the anim collection (celebrations are the easiest entry point);
   models need `PlayerData.model_url` (`Team::FetchKit` /
   `ObjectLoader::LoadObject` are the seams).
3. **Face rig in the engine** (fidelity rule: engine change, not asset
   drop): add face bone nodes + a `face.anim` channel so `face_skel.frig` +
   FHSequence expressions can drive imported heads.
4. **Richer body skeleton**: PES animates clavicles, hands, spine chain
   (20 bones vs GF's 14 nodes). Candidate engine extension to stop
   collapsing those into the coarse rig.
5. **Stadiums**: multi-`.fmdl` scenes; fmdl→ase applies, scene assembly is
   its own project. Billboards/chants already have a home.

## Pack layout

```
data/imports/<pack>/
  animations/<source-mtar>/<anim>.anim
  models/<player>.ase
  portraits/<player>.png
  adboards/<name>.png
  chants/<name>.ogg
```
