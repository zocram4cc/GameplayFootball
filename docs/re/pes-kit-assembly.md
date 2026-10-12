# PES 2021 kit / uniform assembly — evidence report

Scope: read-only RE of `sider.dll` (Ghidra headless, image base 0x180000000) and of the **live** `PES2021.exe` process (pid <pid>; the on-disk exe is packed, so `.trace`/`.rdata`/`.data`/`.pdata` 0x140000000–0x143d14000 were dumped from `/proc/<pid>/mem` to `<scratch>/pesmem.bin` and loaded raw into Ghidra at 0x140000000; function bounds come from the in-memory `.pdata`). All addresses below are those VAs. Scratch only in `/tmp` (`<scratch>/ghk/out/p*.txt` hold every decompile quoted). Nothing copied into the repo.

Legend: **OBS** = observed (decompile / bytes / live memory), **INF** = inferred (confidence given).

---

## 0. Headline facts (read first)

1. **OBS — PES takes kit config bytes from `UniformParameter.bin`, not from the loose `*_realUni.bin`.** The kit loader `FUN_141ea3190` gets the record with `FUN_141ea2c90(tbl, path)` (size by file name) and `FUN_141ea2ad0(tbl, path)` (data pointer) (`tbl = *(*(DAT_1437f4868+8)+0x50)`). Those accessors use `FUN_1414a98d0`/`FUN_1414a9900`, which walk 12-byte `{u32 dataOff, u32 size, u32 nameOff}` records. That is exactly the `UniformParameter.bin` layout (`u32 count, u32 tableOff`, then records). Live check: `DAT_1437f4868=0x1f3759c0 → +8 → +0x50 = 0x1f37b560`, giving `base 0x7f8f0010`, `count 2108`, first name `701_DEF_1st_realUni.bin`. That is byte-identical to the VFS-winning `UniformParameter.bin`. The loose file only feeds an **existence test**: `FUN_1414a8d60(path)`, called from `FUN_141ea1320`/`FUN_141ea54a0`. This matches kserv.lua:1078 ("dummy real bins. Game still checks for existence of these files") and sider's `find_kit_info` (`FUN_180018500`), which walks the same in-memory table.
2. **OBS — In vanilla data both sources are identical.** The vanilla `dt34_g4.cpk` `UniformParameter.bin` is a WESYS zlib container (header `05 10 d1 57 'WESYS'`, zlib from +0x10, 326,992 B decompressed). It has 2,174 entries: 1,359 × 120 B `*_realUni.bin`, 802 × 96 B `*.bin`, and 13 × 120 B other files (referee `.bin`, sizes from the same per-entry tally). All 1,359 realUni entries equal the VFS-winning loose files byte for byte (0 diffs).
3. **OBS — This install's mods change what PES actually renders:**
   - The VFS-winning `UniformParameter.bin` comes from `4cc_69_midcup.cpk` (raw, 2,108 entries, only 210 teams) and has **no team-1 entries**. With this install, PES therefore falls back to the default-team kit for team 1: `FUN_141ea29c0` when the lookup returns null.
   - `4cc_04_fpc.cpk` replaces these `nocloth` parts with 1,218-byte `MESH_placeholder` stubs: `shirt`, `shirt_in`, `sleeve_glove`, `sleeve_glove_cap`, `sleeve_long_001..003`, `sleeve_long_cap_001..003`, `collar_105`, `pants_016`.
   - It also ships its own `shirt_out.fmdl`: 106,544 B vs vanilla 79,442 B, and it carries an extra `uni_collar` mesh.
   - The vanilla versions are in `dt35_g4.cpk` (shirt_in 63,132 B, sleeve_long_001 100,365 B, …). Query: `<scratch>/srcscan/s <root> <substr>` (lists every cpk TOC hit).
   - Vanilla `common_package.fpk` (dt00 only) itself contains placeholders: `pants_in_sub`, `socks_short`, `thigh_short`.

---

## 1. `*_realUni.bin` (120 B) layout

Primary evidence:
- sider `FUN_18009c810` (bin → Lua table), `FUN_18009b060` (Lua → bin), `FUN_18009afa0(u16* p, lo, hi)` = `(*p & bits[lo,hi)) >> lo` (hi exclusive), `FUN_18009ae70` = the matching setter.
- The game's own parser is `FUN_141ea3190`, branch `if (size == 0x78)`. `puVar5` = `u32*` over the record, so `dN` = u32 at byte offset 4·N.

Where sider and the game disagree, the game wins; disagreements are flagged ⚠.

Statistics: 1,775 VFS-winning loose realUni files (all 120 B), script `<scratch>/stats.py`; files fetched with `pesfmt_dump cat` for every `team/<id>/*.bin` in `<scratch>/gfscan/allpaths.txt` (2,577 files: 1,775 × 120 B, 802 × 96 B).

| Bytes / bits | Field (sider name) | Game use (`FUN_141ea3190`) | Observed range (n=1775) |
|---|---|---|---|
| d0 bits 0–11 | ⚠ sider splits bytes 0/1/2 as `ShortSleevesModel`=b0, `ShirtModel`=b1, `LongSleevesType`=b2 | **12-bit sleeve code A** = `b0 \| (b1&0xF)<<8`. Decoded as `type = A/1000`, `model = A%1000 (0→1)` | A ∈ {1:1722, 2:41, 3841:12} |
| d0 bits 12–23 | (same three bytes) | **12-bit sleeve code B** = `(b1>>4) \| b2<<4` | B ∈ {3001:1050, 1003:342, 1001:241, 1002:98, 3002:32, 1007:12} |
| b3 (d0 bits 24–31) | ShortsModel | `FUN_141966be0` → struct +0x0C (0→1); `FUN_141966d80(b3-11 < 2)` → +0x10 | 0–16 (1:942, 16:373, 2:286, …) |
| b4–6 | ShirtColor1 RGB | only used when `param_7==0` (`puVar5[1] \| 0xff000000` → `FUN_14197ad30`); render use unknown | — |
| b7–9 | ShirtColor2 RGB | not read in this parser | — |
| b0x0A–0C | ShortsColor RGB | → team-colour struct +0xB8 | — |
| b0x0D–0F | SocksColor RGB | → +0xC8 | — |
| b0x10–12 | UndershirtColor RGB | → +0xA8 | — |
| b0x13 | — (kserv `Unk_0x13`) | not read in this parser; **UNKNOWN** | 0–145 |
| b0x14 | Collar | `FUN_141965330` → +0x98 | 1–131 |
| b0x15 | WinterCollar | → +0x9C | 0–131 |
| d5 bits 16–20 | ShortsNumberY ⚠ sider reads bits 16–19 only. **Corrected (§8.7):** the game uses this field as the *horizontal* leg-number position (side-dependent range) | game reads 5 bits | 0–17 (17 > 4-bit max ⇒ game is right) |
| d5 bits 21–25 | ShortsNumberX ⚠ sider reads bits 22–25. **Corrected (§8.7):** the game uses this field as the *vertical* leg-number position | game reads 5 bits from bit 21 | 0–28 |
| d5 bits 26–30 | ShortsNumberSize | ✓ | 0–27 |
| d5 bit 31 | ShortsNumberSide | ✓ | 0/1 |
| d6 bits 0–4 / 5–9 / 10–13 | BackNumberY / Size / Spacing | ✓ | 0–29 / 10–28 / 0–14 |
| d6 bits 14–18 | ChestNumberY | ✓ (sider stitches u16@0x18 b14–15 with u16@0x1A b0–2) | 0–28 |
| d6 bits 19–23 / 24–28 | ChestNumberX / ChestNumberSize | ✓ | 0–25 / 0–27 |
| d6 bits 29–30 | (kserv `SleeveLimits`; sider has no field) | **selects which code feeds which sleeve slot**: 1 → both from B; 2 → both from A; else long-slot = B, short-slot = A | 0:1759, 1:13 (mostly GK kits), 2:3 |
| d6 bit 31 | TightKit | `FUN_141967030` → +0x18 | 0:1682, 1:93 |
| d7 bits 0–1 | (kserv `Unk_0x1C_Bits0to1`; README "CaptainArmband") | passed to `FUN_141ea1f50`, which formats `CL_captainmark_%02d` / `half_` | 0/1/2 |
| d7 bit 2 | BackNumberType | `FUN_1419659a0` | 0/1 |
| d7 bits 3–8 / 9–13 | NameY / NameSize | ✓ | 0–63 / 0–20 |
| d7 bits 14–16 | NameShape (bits 14–15) + Name-off (bit 16, sider `Name` at u16@0x1E bit 0) | the game reads all 3 bits as one value → `FUN_141965850` (+0x44) | 0–5 (4/5 occur exactly in the 14 Name-off files) |
| d7 bits 17–21 / 22–26 / 27–31 | LeftShortX / LeftShortY / RightShortX | ✓ (`FUN_141e592b0`) | 0–29 / 0–30 / 0–29 |
| d8 bits 0–4 | RightShortY | ✓ | 0–23 |
| d8 bits 5–9 / 10–14 / 15–19 / 20–24 | LeftLongX / LeftLongY / RightLongX / RightLongY | ✓ (`FUN_141e58f60`) | ≤ 29 |
| d8 bits 25–30 | (kserv `Unk_0x23`) | **UNKNOWN** | {15, 0, 21, 40} |
| d8 bit 31 | — | sets team struct +0xA0 flag; meaning **UNKNOWN** | always 0 |
| d9 bits 0–4 | (kserv `Unk_0x24_Bits0to4`) | **UNKNOWN** | {15, 0, 16, 31, …} |
| d9 bits 5–7 | ShirtPattern | `FUN_141965460` → +0x14 | 0–6 |
| d9 bits 8–9 | NameStretch | read; value 0 is replaced by 3 | 0–3 |
| d9 bits 10–31 | — | not read | always 0 (except one file) |
| 0x28 / 0x38 / 0x48 / 0x58 / 0x68 | KitFile / BackNumbersFile / ChestNumbersFile / LegNumbersFile / NameFontFile, `char[16]` (sider `strncat …,0xF`) | `FUN_141965470` (+0x20), `FUN_141964fd0`, `FUN_1419655f0`, `FUN_141966c40`, `FUN_141965860` | patterns: `(*, *_back, '', *_leg, *_name)` 1022; `(*, *_back, *_chest, *_leg, *_name)` 267; … |

### 1.1 Sleeve code semantics (OBS)

`FUN_141965730(type, model)` writes struct +0xA8/+0xAC. `FUN_141966dd0(type, model)` writes +0xA0/+0xA4. The type indexes the suffix table `DAT_143516838 = {"_short","_long","_half","_inner"}`, and type 3 replaces the `sleeve` prefix with `s` (see §3).

So the kserv UI labels are byte views of two packed codes:
- `LongSleevesType` 62 → B = 0x3E? = 100x → `sleeve_long_00x`.
- `LongSleevesType` 187 → B = 0xBB? = 300x → `s_inner_00x` ("only undershirt").
- `ShirtModel` high nibble 9/A/B → long model 1/2/3 (kserv 144 / 160 / "176=Legacy").
- `ShortSleevesModel` → short model.

INF (medium): kserv's "151=Semi-long" would make A = 0x7xx and land in a non-type range. It does not occur in the data.

The 12 files with b1 = 255 (A = 3841) are all `7xx_DEF_GK1st` from 4cc packs, i.e. mod artefacts.

---

## 2. The 96-byte `<team>_DEF_<n>.bin` and which file PES uses

- **OBS** `FUN_141ea2400(out, kitId, bool real, …)` builds the name:

  ```
  "cpk_dat/common/character0/model/character/uniform/team/" + "%d"team + "/" + "%d"team
    + compTag[FUN_1414bda30(team)] + {"","GK"}[(kitId>>7)&3] + {"1st".."9th"}[kitId&0x7F]
    + (real ? "_realUni.bin" : ".bin")
  ```

  - `compTag = DAT_14351e460 = {_DEF_, _LB_, _LBN_, _JL_, _ACL_, _SDA_, _SDN_, _DEF_}`
  - team = `kitId >> 14`
  - With `real=1` it returns "" when `FUN_141ea2f80(id)` is 0.
- **OBS** `FUN_141ea2f80`: returns 1 for team ids 4971, 5050, 5051, 5129, 5604, 5607, 5610, 5619. For other teams it returns 0 when a team-edit record has bit 25 at +0x420 set. INF (medium): that bit means "user-edited uniform".
- **OBS** `FUN_141ea1320` / `FUN_141ea54a0`:
  1. `p = FUN_141ea2400(id, real=1)`.
  2. If `FUN_1414a8d60(p)` (file exists), load `p`.
  3. Otherwise, if the kit is not an edited/save kit (`FUN_141ea2e40` → flag 0), load `FUN_141ea2400(id, real=0)`, the `.bin` name.
  4. Otherwise build from a 0x84-byte edit record (`FUN_141ea18c0`).

  "Load" = `FUN_141ea3190`, which pulls the bytes from the UniformParameter table by file name (§0.1).
- **OBS** `FUN_141ea3190` has two parsers, chosen by entry size: `== 0x78` (realUni, §1) and `== 0x60`.
  - The 96-byte branch decodes a different, layer-based description: `FUN_141966da0(layer 0..4, …)`, `FUN_141966c10`, `FUN_141966e00`, colour bytes << 2.
  - It sets collar = `(b0x33 & 0xF) + 0x385` for both collar fields.
  - INF (medium): it is the "edit-mode" kit, drawn from `uniform/edit/#windx11/{sh,pa,so,ca}_NNN.ftex` templates instead of a real kit texture.
- **Answer:** when both exist, PES uses the **realUni** name; `.bin` is the fallback when the loose realUni is absent. In both cases the bytes come from `UniformParameter.bin` (vanilla: identical to the loose file).

---

## 3. Config → part model files

Assembly function **`FUN_141a89120`** (1,744 decompiled lines, `<scratch>/ghk/out/p2.txt`) iterates the 15-slot table at `0x143517b30`. Each slot is `{u48 StrCode64(name) & 0xFFFFFFFFFFFF, u32 category}`. Names were recovered by brute-forcing GF `strcode.strcode(name) & 0xFFFFFFFFFFFF`.

Inputs:
- Kit struct `lVar31` = `FUN_1419a5690(...)`; fields set in §1.
- Per-player struct `param_1` (filled by `FUN_141a83850`, §5): +0xB8, +0xBC, +0xC0, +0xC4, +0xD4–D8.
- Flag word `f = FUN_141a7ff90(player)`:
  - `bVar33 = f>>2 & 1` — shirt worn, INF.
  - `pantsOn = f>>1 & 1` — shorts worn, INF.

Sleeve selection: if `+0xC0 == 0`, primary = (+0xA0, +0xA4) and collar = +0x98 (Collar). Otherwise primary = (+0xA8, +0xAC) and collar = +0x9C (WinterCollar). Secondary is the other pair.

| Slot (hash) | Rule (OBS unless marked) | File |
|---|---|---|
| `collar` 0x506226d207df | needs `bVar33` and `FUN_141a7ff50 != 1`; `+0xB8 ∈ {1,2}` → `referee_collar_%03d` | `/Assets/pes16/model/character/uniform/nocloth/` + `collar_%03d`(collar) + `.fmdl` |
| `shirt_in` 0x7575fa4e9626 | needs `bVar33`, not referee; TightKit (+0xD5 ← struct +0x18) picks `_tight`; visible iff `+0xBC == 0`; cloth variant when `uStack_1d0` (see below) | `nocloth/shirt_in.fmdl` / `shirt_in_tight.fmdl` |
| `shirt_out` 0xb69167e9ca5b | same; visible iff `+0xBC != 0` | `nocloth/shirt_out.fmdl` / `shirt_out_tight.fmdl` |
| `sleeve` 0x1158d590d986 / `sleeve_cap` 0x1efca60661a9 | `FUN_1419637c0(dir, type, model, glove=+0xD6, cap=0/1, tight=+0xD5)` on the primary pair | name rule below |
| `sleeve_alt` 0x335cade007ee / `sleeve_alt_cap` 0x582a734dcac9 (names still unhashed; **identified live in §7.3**: they hold the *secondary* sleeve pair's file, e.g. `sleeve_long_003` / `sleeve_long_cap_003`) | same builder on the **secondary** pair, cap 0/1; only when `+0xD7` | name rule below |
| `socks` 0xf3fdfc929c53 | needs `pantsOn`; `+0xC4`: 0 → `socks_middle`, 1 → `socks_long`, 2 → `socks_short`, 3 → `socks_noguard`, loaded by hash (`FUN_141910c60`) | `common_package.fpk` → `common/socks_*.fmdl` |
| 0xcb3abd43836b, 0xa8363661cd3c, 0x452639daa228, 0x3f1d3df12735 (**live §7.3**: socks_middle / socks_long / socks_short / socks_noguard respectively) | each maps to socks_middle / long / short / noguard when `pantsOn && +0xD8` | same files |
| `pants_in_sub` 0xf51d942457c3 | needs `pantsOn`; visible unless (`+0xBC != 0 && bVar33`) | `common/pants_in_sub.fmdl` (vanilla placeholder) |
| `pants_out_sub` 0x4254cfba25a7 | TightKit → `pants_out_sub_tight`; else if player has item 7/8 (`FUN_141a92b20`) or primary type == 3 → `pants_out_inner_sub`; else `pants_out_sub` | `common/pants_out_*` |
| `cap` 0xa4e48873bd5c | captain (`FUN_1419a5750`), `bVar33`, `+0x1A8 == 0` | `nocloth/cap_sleeve_%s%s.fmdl`: `%s` = `long` if primary type ∈ {1,2} else `short`, plus `_tight` |
| pants (pre-loop) | if `pantsOn`: `FUN_141963120("/…/nocloth/pants.fmdl", ShortsModel)` sets the variant key `pants` → `pants_%03d` (`FUN_141b338f0`) | `nocloth/pants_%03d.fmdl`, number = b3 (0→1) |

**Sleeve name rule — OBS, `FUN_1419637c0`:**
- Start from `"sleeve"`; type 3 replaces it with `"s"`.
- If the result is not "long + glove", append `{_short,_long,_half,_inner}[type]`.
- Then `+_glove` (type 0/2 force glove off), `+_cap`, `+_tight`.
- Then `_%03d(model)`, except long + glove, which drops the suffix and the number.
- Then `.fmdl`.
- Examples: `sleeve_short_001`, `sleeve_long_cap_tight_002`, `s_inner_glove_cap_001`, `sleeve_glove_cap_tight`. All of these exist in the VFS listing.

**Cloth path — OBS:** `uStack_1d0 = (+0xB8 == 0 && !tight && !FUN_141a80400 && !(f bit2 && !bit1)) && FUN_141a7ff50 == 0`. When set, `shirt_in` / `shirt_out` are replaced by cloth objects (`FUN_141a828b0(..., 1 / 0)`). INF (medium): these use `uniform/cloth/#Win/shirt_in.fmdl` / `shirt_out.fmdl` with `*_cloth.skl` and `fcl_*_sim.fclo`.

**Second table** `0x143517c20`: arm, arm_cap, hand_l/r, glove_pl_*, thigh_*, tights_*, spats, taping, wristtaping, inner*, highneck, glasses.

- Arms (OBS, `FUN_141a8d530`):
  - Slot `arm`: the variant choice below runs only when NOT (primary sleeve type ∈ {1,3} (`FUN_141a8c840`) and shirt worn). For `arm_cap` that case explicitly jumps past loading (`goto LAB_141a8dd1e`). **Corrected (§8.9):** for `arm` the load label `LAB_141a8db21` sits inside the other branch, so the long-sleeve case also falls out without loading; "bare arm not loaded under long sleeves" is OBS (control flow), not INF.
  - Otherwise: type 2 → `arm_half`; gloves (+0x9E) → `arm_glove`; else `arm`.
  - Slot `arm_cap`: same, with `arm_glove_cap`. Visibility is tied to +0x9D (INF: captain).
- Thighs (OBS, `FUN_141a91210`, keyed by the socks type): 0 or 3 → `thigh_middle`, 1 → `thigh_long`, 2 → `thigh_short`; `_classic` when +0x58.
- `hand_l` / `hand_r` / `neck`: listed in the tables. Selection logic was not traced (INF, high: always loaded).

---

## 4. Kit textures: where they are and which materials receive them

- **OBS — paths:** `Asset/model/character/uniform/texture/#windx11/<KitFile>.ftex` plus `<KitFile>_srm.ftex` and the four number/name slots.
  - Counts across the VFS: `u####p1` 491, `_back` 475, `_name` 475, `_leg` 465, `_srm` 438, `_chest` 435.
  - There is **no `_nrm`** kit texture.
  - Engine dir string (live, `*DAT_1437cafe8`) = `/Assets/pes16/model/character/uniform/texture/`.
  - `FUN_141963c50` appends `"_srm" + ".ftex"` to KitFile (struct +0x20), but only if the file exists (`FUN_141b33050`).
- **OBS — team 1 / 1st:** `u0001p1` is BC7 (pf 11), 2048², 10 mips. `_srm` is DXT1. `_back` is 2048×256, `_chest` / `_leg` 1024×128, `_name` 1024×256. All come from `dt34_g4.cpk`.
- **OBS — pattern/normal:** dir string `/Assets/pes16/model/character/uniform/common/`.
  - `FUN_141960cb0`: `uni_pattern_mtm%02d(ShirtPattern)` for the shirt; `pants_pattern_mtm%02d(p)` for pants, with p = 0 for pattern 0/5, 1 for 1–4, 2 for 6.
  - `FUN_141960f00`: `uni_pattern_normal%02d(ShirtPattern)`.
  - `FUN_141960a70`: `nocloth/collar_%03d_alp.ftex` (+0x98 or +0x9C by a flag).
- **OBS — material slots in the part fmdls** (`<scratch>/fmdlx.py`, a clean-room section reader):
  - `uni_shirts` (`pes_3ddf_shirt_nb`), `uni_collar` (`pes_3ddf_collar`), `uni_sleeve` (`pes_3ddf_sleeve`: `Pattern_Tex_LIN`, `BadgeL/R_Tex_LIN`), `uni_pants` (`pes_3ddf_pants_nb`: `NormalMap`, `Pattern_Tex_LIN`, `Number_Tex_LIN`), `uni_socks` (`pes_3ddf_uniform`).
  - Every one points at placeholders `common/sourceimages/uni_pattern.dds` / `dummy.tga`, so kit textures are bound at runtime. `FUN_1419640e0` binds by material keys `shirt`, `short_sleeve`, `glove_sleeve`, `socks` with sub-keys `model`, `stain`, `mask`, `detail`, … .
  - The role name that receives the kit atlas is **not resolved**: role names are hashed in the exe.
- **OBS — UVs:** every uniform part has two UV sets (vertex usages 8 = UV0, 9 = UV1, half2).
  - UV0 bounds: shirt [0.34–0.66]×[0.02–0.91], short sleeve [0.24–0.76]×[0.34–0.59], long sleeve [0.00–0.88]×[0.07–0.32], pants [0.02–0.98]×[0.57–0.95], collar [0.33–0.67]×[0.44–0.96].
  - These match the panel layout of `u0001p1` decoded to PNG (`<scratch>/kittex/u0001p1.png`): centre column shirt front/back, top band long sleeves, mid band short-sleeve panels with cuff stripes at u≈0.24/0.76, bottom shorts.
  - **Corrected (§8.5, OBS from the DXBC shaders):** UV0 samples the kit atlas (`Base_Tex_SRGB`), SRM, stain and stain mask for `uni_shirts` / `uni_sleeve` / `uni_collar` / `uni_pants` / `uni_socks`.
  - **Corrected (§8.5):** UV1 samples the normal map (`NormalMap_Tex_NRM`, a texture array in the shirt/pants/sleeve shaders). The pattern array is sampled at UV1 × RepetitionParam for shirts/sleeves and at UV0 × RepetitionParam for socks/pants. The earlier "UV1 = pattern" guess was wrong.
  - **Corrected (§8.5):** socks UV0 is two separate patches, u∈[0.005,0.19] and u∈[0.81,0.995], v∈[0.31,0.56]. These are the left/right side panels of the atlas beside the short-sleeve band. The earlier union bound [0–1] made it look like a mismatch. Socks are textured from the kit atlas through UV0.

---

## 5. Per-player appearance settings

- **OBS:** `FUN_141a83850` fills the per-player model fields from getters on the player appearance object. Each getter is `*(*(obj+0x20)+0x18)+off`; `+0xB8` uses `+0x18` and offset 8.

  | Player field | Source offset | Use |
  |---|---|---|
  | +0xB8 | +8 | referee kind; 1–2 → referee parts |
  | +0xBC | +0x1C | shirt_in vs shirt_out visibility. INF: tucked/untucked |
  | +0xC0 | +0x20 | long-sleeve state: selects sleeve pair and Collar/WinterCollar |
  | +0xC4 | +0x28 | socks type 0–3: socks + thigh variant |
  | +0xD0 | +0x38 | not traced |
  | +0xD4 | +0x14 == 1 | not traced |
  | +0xD6 | +0x34 | gloves (`FUN_141a80010`) |
  | +0xD7 | +0x24 | enables secondary-sleeve slots. INF: undershirt |
  | +0xD8 | +0x2C | enables the 4 extra socks slots and loads all thigh variants |
  | +0x1A8 | `FUN_141a7ffd0` | blocks the captain sleeve (`cap`) when non-zero |

- **Superseded by §7.1** (live). Correction: `common/character0/model/character/appearance/PlayerAppearance.bin` *does* exist in the VFS (dt00_x64 and `4cc_01_base.cpk`, 320,160 B = 5,336 × 60 B); my earlier `allpaths.txt` grep missed it. For 4cc players it holds identical placeholder records; the live values come from the EDIT save (§7.1).

---

## 6. Minimal faithful recipe: team 1, kit 1st, one outfield player

Config: vanilla `UniformParameter.bin` entry `1_DEF_1st_realUni.bin` (== loose `common/character0/model/character/uniform/team/1/1_DEF_1st_realUni.bin`, dt34). Decoded:
- A = 1 → (short, 001); B = 1001 → (long, 001); SleeveLimits 0.
- ShortsModel 2; Collar 58; WinterCollar 58; TightKit 0; ShirtPattern 0; Armband 0.
- All colours #007f56.
- Textures: `u0001p1`, `u0001p1_back`, `u0001p1_chest`, `u0001p1_leg`, `u0001p1_name`.

Player assumption: Player.bin is unread (§5), so every appearance field is set to 0:
- short sleeves, so primary = A and collar = Collar;
- `+0xBC = 0`, so shirt_in;
- socks type 0, so socks/thigh middle;
- no gloves, undershirt or captain;
- cloth off (static render).

| Part | VFS path (inner) | Evidence |
|---|---|---|
| collar | `Asset/model/character/uniform/nocloth/#Win/collar_058.fmdl` + `nocloth/#windx11/collar_058_alp.ftex` | §3 collar slot, `FUN_141960a70` |
| shirt | `nocloth/#Win/shirt_in.fmdl`. ⚠ The VFS winner here is the `4cc_04_fpc` placeholder: use the dt35 vanilla copy or `uniform/cloth/#Win/shirt_in.fmdl` (vanilla). PES itself uses the cloth path for default players (§3) | §0.3, §3 |
| sleeves | `nocloth/#Win/sleeve_short_001.fmdl` | `FUN_1419637c0`(0, 1, 0, 0, 0) |
| shorts | `nocloth/#Win/pants_002.fmdl` | `FUN_141963120`, b3 = 2 |
| under-shorts | `common/pants_in_sub.fmdl` (vanilla placeholder → nothing to draw) | §3 |
| socks | `Asset/model/character/#Win/common_package.fpk` → `/Assets/pes16/model/character/common/socks_middle.fmdl` | socks slot, +0xC4 = 0 |
| thighs | same fpk → `common/thigh_middle.fmdl` | `FUN_141a91210` |
| arms | same fpk → `common/arm.fmdl`; `hand_l.fmdl`, `hand_r.fmdl`, `neck.fmdl` | `FUN_141a8d530`; hands/neck INF |
| kit texture → UV0 of uni_shirts/uni_sleeve/uni_collar/uni_pants | `Asset/model/character/uniform/texture/#windx11/u0001p1.ftex`, `u0001p1_srm.ftex` | §4 |
| pattern → UV1 (INF) | `Asset/model/character/uniform/common/#windx11/uni_pattern_mtm00.ftex`, `uni_pattern_normal00.ftex`, `pants_pattern_mtm00.ftex` | `FUN_141960cb0` / `FUN_141960f00` |
| numbers / name | `u0001p1_back`, `u0001p1_chest`, `u0001p1_leg`, `u0001p1_name` `.ftex`, placed by §1 Number/Name fields | §1 |
| head / skin | face pack (`face/real/<id>/#Win/face.fpk`), outside this report | — |

Caveat for this install: with the 4cc `UniformParameter.bin`, PES itself would not find team 1 and would fall back to the default team (§0.3). The recipe above is what vanilla PES assembles.

---

## Open items (tried / not resolved)

- Secondary-sleeve slot names 0x335cade007ee / 0x582a734dcac9: their contents were identified live (§7.3), but the literal names are still unhashed.
- Socks UV: resolved in §8.5 (UV0, kit atlas).
- b0x13, d8 bits 25–31, d9 bits 0–4: no reader found in `FUN_141ea3190`.
- Texture parameter 0x16f09b71 = `Marking_Tex_LIN` and 0x7bcbb6cb = `Dirty_Tex_LIN` (§8.8). Path code 0xb8a0bf169f98 is the engine's "no path" value: `FUN_14022dfa0` tests a path against exactly this code, and `FUN_14022dfc0` maps it to an empty string. So "engine default" in §7.2 means *nothing bound*.
- The `collar_%03d_alp` flag polarity in `FUN_141960a70` (+0x9C when flag 0, else +0x98).

---

## 7. Live findings (this install)

### 7.0 How this was read, and the game state

- **Target:** live `PES2021.exe`, pid <pid>, under Wine (Heroic prefix). Memory was read **only** via `sudo` reads of `/proc/<pid>/mem`; nothing was written and no input was sent. Scripts are `<scratch>/live.py`, `<scratch>/snap.py`, `<scratch>/fullsnap.py` and `<scratch>/mm2.py`.
- **Class identification:** RTTI names from the dumped image. A Complete Object Locator sits at vtable−8 and points to a TypeDescriptor whose name starts at +0x10.
- **Instance discovery:** heap scans for vtable pointers.

  | Class | Vtable |
  |---|---|
  | `cobra::rd::UniformUnit` | 0x142aad438 |
  | `fox::gr::Model` | 0x1429ff000 |
  | `fox::gr::Texture` | 0x1429fad90 |
  | `fox::gr::MaterialModifier2` | 0x142a19150 |

- **Path-code resolution:** in-game resources carry Fox path codes, resolved against `allpaths.txt` plus every `.fpk` member under `Asset/model/character/` (13,089 members; `<scratch>/fpk_inner.tsv`). The code is `StrCode64(path without extension) & 0xFFFFFFFFFFFF` in the low 48 bits; the high bits are the extension id (0x330 = fmdl). 4cc face/boots fpks store bare member names, so their models hash the bare stem (`strcode("face_high")`); the owning fpk comes from the model's `BlockPackage` (FmdlFile+0x38 → BlockPackage+0x08 = fpk path code).
- **sider is NOT loaded in this session:** `sider.dll` is not in `/proc/<pid>/maps` and `sider.log` is from 2025. So livecpk roots and kserv are inactive for what the game shows now; only Data cpks and download cpks apply.
- **State 1 (≈19:1x local), a match:** 25 `UniformUnit`s, i.e. 22 players + 3 referees.
  - uid 0 = team **769**, kit `u0769p2` (2nd); uid 1 = `u0769g1`.
  - uid 2 = team **739**, kit `u0739p2` (2nd); uid 3 = `u0739g1`.
  - uid 4 = `referee_1`.
  - Heap held 1,094 `Model`s.
- **State 2 (≈19:45–20:00), a 2-model preview screen:** 2 `UniformUnit`s, kits 725 1st (uid 0) and 767 1st (uid 2). INF (high): pre-match/menu preview, because both units have the "preview" flags +0xD7 = +0xD8 = 1 and load all six thigh variants. Unit addresses changed between reads; every read below is from one snapshot.

### 7.1 Per-player appearance (priority 1)

**Object chain (OBS):** `UniformUnit` (vtable 0x142aad438; +0x30 → `cobra::rd::property::Player`).
- `Player+0x20 → Property; +0x18 →` the `Holder<PlayerProperty>` object, called **A** below. This is what `FUN_141a83850` reads.
- `Player+0x18 → Property; +0x18 →` the `Holder<SystemProperty>` object, called **F**.
- `A+0x08` → a fox string whose chars are the shirt print name.

Fields (u32 unless marked). Cross-checked against the unit copy in `FUN_141a83850` on all 25 units.

| A offset | → UniformUnit | Meaning | Evidence |
|---|---|---|---|
| +0x10 | +0x1AC | shirt number | INF (high): values 1 (GK), 4, 10, 13, 89 … |
| +0x14 | +0xD4 (`==1`) | captain | INF (high): exactly one player per team = 1 (76902 for 769, 73909 for 739). Not present in the appearance record |
| +0x18 | +0x6C | uniform slot uid: 0/1 home outfield/GK, 2/3 away outfield/GK, 4 referee | OBS (registry, §7.2) |
| +0x1C | +0xBC | shirt tail out (1 → `shirt_out` drawn, 0 → `shirt_in`) | OBS |
| +0x20 | +0xC0 | long sleeves (1) | OBS |
| +0x24 (u8) | +0xD7 | enable secondary-sleeve slots | OBS: 0 in match, 1 in preview |
| +0x28 | +0xC4 | socks type 0–3 | OBS |
| +0x2C (u8) | +0xD8 | enable all socks/thigh variants | OBS: 0 in match, 1 in preview |
| +0x30 | +0x70 | always 1 | OBS, meaning unknown |
| +0x34 | +0xD6 (≠0) | **glove id** | OBS: 0, 1, 4, 12, 1052–1061 |
| +0x38 | +0xD0 | 0 for players, 2 for referees | OBS |
| +0x3C (u8), +0x40 | — | 0 for players; +0x40 = 2 for referees | OBS |
| F+0x08 | +0xB8 | 0 player, 1/2 referee kind | OBS |
| F+0x0C | flags `f` | 0xFFFFFFFF for every player | OBS |

**Source record (OBS):** a live 72-byte-per-player appearance table at 0x7fdc0920. It has 5,336 records in id order (70101…93223), found by scanning for consecutive player ids at a fixed stride. Record layout:
- `u32 playerId` (== face id for 4cc players).
- `u32 X`: bits 4–17 = boots id, bits 18–31 = glove id; bits 0–3 unknown (0x4 / 0xC / 0xF).
- `u32 0`.
- 56 bytes = the `PlayerAppearance.bin` body (the file record minus its id).
- `u32 0`.

In that body, the u16 at record offset 20 (= PlayerAppearance.bin record offset 12) holds:
- bits 6–7 = **sleeves** (1 short, 2 long → A+0x20 = 1 iff 2);
- bits 10–11 = **socks** (= A+0x28);
- bits 14–15 = **shirt tail** (= A+0x1C);
- bits 8–9 and 12–13 not identified.

Validation: all 24 named players of the State-1 match matched on (sleeves, socks, tail, glove) = **24/24** (script output in session). The decoded `X` boots id matches the 4cc boots folders that exist for these teams, e.g. 1060 → `boots/k1060`, 1806 → `boots/k1806`.

**Where the values come from (OBS):**
- The in-memory copy of `PlayerAppearance.bin` (60-byte records at 0x2c000010, 5,336 records) is byte-identical to the VFS file (`4cc_01_base.cpk`). In it, every 4cc player has the same placeholder record (`… 40 40 00 02 …` = short sleeves, socks 0, tail out).
- `4cc_01_base` `Player.bin` is likewise `PLACEHOLDER` names.
- The real names and appearance live only in the decrypted **EDIT save**: the live player table at 0x45770040 (stride 0x17C, 4,830 players; real names present, not reproduced here) and the 72-byte table above.
- On disk the save is `~/Documents/KONAMI/eFootball PES 2021 SEASON UPDATE/<steamid>/save/EDIT00000000` (11,011,039 B, dated 2022-10-22). The prefix `Documents` folder is a symlink to `~/Documents`. **Update (§8.1):** the cipher is now reimplemented and the file was decrypted into scratch; the 72-byte records map 1:1 to it.
- **Consequence:** per-player sleeves/socks/tail/glove/boots for this install come from the decrypted EDIT00000000 (§8.1). They are not in any cpk.

### 7.2 Kit atlas binding (priority 2)

**Method (OBS):** `MaterialModifier2` instances (71 live).
- +0x10 = StrCode32 of the material name (`uni_shirts` = 0xa4a25931, `uni_collar` = 0x914b88f7, …).
- +0x20/+0x28 = parameter table of 8-byte entries `{u32 StrCode32(param), u8 slot, 0, 0, u8 kind}`; kind 0xC0 = texture, 0xA0 = vector.
- +0x40/+0x48 = texture slot array of `{u16 1, u16 textureIndex}`.
- `fox::gr::Texture` objects carry +0x10 = path code and +0x22 = u16 index, so each slot resolves to a file.

Param names were brute-forced as StrCode32: `Base_Tex_SRGB` 0x7be81b61, `NormalMap_Tex_NRM` 0x05511ae0, `SpecularMap_Tex_LIN` 0x6b98b10e, `Stain_Tex_LIN` 0xcbe9de46, `StainMask_Tex_LIN` 0x87143ee6, `Badge_Tex_LIN` 0xc45cb7b2, `Number_Tex_LIN`. **Resolved in §8.8:** 0x16f09b71 = `Marking_Tex_LIN`, 0x7bcbb6cb = `Dirty_Tex_LIN`.

Live bindings (State 2, kits `u0725p1` / `u0767p1`; full dump `<scratch>/mm2_2.json`):

| Material | Base_Tex_SRGB | Other textures |
|---|---|---|
| `uni_shirts`, `uni_collar` | **`uniform/texture/u####pN`** | Normal `common/sourceimages/shirts_normal`; Stain `uni_stain_g`; StainMask `uni_stain_mask_g`; Specular + `Marking_Tex_LIN` (0x16f09b71) = null path 0xb8a0bf169f98, i.e. nothing bound (§8.8) |
| `uni_sleeve` | **kit atlas** | same + Badge = default |
| `uni_pants`, `uni_pants.001` | **kit atlas** | Number = default (State 2), Stain/StainMask |
| `uni_socks` | **kit atlas** | Number, Stain/StainMask |
| `basic_fx` (the 4cc placeholder stubs) | kit atlas too | — |
| `arm_mat` | `common/sourceimages/arm_1_bsm`, **or the face folder's `face/real/<id>/sourceimages/arm_bsm`** when present (72503) | `Dirty_Tex_LIN` (0x7bcbb6cb) = `skin_dtm` |
| `thigh_mat` | `common/sourceimages/thigh_1_bsm` | `Dirty_Tex_LIN` = `skin_dtm` |

Notes:
- **Socks get their colour from the kit atlas** (OBS). The fmdl-stored `Pattern_Tex_LIN` roles are replaced at runtime; no `uni_pattern*` texture was bound in State 2 (pattern 4/6 kits).
- `u0767p1_srm` does not exist in the VFS. That is consistent with Specular = the engine default (INF high).

### 7.3 What a 4cc-league player actually loads (priority 3)

**Per-character draw list (OBS):** `fox::gr::Model` objects of one character form a doubly linked list via Model+0x10 (prev) / +0x18 (next). Each Model has +0x24 = 1 (INF: attached to the draw list) and +0x118 → `FmdlFile` (+0x28 = path code). The **UniformUnit part list** is at +0x38..+0x40, with classes `GeneralParts` / `ClothShirtParts`: +0x20 Model, +0x40 category, +0x48 slot hash.

**`ClothShirtParts` (OBS):** each holds two models.
- +0x20 → `uniform/nocloth/shirt_{in,out}.fmdl` — the one in the draw list.
- +0x78 → `uniform/cloth/shirt_{in,out}.fmdl` — not in the draw list in State 2.
- State 1 had 22 of each of the four.

**The placeholder stubs:** the "1,218-byte" models used below (`pants_016`, `collar_105`, `nocloth/shirt_in`, `sleeve_long_00x` from `4cc_04_fpc`) and the vanilla `socks_short` / `thigh_short` / `pants_in_sub` are single degenerate triangles at y = −1.6, so they draw nothing (OBS, §4 extent dump).

**State 2, player 76711** (team 767 1st, uid 2; short sleeves, socks 0, tail out, no glove/boots id). Draw list, in order:

```
uniform/nocloth/pants_016            (4cc_04_fpc stub → empty)        [ShortsModel 16]
common/shadow, common/mouth, common/eye_new                           (common_package.fpk)
hair_high, face_high                 (face/real/76711/#Win/face.fpk)
common/arm_cap, common/arm           (common_package.fpk)
uniform/nocloth/collar_105           (4cc_04_fpc stub → empty)        [Collar 105]
uniform/nocloth/shirt_out            (4cc_04_fpc, 106,544 B, has its own uni_collar mesh)
uniform/nocloth/sleeve_short_001     (dt35, vanilla)                  [code A = (0,1)]
common/socks_middle, common/pants_out_sub                             [socks 0]
common/thigh_{middle,long,short}{,_classic}  (all six: preview flag +0xD8)
common/hand_l, common/hand_r
boots.fmdl                           (boots/k0062/#Win/boots.fpk; boots id 0 → BootsList.bin lookup by player id → list default 62, OBS §8.3)
```

Part slots also loaded but not in the draw list (preview only): `shirt_in` (stub), the secondary pair 0x335cade007ee → `sleeve_long_003` (stub) and 0x582a734dcac9 → `sleeve_long_cap_003` (stub), and the extra socks slots 0xcb3a…/0xa836…/0x4526…/0x3f1d… → socks_middle/long/short/noguard.

**State 2, player 72503** (team 725 1st, uid 0; long sleeves, socks 2, tail in):
- Draw list is the same structure, but every uniform model is a stub: `pants_016`, `collar_105`, `nocloth/shirt_in`, `sleeve_long_003`, `socks_short`, `pants_in_sub`.
- The face pack `face/real/72503/face.fpk` has only `hair_high.fmdl`. Mesh 0 is an 8-vertex card spanning y 0.018–2.196, textured `face_bsm_alp` (a 512² photo cut-out with alpha). The other meshes are degenerate.
- The face folder's `arm_bsm.ftex` is a **4×4 fully transparent** texture. The engine binds it to `arm_mat` (arms and hands), so the bare arms disappear.
- Visible result (INF high): a flat 2D cut-out plus the thighs (`thigh_1_bsm`) and boots.

**State 1 (match), from the global Model census:** team 739 (all-car team).
- Face packs carry `oral.fmdl`: car meshes, ~120k verts, material `kit` → `u0739p0.dds`.
- Boots `k1052–k1061` carry car parts with material `metal_kit` → `u0739p0.dds`, plus `u0739p0..p3.ftex` beside them.
- The uniform side is all stubs (`pants_016`, `collar_105`, `sleeve_long_003` stubs).
- **UNRESOLVED:** the `u0739p0` → current-kit (`p2`) texture swap. No team-739 player was loaded during the texture-binding reads, so it is unobserved. The exe has no `u%04dp%d`-style format except `u%04dp1` (GameModeMatchListener, `FUN_141fb4b70`).

**Composition rule (OBS + INF):** the 4cc "kit transparency" here is **not** alpha in the kit atlas: the kit atlases are opaque (e.g. `u0739p2`, `u0769p2`, `u0725p1` all have alpha min = max = 1). It is:
1. a kit config pointing at stub parts (ShortsModel 16, Collar 105, long code (1,3)) that resolve to degenerate meshes;
2. EDIT-save appearance (long sleeves, gloves 1052–1061, boots 1052–1061/1801–1811) choosing 4cc glove/boots fpks that carry geometry;
3. face packs (`face_high`/`hair_high`/`oral`) carrying the head, and for some teams the whole body;
4. face-folder `arm_bsm.ftex` overriding, or blanking, the arm skin.

Teams with human players (767) keep vanilla sleeves, socks and `pants_out_sub` textured with the kit atlas.

### 7.4 Recipe: one currently loaded 4cc player — 76711, team 767, kit 1st (outfield)

| Item | VFS source | Evidence |
|---|---|---|
| Kit config | `common/character0/model/character/uniform/team/UniformParameter.bin` (VFS winner `4cc_69_midcup.cpk`), entry `767_DEF_1st_realUni.bin`: A (0,1), B (1,3), ShortsModel 16, Collar 105/105, ShirtPattern 6, TightKit 0, KitFile `u0767p1` (+`_back/_chest/_leg/_name`) | live `UniformImpl` U+0x0C=16, +0x14=6, +0x20 `u0767p1`, +0x98/9C=105, +0xA0..AC=(0,1),(1,3) |
| Appearance | EDIT save: sleeves 1 (short), socks 0, tail 1 (out), glove 0, boots 0, shirt no. 11 | §7.1 table 0x7fdc0920 record 76711; A fields |
| Head | `Asset/model/character/face/real/76711/#Win/face.fpk` → `face_high.fmdl` (`face.ftex`, `face_nrm`, `face_srm`, `face_trm`) and `hair_high.fmdl` (`head.ftex`, …), textures in `…/76711/sourceimages/#windx11/` | draw list |
| Eyes / mouth | `Asset/model/character/#Win/common_package.fpk` → `common/eye_new.fmdl`, `common/mouth.fmdl` | draw list |
| Arms / hands | same fpk → `common/arm.fmdl`, `common/arm_cap.fmdl`, `common/hand_l.fmdl`, `common/hand_r.fmdl`; `arm_mat` Base = `Asset/model/character/common/sourceimages/#windx11/arm_1_bsm.ftex` (76711 has no face-folder `arm_bsm`) | draw list + §7.2 |
| Shirt | `Asset/model/character/uniform/nocloth/#Win/shirt_out.fmdl` (VFS winner `4cc_04_fpc`; carries `uni_shirts` + `uni_collar`) | tail = 1 → shirt_out |
| Sleeves | `Asset/model/character/uniform/nocloth/#Win/sleeve_short_001.fmdl` (dt35) | code A |
| Shorts | `common_package.fpk` → `common/pants_out_sub.fmdl` (both `uni_pants` meshes). `uniform/nocloth/#Win/pants_016.fmdl` is a stub: skip or load (it draws nothing) | draw list |
| Collar | `uniform/nocloth/#Win/collar_105.fmdl` is a stub. The collar comes from `shirt_out`'s `uni_collar` mesh | draw list |
| Socks / thighs | `common_package.fpk` → `common/socks_middle.fmdl`, `common/thigh_middle.fmdl` (socks 0; thigh mapping per §3 `FUN_141a91210`); `thigh_mat` Base = `common/sourceimages/#windx11/thigh_1_bsm.ftex` | draw list (preview loads all six thighs; a match loads one, INF) |
| Boots | `Asset/model/character/boots/k0062/#Win/boots.fpk` → `boots.fmdl` (appearance boots id 0 → `BootsList.bin` has no entry for this player → list default 62, §8.3) | draw list |
| Kit textures | `Asset/model/character/uniform/texture/#windx11/u0767p1.ftex` → `Base_Tex_SRGB` of `uni_shirts`, `uni_collar`, `uni_sleeve`, `uni_pants(.001)`, `uni_socks`; Normal `Asset/model/character/common/sourceimages/#windx11/shirts_normal.ftex`; Stain `…/uni_stain_g.ftex`, `…/uni_stain_mask_g.ftex`; Specular = engine default (no `u0767p1_srm`) | §7.2 |
| Numbers / name | `u0767p1_back`, `_chest`, `_leg`, `_name` `.ftex`, placed by the §1 fields of the config entry | §1 |
| Skip | `common/shadow.fmdl` (shadow caster) | draw list |

Remaining live gaps (status after §8):
- socks UV set: **closed**, UV0 (§8.5);
- the `u####p0` → `pN` swap in 4cc face/boots packs: still unobserved live; statically there is no swap rule (§8.6);
- EDIT00000000 decryption: **closed** (§8.1).
