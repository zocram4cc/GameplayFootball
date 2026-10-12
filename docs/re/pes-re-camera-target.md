# PES 2021 — in-play broadcast camera: provisional inputs (CameraTargetRE-2)

Scope: close items 1–7 of the assignment for `Dynamic Wide` (mode 5) and the record build.
Read-only. No process was running this session (`pgrep -f PES2021.exe` empty; last wineserver
activity 23:02 vs. session start later). All live evidence is therefore from the **previously
captured 20:39 live samples** recorded in `docs/re/pes-re-stadium-camera.md` §4.6, plus a 64 MB
image dump `<scratch>/pesmem.bin` (2026-10-12 18:52) and the on-disk `PES2021.exe`.

## Runtime-readable sources

| Source | Rule |
|---|---|
| `PES2021.exe` `.trace` + `.rdata` | file offset = VA − `0x140000A00` |
| `PES2021.exe` `.data` | file offset = VA − `0x140000E00` |
| `PES2021.exe` `.data1` | VA `0x143ca1000` → file `0x38a8a00` (delta `0x140000E00`-family, verify per section) |
| `common/match/constant/constant_match.bin` → `cameraInplay.o` | already handled by `src/pes/camera.cpp` |

OBS = observed with the method given; INF = inferred with confidence.

## Corrections applied (second and third passes — raw asm re-derivation)

The first pass trusted Ghidra's variable names in three places, and the second pass dropped one
scaling instruction. All re-derived from the raw instruction stream (capstone over
`<scratch>/pesmem.bin` / the on-disk exe):

1. **`u` and `h` are BOTH scaled by `100.0` and BOTH divided by it — there is no asymmetry.**
   `h = clamp(height,0,1)·100` at `0x1408a1e8a` and **`u = (1 − clamp(zoom,0,1))·100` at
   `0x1408a1ebf`** (`mulss xmm8,xmm10`, bytes `f3 45 0f 59 c2` — the instruction the second pass
   omitted). The third pass's claim that `u` was a 0…1 fraction was wrong; it produced
   `Fov = 24.02°` instead of the observed `23.26°`. → items 5a, 6.
2. **`fVar44 + fVar39` is `[rbp-0x78]` = `record.X_limit`**, not `target.x + X_limit`; and the
   blend is `(L+1) − ((L+1) − X_limit)·k` with `L = halfLength`, so
   `xHi = X_limit + (L+1)·(1−k)`. → item 4.
3. **The `0x1408a3afb` stateful offset is not `Mani`.** Its magnitude is `xmm13` (= `8.268`
   at `.rdata 0x14260b8f4` in the mode-5/state-3 case, else the stored target x or 0), and
   `Mani` (`record+0x00`) is never read by the final pose at all. → item 2.
4. **The `t` factor's low bound is negated:** `lo = −(halfWidth − marginUpper) = −24.06`,
   `hi = +(halfWidth − marginUnder) = +29.06` (the `xorps` at `0x1408a1ee8` flips the sign).
   The numerical `t = 0.302334` was already correct; only the written range was wrong. → item 5a.

---

**Item 1 — `target.y`'s ball-height term**

**Rule (OBS, `0x1408a3e73`–`0x1408a3eae`, `FUN_1408a2180`):**

```
lift = clamp( (ballFocusY + 4) * (60/fps), 0, 5 )
target.y = LookY + lift
```

- `ballFocusY` = the **y of the raw focus point** — the value read at `0x1408a3d54`
  (`movss xmm10, [rsi+4]`, where `rsi` is the raw focus triple built at `0x1408a3571`–
  `0x1408a3592`); **not** the sim ball y, not a predicted landing point.
- `4` = `.rdata 0x1425656c0` = `4.0` (OBS, byte read; added at `0x1408a3e7d`).
- `60/fps`, with `fps` the `.data 0x14351ae54` float = **54.0** (OBS; its setter `0x141e532f0`
  stores `54.0` read from `.rdata 0x14259d624`). So the factor is **1.111111…**.
  Loaded at `0x1408a225c` (`xmm11 = 60/fps`).
- `5` = `.rdata 0x14259222c` (OBS; the clamp upper at `0x1408a3e86`–`0x1408a3e9d`),
  lower clamp = 0 (`xmm12`, OBS).
- `LookY` = the record field at `+0x0C`, added at `0x1408a3eae`
  (`addss xmm3, [rax+0xc]`, where `rax = [rbp+0xa0]` = the record pointer, OBS).

There is one override: if `byte[rbp-0x7d] != 0 && byte[rbp+0x30] != 0` the lift is replaced by
`0.5` (`.rdata 0x142565600`, `0x1408a3e6b`) — OBS; INF (high) that `[rbp-0x30]` is the
`isTargetKeepPlayerEnable`-style "keep player" flag, since it is set at `0x1408a2811` from a
match-state byte.

**Cross-check against the live 20:39 samples (OBS, `docs/re/pes-re-stadium-camera.md` §4.6).**
Those samples give `target.y = 0.90` constant and `LookY = 0.5`, with the ball on the ground.
Substituting: `0.90 − 0.50 = 0.40 = clamp((y+4)·1.1111, 0, 5)` ⇒ `y = −3.64`, which is not a
plausible focus height. **So the live sample is consistent only with the lift clamping at 0**,
i.e. `focusY ≤ −4`. That is possible (a camera focus anchored at/past the touchline plane), but
it cannot be confirmed without an in-air sample.

**Status: formula OBS, numeric confirmation OPEN.** No live sample with the ball in the air
exists (the game is not running), so the `+4` / `60÷54` factors were verified against the binary
only. The repo's current behaviour (`LookY + clamp(lift, 0, 5)`, lift 0 on the ground) reproduces
the live 0.90 exactly, so nothing in the pose test changes.

**Coder recipe.** `target.y = LookY + clamp((focusY + 4.f) * (60.f/54.f), 0.f, 5.f)`, or with the
`0` override, `0.5f`. `LookY` = `cameraInplay.o` `paramMax.paramTable.LookY` (in-memory offset
`+0x68`, sorted-key order, §4.5) for the far side, `paramMin.paramTable.LookY` (`+0x9C`) for the
near side — see item 5 for the blend.

---

**Item 2 — the raw focus point the smoothing consumes, and `Mani`**

**Raw focus (OBS, `FUN_1408a2180` `0x1408a3498`–`0x1408a3592`).** The camera keeps a previous
target and a delta on the plugin object:

```
[InplayCamera + 0x38] = previous target      (3 f32, set at 0x1408a34ec / 0x1408a349c)
[InplayCamera + 0x44] = previous delta      (3 f32, set at 0x1408a3509)
raw.x = delta.x + prevTarget.x              0x1408a3571 / 0x1408a3579
raw.y = delta.y + prevTarget.y              0x1408a357d / 0x1408a3582
raw.z = delta.z + prevTarget.z              0x1408a358d / 0x1408a3592
```

then a dead-zone is applied to `raw.x` (`[rbp+0x10]`, `0x1408a359f`–`0x1408a35bd`) and `raw.z`
(`xmm15`, `0x1408a35cd`–`0x1408a35df`).

The **source** of the delta is the per-situation focus table reached through
`[this+0x1e8] + 0x24840`, indexed 0/1 by the situation word computed at `0x1408a2b19`–`0x1408a2c3d`,
with an 11-slot search (`[r8 + 0x4c00 + 4·k]`, `0x1408a2bf0`–`0x1408a2c0c`) that can replace the
record. INF (high): this is `registry::BallInfo`/ball-carrier focus as the game tracks it.

**`Mani` has no effect on the pose at all.** OBS: the record pointer (the 9-float block) is
captured at `0x1408a2232` into `[rbp+0xa0]` and is read **only** at `0x1408a3faa`
(`[rax+0x10]` = PosZ), `0x1408a3fbe` (`[rax+8]` = PosY), `0x1408a3fc7` (`xmm1` = PosZ) and
`0x1408a3eae` (`[rax+0xc]` = LookY). `record.Mani` (`+0x00`) is written by `FUN_1408a1a70`
(record `+0x00`, near value `paramMin.Mani@CameraInplay+0xA0`) and **never read in the final
pose**. There is no velocity lead using `Mani` in this build.

**The state-dependent horizontal offset of the stored target (OBS, `0x1408a3aca`–`0x1408a3b03`)
has nothing to do with `Mani`.** Its magnitude lives in `xmm13`, whose value depends on the
camera state:

| `xmm13` source | value | site |
|---|---|---|
| mode 5, `r12b != 0`, `[r15+0x116c] != 1`, `[r15+0x1554] == 3` | `8.268` (`.rdata 0x14260b8f4`) | `0x1408a2a34` |
| stored target x (`xmm8 = [rbx]`, rbx = `[r14+0x38]`) | the stored target x itself | `0x1408a259a` |
| otherwise | `0` | `0x1408a2679`, `0x1408a2c9d` |

and the gate, with `eax = [rbp+0x14]` = the match-state word from `[r12+0x1554]` (`0x1408a23c3`):

| state `eax` | rule | site |
|---|---|---|
| `1` or `4` | whole block skipped → **no offset** | `0x1408a3ad6`, `0x1408a3adb` |
| `2` | magnitude × `0.7` (`.rdata 0x14257a194`) | `0x1408a3af3` |
| any other | full magnitude | fallthrough |

The sign comes from `xmm10` (`0x1408a3add`–`0x1408a3ae7`; negation constant `.rdata 0x1425656d0`
= `-0.0`), and the result is stored back at `0x1408a3aff`/`0x1408a3b03`
(`storedTarget.x -= sign·magnitude`).

So: **state 1 and 4 do nothing; state 2 subtracts `0.7·|8.268|`; every other state subtracts
`|8.268|` — and because the magnitude is only non-zero in the mode-5 + state-3 case, the
practical effect is: mode 5, state 3 (not 1/2/4), the stored target x is pulled
`sign(xmm10)·8.268` metres.** INF (medium) that this is a set-piece/goal-side bias; only the
constant and the gating are OBS.

**The lead that does exist (OBS, `0x1408a3108`–`0x1408a31ff`).** Between the raw focus `R` and the
ball `B` the camera blends:

```
d3   = |R - B|_xyz                       (0x1408a3166, sqrtf)
dxz  = |R - B|_xz                        (0x1408a312e, sqrtf)
a    = d3 / dxz                          (0x1408a3190)
w    = a * 100 * 0.7 / 100  = 0.7*a      (0x1408a31ab: 0x14253ee30=100, 0x14257a194=0.7)
out  = w*R + (1-w)*B                     (0x1408a31c1 … 0x1408a31ff, per axis)
```

`0x1425a2670 = 400` and `0x14259be10 = 16` (`0x1408a379f`–`0x1408a37b0`) are a separate clamp on
the **smoothing weight**, not on this blend.

| off | field | read by final pose |
|---|---|---|
| +0x00 | Mani | no |
| +0x04 | Fov | `out+0x30 = Fov·π/180` (`0x1425a0c48`) |
| +0x08 | PosY | `0x1408a3fbe` |
| +0x0C | LookY | `0x1408a3eae` (target.y) |
| +0x10 | PosZ | `0x1408a3faa`, `0x1408a3fc7` |
| +0x14 | Z_min | `0x1408a23f7` → `[rbp-0x1c]` |
| +0x18 | Z_max | `0x1408a23d0` → `[rbp-0x20]` |
| +0x1C | X_limit | `0x1408a23d8` → `[rbp-0x78]` |
| +0x20 | Pan | `[rbp+0x78]` |

(This corrects the doc's §4.3 "record #N" naming only in that Z_max/X_limit/Pan are contiguous
with the rest; the nine values are one contiguous block at `&uStack_f8`.)

---

## Item 3 — update rate of the smoothing and the k spring

**Both are stepped once per `InplayCamera::update` call, i.e. per rendered frame — OBS:**

- `InplayCamera::update` = vtable slot 4 = `0x14087f280`; it has no caller except the virtual
  dispatch (no `E8` call site exists anywhere in `.trace`, OBS by an exhaustive byte scan), so the
  cadence is whatever drives the camera plugin — the render path.
- the mode-5 ball-side factor `t` and the pose are recomputed in the same call
  (`FUN_1408a1a70` → `FUN_1408a2180`, single call chain `0x14087f398` → `0x1408a216e`, OBS);
- `k` spring update at `0x1408a3ea2`–`0x1408a3f11` is inside `FUN_1408a2180` (OBS);
- target smoothing at `0x1408a3cd1`–`0x1408a3d41` is also inside `FUN_1408a2180` (OBS).

**Hz.** The **frame-rate reference is 54 Hz**, not 60 — OBS: every `call 0x141e532d0` returns the
constant `54.0` from `.data 0x14351ae54` (`movss xmm0, [0x14351ae54]; ret`), and the constant's
setter `0x141e532f0` stores `54.0` read from `.rdata 0x14259d624`. This 54 appears in:

| site | expression |
|---|---|
| `0x1408a225c` (`FUN_1408a2180` head) | `xmm11 = 60 / fps` → **1.111111**, used as the target.y lift factor (item 1) and as the `[this+0x34]` step in `0x1408a3639`–`0x1408a36da` |
| `0x1408a2498` | `cameraMoveMarginTime` value × `4` → `xmm7`, and `xmm6 = (f32)(matchState−[r15+0x10])` → a time in frames |
| `0x1408a2481` | `cameraMoveMarginTime` value read from `+0x28` (the `7` of §4.5) |
| `0x1408a2c5f` | `0.25 * fps` (dead-zone guard) |
| `0x1408a380c` | `48 / fps` → `xmm7` = the smoothing weight's per-frame increment |

**The repo's provisional "60 Hz step" is therefore wrong on two counts:** the reference is 54 Hz,
and the step is **per rendered frame**, not per logic tick. The value `60` at `0x14259a764` is the
numerator of a ratio, not a tick rate.

**Target smoothing (OBS, `0x1408a3cd1`–`0x1408a3d41`):**

```
tgt.x = (prev.x + w·dx) / (1 + w)      dx = raw.x − prev.x    (likewise y, z)
```

with a **running** weight `w` at `.data 0x1434a00b0` (image default `1.0`, OBS read from the file),
updated once per call at `0x1408a39f2`–`0x1408a3a4a`:

```
if (marginTimeActive)  w = min(w * 1.1, 10000)     # 0x1425a3590 = 1.1, 0x14259d634 = 10000
else                   w = max(w / 1.1, 1)
```

and additionally set to `1.0` on the reset path (`0x1408a3a39`, `0x1408a3a5c`) and when
`[this+0x24] != 0` (the "free/locked" flag). `marginTimeActive` is
`[rbp-0x40]==2 && [r15+0x11d4]!=2 && [rbp-0x7e]!=0`, where `[rbp-0x7e]` is one of the
match-state flags raised by the jump table at `0x1408a22a1` (state 5 → `0x1408a22cd`).
**With w = 1 the camera moves half the remaining distance per frame** — an exponential, not a
first-order lag with a fixed weight. The repo's `tgt = (raw + 10·prev)/11` is a *different*
(heavier) filter: OBS it does not match the game.

**Dead zone (OBS, `0x1408a25b8`–`0x1408a2629`).** Default `dz = (zoom·1.14286 − 0.0142857)·2`
(`0x14260b8ec`, `0x14260b8d4`); for mode 5 with `cameraMoveAreaSize.isCustomize`
(`[CameraInplay+0x18]`) the two values `Fov` (`+0x1C`) and `Pan` (`+0x20`) replace it. The
30 (`0x142591140`) / 10 (`0x142588210`) pair at `0x1408a261f`–`0x1408a265c` are the
`cameraMoveMarginTime` {30, 7}-style defaults read from `CameraInplay+0x28/+0x2C`.

**k spring (OBS, `0x1408a3ec8`–`0x1408a3f11`) — matches the repo:**

```
if ([this+0x24] == 0) {
    v' = clamp((goal − k)*0.02 − v,  −0.005k … +0.005k)     # 0x1425c4894, 0x1425b8708, 0x14260a24c
    v += v';  k += v
} else { k = goal;  v = 0;  [this+0x100] = 0; }
```

### 3a. What this means for an engine at another frame rate — **read before coding**

The step is **once per rendered frame** (OBS, §3 above). The constants `0.02`, `±0.005`, `0.7`,
`1.1`, `1.0` are all *per frame*, and the two frame-count-like quantities are derived from the
54 Hz reference (`60/fps` at `0x1408a225c`, `48/fps` at `0x1408a3605`/`0x1408a380c`). An engine
that renders at a different rate **must scale by elapsed time, not step a fixed number of times
per second** — otherwise the smoothing, the `k` spring and the ball-height lift all run at the
wrong speed.

The exact per-frame recurrences (all OBS, `FUN_1408a2180`):

```
# 1. running smoothing weight w  (.data 0x1434a00b0)
#    image default = 1.0  (OBS: bytes 00 00 80 3f at file offset 0x1434a00b0 − 0x140000E00)
w0 = 1.0
if ([rbp-0x40] == 2                       # match-state word from [r12+0x116c] (0x1408a23ff)
    && [r15+0x11d4] != 2                  # 0x1408a39e3
    && byte[rbp-0x7e] != 0)              # 0x1408a39ed — set 1 only by jump-table index 4,
                                         #   i.e. match-state word−1 == 4 (0x1408a22cd)
    w = min(w * 1.1, 10000)               # 0x1408a1fa4 x·1.1 (0x1425a3590), cap 0x14259d634
else
    w = max(w / 1.1, 1)                   # 0x1408a1a24 ÷1.1; the 1.0 clamp is 0x3f800000 at 0x1408a3a39
# and w is forced to 1.0 on the reset path (0x1408a3a39) and when [this+0x24] != 0 (0x1408a3a5c)

# 2. target smoothing, per axis (0x1408a3cd1 … 0x1408a3d41)
tgt = (prev + w·(raw − prev)) / (1 + w)
#    with w = 1 (settled):  tgt = (prev + raw)/2   — half the remaining distance per frame

# 3. k spring (0x1408a3ec8 … 0x1408a3f11)
v' = clamp((goal − k)·0.02 − v, −0.005·k, +0.005·k)
v += v';  k += v                            # or, on reset: k = goal, v = 0

# 4. ball-height lift (item 1)
lift = clamp((focusY + 4)·(60/fps), 0, 5)  # fps = 54  ->  factor 1.111111
```

To make these frame-rate independent, replace each "per frame" factor `c` with
`c · (fps_now / 54.0)` (first-order, OBS-derived constants scaled by the frame-time ratio) and
treat `w`'s `×1.1` / `÷1.1` as a per-frame rate to be raised to the elapsed-frame count.
INF (medium) that PES itself simply assumes a fixed 54 Hz frame and does not scale — the `fps`
global is *read* but never varied, so the safest 1:1 reproduction is to step the whole camera
update at a fixed 54 Hz cadence regardless of render rate.

---

## Item 4 — `target.x` / `target.z` clamps

**The starting bound `fVar44 + fVar39` in `d3a.txt:1184` is `[rbp-0x78]` = `record.X_limit`**, not
`target.x + X_limit`. Ghidra's names misassigned it; the raw asm is unambiguous:
`0x1408a23d8 movss xmm7,[rbx+0x1c]` then `0x1408a2412 movss [rbp-0x78],xmm7`, with
`rbx = r8 = the record pointer` (`0x1408a21fa`). The three later branches `0x1408a273c` /
`0x1408a27ad` / `0x1408a289f` that could add to that slot are all gated on
`byte[rbp-0x7d] != 0 && byte[rbp+0x30] != 0`, so in the plain in-play path it stays `X_limit`.

Exact operands (all `FUN_1408a2180`, OBS):

| site | instruction | meaning |
|---|---|---|
| `0x1408a2215` | `movss xmm10,[rax]` | `rax = [rcx+0x120]` = FieldInfo; `[rax+0]` = halfLength **52.56** |
| `0x1408a221a` | `movss xmm6,[rax+4]` | halfWidth **34.06** |
| `0x1408a221f` | `movss [rbp+0x90],xmm10` | halfLength saved |
| `0x1408a2228` | `movss [rbp-0x58],xmm6` | halfWidth saved |
| `0x1408a23d8` | `movss xmm7,[rbx+0x1c]` | `record.X_limit` |
| `0x1408a2412` | `movss [rbp-0x78],xmm7` | **the x-bound base** |
| `0x1408a23d0` | `movss [rbp-0x20],[rbx+0x18]` | `record.Z_max` |
| `0x1408a23f7` | `movss [rbp-0x1c],[rbx+0x14]` | `record.Z_min` |
| `0x1408a392f` | `movss xmm7,[rip+0x1cc74e5]` | `.rdata 0x14256ae1c` = **1.0** |
| `0x1408a3f17` | `movss xmm6,[rbp-0x58]` | halfWidth |
| `0x1408a3f21` | `addss xmm6,[rip+0x1ce833b]` | `.rdata 0x14258c264` = **3.0** → `xmm6 = W+3` |
| `0x1408a3f29` | `movss xmm4,[r14+0x68]` | `k` |
| `0x1408a3f2f` | `movaps xmm0,xmm9` | `xmm9 = L+1` (set `0x1408a3f1c`) |
| `0x1408a3f33` | `subss xmm0,[rbp-0x78]` | `(L+1) − X_limit` |
| `0x1408a3f4a` | `mulss xmm0,xmm4` | `·k` |
| `0x1408a3f52` | `subss xmm9,xmm0` | `xHi = (L+1) − ((L+1)−X_limit)·k` |
| `0x1408a3f3e` | `xorps xmm2,[rip+0x1cc178b]` | `.rdata 0x1425656d0` = `-0.0` → `xmm2 = −(W+3)` |
| `0x1408a3f45` | `subss xmm1,[rbp-0x20]` | `(W+3) − Z_max` |
| `0x1408a3f4e` | `mulss xmm1,xmm4` | `·k` |
| `0x1408a3f5f` | `subss xmm6,xmm1` | `zHi = (W+3) − ((W+3)−Z_max)·k` |
| `0x1408a3f5a` | `subss xmm0,[rbp-0x1c]` | `−(W+3) − Z_min` |
| `0x1408a3f67` | `mulss xmm0,xmm4` | `·k` |
| `0x1408a3f6b` | `subss xmm2,xmm0` | `zLo = −(W+3) − (−(W+3)−Z_min)·k` |
| `0x1408a3f63`–`0x1408a3f88` | `comiss xmm8,xmm9` ; `comiss −xmm9,xmm8` | `target.x = clamp(focus.x, ±xHi)` |
| `0x1408a3f8c`–`0x1408a3f9e` | `comiss xmm13,xmm6` ; `comiss xmm2,xmm13` | `target.z = clamp(focus.z, zLo, zHi)` |

`xmm8 = focus.x` and `xmm13 = focus.z` come from `0x1408a3d4f` / `0x1408a3d5a`
(`movss xmm8,[rsi]`, `movss xmm13,[rsi+8]`, `rsi` = the smoothed focus triple).

**The rules (OBS):**

```
L = FieldInfo.halfLength          # 52.56
W = FieldInfo.halfWidth           # 34.06
xHi = (L + 1) − ((L + 1) − X_limit)·k
zHi = (W + 3) − ((W + 3) − Z_max)·k
zLo = −(W + 3) − (−(W + 3) − Z_min)·k
target.x = clamp(focus.x, −xHi, +xHi)
target.z = clamp(focus.z, zLo, zHi)
```

**What the `1` and the `3` are.** Both OBS as float constants: `1.0` at `.rdata 0x14256ae1c`
(loaded at `0x1408a392f` into `xmm7`, added at `0x1408a3f1c`), `3.0` at `.rdata 0x14258c264`
(added at `0x1408a3f21`). **Meaning INF (medium-high):** the `+1` widens the x bound one metre
beyond the pitch half-length before the k-interpolation and the `+3` widens the z bound three
metres beyond the pitch half-width — i.e. a small "safety margin" so the camera frame is not
flush with the pitch edge when `k ≠ 1`. The same `3.0` (`.rdata 0x14258c264`) is reused as the
`Fov_near` slope in `FUN_1408a1a70` (`0x1408a1f9b`) and in `offset.y = (rbp+0x10)+3`
(`0x1408a284d`, `0x1408a288b`); the `+4` in the lift is a *different* constant (`.rdata
0x1425656c0`).

Numerically, at `k = 1` (settled) the clamps collapse onto the record values — which is what the
live samples show. For the repo's Dynamic Wide (`X_limit` blended ≈ 41.01, `Z_min = −8`,
`Z_max = 21.921`):

| k | xHi | zHi | zLo |
|---|---|---|---|
| 1.00 | 41.010 | 21.921 | −8.000 |
| 1.20 | 38.500 | 18.893 | −2.188 |
| 0.80 | 43.520 | 24.949 | −13.812 |

**Live check (OBS, §4.6 sample 20:39).** `target.x = −5.31`, `target.z = −8.00`, `k ≈ 1.001`.
`xHi = 41.01`, `zHi ≈ 21.90`, `zLo ≈ −8.01` — the target is just inside the lower z bound (it is
the clamped value itself, so this is circular) and far inside the x bound. The sample therefore
confirms the `k=1` collapse but cannot discriminate the `+1`/`+3` margins (no live sample with the
play near the touchline).

**Final pose (OBS, `0x1408a3faa`–`0x1408a3ffc`) — confirms
`docs/re/pes-re-stadium-camera.md` §4.3 exactly.** `r13 = param_2 = the output struct`
(`0x1408a2239 mov [rbp+0x88],rdx`, then `0x1408a36f1 mov r13,[rbp+0x88]`), `rax = [rbp+0xa0]`
= the record, `xmm4 = k`:

```
out+0x00 = target.x    (xmm8)    0x1408a3fcc
out+0x04 = target.y    (xmm3)    0x1408a3fd2
out+0x08 = target.z    (xmm13)   0x1408a3fe1
out+0x0C = target.x·(1/pan_eff − 1)·k + target.x   0x1408a3faf,0x1408a3fc3,0x1408a3fdc,0x1408a3ff7
out+0x10 = record.PosZ·record.PosY·k               0x1408a3fbe,0x1408a3fd8
out+0x14 = record.PosZ·k + target.z                0x1408a3fba,0x1408a3fc7
```

with `pan_eff` the 4th argument (`xmm3` → `[rbp+0x78]` at `0x1408a222d`). Equivalently
`cam.x = target.x·(1 + k·(1/pan_eff − 1))`.

**Confirmed record layout (OBS).** The block is one contiguous 9-float record at `&uStack_f8`
(frame resolved through `lea rbp,[rsp-0x47]`); `rbx = r8 = the record pointer`:

| off | field | written by `FUN_1408a1a70` | read by `FUN_1408a2180` |
|---|---|---|---|
| +0x00 | Mani | `[rsp+0x20]` | never (OBS) |
| +0x04 | Fov | `[rsp+0x24]` | `out+0x30 = Fov·π/180` (`0x1425a0c48`, `0x1408a2156`) |
| +0x08 | PosY | `[rsp+0x28]` | `0x1408a3fbe` |
| +0x0C | LookY | `[rsp+0x2c]` | `0x1408a3eae` (target.y) |
| +0x10 | PosZ | `[rsp+0x30]` | `0x1408a3faa`, `0x1408a3fba` |
| +0x14 | Z_min | `[rbp-0x7d]`→`[rbp-0x1c]` | `0x1408a23f7` |
| +0x18 | Z_max | `[rbp-0x79]`→`[rbp-0x20]` | `0x1408a23d0` |
| +0x1C | X_limit | `[rsp+0x38]`-sibling | `0x1408a23d8` |
| +0x20 | Pan | `[rsp+0x3c]` | `0x1408a3fb2` (`1/pan_eff`) |

---

## Item 5 — Dynamic Wide slider defaults and where the values live

### 5a. `u` and `h` — exact expressions (this is the load-bearing part)

**The two slider operands are `[InplayCamera+0xC]` (zoom) and `[InplayCamera+0x10]` (height),
plain floats clamped to `[0, 1]`. BOTH are then scaled by the same `100.0` and BOTH are divided
by it at the point of use, so both live in 0…100 and the ÷100 cancels for both. **There is no
asymmetry.** OBS, `FUN_1408a1a70`, decoded from `<scratch>/pesmem.bin` RVA `0x8a1e40` with bytes shown:

```
0x1408a1e5b  f3440f10470c      movss  xmm6, [rdi+0x10]         # the HEIGHT slider
0x1408a1e60  0f57ff            xorps  xmm7, xmm7               # xmm7 = 0
0x1408a1e63  410f2ff7          comiss xmm6, xmm7
0x1408a1e66  7205              jb     0x1408a1e6d
0x1408a1e68  0f28f7            movaps xmm6, xmm7               # height <= 0 -> 0
0x1408a1e6b  eb0a              jmp    0x1408a1e77
0x1408a1e6d  410f2ff5          comiss xmm6, xmm13              # xmm13 = 1.0
0x1408a1e71  7204              jb     0x1408a1e77
0x1408a1e73  450f28f5          movaps xmm6, xmm13              # height >= 1 -> 1
0x1408a1e77  f3440f10470c      movss  xmm8, [rdi+0xc]          # the ZOOM slider
0x1408a1e7d  410f2ff8          comiss xmm8, xmm7
0x1408a1e81  f3440f1015a6cfc901 movss xmm10, [rip+0x1c9cfa6]   # .rdata 0x14253ee30 = 100.0
0x1408a1e8a  f3410f59f2        mulss  xmm6, xmm10              # h = clamp(height,0,1) * 100
0x1408a1e8f  7206              jb     0x1408a1e97
0x1408a1e91  450f28c5          movaps xmm8, xmm13              # zoom >= 1 -> 1
0x1408a1e95  eb1a              jmp    0x1408a1eb1
0x1408a1e97  450f2fc5          comiss xmm8, xmm13
0x1408a1e9b  7206              jb     0x1408a1ea3
0x1408a1e9d  440f28c7          movaps xmm8, xmm7               # zoom <= 0 -> 0
0x1408a1ea1  eb0e              jmp    0x1408a1eb1
0x1408a1ea3  f3450f5cc5        subss  xmm8, xmm13              # zoom − 1
0x1408a1ea8  f3440f59056763ce01 mulss xmm8, [rip+0x1ce6367]    # .rdata 0x142588218 = −1.0 -> 1 − zoom
0x1408a1eb1  488b8720010000    mov    rax, [rdi+0x120]         # FieldInfoRef
0x1408a1eb8  488b8f48010000    mov    rcx, [rdi+0x148]
0x1408a1ebf  f3450f59c2        mulss  xmm8, xmm10              # <<< u = (1 − zoom) * 100  — the SAME 100 as h
```

So, with `xmm13 = 1.0`:

```
u = (1 − clamp(zoom,   0, 1)) * 100          # range 0 … 100
h = (    clamp(height, 0, 1)) * 100          # range 0 … 100
xmm10 = 100.0  (.rdata 0x14253ee30)
```

Both are divided by `xmm10` at every point of use, so **`u/100 = 1 − zoom` and `h/100 = height`.**
This is the form to implement.

```
# FAR side (t = 0) — taken when byte[CameraInplay+0x60] != 0 (paramMax.isCustomize)
#                    AND byte[CameraInplay+0x88] != 0            [0x1408a1f24, 0x1408a1f2a]
0x1408a1f33  movss  xmm11, [rip+0x1cf9edc]   # .rdata 0x14259be18 = 18.0
0x1408a1f3c  subss  xmm11, [rbx+0x64]        # 18 − paramMax.Fov
0x1408a1f42  movss  xmm12, [rip+0x1cf9955]   # .rdata 0x14259b8a0 = 0.52
0x1408a1f4b  subss  xmm12, [rbx+0x74]        # 0.52 − paramMax.PosY
0x1408a1f65  mulss  xmm11, xmm8              # (18 − Fov_max) · u
0x1408a1f6a  mulss  xmm12, xmm6              # (0.52 − PosY_max) · h
0x1408a1f6f  divss  xmm11, xmm10             # ÷ 100
0x1408a1f74  divss  xmm12, xmm10             # ÷ 100
0x1408a1f79  addss  xmm11, [rbx+0x64]        # + paramMax.Fov
0x1408a1f7f  addss  xmm12, [rbx+0x74]        # + paramMax.PosY
  =>  Fov_far  = Fov_max  + (18   − Fov_max ) · u / 100
      PosY_far = PosY_max + (0.52 − PosY_max) · h / 100
```

```
# NEAR side (t = 1) — hard-coded, OBS
0x1408a1f9b  mulss  xmm8, [rip+0x1cea2c0]    # .rdata 0x14258c264 = 3.0     -> u·3
0x1408a1fa9  mulss  xmm6, [rip+0x1d6992b]    # .rdata 0x14260b8dc = 0.0702  -> h·0.0702
0x1408a1fb1  movss  xmm0, [rip+0x1d69943]    # .rdata 0x14260b8fc = 28.698
0x1408a1fc7  divss  xmm8, xmm10              # u·3 ÷ 100
0x1408a1fcc  divss  xmm6, xmm10              # h·0.0702 ÷ 100
0x1408a1fd1  subss  xmm0, xmm8               # 28.698 − 3u/100
0x1408a1fd6  addss  xmm6, [rip+0x1d69902]    # .rdata 0x14260b8e0 = 0.3798
0x1408a203b  movss  xmm0, [rip+0x1d698bd]    # .rdata 0x14260b900 = 35.44217300415039
  =>  Fov_near  = 28.698 − 3·u / 100
      PosY_near = 0.3798 + 0.0702·h / 100
      PosZ_near = 35.44217300415039
```

**The coder-visible simplification (algebraically identical, OBS) — implement this form:**

```
u = (1 − zoom) · 100                     # 0…100
h = height · 100                          # 0…100

Fov_far   = Fov_max  + (18    − Fov_max) · u / 100      # = Fov_max  + (18 − Fov_max)·(1 − zoom)
PosY_far  = PosY_max + (0.52  − PosY_max)· h / 100      # = PosY_max + (0.52 − PosY_max)·height
Fov_near  = 28.698   − 3.0   · u / 100                  # = 28.698 − 3·(1 − zoom)
PosY_near = 0.3798   + 0.0702 · h / 100                 # = 0.3798 + 0.0702·height
PosZ_near = 35.44217300415039
value     = far + (near − far)·t
```

Both simplified forms are exactly what the disassembly computes; the `u/100` / `h/100` pair is the
uniform scaling, and **both cancel the same way** — nothing is asymmetric about them.

**Blend (OBS, `0x1408a1fc2`, `0x1408a1fe8`, `0x1408a1ff0`, `0x1408a1ffa`):**
`xmm7 = t·100` then the term is `·xmm7 ÷ 100`, so the weight is exactly `t`:

```
value = far + (near − far)·t
```

**Ball-side factor `t` (OBS, `0x1408a1eb1`–`0x1408a1f1f`):**

```
0x1408a1ec4  movss  xmm14, [rax+4]           # rax = [rdi+0x120] = FieldInfo; halfWidth = 34.06
0x1408a1ece  subss  xmm15, [rbx+0x50]        # CameraInplay+0x50 = ballZRangeMarginUnder = 5
0x1408a1ed4  subss  xmm14, [rbx+0x4c]        # CameraInplay+0x4c = ballZRangeMarginUpper = 10
0x1408a1ee8  xorps  xmm15, [rip+0x1cc37e0]   # .rdata 0x1425656d0 = −0.0
0x1408a1edf  movss  xmm1, [rdi+0x20]         # tz = the SMOOTHED target z
0x1408a1f15/1a/1f  subss/divss               # t = (tz − lo)/(hi − lo)

lo = −(halfWidth − marginUpper) = −(34.06 − 10) = −24.06
hi =  (halfWidth − marginUnder ) =  (34.06 −  5) =  29.06
t  = clamp( (tz − lo) / (hi − lo), 0, 1 )
```

**This corrects the earlier summary's `lo = +24.06`:** the low bound is *negated* (the xorps at
`0x1408a1ee8`), so the range is **−24.06 … +29.06**, not 24.06 … 29.06. With `tz = −8`:
`t = (−8 + 24.06)/53.12 = 0.302334` — the value the doc's §4.6 fit already used, so the fit
itself was right; only the written formula was wrong.

**Mode-5 field → source mapping (OBS, from `0x1408a1f51`–`0x1408a2094`; `rbx` = the
`constant::CameraInplay` object, sorted-key paramTable layout):**

| record field | far value (t = 0) | near value (t = 1) |
|---|---|---|
| Mani (`+0x00`) | `paramMax.Mani` (`CameraInplay+0x6C`) | `paramMin.Mani` (`+0xA0`) |
| Fov (`+0x04`) | `paramMax.Fov` (`+0x64`) via `18`/`u` | `28.698 − 3u/100` |
| PosY (`+0x08`) | `paramMax.PosY` (`+0x74`) via `0.52`/`h` | `0.3798 + 0.0702h/100` |
| LookY (`+0x0C`) | `paramMax.LookY` (`+0x68`) | `paramMin.LookY` (`+0x9C`) |
| PosZ (`+0x10`) | `paramMax.PosZ` (`+0x78`) | `35.44217300415039` |
| X_limit (`+0x1C`) | `paramMax.X_limit` (`+0x7C`) | `paramMin.X_limit` (`+0xB0`) |
| Z_min (`+0x14`) | `−8` (`.rdata 0x1425c7ba4`) always | |
| Z_max (`+0x18`) | `21.921` (`.rdata 0x14260b8f8`) always | |
| Pan (`+0x20`) | bilinear blend of `.rdata 0x14260b720` records #0…#3 field `+0x20` | |

`pan_eff = 1 + (Pan_bilinear − 1)·0.2` (OBS `0x1408a1fa4` −1, `0x1408a1fb9` × `.rdata
0x142598578` = 0.2, `0x1408a20ef` +1). The non-mode-5 branch uses `[this+0x14]` instead of 0.2
(`0x1408a20cf`).

**Slider stored range and units (OBS).** Both `[InplayCamera+0xC]` (zoom) and `[InplayCamera+0x10]`
(height) are `float` in `[0, 1]` — the clamps at `0x1408a1e63`–`0x1408a1e9d` compare against `0.0`
and `1.0` and there is no other scaling on the stored value. So the live fit
`zoom ≈ 0.79`, `height ≈ 0.20` is **already in the stored units**: `1 − zoom = 0.21`, and therefore
`u = 0.21 · 100 = 21.0` and `h = 0.20 · 100 = 20.0`.

**Live check of the corrected formula (OBS inputs from `docs/re/pes-re-stadium-camera.md` §4.6;
`Fov_max = 22`, `PosY_max = 0.42`, `PosZ_max = 49`, `t = 0.302334`):**

```
u = (1 − 0.79)·100 = 21.0
h = 0.20·100       = 20.0

Fov_far  = 22 + (18 − 22)·21/100        = 21.16
Fov_near = 28.698 − 3·21/100            = 28.068
Fov      = 21.16 + (28.068 − 21.16)·0.302334 = 23.248°      observed 23.26°   → −0.012°

PosY_far  = 0.42 + (0.52 − 0.42)·20/100 = 0.4400
PosY_near = 0.3798 + 0.0702·20/100      = 0.3938
PosY      = 0.4400 + (0.3938 − 0.4400)·0.302334 = 0.4260    observed 0.4262

PosZ      = 49 + (35.44217300415039 − 49)·0.302334 = 44.901   observed 44.90
```

**Fov reproduces the live 20:39 lens to 0.012°** (the earlier 24.02° / "0.76° high" line was the
symptom of the missing `0x1408a1ebf` `u`-scaling, not an unpinned `Fov_max`: `u = 0.21` without the
×100 gave `Fov_far = 21.9916` instead of `21.16`). `PosY` and `PosZ` reproduce to the sample's
own precision. The install-only test in `tests/pes_camera_test.cpp` that reconstructs the same pose
is therefore consistent with these numbers.

**A one-line note on which form to implement:** use the **uniform-scaled** form
`u = (1 − clamp(zoom,0,1)) * 100`, `h = clamp(height,0,1) * 100`, and divide both by 100 at the
point of use — that is literally what the exe does, and it is the form that keeps the far/near
`Fov` and `PosY` terms visually parallel. (Equivalently, `u = 1 − zoom` and `h = height` with no
÷100 anywhere; both give identical results.)

**The four slider presets (§4.3/§4.5) — OBS byte-verified from the exe file at
`.rdata 0x14260b720`, 9 f32, stride 0x24:**

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

(Verbatim PES numbers — evidence only, never to be transcribed into the repo.)

**Defaults for the sliders themselves (OBS).** `paramMax.isCustomize` at
`CameraInplay+0x60` selects the data-driven far side; when it is false the code uses the
bilinear blend of records #0…#3 at `0x14260b720` with `a = [this+0xC]` (zoom) and
`b = [this+0x10]` (height) — i.e. `R = (R0(1−a)+R1·a)(1−b) + (R2(1−a)+R3·a)·b` — exactly the
repo's provisional path. Records #6…#9 replace #0…#3 when
`FUN_14149f5e0(FUN_14149a470()) != 0` (condition still unidentified, OBS at `0x1408a1b38`).

**Live fit (OBS, from the §4.6 samples).** `zoom ≈ 0.79`, `height ≈ 0.20`, with the
`cameraInplay.o` `paramMax` branch active. That is INF-high consistent with the
`paramMax` header `0.9, 0.7` at `CameraInplay+0x58` and the fitted `PosZ` 44.90 vs 44.95
observed.

**Where the user's slider values live — OPEN.** The two floats are `[InplayCamera+0xC]` (zoom) and
`[InplayCamera+0x10]` (height), written by the camera-manager plugin, not read from a file by
`InplayCamera` itself. What was tried and found:

- the settings section registry exists: `.rdata 0x14259c938…` holds a serialised list of settings
  blocks, each `12 × {fn RVA, 1}` followed by the block name. **OBS** (names read from the file:
  `DEMO_INFO`, `GENERALSETTINGS`, `CAMERASETTINGS`, `PKMATCH`, `NO_OPERATION_LEAVE`,
  `GPACHIEVEMENT`), and the `CAMERASETTINGS` block's vtable is at `0x14259cba8` with the class-name
  thunk `0x14044e060` (`lea rax,[rip+0x214eb31]; ret` → the ASCII `CAMERASETTINGS` at
  `0x14259cb98`).
- the field-name thunks of that block are mostly plain accessors of the form
  `mov rax,[rcx+0x10]; add rax,IMM; ret` (e.g. `0x140454110` → `+0x44`, `0x140454120` → `+0x1b4`,
  `0x140454130` → `+0x34`, `0x1404540f0` → `+0x13`), but **the block's field-name strings are
  inside the mutation-obfuscated region** (`0x14044e0??` thunks jump into `0x15b0fb760`), so the
  mapping name → offset could not be read.
- a 48-bit CityHash (`strcode64 & 0xFFFFFFFFFFFF`) dictionary attack over 106 k identifier strings
  in the exe matched 9 of the 15 keys seen near the serializer, all belonging to a *model LOD*
  record (`castShadow`, `hasSpecular`, `shadowBias`, `dimmer`, `lodFarSize`, `lodNearSize`,
  `lodRadiusLevel`, `lodFadeType`, `enable`) — i.e. that reader is not the camera settings.
- the `[this+0xC]` / `[this+0x10]` writers inside the camera manager region are
  `0x14087bb8b`, `0x14087bc55`, `0x14087c5aa`, `0x14087ce09`, `0x14087523`, `0x14087f59a`
  (the last being the output struct, not the plugin), and none of them reads a constant.

**Blocker, exactly.** To finish item 5 the code needs the save-file/serialized-registry path:
`CAMERASETTINGS`' field-name table, which lives behind `0x14044e0?? → 0x15b0fb760` (protected).
Untried because it needs the running game or a de-obfuscator pass: reading the live
`Settings.exe`/game save on disk and diffing after a slider change (not allowed here), or
emulating the obfuscated loader.

---

## Item 6 — near-side Fov span and the §4.5 constants

**OBS, answered.** The `3` is `.rdata 0x14258c264` = `3.0f` (OBS byte read `0x00004040`). **The
correct expression is `Fov_near = 28.698 − 3·u/100` with `u = (1 − clamp(zoom,0,1))·100`**, i.e.
`28.698 − 3·(1 − zoom)` in the cancelled form — see item 5a for the exact operand chain
(`0x1408a1e9d`→`0x1408a1ea3`→`0x1408a1ea8` build `1 − zoom`, `0x1408a1ebf` ×100, `0x1408a1f9b` ×3,
`0x1408a1fc7` ÷100, `0x1408a1fd1` subtract). The first pass's `28.698 − 3u` form would have
given 28.068° at zoom 0.79 (right answer, wrong reason); the second pass's `3u/100` with
`u = 1 − zoom` would have given 28.692°.

The near-side constants and their file-readable addresses (all in the contiguous `.rdata` block,
delta `VA − 0x140000A00`):

| VA | value | used for |
|---|---|---|
| `0x14259be18` | 18.0 | `Fov_far` ceiling |
| `0x14259b8a0` | 0.52 | `PosY_far` blend target |
| `0x14260b8dc` | 0.0702 | `PosY_near` slope |
| `0x14260b8e0` | 0.3798 | `PosY_near` base |
| `0x14260b8fc` | 28.698 | `Fov_near` base |
| `0x14258c264` | 3.0 | `Fov_near` slope, the z-bound margin (`0x1408a3f21`), and `offset.y+3` |
| `0x14260b8f4` | 8.268 | the mode-5 state-3 target-x offset (item 2) |
| `0x14260b8f8` | 21.921 | Z_max |
| `0x1425c7ba4` | −8.0 | Z_min |
| `0x14260b900` | 35.44217300415039 | PosZ_near |
| `0x142598578` | 0.2 | `pan_eff = 1 + (Pan−1)·0.2` |
| `0x1425656c0` | 4.0 | lift `+4` |
| `0x14259222c` | 5.0 | lift clamp upper |
| `0x142588218` | −1.0 | `u = (zoom − 1)·(−1)` |
| `0x14253ee30` | 100.0 | the `u`/`h` scale and the ÷100 denominator |

All confirmed byte-identical in the on-disk exe at `VA − 0x140000A00` — OBS.

---

## Item 7 — mode int → menu name for modes 0, 1, 2, 4

**OBS (new this session, `0x14087ac70` = the camera plugin factory, `0x14087a47d` the demux):**

```
edx = the requested camera mode (the menu option index, passed down from the settings byte)
if (1 <= mode <= 6)  -> ctor 0x14087e480  (size 0x2e8) -> vtable 0x142609c28 = InplayCamera
else if (7 <= mode <= 9) -> ctor 0x14087f870 (size 0x598) -> vtable 0x142609f10 = VerticalCamera
```

Both ctors store their incoming mode into `[this+8]`:

- `0x14087e480` (InplayCamera): `0x14087e965` `mov [rdi+8], eax` with the mode, and the same
  value also drives `[rdi+0x2c]`, `[rdi+0xc]*/+0x10 = 1.0`, `k=1.0`, `[rdi+0x28]=0xff`,
  `[rdi+0xa4]=0xff`, `[rdi+0x100]=0`, `[rdi+0xb8]=1000`, `[rdi+0xec]=2` — the same field set the
  doc's §4.3 attributes to the plugin.
- `0x14087f870` (VerticalCamera): `0x14087f8a9` `mov [rbp-0x45], edx` → `0x14087fd84`
  `mov [rsi+8], eax`.

And the doc's `InplayCamera::update` `0x14087f280` dispatches on `[this+8]`:
`6 → FUN_1408a57f0`, `3 → FUN_1408a57f0`, `2 → FUN_1408a1a70` (fixed record #4),
`4 → FUN_1408a1a70` (fixed record #5), `else → FUN_1408a1a70`; mode 5 hits the `cameraInplay.o`
branch.

**INF (high) mode-name mapping, OBS range evidence + the doc's option order:**

| mode int | plugin | exe path | INF menu name |
|---|---|---|---|
| 0 | InplayCamera | bilinear #0..#3, `a=zoom, b=height` | **Normal** |
| 1 | InplayCamera | same bilinear | **Long** |
| 2 | InplayCamera | fixed record #4 (Fov 14.9, PosZ 113.3, X_limit 31.3) | **Wide** |
| 3 | InplayCamera → `FUN_1408a57f0` | `.data` table, 3 sliders | **Stadium** |
| 4 | InplayCamera | fixed record #5 (Fov 25.6, PosZ 70.5, X_limit 32.9) | **Mid Range** |
| 5 | InplayCamera | `cameraInplay.o` | **Dynamic Wide** |
| 6 | InplayCamera → `FUN_1408a57f0` | `.data` table, 3 sliders | **Stadium (Custom)** |

The 3/6 and 5 rows were already INF-high in the doc. **New:** the *range* is now OBS
(`1…6 = InplayCamera`, `7…9 = VerticalCamera`), and mode 2/4 are OBS-tied to their fixed records.
**Not resolved (open):** the exact name for 0/1 (the option order in `eng/string/all.str` gives
`Normal, Long, Vertical, Wide, Zoom, Tailing, Blimp, Pitch Side, Mid Range, Live Broadcast,
Fan View, Dynamic Wide, Dynamic Wide Custom, Stadium, Stadium (Custom)` — 15 options against only
10 mode ints, so the option list is not 1:1 with the mode int and includes non-InplayCamera
entries such as `Tailing Camera (Player/Ball)`, `Blimp`, `Pitch Side`, `Fan View`, which are
`VerticalCamera`/other plugins). The writer of `[InplayCamera+8]` from the menu is the
`CAMERASETTINGS` serializer (see item 5's blocker), so the menu-name → mode-int table is
**gated behind the same protected code**.

---

## Cross-check summary against the live 20:39 sample

Given: cam (−4.99, 19.16, 36.96), target (−5.31, 0.90, −8.00), vFOV 23.26°, near 0.9.

| quantity | predicted by the rules above | observed | note |
|---|---|---|---|
| target.z | clamp → Z_min = −8 (mode 5 always) | −8.00 | OBS match |
| target.y | LookY + clamp(lift,0,5) = 0.90 with lift 0 | 0.90 | the lift was zero; the `+4` branch could not be fitted (see item 1) |
| cam.x/target.x | 1/pan_eff, pan_eff = 1+(Pan−1)·0.2 | 0.938 (pan_eff 1.066) | Pan ≈ 1.33 |
| cam.y/(cam.z−target.z) | PosY (blended) | 0.4262 | PosY ≈ 0.426 — predicted **0.4260** with zoom 0.79 / height 0.20 |
| cam.z − target.z | PosZ·k | 44.96 | PosZ ≈ 44.90 — predicted **44.901** with the same inputs |
| vFOV | Fov·π/180 | 23.26° | predicted **23.248°** with u = 21, h = 20, t = 0.302334 (item 5a) |
| near | mode ≠ 2,4 → 0.9 | 0.9 | OBS |

The `t` factor for the predicted rows is `t = (−8 − (−24.06))/(29.06 − (−24.06)) = 0.302334`
(OBS bounds, item 5a). **`Fov`, `PosY` and `PosZ` all reproduce the live 20:39 sample** (Fov to
0.012°, PosY to 0.0002, PosZ to 0.001). The pose check holds for every item the live sample can
discriminate; the one item it cannot (the ball-height lift constant) is the open one, together
with the exact zoom/height save source.

---

## Open, with what was tried

1. **Ball-height lift constant** — the formula and every constant are OBS; only the *live*
   confirmation is missing (no in-air ball sample). Tried: the 20:39 sample (ball on ground, lift
   clamped to 0, so it constrains nothing) and the 20:27 sample (same).
2. **Where the Dynamic Wide zoom/height sliders are stored** — blocker: the `CAMERASETTINGS`
   serializer's field-name table is behind `0x14044e0?? → 0x15b0fb760` (mutation-obfuscated, same
   protection as the `cameraInplay.o` loader). Tried: the settings-block registry at
   `.rdata 0x14259c938`, the 12 `{rva,1}` vtable slots per block, the accessor thunk families
   (`mov rax,[rcx+0x10]; add rax,IMM; ret`) which give offsets but no names, a 48-bit
   `strcode64` dictionary attack over the exe's strings, and a scan for `[InplayCamera+0xC/+0x10]`
   writers (none read a constant).
3. **Menu name for mode 0 and 1** — the option list (15 entries) is not 1:1 with the mode ints
   (0–9, of which 7–9 are `VerticalCamera`). Needs the `CAMERASETTINGS` mapping (same blocker).
4. **`FUN_14149f5e0`** — the condition that switches records #0…#3 → #6…#9 (OBS call site
   `0x1408a1b4a`, body not traced).
5. **`FUN_1408a5b60`** — how modes 3/6 apply their result (OBS call site `0x14087f3a5`).
6. **Mode-switch blend N** — `FUN_141e532d0` returns 54, then
   `N = (f32)i * 48 / (54*211*0.5)` … more precisely `N_frames = ceil( (f32)([this+0x80]+1) /
   (211 * 54 / 48 * 0.5) )` (OBS at `0x14087f4bd`–`0x14087f4e8`), weight
   `w = (1 + cos(180°·(1 − i/N) + 90°))/2` (OBS `0x14087f531`–`0x14087f54c`). Exact N still INF.
