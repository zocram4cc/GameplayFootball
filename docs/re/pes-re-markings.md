# PES 2021 uniform markings: numbers, names, captain armband, badges, GK and referee kits

Scope: what a uniform shows beyond the kit atlas. This report continues `pes-kit-assembly.md` (§1 realUni layout, §3 part slots, §8.7 shader placement math) and `pes-re-shaders.md` §3.1 (marking-atlas sampling). Read those first: this report reuses their names (`kit+0xNN` = kit struct `*(kit+8)+0xNN`, unit = `cobra::rd::UniformUnit`).

Legend: **OBS** = observed (decompile of `<scratch>/pesmem.bin`, VFS bytes, or live `/proc/<pid>/mem` read); **INF** = inferred, with confidence.
Method: headless Ghidra decompiles on a private copy of the small project (`-noanalysis -readOnly`). Outputs are in `<scratch>/MarkingRE/out/*.txt`. Live reads were read-only `/proc/<pid>/mem` during a loaded match (25 units: 22 players + 3 referees). Nothing was written to the process.

---

## 0. Headline

1. **OBS. Numbers and name are drawn at runtime into a per-player texture.** No file holds the composed marking. Per player, `FUN_141a82a70` → `FUN_1419e3c60` renders a **1600×256** render target with four regions: the name, the back number, the chest number and the chest badge.
   - The CPU reads the target back, builds 2 extra mips on the CPU, encodes it as fox format `0x4002` (BC3-class; 1 byte/pixel), and creates a texture from memory.
   - The result is bound as `Marking_Tex_LIN` on `uni_shirts` and `uni_collar`.
   - The leg number gets its own **128×128** texture from `FUN_141a829c0` → `FUN_1419e3a30`, bound as `Number_Tex_LIN` on the pants.
   - Live check: the textures had the expected sizes (§6).
2. **OBS. Glyphs are cut from the kit's `_back`, `_chest`, `_leg`, `_name` and `_name_ex` ftex.** Each source rectangle comes from fixed tables in the exe:
   - digits: one 10-cell strip, horizontal or vertical (§2.1);
   - name: a 78-entry glyph table on two pages (§3.1).
3. **OBS. Captain armband: the normal armband is in the kit atlas.** The panel at u∈[0,0.25], v∈[0,0.0625] of `u####pN`/`g#` is the armband. It is drawn through the `sleeve_*_cap_*` sleeve variants, which are visible only for the captain.
   - The `CL_captainmark_%02d` textures apply only to competition id `0x21` (Champions League).
   - They are drawn on a separate `cap_sleeve_*.fmdl` (part slot `cap`, category 5).
4. **OBS. Referees get no markings and no captain parts.** The `referee_collar_%03d` model is the whole referee torso.
   - The referee kit number (`referee_<TAG><n>.bin`) is the one whose colour contrasts most with both teams' colours. The contrast is a CIELAB ΔE score from `RefereeColor.bin`.

---

## 1. Data flow (OBS)

```
realUni parser FUN_141ea3190 (kit config, §1 of kit report)
  kit+0x20 KitFile, +0x38 NameFontFile, +0x50 BackNumbersFile, +0x68 ChestNumbersFile, +0x80 LegNumbersFile
  kit+0x40 NameY, +0x44 NameShape(d7 14-16), +0x48 NameSize, +0x4C NameStretch
  kit+0x58 BackSize, +0x5C BackY, +0x60 BackNumberSpacing  (FUN_141e58c80: v/14 for d6 bits 10-13)
  kit+0x94 BackNumberType (d7 bit 2, FUN_1419659a0)
  kit+0xC8 captain-mark texture name (only competition 0x21, §4)
kit resource loader FUN_1419a6860 → kit texture node (value struct V):
  V+0xB8 atlas  V+0xB0 _srm  V+0xD0 back  V+0xD8 chest  V+0xE0 leg  V+0xE8 name
  V+0xF0 name with ".ftex"→"_ex.ftex"  (null if the file is absent)
  V+0xF8 captain mark "/Assets/pes16/model/character/uniform/cap/<kit+0xC8>.ftex"; V+0x198 = 1 if it loaded
  getters: FUN_1419a55c0(kit, idx) reads V+0x88+8*idx (idx 6 atlas, 9 back, 0xA chest, 0xB leg, 0xC name, 0xD name_ex, 0xE captain)
unit build FUN_141a89120 (after all parts are created):
  if unit+0xB8 == 0 (not a referee): FUN_141a82a70 (shirt marking job → unit+0x1B0), FUN_141a829c0 (leg job → unit+0x1B8)
  each frame: when the job is done (state 5), its texture → unit+0x1C0 (marking) / unit+0x1C8 (leg)
  then FUN_141a86310 binds Marking_Tex_LIN + FNumber/BNumber/NameScaleXParam + BadgeMatrix0/1 on category-1 parts (shirt)
       FUN_141a85fe0 binds Number_Tex_LIN + NumberScaleXParam on category-3 parts (pants)
```

Unit fields used here (OBS, live and decompile):

| Field | Meaning |
|---|---|
| `+0x1A0` | fox string: the print name, already converted by `FUN_141ea0f50` (§3.2) |
| `+0x1A8` | referee kind (0 player, 1/2 referee). Non-zero → marking params are nulled |
| `+0x1AC` | shirt number (i32). `< 0` → back and chest textures are nulled (no number). `FUN_141a96d50`, the menu/replica path, forces −1 |
| `+0xD4` | captain flag (u8). Live: one captain per team |

The textures are shared per kit; only the composed texture is per player.

---

## 2. Numbers

### 2.1 Source glyph strip (digit cell grid) — OBS `FUN_1419e4a90(&u,&v,&w,&adj, digit, vertical)`

`vertical = (texture width < height)`: the engine tests the bound texture's dimensions with `FUN_14167e1d0`/`FUN_14167e190`. Both layouts occur in the VFS:
- horizontal: `_back` 2048×256 ×1357, `_chest`/`_leg` 1024×128;
- vertical (4cc packs): 128×2048 ×289 (back), 64×1024, and so on.

| Layout | u0 | v0 | w (width in UV) | h | adj (centring) |
|---|---|---|---|---|---|
| horizontal | `d·0.09765625 − (d>1 ? 0.029296875 : 0)` | 0 | `d==1 ? 0.048828125 : 0.078125` | 1.0 | `d==1 ? 0.1875 : 0` |
| vertical | 0 | `d·0.099609375` | `d==1 ? 0.75 : 1.0` | 0.099609375 | `d==1 ? 0.15625 : 0` |

In 2048-px units, horizontal cells are:
- `0` → [0,160), `1` → [200,300), `2` → [340,500);
- in general, digit d ≥ 2 → [200d−60, 200d+100).

This matches the shipped strips (OBS, `u0001p1_back.ftex` decoded and examined: ten digits 0–9 left to right). In the vertical layout, cell pitch and height are both 204/2048.

**Clarification (OBS, MarkingPixelsRE-2, raw disassembly of `FUN_1419e4a90`):** the function writes only `u, v, w, adj` (four outputs); **`h` is not one of them.** The caller `FUN_1419e2c10` supplies `h = 0.099609375` (vertical) or `1.0` (horizontal) itself, chosen by the same width/height test, at 0x1419e2dfa/0x1419e2e05. The `u0`/`adj` zero cases are written as 32-bit integers (bit-identical to float 0). Exact float bit patterns: `0.09765625` = 3dc80000, `0.029296875` = 3cf00000, `0.048828125` = 3d480000, `0.078125` = 3da00000, `0.1875` = 3e400000, `0.099609375` = 3dcc0000, `0.75` = 3f400000, `0.15625` = 3e200000 — all exact binaries of 200/2048, 60/2048, 100/2048, 160/2048, 384/2048, 204/2048, 3/4, 320/2048.

**Strip census (OBS, MarkingPixelsRE-2, VFS listing of all 1672 kit prefixes in `Asset/model/character/uniform/texture/#windx11`):** `_back` 1671, `_chest` 1431, `_leg` 1593, `_name` 1670, `_name_ex` 29. So 241 kits have **no** chest strip, 79 no leg strip, 2 no name strip, 1 no back strip — the compositor must treat an absent file as "draw nothing", not "draw a blank glyph". Examples of absent strips: `u0758p4` (no `_back`), `u0704p*`/`u0705p*` (no `_chest`), `u0714p*`/`u0707p1` (no `_leg`), `u0756p2`/`u0756p3` (no `_name`).

### 2.2 Layout of a number inside its region — OBS `FUN_1419e2c10`

Arguments:
- `rt`; source texture; number n; spacing `s`; `zeroPad`;
- region `(x0, y0, rw, rh)` in RT-normalised units, RT size `(W, H)`;
- `rotate`; padding `pad` (pre-divided: `pad ← pad/rh`); colour.

Rules:
- n < 1 or n ≥ 1000 → nothing is drawn.
- Let `f = 0.08·(s−1)` (= `(0.16 s − 0.16)/2`).
- All x below are fractions of the region width. Every digit gets y = `pad/2/H`, h = `(H−pad)/H` (fractions of `rh`).
- Constants: `a = 0.0520833` (1/19.2), `b = 0.1041667` (2a), `a' = 0.0520867`.

**One digit (n<10 and `zeroPad==0`):** x = `0.25 + adj/2`, width = `0.5 − adj`.

**Two digits (10–99, or n<10 with `zeroPad`, which draws "0d"):**
- tens at x = `oT`, width `0.5 − adjT`;
- units at x = `0.5 + oU`, width `0.5 − adjU`.

| case | oT | oU |
|---|---|---|
| n == 11 | b − f | f |
| units == 1 | a − f | f + a |
| tens == 1 | a − f | f − a |
| else | −f | f |

**Three digits (100–999):**
- hundreds at `adjH/2 + oH`, width `1/3 − adjH`;
- tens at `1/3 + adjT/2 + oT`, width `1/3 − adjT`;
- units at `2/3 + adjU/2 + oU`, width `1/3 − adjU`.

| tens | units | hundreds==1 | oH | oT | oU |
|---|---|---|---|---|---|
| 1 | 1 | yes | b−f | 0 | f−b |
| 1 | 1 | no | b−f | a | f−a |
| 1 | ≠1 | yes | a−f | −a | f−b |
| 1 | ≠1 | no | a−f | 0 | f−a |
| ≠1 | 1 | yes | a−f | 0 | f−a |
| ≠1 | 1 | no | a−f | a' | f |
| ≠1 | ≠1 | yes | −f | −a | f−a |
| ≠1 | ≠1 | no | −f | 0 | f |

**Clarification (OBS, MarkingPixelsRE-2, raw disassembly 0x1419e3372–0x1419e398b):** the `hundreds==1` column never changes `oH` — the decompiled branches collapse to `b−f` when `tens==1 && units==1`, `a−f` in every other case except `tens!=1 && units!=1` (where it is `−f` regardless of the hundreds digit). The table is correct; it just implies a distinction that the code does not make. Exact constants (float bit patterns at their image VAs): `a = 0x142a96c00 = 3d555555` (the float nearest 1/19.2 = 0.0520833), `a' = 0x142a96c04 = 3d5558d4` (0.0520867 — **different from `a`**, and used only in the `oU` slot of the `tens!=1 && units==1` row), `b = 0x142a96c0c = 3dd55555` (0.1041667 = 2a exactly), `−a = 0x142a96c1c = 0xbd555555`. The `W/3` divisor is the float `3.0f` (0x14258c264 = 40400000, a `divss`), whereas `W/4` and `W/2` are **integer** shifts (`(W>>1) − (W>>2)` and `W>>1`) converted to float — a CPU rasteriser must reproduce both. `f` is computed as `((s·0.16f) − 0.16f)·0.5f` (0.16f = 3e23d70a, 0.5f = 3f000000), not `0.08·(s−1)` directly.

**`rotate == 1` (leg number):** x and y swap roles. Digits run along v, with the offsets scaled by `rw`, and the quad UVs are rotated 90° (`func 0x1419c0290(…, 1)`).
  - **Exact quad (OBS, raw disassembly 0x1419e2ecb–0x1419e2f17, render-verified):** for a digit with cell `(u, v, w, h)` and `adj`, with `pad' = pad/rh`:
    - `x = x0 + (pad'/2)/W · rw`, `y = y0 + (base + adj/2) · rh` (base = 0.25 for one digit, the `oT`/`oU`/`oH` table values otherwise),
    - `w = (H − pad')/W · rw`, `h = (step − adj) · rh` (step = 0.5 for 1–2 digits, 1/3 for 3).
  - **Quad UV mode 1 = `U = fy`, `V = 1 − fx`** (OBS, the permutation table at `0x142a91cd8` is `(1,3,0,2)`; mode 0 = identity `U=fx,V=fy`, mode 2 = `U=1−fy,V=fx`, mode 3 = `U=1−fx,V=1−fy`). Net effect: strip-u runs **down** the quad's x axis and strip-v runs **right-to-left** along y — because the vertical strip's glyphs are pre-rotated clockwise and cropped to the cell height (ink 178–180 of a 204.8-px cell, OBS measured), the leg number appears rotated **90° clockwise** (glyph top → +x). This supersedes any "direction unknown" reading: the render `leg3_10.png` shows upright digits.
  - **`src/gdext/pes_marking.cpp:98` gets this wrong** — it blits with `angle = +M_PI/2`, which yields the mirrored digit, and it passes the vertical stretch as the quad's width and the digit width as its height (both swapped). See `docs/re/pes-re-marking-pixels.md` §4 and §7.1.

**Passes:** draws twice, blend mode 4 then mode 2 (`func_0x1416f0890`), because the RT descriptor flag bit1 is always set. Vertex colour = `0xFF<<24 | rgb` (§2.4).

### 2.3 Region of each number in the RT — OBS `FUN_1419e3c60` and `FUN_1419e3a30`

| Number | RT | Region (x0, y0, rw, rh) | pad | rotate | spacing `s` | zeroPad |
|---|---|---|---|---|---|---|
| back | 1600×256 | (0.64, 0, 0.24, 1.0) | 0 | 0 | `kit+0x60` (real kits; 1.0 for edit kits) | `kit+0x94` |
| chest (front) | 1600×256 | (0.88, 0, 0.12, 0.5) | 26/0.5 | 0 | same | same |
| leg | 128×128 | (0, 0, 1, 1) | 26 | **1** | same | same |

The shader regions for name, back, front and badge match `pes-re-shaders.md` §3.1, OBS consistent. Placement on the garment = §8.7 of the kit report.

Vanilla `UniformParameter.bin` (2174 entries, OBS):
- BackNumberType (d7 bit 2) is always 0, so zero-padding never occurs in vanilla.
- Spacing values 0–14, mode 7.

### 2.4 Colour

OBS: for real kits (`kit+8 == 0`) the vertex colour is white `FF FF FF`, so glyph colours come from the ftex as-is. For edit kits (`kit+8 ∈ {1,2}`) the colours come from the edit-uniform struct:
- name `+0x90..92`;
- back `+0x98..9a`;
- chest `+0xa0..a2`;
- leg `FUN_141963440`.

The edit-kit fonts `uniform/edit/font/name%02d`, `nb%02da` (back), `nb%02db` (chest/leg) are, in this install's vanilla dt32, **blank** (RGB 255, alpha 0 everywhere; OBS decoded). INF (medium): edit-kit markings are not shown in PES21.

---

## 3. Name

### 3.1 Glyph table — OBS `DAT_142a96ce0`

78 entries of 16 bytes `{i32 code, i32 page, u16 x, u16 y, u16 w, u16 h}`, read by `FUN_1419e4940`/`FUN_1419e4a20`:
- code = UTF-8 bytes packed big-endian (`'É'` = 0xC389);
- page 0 = `<NameFontFile>.ftex` (1024×256), so UV = (x/1024, y/256, w/1024, h/256);
- page 1 = `<NameFontFile>_ex.ftex`, with v divided by **512** (1024×512 page);
- all glyphs are 124 px tall.

Page 0 (row y=2): A 8/72, B 94/60, C 168/64, D 248/64, E 324/60, F 388/60, G 456/64, H 532/64, I 600/40, J 648/52, K 710/64, L 784/56, M 850/84, N 940/72 (format x/w).
Page 0 (row y=130): O 8/72, P 94/60, Q 168/72, R 256/64, S 334/60, T 408/64, U 484/64, V 560/72, W 640/96, X 744/64, Y 824/64, Z 904/64, `.` 980/28, space 1012/12.
Also `_` 0,0/4 (width 4 only).

Page 1 (four rows y=2,130,258,386), in order:
`' - À Á Â Ã Ä Å Ă Æ Ç Ć Č Ð Đ È É Ê Ë Ę Ə Ğ Ì Í Î Ï İ Ł Ñ Ò Ó Ô Õ Ö Ø Ř Ş Š ß Ţ Þ Ù Ú Û Ü Ý Ż Ž Ą`.
The full x/w list is in `<scratch>/MarkingRE` (dump command in §7). It is read from the exe at runtime; do not transcribe it.

This matches the shipped page-0 textures (OBS, `u0001p1_name` and the 4cc `u0767p1_name` 512×128 examined: A–N, then O–Z and `.`).
- 4cc half-size pages (512×128) work because the table's UVs are normalised. INF (high).
- VFS sizes: `_name` 1024×256 ×1374, 512×128 ×293; `_name_ex` exists for 29 kits.

### 3.2 String → glyph codes (OBS)

1. **At character build** (`FUN_141ec4560` → `FUN_141ea0f50(kitId, out, printName)`): each UTF-8 code point is kept if it is in that team's allow-list in `UniNameFontPermissions.bin`. Otherwise it is replaced by the ASCII fallback from `info_name_font.bin` (below).
   - `UniNameFontPermissions.bin` (raw, not WESYS; 876 B in this install) records: `{u32 key, u32 n, u32 codes[n]}` (OBS from bytes), codes binary-searched (OBS `FUN_141ea0f50`).
   - The lookup tries `kitId`, then `(kitId & ~0x3FFF) + 0x109` (team-wide record). All 876-byte vanilla records use the team-wide key.
2. **At draw time** (`FUN_1419e4760`): if the kit has no `_name_ex` texture, every code is again mapped through `info_name_font.bin`. If it has one, codes are used raw. Codes absent from the glyph table get width 0 (invisible).
3. **`info_name_font.bin`** (`common_package.fpk` → `common/info_name_font.bin`, 1776 B = 148 × 12 B, loaded by path code `strcode("info_name_font")` in `FUN_1419a7f00`): records `{u32 code, u32 upper, u32 ascii}`. `ascii` packs up to 4 chars big-endian, emitted high byte first, zero bytes skipped. Examples:
   - `a`→`A`; `-`→space; `'`→dropped (0); `Æ`→`AE`; `Þ`→`TH`; `ß`→`SS`; `Đ`→`DJ`.
   - A character not in the map is dropped.

### 3.3 Layout and curvature — OBS `FUN_1419e1df0`

Called with region (0, 0, 0.64, 1.0), RT 1600×256, curve mode `m = kit+0x44` (NameShape).

Name space is 512×128 units, mapped to RT pixels as `X_rt = X·(1600·0.64)/512` and `Y_rt = (128 − Y)·2` (Y is flipped).

**Width and scale:**
- `total = Σ w_i`, using glyph-table px widths.
- `N = total/2` (integer). If `m ≠ 0` and `N < 512`: `gap = 6` and `N += 6·(count−1)`; otherwise `gap = 0`.
- `scale = min(1, 512/N)`; then `N = min(N, 512)`.

**Glyph loop, right to left:**
- The cursor starts at `256 + N/2`.
- Per glyph: `gw = w_i·scale/2`, `gh = 62·0.98` (page-1 glyphs double their normalised height, so the same 62 units). `centre = cursor − gw/2`.
- The quad is **mirrored in x**: local x = −gw/2 takes u+w, +gw/2 takes u.
- Next cursor = `centre − gap − gw/2`.

**Placement:**
- Flat (`m = 0`): glyph at `(centre, 64)`.
- Curved: `R = {1: 672, 2: 336, 3: 292}[m]`, `θ = (centre − 256)/R`.
  - Glyph centre = `(256 + 0.8R·sinθ, 0.89R − 0.8R·cosθ)`, rotated by θ.
  - Local x is scaled again by `scale·0.95`.
  - INF (medium) for the exact matrix order: the decompile shows composite rotation/translation rows. The values above come from reducing them with identity bases.
  - **Confirmed by render (OBS, MarkingPixelsRE-2):** a synthetic raster of `ABCDEF` through these rules against the decoded `u0001p1_name.ftex` produces a correct upward arch with each glyph rotated along it (`<scratch>/MarkingPixelsRE/name_curve_A.png`), and the flat case produces `ABCDEF` left-to-right (`name_flat3.png`). The RT write applies `Y_rt = (128 − Y)·2` and `X_rt = X·2` (0x1419e2a61/0x1419e2a79), so the canvas is vertically flipped relative to the source page — the CPU rasteriser must apply that flip and must not mirror the source y a second time.
  - On the garment the shader mirrors back: back-number sampling has a negative x scale, the name a positive one; INF (high).

The name's shader `.x` scale uses the same width sum (`FUN_1419a4d20`, kit report §8.7). `NameShape` both bends the glyphs here and adds the curvature constant to NameY there. Vanilla NameShape distribution: 0 ×1204, 1 ×123, 2 ×45.

Blend mode 6. Colour: white for real kits (§2.4).

### 3.4 Chest badge (4th region)

OBS: `FUN_1419e1610` draws the kit's chest-badge texture into (0.88, 0.5, 0.08, 0.5) with blend mode 6. The texture is `FUN_1419a4660(kit, 4)` = kit node `+0x1A0`, and its aspect is used for `BadgeMatrix0/1`.

### 3.5 Output texture (OBS: `TextureCompositor` vtable `0x142a91c80` and live)

State machine `FUN_1419c0770`:

| State | Action |
|---|---|
| 0 | create RT |
| 1 | `FUN_1419c0dd0`: read back RGBA8 (pitch = w·4) |
| 2 | `FUN_1419c0890`: CPU mips up to `+0x24` = 2 (box filter 1; leg flag bit3 → filter 4) |
| 3 | `FUN_1419bffe0`: compress type `+0x28` (2 → codec 5; 1 → codec 1). Images > 0x4000 px go through `DxtEncodeController` in chunks |
| 4 | `FUN_1419c09c0`: concatenate mips; create texture from memory with format `0x84` / `0x4000` / `0x4002`, `w`, `h`, `mips + 1`, null path `0xb8a0bf169f98` |

- Clear colour = `0x00FFFFFF` (white, alpha 0).
- Edit kit type 2 skips compression.

---

## 4. Captain armband (OBS)

**Who:** unit `+0xD4` = A+0x14 = 1 for exactly one player per team (live: 2 of 22). Set through `FUN_141a85a00`.

**Visibility** (`FUN_141a85a00`, OBS; live part lists match):

| Slot | Visible iff |
|---|---|
| `sleeve`, `sleeve_alt` | **not** captain, and the part's sleeve state == unit `+0xC0` |
| `sleeve_cap`, `sleeve_alt_cap`, `cap` | **captain**, and state == `+0xC0` |

Live, captain (#11, short tight): `sleeve_short_tight_001` hidden, `sleeve_short_cap_tight_001` shown. Non-captain GK: the reverse.

**Default armband (every competition):** `sleeve_*_cap_*.fmdl` has an extra mesh section whose UV0 lies in u∈[0.001,0.248], v∈[0.001,0.061] of the kit atlas. OBS: UV dump of `sleeve_short_cap_001`; the atlas corner was examined.
- `u0001p1`: plain gold band.
- `u0725p1`: yellow/black band with a "C".

So the armband is the atlas panel [0,0.25]×[0,0.0625], drawn by the normal sleeve binder (category 2).

**Champions League armband (competition id 0x21 only):**
- `FUN_141ea1f50(out, compe, d7&3, d8>>31)` → `"CL_captainmark_" + ("half_" if d8 bit31) + "%02d"(d7&3 < 3 ? d7&3 : 0)`. For any other competition it returns "".
- Called from the realUni parser only when the match context is present; result stored at kit+0xC8.
- File: `Asset/model/character/uniform/cap/#windx11/CL_captainmark[_half]_0{0,1,2}.ftex`:
  - full 512×128, half 256×128, BC7, opaque;
  - 00/01 lime "RESPECT", 02 navy (examined).
- If it loads, kit node `V+0x198 = 1`, which enables part slot `cap` (`0xa4e48873bd5c`, category 5, needs shirt worn and not referee).
- Model: `nocloth/cap_sleeve_{short|long}{|_tight}.fmdl`. `long` is chosen when the primary sleeve type ∈ {1,2}.
  - Material `uni_sleeve`, UV0 spans the full [0,1]².
  - Binder `FUN_141a85770`: `Base_Tex_SRGB` = captain-mark texture (kit idx 0xE), Badge/Stain/StainMask = null, RepetitionParam from the pattern.
- Vanilla d7&3: 0 ×1222, **2 ×147 (GK kits, e.g. `1_DEF_GK1st`)**, 1 ×3; d8 bit31 always 0. So GKs get the navy armband, outfield players the lime one.

---

## 5. Badges / competition patches (OBS)

**Selection** (`FUN_141ea1bf0`, called from the parser per side): enabled only if the match context flags `+0x34` (`param_4`) and `param_7` are set. Mode = context `+0x35`.

Competition remap:
- id unset (`0xFFFF` or the global default) and team kind `(team>>28)&7 == 4` → compe 8;
- compe 5–7 → nothing;
- compe 8 → mode forced 3 if the league flag allows.

**Static table** `0x142b65488` `{u32 compe, u16 L, u16 R}`:
- compe 8 → (14, 13);
- compe 0x21 → (100, 19), where 100 = `respect_badge`.

**BadgeData.bin** (WESYS zlib, 2552 B). Header `u32 6, u16 0, u16 20`, then 318 sorted records `{u32 teamId, u8 b0, u8 b1, u8 b2, u8 0}`, keyed by `kitId>>14`.
- If the compe is not in the table:
  - mode 1: R = rec[side], or 0x1B if the bytes are `$$`;
  - mode 2: L = rec[side];
  - mode 3: L = R = rec[side].
- UCL: R = 0x14 when b2 == 0x14 (INF: title-holder badge). In this install b0 == b1 and b2 == 0 everywhere.
- `param_6` (custom) → 300 → the custom texture from the context.

**Names** (`FUN_141ea4be0`): id < 100 → `badge%02d`; 100–199 → `respect_badge`; ≥ 200 → the supplied texture. Dir: `/Assets/pes16/model/character/uniform/badge/`.
- L goes to badge slots 0 and 2, R to slots 1 and 3 (`FUN_141965110`, kit list +0xB0).
- Sleeve binder `FUN_141a874a0`: `Badge_Tex_LIN` = per-kit composite (node +0x1A8), with `BadgeMatrix0/1` from slots (0,1) or `BadgeMatrix2/3` from slots (2,3) depending on the sleeve side (part +0x50).
- Shader: the first badge occupies u∈[0,0.5), the second u∈[0.5,1) (shader report).
- Positions come from the realUni LeftShort/RightShort/LeftLong/RightLong fields (`FUN_141e592b0`/`FUN_141e58f60`).

**Assets:**
- 54 `badgeNN.ftex` (e.g. `badge09` Ligue 1, `badge13` ACL).
- Several are blank 64×64 placeholders: `badge14`, `badge19`, `badge20`, `respect_badge` have alpha 0 (OBS: vanilla dt35 licence stubs). Expect invisible badges for those ids.

**Open:** the composite-badge texture builder (`BadgeLoadJob`) and the `FUN_1419a2a60` matrix maths were not decompiled.

---

## 6. Live confirmation (OBS, match loaded)

- 25 `UniformUnit`s. Each of the 22 players has `+0x1C0` and `+0x1C8` = `fox::gr::Texture`, path = null code (runtime texture). The 3 referees have none (`+0x1A8` = 1/2).
- Shirt marking, via handle → `TextureResourceDx11` (table `*0x14373e930 + 8`, stride 0x60) → descriptor:
  - `+0x7C` = `0x01000640` (**1600×256**), `+0x74` = format `0x4002`, `+0x6C` = **537,600 B** = 409,600 + 102,400 + 25,600 (BC3-sized, 3 mips);
  - per-mip row pitches 6400 / 3200 / 1600 B.
- Leg: 128×128, `0x5400` = 21,504 B (3 mips).
- Pixels were **not** readable:
  - the data lives only in the DXVK GPU image;
  - a heap scan for the RGBA readback (runs of `ff ff ff 00`) found only unrelated fragments;
  - a BC3 transparent-block scan was started.
- Part lists (`lv7.py`) confirm §4 and §7:
  - captain → `*_cap_*` visible;
  - GK (glove id 12, long sleeves) → `sleeve_glove`;
  - referee → `referee_collar_001` + `sleeve_glove` + `pants_014`, with no shirt parts; glove field 9908.

---

## 7. Goalkeeper kits

- **Config:** `<team>_DEF_GK<n>[_realUni].bin`; kitId bit 7 = GK (kit report §2). Texture prefix `u####g#` with its own `_back/_chest/_leg/_name` (OBS: `u0001g1_back` is a white-outline digit style). Assembly and markings are identical to outfield players.
- **Typical GK config differences (vanilla, OBS):** captain-armband colour d7&3 = 2 (navy) on GK kits such as `1_DEF_GK1st`; SleeveLimits = 1 (both sleeve slots from code B) occurs in 13 loose files, mostly GK kits (kit report §1).
- **Gloves:** the appearance glove id (A+0x34, EDIT save) picks `glove/g%04d/glove.fpk` (kit report §8.3). The sleeve name gets `_glove` → `sleeve_glove[_cap]` (long + glove drops the number).
  - Live: GK of one team (glove 12, long sleeves) → `sleeve_glove.fmdl` / `sleeve_glove_cap.fmdl`; the other GK has glove 1 and short tight sleeves → `sleeve_short_tight_001`.
  - INF (medium): no GK-forced glove rule; GKs have gloves because their appearance records do.

## 8. Referee kits

- **Kind:** character kinds 4/5 (`FUN_141ec4560`). Appearance comes from `RefereeAppearance.bin` (35 × 60 B, ids 1–35, same body layout as PlayerAppearance; this install's 4cc copy has 35 identical bodies). Not from the EDIT save.
- **Face:** `face/real/referee%03d/#Win/face.fpk` (`FUN_141ec53d0`, id masked `& 0x800bffff`).
- **Boots/gloves:** id + 9900 (kit report §8.3; live glove field 9908).
- **Kit file** (`FUN_141ea2160`): `cpk_dat/common/character0/model/character/uniform/team/` + `referee/referee` + tag + `%d` + `.bin`.
  - tag by competition: 5 → `_LB_`; 6–7 → `_SDA_`; 8 → `_ACL_`; 0x21 → `_EU_`; else `_DEF_`.
  - n = selector + 1.
- **Selector** (`FUN_14152b2c0`). Inputs: `RefereeColor.bin` (WESYS zlib, 3 × 85 B records `{u32 group (0 DEF / 4 ACL / 7 EU), u8 count, 10 × {u8 kitNo, u8 flags, rgb1, rgb2}}`) and both teams' two kit colours (UniColor team record +0x5D8 entry matching the kit in use).
  - Colour → CIELAB:
    - `c = (v/255)^2.2`;
    - `X = (0.3933R + 0.3651G + 0.1903B)/0.95045`, `Y = 0.2123R + 0.701G + 0.0858B`, `Z = (0.0182R + 0.1117G + 0.957B)/1.08892`;
    - `L = 116·Y^(1/3) − 16`, `a = 500(X^(1/3) − Y^(1/3))`, `b = 200(Y^(1/3) − Z^(1/3))`.
  - Per team: `d1 = ΔE(team c1, ref c1)`, `d2 = ΔE(team c2, ref c1)` (the referee's 2nd colour is unused).
  - Score `s(d) = 0` if d ≤ 30, else `min(100, (d−30)·1.4285)`.
  - Team score = `s1 + 0.5·s2`, halved if s1 ≤ 0 or s2 ≤ 0.
  - Pick the kit 0..8 with the max sum over both teams (first max wins). INF (high): the return value is the argmax; the decompiler lost the return.
  - A missing group falls back to group 0.
- **Assembly differences** (OBS: kit report §3, `FUN_141a89120`, live):
  - collar slot loads `referee_collar_%03d` (the Collar byte) instead of `collar_%03d`. That model is the **whole referee torso** (material `uni_referee_shirt`, `pes3DDF_Uniform`, UV0 [0.34,0.66]×[0.10,0.98] of the kit atlas);
  - no `shirt_in`/`shirt_out`, no cloth, no `cap`, no markings (`+0xB8 ≠ 0` skips the marking jobs; `+0x1A8 ≠ 0` nulls the params);
  - sleeves, socks and pants follow the normal rules.
  - Vanilla DEF referee: A = 3 (`sleeve_short_003`), B = 1003, shorts 1, collar 7, KitFile `referee_N`, no number files. ACL uses collar 60, EU collar 85; `referee_collar_{001,007,012,026,060,066,084,085}` exist.
  - This install's `4cc_35_referees.cpk` overrides the DEF bins (collar 105, shorts 16).

---

## 9. Recipes

**Shirt marking texture (per player, not referee):**
1. Build a 1600×256 RGBA canvas, cleared to (255,255,255,0).
2. Name: convert the print name (§3.2) and draw the glyph quads from `_name` / `_name_ex` per §3.3, mirrored in x, into x∈[0,1024).
3. Back number: draw from `_back` per §2.2 into region (0.64, 0, 0.24, 1).
4. Chest number: draw from `_chest` into (0.88, 0, 0.12, 0.5) with pad 52.
5. Chest badge: draw it into (0.88, 0.5, 0.08, 0.5).
6. Make mips and bind as `Marking_Tex_LIN`, with the §8.7 vec4s.

**Leg number:** a 128×128 canvas, `_leg`, rotate = 1, pad 26 → `Number_Tex_LIN`.

**Captain:** swap `sleeve*` for `sleeve*_cap*` (atlas armband panel). For competition 0x21, also add `cap_sleeve_*` textured with `CL_captainmark[_half]_%02d(d7&3)`.

**GK:** as outfield, using the `g` kit. **Referee:** §8.

---

## 10. Killed hypotheses

- **"Marking textures are a per-kit atlas the shader samples directly"** — killed. The shader samples a per-player 1600×256 texture (live sizes), composed by `FUN_1419e3c60`.
- **"`FUN_141a96d50` composes the marking"** — killed. It is a menu/replica binder: it forces number −1 and binds `Marking_Tex_LIN` to null.
- **"`CL_captainmark` / `half_` are models"** — killed. They are textures in `uniform/cap/`; the model is `cap_sleeve_*`.
- **"d7 bits 0–1 choose the armband for every match"** — killed. They only matter for competition 0x21; the normal armband is the kit-atlas panel.
- **"d8 bit 31 is an unknown team flag"** — refined. It is the `half_` captain-mark selector, and it also sets context +0xA0.
- **"The composed texture is readable from process memory"** — killed for this setup. The GPU image is not mapped; the CPU readback buffer is freed (heap scan negative).

## 11. Open

- Exact pixel output of curved names: the matrix reduction is INF (medium), with no live pixels to compare. To close it, hook or capture the GPU image, or render §3.3 and compare against a screenshot. *(Partial progress: MarkingPixelsRE-2 rendered §3.3 synthetically against the decoded `u0001p1_name.ftex` and got a correct upward arch + correct flat string — see `docs/re/pes-re-marking-pixels.md` §6. The remaining unknown is only the composite matrix's exact operation order, not the constants.)*
- Badge composite builder (`BadgeLoadJob`) and the badge matrix maths (`FUN_1419a2a60`). *(The chest badge inside the marking RT is now closed: `FUN_1419e1610` → region `(0.88, 0.5, 0.08, 0.5)`, blend mode 6, identity quad permutation, full-texture source. Only the sleeve badges remain open.)*
- Fox format `0x4002` → DXGI mapping. INF (high): BC3, from the byte size and the alpha need.
- What BackNumberType means in the menu (behaviour = zero-pad single digits; never set in vanilla).

**Known compositor divergences (OBS, MarkingPixelsRE-2 — see `docs/re/pes-re-marking-pixels.md` §7 for the full list):**
1. `src/gdext/pes_marking.cpp:98` rotates the leg number `+90°` where PES uses net `−90°` (the digits come out mirrored), and swaps the quad's width/height; `:97` treats the y origin as the quad's centre rather than its top.
2. `pes_marking.cpp:142` folds names through `info_name_font.bin` even when the kit has a `_name_ex` texture (29 kits) — PES uses the raw codes there.

Scratch: `<scratch>/MarkingRE/` (decompiles `out/`, live scripts `lv*.py`, decoded textures `tex/`); the pixel-exactness pass is `<scratch>/MarkingPixelsRE/`.
