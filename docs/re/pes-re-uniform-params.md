# PES 2021 uniform material parameters at run time — evidence report

Main session + UniformParamsRE-2, 12-10-2026. Sources:
- headless decompiles on the analysed image copy (`<scratch>/pesmem.bin` loaded raw at 0x140000000; bounds from in-memory `.pdata`; outputs in `<scratch>/out2/writers*.txt`, `getters.txt`, `unitsetup*.txt`, `unitctor.txt`, `mgr.txt`; prior pass in `<scratch>/UniformParamsRE/out/`, `<scratch>/UniformParamsRE/out2/`);
- raw-byte scans of the same image with capstone/`<scratch>/pdata.py`;
- the install through `build/apps/pesfmt_dump`;
- StrCode32 reversal with GF `tools/pes21_import/strcode.py`.

Tags: OBS (observed: code / bytes / live address+value) or INF (inferred, confidence).
VA = image virtual address. File offset in the raw image = VA − 0x140000000 (raw load at base; e.g. DAT_142a83c70 is at image offset 0x2a83c70). The on-disk exe is packed: statics below are readable from the live/unpacked image, not from the exe file. No PID is live at write time (game down), so no new live cross-checks; live numbers quoted are from pes-re-shaders.md §6 / this report's prior pass.

Prior §1–§2 (fmdl defaults, setter/getter, StrCode32 sites) still stand, with corrections/additions below. The old §5 "byte search running" is closed (negative, §6).

## 1. Shirt `Repetition` xy table (partly open: code path closed, file source open)

- OBS (code, `FUN_141961050` @ 0x141961050, 8 bytes): with `Q` = the uniform-manager map object,
  `x = *(float*)(*(Q+0x10) + 0xd8 + 4p)`, `y = ...+0xf4 + 4p`, `w = ...+0x110 + 4p`, `p` = ShirtPattern (no bounds check).
- OBS (code): `p = FUN_141960c70(kit) = *(kit+8)->+0x14` (= KitConfig ShirtPattern, 0–6). Callers of `FUN_141961050` (OBS, call scan): exactly 3 — 0x141a85867 (`FUN_141a85770`, cap), 0x141a8695f (`FUN_141a86830`, collar+shirt), 0x141a875f4 (`FUN_141a874a0`, sleeve). Pants/socks (`FUN_141a85c70`) never call it.
- OBS (code, all three callers): `Repetition = (x, y, float(p), 0)`; fallback without kit = static default `DAT_142a83c70.. = (80.0, 0.8, 1.25, 0)`.
- OBS (live, prior pass): `T = *(Q+0x10)` reached via `m = DAT_1437cb6b0 → +8 → +8 (Q) → +0x10 (T)`; at `T+0xd8`: seven `x = 25.0`, at `+0xf4` seven `y = 50.0`, at `+0x110` seven `w = 1.25`. Live shirt `(25, 50, p, 0)` for kits 752 (p=1) / 754 (p=0) fully explained.
- Row count: 7 (indices 0–6 = ShirtPattern range; 7 `uni_pattern_mtm0N` / `uni_pattern_normal0N` files). INF (high).
- OPEN — the file backing T: closed negative after trying (all OBS): byte scan of all 2,596 small `.bin` files under `common/character0` (incl. all 2,595 team realUni/96B files) for the `(25,50,1.25)` triple or 7-rows — no hit; `UniformParameter.bin` (VFS winner, 346,086 B raw) contains zero `25.0/50.0/1.25` floats; same for `UniColor.bin`, `BadgeData.bin`; no 7-run of any of the three floats anywhere in the 61 MB image; `uniformPattern_0..23` strings are pool-only (no hash immediate, no LEA ref); no row array adjacent to the default at `DAT_142a83c70` (garbage after); T's first u32s (`0x153, 0, 0x42`) match no VFS header sampled. Not tried: live-heap walk from `DAT_1437cb6b0` (needs game up), Fox buffer loader audit (`FUN_141965a80` family).
- Coder recipe: hardcode `Repetition = (25.0, 50.0, float(ShirtPattern), 0)` for collar/shirt/sleeve/cap (slot categories 0,1,2,5); verify against one live shirt MM2 (`RepetitionParam` 0x40547b2f via getter `FUN_141695460`); if a pattern ever reads differently, walk `DAT_1437cb6b0 → +8 → +8 → +0x10 → +0xd8/0xf4/0x110` live.

## 2. Pants / socks `Repetition.z` (closed)

- OBS (code, `FUN_141a85c70`): base = current MM2 value (fmdl default on first write: pants `(35,0,0,0)`, socks `(50,0,0,0)` — confirmed via `pesfmt_dump fmdl`), keeps `.x/.y/.w`, sets `.z = (param_4 == 0) ? DAT_1425e66b8 : DAT_142aad7d8`.
- OBS (bytes): `DAT_1425e66b8 = 8.0`, `DAT_142aad7d8 = 7.0`.
- OBS (code, dispatch in `FUN_141a89120` + `FUN_141a8b7b0`): pants slots (cat 3: `pants_in_sub`, `pants_out_sub`) call with `param_4 = 1` (0x141a896d9: `r9b = 1`); socks slots (cat 4) with `param_4 = 0` (0x141a89924: `r9d = 0`).
- Rule: pants `.z = 7.0`, socks `.z = 8.0` — per-part constants, no data file, no kit dependence (matches live `(35,0,7,0)` / `(50,0,8,0)` on both kits).
- Exceptions (OBS): material `skin_limb` (0x34362fe2) skips the whole write (keeps fmdl); material 0x4d23f88a (identity TBD — not in sampled nocloth/common/cloth/face/boot fmdls) gets static row `(1,1,1,1)` (= `DAT_142552690..`) under param 0x9224571e instead.
- Coder recipe: `if part is pants: z = 7.0; if socks: z = 8.0; x/y/w = fmdl default`. No runtime read needed.

## 3. Shirt wrinkle slice (closed as rule + recommendation; full pose recompute open)

- The "slice" is `BlendNormalX.y` (shader: `nA = NormalArray[BlendNormalX.y](uv1)`).
- OBS (code, `FUN_141a84010`, per-part tail dispatch on `[part+0x40]`): cat 0/1 (collar/shirt) and cat 2 (sleeve) write `BlendNormalX = (|dAngle|, 2.0|4.0, w1, w2)` where `.y = 2.0` (`DAT_142565604`) if `fVar32 <= fVar27` else `4.0` (`DAT_1425656c0`); cat 3 (pants) writes `.y = 1.0` (`DAT_14253ee50`) or `3.0` (`DAT_142aad790`); cat 4/5 (socks/cap) get no BlendNormal write. Inputs are joint-angle terms (rad→deg ×57.29578, thresholds 3.0/17/22.5/34/49 etc. from `DAT_14259be40/14258c264/14259d614…`) over bone quaternions at `[unit+0xe0..0x198]` — i.e. pose-driven per-frame cloth wrinkle blending, not time/weather/kit/match-state selected.
- Live: all shirt samples `.y = 2.0` (`(0,2,0.0376,0.0434)`, `(0.1115,2,0,0)`).
- Coder recipe: constant `BlendNormalX.y = 2.0` for shirt/collar/sleeve; do not recompute the pose math (joint set未被 traced — live-MM2 sampling is the check). Pants `.y` (1/3) unobserved live — open.

## 4. `BlendNormalX` / `BlendNormalLeg` / `MatParamIndex` rules (closed, one value open)

Writers, all OBS (code):
- `FUN_141a84010` (0x141a84010–0x141a8576d) computes both BlendNormals per part (see §3 for values/selectors). `BlendNormalLeg`: shirt/collar `= (fVar44, fVar27)` leg-angle pair (live `(0.0579,0.0166)` / `(0,0.0461)`); pants `= (fVar32, fVar30)`-derived pair (live unobserved — open).
- `FUN_141a87f50` (0x141a87f50–0x141a881cf) writes `MatParamIndex_0 = v/255` (divisor `DAT_1429e3910 = 255×4`), skipped for materials `arm_mat` (0x57948546, OBS reversed), `skin_limb` (0x34362fe2, OBS reversed), `skin_dirt` (0xd441a2ff, OBS reversed), `0x4d23f88a` (TBD), `0x34362fe2/0x57948546` also deferred to `FUN_141a86ca0` in sleeve/shirt paths. Per slot category `[part+0x40]` (= 15-slot table at 0x143517b30, 16 B entries `{u64 hash, u32 cat}`: collar 0, shirt 1, sleeve 2, pants 3, socks 4, cap 5 — OBS, dump):
  - cat 0/1/2 (collar/shirt/sleeve): dynamic `FUN_141a82cf0([unit+0xcc], [unit+0x200]==2)`: `...==2 → 157`; else `cc` 1→151, 2→152, 3→153, 4→154, 5→155, 6→156, else 150 (`DAT_14259be68/142aad76c/…/1425c6784/1425ccc3c/1425cfda0`, all OBS floats).
  - cat 3 (pants): 157 if `cc == 0` or `[unit+0x200] == 2`, 149 if `cc ∈ 1..4`, 148 iff `cc == 5`, 158 if `cc ≥ 6` (OBS branch; `DAT_1425cfda0/142aad764/142aad768/142aad77c` = 157/148/149/158).
  - cat 4 (socks): constant 147 (`DAT_142aad760`), MPI always written (matches live 147; fmdl default is also 147).
  - cat 5 (cap): MPI never written (falls through with write flag false).
- Shader meaning (OBS, pes-re-shaders.md §3.1 + §6 MM2 layout): `MatParamIndex` = cloth preset index into `material_params.fmtt` pairs (147 socks F0≈0.028 wrap 0; 148–151 cloth F0 0.035–0.051 wrap 0.698); `BlendNormalX = (mix |x−0.5|, slice .y, detail weights .zw)` with `n = lerp(nB[((y+1)·ceil(x−0.5))], nA[y], 1−2|x−0.5|)`, then `lerp(n, slice1, dot(.zw, s1.xz))`; `BlendNormalLeg.xy = dot weights vs slice-3 (s3.xz)`.
- Resolved identities (OBS, StrCode32): skip-Spec 0x2310a08c = `uni_sleeve_inner`; 0x7c810865 = `uni_referee_sleeve`; TBD: 0x4d23f88a, 0xb5be0c46 (skin-ish material handled in `FUN_141a86ca0` alongside `arm_mat`/`skin_limb`).
- Live fit: kit-752 unit behaves as `cc = 1` → shirt/collar/sleeve 151, pants 149, socks 147 — all match. Kit-754's shirt-150 needs `cc ∈ {0,7+}`, but its pants-148 needs `cc = 5`: no single `(cc, +0x200)` pair yields both, so one of the two values is stale or belongs to another unit (referee/GK/preview states were resident) — OPEN, with the branch table above as the exact predictor once `[unit+0xcc]` is read live. `[unit+0xcc]`/`[unit+0x200]` semantics (kit-side fields; zeroed by ctor `FUN_141a81e90`, not set by `FUN_141a83850`) also open — but the recipe only needs the live u32 reads.
- Coder recipe per part: read u32 `[unit+0xcc]`, u32 `[unit+0x200]` (UniformUnit, vtable 0x142aad438, ctor 0x141a81e90); apply the branch table; socks = 147, cap = fmdl default (40); verify against live `MatParamIndex_0` (0xe38487ff).

## 5. Negative / open list (what was tried)

- T file source: open (§1). Tried: 2,596-file VFS byte scan; UniformParameter/UniColor/BadgeData float scan; full-image 7-row scan; `uniformPattern_*` ref/hash search; rows-next-to-default check. Not tried: live-heap walk, Fox-loader audit.
- Kit-754 (150, 148) joint origin: open (§4). Needs live `[unit+0xcc]` sampling per unit.
- Pants `BlendNormalX/Leg` live values: unobserved (not in shaders §6 table) — open.
- 0x4d23f88a / 0xb5be0c46 identities: open (not in ~25k fmdl/dump strings hashed).
- `PatternIndexParam` (0xf5536710): no immediate anywhere in image — never written at runtime (prior finding, reconfirmed by hash-imm scan which found only the 9 Repetition / 3+1 BlendNormal / 5 MPI sites).

## 6. Static address cheat-sheet (all OBS)

Setters: `FUN_141697f30` (vector), `FUN_141697f70` (texture); getter `FUN_141695460`. Hashes: Repetition 0x40547b2f (sites 0x141a858dd/0x141a85dcc/0x141a85de3/0x141a86a48/0x141a87768/0x141a87df5/0x141a87e19 + marking 0x141a96f70/0x141a96fac); BlendNormalX 0xaa51f6fe (0x141a8554b/5c/76); BlendNormalLeg 0x7b9e39d1 (0x141a8558a); MPI 0xe38487ff (0x141a8812e + non-uniform 0x1417009f5/0x141a3592e/0x141a41286/0x142a13340-data).
Writers: 0x141a85770 cap / 0x141a86830 collar+shirt / 0x141a874a0 sleeve / 0x141a85c70 pants+socks / 0x141a87d50 `.w` fix (`0.5`/`0`) / 0x141a87f50 MPI / 0x141a84010 BlendNormals / assembly 0x141a89120 (dispatch `[rdi+0x68]`, jump table 0x141a8b5f8, slot table 0x143517b30) / 0x141a8b7b0 (collar/marking path, MPI call 0x141a8bf75).
Statics: default Repetition 0x142a83c70 `(80, 0.8, 1.25, 0)`; pants-z 0x142aad7d8 `7.0`; socks-z 0x1425e66b8 `8.0`; slice 2.0/4.0 0x142565604/0x1425656c0; pants slice 1.0/3.0 0x14253ee50/0x142aad790; /255 divisor 0x1429e3910; MPI rows: 150/160/225/500 @0x14259be68, 151–154 @0x142aad76c, 152–154+158 @0x142aad770, 153/154/158 @0x142aad774, 154/158 @0x142aad778, 155 @0x1425c6784, 156 @0x1425ccc3c, 157 @0x1425cfda0, 147/148/149/151 @0x142aad760, 148/149/151/152 @0x142aad764, 149/151/152/153 @0x142aad768, 158 @0x142aad77c. Manager static `DAT_1437cb6b0` (= `FUN_1419a4c50()`); vtable `UniformUnit` 0x142aad438 (ctor `FUN_141a81e90`).
Decompiles: `<scratch>/out2/writers{1,2,3,4,5}.txt`, `getters.txt`, `unitsetup{,2}.txt`, `unitctor.txt`, `mgr.txt` (this pass); `<scratch>/UniformParamsRE/out*/` (prior pass).
