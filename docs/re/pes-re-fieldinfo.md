# PES 2021 — `registry::FieldInfo`: every field, live

Scope: the 18-float pitch-geometry record PES builds at match start. Closes the
"FieldInfo builder hunt" open item in `pes-re-stadium-camera.md` §2 and the
"remaining 7 fields" gap in `pes-re-setpiece-choreography.md` §11.10.

Method: **read-only** `/proc/<pid>/mem` `pread` of the running `PES2021.exe`
(pid <pid>); static byte search and PE section mapping of the user's on-disk
the install's `PES2021.exe`; Ghidra decompiles (`dec.sh`).
Nothing was written to the game, no input was sent.

Evidence tags: **OBS** = observed (method named). **INF(c)** = inferred,
confidence in brackets.

---

## 0. Headline results

1. **OBS.** All 18 floats are read live and are bit-exact below. The record is
   **0x4c bytes**: `{u32 refcount; f32 F[18]}`, floats at `+0x04 … +0x48`.
2. **OBS — correction to the docs.** `*(G+0x2aa8)` is **null** and is *not* the
   FieldInfo pointer. The live chain is `G + 0x2a90`. Full chain in §2.
3. **OBS — every field's value is VM-computed.** A byte-exhaustive search of the
   **on-disk exe file** finds **no** authoritative constant for F0, F1, F2, F3,
   F4, F6, F7, F8, F11 or F14. The isolated hits for F5/F10/F12/F13/F17 are in
   unrelated float pools (contexts dumped in §5). **A GF port cannot read any
   field from the exe at a fixed VA.** Every one must be a named constant with
   the provenance in §4/§5.
4. **OBS.** F17 = **9.15** is the centre-circle / penalty-arc radius, proven by a
   pitch-region classifier (`FUN_140a72e70`) that sets a "centre circle" bit on
   `|x| < F17 && |z| < F17` (§3.2). This upgrades the old INF(med).
5. **OBS.** 15 of 18 fields now have an **OBS** name from a named consumer.
   **F10, F11, F12 are still unnamed** (§6, exact next step given).

---

## 1. The live record (OBS, ground truth)

Read twice from pid <pid> with identical bits. Single instance (refcount 1),
shared by both goals and both teams — one global.

```
G        = 0x2C9FF910          ( *(u64*)0x1436F3DC8 )
record   = 0x2CA023A0          ( = G + 0x2a90, vtable 0x142608810 = FieldInfoRef )
ptrRec   = 0x0A3EFF80          ( vtable 0x142608770 = Pointer<FieldInfo> )
block    = 0x1A80E450          ( 0x4c bytes: { refcount = 1, f32 F[18] } )
```

| idx | off | bits | value | name | how named |
|---|---|---|---|---|---|
| F0 | +0x04 | `0x42523D70` | **52.5599976** | half-length, to the **outer** edge of the goal line | OBS `FUN_140a72e70` `F[0]`; camera-target §; ball-collision §3.2 |
| F1 | +0x08 | `0x42083D70` | **34.0599976** | half-width, to the outer edge of the touchline | OBS `FUN_140a72e70` `F[1]`; stadium-camera §2 |
| F2 | +0x0C | `0x406A3D71` | **3.66000009** | goal **inner** half-width | OBS `FUN_1404aa610` `param_2[2]`; ball-collision §3.2 |
| F3 | +0x10 | `0x401C28F6` | **2.44000006** | goal height (inner, crossbar underside) | OBS ball-collision §3.2 |
| F4 | +0x14 | `0x423C3D70` | **47.0599976** | goal-area line x (`F0 − 5.5`) | OBS `FUN_140a72e70` `F[4]` |
| F5 | +0x18 | `0x41128F5C` | **9.15999985** | goal-area half-width (`F2 + 5.5`) | OBS `FUN_140a72e70` `F[5]` |
| F6 | +0x1C | `0x42108F5C` | **36.1399994** | penalty-area line x | OBS `FUN_140a72e70` `F[6]` |
| F7 | +0x20 | `0x41A0F5C2` | **20.1199989** | penalty-area half-width | OBS `FUN_140a72e70` `F[7]` |
| F8 | +0x24 | `0x42263D70` | **41.5599976** | penalty spot x (`F0 − 11`) | OBS keeper-port §; defensive-shape § |
| F9 | +0x28 | `0x40200000` | **2.5** | goal-net height `H` | OBS `FUN_1404aa610` `param_2[9]` |
| F10 | +0x2C | `0x3F8E0000` | **1.109375** | **unnamed** | — |
| F11 | +0x30 | `0x4015C000` | **2.33984375** | **unnamed** | — |
| F12 | +0x34 | `0x424CB000` | **51.171875** | **unnamed** | — |
| F13 | +0x38 | `0x42594800` | **54.3203125** | goal-net **roof** back x (`XT`) | OBS `FUN_1404aa610` `param_2[0xd]` |
| F14 | +0x3C | `0x4259BC00` | **54.4335938** | goal-net **floor** back x (`XB`) | OBS `FUN_1404aa610` `param_2[0xe]` |
| F15 | +0x40 | `0x3DF5C28F` | **0.119999997** | crossbar thickness | OBS ball-collision §3.2 |
| F16 | +0x44 | `0x3DF5C28F` | **0.119999997** | post thickness | OBS ball-collision §3.2 |
| F17 | +0x48 | `0x41126666` | **9.14999962** | centre-circle and penalty-arc radius | OBS `FUN_140a72e70` `F[0x11]` (new, §3.2) |

Index convention used by PES code: **`F[k]` is at `block + 4 + 4k`**. A pointer
to the first float (`block + 4`) is what consumers hold, so Ghidra shows
`param_2[0]` = 52.56 and `param_2[9]` = 2.5 (not `+0x24`).

Relations that hold bit-exactly (OBS by arithmetic on the read values):

- `F0 = 52.5 + 0.12/2`, `F1 = 34 + 0.12/2` — line **outer** edges, 0.12 m lines.
- `F4 = F0 − 5.5` exactly (47.06), `F5 = F2 + 5.5` exactly (9.16).
- `F6 = F0 − 16.42`, `F7 = 3.5·(F2 + 5.5) − 0.02`… **no**: `F7` is exactly
  `2 · 10.06`; the clean statement is `F7/F5 = 2.1962…`, i.e. PES's box is
  **40.24 m wide / 16.42 m deep**, 8 cm inside Law 1 on both axes (already noted
  in stadium-camera §2). INF(med): a line-edge convention, not a data error.
- `F8 = F0 − 11` exactly (41.56).
- `F9 = F3 + F15/2 = 2.44 + 0.06 = 2.5` exactly — the same number as the
  crossbar-top / post-centre y used by the frame collision.
- Post-centre axis used by collision = `F0 − F16/2 = 52.5`, `F2 + F16/2 = 3.72`.

---

## 2. How to read it live (OBS) — and the doc correction

`pes-re-stadium-camera.md` §2 and `pes-re-setpiece-choreography.md` §11.10 both
say *"`FieldInfo` (`G+0x2aa8`)"*. **That is wrong**: `*(G+0x2aa8)` is 0
(checked live, twice). `G+0x2a90` is the start of the `FieldInfoRef` record:

```
G + 0x2a90  +0x00  0x142608810   FieldInfoRef vtable
            +0x08  refcount/id
            +0x10  0x0A3EFF80     -> Pointer<FieldInfo> record
0x0A3EFF80  +0x00  0x142608770   Pointer<FieldInfo> vtable
            +0x10  0x1A80E450     -> the 0x4c block
0x1A80E450  +0x00  0x00000001   refcount
            +0x04  F0 … +0x48 F17
```

Script (read-only, `sudo`, takes the pid):

```python
# the FieldInfoRE scratch dirfi_chain.py
G    = u64(0x1436F3DC8)
ptr  = u64(G + 0x2a90 + 0x10)
blk  = u64(ptr + 0x10)
floats = blk + 0x04              # 18 f32
```

Self-check built into the script: `record+0 == 0x142608810`, `ptr+0 == 0x142608770`,
`blk+0 == 1`. If those fail the chain is stale (match torn down).

The alternative documented path — `simBall+0x540 → +0x10 → +4` — is the *same*
object reached through the ball (`fox-registry.md` §: `FieldInfoRef` at
`simBall+0x530`, data `+0x540`). The camera reaches it as `[obj+0x120]`
(`pes-re-camera-target.md` §, `0x1408a1eb1`). All four agree on the same 0x4c
block.

---

## 3. Consumers that pin the names (OBS)

### 3.1 Goal net — `FUN_1404aa610` (decompile `out/fi_net.c`)

```c
_DAT_14344644c = param_2[2] + 0.046875;   // W  = F2 + 0.046875 = 3.706875
_DAT_143446450 = param_2[9];              // H  = F9 = 2.5
_DAT_143446454 = *param_2 - 0.1;          // X0 = F0 - 0.1    = 52.46
_DAT_143446458 = param_2[0xd];            // XT = F13          = 54.32031
_DAT_14344645c = param_2[0xe];            // XB = F14          = 54.43359
```
`FUN_1404bece0` maps the net tables' percent coordinates onto
`y = fy·H`, `z = Z/100·W`, `x = ((XB − X0)(1 − fy) + (XT − X0)·fy)·fx + X0`.
So **F13 is the roof-back and F14 the floor-back**, not two "net depth" numbers
as the older docs hedged. `.data 0x14344644c…45c` holds the cached results
(byte-identical on disk and live — OBS).

### 3.2 Pitch region classifier — `FUN_140a72e70` (**new**, OBS)

Decompile `out/fi_reg.c`. Takes `float* FieldInfo, bx, bz, side*` and returns a
bitmask. Field accesses:

| test | field | meaning |
|---|---|---|
| `fVar1 = F[0]; if (F[0] < |bx|) → bit 0x0a` | F0 | behind the goal line |
| `if (F[1] < |bz|) → bit 0x06` | F1 | outside the touchline |
| `F[7] <= |bz|` / `\|bz\| > F[7]` | F7 | outside the penalty area |
| `_DAT_14259be04 < bx < F[6] - _DAT_14259bdf4` | F6 | in the box, offset band |
| `F[6] - e < bx < F[0]` | F6 | penalty-area / goal-area band |
| `F[4] <= bx && |bz| < F[5]` | F4, F5 | **goal area** (bit 0x200) |
| **`if (bx < F[0x11] && bz < F[0x11]) → bit 0x4000`** | **F17** | **centre circle** |

That last line is the confirmation for F17 = 9.15: the *only* place a single
field gates "both coordinates small" with radius 9.15 is the centre circle, and
the same 9.15 radius defines the penalty-arc (Law 1). **OBS, not INF.**

### 3.3 Goal frame — `pes-re-ball-collision.md` §3.2

`post axis = ((F0 − F16/2)·sx, (F2 + F16/2)·sz) = (±52.50, ±3.72)`;
`bar axis = ((F0 − F16/2)·sx, F3 + F15/2) = (±52.50, 2.50)`. Confirms F15 =
crossbar, F16 = post, and that they are the same 0.12 as the pitch line width
(the two `+0x40/+0x44` slots hold the identical float bit pattern `0x3DF5C28F`).

---

## 4. Provenance: is there a VA in the user's exe? (OBS — answer: no)

Byte-exhaustive search of the **on-disk** `PES2021.exe` (0x1B57D600 bytes) for
each field's exact f32 bit pattern. `.rdata` VA 0x14252F000 @ raw 0x252E600,
`.data` VA 0x14339B000 @ raw 0x339A200, `.impdata` VA 0x143D14000 @ raw 0x391B400.

| field | bits | occurrences in the exe file |
|---|---|---|
| F0 52.56 | `42523D70` | **0** |
| F1 34.06 | `42083D70` | **0** |
| F2 3.66 | `406A3D71` | **0** |
| F3 2.44 | `401C28F6` | **0** |
| F4 47.06 | `423C3D70` | **0** |
| F5 9.16 | `41128F5C` | 1 — @raw 0x25dd6a0 = VA 0x1425DE0A0, **interpolation LUT**, see §5 |
| F6 36.14 | `42108F5C` | **0** |
| F7 20.12 | `41A0F5C2` | **0** |
| F8 41.56 | `42263D70` | **0** |
| F9 2.5 | `40200000` | 136 — 2.5 is a common tuning value, none is authoritative |
| F10 1.109375 | `3F8E0000` | 9 — generic float pools |
| F11 2.33984375 | `4015C000` | **0** |
| F12 51.171875 | `424CB000` | 1 — @raw 0x2f3be2f = VA 0x142F3C82F, **misaligned read inside a `{ptr,ptr,ptr,u32}` array**, see §5 |
| F13 54.3203125 | `42594800` | 1 — @raw 0x259aeb8 = VA 0x14259B8B8, **generic pool**, see §5 |
| F14 54.4335938 | `4259BC00` | **0** |
| F15/F16 0.12 | `3DF5C28F` | 273 — 0.12 is a common value, none is authoritative |
| F17 9.15 | `41126666` | 1 — @raw 0x25a8a5c = VA 0x1425A945C, see §5 |

**Conclusion (OBS).** None of the 18 is readable from the exe at a stable VA.
They are all materialised at runtime by the Denuvo-VM constructor
`0x140a85f40 → 0x144295920 (.impdata)`. The VM entry itself is a dispatcher
(`2e 36 9c ff` / `34 24` / `48 83 c4 f8` …) with **no embedded float pool** —
dumped `0x144295920…0x144295A20` as both bytes and f32: nothing resembling pitch
geometry. GF therefore has **no runtime hook** available: headless GF cannot read
another process's heap, and the exe offers no constant.

**Every field must be a named constant in GF, with this provenance.**

### The three false positives, dumped

- `0x1425DE0A0` (9.16) sits in a monotonic interpolation LUT:
  `… 5.8, 5, 0.75, 0.66, 6.5, 5.8, 0.82, 0.75, 7.1, 6.5, 0.871, 0.82, 7.3, 7.1,
  0.887, 0.871, 8.3, 7.3, 0.96, 0.887, 9.16, 8.3, 1, 0.96` — a `[x, y]`-style
  ramp, not a pitch record.
- `0x142F3C82F` (51.171875) is inside `… 0x1424ca4, 5, 0x1424cb0, 3,
  0x1424cbc, 3 …`, i.e. a `{ptr, int}` table read at the wrong alignment.
- `0x14259B8B8` (54.3203125) is in a pool with `0.8166, 0.7062, 0.2286, 0.5250,
  0.5503, 0.0284, 2.4751, 7.1837, 54.3203, 7.8023` — no structure.

### In-memory hits that ARE caches, not sources (OBS)

| VA | content | live status |
|---|---|---|
| `0x143704DB8` | `52.56, 34.06` | written by code at `0x14149356c` (a float-pair copy) |
| `0x1436F2068` | goal-mouth vertex ring `52.56 / ±3.66` | static |
| `0x1436F39D0` | mirror ring | **zeroed at runtime** |
| `0x14344644c` | `3.706875, 2.5, 52.46, 54.32031, 54.43359` | written by `FUN_1404aa610` |

All four are *consumers* of FieldInfo, i.e. downstream copies. A GF port must not
read them.

---

## 5. What GF replaces (OBS comparison, GF constants from `src/sim/gametypes.hpp`)

GF already changed its pitch to PES's 105 × 68 (`pitchHalfW = 52.5`,
`pitchHalfH = 34`), which are the **line centres**. PES's record is the
**outer edges**. Every mark therefore differs by 0.06 m on the axes and by 0.08 m
on the box.

| GF constant (current) | value | PES field | PES value | Δ |
|---|---|---|---|---|
| `pitchHalfW` | 52.5 | **F0** | 52.56 | +0.06 (edge convention) |
| `pitchHalfH` | 34 | **F1** | 34.06 | +0.06 |
| `lineHalfW` | 0.06 | **F15/2 = F16/2** | 0.06 | none — but F15/F16 are the *frame* thickness; the line width is implied, not stored |
| `goalHeight` | 2.5 | **F9** (net H) / `F3 + F15/2` | 2.5 / 2.5 | none; GF already uses the crossbar-top value. Add `F3 = 2.44` as the inner height |
| `goalHalfWidth` | 3.7 | **`F2 + F16/2`** | 3.72 | +0.02 (post centre) |
| `goalInnerHalfWidth` | 3.66 | **F2** | 3.66 | none ✓ |
| `goalInnerHeight` | 2.44 | **F3** | 2.44 | none ✓ |
| `goalFrameThickness` | 0.12 | **F15 / F16** | 0.12 / 0.12 | none ✓ |
| `goalAreaHalfWidth` | 9.16 | **F5** | 9.16 | none ✓ |
| `goalDepth` | **2.55** | **F13/F14 vs F0** | roof 54.32031 − 52.56 = **1.76031**; floor 54.43359 − 52.56 = **1.87359** | **GF is 0.68–0.79 m too deep.** PES's net slab runs `X0 = F0 − 0.1 = 52.46` to `XT`/`XB` (§3.1) |
| *(missing)* | — | **F4** goal-area line x | **47.06** | GF has no such constant |
| *(missing)* | — | **F6** penalty-area line x | **36.14** | GF uses its own (see below) |
| *(missing)* | — | **F7** penalty-area half-width | **20.12** | GF: none found |
| *(missing)* | — | **F8** penalty spot x | **41.56** | GF hard-codes `11.0f` in `src/sim/data/matchdata.hpp:85` |
| *(missing)* | — | **F17** circle / arc radius | **9.15** | GF: only a comment in `pessetpieceteam.hpp:56` |

Additional notes for GF:

- **Penalty area.** `F6 = 36.14` and `F7 = 20.12` are **8 cm inside** Law 1
  (16.5 / 20.16) on both axes. `pes-re-protection.md` §notes GF currently feeds
  "box line 36.0 / 20.15". `36.0 → 36.14` and `20.15 → 20.12` are the only two
  edits needed; every zone, wall distance and GK clamp inherits them.
- **Distance convention.** F0/F1 are outer edges, so anything PES compares
  against `|x| ≤ F0` (in-goal, corner flags at ±52.47…52.49) is on the outer
  edge. Marks that are *drawn* (corner flag positions from `fmdl`, ±52.47) sit
  just inside F0, which is consistent with F0 = 52.5 + 0.06.
- **`F10/F11/F12` are not needed by anything GF ports today.** They are not read
  by any consumer found so far. If a future port needs them, they must be named
  constants; there is no VA to read.

---

## 6. Open

| id | item | status | exact next step |
|---|---|---|---|
| FI-01 | **F10 = 1.109375, F11 = 2.33984375, F12 = 51.171875 unnamed** | **open** | The two proven consumers (`FUN_140a72e70`, `FUN_1404aa610`) skip exactly F10–F12. They sit between the net block (F9, F13, F14) and the thickness pair (F15, F16), so the hypothesis is a second geometry group (net hang-pole / net front face — INF low, **no supporting code read**). Settle it by decompiling every `FUN_1404aa610`-style consumer of `float* FieldInfo`: find the call sites of `FUN_1404aa610` and `FUN_140a72e70` in Ghidra xrefs, then decompile their siblings in the same translation unit. A cheap filter: the consumer must read `[+0x28] [0x2c] [0x30] [0x34]` consecutively — the byte scan `firescan3.py` in the FieldInfoRE scratch dir already lists every such site (58 candidate windows; the ones decompiled so far, e.g. `0x141ADE35C`, turned out to be a 0xB0-stride struct copy, not FieldInfo). |
| FI-02 | Emulation past the derail point | **open** | `emu.py 0x140a85f40 --rcx 0x10000000 --live` runs 910 s+ and never reaches a clean stop; the Denuvo VM reads its own image through VM-indirect addresses that unicorn resolves to garbage once a chunk is missing. Since FI-01 can be closed purely statically and every value is already OBS live, the emulator is **not needed** — do not spend more time on it. |
| FI-03 | Docs to fix | **not my file** | `pes-re-stadium-camera.md` §2 and `pes-re-setpiece-choreography.md` §11.10 both state "`FieldInfo` (`G+0x2aa8`)". Replace with `G + 0x2a90 → +0x10 → +0x10 → +0x04`. `pes-re-stadium-camera.md` §2's `+0x28…+0x44` "?" rows are superseded by §1 above. RE never edits `docs/re`; hand these to the orchestrator. |

---

## 7. Ledger

| id | claim | conf. | evidence |
|---|---|---|---|
| EV-FI-0001 | FieldInfo is a 0x4c block `{refcount, f32 F[18]}`; floats at +0x04…+0x48 | **Confirmed (live)** | `fi_chain.py` output, pid <pid>, block `0x1A80E450` |
| EV-FI-0002 | All 18 values, bit-exact | **Confirmed (live)** | §1 table, two independent reads |
| EV-FI-0003 | The live chain is `G+0x2a90 → +0x10 → +0x10 → +0x04`; `*(G+0x2aa8)` is null | **Confirmed (live)** | §2; vtables `0x142608810` / `0x142608770` checked at each hop |
| EV-FI-0004 | No field has an authoritative on-disk constant in the exe | **Confirmed (exhaustive byte search)** | §4 table over 0x1B57D600 bytes |
| EV-FI-0005 | F0/F1 are line **outer** edges; F4/F5/F6/F7/F8/F9/F13..F17 semantics | **Confirmed (code)** | `FUN_140a72e70`, `FUN_1404aa610`, ball-collision §3.2 |
| EV-FI-0006 | F17 = 9.15 is the centre-circle / penalty-arc radius | **Confirmed (code)** | `FUN_140a72e70` `if (bx < F[0x11] && bz < F[0x11]) → bit 0x4000` |
| EV-FI-0007 | F13 is the net roof-back x, F14 the floor-back x (not two "net depths") | **Confirmed (code)** | `FUN_1404aa610` + `FUN_1404bece0` lerp |
| EV-FI-0008 | F10/F11/F12 are unnamed | **open** | FI-01 |
| EV-FI-0009 | GF `goalDepth = 2.55` is 0.68–0.79 m deeper than PES's net slab | **Confirmed (live + code)** | `54.32031/54.43359 − 52.56` vs `goal.fmdl` AABBs |

## 8. Scratch

the FieldInfoRE scratch dir (this session):

| file | what |
|---|---|
| `fi_chain.py` | **the** live reader, with the two vtable assertions as a staleness self-check. `sudo python3 fi_chain.py <pid>` |
| `scan.py` | heapscan for the 18-float pattern (used to find the block when the registry chain was unknown) |
| `firescan3.py` | `movss` load scanner with base-register tracking; lists every site that loads ≥3 FieldInfo-shaped offsets through one register (58 windows). Tool for FI-01 |
| `fiwin.py` | `FieldInfoRef`-accessor window scan (`mov r64,[r+0x120]`); found only the camera, i.e. most consumers get FieldInfo as a parameter — kept as a dead end record |

> **Orchestrator note on `*(G+0x2aa8)` (2026-10-14):** many decompiles read the pitch table as `*(G+0x2aa8)` inside the
> tick (e.g. the referee handlers of `pes-re-protection.md` §3.3, whose outputs matched live restarts), so the code path
> is real. The null read here is most likely the same timing effect as `MI = *(G+0x270)`, which is null for part of
> every tick (INF high). Use the `G+0x2a90 → +0x10 → +0x10` chain for out-of-tick reads; do not rewrite the other
> reports' `G+0x2aa8` references.
