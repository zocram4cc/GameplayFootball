# PES 2021 officials and ball — evidence report

Scope: read-only RE of the shipped data (VFS via `pesfmt_dump`, cpk TOCs read directly), of `PES2021.exe` (memory image `<scratch>/pesmem.bin`, base 0x140000000, headless Ghidra decompiles of single functions on a private copy of the small project, `-noanalysis -readOnly`), and of the live process (pid <pid>, reads of `/proc/<pid>/mem` only; nothing written, no input sent). Scratch scripts: `<scratch>/OfficialsBallRE/` (`toc.py` cpk TOC reader, `fm.py` fmdl dump, `watch.py`, `simsamp.py`, `futsnap.py`, `inst.py`; decompiles `d1.txt`…`d12.txt`).

Tags: **OBS** = observed (how is stated); **INF** = inferred (confidence stated).

Live state during this pass: sider **not** loaded (no `sider.dll` in `/proc/<pid>/maps`). Three matches were seen: one in progress at the start (ball 030), menus, then a new match (ball 006, referee kit `referee_2`, referee ids 8/23/32/35/19).

---

## 0. Headline facts

1. **OBS — Ball file rule:** `/Assets/pes16/model/ball/ball%03d/ball.fpk` (VFS `Asset/model/ball/ballNNN/#Win/ball.fpk`). The fpk holds exactly one member `ball.fmdl`. Ids > 999, or an id whose fpk does not exist, fall back to the *default ball* (settings u16 at `*(*(0x143705e10)+0x50)+0x3fcce`).
2. **OBS — Default ball = the ball whose `BallCondition.bin` record has condition 26 (0x1A)**; if none, the first `Ball.bin` record. 4cc install: ball 1. Vanilla data: ball 22.
3. **OBS — PES does not read `livecpk/` by itself.** The exe image has no `livecpk`/`cpk.root` string, the process has no open fd under `livecpk/`, and sider is not loaded. The `livecpk/ball-root` ball021 differs from the 4cc_38 ball021 (different md5). **GodotFootball's VFS mounts sider roots (plan Step 2.3), so for this install it would show a different ball021 than the game does without sider.** Decision for Main: mount sider roots only if sider is actually used.
4. **OBS — The match simulation runs at 54 ticks/s.** Global f32 at 0x14351ae54 = 54.0 (getter `FUN_141e532d0`). Live: position step = velocity × 0.0185185 (= 1/54) on every tick (median over 2,472 ticks). Wall-clock tick rate was 54.95/s.
5. **OBS — Ball units:** position in metres (Fox space, Y up), velocity in m/s, spin stored as raw × 492.181 = rad/s (492.181 = 2π·4700/60, i.e. raw 1.0 = 4700 rpm, INF high). The orientation quaternion advances by |ω|·dt per tick (ratio 0.99999 over 2,465 ticks).
6. **OBS — Radii:** physics r = **0.10868593 m** (hard-coded immediate `0x3dde96bb`, 24 code sites; the ball rests at y = r). Render mesh radius = **0.110 m** (fmdl bounding box ±0.110003; vanilla ±0.11/0.109932). The render node has scale 1 and its translation = the sim position, so the drawn ball sits 1.3 mm into the ground. INF (medium): r = 0.6829 m circumference / 2π.
7. **OBS — Flight model** (`FUN_140874470` + integrator `FUN_140876360`): gravity 9.80665, drag with a drag-crisis band, and Magnus. It reproduces the live per-tick acceleration to a median error of 0.0013 m/s² (gravity-only: 3.55 m/s²). Full formula in §6.3.
8. **OBS — Referee assembly** = the normal character builder with character kind 4 (chief) / 5 (assistants). Face `face/real/referee%03d/face.fpk`. Boots folder `k(9900+id)`. Kit `referee/referee_<CAT>_<n>.bin` → the realUni parser. Props come from `common_package.fpk`.
9. **OBS — The 4cc referee pack replaces the referee's look by the "boots" trick.** `boots/k99NN/boots.fpk` holds a full-body mesh (`MESH_medical_staff`, 32 body bones). The face fpk is emptied (only `face_diff.bin`) and `arm_bsm.ftex` is a fully transparent 4×4 DXT5 hider.

---

## 1. Ball list and selection

### 1.1 Files and which archive wins (OBS, cpk TOCs)

| VFS path | Archives (mount order) | Winner |
|---|---|---|
| `common/etc/pesdb/Ball.bin` | dt00_x64 (627 B), dt10_x64 (627 B), download/4cc_01_base (6,860 B), 4cc_38_balls (6,860 B) | 4cc_38 |
| `common/etc/pesdb/BallCondition.bin` | dt00/dt10 (288 B), 4cc_01 (544 B), 4cc_38 (1,800 B) | 4cc_38 |
| `Asset/model/ball/ballNNN/#Win/ball.fpk(+d)` | dt15_x64: 49 ids; 4cc_38_balls: 50 ids | per id, last wins |
| `Asset/model/ball/ballNNN/#windx11/*.ftex`, `name.txt` | dt15 / 4cc_38 | — |
| `Asset/model/ball/ball_stain/*` | dt00_x64 | — |
| `common/render/thumbnail/ball/ball_NNN.dds` | dt15, 4cc_38 | — |

The vanilla `Ball.bin` / `BallCondition.bin` are WESYS-wrapped (header `ff 10 .. 'WESYS'`, zlib from +0x10). The 4cc ones are raw. All decompress to the sizes below.

### 1.2 `Ball.bin` (OBS)

- 140-byte (0x8C) records: `u16 ballId, u16 order, char name[136]` (UTF-8, NUL-padded). No other bytes are non-zero in any record.
- 4cc: 49 records, ids 1..49, order = id.
- Vanilla: 49 records, ids 0..952, order 1..99 (myCLUB balls 901–950 share order 99).
- In memory: pesdb + 0x183c09c, count at +0x183e6e4. Lookup by id (`FUN_1414bae10`) uses a binary search; id 0xFFFE means "the default ball".

### 1.3 `BallCondition.bin` and the default ball (OBS)

- 8-byte records: `u16 ballId, u16 seq, u8 condition, u8[3] 0`.
- Loader `FUN_1414f0710` stores `condition & 0x3F` per ball. If a record has condition **0x1A**, that ball becomes the default: `*(settings+0x3fcce) = ballId`. Otherwise the default is `Ball.bin` record 0.
- 4cc: ball 1 has conditions 0..28 (incl. 26); balls 2..49 have {0, 4, 5, 13}. Default = 1; OBS live: u16 = 1.
- Vanilla: `1600 2a00 1a00` → ball 22 is the default.
- INF (medium): the condition values are competition ids (which ball each competition uses). The consumer (`thunk 0x144a58df0`) lies outside the dumped image; not decoded.
- **Update (orchestrator, 2026-10-13, PESLive full image):** `0x144a58df0` (`.impdata`, clean) decompiles to a
  membership query `bool(table, cond, value)`: for each BallCondition record `i < *(table+0x21c0)` with
  `(u16 condition & 0x3F) == cond`, look the ball up (`FUN_1414bae10` on `*(db+0x48)`), read its attribute 0
  (`func_0x1414dfed0(ball, 0, &out)`) and return 1 if `out == value` (OBS). Reached through the jmp chain
  `0x1414d6010` / `0x14eb61782` / `0x140ea942c`, with no direct `call` sites in the image (OBS byte scan), so it is called
  through a pointer or vtable. What `cond` and the attribute mean stays INF(medium); still open: name the vtable slot
  that holds the thunk and its callers.

### 1.4 Per-match ball id (OBS)

- `draw::BallList` (vtable 0x142b61f30), slot 14 = `FUN_141ecb530(list, req)`. `req` is the `draw::Ball` object (vtable 0x142b8b9b0):
  - `req+8` = ball id. If > 999 → default.
  - path = `"/Assets/pes16/model/ball/" + "ball%03d" + "/ball.fpk"`; if `FUN_1414a8d60(path)` (file exists) fails → default id.
  - Then `FUN_1423d5020` builds `<dir>/ball.model`, `<dir>/ball.mtl`, `<dir>/ball.fpk` names into a `cobra::cfg::BallConfig` (vtable 0x142a924a8). Only `ball.fpk` exists in the data.
  - The result handle goes to `req+0x10`.
- Live: `draw::Ball+8 = 6` while `ball006/ball.fpk` was loaded. The first match had `ball030`.
- The match-setup struct (§4.2) byte +0x14 = 6 as well. INF (medium): that byte is the match's ball id.
- How the menu picks the id was not traced (there is a "random ball" thumbnail `ball_randam`). INF (medium): random or chosen in the menu. GodotFootball should take it as a match parameter, defaulting to the §1.3 default.

### 1.5 Ball model (OBS, `fm.py`)

| | vanilla `ball000` | 4cc `ball030` |
|---|---|---|
| mesh group | `MESH_ball`, 1 mesh, no bones | same |
| vertices | 812 | 3,909 |
| vertex elements | pos f32×3, normal f16×4, tangent f16×4, UV0 f16×2, UV1 f16×2 | same |
| bbox | ±0.11, ±0.109932, ±0.11 | ±0.110003, ±0.109998 |
| material | `basic_fx`, shader `fox_3ddf_basic_nrmuv`, technique `fox3DDF_Blin_NrmUV_LNM` | technique `fox3DDF_Blin_NrmUV` |
| textures | `ball000_c_bsm_alp`, `ball000_n_nrm`, `ball000_s_srm` | `ball_col`, `ball_nrm`, `ball_srm` (same folder) |
| param | `MatParamIndex_0` = 0 | 0 |

- Textures resolve like other fmdl refs: `/Assets/pes16/model/ball/ballNNN/X.(dds|tga)` → `Asset/model/ball/ballNNN/#windx11/X.ftex`.
- Some 4cc folders also carry `ball000_*` / `ballNNN_t_mtl` files, which are not referenced by their fmdl.

### 1.6 Ball shader (OBS, GrModelShaders lua + DXBC)

- `GrModelShaders_dx11.lua`: `fox3DDF_Blin_NrmUV` → Deferred `fox3ddf_blin_nu`, Forward `fox3dfw_blin_nu`.
- `GrModelShadersNoLnm_dx11.lua` maps `fox3DDF_Blin_NrmUV_LNM` to the same `fox3ddf_blin_nu` / `fox3dfw_blin_nu`.
- PS `fox3ddf_blin_nu` (disassembled from `GrModelShaders_dx11.fsop`, XOR 0x9C) is identical to `fox3ddf_blin` (shaders report §3.6) except that **the normal map samples UV1** (`v2.zw`). Base and SRM sample UV0.
- So: GB0 = Base.rgb; alpha test on Base.a with the dither noise; GB2.x = f(√(srm.g² + σ²)) with specular AA; GB2.y = srm.r; GB2.z = 1; GB2.w = MatParamIndex (0).

### 1.7 Ball stain overlay (OBS)

- `/Assets/pes16/model/ball/ball_stain/ball_stain.fmdl`: shell mesh, bbox ±0.1106.
- Technique `pes3DDC_Adjust_LNM` (decal). Params `MatParamIndex_0` 21, `AlbedoBlendRate` / `NormalBlendRate` / `SpeRgmBlendRate` = 1.
- `FUN_141a172b0` loads it and picks the texture pair:
  - `g` (`ball_stain_g_bsm/_srm`) when `*(0x1437c9238+0xb8)` is null, or `*(*(that+8)+0x1c) == 0`;
  - else `m` (`ball_stain_m_*`).
- Live: that object's `+0x1c` = 0 → `g`.
- INF (medium): g = grass, m = mud (wet pitch). It is a live model in matches (OBS: one `ball_stain.fmdl` Model).

---

## 2. sider `livecpk/ball-root` (OBS)

- `sider.ini`: `livecpk.enabled = 1`, `cpk.root = ".\livecpk\ball-root"`.
- That root holds 7 files: `ball021` fpk/fpkd/3 ftex, `thumbnail/ball/ball_021.dds`, `common/menu/general/topModeSelectDMM.bin`.
- md5 comparison:
  - livecpk `ball021/#Win/ball.fpk` is 44,816 B; dt15 has 38,288 B and 4cc_38 has 104,368 B. All three differ.
  - Only the 48-byte `.fpkd` is identical in all three.
- The exe has no `livecpk` / `cpk.root` / `ball-root` strings.
- `/proc/<pid>/fd` lists every Data and download cpk and nothing under `livecpk/`.
- `sider.dll` is not mapped.
- ⇒ The game reads `livecpk` only through sider. Without sider, ball021 = the 4cc_38 version.

---

## 3. Officials: models and textures

### 3.1 Character kind and records (OBS)

- `FUN_141ec2890(rec, idx, id, p4, p5)` builds the appearance record for official `idx`. It is called by:
  - the match entity factory `FUN_140a35a00(kind 0 chief → 0x30E0-byte `RefereeChief`; 1/2 → 0x3080-byte `RefereeLinesman`)`;
  - demo actors `FUN_141f577b0`.
- Record contents:
  - `+0x04` = official id; `+0x08` f32 = 180.0 (INF high: height cm);
  - `+0x4C` = character kind: **4 for idx 0 (chief), 5 otherwise**;
  - `+0x58` name = `"REFEREEA"/"REFEREEB"/"REFEREEC"/"REFEREED"` for idx 0..3;
  - `+0x4F` / `+0x50` = p5 / p4.
- Live `UniformUnit` for officials: kit slot uid 4; `F+0x08` = 1 chief / 2 assistants (matches the kit report §7.1).

### 3.2 Face (OBS)

- `FUN_141ec53d0(id, isReferee)`: `"/Assets/pes16/model/character/face/real/" + ("referee%03d" if referee else "%d") + "/face.fpk"`.
- Montage FaceData: `skull/player/referee/Id%dFaceData.bin` (`FUN_1419db4c0`, type 2). These files are absent from this install (face report §3).
- Vanilla `face/real/referee001..035` are in `Data/dt36_g4.cpk`: face_bsm_alp / nrm / srm / trm / eye_occlusion, and hair_parts_* for some.
- **4cc_35_referees overrides all 35:**
  - `face.fpk` is 1,072 B and contains only `face_diff.bin`;
  - `sourceimages/#windx11/arm_bsm.ftex` is 208 B: 4×4 DXT5, alpha block `ff 00 49 92 24 49 92 24` = every texel alpha 0. This is a hider by the 4cc rule (GF `texture_is_hider`).

### 3.3 Body: boots / glove ids (OBS)

- Kit report §8.3: character kinds 4/5 use `playerId + 9900`. **Confirmed live:** the three official UniformUnits carry `A+0x34` = 9908, 9923, 9932, and the match setup's official ids are 8, 23, 32.
- `BootsList.bin` has no 99xx keys, so the folder is `boots/k99NN`.
- 4cc_35 ships `boots/k9901..k9935/#Win/boots.fpk` (+ textures `skin`, `glow`, `head`, `bike`, `player`, `memebike` in some) and `glove/g99NN` for 10 ids.
- `k9901/boots.fmdl`:
  - group `MESH_medical_staff`, 32 bones (`dsk_hip`, thigh/leg/foot, belly/chest/neck/head, arms, hands, d-helpers);
  - material `kit` (`fox3DDF_Blin`, Base `/Assets/pes16/model/character/uniform/texture/referee_1.dds`, normal `common/999/sourceimages/kit_nrm`, SRM `boot_srm`, SubNormal `common/biz/...`);
  - material `medic` (Base `boots/k9901/skin`).
- So in this install the official's visible body is a full mesh skinned to the body skeleton and loaded as the boots part. The face and arms are hidden by §3.2.
- **All 35 packs (OBS, fmdl dump of each `k99NN/boots.fpk`):**
  - 33 have group `MESH_medical_staff` (+ `split-mesh` sub-meshes) and 4–83 bones; ids 25 and 34 have group `thomas` with material `player` (`player.dds`), 86 bones.
  - 24 packs carry a `kit` material whose Base is always `uniform/texture/referee_1.dds`; ids 6–9, 18, 23, 25, 32–35 have no `kit` material (skin/head/bike/player textures only).
  - Ids 11–15 and 26–30 add `constant_NF` with `glow`; they have only 4 bones (rigid).
  - So for GodotFootball the official's look is the boots-pack mesh, rendered with the 4cc pack rules (hider arm_bsm, kit slot), not a kit-driven body.
- INF (medium): whether the `kit` material's `referee_1.dds` follows the chosen referee kit (live kit was `referee_2`) was not checked. This is the same open question as kit report §8.6.

### 3.4 Kit (OBS)

- **Name:** `FUN_141ea2160(out, compCode)` →

  ```
  "cpk_dat/common/character0/model/character/uniform/team/" + "referee/referee"
    + CAT + "%d"(n+1) + ".bin"
  ```

  - CAT = `{_DEF_,_LB_,_LBN_,_JL_,_ACL_,_SDA_,_SDN_}` (table 0x14351e460) or `_EU_` (index 7).
  - compCode → index: 5 → LB(1); 6, 7 → SDA(5); 8 → ACL(4); 0x21 → EU(7); anything else → DEF(0).
  - compCode = the request's `+0x20` if it equals `+0x24`, else 0xFFFF. INF (medium): the two teams' competition codes.
  - If the colour map below has no entry for that index, the index falls back to 0 (DEF).
- **Kit number n = `FUN_14152b2c0(colorList)`, a colour-contrast choice (OBS asm):**
  - For each team (home, away): take the shirt's two RGB colours from the team kit colour table (team+0x5D8, 10 × 8-byte records matched on kit type/flags).
  - Convert each colour to Lab (`FUN_141522790`):
    - `c = (v/255)^2.2`;
    - `X = 0.3933R + 0.3651G + 0.1903B`, `Y = 0.2123R + 0.701G + 0.0858B`, `Z = 0.0182R + 0.1117G + 0.957B`;
    - `L = 116·Y^{1/3} − 16`;
    - `a = 500·((100X/95.045)^{1/3} − Y^{1/3})`;
    - `b = 200·(Y^{1/3} − (100Z/108.892)^{1/3})`;
    - no linear segment.
  - For k = 0..8: use the first colour record with `(byte1 & 1) == 0 && byte0>>4 == k`, and its **first** RGB (the second is converted but unused).
  - Score = f(home) + f(away). Per team:
    - `s(d) = 0` if d ≤ 30, `(d−30)·1.4285` if 30 < d < 100, else 100; clamp ≤ 100;
    - `f = s(|team1 − ref|) + 0.5·s(|team2 − ref|)`, halved if either term ≤ 0.
  - Pick the k with the strictly largest score (first wins); n = k + 1.
- **Colour list = `RefereeColor.bin`** (`common/character0/model/character/uniform/team/RefereeColor.bin`, WESYS, 255 B decompressed; vanilla dt00 = dt34, no 4cc override):
  - 3 groups of `u32 category, u8 count, 10 × {u8 index, u8 flags, RGB, RGB}`;
  - categories 0 (5 kits), 4 (4 kits), 7 (4 kits).
  - Live parsed map (`*(*(0x1437f4868)+8)+0x28`) holds keys 0/4/7, records `byte0 = k<<4`, empty slots `0x91`.
- **Config bytes:** `FUN_141ea3190` → the UniformParameter table (kit report §0.1). The winning `UniformParameter.bin` holds referee DEF_1..5 and ACL_1..4 but **no EU entries**. INF: an EU match would find no config (behaviour not traced).
  - DEF_n: sleeve A = 3, B = 1003, shorts 14, collar 1, KitFile `referee_n`, no number/name files.
  - ACL_n: collar 66, shorts 2, KitFile `referee_ACL_n`.
  - The 4cc_35 loose `referee_DEF_n.bin` (collar 105, shorts 16) are **not** what the game parses (OBS live below).
- **Live official parts (UniformUnit uid 4, F8 = 1 and 2):**
  - `nocloth/pants_014.fmdl`;
  - `nocloth/referee_collar_001.fmdl` (collar rule: kind 1/2 → `referee_collar_%03d`);
  - `nocloth/sleeve_glove.fmdl` + `sleeve_glove_cap.fmdl`;
  - `common/socks_short.fmdl`, `common/pants_in_sub.fmdl`, `common/pants_out_sub.fmdl`;
  - no `shirt_in/out` part. INF (medium): with the 4cc body mesh there is no shirt; vanilla referees were not observed.
  - KitFile textures: `Asset/model/character/uniform/texture/#windx11/referee_N.ftex` + `referee_N_srm.ftex`. Common normal maps: `character/common/sourceimages/referee_{shirt,sleeve}_{nrm,normal}.ftex`.
  - `referee_collar_{001,007,012,026,060,066,084,085}.fmdl` exist (dt35_g4).
- **Part-category enum (OBS, table 0x142a8fc10):** `referee_shirt` 38, `referee_sleeve` 39, `referee_prop_base` 40, `referee_flag` 41, `referee_flag_close` 42.

### 3.5 Props (OBS, `common_package.fpk` / `.fpkd` `assets.fox2` CharacterAsset entities, and fmdl dumps)

| Asset name | File | Material / technique | Bound bone(s) | Live count per match |
|---|---|---|---|---|
| `referee_prop` (wrist watch) | `common/referee_prop.fmdl` | `judge_watch`, fox3DDF_Blin, Base `cup_logo` | forearm/wrist/hand helpers | 3 (all officials) |
| `whistle_mouth` | `common/whistle_mouth.fmdl` | `judge_whistle`, pes3DDF_Blin (`pes_3ddf_basic_color`) | `sk_head` | 1 |
| `whistle_hand` | `common/whistle_hand.fmdl` | `judge_whistle` | `sk_hand_l` | 1 |
| `card_yellow` / `card_red` | `common/card_*.fmdl` | `judge_card_yellow/red`, fox3DDF_Blin, Base `card_*_bsm` | `sk_hand_r` | 1 each |
| `note_yellow` / `note_red` | `common/note_*.fmdl` | same materials | `sk_hand_l` | 1 each |
| `pen` | `common/pen.fmdl` | `judge_pen`, pes3DDF_Blin, `pen_bsm/nrm/srm` | `sk_hand_r` | 1 |
| `referee_flag` + `skl_referee_flag` | `referee_flag/scenes/referee_flag.fmdl/.skl` | `cordura_1000d_a`, fox3DDF_Blin_LNM, Base `prop/referee_flag/referee_flag` | own skeleton `sk_flag` → cloth chains `sk_cloth_b1..3`, `sk_cloth_t1..3` | 4 |
| `referee_f_close` + skl | `referee_flag/scenes/referee_f_close.*` | `basic`, fox3DDF_Blin_LNM | `sk_flag_close` → `tip_sk_flag_close` | 4 |

- Flag textures: `Asset/model/prop/referee_flag/sourceimages/#windx11/{referee_flag, cordura_1000d_a_{nrm,srm}_preset, basic_{nrm,srm}_preset}.ftex` (dt00).
- Prop visibility is driven by demo events `TYPE_REF_CARD_Y/R`, `TYPE_REF_NOTE_Y/R`, `TYPE_REF_PEN`, `TYPE_REF_WHISTLE_MOUTH/HAND`, `TYPE_REF_FLAG_L/R`, `TYPE_REF_FLAG_CLOSE_L/R`, `TYPE_JUDGE_BOARD_NUMBER` (enum strings 0x142ba73e8…, function 0x141f6b5d0).
- Four open + four closed flags live = 2 assistants × left/right hand (INF high).
- Runtime classes: `cobra::rd::RefereePropUnit` (1 live), `AssistantRefereePropUnit` (2 live, each with `CharacterClothPropUnit`s → the flag cloth is simulated).
- Flag attach bone / hand offset: not decoded (the `sk_flag` root sits near the origin, so it is attached to a hand bone; INF).

---

## 4. How the officials are chosen for a match

### 4.1 Entity creation (OBS, `FUN_1404187b0` disasm)

```
S = FUN_14041b640(match+0x5e8)            // match-setup data = *(holder+0x10)+4
for i in 0..2: FUN_140a35a00(kind=i, id=S.u32[0x30+4i], S.u8[0x12], S.u8[0x10])
```

Demo actors use `S.u32[0x30+4·idx]` for idx < 5, else `idx+1` (`FUN_141f577b0`).

### 4.2 Live ids (OBS)

- Match-setup copy at 0xa3ef534 (a second copy at +0x90): `+0x30.. = 8, 23, 32, 35, 19`. So 5 officials: chief, 2 assistants, and INF fourth / fifth official.
- The referee units' boots ids 9908 / 9923 / 9932 match ids 8 / 23 / 32.
- `RefereeAppearance.bin` (dt00 vanilla, overridden by 4cc_35): 35 × 60-byte records, `u32 id` (1..35) + the 56-byte appearance body of the player record (kit report §8.1). In 4cc all 35 bodies are the same placeholder.

### 4.3 OPEN: how the 5 ids are picked

Tried:
- the callers of the setup reader;
- the RefereeAppearance holder (vtable 0x142b8cbb8, loader `FUN_141ec5820`);
- an immediate-9900 scan (only unrelated hits).

The writer of `S+0x30` was not found. INF (medium): random distinct ids from the RefereeAppearance list (1..35).

---

## 5. Ball state in memory (live)

### 5.1 Objects (OBS)

| Object | How to find it | Content |
|---|---|---|
| **Sim ball** (non-polymorphic, 0xD68 B, ctor `FUN_14086ca60`, reset `FUN_140870020`) | it embeds a `BallInfoRef` (vtable 0x14259afb0) at +0x4E0 ⇒ `simBall = ref − 0x4E0`. Also `BallControl@match` (vtable 0x1425988b0) +0x10 → simBall | see 5.2 |
| `match::registry::BallInfo` (inside `Copy<BallInfo>`; every `BallInfoRef+0x10` points to it) | `BallInfoRef+0x10`, or simBall+0x4F0 | prediction record (5.3) |
| `cobra::rd::SceneBallNode` (vtable 0x142a9ec68) | heap scan | render world matrix at +0x40 (row-major 4×4: X, Y, Z axes, translation); scale 1; colour `d7d7d7ff` at +0x3C |
| `draw::Ball` (0x142b8b9b0) | heap scan | +8 ball id, +0x10 loaded model handle |
| `scene::rc::BallImpl` (0x142c6b048) | heap scan | +0x54 vec3 tracked the ball in the first match; it also holds goal-net film data. INF: ball/goal-net collision scene object |

### 5.2 Sim ball layout (state block at +0x64 = `float* p` passed to the integrator; OBS)

| Offset | Field | Units / evidence |
|---|---|---|
| +0x64 | position x, y, z | m. Rest y = 0.10868593. `dp = v/54` every tick |
| +0x70 | velocity | m/s. Max seen 25.9 |
| +0x7C | spin raw (ω = 492.181·raw) | rad/s. Quaternion advances by \|ω\|/54 per tick (ratio 0.999999). Max seen 50.0005 rad/s; INF (low): clamp at 50 |
| +0x8C | timer for the wobble table (`+0x28` in the block) | s |
| +0x90.. | 6 × vec3 wobble-spin table (block +0x2C) | used only when flag byte block+0x74 (simBall+0xD8) ≠ 0 |
| +0xDC | mode u32 (≤ 5) | live always 1 |
| +0xE0 | orientation quaternion (x, y, z, w) | reset (0, 0, 0, 1) |
| +0xF0..+0x16F | two 4×4 matrices copied at reset | not decoded |

### 5.3 BallInfo (OBS, prediction data shared with AI and camera)

- `+0xBC`: **250 records × 40 B** `{vec3 pos, quat rot, vec3 spinRaw}`. Record 0 = the current tick (equals the sim position, sometimes one tick behind); the array is shifted every tick.
- `+0x34` vec3: next apex (or (0, −1, 0) when none); `+0x4C` apex height.
- `+0x40` vec3: next ground-contact point.
- `+0x84 / +0x90 / +0x9C`: further landing / receive points.
- `+0xB0` u32: ticks to the next event; `+0xB4` u32 flag; `+0xB8` u32: ticks since the last kick.
- All INF (medium) from watching them change. The update classes are `BallInfoUpdate{FutureDataAll, TopPoint, DropPoint, DropPointLast, ReceiveSpeedPos, FutureDataLast}`.

### 5.4 Render vs sim (OBS)

The `SceneBallNode` translation follows the sim position with a median lead of 0.03 s of motion (about 1.6 ticks; render pipelining). Its minimum y is exactly 0.10868593 and its rotation rows are unit length.

---

## 6. Ball physics constants

### 6.1 Tick

dt = 1/54 s (§0.4). The integrator is semi-implicit Euler: `v += a·dt; p += v·dt` (`FUN_140876360`). The orientation quaternion is integrated with ω after that.

### 6.2 Constants source (OBS live)

- `constant::Ball` (vtable 0x142b57448, one live instance). The integrator's constant pointer = object + 8. Values used:
  - `C[0]` (+0x08) = 1.0;
  - `C[0x50]` (+0x148) = 96.995 km/h;
  - `C[0x51]` (+0x14C) = 56.9956 km/h;
  - `C[0x80]` (+0x208) = 0.035;
  - wobble clamps `C[0x8F]` = 105, `C[0x90]` = 45, `C[0x91]` = 1.0.
- The whole 0x300-byte block was dumped (`<scratch>/OfficialsBallRE/cball.bin`).
- **Source file OPEN:**
  - not found as a block in `constant_match/player/team/stadium.bin` or in the exe image;
  - `constant_match.bin` section `ball.o` (352 B) holds different, unnamed values;
  - the dev path string `DevelopData/common/match/constant/match/ball.json` exists.
- Image constants: ρ = 1.293, A = 0.037110474 (= π r²), r² = 0.0118126301, r = 0.10868593, g = 9.80665, 1000/3600 km/h→m/s, spin scale 492.181, wobble period 0.4 s × 6 entries.

### 6.3 Acceleration (`FUN_140874470`, OBS; validated live)

```
s2 = vx² + vz²                               // horizontal speed squared
if s2 <= 7.716049: a = (0, -g, 0); return     // below 2.778 m/s: gravity only (also no Magnus)
s  = sqrt(s2); v1 = C[0x50]/3.6; v2 = C[0x51]/3.6
s' = (s <= v1) ? (s > v2 ? 2*v2 - s : s) : (2*(v2 - v1) + s)   // drag-crisis shaping
k  = C[0] * -0.5 * 1.293 * 0.037110474 * s'
a  = k * v + (0, -g, 0)                        // drag on all 3 velocity components
if !noMagnus:
    w  = flag ? 492.181 * table[int(t/0.4) mod 6] : 492.181 * spinRaw     // rad/s
    m  = flag ? clamp-ish(s, C[0x90]/3.6, C[0x8F]/3.6) * C[0x91] : min(s, 23.611111)
    a += m * C[0x80] * π² * r² * r * cross(w, v)
```

Validation: 1,489 airborne live ticks; observed `(v[n+1] − v[n])·54` vs this formula: median error 0.0013 m/s², p90 0.0037.

### 6.4 OPEN: ground contact, bounce and rolling

Not decoded. Live statistics from 45 s of play:
- vertical restitution ratios spread 0.37–2.26, i.e. not a single coefficient;
- ground "bobble" hops of y 0.109–0.14 while rolling;
- rolling decel ≈ 2.05 m/s² (ratio 0.981 per tick) at 0.5–3 m/s (35 ticks);
- the spin vector changes only at contacts.

The contact code is among the ~40 callers of `FUN_140876360` (0x140865f20–0x14086b513), not decompiled.

---

## 7. Killed hypotheses

- *PES itself reads sider `livecpk` roots.* Killed by the strings, open fds and maps (§2).
- *The sim runs at 60 Hz (or 59.94 like the animation clips).* Killed: `dp/v` = 1/54 exactly on every tick, and the global is 54.0.
- *The third vec3 in each BallInfo record is velocity per tick.* Killed: it stays constant through flight and changes only at contacts. It is spin raw (|raw|·492.181 matched `v/r` while rolling and the quaternion rate).
- *Render radius = physics radius.* Killed: mesh 0.110 vs physics 0.10868593 at render scale 1.
- *`BallImpl+0x54` is the authoritative ball position.* Killed: in the second match the only `BallImpl` stayed at 0 while the sim ball moved. The scene node and the sim ball are authoritative.
- *The referee kit config comes from the loose 4cc `referee_DEF_n.bin`.* Killed: live parts (pants_014, referee_collar_001) match the UniformParameter entries (shorts 14, collar 1), not the loose files (16, 105).
- *A simple `vy_out = −e·vy_in` bounce.* Killed by the spread in §6.4.
- *`constant::Ball` values come from `constant_match.bin` `ball.o`.* Killed: the values differ.

## 8. Status

| Question | Status |
|---|---|
| Q1 officials: models, textures, face (+9900 rule), kits, props | **closed** (flag attach offset open) |
| Q1 how the referee is chosen | kit choice **closed** (colour contrast + category); ids found in match setup; **id picking open** |
| Q2 ball list, files per id, default/selection rule | **closed** (menu/random choice of the per-match id not traced) |
| Q2 livecpk without sider | **closed: no** |
| Q2 shader / material | **closed** |
| Q2 render vs physics radius | **closed** (0.110 vs 0.10868593) |
| Q3 live ball object, fields, units | **closed** (pos m, vel m/s, spin rad/s = raw·492.181, dt 1/54 s); flight model closed; **ground contact open** |
