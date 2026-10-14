# PES 2021 set-piece zones: template, marking, corner variant, kick-request writer (SetpieceZonesRE)

Read-only RE. Goal: close the set-piece open items (choreography report §11.11 items 1–2;
§9 items 2–4; §12's 0x31-writer question).
Live: read-only `pread` of `/proc/<pid>/mem` of the running AI-vs-AI match.
Conventions as in `pes-re-setpiece-choreography.md` (G, MI, F[]; `dir` = attack sign
of the restarting team; `T` = team record; slot 0..10 per side, idx 0..21).
State 54 = 0x36 MARK (choreography §11.1). Field: F[0] 52.56 half-length,
F[4] 47.06 goal-area line, F[6] 36.14 box line, F[8] 41.56 spot line,
F[2] 3.66 goal half-width, F[17] 9.15.

Tags: **OBS** = read in raw bytes/disassembly/decompile at the named address, or observed
live. **INF(conf)** = inference.

Scratch: the SetpieceZonesRE scratch dir (`spcap.py`, `live.jsonl`, `ck.bin`, raw dumps).

## 0. Live captures

Two MODE-5 free kicks captured with per-frame positions, `T+0xa2a0` states, full
`T+0x26c` (11×0x40) and `T+0x4b4c` (11×12B) tables (OBS `live.jsonl`):

- **FK1**: R = (−23.58, −13.49), kick = idx 12 (side 1 restarts, defensive FK —
  77 m from side 1's attacked (+x) goal). Teleport window fr 34421–34439
  (stages 2→20), ready (play 3) at fr 34442.
- **FK2**: R = (21.93, 0.31), kick = idx 16 (side 1 restarts, attacking FK —
  d = 30.6 m from the +x goal). Teleport window fr 36740–36771 (stages 1→21),
  ready at fr 36774.

One MODE-4 corner captured with two spot reads (no per-frame recorder running at the
time, so no stage-by-stage track — OBS single reads):

- **CK1**: R = (−51.78, +33.61), kick = idx 4 (side 0 attacks −x). Early read
  (play 2): defending side 1's `T+0x4b4c` = zone template (§1). Ready read
  (play 3, stg 21): markers + attacker spots (§2).

## 1. The zone template (CLOSED live)

### 1.1 Corner defence (OBS, CK1 early read, side 1 defending the −x goal)

`T+0x4b4c` per slot (world; side 1's own frame = world mirrored by its dir):

| slot | x | z | identity |
|---|---|---|---|
| 0 | −50.73 | 0 | keeper: −(F[0]−1.83) (choreography §4.4 ✓) |
| 1 | −36.14 | 0 | box line: −F[6] (choreography §4.4 ✓) |
| 2 | −41.56 | +1.50 | spot line: −F[8] |
| 3 | −49.81 | +7.16 | goal-area corner (−(F[0]−2.75), +(F[5]−2.0)) |
| 4 | −41.56 | +2.66 | spot line |
| 5 | −41.56 | +6.00 | spot line, +6.0 |
| 6 | −41.56 | −6.00 | spot line, −6.0 |
| 7 | −41.56 | −2.02 | spot line |
| 8 | −41.56 | −3.60 | spot line |
| 9 | −41.56 | +1.16 | spot line |
| 10 | −9.15 | −1.40 | outlet: F[17] from halfway, own half |

So the corner zone template = **own-frame x = −F[8] (penalty-spot line, 11 m in
front of the own goal), z spread over [−6.0, +6.0]** (OBS: max |z| = 6.00 exactly).
The ±6.0 edge = **`dfLineWidth_Corner` = 6.0** (basePosition.o, choreography §11.7:
live value confirmed) — the prior INF(med) is now OBS live. The §1.2 provisional
spots (z = 2.0/0.5/5.99/−6.0) are four samples of this same [−6,+6] spread.
The writer is still not located (no `mov` store to +0x4b4c/50/54 anywhere in the
image — all 60 displacement hits are reads/`lea`; `scan.py`): the template is
written through a pointer/base+register path, not a displacement store. But its
**values are closed**: (−F[8], z ∈ [−6, +6]) + the fixed posts above.

At the ready read the same side's `T+0x4b4c` had evolved (slots 5–9 z: 5.10,
−4.34, −1.20, −2.70, +2.10 — still |z| ≤ 6) while slot 2 moved to (−47.06, +1.50)
and slot 3 to (−49.81, +7.16): the line holds through the window, two men drop to
the goal-area corners. OBS.

### 1.2 Free-kick defence (OBS, FK1 + FK2 ready reads, side 0 defending +x)

- FK2 (attacking FK, d = 30.6): keeper (49.94, 0.03); **line of four at x = 33.68**
  (slots 1–4, z −3.3/+1.11/−9.91/+7.98); then (30.12, −3.76), (29.2, ±8.44/−9.91),
  (30.39, 3.79); outlets (8.5, +7.0/+2.66). Ball R = (21.93, 0.31), goal at +52.56:
  **the line sits 11.75 m goal-side of the ball** (33.68 − 21.93).
- FK1 (defensive FK, R = (−23.58, −13.49)): keeper (52.09, 0); **line of four at
  x = 19.67** (slots 1/2/4/10, z −5.44/−0.77/+10.2/−13.76); midfield pairs at
  x = 10.4–11.89; deep men (−6.21, −1.01), (7.66, 6.94).

INF(high): the FK line ≈ 12 m goal-side of the ball when the box-zone gate holds
(FK2: 11.75 m; gate true). FK1's gate is false (defensive FK) and its line at
x = 19.67 is the ordinary shape line, not an FK guard — consistent with pass 5
being skipped there. Exact 12 m formula (which F[]/rounding) still open: needs an
FK at d ≈ 20 and d ≈ 40 with the gate true to fit.

### 1.3 GF code each finding replaces

- Corner zone (−F[8], ±6.0) + keeper −(F[0]−1.83) + box-line −F[6] + outlet F[17]:
  `pessetpieceteam.cpp` zone placements / `PrepareSetPiece` defender spots.
- FK line ~12 m goal-side: same files' FK line placement.

## 2. Who marks whom at corners/FKs (CLOSED live + code)

### 2.1 The gate both restarts pass (OBS raw 0x140a532f0–0x140a53399)

`FUN_140a533a0(ctx, 0)` = 1 (run mark pass 5, the set-piece pairing) iff stopped
(`MI+0x116c != 1`) AND:
- MODE 4 (corner): always (`cmp r9d,4` → `mov al,1`, 0x140a532f4–0x140a53300);
- MODE 5 (FK): only the **box zone**: `MI+0x11d4 == 0`, `dir·R.x ≥ 0` (ball in the
  attacking half), `|R.z| ≤ F[7]` (20.12, `[rcx+0x1c]`), `dir·R.x ≤ F[6]` (36.14,
  `[rcx+0x18]`) — raw 0x140a53301–0x140a53393. I.e. FKs in the attacking third's
  box width; else pass 5 is skipped and marking falls through to passes 6–7.
Gate vs live (OBS): the FK branch tests the **restarting** side's dir
(`T(MI+0x1558)+0x254`). FK2 (side 1 restarts, dir +1): dir·R.x = +21.93 ≥ 0 ✓,
|z| = 0.31 ≤ 20.12 ✓, ≤ 36.14 ✓ → pass 5 ran. CK1 (side 0 restarts) → pass 5 ran.
FK1 (side 1 restarts, dir +1): dir·R.x = **−23.58 < 0 → gate FALSE** (defensive FK
in the restarters' own half): pass 5 skipped, pairing fell through to passes 6–7.
The two FKs therefore exhibit **both** pairing paths (§2.3–2.4).

`FUN_140534a20` itself runs in play, at ready (play 3), and while stopped (play 2)
only at stages 1–21 (raw 0x140534b83–0x140534baa: play ≠ 2 skips the stage test;
play 2 needs `MI+0x11b0 − 1 ≤ 0x14`, i.e. stages 1–21; MODE 1 stopped skips).
At the stopped branch it calls the `FUN_140a533a0` gate to choose pass 5 vs the
open-play danger list (raw 0x140534bbd–0x140534bc7). OBS.

### 2.2 Pass 5 `FUN_140530320` — what it does (OBS decompile `ds3.c`)

Gated on the §2.1 test (raw 0x14053037b–0x140530382, first call of the function).
Reduced from the decompile (structure OBS, per-man weights INF(med)):

1. **Rank defenders** by stat bytes `b[0x31] + b[0x2c] + b[0x1d]`
   (`FUN_14124dad0`; −100 for slot `T+0x3ed0`), skipping outlets
   (`FUN_140a41cc0`), wall-zone men and plan-bit men; sort with `FUN_140a79640`.
2. **Set-piece target opponents** (`M+0x1ea4` bits): each takes the free defender
   with the smallest MA distance (+10000 when `T+0x3ecc` and position < 4, +2500
   for `T+0x3ed0`/plan bits).
3. **The rest of the danger list** (same stat sum, +100 with plan 0xe; or by MA
   distance) paired best-to-best.
4. **The kicker/runner (`MI+0x173c`) is moved into the top 3** of the opponent
   order (raw tail loop) — the most dangerous opponent keeps an elite marker.

### 2.3 Live check — CK1 ready (OBS)

Defending side 1 states: `[0,0,0,0,109,54,54,54,54,54,0]` — slot 4 POST_MAN (109 =
0x6d), slots 5–9 MARK, rest clear. `T+0x558` (mark targets): slots 5–9 →
`[10,9,8,5,7]` (opponent slots of side 0). Bodies vs mark spots (`T+0x26c`):

| def slot | body | mark spot | nearest attacker |
|---|---|---|---|
| 5 | (−46.50, −8.56) | (−46.48, −8.56) | idx 16 (−46.50,−8.56) d = 0.02 |
| 6 | (−46.75, +2.80) | (−46.79, +3.10) | idx 17 (−46.75,+2.80) d = 0.30 |
| 7 | (−46.44, +8.60) | (−46.48, +8.56) | idx 18 (−46.44,+8.60) d = 0.06 |
| 8 | (−40.56, +0.32) | (−40.53, +0.28) | idx 19 (−40.56,+0.32) d = 0.05 |
| 9 | (−46.74, −1.80) | (−46.76, −1.78) | idx 20 (−46.74,−1.80) d = 0.03 |

**Every marker stands within 0.3 m of his mark spot, and each mark spot is within
0.3 m of exactly one attacker** — the pairing is exact man-marking at the ready
frame (budget moves complete). The five marked attackers (idx 16–20) are the five
nearest the goal; idx 21 (outlet, (−9.15,−1.40)) and the deep men are unmarked.
The kicker (idx 4) is not marked (he is at the corner flag). Consistent with pass 5
+ the `MI+0x173c`-in-top-3 rule applied to the box attackers.

Attacking side 0 at CK1 ready: idx 5 (−38.56, 0), idx 6 (−27.77, −1.99),
idx 7–10 all x = −46.06 (z −2.00/+9.16/+2.64/−9.22). idx 6 = `positionCK_6_1`
ordinal 5 (27.77, −1.99) mirrored in x exactly (OBS). The x = −46.06 collapse of
idx 7–10 (= 6.5 m in front of the −52.56 goal line) does NOT match any table rows
(table x = 36–43): INF(high) these four reached the budget-move clamp
(`FUN_140a544d0`/overlap clamp after the 16 km/h budget, choreography §11.3) —
their table spots are deeper than the budget allows from the stage-1 positions,
OR the stage-10 `MARK`-state branch moved them onto outlet marks. Exact cause open
(needs the stage-by-stage track this capture lacked).

### 2.4 Live check — FK1/FK2 ready (OBS)

- FK2 defence (side 0): states slots 1–5 = MARK, `T+0x26c` == bodies within
  0.15 m (e.g. slot 5 body (30.27,−4.22) = mark (30.27,−4.22)). Five markers cover
  the three box attackers (idx 17/20/21) plus the short option (idx 18) and the
  box-edge man; each mark spot within 3.6 m of its nearest attacker (geometry
  looser than the corner because the line, not the man, is the primary shape).
- FK1 defence (side 0, gate false → passes 6–7 only): 7 MARK states (slots
  1,2,4,5,8,9,10); marks == bodies within 0.5 m. Slot 2's mark (19.67,−1.16) sits
  0.49 m off attacker idx 21 (19.67,−1.26); slot 10's mark (19.67,−13.80) 1.13 m
  off idx 17 (19.23,−14.6); slot 4's mark (19.67,+10.34) 1.29 m off idx 16
  (19.23,+11.41). The far-side men (slots 5/8/9) hold zonal spots 4–9 m off the
  nearest attacker — continuity + nearest-free fill, not the aerial ranking.

### 2.5 GF code each finding replaces

- `CalculateManMarking` (pass 5 at stoppage: gate + stat-ranked pairing +
  kicker-in-top-3) and `AddDefensiveComponent`/`GetMarkSpot` (mark spot =
  goal-side point, bodies reach it exactly by the ready frame).
- `T+0x558` semantics at restarts (global idx targets, 0xff = none).

## 3. The corner variant (CLOSED static + live)

### 3.1 The tables (OBS VFS decode, scratch `ck.bin`)

`common/match/constant/constant_positionCK.bin` (775 B): WESYS container, zlib
payload at +16 → 3984 B. Entry table: 10 × `{id, ?, offset}` (12 B each, first
`{0xa, 8, 304}`), bodies at 304/672/…/3616, each `{16,0,0,0}` + 11 ×
`{u32 enable, u32 ordinal, f32 x, f32 z}` (the 368-B layout of choreography §4.3).
Decoded ordinals present per body:

| file | N (men in box) | ordinals with enable = 1 |
|---|---|---|
| `positionCK` | base (all 11) | 0–10 |
| `positionCK_4_1/2/3` | 4 | 7,8,9,10 |
| `positionCK_5_1/2/3` | 5 | 6,7,8,9,10 |
| `positionCK_6_1` | 6 | 5:(27.77,−1.99), 6:(36.33,−10.74), 7:(36.23,−4.84), 8:(40.83,−10.23), 9:(43.35,−9.79), 10:(38.66,−7.76) |
| `positionCK_6_2` | 6 | 5:(27.82,−1.49), 6:(38.68,−8.22), 7:(38.77,−0.49), 8:(42.12,−7.90), 9:(41.84,−2.69), 10:(41.41,+3.80) |
| `positionCK_6_3` | 6 | 5:(28.45,−1.27), 6:(40.12,+0.80), 7:(44.08,+5.05), 8:(39.75,−8.03), 9:(42.45,−10.20), 10:(45.20,−10.70) |

Frame: attack toward +x, corner on the +z side (choreography §4.3 ✓ — the two
prior live corners matched _6_1/_6_2 to 0.02 m).

### 3.2 Variant selection (OBS code + live)

- **N = `T+0x6d0` = 4 + min(attack level, 2)** (`FUN_1404f6680`, raw
  0x1404f6680–0x1404f66f2: level 0 → 4, 1 → 5, else `min(level,4)`→6 via
  `cmovae eax,edx` with edx = 6). So N ∈ {4, 5, 6}; never other values. OBS.
- **V ∈ {1,2,3}**: the registry ids are 0x3f (base) + 0x40–0x48
  (`positionCK[_N_V]`, refl registry OBS). Reader: every `mov edx,0x4?` in the
  image that decodes as a true `mov edx,imm32` was checked — the only readers in
  set-piece code paths take the id in a register (computed `base + N·3 + V`),
  which is why the immediate scan of choreography §11.6 found nothing:
  INF(high) the reader computes `0x40 + (N−4)·3 + (V−1)`. The exact reader
  function is still open (next step: breakpoint/log `FUN_141e5ca20`'s second arg
  during a corner, or find `lea r?`, `[base + N·3]` near the ordinal fill
  `FUN_14051b8b0`).
- **Live**: both teams at attack level 2 this session → N = 6 throughout
  (OBS: `T+0xb8a4` = 2 both teams). CK1's idx 6 sits exactly on `_6_1` ordinal 5;
  the prior session showed V = 1 then V = 2 for one team (choreography §4.3) —
  so **V varies corner to corner while N is fixed by attack level**.
  INF(med): V is a per-corner roll or short/long choice (the `CORNERKICK_SHORT`
  path of choreography §12 fires inside 108 frames when the partner test passes;
  V may be its variant). The `cornerKickType` constant (2) is NOT the selector
  (already killed, §11.12).
- **Ordinal → slot** `FUN_14051b8b0` (OBS §11.6, unchanged): designated
  header kinds → keeper-if-chasing → stat-sum ranking; ordinal 0 = keeper.

### 3.3 GF code each finding replaces

- `constant_positionCK.bin` decode (entry table + 368-B bodies) → GF's
  `positionCK` loader; N from attack level (`4 + min(level,2)`); V still open.

## 4. The corner-path kick-request writer (PARTLY closed)

Exe-wide scan for `[reg+0x28] = 0x31` stores (OBS raw): exactly three sites.

1. **`0x14208142d`** — inside the big key-dispatcher `0x142081190`. Path: outer
   key `ecx == 0x808` (`sub 0x804` → 4, `sub 4` → 0, raw 0x1420813f7–0x142081402),
   then `[rsi+0x5c]` switch: 0 → handler A, 1 → handler B (`ebx` = 1/2),
   **≥ 2 (fall-through past the two `je`) → `[rax+0x28] = 0x31`** with
   `rax = [rsi]` (raw 0x142081419–0x14208142d), then the virtual call
   `call [rax]` (0x142081546). So: key 0x808 with count ≥ 2.
2. **`0x1420815cd`** — same dispatcher, common tail: after the handler address is
   stored (`[r12] = rdi`) and the guards pass (`[r14+0xc] != 0`, `[r15] != 0`,
   `[r14+0x28] != 0`), `ebx` switch: 0 → return, 1 → path at 0x142081715,
   2 → path at 0x1420815de, **else (≥ 3) → `[rax+0x28] = 0x31`** with
   `rax = [rsi]` (raw 0x1420815b1–0x1420815cd), then `call [rax]`.
3. **`0x14208397c`** — inside the small function `0x142083930`, keyed on
   `[rcx+0x64]`: 0 → setup path (writes `[rdi+8]` tables, returns);
   1 → alternate path; **else (≥ 2, incl. the fall-through) →
   `[rax+0x28] = 0x31`** with `rax = [rbx]` (raw 0x142083958–0x14208397c),
   then `jmp [rax]`.

Neither function has any `rel32` caller anywhere in the image (all-section scan,
OBS) and neither address appears in any vtable/pointer scan — both are reached
by register-indirect dispatch (consistent with the AI command/think-unit
dispatch through `FUN_142132660`-emitted targets). The `lea rdi,[rip+X]`
targets in the big dispatcher are code (handler functions), not strings. The
outer key `ecx = ([r14−4] << 8) + [r14]` is a (kind, sub) pair; 0x808 = (8, 8).

**Which one fires for corners is still open** — the match was between games
(mode 0) when the live check ran, and no corner occurred while the kicker's
`ap+0xab8` watch was armed. INF(low): site 1/2 (the big dispatcher, key 0x808)
is the pad/AI command path (§11.1's translators feed it), site 3 (small func)
the wall/loop path; the corner kick request should be site 1/2. Next step: arm a
watch on the kicker's `ap+0xab8` word during a MODE-4 wait and read which site's
`[rax+0x28]` store lands (hardware watchpoint on `ap+0xab8+0x28`, or poll
`ap+0xab8` at 54 Hz from the armed counter to the CORNERKICK action).

Note for §12: the "0x31" here is the **AI-command word** at `[obj+0x28]`, not the
pad enum `PAD_COMMAND_KIND_WALL` (0x31) — the §11.1 flag stands: same number,
different enums.

## 5. Open list (exact next step each)

1. Zone-template writer function: no displacement store exists; find the
   base+register copy (watch `T+0x4b4c` for writes during a corner stoppage, or
   trace readers of `dfLineWidth_Corner`/`F[8]` in shape code). Values closed (§1).
2. `FUN_1405346e0` passes 2b/6/7 internals at restarts: entry conditions and
   weights per pass (which pass paired each FK1/FK2 man). Needs MA-distance-list
   logging per pass — breakpoint tracing (debug registers were busy) or a
   per-frame `M+0x58/M+0x84` dump during a stoppage.
3. Corner V selector + `positionCK` reader: log `FUN_141e5ca20`'s id arg across
   corners (ids 0x40–0x48), then read V's producer.
4. 0x31-writer attribution: watch kicker `ap+0xab8+0x28` during a MODE-4 wait (§4).
5. CK1 idx 7–10 x = −46.06 collapse: stage-by-stage track of one corner
   (positions + `T+0x4b4c` + `T+0x26c` per stage 5/7/10/13) to separate the
   budget clamp from the MARK-branch move.
6. FK-line exact formula (11.75 m vs open-play line): two samples only; needs an
   FK at d ≈ 20 and d ≈ 40 to fit.

## 6. Ledger entries

| ID | Claim | Conf. | Evidence |
|---|---|---|---|
| EV-SPZ-0001 | Corner zone = own-frame (−F[8], z ∈ [−6,+6] = dfLineWidth_Corner) + keeper −(F[0]−1.83), box-line −F[6], goal-area corners, F[17] outlet | Confirmed (live) | CK1 early `T+0x4b4c`, scratch `live.jsonl` reads |
| EV-SPZ-0002 | FK line ≈ 12 m goal-side of ball when box-zone gate true (FK2); ordinary shape line when false (FK1) | Confirmed (live ×2) | FK1/FK2 ready `T+0x4b4c` + gate §2.1 |
| EV-SPZ-0003 | Pass-5 gate: corners always; FKs only in box zone (`dir·R.x ∈ [0,F[6]]`, `\|R.z\| ≤ F[7]`) | Confirmed (raw) | 0x140a532f0–0x140a53399 |
| EV-SPZ-0004 | Pass 5 = stat-ranked pairing + MA-distance fill + kicker-in-top-3 | High (decompile) | `ds3.c` `FUN_140530320` |
| EV-SPZ-0005 | At ready, marks == bodies ≤ 0.5 m; corner marks within 0.3 m of one attacker each | Confirmed (live) | CK1/FK1/FK2 ready reads |
| EV-SPZ-0006 | N = 4 + min(attack level, 2); `positionCK[_N_V]` bodies decoded (10/10) | Confirmed (raw + VFS) | 0x1404f6680–0x1404f66f2; `ck.bin` decode |
| EV-SPZ-0007 | V varies per corner at fixed N; reader computes the id (no immediate) | High (live + scan) | prior V = 1→2; imm32 scan |
| EV-SPZ-0008 | Three `[obj+0x28] = 0x31` sites: 0x14208142d (key 0x808, count ≥ 2), 0x1420815cd (count ≥ 3), 0x14208397c (key ≥ 2) | Confirmed (raw) | exe-wide store scan + branch traces |
