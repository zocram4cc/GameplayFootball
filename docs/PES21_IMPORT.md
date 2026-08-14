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
