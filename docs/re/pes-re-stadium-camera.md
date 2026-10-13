# PES 2021 — stadium, pitch, crowd and the in-play broadcast camera (RE report)

Scope: how PES 2021 assembles a stadium (scene models, pitch, goals and nets, corner flags, ad boards, lighting and time of day), the pitch dimensions and world units, how the crowd is built, and how the in-play camera is parameterised and moves. Cutscene and replay camera *data* (`.fdc`/`.canm`) is out of scope; it is covered by GF `docs/PES21_CAMERA_TRACE.md` and the CutsceneRE report.

Evidence tags: **OBS** = observed, with the method given. **INF** = inferred, with confidence. VAs are those of the live image (base 0x140000000, `.text`/`.rdata`/`.data` dumped to `pesmem.bin`). Scripts and decompiles are in `<scratch>/stadcam/`:

| Script / file | What it does |
|---|---|
| `fox2.py` | Fox2 binary (`box5`) parser: entities, properties, string table. Property names come from StrCode64 matched against every identifier string in the exe |
| `ncpk.py` | lists or extracts members of a cpk nested inside another cpk without extracting the outer one |
| `fbox.py` | FMDL bounding-box reader (FMDL section 0 type 13) |
| `pdis.py` | capstone disassembly of live or dumped memory with string, float and RTTI annotation |
| `dumpf.py` | live object dumper (floats, pointers, RTTI) |
| `samp.py`, `samp3.py` | live `fox::gr::Camera` sampling |
| `scanvt.py`, `scan2.py` | heap scans for vtables and stadium strings |
| `tbl.py` | dumps the stadium-camera table |
| `annot.py` | annotates `_DAT_` constants in Ghidra output |
| `d2a.txt`, `d3a.txt` | annotated headless decompiles of `FUN_1408a1a70`, `FUN_1408a57f0` and `FUN_1408a2180` |

Process rule kept: `/proc/<pid>/mem` was only read; nothing was written and no input was sent.

---

## 0. Game state observed (live, pid <pid>)

- **20:18–20:25.** A match was loaded. Heap counts:
  - `InplayCamera@plugin@camera@match` ×1;
  - `StadiumImpl@scene@rc@cobra` ×8, `StadiumModel@rd@cobra` ×82;
  - `AudienceImpl` ×1, `AudienceArea` ×1, `AudienceModel` ×5;
  - `fox::gr::Crowd` ×4, `fox::gr::CrowdModel` ×209;
  - `GoalnetImpl` ×2, `PitchImpl` ×2, `CameraInplay@constant` ×1, `CameraSetplay@constant` ×1.

  At 20:25:40 the main camera was flying a low cutscene or replay shot: height 2 m, moving, vFOV 37.85° then 44.96°. **OBS** via `samp.py`.
- **20:27:14–20:27:36.** The main camera sat static on the in-play pose (paused or dead ball). **OBS**.
- **20:27–20:30.** A heap rescan found no `InplayCamera` or `StadiumImpl` and 2 `UniformUnit`s: a menu or preview screen. The heap still held 159 copies of `/Assets/pes16/model/bg/st071/sourceimages/tga/` plus st071 scene and effect paths, so the last stadium loaded was **st071** (INF, high). **OBS** via `scan2.py`.
- **20:39.** In play again. The camera tracked play for 6 s (§4.6).

---

## 1. Stadium files

### 1.1 Mount topology — the stadium content is in nested cpks (important for `PesVfs`)

- **OBS.** `Data/dt30_g4.cpk` holds 56 entries directly under `Asset/model/bg/`. They are **cpk files**, not folders:
  - `Asset/model/bg/common.cpk` (167.8 MB) and `common_ext.cpk`;
  - `Asset/model/bg/stNNN.cpk` (about 120–235 MB each) for every vanilla stadium.

  Their inner TOCs list the real files: `Asset/model/bg/stNNN/#Win/stNNN.fpk`, `pitch/`, `light/`, `audi/`, etc. (`ncpk.py`).
- **OBS.** The GodotFootball `PesVfs` indexes only outer cpk TOCs, so `pesfmt_dump ls` shows `Asset/model/bg/st002.cpk` as a single file. Everything inside the nested cpks is **invisible to the current VFS**. The only stadium files the VFS can see are loose download overrides from `4cc_30_stadiums.cpk` and `4cc_32_stadiums2.cpk`.
- **OBS.** `common.cpk` contains:
  - `goal/#Win/goal_post.fpk`, `goalnet_{l,r}[_hang3pt|_hang4pt|_hang5pt].fpk`, `goalnet_hangpole_{l,r}.fpk`;
  - `cornerflag/#Win/cornerflag.fpk`, `standsFlag/#Win/standsFlag.fpk`, `turf3d/#Win/turf3dtexlodu4.fmdl` (grass fins);
  - pitch textures `common/pitch/sourceimages/#windx11/{line_alp,line_nrm,line_srm,line_mtl,turf_bsm_alp_rgba32,turf_nrm,turf_srm_alp,grass_slopetable_alp}.ftex`;
  - `common/lut/` (32), `common/ad/` (561), staff, cheer textures;
  - `common/bg/model/bg/{bill/*,draw_parameter/stNNN/*,tv/stNNN/*}` xml.
- **OBS.** The crowd models and textures are **not** in a nested cpk. They are plain entries of `dt00_x64.cpk` (`Asset/model/bg/common/audi/...`).
- **INF (high).** The game mounts the stadium's nested cpk when it pre-reads a stadium (RTTI `PreReadStadium@sys`, `NonCopyable<PreReadStadium>`). Loose download files still win over nested content; the 4cc packs depend on that, because they ship only loose `Asset/model/bg/stNNN/...` paths. Two things are **not** observed:
  - the exact precedence between a nested stadium cpk and download cpks;
  - whether an empty override (see below) suppresses a nested original.

  `/proc/pid/fd` shows only outer cpks open (dt30 is open), which is consistent with reading the nested archive through the outer handle.
- **OBS.** **48-byte fpk/fpkd = an empty override.** Many 4cc files are 48 B: a header with zero entries. They replace a vanilla package with nothing; for example `cheer_st071_h_a1.fpk`, `scarecrow_st071.fpk`, `tv_st071.fpk`.

**Reimplementation consequence.**
1. `PesVfs` must index nested cpks: TOC of inner cpk = file at `outer offset + inner FileOffset + inner base`; the inner entries are uncompressed in this install.
2. Lay them under the download layer, so loose overrides win.
3. Treat 48-byte fpk/fpkd as "package intentionally empty".

### 1.2 Stadium id → files

- **OBS** `common/etc/pesdb/Stadium.bin` (21,560 B) is 55 records of **392 B**:

  | Offset | Field |
  |---|---|
  | +0 | u32, same value in all records |
  | +4 | u16 stadium id |
  | +6 | u16 0x001f |
  | +8 | name, UTF-8, 181 B |
  | +189 | second name, UTF-8, 181 B |
  | +370 | folder string, `stNNN`, 22 B |

  The ids are 2, 5, 6, 7, 10, 11, 13–24, 27–29, 31, 33–36, 40–47, 49, 50, 52, 54–56, 59–66, 70, 71, 74, 80, 82–84. Folder = `st%03d` of the id in every record.
- **OBS** `common/etc/data_st_list.bin` (220 B) = 55 × {u16 id, u16 0x1001} with the same ids.
- **OBS.** Per-stadium VFS paths, all under `Asset/model/bg/stNNN/`:

  | Package | Contents (OBS from vanilla st002 and 4cc st071 unless noted) |
  |---|---|
  | `#Win/stNNN.fpk` + `.fpkd` | Scene fmdls `scenes/center1.fmdl`, `front1..3`, `back1..3`, `left1..3`, `right1..3` (+`_detail`, `_probe`, `_rain` variants in vanilla). The fpkd is `stNNN.fox2` with `StadiumModel` / `EnlightenStadiumModel` entities (one per fmdl), `EnlightenSystemContainer`, `EnlightenProbeSet`, `StadiumAnime`, `StadiumTexture`, `LimitedRotatableObjectLinks`. 4cc st071 ships only `center1` + `front3` with plain `StadiumModel`s |
  | `#Win/stNNN_{df,dr,nf,nr}.fpk/.fpkd` | Weather/time variant overlays (`left3_df.fmdl`, …). **INF (high):** d/n = day/night, f/r = fine/rain. LUT names `lut_[hs]_{day,evening,night,cloudy}_{demo,game}` use the same vocabulary |
  | `pitch/#Win/pitch_stNNN.fpk/.fpkd` | One pitch fmdl (`EnlightenStadiumModel` or `StadiumModel`). Vanilla `pitch_st002.fmdl` materials: `line_alp_dds1` (`fox_3ddc_ggx`; common line textures) and `pitch_mat` (`pes_3ddf_pitch2020`; stadium `pitch_alp` + common `turf_nrm`, `turf_srm_dummy`, `turf_bsm_alp_rgba32`). Pitch mesh AABB x ±58.41, z −39.42…+37.05, y 0…0.008 (`fbox.py`) |
  | `light/#Win/light_stNNN_{af,ar}.fpk/.fpkd` | Light rig. INF: a = all times of day, f/r = fine/rain. The 4cc st071 `af` fox2 is internally `/Assets/pes16/model/bg/st009/light/light_st009_af.fox2` (4cc reuses a vanilla rig). Entities: `TimeOfDay` (9 `TimeSpan`s 1200, 1300, 1400, 1500_day, 1600, 1700, 1800_night_s, 1900_night_w, …), 10 `TimeOfDaySettings` (dateTimeHour/Minute, sunLightMode), 123 `SpotLight`, 24 `PointLight`, 41 `Locator`, 9 `ReflectionProbe` (+Set), 7 `LightOccludeCube`, 3 `ColorGrading`, `TppSky`, `TppAtmosphere` (→ `.atsh`/`.pcsp`), `GlobalLightArray`, `GrShadowSettings`, `PesPluginSettings` |
  | `audi/#Win/audiarea_stNNN.fpk/.fpkd` | `audiarea.bin` + `AudienceAreaData{areaFile}` (§3) |
  | `standsFlag/#Win/flagarea_stNNN.*` | `AudienceAreaData` with `flagarea.bin` and stand-object spawn parameters (`standsObjectNumHA1/2`, variation counts, height min/max 0.55/1.55, model occurrence rates, scale and angle ranges) |
  | `big_flag/#Win/home_big_flag_stNNN.*` | `StadiumModel_bigFlag` (`home_big_flag.fmdl`) |
  | `cheer/#Win/cheer_stNNN_h_a{1,2}.*` | Home cheer / tifo packages; textures in `common/cheer/` (179 in 4cc_30) |
  | `staff/#Win/staff_stNNN[_cl].*` | Touchline people (`MobCharacterModel`), cameramen, crane camera with `crane_cam00.gani` |
  | `scarecrow/#Win/scarecrow_stNNN[_cl].*` | Static stand people (vanilla st002: 181 kB fox2) |
  | `effect/#Win/effect_stNNN_{df,dr,nf,nr}.fpk` | Flash and star locators + `effect_config.xml` |
  | `tv/#Win/tv_stNNN.*`, `tv/tv_template_stNNN_basic.json` | Big screen; `common/bg/model/bg/tv/stNNN/tv_condition.xml` picks the template |
  | `sourceimages/tga/#windx11/*.ftex` | Stadium textures (55 in 4cc st071) |
  | `cornerflag/#Win/cornerflag_NNNNN.fpk` | Per-stadium corner flag override (vanilla st002 has `cornerflag_00108`) |

  Shared draw config: `common/bg/model/bg/draw_parameter/stNNN/draw_parameter_{df,dr,nf,nr}.xml` (`<tv_model_intensity box line>`).
- **OBS.** `StadiumModel` fox2 fields used for assembly: `modelFile` (FilePtr), `transform` → `TransformEntity{transform_scale, transform_rotation_quat, transform_translation}`, `flags`, `color`, `drawMode`, `direction`, `kind`, `lodFarSize`, `lodNearSize`, `lodPolygonSize`, `drawRejectionLevel`, `rejectFarRangeShadowCast`, `demoGroup`, `customBits`.
  - `direction` values seen: 0 for `_F*`, 1 `_B*`, 2 `_L*`, 3 `_R*`, 4 `_C*` (INF, high, from entity names).
  - `kind` values seen: 0 for center/pitch/bigFlag, 1 for the weather overlays, 2 for F3, 4 for billboards, 17 for vanilla scene, pitch and goal models.

  Every transform in the scene, pitch and goal packages is identity or a 180° Y rotation. Models are authored in world space.
- **OBS** `common/match/constant/constant_stadium.bin` (WESYS-zlib constant archive, GF `wesys_constant.py`) has 84 objects of 432 B:
  - `demoarea_stNNN.o` per stadium;
  - `demoarea_bill_*.o`;
  - `studiumPositionData.o`.

  They are self-relative offset trees; their semantics were not decoded.

### 1.3 Goals, nets, corner flags

- **OBS** `goal_post.fox2` (common.cpk) has 4 `StadiumModel`s:
  - `goal.fmdl` with identity transform and with quaternion (0,1,0,0) (180° about Y);
  - `shadow.fmdl` the same two ways.

  So one goal mesh is authored at −X and mirrored for +X.
  - `goal.fmdl` AABB: x −54.584…−52.44, y 0…2.56, z ±3.78.
  - Material `iron_b`, shader `fox_3ddf_basic_nrmuv`, textures `pes17_goalpost_{bsm,nrm,srm}`.
- **OBS.** Net: `goalnet2L.fox2` → `GoalnetData{modelFile=goal2L_net.fmdl, clothFile=goalnet2L.cloth}`.
  - Material `fabric_mid_glossy`, shader `pes_3dfw_goalnet`, textures `goalnet_{bsm_alp,nrm,srm}`.
  - Net AABB x −55.456…−52.56, y 0…2.883, z ±4.332.
  - Variants `_hang3pt`, `_hang4pt`, `_hang5pt` and `hangpole` packages exist.
  - Runtime classes: `GoalnetImpl@scene@rc@cobra`, `GoalnetModel@gr@fox`, `GoalnetRendering(Dx11)`, `GoalnetClothPoint`, `…Link`, `…TriList` — a cloth-simulated net.
  - `.rdata` holds the net visibility AABBs: 52.44…55.5 × 0…2.903 × ±4.354, and the pitch box ±52.56 × 0…50 × ±35.5 at `0x142abdb00`.
- **OBS.** Corner flags: `cf_common_pes2019A.fox2` places `cf_common_2019E_vatB.fmdl` at (−52.487, 0, 33.994), (52.474, 0, 34.003), (−52.492, 0, −33.994), (52.474, 0, −33.988). Skeleton `c_flag.skl`; the flag animation is vertex animation (`common/vertex_anim/va_flag001`).

### 1.4 Ad boards

- **OBS.** Selection: `common/bg/model/bg/bill/config/config.xml` holds `<condition stid compeid hometeamid teamexclusion language region title pattern>` rows, with −1 meaning "any". The first matching row picks a `pattern`.
  - The 4cc file has a neutral-stadium row, one row per home team, competition rows and a default.
  - The pattern's `bill/<pattern>/<pattern>_{df,dr,nf,nr}.xml` points to `billBody/bill_type_NN/bill_df.xml`, a `bill_texlist.xml` and a `bill_anime.json`.
- **OBS.** Content: `Asset/model/bg/common/bill/<pattern>/#Win/<pattern>.fpk` + `.fpkd` and `sourceimages/#windx11/*.ftex`. For the 4cc pattern:
  - 20 `BillModel` entities: 16 board fmdls `4cc_bill_{back,left,right}NN_MM.fmdl` and 3 `*_ShadowModel.fmdl`. All identity transforms (world-authored). Fields `id` and `isMovie`.
  - 1 `BillAnimeData{animeFile=bill_anime.json, texPaths[149], models[16]}`: the rotating LED texture sequence.
  - Movie boards use `bill_myclub.usm` style `.usm` files.
- `4cc_15_billboard.cpk` supplies `Asset/model/bg/common/bill/*` (1,555 entries); `4cc_13_seasonal` adds `common/ad/`. **OBS** by cpk listing.

### 1.5 Which stadium the 4cc install uses

- **OBS.** The 4cc download layer overrides most slots: `4cc_30_stadiums.cpk` touches st001–st084 (63 slots), and `4cc_32_stadiums2.cpk` re-overrides st002, st011, st019, st041, st060 and st065. The stadium is per match: home team or competition. The ad-board config keys on stadium id, competition id and home team.
- **OBS (live).** The most recent match loaded **st071**. Its 4cc package is complete:
  - `center1.fmdl` (12.2 MB) + `front3.fmdl`;
  - pitch `st071_pitch.fmdl`;
  - light rig borrowed from st009;
  - an `audiarea.bin` with 2 stands;
  - empty cheer, scarecrow, standsFlag and tv.
- **Not parsed:** `Team.bin`'s home-stadium field. The team → stadium mapping is therefore open (§6).

---

## 2. Pitch dimensions and units

**World unit = 1 metre.** The pitch long axis is **X** (goals at ±X), the width is **Z**, and Y is up. This is the same space the renderer, the camera and the gameplay registry use.

- **OBS (files).**
  - Corner flags at |x| ≈ 52.47–52.49 and |z| ≈ 33.99–34.00.
  - Goal frame: posts at z ±3.66 inner / ±3.78 outer, so posts and crossbar are 0.12 m. Crossbar top 2.56 = 2.44 + 0.12. Frame face at x = −52.44, i.e. the post centre is on the 52.5 line.
- **OBS (live gameplay registry).** `registry::FieldInfo` (via `FieldInfoRef`, data at heap `0x1f69e450`) holds f32s from +4:

  | Offset | Value | Meaning (INF) |
  |---|---|---|
  | +0x04 | 52.56 | half-length to the **outer edge** of the goal line (52.5 + half of the 0.12 line) — OBS value |
  | +0x08 | 34.06 | half-width to the outer edge of the touchline — OBS value |
  | +0x0C | 3.66 | goal inner half-width |
  | +0x10 | 2.44 | goal height |
  | +0x14 | 47.06 | goal-area line x (= 52.56 − 5.5) |
  | +0x18 | 9.16 | goal-area half-width (= 3.66 + 5.5) |
  | +0x1C | 36.14 | penalty-area line x (52.56 − 16.42) |
  | +0x20 | 20.12 | penalty-area half-width |
  | +0x24 | 41.56 | penalty spot x (= 52.56 − 11) |
  | +0x28 | 2.5 | ? |
  | +0x2C | 1.109 | ? |
  | +0x30 | 2.34 | ? |
  | +0x34 | 51.17 | ? |
  | +0x38 | 54.32 | ? (goal back / net region, INF) |
  | +0x3C | 54.43 | ? (goal back / net region, INF) |
  | +0x40 | 0.12 | line width or post width |
  | +0x44 | 0.12 | line width or post width |
  | +0x48 | 9.15 | centre-circle and penalty-arc radius |

  Only 52.56 and 9.15/9.16 occur as literal constants in the image; the rest are computed at runtime (byte search, `<scratch>/stadcam`).
- **Against FIFA** (105 × 68, goal 7.32 × 2.44, goal area 5.5, penalty area 16.5 × 40.32, spot 11, circle 9.15, lines ≤ 0.12):
  - length, width, goal, goal area, spot and circle match exactly, measured to the line outer edges;
  - the PES penalty area is 16.42 deep (−8 cm) and 40.24 wide (−8 cm). **OBS** numbers; **INF** that this is a line-edge convention, not a data error.
- **Live BallInfo** (`registry::BallInfo` at `0xa3e8b00`): +4 ≈ (−28.24, 1.091, 30.27) — plausible metres. The in-play camera target has y = 0.90 m and moves in metres (§4).
- **Cross-check with `PES_POS_TO_M_GAMEPLAY = 1/20480`.** That constant only scales **gani root-motion curve values** (GF `retarget.py:80-90`: it was *fitted* by `calibrate_pos_scale.py`, not read from PES). It has no counterpart in world space. World, gameplay and camera coordinates are plain metres, with no scale between render and gameplay. Nothing here confirms or refutes 1/20480 itself; that needs the gani decoder's dequantisation in the exe (open, §6).
- **FieldInfo builder hunt (orchestrator, 2026-10-13, full image `PESLive`).** No code in `.trace` or `.impdata` holds 52.56, 34.06, 36.14, 20.12, 41.56 or 47.06 as a float immediate or rip-relative literal (the only 52.56 is the pitch AABB data at 0x142abdb20, unreferenced by float loads), so the record is computed from other values at runtime. RTTI finds only the reference/pointer wrappers (`match::registry::FieldInfoRef` vtable 0x142608810, `ScopedRead` 0x142bbb6f0), not a writer class. Next step: find the writer through `ScopedWrite<FieldInfo>` (search `cobra::registry` write scopes for the FieldInfo type id) or a live write watch while a match loads. Until then GodotFootball derives 52.56 and 9.16 exactly from its own line constants, and the PES box values (36.14 / 20.12, 8 cm and 3 cm inside FIFA's) remain an open difference.
- **GF difference:** GF's pitch is 110 × 72 (`pitchHalfW = 55`, `pitchHalfH = 36`). The reimplementation must use PES's FieldInfo values, not GF's.

---

## 3. Crowd

- **OBS (files, dt00_x64.cpk, `Asset/model/bg/common/audi/#Win/`).**

  | Package | Contents |
  |---|---|
  | `audi_model.fpk` | `au00.skl` + 14 spectator fmdls (`au00_mouthOpen`, `au02`, `au03_mouthOpen`, `au05_mouthOpen`, `au06_mouthOpen`, `au08`, `au09_mouthOpen`, `au11_mouthOpen`, `au12`…`au17`, ~115–142 kB each). Fpkd: `AudienceModel{models[14], sklFile=au00.skl}` |
  | `audiParts_model.fpk` | the same 14 as `_parts` (accessories) |
  | `audiLow_model.fpk` | `au_Low.fmdl` (low LOD), `au_sky.fmdl` |
  | `audiLowParts_model.fpk` | `au_Low_parts.fmdl` |
  | `audi_seat_model.fpk` / `seatOnly_model.fpk` | `chair.fmdl` |

  Textures: `au_h_{face_bsm,face_bsm_alp,nrm,srm,parts_bsm,col_bsm_rgba32,mask_nomip_rgba32}`, `au_l_{col_bsm,mask_{s,w,ns,nw}_alp_nomip,nrm,srm}`, `o_phonelight_alp`, `o_strobo_alp`.
- **OBS (placement).** Per stadium, `audi/audiarea.bin` (referenced by `AudienceAreaData.areaFile`):
  - u32 version = 1;
  - a header slot table of u32 offsets to stand blocks; st071 has slots at +0x14 = 0x38 and +0x20 = 0x11c;
  - each block: u16 block-id pair, u32 AABB offset (= block + 8), 6 f32 AABB, u32 record count, pad;
  - then records of **96 B**: f32 row step (1.0 in st071; 2.1 in st002 per GF), 2 f32 scale (0.86, 0.86), 12 f32 = 4 corners of a sloped stand quad, padding, trailing u32.

  st071 has 2 blocks of 2 records each; its stands sit behind the goal lines at |x| ≈ 79–87 m. The layout matches GF `crowd_gen.py:28-60` and was re-verified on st071 by hex.
- **OBS (runtime classes, RTTI).** The crowd is **GPU-instanced skinned meshes with an impostor LOD**, not cards:
  - `fox::gr::Crowd`, `CrowdModel`, `PluginCrowd`, `PluginCrowdDeferred`, `PluginCrowdDepth`, `PluginCrowdImpostor`, `PluginCrowdGenerateMips`, `CrowdRendering(Dx11)`;
  - jobs `CrowdSetupJob`, `CrowdVisibilityTestJob`, `CrowdInstanceUpdateJob`, `CrowdInstanceAnimeBufferUpdateJob`, `BuildCrowdCommandsJob`, `BuildCrowdImpostorCommandsJob`;
  - packets `CrowdModelMacroPacketParametersPacket`, `CrowdBillboardMacroPacketParametersPacket`;
  - `CrowdRenderingSettings(Parser)` with `HashMap<StringId, CrowdRenderingSettingDetail>`.

  Colours and textures are composited per match: `AudienceColorTextureCompositor`, `AudienceTextureCompositor`, `AudienceColorTextureMergeJob`, `AudienceTextureMergeJob`. Config classes: `AudienceConfig{AudienceAreaElem, AnimationElem, TextureElem, SkeletonElem, PreRenderData, Transition}`.

  Live during the match: `AudienceModel` ×5, `Crowd` ×4, `CrowdModel` ×209, `AudienceArea` ×1 (heap scan, `scanvt.py`).
- **OBS (animation).** `common/mob/audience_BB/`:
  - `motion/*.gani` (≈190 clips: sit/stand idle, clap, yale, angry, frustrate, goal, balloon, flag, smartPhone, enter_SA…);
  - `motion/audi_motion.bin` (3.8 MB);
  - `animeSet/audi_animeset.bin`, `transitionTable/audi_transition.bin`;
  - `skeleton/mob_high_skeletons.hkx`.

  `common/mob/control/mob_anime_control.xml` maps match sound events (`SE_H_GL_01`, `SE_A_EXCITE_LV3_01`, …) to crowd states per group: `Home`, `Away`, `SouthHome`, `SouthAway`, `CoachHome`, `CoachAway`, `StandsFlagHome`, `StandsFlagAway` (states such as `idole`, `get_goal_level3`, `chance_level3`, `return`). Stand flags: `common/mob/prop/standsFlag/*` + `common.cpk standsFlag.fpk` + per-stadium `flagarea.bin`.
- **Not decoded:** how many instances per stand row, instance scale and yaw, the impostor switch distance (`CrowdRenderingSettings`), and `audi_motion.bin`.

---

## 4. In-play broadcast camera

### 4.1 Classes (RTTI, `pesmem.bin`)

| Group | Classes |
|---|---|
| In-play plugins `match::camera::plugin::*` | `InplayCamera` (vt `0x142609c28`), `VerticalCamera` (`0x142609f10`), `KickoffCamera` (`0x14260a428`), `FreeKickCamera`, `CornerKickCamera`, `GoalKickCamera`, `PenaltyKickCamera`, `OutOfPlayDemoCamera` (`0x142609a08`), `FreeTrainingBallSetCamera` |
| Demo / replay | `match::CameraPlay` (`0x142bd3d58`), `CameraUp`, `CameraLow`, `CameraWalk`, `CameraShoot`, `CameraOffside`, `CameraReplay`, `ReplayCameraListener`; `match::camera::demo::{DmCamera, DmCamView, DmCamTarget, DmCamShake, DmFixCam, DmCamAnime, DcAverage, DcRangeAverage, DcPendulumVibration, DcCurve1dBezier, dcNoise, ktCameraAnimation}`; `FixDemoCamera`, `FixDemoCameraData` |
| Constants | `constant::CameraInplay` (vt `0x142b5d4b8`), `constant::CameraSetplay` (vt `0x142b501b8`) |
| Registry | `registry::CameraSettings` (Copy/ScopedRead/ScopedWAndC), `CameraInfo`, `CameraTargetInfo`, `FieldInfo`, `BallInfo` |
| Engine | `fox::gr::Camera` (vt `0x142a003e0`), `cobra::rc::camera::CameraImpl`, `cobra::rd::CameraSystem`, `fox::grx::GlobalCameraSettings`, `cobra::rd::SpecificCameraParameter` (`cameraNames`) |
| Menu | `menu::OptionCamera`, `MatchPauseCameraSetting(Dialog/Bg)` |

- **OBS.** The English option names, in order from `eng/string/all.str`: Normal, Long, Vertical, Wide, Zoom, Tailing Camera (Player / Ball), Blimp, Pitch Side, Mid Range, Live Broadcast, Fan View, Dynamic Wide, Dynamic Wide Custom, Stadium, Stadium (Custom).
- **OBS.** Exe enums: `PLAY_DATA_CAMERA_{MIDDLE, FAR, WIDE, PITCHSIDE, CUSTOM, DEMO, VERTICAL, PLAYER, SKY, DUMMY, NEW_WIDE, NEW_CUSTOM, TACTICAL, STADIUM, STADIUM_CUSTOM}`, plus `…_TARGET`, `…_CUSTOM_ZOOM`, `…_STADIUM_CUSTOM_{ZOOM, HEIGHT, PAN}`.

### 4.2 `fox::gr::Camera` object (live, main camera at heap `0x1c14a10`)

**OBS** by sampling against known geometry:

| Offset | Field |
|---|---|
| +0x30 | 4×4 world matrix, rows r0, r1, r2, T. r2 is the **forward** vector (camera → target), r1 is up, T is the position. Numerically r0 × r1 = r2. Screen-right is the RH-math `forward × up`; r0 points the other way (r0 = (−1,0,0) for a camera on +Z looking −Z) |
| +0x70 | view matrix |
| +0xB0 | copy of the view matrix |
| +0xF4 | focus or target distance: equals \|target − position\| |
| +0x100 | **vertical FOV in radians**. The demo shot read 0.6606 rad = 37.85° exactly, the Maya 35 mm vertical-FOV value that dominates the canm corpus (GF `PES21_CAMERA_TRACE.md` §4) |
| +0x110 | aspect 1.77 |
| +0x158 | near (in play 0.9; demo 0.5) |
| +0x15C | far 4000 |

### 4.3 What the InplayCamera produces (OBS, code)

- **`InplayCamera::update`** (vtable[4], `0x14087f280`) dispatches on mode `[this+8]`:
  - modes 3 and 6 → `FUN_1408a57f0`;
  - everything else → `FUN_1408a1a70`.

  It then writes the near plane into `out+0x34`: **60** for mode 2, **20** for mode 4, **0.9** otherwise (mode 3 keeps its own). The live near plane 0.9 confirms the field.
- **Output struct** (`FUN_14087eef0` copies it into the plugin at +0x90 and +0x84; live values matched):

  | Offset | Field |
  |---|---|
  | +0x00 | target xyz |
  | +0x0C | camera position xyz |
  | +0x30 | vFOV (rad) |
  | +0x34 | near |
  | +0x44 | float, tested `== 0` as a "free/locked" flag |

- **Mode switch blend** (`0x14087f4a0`). When a mode change is pending (`+0xA0`), position, target and FOV are blended from the stored previous pose (+0x84/+0x90/+0x9C) over N frames. N comes from a timer (`FUN_141e532d0`) scaled by the constants 211, 48 and 0.5; the exact N was not derived. The weight is `w = (1 + cos(180°·(1 − i/N) + 90°))/2` (cosine ease).
- **`FUN_1408a1a70` (modes ≠ 3, 6) builds a 9-float camera record.** Record order in code:

  | Index | Name | Meaning |
  |---|---|---|
  | [0] | Mani | lateral or lead amount; semantics not closed |
  | [1] | Fov | vertical FOV in **degrees**; `out+0x30 = Fov·π/180` (constant 0.0174533) |
  | [2] | PosY | **height ratio**: cam.y = PosZ·PosY·k |
  | [3] | LookY | target height offset |
  | [4] | PosZ | **distance** in Z from the target: cam.z = target.z + PosZ·k |
  | [5] | Z_min | clamp of target z |
  | [6] | Z_max | clamp of target z |
  | [7] | X_limit | clamp of \|target x\| |
  | [8] | Pan | camera x follows target x at `cam.x = target.x·(1 + k·(1/pan_eff − 1))` |

  Mode 5 uses `pan_eff = 1 + (Pan − 1)·0.2`; other modes use `1 + (Pan − 1)·[this+0x14]`.
- **The record source per mode:**

  | Mode | Source |
  |---|---|
  | 2 | fixed record #4 |
  | 4 | fixed record #5 |
  | 0, 1, 5 | **bilinear** blend of records #0..#3. With `a = clamp([this+0xC])` (zoom slider) and `b = [this+0x10]` (height slider): `R = (R0(1−a) + R1·a)(1−b) + (R2(1−a) + R3·a)·b`. Records #6..#9 replace #0..#3 when `FUN_14149f5e0(FUN_14149a470())` is true (condition not identified) |
  | 5 | (`constant::CameraInplay` = cameraInplay.o) as in §4.5 |

- **The 10 records** are 9 × f32 each at `.rdata 0x14260b720`, stride 0x24. They sit **unpacked on disk** at file offset VA − 0x140000A00 (OBS byte match), so they can be read at runtime from the user's `PES2021.exe`:

  | # | Mani | Fov | PosY | LookY | PosZ | Z_min | Z_max | X_limit | Pan |
  |---|---|---|---|---|---|---|---|---|---|
  | 0 | 2.73 | 19.05 | 0.30 | 0.5 | 32 | −20.1 | 27.5 | 46.9 | 1.35 |
  | 1 | 4.29 | 19.05 | 0.30 | 0.5 | 48 | 10 | 20.5 | 44.8 | 1.30 |
  | 2 | 2.73 | 19.05 | 0.471 | 0.5 | 33.1 | −20.1 | 29.7 | 46.9 | 1.40 |
  | 3 | 4.29 | 19.05 | 0.471 | 0.5 | 55.8 | −17.3 | 21.4 | 44.8 | 1.40 |
  | 4 (mode 2) | 3.9 | 14.9 | 0.488 | 0.39 | 113.3 | 7.4 | 7.4 | 31.3 | 1 |
  | 5 (mode 4) | 3.9 | 25.6 | 0.43 | −2.8 | 70.5 | 7.5 | 7.5 | 32.9 | 1 |
  | 6 | = #0 | | | | | | | | |
  | 7 | = #1 | | | | | | | | |
  | 8 | 2.73 | 19.05 | 0.471 | 0.5 | 33.1 | −18.4 | 29.7 | 46.5 | 1.1 |
  | 9 | 4.29 | 19.05 | 0.471 | 0.5 | 55.8 | −7.3 | 21.4 | 42 | 1.1 |

  (Policy note for Main: these are exe-resident PES numbers; read them at runtime, do not transcribe them into the repo.)
- **The final pose** (`FUN_1408a2180`, output lines `d3a.txt:1181-1202`), with `k = [this+0x68]` a smoothed "zoom" scalar:
  - `target.x` is clamped to ±a bound that blends towards `X_limit` by k. The starting value of the blend (`fVar44 + fVar39` at `d3a.txt:1184`) was not resolved; INF that it is a wide default;
  - `target.z = clamp(tz, Z_min', Z_max')`, where the bounds are blended from ±(halfWidth+3) towards the record values by k;
  - `target.y = LookY + clamp(f(ball height), 0, 5)`;
  - `cam = (target.x·(1 + k(1/pan_eff − 1)), PosZ·PosY·k, target.z + PosZ·k)`.

  The camera is always on the **+Z touchline side**, looking −Z.
- **`k` spring.** If not reset, per frame:
  - `v' = clamp((goal − k)·0.02 − v, ±0.005k)`, `v += v'`, `k += v`.
  - `goal` = 1 normally. When the match-state word `*piVar15 == 5`, it is `1.2 − 0.2·min(dist/FieldInfo[6], 1)`, where FieldInfo[6] = 9.16 = goal-area half-width and dist is a ball-to-point distance (not identified). It is 0.5 in one replay/foul branch.
  - On a reset, `k = goal` and `v = 0`.

  `[this+0x100]` latches a mood id 4/8/12 when target x > 15, < −15 or between, and sends it to `FUN_141794410(3, id)` (INF: audio or crowd hook).
- **Target smoothing.** `tgt = (raw + w·prev)/(w + 1)` with `w` = 10 by default, or `cameraMoveMarginTime` values when customised (mode 5). The dead-zone sizes default to `fStack_208 = (zoom·1.142857 − 0.0142857)·2` unless `cameraMoveAreaSize.isCustomize`.

### 4.4 Modes 3 and 6 (`FUN_1408a57f0`): Stadium / Stadium (Custom)

- **OBS.**
  - Records come from a `.data` table at `0x1434a00c0`: 0x58-byte entries indexed by `[this+0x78]`; 5 entries; disk offset VA − 0x140000E00.
  - Mode 6 reads **three** sliders: `+0xC`, `+0x10` and `+0x14`. Each is mapped piecewise-linearly with breakpoints (0.2/0.4, 0.6 …) onto PosY, PosZ/Z_min/Z_max/X_limit and LookY.
  - Mode 3 forces `PosZ = 63` (`_DAT_14260ba74`).
  - The result goes through `FUN_1408a5b60`, which was not decompiled.
- **INF (medium-high).** Mode 6 = **Stadium (Custom)** (three sliders = `STADIUM_CUSTOM_{ZOOM, HEIGHT, PAN}`), mode 3 = **Stadium**.

### 4.5 Dynamic Wide (mode 5) and `cameraInplay.o`

- **OBS.** `common/match/constant/constant_match.bin` (WESYS) holds `cameraInplay.o` (352 B) and `cameraSetplay.o` (80 B).
  - The loader (JSON-path table at `0x142b48ca0`: {`DevelopData/.../cameraInplay.json`, ctor}) is a jump into the protected region (`0x15b4da4e0`, mutated code) and was not decompiled.
  - The loaded object (`constant::CameraInplay`, live at heap `0x4352a070`) carries the same values. **In memory**, each 9-value `paramTable` is stored in ASCII-sorted key order.
- **Field names.** JSON key strings in `.rdata 0x142b5c978…0x142b5d3b0` (`keys.txt`) give:
  - `aiDebugCamera{cameraY, cameraZ, isEnable, lookAtZ}`;
  - `cameraMoveAreaSize{isCustomize, Fov, Pan}`;
  - `cameraMoveMarginTime{isCustomize, Fov, Pan, …}`;
  - `dropPointDispFast{afterKickTime, decelerateRate, isEnable, isGrounderEnable, passDistFly, passDistGrounder, speedLimit}`;
  - `isTargetKeepPlayerEnable`;
  - `newWideCamera{ballZRangeMarginUnder, ballZRangeMarginUpper, isEnable, paramMax{distance, isCustomize, paramTable}, paramMin{…}, relayParam{distance, isEnable, paramTable, relayBallPosZ}}`;
  - `paramTable{Fov, LookY, Mani, Pan, PosY, PosZ, X_limit, Z_max, Z_min}`.
- **OBS (key order proof).** The file/in-memory paramTable order is ASCII-sorted (`Fov, LookY, Mani, Pan, PosY, PosZ, X_limit, Z_max, Z_min`). The relayParam table read in that order maps exactly onto hard-coded record #2 in code order (`Mani, Fov, PosY, LookY, PosZ, Z_min, Z_max, X_limit, Pan`): 2.73, 19.05, 0.471, 0.5, 33.1, −20.1, 29.7, 46.9, 1.4.
- **In-memory offsets used by code** (`constant::CameraInplay`):

  | Offset | Field |
  |---|---|
  | +0x18 | cameraMoveAreaSize.isCustomize |
  | +0x1C | cameraMoveAreaSize.Fov (0.9) |
  | +0x20 | cameraMoveAreaSize.Pan (0.9) |
  | +0x24 | cameraMoveMarginTime.isCustomize |
  | +0x28 | cameraMoveMarginTime value (7) |
  | +0x2C | cameraMoveMarginTime value (30) |
  | +0x40 | dropPointDispFast.passDistGrounder (25) |
  | +0x4C | newWideCamera.ballZRangeMarginUnder (5) |
  | +0x50 | newWideCamera.ballZRangeMarginUpper (10) |
  | +0x58 | paramMax header: 0.9, 0.7 |
  | +0x60 | paramMax.isCustomize |
  | +0x64…+0x84 | paramMax.paramTable (sorted): Fov 22, LookY 0.5, Mani 3, Pan 1.3715, PosY 0.42, PosZ 49, X_limit 41.01, Z_max 21.921, Z_min −10.209 |
  | +0x88 | second customise flag |
  | +0x8C | paramMin header: 0.9, 0.2 |
  | +0x98…+0xB8 | paramMin.paramTable: Fov 29.698, LookY 0.5, Mani 3, Pan 1.324, PosY 0.4, PosZ 35.5, X_limit 41.01, Z_max 21.406, Z_min 2.076 |
  | +0xC0… | relayParam |
  | +0xCC… | relay paramTable |

- **Mode 5 formula** (`d2a.txt:116-175`). Inputs: `zoom = [this+0xC]`, `height = [this+0x10]`, `u = 1 − clamp(zoom)`, `h = clamp(height)`.
  1. Ball-side factor `t = clamp((tz − (−(W − marginUpper))) / ((W − marginUnder) − (−(W − marginUpper))), 0, 1)`. Here `W = FieldInfo.halfWidth (34.06)` and `tz = [this+0x20]`, the smoothed target z. That gives the range −24.06…29.06; `t = 1` when play is on the camera's near touchline.
  2. Far-side (t = 0) values, when the paramMax customise flags are set:
     - `Fov_far = paramMax.Fov + (18 − paramMax.Fov)·u`;
     - `PosY_far = paramMax.PosY + (0.52 − paramMax.PosY)·h`;
     - `LookY, Mani, PosZ, X_limit` from paramMax.
  3. Near-side (t = 1) values: `Fov = 28.698 − 3u`, `PosY = 0.3798 + 0.0702h`, `PosZ = 35.442`, `LookY/Mani/X_limit` from the second table (+0x9C/+0xA0/+0xB0).
  4. Every output = `far + (near − far)·t`.
  5. Mode 5 always uses `Z_min = −8`, `Z_max = 21.921`, `pan_eff = 1 + (Pan_bilinear − 1)·0.2`.
- **Constants referenced:** 18 (`0x14259be18`), 0.52 (`0x14259b8a0`), 0.0702 (`0x14260b8dc`), 0.3798 (`0x14260b8e0`), 28.698 (`0x14260b8fc`), 3, 21.921 (`0x14260b8f8`), −8 (`0x1425c7ba4`), 35.442 (`0x14260b900`), 0.2 (`0x142598578`).

### 4.6 Live validation of §4.3–4.5 (in play, 20:39:37–43, `samp3.py`)

- **OBS** over 24 samples while play moved left:
  - target.y = 0.90 constant;
  - target.z clamped at **−8.00** (= the mode-5 Z_min);
  - cam.y / (cam.z − target.z) = **0.4262** constant (= PosY ratio);
  - cam.x / target.x = **0.938** constant (`1/pan_eff` → pan_eff 1.066, Pan ≈ 1.33);
  - vFOV **23.26°** constant;
  - near 0.9;
  - cam.z − target.z fell from 46.27 to 44.95: the k spring converging.

  Example: cam (−4.99, 19.16, 36.96), target (−5.31, 0.90, −8.00).
- **Fit.**
  - With t = (−8 + 24.06)/53.12 = 0.3023, solving the mode-5 formulas gives zoom ≈ 0.79 and height ≈ 0.20. PosZ then comes out at 44.90 vs 44.95 observed (k ≈ 1.001).
  - The same zoom and height reproduce the 20:27 pose (target z 5.107 → t = 0.549): predicted vFOV **24.96°** vs observed 24.99°; predicted PosY **0.4148** vs observed 0.4146.
  - Pose at 20:27: cam (3.456, 18.46, 49.63), target (3.70, 0.90, 5.107), dist 47.86, vFOV 0.4361 rad.
- **INF (high).** The user's camera is **Dynamic Wide** (mode 5) with the custom sliders, and the cameraInplay.o paramMax branch is active.

### 4.7 cameraSetplay.o

- **OBS** keys: `ballChase{isEnabled, lookDelay, posDelay}`; `cameraChangeType{fixedChangeTime, keepTime, lineOutChangeTime{keepTime, …}}`.
- **OBS** values (80 B): {1, 0.03, 0.03} plus a sub-object {2 (int), 0.5, 0.25, 0.25, …, 0.25, 1}.
- **Open:** the pointer convention of this small file (offset base) is not pinned down, so the value → key mapping is not given.

---

## 5. Killed hypotheses

1. **"cameraInplay.o fields are in struct-declaration order."**
   - Killed: the serialized paramTable is ASCII-sorted by key. The relay table permutes exactly onto the hard-coded code-order record #2.
   - The camera struct the code consumes is the sorted one at +0x64 (paramMax.Fov = 22 is read from +0x64 and is the value that fits the live FOV).
2. **"World or gameplay positions carry the 1/20480 gani scale."**
   - Killed by corner flags (±52.48, ±34.0), goal geometry (7.32 × 2.44, 0.12 posts), FieldInfo (52.56 / 34.06 / 9.15) and live camera, ball and target values. All are metres.
3. **"The low 2 m moving camera at 20:25 is the broadcast camera."**
   - Killed: its vFOV was 37.85°/44.96° (canm Maya lens values) and its near plane 0.5. The in-play pose later was 18–19 m high at 23–25° with near 0.9.
4. **"Stadium models are reachable through the current `PesVfs`."**
   - Killed: they are inside nested `Asset/model/bg/*.cpk` in dt30_g4.cpk. Only 4cc loose overrides are visible.
5. **"The cameraInplay loader can be decompiled for field names."**
   - Blocked: `0x141e81170` → `jmp 0x15b50b070` → ctor `0x141e81070` → `jmp 0x15b4da4e0` (mutation-obfuscated). Field names came from `.rdata` JSON key strings instead.
6. **"Camera presets live in data files."**
   - Partly killed: only Dynamic Wide (cameraInplay.o) is data-driven. The other modes use the `.rdata` records at `0x14260b720` and the `.data` table at `0x1434a00c0`. Both are readable from the on-disk exe at fixed file offsets.
7. **"FieldInfo constants exist as literals."**
   - Killed: only 52.56 and 9.15/9.16 appear as literals. The others are computed at runtime.
8. **"The `0x1437cf4f0` float block is the pitch definition."**
   - Killed: it is a set of goal-net and pitch visibility AABBs that duplicate `.rdata 0x142abdb00`.

## 6. Open (what was tried)

- **Mode int → menu name** for modes 0, 1, 2 and 4. Only 5 = Dynamic Wide (high), 6 = Stadium (Custom) and 3 = Stadium (medium-high) are inferred.
  - Tried: the option string table (order known, §4.1) and the dispatch code.
  - Not found: the writer of `InplayCamera+8`, or the `registry::CameraSettings` layout.
  - Next step: scan for the `CameraSettings` copy object (`Copy<CameraSettings>` vtable) and change the option in-game (not allowed here).
- **`Mani` semantics**; the condition selecting records #6..#9 (`FUN_14149f5e0`); `FUN_1408a5b60` (mode 3/6 apply); the set-piece plugins (Kickoff, FreeKick, Corner, GoalKick, Penalty, OutOfPlayDemo, Vertical).
  - Only their vtables were listed; Ghidra MCP was down. The private headless copy at `<scratch>/stadcam/gh` (`-noanalysis -readOnly`, `DecompAt.java`) works for single functions.
- **Team → home stadium.** `Team.bin` was not parsed. Nor were the `constant_stadium.bin` `demoarea_*` / `studiumPositionData` semantics, or the CrowdRenderingSettings source file.
- **`audi_motion.bin`**, the `audiarea.bin` slot meaning (4 slots = stand sides, INF) and the per-row spectator spacing (GF assumes 0.5 m) are not decoded.
- **Nested-cpk vs download precedence** and whether empty 48-byte overrides suppress nested originals are not observed (§1.1).
- **The 1/20480 gani scale** needs the gani position dequantisation in the exe; it is not in this scope.

## 7. Reimplementation recipe (summary)

1. **VFS.** Index the nested `Asset/model/bg/{common,common_ext,stNNN}.cpk` from `dt30_g4.cpk` under the download layer. Honour 48-byte empty fpk/fpkd overrides.
2. **Stadium assembly.** For stadium id N (from `Stadium.bin` +4, folder +370):
   - load the fox2 of `stNNN.fpkd`, `stNNN_{d|n}{f|r}.fpkd`, `pitch_stNNN.fpkd`, `big_flag`, `light_stNNN_a{f|r}.fpkd`;
   - instantiate each `StadiumModel` / `EnlightenStadiumModel` / `BillModel` `modelFile` with its `TransformEntity` (scale, quat, translation; all in metres, Fox space);
   - add goal_post (×2 via 180° Y), goalnet (cloth), cornerflag and the selected bill pattern from `config.xml`.
3. **Pitch.** Use `FieldInfo` semantics: half-length 52.56 and half-width 34.06 to the line outer edges, goal 3.66 × 2.44, areas and spot per §2. Do not use GF's 110 × 72.
4. **Camera (Dynamic Wide).** Implement §4.3 and §4.5:
   - target = smoothed ball focus with exponential weight 10, clamped to x ±X_limit and z [−8, 21.921];
   - t from target z;
   - Fov, PosY, PosZ, LookY, Mani, X_limit blended far → near by t, with the user zoom/height sliders;
   - cam = (tx·(1 + k(1/pan_eff − 1)), PosZ·PosY·k, tz + PosZ·k);
   - vFOV = Fov°; near 0.9, far 4000; mode switches cosine-blended;
   - read the paramTables from `cameraInplay.o` at runtime (sorted keys), and the other presets from the exe at `0x14260b720` (file offset VA − 0x140000A00).
