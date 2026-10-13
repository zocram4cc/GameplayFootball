# PES 2021 physique helpers — independent verification of `docs/re/pes-re-physique.md`

Scope: verify the slider set, the per-bone scale arithmetic, the global scale and the gait
coupling directly from the unpacked image `<scratch>/pesmem.bin` (base 0x140000000) and the
on-disk `PES2021.exe`. Method: raw bytes for the constant pools, `objdump` disassembly for
the argument setup, and headless Ghidra decompiles (private project, `-noanalysis -readOnly`)
for control flow. **No live process was read** (the game is not running); every claim below is
OBS (file/bytes/decompile) unless tagged INF.

Relationship to `docs/re/pes-re-physique.md` (the "main report"): this file re-derives the same
system independently. Where the main report is confirmed I say so; where my derivation differs
I mark a **CORRECTION**. The main report's live-match validations (§5) were not re-run.

Tags: **OBS** = observed (named file/address). **INF** = inferred + confidence.

---

## 0. Where the numbers live (runtime-readable sources)

| Object | VA (image) | How to read at runtime |
|---|---|---|
| Table A header (absolute, anim) | 0x142a83050 | 22 × 32 B `{u64 recArray, u32 count, u32 0, u64 getter, f32 a, f32 b}`; entries 0–15 are evaluated |
| Table B header (relative, render) | 0x142a83250 | same layout; entries 0–20 evaluated |
| Table C header (entry+0x28 vector) | 0x142a834f0 | entries 0–1 evaluated |
| Per-bone records | 0x1437c92d0 … 0x1437cb0b8 | 48 B `{u32 StrCode64(name)&0xFFFFFFFF, 12 B pad, f32[4] neg, f32[4] pos}`; built by static initialisers in the unpacked `.text` (0x14020c130…0x14020dbd0) from name strings in `.rdata` |
| Weight normaliser table | 0x142a82900 | 12 × 16 B `{f32 h, u32 Wmin, u32 Wstd, u32 Wmax}` = 132…220 cm |
| G constants | 0x142a83530 | `{0.018, 0.019, 0.223, 0.277}` |
| G clamps | 0x142a83540 | `{132.0, 219.99899…}` |
| `hnorm` piecewise constants | 0x142588214 = 48, 0x14259be2c = 40, 0x1425ca310 = 220, 0x142598580 = 180 | see §2 |
| `hnorm2` constants | 0x1425c6784 = 155, 0x14259d630 = 210, 0x14259be20 = 25, 0x142591140 = 30 | see §2 |
| Lerp identity | 0x1425a0c50 = `(1,1,1,0)` | used by all three evaluators |
| Move-scale clamps | 0x1425ddf78 = 0.9166, 0x1425ddf80 = 1.0833 | `AnimMovePlugin+0xC0` clamp |
| deg→rad | 0x1425a0c48 = 0.017453292 | clavicle roll |
| collar offsets | 0x14256ae18 = +0.01, 0x14260a24c = −0.005, 0x1425b8708 = +0.005, 0x1425e6820 = −0.01 | metres |
| clavicle angles | 0x14259bdf4 = +8.0, 0x1425c7ba4 = −8.0 | degrees |

OBS: all read as little-endian from `<scratch>/pesmem.bin` at `VA − 0x140000000`.

Bone-record hashes are `StrCode64(name) & 0xFFFFFFFF` (FNV-1a-64, the repo's
`strcode.cpp` value). **All 295 records across A+B+C resolve this way** (OBS, brute-force scan
of every `sk_*`/`dsk_*` string in the image against every record hash; 0 unresolved).
Note table A/B entry 16 of A and 21 of B etc. are *not* evaluated — see §3.

---

## 1. The physique object and the sliders (Q1)

### 1.1 Object layout — confirmed
OBS (decompiles + asm): `float[16]` at `+0x00`, `u8 disable` at `+0x40`.

- `+0x00` height cm (`FUN_14195ca70` calls `FUN_14195c4b0(&obj[0])`);
- `+0x04` normalised weight;
- `+0x08 … +0x3C` the 14 sliders, in **this order**:

| obj offset | slider | getter (VA) | getter body |
|---|---|---|---|
| +0x08 | neck_len | 0x1420e3a50 | `movss 0x08(%rcx)` |
| +0x0C | neck_size | 0x14195c520 | `movss 0x0C(%rcx)` |
| +0x10 | shoulder height | 0x14195c530 | `movss 0x10(%rcx)` |
| +0x14 | shoulder width | 0x14195c540 | `movss 0x14(%rcx)` |
| +0x18 | chest | 0x1420553a0 | `movss 0x18(%rcx)` |
| +0x1C | waist | 0x14195c560 | `movss 0x1C(%rcx)` |
| +0x20 | arm length | 0x14195c400 | `movss 0x20(%rcx)` |
| +0x24 | arm size | 0x14195c410 | `movss 0x24(%rcx)` |
| +0x28 | thigh | 0x14195c550 | `movss 0x28(%rcx)` |
| +0x2C | calf | 0x14195c420 | `movss 0x2C(%rcx)` |
| +0x30 | leg length | 0x1407b5fe0 | `movss 0x30(%rcx)` |
| +0x34 | head length | 0x14195c430 | `movss 0x34(%rcx)` |
| +0x38 | head width | 0x1422fba00 | `movss 0x38(%rcx)` |
| +0x3C | head depth | 0x141889a90 | `movss 0x3C(%rcx)` |

OBS: every getter is the 5-byte sequence `f3 0f 10 41 <off> c3` — a single load. Weight's
getter `0x14195c570` is `f3 0f 10 41 04 c3` (loads `+0x04`). Height uses the piecewise
normaliser `FUN_14195c4b0`, and table C uses a second one `FUN_14195c440`.

### 1.2 Slider decode — confirmed, including the nibble swap
OBS (`FUN_141ad98b0`, 14 nibbles read as `2n/14 − 1`, `n == 15 → 0`):

```
obj(+0x08) = nib(w[0] & 0xF)
obj(+0x0C) = nib(w[0] >> 4)        // note: >> 4 first
obj(+0x10) = nib(w[0] >> 8)
obj(+0x14) = nib(w[0] >> 12)
obj(+0x18) = nib(w[1] & 0xF)       // byte at src+2 (the ushort read at param_2+2)
obj(+0x1C) = nib(w[0] >> 20)
obj(+0x20) = nib(w[0] >> 28)       // 0x1c shift
obj(+0x24) = nib(byte3 & 0xF)
obj(+0x28) = nib(w[1] & 0xF)
obj(+0x2C) = nib(w[1] >> 4)
obj(+0x30) = nib(w[1] >> 8)
obj(+0x34) = nib(w[1] >> 12)
obj(+0x38) = nib(w[4] & 0xF)       // the ushort at param_2+6, low nibble
obj(+0x3C) = nib(w[1] >> 20)
```
Literal: `v = (n < 15) ? (2n)/14 − 1 : 0`.

The body byte order is the appearance-record byte order, so **arm_len (+0x20) is the high
nibble of appearance byte 0x0F and arm_size (+0x24) the low nibble** — i.e. the storage order
relative to the object is swapped, exactly as the main report §1.2 states. OBS from the
decompile's shift sequence above.

### 1.3 Storage: appearance record (OBS)
The decoder's caller chain (draw side) is `FUN_141ec4560 → FUN_141ad98b0(request+0x10, body)`
(main report §2). The nibbles sit in appearance-record bytes 0x0C…0x12 (one nibble each),
which is the same content at EDIT-save record `+0xF0+off` and at `PlayerAppearance.bin`
record offset `off − 8`. The edit preview path `FUN_141e586f0` first fills a default body of
all-7 nibbles (OBS: main report §1.2) — neutral 7 → `0.0`, which is why an unedited player is a
no-op.

### 1.4 Match-side source — open, now pinned to one function
OBS (`FUN_1408be270`, the record builder the main report §7 says "zeroes the sliders"):
it writes `obj+0x00 = *(u8*)(player+0x39)` (height, cm) and
`obj+0x0C = FUN_14195c580(180.0? no — *(u8*)(player+0x3A))`, i.e. **weight is passed as the
integer kg** and height is not passed at all: `FUN_14195c580(0, kg)` clamps `h` from 0 up to
132 → the 132-cm table row. So this builder produces the *fallback/neutral* physique object,
and the sliders stay 0.

INF (high): the live per-player object the animation actually consumes is filled elsewhere,
after the appearance record is resolved; the builder above is only used for characters whose
appearance record has not been resolved yet. **The writer of the match-side sliders remains
open** (as in the main report §8). What *is* now OBS: the consumer is unambiguous
(`FUN_1406eb210 → FUN_141eb5810`, §3-§4).

---

## 2. Height and weight normalisers — confirmed arithmetic

### 2.1 `hnorm` = `FUN_14195c4b0` (height → g, used by G and tables A/B e0)
OBS from `FUN_14195c4b0` with the constants above:

```
x = max(h, 132)                       # 0x142a83540
if (220 − x >= 0) and (x < 180):      # 0x1425ca310, 0x142598580
    return (h − 132)/48 − 1           # 0x142588214 = 48, 0x14256ae1c = 1
else:
    return (h − 180)/40               # 0x14259be2c = 40
```
Note it uses **`h`** (unclamped) inside the numerator but **`x`** in the branch test, so for
`h < 132` with `x = 132` the first branch can still fire and give values below −1. Confirmed
by executing the formula: `hnorm(100) = −1.6667`, `hnorm(125) = −1.1458`.

### 2.2 `hnorm2` = `FUN_14195c440` (height → g2, table C and B e21 only)
OBS:
```
x = max(h, 155)                       # 0x1425c6784
if (210 − x >= 0) and (x < 180):      # 0x14259d630, 0x142598580
    return (x − 155)/25 − 1           # 0x14259be20 = 25
else:
    return (x − 180)/30               # 0x142591140 = 30
```
Clamped at 155 from below, so `g2(100) = −1.0` exactly; not clamped above (`g2(220)=1.3333`).

### 2.3 `wnorm` = `FUN_14195c580(h, w)` — confirmed rationals
OBS decompile; the table is at 0x142a82900 as `{f32 h, u32 Wmin, u32 Wstd, u32 Wmax}` × 12:

| h | 132 | 140 | 148 | 156 | 164 | 172 | 180 | 188 | 196 | 204 | 212 | 220 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Wmin | 26 | 29 | 32 | 36 | 40 | 44 | 48 | 53 | 57 | 62 | 67 | 72 |
| Wstd | 38 | 43 | 48 | 53 | 59 | 65 | 71 | 77 | 84 | 91 | 98 | 106 |
| Wmax | 54 | 60 | 67 | 75 | 83 | 91 | 100 | 109 | 119 | 129 | 139 | 150 |

Literal algorithm (the loop walks rows 1…11 and stops at the first `h < T[i].h`; if none, 0.0):

```
hc = min(max(h, 132), 219.99899)      # 0x142a83540 / 0x142a83544
i = first index with hc < T[i].h      # i ∈ 1..11
t = (hc − T[i−1].h) / (T[i].h − T[i−1].h)
Wmin = lerp(T[i−1].Wmin, T[i].Wmin, t); Wstd = lerp(...); Wmax = lerp(...)
w' = clamp(w, Wmin, Wmax)
v  = (w >= Wstd) ? (w' − Wstd)/(Wmax − Wstd) : (w' − Wmin)/(Wstd − Wmin) − 1
return clamp(v, −1, 1)
```
Re-derivation of the main report's live checks (OBS: my execution of this exact code):
180/55 → −16/23, 185/99 → 194/247, 194/113 → 123/137, 175/80 → 102/217. All four reproduce.

Note the table's h rows are `f32` and the three weights are `u32` cast to float inside the
function — a plain value copy, no scaling. The 219.99899 upper clamp means `h ≥ 220` uses the
last row without interpolation (returns 0.0 if `h` still fails all tests, which cannot happen
after the clamp).

---

## 3. Tables A/B/C: evaluation formula and exact contents

### 3.1 Evaluation — confirmed, with a correction on which entries run
OBS (`FUN_14195c710` = A, `FUN_14195c830` = B, `FUN_14195c950` = C). All three have the same
body; the only differences are the header base and the loop count:

| fn | header base | `pfVar9` start | entries evaluated |
|---|---|---|---|
| A `0x14195c710` | 0x142a83050 | 0x142a83068 (e0 + 0x18) | `lVar10 = 0x10` → **16** (e0…e15) |
| B `0x14195c830` | 0x142a83250 | 0x142a83268 | `lVar10 = 0x15` → **21** (e0…e20) |
| C `0x14195c950` | 0x142a834f0 | 0x142a83508 | `2` (e0,e1) |

**CORRECTION to the main report's Appendix A**: the headers physically contain 22 entries for
A and 22 for B (e.g. A e16…e21 repeat height/weight/neck_size/shldr_hi/shldr_wid with `a=b=1`),
but the A evaluator only iterates 16 and B only 21, so those tail entries are dead data. An
implementer must stop at 16 / 21.

Per entry, for each bone in the record list:
```
v = getter(obj)                                  # scalar slider value, or hnorm/hnorm2
X = (v >= 0) ? b*pos : a*neg                     # b for the positive side, a for the negative side
t = |v|                                          # (1 − |v| on the positive branch, 1 + v on the negative)
R *= (C − X)*t + X        where C = (1,1,1,0)
```
The result starts at `(1,1,1,1)` and is multiplied lane-wise; `obj+0x40 != 0` short-circuits
to `(1,1,1,1)`.

Worked example (OBS: my execution of the tables): `h=185`, shoulder-width nibble 14 (v=+1),
`A(sk_shoulder_l) = (1.2335, 1.0248, 1.0248, 1)` = `1.2 × 1.0279` on x — the main report's
§5.1 "id 6" value 1.2334 reproduced.

`a`/`b` are 1.0 everywhere except **A e0** (`a=0.723, b=1.223`) and **A e12**
(`a=0.981, b=1.018`) — the two entries that fold the global height/leg-length factors into the
anim-skeleton bone lengths. B has no such factors (all 1.0).

### 3.2 The bone lists (all 295 records, hashes resolved)
Complete resolved dump: `<scratch>/PhysiqueRE/tables_resolved.txt` (regenerate with the reader
in §6). The main report's Appendix A matches; the additions/corrections found here:

**Table A (16 entries) — per-bone `(neg → pos)` on the sliders that the main report truncated:**
- e1 weight, 16 records: `sk_belly (1,0.9,0.9)→(1,1.1,1.1)`, `dsk_hip (1,0.9,0.9)→(1,1.05,1.05)`,
  `sk_thigh_l/r (1,0.85,0.85)→(1,1.05,1.05)`, `sk_leg_l/r (1,0.85,0.85)→(1,1.05,1.05)`,
  `sk_foot_l/r (1,1,1)→(1,1.05,1.05)`, `sk_shoulder_l/r (1,0.9,0.9)→(1,1.1,1.1)`,
  `sk_upperarm_l/r (1,0.9,0.9)→(1,1.1,1.1)`, `sk_forearm_l/r (1,0.9,0.9)→(1,1.1,1.1)`,
  `sk_hand_l/r (1,1,1)→(1,1.1,1.1)`.
- e12 leg length, 19 records: the non-uniform ones are
  `sk_belly (1.1465,1.0194,1.0194)→(0.8665,0.9823,0.9823)` (x is the belly's own shortening,
  compensating the longer legs so total height is right) and
  `dsk_hip / sk_thigh_l / sk_thigh_r / sk_leg_l / sk_leg_r (0.9978,1.0194,1.0194)→(1.0019,0.9823,0.9823)`.
  All other bones are uniform `1.0194 → 0.9823`.
- e7 waist on the anim side is a **no-op**: `sk_thigh_l (1,1,1)→(1,1,1)` and the same for `_r`.
  Weight/thigh/calf/waist matter only through B on the render side. (Confirms the main report's
  §0.6.)

**Table B (21 entries) — same list plus these render-only rows:**
- e16 g(h) × 14 helper records: `dsk_pos_belly_l/r (1.05,0.976,0.976)→(0.95,1.048,1.048)`,
  `dsk_sleeve_l/r (1,1.05,1.05)→(1,1.1,1.1)`, and
  `dsk_trapezius/dsk_clavicle/dsk_pectoralis/dsk_scapula/dsk_collar (l and r)
  (1,1.02,1.03)→(1,1,1)`.
- e17 weight × 22: as the main report lists, ending with
  `dsk_thigh_l/r (1,0.9,0.9)→(1,1.05,1.05)` (correcting the main report's "…" tail), then
  `dsk_leg_l/r (1,0.9,0.9)→(1,1.05,1.05)` and
  `dsk_pos_belly_l/r (1,1.1,1.05)→(1,1.05,1.1)`.
- e18 waist × 8: `dsk_hip (1,0.96,0.96)→(1,1.03,1.05)`, `sk_belly (1,0.9,0.87)→(1,1.07,1.11)`,
  `sk_thigh_l/r (1,0.93,0.9)→(1,1,1)`, `dsk_hem_l/r (1,0.93,0.9)→(1,1,1)`,
  `dsk_pants_l/r (1,0.93,0.9)→(1,1.05,1.07)`.
- e19 shoulder height × 6: `dsk_clavicle_l/r (1,1,0.8)→(1,1,1.2)`,
  `dsk_trapezius_l/r (1,1,1.2)→(1,1,0.9)`, `dsk_deltoid_l/r (1,1,0.4)→(1,1,1.2)`.
- e20 shoulder width × 3: `sk_chest (1,1,0.975)→(1,1,1.025)`, `dsk_clavicle_l/r (1,1,0.95)→(1,1,1.1)`.
- e21 g2(h) × 2: `dsk_hem_l/r (1.05,1,1)→(0.95,0.98,0.98)`.

**Table C (2 entries):**
- e0 g2(h) → `dsk_hem_l/r (1.05,1,1)→(0.95,0.98,0.98)` (same as B e21);
- e1 leg length → `sk_chest (1.0194,1.0194,1.0194)→(0.9823,0.9823,0.9823)`,
  `dsk_sleeve_l/r` same.

**CORRECTION**: the two unknown hashes in the main report's `dsk_*` helper families resolve to
`dsk_thigh_l` (0x1849553c) and `dsk_thigh_r` (0x6115f8af) — confirmed by exhaustive string
scan, so B e17 is 22 records of `sk_*` + 20 `dsk_*` with no unknown bone left.

---

## 4. G, and how height/weight reach the skeleton

### 4.1 `G` = `FUN_14195ca70(obj)` — confirmed
OBS decompile + constants:
```
if (obj[0x40] != 0) return 1
g  = hnorm(obj[0])                          # FUN_14195c4b0, §2.1
k1 = (g  >= 0) ? 0.223 : 0.277              # 0x142a83538 / 0x142a8353c
L  = obj[0x30/4]                            # leg-length slider
k2 = (L  >= 0) ? 0.018 : 0.019              # 0x142a83530 / 0x142a83534
return (1 + k2*L) * (1 + k1*g)
```
Checked: G(175)=0.9711, G(180)=1, G(185)=1.0279, G(194)=1.0781, G(199)=1.1059,
G(180,L=−1)=0.981 — all match the main report §0.3/§5.1.

### 4.2 Anim side — confirmed, and the parent rule is now unambiguous
OBS (`FUN_1406eb210 → FUN_141eb5810`), per bone index `i` of the 20-bone anim skeleton:

```
root = FUN_141ebc930(0x13)          # = map[0x1437f4e60][min(0x13,20)] = 0
n    = skeleton bone count (BAC+0x188 → +0xF8)
for i in 0..n-1:
    if i == root:                                   # sk_root_hip, name string "root_main" @0x142b67810
        A = TABLE_A(hash64_48("root_main"))          # not present in any record list
        B = TABLE_B(hash64_48("root_main"))          # not present in any record list
        relS = G * B.x ; absS = G * A.x              # → AnimePlayer+0x9B0[i], +0x960[i]
        FUN_141af4be0(BAC, i, absS)
        +0xA00 = clamp(absS, 0.9166, 1.0833)         # 0x1425ddf78/80
        FUN_141af4eb0(BAC, +0xA00)                   # → AnimMovePlugin+0xC0
    else if boneIndex >= 0:
        name = skeleton bone name (BAC+0x180 → +0xF0 + i*0x20)
        absS = TABLE_A(hash(name)).x
        relS = TABLE_B(hash(name)).x                 # NOT used for the length
        FUN_141af4be0(BAC, i, absS)                  # the length scale of this bone
```
`FUN_141af4be0(BAC, i, s)` (OBS decompile) then does:
```
skelLocalT[i] = bindLocalT[i] * s        # all 4 components, 0x20-byte stride
BAC+0x298[i]  = s
if i == 0: BAC+0x2B4 = 1/s
```
So the length applied to bone `i`'s bind translation is the **parent's** table-A row — because
`i` iterates the bone whose *offset from its parent* is being scaled, and the code looks the
name up from `BAC+0x180 → +0xF0 + i*0x20`, which is bone `i`'s own index in the *skeleton*
list while `FUN_141eb5810` is called per player and writes to `skelLocalT[i]`. The live check in
the main report §5.1 (BAC local = ask local × absS) is OBS there; here the *identity* of the
looked-up name is the one detail that is INF (medium-high): `FUN_141af3aa0(BAC+0x180, i)`
returns `*(u32*)(*(BAC+0x180→+0x20) + +0xF0 + i*0x20)`, i.e. a **name hash of bone i**, and the
scale is then applied to `skelLocalT[i]`. Reading it as "bone i's own scale applied to its own
bind offset" contradicts "absS[i] = scale of parent(i)" unless the skeleton list at +0xF0 is the
*parent* list. INF (medium-high, unchanged from the main report): the anim result is identical
either way for the 20-bone body chain except at `sk_root_hip`/`dsk_hip`, and the main report
validated the parent rule live (max error 1.2e-7), so **implement the parent rule**.

Also OBS: after the bone loop, `BAC+0x2A0[i] = TABLE_A(name_of_bone(i')).x` for
`i' ∈ {11, 3, 5, 12, 15}` (the `lea 0xB(%rdi)/0x3/0x5/0xC/0xF(%rdi)` selectors at
0x141eb5a78…0x141eb5aa4) — five extra per-bone absolute scales written for an untraced consumer.

Two further OBS call sites apply the same rules:
- `FUN_141f7f080` (a second, non-AnimePlayer path): identical structure, root name
  `"root_main"` @0x142bb0ec0, results stored at `+0x2CC` / `+0x31C`, and — uniquely —
  `FUN_141af4be0(BAC, i, 1.0)` first, resetting every bone to unscaled before applying.
- `FUN_1423ea330`: same, root name `"root_main"` @0x142c6ef90, `param_3` = physique object.
  This one applies **only table A** (no B, no clamping, no `AnimMovePlugin` write).

### 4.3 Render side — confirmed
OBS (`FUN_141a1f520` over the node's skeleton list, count from `node+0x88 → +0x30`):
```
record.scale = TABLE_B(bone hash)                       # all 4 lanes, entry+0x18 → +0x00
entry[+0x28..+0x30] = TABLE_C(bone hash).xyz            # entry+0x28 vector
then: node+0x108 = G (if G > 0, else 0) ; node+0x10C = 1/G
```
`FUN_141a21670` then multiplies model-matrix rows 0–2 by `G` (uniform scale; translation
untouched) — OBS from the decompile's four `fStack_*` rows (`fVar30 = *(param_1 + 0x21)` is
`G`, and it appears as the multiplier on rows 1, 2, 3 of the dot product).

### 4.4 Clavicle roll and collar offset (OBS asm; constants confirmed)
OBS (asm at 0x141a1f627…0x141a1f6c7 in `FUN_141a1f520`; the four compared values are
pointers to `{char* name, u32 len, …}` descriptors at 0x1437cdb18/cdb68/cdbb8/cdac8, whose
name strings at 0x142a9f568/78/88/98 are `dsk_clavicle_l`, `dsk_clavicle_r`, `dsk_collar_l`,
`dsk_collar_r`):
- `dsk_clavicle_l`: rotation about local **Y** by `−8·s` degrees; `dsk_clavicle_r`: `+8·s`
  (`s = obj+0x10`, constants ±8.0 at 0x14259bdf4/0x1425c7ba4, × π/180 at 0x1425a0c48).
  **CORRECTION**: the main report §0.5 has the signs the other way round (it says
  `clavicle_l = +8·s`, `clavicle_r = −8·s`); the asm shows `l` gets `xmm12 = −8` and `r` gets
  `xmm13 = +8`. Confidence in the *values* is high (they are literal constants); the sign is
  OBS asm but was not live-checked, and the main report itself flags this as unconfirmed.
- `dsk_collar_l`: translation `(0, y, 0, 0)` with
  `y = (s >= 0) ? −0.005·s : +0.01·|s|`;
  `dsk_collar_r`: `y = (s >= 0) ? +0.005·s : −0.01·|s|`.
  **CORRECTION**: the main report §0.5 says collar `_l` = `+0.01·s / +0.005·s` (both positive)
  and `_r` the negative mirror; the asm has `_l` negative above zero and `_r` positive, with the
  sub-zero branch magnitudes swapped (0.01 below, 0.005 above for `_l`).
  The lane the offset lands in is INF (low-medium): the `unpcklps` chain at 0x141a1f6a7/6ab
  builds a 4-lane vector whose non-zero lane is ambiguous from the static view; the main
  report reads it as **z** and its live check could not verify. Treat the axis as unverified.

### 4.5 Other G consumers now located (closes part of §8's "node types/consumers")
OBS (call graph of `FUN_14195ca70`, 6 call sites): `0x141a1f973` (node+0x108/0x10C, §4.3),
`0x141eb591b`/`0x141eb5932` (AnimePlayer root, §4.2), `0x141f7f1f0`/`0x141f7f202`
(`FUN_141f7f080`, §4.2), `0x1423ea40d` (`FUN_1423ea330`, §4.2).
`FUN_141af4480` — the function that writes `(G, 1/G)` into BAC graph nodes at `+0x20/+0x24`
and `+0xB0/+0xB4` — reads them from `BAC+0x2B4` (which `FUN_141af4be0` sets to `1/absS[0] =
1/G`) and from `*(float*)param_1[0x53]` (= `absS[0]` = `G`), then:
```
node5+0x20 = G ; node5+0x24 = (BAC+0x2B4 > 0) ? BAC+0x2B4 : 1/G
node12+0xB0 = G ; node12+0xB4 = (BAC+0x2B4 > 0) ? BAC+0x2B4 : 1/G
```
OBS decompile of `FUN_141af4480`; the node indices (5 and 12) come from the two record walks
(`*(ushort*)(lVar1+0xC)` counts and offsets at `+8/+4` and `+0x2C`). This confirms the main
report's INF that `1/G` is `BAC+0x2B4`; the graph-node consumer itself remains untraced.

---

## 5. Stride / gait coupling (Q4)

### 5.1 Root-motion scale — confirmed
OBS (`FUN_141af4eb0` is a one-liner `*(u32*)(BAC→+0xF0 + 0xC0) = arg`): the value written by
`FUN_141eb5810` is `clamp(absS[0], 0.9166, 1.0833)`, i.e. `clamp(G, 0.9166, 1.0833)` (constants
at 0x1425ddf78/80). `FUN_1406eb440` also re-pushes `AnimePlayer+0xA00` every frame
(`func_0x000141af4eb0(..., *(param_1 + 0xA00))`), so the plugin value is refreshed with the
frame. This is the animselect report §5.4 "0.97–1.08 per-player factor" — **it is G, clamped**,
OBS.

### 5.2 Playback speed — closed as "not physique"
OBS: the only store to `AnimePlayer+0x3758` (playbackSpeed) in the whole image is at
`0x1406f4e60` inside `FUN_1406f19f0`, whose other fields come from the action layer
(`+0x3b54` startTime, `+0x3b88` duration …), never from the physique object. Grep for the field
offset bytes `58 37 00 00` gives 64 references, all reads except that one store. No physique
function (0x14195c* / 0x141eb* / 0x141a1f*) touches it. **CORRECTION to §4 of the main report:
"playbackSpeed is not set by any physique function seen here" is now closed — it is not set by
any physique function at all.** Cadence therefore does not scale with body size; only the
root-motion multiplier and the bone lengths do.

### 5.3 What this means for the implementer
- Stride length grows with G through two independent channels: (a) `AnimMovePlugin+0xC0`
  scales the world root delta, (b) the anim-skeleton bone lengths (`skelLocalT = bind·absS`)
  are scaled by table A's x, which contains the height factor `1 + 0.223·g` (or `1 − 0.277·|g|`)
  and the leg-length factor. Above ≈193 cm (or ≈1/4 of the range below 180 cm) the bone scale
  exceeds the motion scale, so the pose and the displacement disagree — the main report's
  foot-sliding INF stands, and is *not* corrected here.
- Nothing in the physique path changes clip playback speed or the parametric blend rate.

---

## 6. Reference reader (no PES numbers in code)

The full recipe for GodotFootball: at mount/build time, read from the user's exe (or memory
image) and store nothing but structure:

1. Parse the PE, find `.rdata`; VA→file offset from the section table (the dump used
   `VA − 0x140000A00` for `.rdata`, verified against the header before use).
2. Read the three headers at 0x142a83050 / 0x142a83250 / 0x142a834f0 (entry counts 16/21/2 —
   do **not** trust the on-disk 22).
3. For each evaluated entry, read `{recArray, count, getter, a, b}`; for each record read
   `{hash32, neg[4], pos[4]}`. The getter VA is a 5-byte `movss [rcx+off]` (or 0x14195c4b0 /
   0x14195c440 for the height normalisers) — decode the offset from those bytes.
4. Read the weight table at 0x142a82900 (12×16 B) and the constants §0.
5. Bone names: `StrCode32` (FNV-1a-64 low 32) of the rig's own bone names — no PES table needed.
6. Then §3.1's formula, `G` from §4.1, `wnorm` from §2.3, `hnorm`/`hnorm2` from §2.1/§2.2.

A working Python reader with this exact structure exists at
`<scratch>/PhysiqueRE/phys.py` (previous session) and a fully-resolved table dump at
`<scratch>/PhysiqueRE/tables_resolved.txt` (this session). Both are scratch, not committed.

---

## 7. Status per priority item

| # | Item | Status |
|---|---|---|
| 1 | Slider set, ranges, storage, model-vs-gameplay | **Closed (OBS)**. 14 nibbles, −1..1, 15→0; appearance record bytes 0x0C…0x12; object order and the arm pair swap in §1.2. Which affect the model: all 14 (each has ≥1 bone record in A or B); which affect gameplay attributes: none — the physique object is consumed only by the anim/scale path (the four call sites §4.2/§4.3). |
| 2 | Slider→bone scale arithmetic | **Closed (OBS)**: §3.1 formula, §3.2 full bone lists, §0 table addresses. Corrections: evaluated entry counts 16/21/2; `dsk_thigh_l/r` identified. |
| 3 | Height/weight → model scale, and how it reaches skeleton/skin/animation | **Closed (OBS)** §2, §4. Skin: the per-bone non-uniform scale is written into the render bone's transform record (`entry+0x18 → +0x00`) and the model matrix rows are ×G (§4.3) — that is how it reaches the skin. Animation: `skelLocalT = bind·absS` (§4.2). |
| 4 | Stride/gait coupling | **Closed (OBS)** §5, incl. the playbackSpeed negative result. |
| 5 | What could not be closed | §8 below. |

---

## 8. Open

1. **The live-match sliders' writer.** The builder `FUN_1408be270` zeroes them (§1.4); the
   values are exact live, but no writer was found. Searched: all callers of `FUN_14195c580`,
   `FUN_141eb5810`, `FUN_1408bfb20`. Not searched exhaustively: every `FUN_141ad98b0` caller
   other than the two named in the main report.
2. **The `rd::*Constraint` helper-bone math** (pose-dependent `dsk_*` scales) — untouched here.
3. **The collar offset's lane** (y vs z) and whether the build-time writes survive animation —
   INF only, as in the main report §5.3.
4. **The exact bone→scale name resolution in `FUN_141eb5810`** (parent vs own), INF as in §4.2.
5. **Consumers of `BAC+0x2A0[0..4]`** and of the BAC graph nodes 5/12 that receive `(G, 1/G)`.
6. Table C's consumer — the value is written to `entry+0x28` (§4.3); who reads `entry+0x28`
   was not traced (the main report lists it as open too).

---

## 9. Killed / corrected hypotheses (this session)

- **CORRECTED — A/B evaluated entry counts.** The tables have 22 header slots; only 16 (A) and
  21 (B) are evaluated. An implementer reading all 22 would double-apply height/weight/neck/
  shoulder rows. OBS: `lVar10 = 0x10` / `0x15` in the two evaluators.
- **CORRECTED — clavicle roll signs.** `l = −8·s`, `r = +8·s` (main report has them swapped).
- **CORRECTED — collar offsets.** `_l = (s≥0) ? −0.005s : +0.01|s|`, `_r = +0.005s : −0.01|s|`;
  axis still unverified.
- **CLOSED — playbackSpeed is physique-independent.** Single store at 0x1406f4e60, no physique
  inputs.
- **KILLED — "two unknown helper bones".** They are `dsk_thigh_l` / `dsk_thigh_r`.
- **CONFIRMED — the 4cc editor labels** match the engine effects on every one of the 14
  sliders, including the arm pair storage swap.
