# PES 2021 defence leftovers: cover + offside open items (DefenceLeftoversRE)

Tags: **OBS** = decompile at a stated VA checked against raw (`pdis.py` over the
installed exe) or a live read; **INF(conf)** = inference with confidence.
Notation: as `pes-re-defensive-shape.md` §15 (`T` = team record, `dir` = attack
sign, `MA` = analysis object, own frame x toward the opponents' goal, metres).
`fps` = `FUN_141e532e0()` (54). Match state: the game sat at a menu the whole
session (`MI = *(G+0x270)` null over repeated polls, `G+0x2aa8` null), so every
live-sample item below stays open with its exact next step. No names of players
or teams appear.

Scratch: the DefenceLeftoversRE scratch dir (`memu.py` read-only `/proc` helper;
no samples taken). Decompiles: Unpacker `out/dl1.c`–`out/dl5.c` (`annot.py` run
on each).

## 1. Defensive cover (`pes-re-defensive-shape.md` §15 open list)

### 1.1 `FUN_14062ade0` — CLOSED (OBS raw + decompile `dl1.c`)

The >7 s branch of `FUN_14062acf0` (§15.6: lane-start line used by the cover
gate `FUN_140632210`). Raw 0x14062ade0–0x14062aebd:

```
```
b = sheet byte FUN_1408c52e0(T+0x8e08, 2, 3)      // k2 = Defensive Line slider
w = b <= 0 ? 0 : b >= 10 ? 1 : b / 10.0           // 10.0 @0x142588210
style = FUN_140a62180(ctx, side, 4)                // 4 = Defensive Style
lo   = style==0 ? 20.0 : -8.0                     // 20 @0x14259be1c / -8 @0x1425c7ba4
base = style==0 ? F[0] (52.56, FieldInfo live) : 22.0   // 22 @0x1425c48c0
return lo + (base - lo) * w * 100.0 / 100.0       // 100 @0x14253ee30 (dead scale)
```
I.e. style 0 (fore-check): `20 + (F[0]−20)·w` (20…52.56 m); style 1 (retreat):
`-8 + 30·w` (−8…22 m). At the observed sliders (k2 = 4/8 → w = 0.4/0.8) that is
33.0 m vs 16.0 m — the front-foot team's lane start sits far higher, INF(high).
The `×100/100` is a decompile-faithful no-op (INF(high): leftover
normalisation). The report's §15.6 "−40/−30/+20/+30 attack-level offsets" live
in `FUN_14062acf0` itself, not here — this branch only replaces the base by the
Defensive-Line slider ramp when > 7 s (`7·fps`, 0x14062ad53) have passed since
the newest kick-off. No kick-off-straddling sample exists to live-check it
(open §1.8 item 1: refit `linecheck2.py` against a 60 s kick-off sample).

GF: the port's lane-start equivalent (the value `FUN_140632210` compares the
ball against) gains this branch: `> 7 s since kick-off → slider ramp above`
instead of the level-offset table. File: the port's cover-gate helper (port
owner to place; §15.17 composition map stands).

### 1.2 `FUN_14062ff60` — CLOSED (OBS raw + decompile `dl1.c`)

Cover feasibility used as the no-progress guard `FUN_140630040` calls (§15.10.3)
and at the head of `FUN_14062e240` (below). Raw 0x14062ff60–0x14063003e,
`rec` = the cover record (`+0x10` = line index word, `+0` = covered slot,
`+4` = per-point flag, `+0x36` = point-valid byte):

```
s = dir (own sign); F18 = pitch+0x18 (FieldInfo depth scalar, live value open)
if 5.0 - F18 <= s * rec[1]:                        // 5.0 @0x14259222c
    if rec[4] == 1:
        line = T+0x87d4 + covered*8 (covered man's sub-line byte)
        if (line == 0 && flag == 0) || (T+0x877c - 1 == line && flag == 1): return 0
    side3 = side + 3; rep = MA line MA+0x18 + side3*0x14 (per-side 5-wide row)
    if s * rep <= s * MA.ball.x  ||  rec+0x36 != 0: return 1
return 0
```

I.e. returns 1 ("feasible") when the cover point is upfield of the
`5 − pitch+0x18` line **and** (`rec+0x36` set **or** the side's MA report value
is goal-side of the MA ball). The `T+0x87d4`/`T+0x877c` sub-line comparison is
OBS structure; the meaning of the sub-line bytes is INF(low). Note this also
gives a second sighting of `pitch+0x18` (offside item §2.3): here it is a depth
threshold compared against the cover point's own-frame x after a 5 m offset.

GF: veto helper in the cover/press enrolment path (port owner to place).

### 1.3 `FUN_140633350` — CLOSED (OBS raw + decompile `dl1.c`)

The second-presser enrolment test (§15.15: `FUN_140633940` calls it per
candidate "within radius → `FUN_140633350` enrolment test"). Args
`(ctx, T, slotIdx, mode)`:

- mode 1: return 1 iff plan `(0xe)` is armed for the slot's mark target
  (`FUN_140a63950(T, side, 0xe, T+0x558[mirrored])`) **and** `T+0xb8c4` chain set
  — i.e. the man is already chained to the special target.
- mode 4/5/6 (`param_4` 4..6, incl. the reason-7 path): needs plan `(0xe)` clear
  on that target; then the arrival race: every other on-pitch own man (not the
  candidate, not presser `FUN_140a62960`, not keeper `FUN_140a62690`, not in a
  kick/trap class `(state+1 − 0x11) ≤ 0x3f masked 0x8000000000000009`, not
  0x52/0x57) is tested by `FUN_140a53da0(ctx, man, point=T+0x26c-ish target,
  heading=`FUN_140a4db20`, radius 50.0 @0x14259be34, base `|Δline|+5.0`
  @0x14259222c)`: any man that passes (can arrive) vetoes → return 0, else 1.
  Pre-gates: mode ≠ 1/4/5/6 with plan `(0xd)` armed → 0; the candidate must be
  > 4 m (16.0 = d² @0x14259be10) from the `FUN_14063fdc0` ball-or-receiver
  point; the receiver must be upfield of `MA.line+0x40 + 6` unless within
  20 m of the candidate (`FUN_140a4c720(...,20.0)` @0x14259be1c).
- other modes (0/2/3): plan `(0xd)` veto only, then the `(0xe)` + `T+0xb8c4`
  chain test as mode 1 → 0 unless chained.

INF(med): "enrol the second presser only if no team-mate beats him to the
receiver's point" (the `FUN_140a53da0` arrival sweep is the substance; the exact
50 m/5 m constants are OBS immediates).

GF: the second-presser pick in the port's PRESS writer (port owner to place;
live check against PRESS reason-7 snapshots in `live2.bin` still open — §1.8
item 6 stands).

### 1.4 `FUN_14043b8b0` — CLOSED (OBS raw + decompile `dl1.c`)

Not a cover function: it is the **MA per-player distance-list reader** used by
the arrival comparison in `FUN_14062e240` (below) and elsewhere. Args
`(MA, playerIdx, listKey)`: `entry = MA+0x22e4c + clampIdx·200`,
`rank = entry+0xb2[key]`; returns `entry+4+8·rank` (the distance float at that
rank) via the float-return thunk `*0x1425301f0` (Ghidra shows `void`; the raw
`call [rip]` + xmm use at the call sites confirms a float return — the known
Ghidra float-return drop). `idx ≥ 0x16 → base 0x22e4c`; `key ≥ 0x16 → 0.0`.
This is the same `MA+0x22e4c` table the mark passes read (§14.2: {id, distance}
× 11 per team, rank byte at `+0xb2+id`).

GF: no change (read helper only; the port already reads this table).

### 1.5 `FUN_14062e240` accept rule + low-ball scorer — CLOSED (OBS raw + decompile `dl2.c`)

Signature `FUN_14062e240(ctx, T, rec, pointIdx) → slot or 0xff`. Two loops over
the 11 own slots (raw 0x14062e240–0x14062ed3e; the decompile's float arithmetic
checked against raw — Ghidra drops the `FUN_140a92950`-family float returns but
the compare structure matches instruction-for-instruction):

1. **Nearest-enrolled pre-pass** (raw 0x14062e410–0x14062e522): over own
   on-pitch men (`MI+0x1748`), track the nearest (own-frame distance²) to the
   cover point → `best`. Seeded at 250000.0 (@0x1425cee24).
2. **Accept loop** (raw 0x14062e5c5–0x14062ed3e): per man, skipping off-pitch
   men, the MATCH_UP man (`rec+0x8`), `param_3[5]/[6]` (the two already-taken
   slots), plan-`(0xf)` men, `FUN_140630ba0`-vetoed men, in order:
   - **line/role gate** (raw 0x14062e6d5–0x14062e708): the man's line
     `T+0x87d0` must equal the covered slot's line `iVar7`, with the two OBS
     exceptions: (a) `FUN_14062ff60(...) == 0` (infeasible, §1.2) with covered
     line 0 and the man on line 1; (b) the `T+0x877c/T+0x87d4` sub-line match
     (`line == sub − 1` with the flag test). Structure OBS; sub-line meanings
     INF(low).
   - **low-ball scorer path** (raw 0x14062e708–0x14062e796): `d² =
     |point − T+0x4b4c(man slot target)|²` (own frame, via `sqrtf` =
     `FUN_1415a1780`); when the covered line is 1 and the man is on line 2,
     needs option-byte-5 `FUN_140a62180(...,5) == 0` **and** `T+0x6ec == 2`
     else skip; then the enrolled test at raw 0x14062e79e–0x14062e7af
     (`addss` 5.0 @0x14259222c, `comiss`+`cmova`): 5.0 re-resolved from the raw
     load (OBS constant; the draft's 7.0 was a misread — @0x14259bdf0 is the
     incumbent-hold constant at 0x14062eab1, a different site). Per the `dl2.c`
     decompile this reads as `d + 5.0 ≤ best` (beat the pre-pass nearest by
     5 m) — INF(med) on the register→variable mapping, since xmm13's reaching
     value was not fully traced to `d`. The §15.8 "low-ball" name stands (this
     path runs when the ball is below 14 m and the `FUN_14062c5a0` high-ball
     scorer did not run).
   - `FUN_140630d50` veto (unless enrolled); the **no-progress guard**
     (§15.10.3 `FUN_140630040` family, raw 0x14062e800–0x14062e83c):
     needs `rec+0x36 ≠ 0`, the covered-line match, the stored best-slot match
     and `d ≤ 4.0` (4.0 @0x1425656c0) else skip to the next man.
   - **arrival race** (raw 0x14062e83c–0x14062e9b0): the current best man
     (`ebx`) vs this man (`esi`): `dBest − d ≥ 5.0` (5.0 @0x14259222c) wins
     outright; else compare goal-side-ness: `|candBody − point| ≤
     |bestBody − point|` (both own-frame, `sqrtf`'d) wins the swap.
   - **same-point incumbent hold** (raw 0x14062eaa8–0x14062eac5): when the
     candidate is the current best (`ebx == esi`, enrolled): hold iff both
     are within 7.0 m (7.0 @0x14259bdf0, raw 0x14062eab1–0x14062eabd), then
     `dCand += −5.0` (−5.0 @0x14259be7c, raw 0x14062eac5 — re-resolved; the
     `0x…be7b` read was a 1-byte-misaligned artefact, confirmed by the
     neighbour reads `0x…22b`→5.0-at-`0x…22c` behaving the same way).
   - **angle windows** (raw 0x14062e9d0–0x14062ec46): the point must be
     upfield of the `FUN_14062d790` floor (§15.7.1) within the fabs-masked
     compare (mask @0x142565d30; floor in `rbp−0x6c`); bearing(point→man) vs
     straight-back `FUN_140a4db20`, wrapped to ±180° via `FUN_140439e50`
     (±360.0 @0x14259858c/98, 180.0 @0x142598580, raw 0x14062ebd6–0x14062ebf5):
     accept iff within **90°** (90 @0x14259be5c, raw 0x14062ec22), or within
     **180°** when `rec+0x36 ≠ 0`. Side-sign seed: −1.0/1.0
     (@0x142588218/0x14256ae1c by wrap side, raw 0x14062ebfd–0x14062ec0f);
     `rec+0x36 == 0` adds +2.0 (@0x142565604, raw 0x14062ec36). Enrolled-slot
     arrival: a man already covering this point (`param_1+0x28[pointIdx] ==
     slot`) runs his arrival on the `fps/6` time base (@0x14259bdec, raw
     0x14062eb19) vs 0, and the arrival/base compare divides by `fps/3`
     (3.0 @0x14258c264, raw 0x14062eb59).
   - **final scorer** (raw 0x14062ec50–0x14062ed3e): `score = d − credit`;
     `T+0x4b48`-slot / `ball + 10.0` (@0x142588210, raw 0x14062eb7d)
     goal-side test; `FUN_140630780` veto → `+5.0` (5.0 @0x14259222c, raw
     0x14062ec81 — re-resolved to the aligned address); state-class ≠ kick
     `+15.0` (@0x14259be0c, raw 0x14062eca9); `score < bestScore → best =
     slot` (seed 200.0/340.0 @0x1425a2668/0x1425aa1c4 by side sign — OBS
     presence, INF(med) on which side gets which).
   - constants re-resolved from the exe this session: 250000.0 @0x1425cee24
     (seed), 200.0 @0x1425a2668 / 340.0 @0x1425aa1c4 (side-sign score bases),
     100.0 @0x14253ee30, 12.9 @0x14259be08, 7.0 @0x14259bdf0 (incumbent hold),
     5.0 @0x14259222c (low-ball beat, arrival race, +5 veto — three sites),
     −5.0 @0x14259be7c (incumbent bonus), 4.0 @0x1425656c0,
     3.0 @0x14258c264, 2.0 @0x142565604, 90.0 @0x14259be5c,
     180.0 @0x142598580, ±360.0 @0x14259858c/98, 0.6944/fps @0x14259bdd0,
     10.0 @0x142588210, 15.0 @0x14259be0c.

   The §15.10.1 "100 m/×2" enrolment radius: OBS in the decompile
   (`fStack_16c` divisor path uses the 100 m cap @0x14253ee30; the ×2 line-1
   scale is at raw 0x14062e929 in the prior pass's read — confirmed present in
   this decompile as the `param_3[4]`-scaled compare, INF(med) on the exact
   operand order).

GF: the port's cover-man pick (slot-fill `FUN_140631cb0` equivalent) — replace
its distance-only choice with the accept rule above (line match → 5 m
low-ball beat → arrival race → 90°/180° windows → capped score).

### 1.6 `FUN_1406350c0` keep/drop — PARTIALLY CLOSED (OBS structure + new constants; exact compare order open)

Raw 0x1406350c0–0x140635aa0 head + the prior pass's raw tail (§15.13.2).
New OBS this session from the head (raw 0x140635199–0x140635221):

```
recvHdg = T+0x250 + 180°                      // receiver heading + 180
x0 = 6.0 (@0x14259bdec); x1 = 2.0 (@0x142565604); x2 = -10.0 (@0x14259be84)
plan (0xd) armed → x0 += 2, x1 -= 10          // 6→8 m floor, wider drop
side test: comiss(0.0, recvHdg + sideSign) ... // 1.0/-1.0 side weights
```

The 40 m (@0x14259be2c) / 8 m (@0x14259bdf4) ball↔receiver gap gates and the
`min(6, r)` floor at attack-direction parity (§15.13.2) are confirmed present;
the option-byte-5 ball-side reroute (45.0 @0x14259be30) is confirmed at
0x1406352ea. What remains open (unchanged): the exact keep/drop compare order
in the 0x1406354bf–0x1406355c7 tail — the lane points are stack-local, so this
needs a lane-point live capture (stack values are not in `T`; §1.8 item 3
stands with a narrower target: instrument `rsi+0x58/0x2c` per lane).

GF: the port's PCC lane-keep helper (port owner to place); the 6→8 m plan-0xd
floor delta is new.

### 1.7 `FUN_140536fc0` middle gates — CLOSED as far as static goes (OBS addresses; meanings INF(med), tracer still open)

Raw 0x140537261–0x140537380 (decompile `dl2.c` `FUN_140536fc0`, structure
checked against raw):

1. **Concept-flag gate** (raw 0x140537261–0x14053727f): run the body iff
   `T+0xf1 ≠ 0 || T+0xb712 ≠ 0 || T+0xb715 ≠ 0`. Cross-report: no other report
   names these three bytes; `T+0xb70c` (next gate) and `T+0xb709` (the
   PRESS-vs-DELAY branch, §15.14.1) sit in the same `0xb70x` concept-flag row
   as TransitionsRE's `T+0xb6e8` flags and the `T+0xb748` concept ids — so
   INF(med): `0xf1/b712/b715` = "a team concept (press/cover scheme) is
   armed". Exact concept ids open → tracer item (§1.8 item 4 stands, narrowed
   to: log `T+0xf1/b712/b715/b70c/b709` alongside `T+0xb748` over a press
   episode).
2. **Level/match-object gate** (raw 0x140537285–0x1405372dd): team level flag
   `FUN_140a604d0(level, 6)`; if clear, the match object `*(G+0x3b0)` must be
   non-null, `FUN_1423e7fd0(obj) ≠ 0` and `FUN_14234e8e0(obj) == 0x10` else
   skip. INF(low): 0x10 = a match-mode (training/online-practice family, cf.
   duels-fouls §2.5's `G+0x3b0`/`G+0x400` suppressions).
3. **`T+0xb70c == 0`** (raw 0x1405372dd–0x1405372e5) else skip. INF(low): a
   "press already committed elsewhere" latch (it is also tested in the
   `FUN_140634470` second-man path, §14.4).
4. **Loose-ball 13 m gate** (raw 0x1405372eb–0x14053733d): no carrier
   (`FUN_140a627e0 > 0x15`) → need `|MA.ball − BallInfo|² ≤ 169.0`
   (169.0 @0x1425c6788 = 13²) else skip. OBS; confirms the §15.14.1 "loose
   ball within 13 m".
5. **Restart pressure-score gate** (raw 0x140537343–0x140537380): after a
   type-1–3/0x10 newest event (`FUN_140a3e190(ctx+0xe0)`), need
   `FUN_140a51320 > 2` (second site; the head needs `≥ 3` at 0x14053712b).
   `FUN_140a51320` semantics: per team-plans §4 (Chance/Counter/SecondLine
   run-flag gates) and off-ball §2 (`PASS_GET with score > 1`), it counts
   current attacking opportunities — INF(med): "pressure/advantage score"
   (number of live chances). Exact unit open → same tracer item.

GF: the port's Delay/Press writer head gates (port owner to place); treat
`T+0xf1/b712/b715` as one OR-ed "concept armed" byte until the tracer lands.

### 1.8 `FUN_1405fb6f0` kick-off rotation + MODE-3 shape — STRUCTURE CLOSED (OBS raw; values need the kick-off sample, still open)

Raw 0x1405fb914–0x1405fbaf3 (checked against the `dl2.c` decompile):

- **Rotation** (raw 0x1405fb914–0x1405fb961): `xmm4` holds the rear-ramp value
  `x`; out of play (`MI+0x116c ≠ 1`): `xmm4 *= 90.0` (@0x14259be5c),
  `x = sin(xmm4·(π/180))` via `FUN_140a92c80`, i.e. **`x = sin(90°·x)`** —
  the §15.16 "sin form" confirmed with the 90.0 constant. In play with
  `bp+0x364 ≠ 0`: `x = sin(90°·x + 90.0)` then `xmm4 = xmm7 − xmm0` where
  `xmm7` = the unrotated rear — i.e. **`rear -= sin(90°·(x+1))`-style dip**
  (exact operand: `mulss xmm4,[90.0]; addss xmm4,[90.0]; call sin;
  xmm4 = base − sin`; OBS order, INF(med) reading: a kick-off dip term gated
  by the `bp+0x364` flag byte). `bp+0x364` semantics open (a basePosition.o
  flag; live value not dumped this session — read `bp+0x364` in the kick-off
  sample).
- **MODE-2 −6 m** (raw 0x1405fba42–0x1405fba89): at MODE 2,
  `rear −= 6·r`, `r` = ball-vs-`F[0]` ramp; `r` uses 10.0 (@0x142588210) as
  the comparison base and 6.0 (@0x14259bdec) as the step — OBS constants.
- **MODE-3** (raw 0x1405fbb44–0x1405fbaf3 region per §15.16 + tail
  0x1405fbaec–0x1405fbaf3): `MI+0x1554 == 3` (goal kick) skips the rotation
  entirely (the `cmp [rsi+0x1554],3` at 0x1405fbaec falls through to the tail
  clamps); the §15.16 "line-2 rear = `dfLineRate_goalKick` (0.1)" store stands
  as code-only. The tail clamp `min(rear, 18.0)` (18.0 @0x14259be18,
  raw 0x1405fbad3) and the `max(x − len, −F[6])` / `min(·, 2.0)` clamps
  (raw 0x1405fb961–0x1405fbbdb) confirmed.
- **Live**: no kick-off rows in any sample to date (`FUN_140a74ec0` reads
  0xffffffff throughout `live2.bin`; this session the game was at a menu).
  The 60 s kick-off-straddling sample at ~20 Hz (goal or half start) still
  needs taking: refit `h` (5 s → 1, 8 s → 0 hold-back), the `bp+0x84 = 2.0`
  base, the `bp+0x364` dip and the MODE-2 −6 m term.

GF: the port's in-possession line builder (phase-1 bands; port currently skips
phase 1 per §15.17 — the rotation only matters if the port adopts phase 1).

## 2. Offside (`pes-re-offside.md` §9 open list)

### 2.1 Ball-event types 9/10/13/15 — PARTIALLY CLOSED (OBS ring table + reader cross-refs; names INF(med))

New OBS this session:

- **Ring table** @0x142642e60 (`FUN_140a3dc40` `iStack_678` map, read from the
  exe): msg kind → ring: **7 → 0** (ball events), 8 → 1, 11 → 2, 23 → 3,
  30 → 4 (fouls/cards), 37 = 0x25 → 5 (restarts — confirms TransitionsRE §1),
  14 → 7, everything else → 10 (incl. kind 0x1f).
- **Ball-event type vocabulary** (OBS reader cross-refs across reports):
  - type 5 = shot (duels-fouls §2.5/§5.2/TransitionsRE trap rules: "after
    event 5 (shot)"; judge advantage "type 5 (INF: a shot)").
  - types 1/2/3 = passes (foul-choreography §5: "newest pass event of types
    1–3 or 0x10"; attack-decisions §1.2: "type 1..3 and 0x10
    (passes/touches)").
  - type 0x10 (16) = deliberate touch (same refs; offside §2.3 groups it
    with 1..8/11/12 as deliberate play).
  - types 6/7 = dribble-touch family (duels-fouls §2.4: "event 6/7: missed
    the ball" bonus for the last ball player).
  - type 4 = trap/receive (duels-fouls §4 item 7: "kinds 4, 10..13 → 1"
    grouped as control-ish; TransitionsRE §4.3: touch kinds).
  - type 10 = ball out of play / touchline (throwin-choreography EV-TI-0001:
    "judge event 10" is the touchline-out event; duels-fouls §5.2 excludes
    10/13/14 from the keeper-interference path).
  - types 11/12 = deliberate play (offside §2.3 jump table: 11/12 clear the
    other team's flags; foul-choreography §2.4 passer search uses 1–3/0x10
    only, so 11/12 are deliberate non-pass plays — INF(med): cross/clearance
    family).
  - types 8 = deliberate play (same jump-table arm: clears).
  - **type 9: INF(med) deflection** — does not clear (§2.3 jump-table arm
    0x1408c4624 shared with 10/13/14/15), and is the only non-clear type in
    the 8/9 boundary that is neither a play (8 clears) nor a stoppage (10).
  - **type 13: INF(med) keeper save / keeper touch** — excluded alongside 10
    from the keeper-interference "not of type 5/10/13/14" test (duels-fouls
    §5.2 step 3: the ball at the keeper's feet after a save is not
    "interference"), i.e. 13 is a keeper-ball event.
  - **type 15: INF(low) ball-out-of-play sequel (goal kick / corner award)**
    — non-clearing like 10/13/14, in the same jump-table arm; no reader
    distinguishes it from 10 yet.
  - type 14: never evaluates (§2.2 step 3, OBS `cmp edx,0xe` at raw
    0x140457ecc) — INF(low): dead-ball / whistle event (no evaluation and no
    clearing either, sharing the 0x1408c4624 arm).
- The §2.3 split therefore reads (names INF(med), arms OBS): deliberate plays
  {1..8, 11, 12, 16} end the opponents' phase; contact/stoppage kinds
  {9 deflection, 10 ball-out, 13 keeper-save, 14 dead-ball, 15 out-sequel}
  do not. Exact 9/15 labels still want the poster trace (find which kick/trap
  action posts types 9/15 — breakpoint or anime-action cross-check, open).

GF: `referee.cpp` `BallTouched` / `TouchResetsPhase`: keep the current mapping
until 9/15 are poster-confirmed; no code change now.

### 2.2 Kind-0 sub-codes 2/3 — CLOSED (OBS dispatcher raw)

`FUN_140458a10` raw (0x140458a10–0x140458a54):

```
kind = msg[0]
kind == 7 → FUN_1404621d0 (ball event)
kind == 0 && msg[0x14] - 2 <= 1 (i.e. sub ∈ {2,3}):
    S = MA+0x25378 (via +0x24d70/+0x608); S.bits[0..1][0..3] = 0 (4 u64 stores)
else: return
```

So the all-clear poster is **any kind-0 message with `+0x14 ∈ {2,3}`**
(OBS addressing; the poster itself still untraced — the kind-0 family is the
match-flow/stoppage message family per foul-choreography §1's kind-3 scene
steps, but no caller writing `+0x14 = 2/3` was found in this session's static
pass). INF(med, narrowed from INF(m)): sub 2/3 = stoppage / restart set-up
(the report's existing label stands; what is new is the OBS clear width:
**both teams × all four categories, 32 bytes**, and that the clear bypasses
the gate entirely — it runs even out of play, unlike evaluation).

GF: `referee.cpp` stoppage handler: clear both teams' flag categories 0..3 on
stoppage (already the behaviour; this confirms the width).

### 2.3 `pitch+0x18` and `team+0x4bd4[slot]` — CLOSED (OBS raw + second sighting)

Raw 0x140457f00–0x140457f38 (`FUN_140457de0` step 4, checked against the
`dl3.c` decompile — the decompile's XOR-mask rendering is wrong; raw has
`xorps xmm0, [0x1425656d0]` with the all-0x80 mask = **negate**, so the
compare is `s·player.x ≥ −pitch[+0x18]`, OBS):

- `team+0x4bd4+4·slot` (slot = the event player's team-sheet slot via
  `FUN_140a629c0`; slot ≥ 0xb reads `team+0x4bd4[0]`): **nonzero skips the
  depth test** — i.e. when set, types 10/13 always re-evaluate. Semantics:
  a per-player flag word; only bit-tested for zero/nonzero here. Cross-ref:
  no other report reads `team+0x4bd4`. INF(low): "player is upfield / is a
  relevant outfield participant" (e.g. set for anyone past his own box, so
  keeper clearances deep in his own end — flag clear — don't re-evaluate).
  The exact setter is open (byte-scan for `+0x4bd4` writers: next step).
- `pitch+0x18`: **negated** in the compare (`−pitch[+0x18]` vs `s·x`), so
  with `pitch+0x18 > 0` the test is "`s·x ≥ −(pitch+0x18)`", i.e. the event
  player must not be deeper than `pitch+0x18` inside his own half.
  Second sighting: `FUN_14062ff60` compares `5.0 − pitch+0x18` against the
  cover point (own-frame x, §1.2). Both uses are consistent with
  INF(med, upgraded from INF(l)): **`pitch+0x18` = a defensive-third depth
  (own-half depth limit, metres)** — events/points deeper than it (own box
  region) are exempt from re-evaluation / infeasible for cover. Live value
  not read this session (menu); read `pitch+0x18` alongside FieldInfo in the
  next live window.

GF: no change (gate-internal; GF has no 10/13-depth gate — note as
`// RE-APPROX` gap in `offsiderule.cpp` if the port ever splits keeper-save
vs pass events).

### 2.4 Body-point names — CLOSED (OBS offsets + radii + live heights; anatomical labels INF(med))

Consolidating the offside report §3/§4 with the collision report's capsule
inventory (`pes-re-collision.md` §2.1–2.4: outfield players have no hand/arm
capsules; keeper has 15) and the skeleton asset
(`body_skel.frig` / `body_*_skel.ask` exist in VFS, §2.1 item — not parsed):

| offset | r (m) | const | name (INF med) | basis |
|---|---|---|---|---|
| root `player+0x55c` | 0 | — | hips/root | base point of both loops |
| `body+0x70` | 0.15 | 0x14259d64c | head | 0.15 radius + live heights to 1.64 m (top of the stored `P` range, §7) |
| `body+0xc4` | 0.15 | 0x14259d64c | torso/hips centre | same radius, non-arm (does not set the arm flag) |
| `body+0x7c` | 0.06 | 0x14259d644 | elbow L/R side A | 0.06 + arm flag |
| `body+0x88` | 0.06 | 0x14259d644 | elbow L/R side B | 0.06 + arm flag |
| `body+0x94` | 0.06 | 0x14259d644 | wrist L/R side A | 0.06 + arm flag; hand-tip base (`FUN_140a6c360` adds the 0.21/0.05 offsets from `body+0x94/+0xa0`) |
| `body+0xa0` | 0.06 | 0x14259d644 | wrist L/R side B | 0.06 + arm flag; second hand-tip base |
| `FUN_140a6c270(body,0/1)` | 0.10 | 0x14259d648 | toe L/R | offsets (0.20428, 0.019675, ∓0.10508) × scale from `body+0xd0` rotated by `body+0xec` — a foot-end point |
| `FUN_140a6c360(body,0/1)` | 0 (quirk) | — | hand tip L/R | offsets (0.21, 0.05, 0/−0) from the wrist points; sets arm (→ 0.30 tol) but keeps the previous margin (OBS 0x140a7bc45/0x140a7bca8) |

Why INF(med) not Confirmed: the offsets/radii/flags/tolerances are OBS, but
no skeleton joint-name table was parsed (the `.ask`/`.frig` parse is the next
step) and no live body-object dump was taken (menu all session). Either would
promote this to Confirmed.

GF: `offsiderule.cpp` — no per-limb model (port uses root + 0.15 m torso
radius, `// RE-APPROX` already); nothing further to port until the skeleton
parse lands.

### 2.5 Live line cases + live whistled offside — OPEN (menu all session)

No live work was possible: `MI` null on every poll (§1 item). Standing next
steps (unchanged from §9 item 5, with exact targets):

1. Ball-limited line: watch `MA+0x44+0x14·t` vs `s·MA.ball.x` — case isolated
   when `s·ball.x < second-rearmost surface` (ball is the min in §3 step 5).
2. Halfway cap: case when both rear surfaces are ≥ 0 in the defenders' frame
   (`L = 0` exactly, §3 step 4/6).
3. Fewer-than-two: case with `count < 2` (`L = s·(−half)`, goal line).
4. Whistled offside: watch `S.bits` (via `watch_bits.py`) → `MI+0x1588 = 1`
   with `MI+0x15a0` = flag frame, `MI+0x15b4` = category, `MI+0x15a8` = line
   (detection §0 + duels-fouls §5.1 commit path).

## 3. GF code each finding replaces

| finding | GF code |
|---|---|
| §1.1 `62ade0` slider ramp (> 7 s since kick-off) | port's cover-gate lane-start helper (port owner to place; §15.17 map) |
| §1.2 `62ff60` feasibility (5 − pitch+0x18; sub-line match) | port's cover/press enrolment veto |
| §1.3 `633350` second-presser enrolment (chain + arrival race) | port's PRESS reason-7 writer |
| §1.5 `62e240` accept rule (line match → 5 m beat → arrival race → 90°/180° → capped score) | port's cover-man pick (`FUN_140631cb0` equivalent) |
| §1.6 `6350c0` 6→8 m plan-0xd floor delta | port's PCC lane-keep helper |
| §1.7 `536fc0` middle gates (concept OR, level-6/match-obj, `b70c`, 13 m, score > 2) | port's Delay/Press writer head |
| §1.8 `fb6f0` sin(90°·x) rotation + `bp+0x364` dip + MODE-2 −6 m + 18.0 clamp | port's phase-1 line builder (only if phase 1 adopted) |
| §2.1 event-type vocabulary (9 deflect / 13 keeper-save INF med; 15 out-sequel INF low) | `referee.cpp` `BallTouched` / `TouchResetsPhase` (no change yet) |
| §2.2 kind-0 sub-{2,3} all-clear, 32-byte width, bypasses gate | `referee.cpp` stoppage handler (confirms width) |
| §2.3 `pitch+0x18` defensive-third depth; `team+0x4bd4` per-player gate flag | `offsiderule.cpp` (`// RE-APPROX` gap note) |
| §2.4 body-point table (offsets/radii/arm-quirk OBS; names INF med) | `offsiderule.cpp` root+0.15 m approx (stands) |

## 4. What closed, what is open

Closed: `62ade0`, `62ff60`, `633350`, `43b8b0` (decompiles + rules); `62e240`
accept rule (5 m low-ball beat, incumbent 7 m/−5 m, arrival race, 90°/180° windows, capped score) + low-ball scorer path; `536fc0` middle gates (static; meanings
INF med/low); kind-0 sub-2/3 clear (width + bypass OBS, poster still open);
`pitch+0x18` (depth reading, second sighting) and `team+0x4bd4` (gate-flag
role OBS, setter open); body-point names (table OBS, anatomical labels
INF med); event types 9/10/13/15 (vocabulary OBS, 9/13 names INF med, 15
INF low, posters open); `fb6f0` rotation structure + constants (values need
the sample); `6350c0` head constants (tail compare order open).

Open (exact next step each):

1. 60 s ~20 Hz sample straddling a kick-off (goal/half start), then: refit
   `h` + `bp+0x84` base + `bp+0x364` dip + MODE-2 −6 m (`fb6f0`); refit
   `62ade0` slider ramp (`linecheck2.py`); dump `bp+0x364` and `pitch+0x18`
   live. (Needs a live match; menu all this session.)
2. `6350c0` tail compare order: lane-point live capture (`rsi+0x58/0x2c`
   per lane — stack-local, not in `T`).
3. `536fc0` concept bytes: log `T+0xf1/b712/b715/b70c/b709` with `T+0xb748`
   over a press episode; `FUN_140a51320` unit from the same trace.
4. `team+0x4bd4` setter: byte-displacement scan for writers; `pitch+0x18`
   live value with the FieldInfo read.
5. Body-point Confirmed: parse `body_skel.frig` joint names **or** live-dump
   one body object (offsets `+0x70/+0x7c/+0x88/+0x94/+0xa0/+0xc4/+0xd0`).
6. Event posters for types 9/15: breakpoint or anime-action cross-check on
   which kick/trap action posts them; PRESS reason-7 live check vs `633350`.
7. Live line cases (ball-limited / halfway / <2 defenders) + a whistled
   offside `MI+0x1588 = 1` with flag frame/category/line (§2.5 targets).

---

## 5. Live section (DefenceLiveRE, 2026-10-14 — match live all session)

Tags: **OBS** = live pread at stated offsets; **INF(conf)** = inference.
Samples in the DefenceLiveRE scratch dir: `ko_live.jsonl` (268 rows, ~11 Hz
effective, frames 24440–25578, `k119c` 1674→2812 — corner restart, NOT a
kick-off: `h = 0` throughout), `ko_fresh.jsonl` (395 rows gated, dt med
0.089 s / p90 0.306 s, frames 27534–28148, `k119c` 148→762 — covers the
`h = 1 → ramp → 0` zones), `press1.jsonl` (193 change/heartbeat rows over
300 s, 34 with nonzero `T+0xb88c` words), `off1.jsonl` (11 rows),
`flag1.jsonl` (long flag/kick-off watcher, still running at write time).
Samplers: `ko60.py` (ungated — superseded), `ko60b.py` (gated: every row
waits for `MI` valid; F/BallInfo/roots re-polled per row), `presswatch.py`,
`offwatch.py`, `flagwatch.py`. `MI`/`F`/`G`-slots are null for part of every
tick (OBS: `F` reads null ~19/20 polls): every row below is tick-gated
(INF(high): same timing effect as the fieldinfo report's `G+0x2aa8` note).
FieldInfo live: F = {52.56, 34.06, 3.66, 2.44, 47.06, 9.16, 36.14, 20.12}
(= F0..F7, OBS via the `G+0x2a90 → +0x10 → +0x10` chain).
Team dirs: side 0 `dir = −1`, side 1 `dir = +1` (OBS, all rows).

### 5.1 Kick-off-straddling sample — PARTIALLY CLOSED (OBS rows; refit limited)

- **Coverage (OBS).** `ko_fresh.jsonl`: 389 usable rows, all `st = 1`
(in play), `m1554 = 0`, attack level 2 both teams throughout, ball x
−43.6…−29.2 m in the `k > 378` zone. `h` zones (fps 54: `h = 1` for
`k ≤ 270.5`, ramp 271…431, 0 above): 272 / 39 / 78 rows (team-0 frame).
The `> 7 s since kick-off` (`k > 378`) zone needed by the `62ade0` refit
has 91 rows (OBS).
- **`h` effect (OBS, phase-0 edge only).** `T+0x875c` (lane 23 of the
`T+0x8700` block = back-line lower edge per the defensive-shape notation)
mean: team 0 −8.19 (`h = 1`) → −8.09 (`h = 0`); team 1 −29.85 → −35.13.
The fb6f0 `x = base + span·w` model (base 12.0, span 31.14, `lo = −39.14 +
20h`, `hi = 31.14`) predicts line-2 rear ≈ 43.0 / 12–14 in the same zones
— i.e. the sampled `T+0x875c` is the **phase-0** back-line edge, NOT the
fb6f0 phase-1 `S+0x7b38` rear, so no direct refit of `h`, `bp+0x84`,
`bp+0x364` or MODE-2 −6 m is possible from this block (INF(high): `S`
lives MA-side; MA was not relocated — perf watchpoints exhausted, §5.4).
What the rows do give: the phase-0 edge moves ≤ 0.1 m (team 0) / ~5 m
(team 1, ball-driven: bx range −33…−25 in `h = 1`) across the `h = 1 → 0`
transition — the 20 m hold-back does NOT appear in the phase-0 edge,
consistent with it living in the phase-1 builder only (INF(med)).
- **`62ade0` (open).** Level is 2 in all rows (no ±30/±40 offsets active);
the sheet bytes (`T+0x8e08` plan block: Defensive-Line slider k2,
Defensive Style) and the press timer were not sampled, so the slider ramp
is still unfitted — next step unchanged: re-run `ko60b.py` adding
`T+0x8e08` (≥ 0x20 B), `T+0xa53c`, `T+0xa298`, then `linecheck2.py`.
- **`pitch+0x18` (open, narrowed).** The offside `pitch` object (`*(X+0x10)`,
`+0` half-length, `+4` half-width, `+0x18` depth) is NOT the FieldInfo
global (OBS: exact-bit scan finds the {52.56, 34.06} pair only at the
FieldInfo block `0x1cc5f024` plus one `.rdata` copy and stack scraps —
no second live pitch-shaped block). `X` itself was not relocated (MI-value
scan: 0 refs — MI is tick-gated like everything else). Next step: relocate
`X` via the `FUN_14045cf30` perf breakpoint when a watchpoint frees up
(`flock` on the gf-perf lock), then read `*(X+0x10)+0x18` directly.
Note: the sampled `F` array above doubles as the FieldInfo ground truth.

### 5.2 Offside line cases — PARTIALLY CLOSED (OBS root/ball geometry; MA line unread)

Method: root-approximated line `L = s·min(0, second-rearmost root,
`s·ball.x) per team-row (roots are the rear-surface anchor; body-point
refinement unfitted — INF(med) per §2.4).
- **Ball-limited line: OBS (2 samples).** `ko_live.jsonl` frame 25142:
team 1 (`s = +1`), `s·ball.x = −41.46` < second-rearmost −38.31 →
`L(world) = −41.46` (ball is the min by 3.2 m). `ko_fresh.jsonl`: 0/460
team-rows (ball never the min — play was deep in team 0's end, ball x
−43…−29 vs rearmost roots ≥ −49).
- **Halfway cap: OBS (71 team-rows).** `ko_fresh.jsonl` frames ~27547+:
e.g. frame 27547, team 0 (`s = −1`): own-frame rears
[−0.01, 0.01, 26.01, …], `s·ball.x = +30.95` → both ≥ 0 → `L = 0`
exactly (the §3 step-4/6 cap). All 71 are this shape (defenders fully
behind halfway in their own frame while the ball is in the other half).
- **Fewer-than-two: OPEN.** All rows have 11 roots per team and
`MI+0x1748` was not sampled, so `count < 2` never isolates. Next step:
add `MI+0x1748+22` bytes to the gated sampler and re-run; case = any row
with `count < 2` → expect `L = s·(−half)`.
- **Whistled offside `MI+0x1588 = 1`: OPEN (not observed).** `m1588 = 0`
in all 395 + 268 + 11 rows and in final 16-row `flag1.jsonl` (5 kick-offs
caught, §5.4 item 3). Next match: keep `flagwatch.py` running; on
`m1588 = 1` capture `m15a0/m15a8/m15b4` (= flag frame/category/line).

### 5.3 PRESS reason-7 — CLOSED (OBS live words + geometry)

- **Word layout (OBS, corrects the u32 read).** `T+0xb88c+2·slot` is a u16
reason array; the watcher read u32, so each reason code `c` appears as an
adjacent-slot pair `(c·65536, c)`: (262144, 4) × 22, (327680, 5) × 10,
(458752, 7) × 2, (65536, 1) × 1. I.e. **reason 4 (×22), reason 5 (×10),
reason 7 (×2), reason 1 (×1)** over 300 s. Reason 4 dominates live; code 7
is rare (≈ the §15.14 93/354 share, INF(med): same family, different
match). No SAND/DELAY words seen (OBS).
- **Reason-7 geometry (OBS).** Frame 30689 (team 1, `dir = +1`, slot 8):
presser root (−28.80, 16.11), ball (−32.79, 19.99) → 5.57 m from the ball,
ball low (y 0.20). Frame 33407 (team 1, slot 7): presser (−34.67, −13.76),
ball (−37.65, −15.33) → 3.37 m, y 0.11. Both: the reason-7 man stands
3–6 m from a low ball on the same flank (INF(med): the enrolled second
presser at the receiver's point per `633350` — consistent with, not a
proof of, the arrival-race enrolment; the `FUN_140a53da0` race itself
needs the MA distance lists).
- **GF note.** The port's PRESS writer should treat reason 7 as "second
presser already at the point" (3–6 m), not sent from afar — same reading
as the §15.14 PRESS-vs-MATCH_UP distances (medians 9.0/9.2 m).

### 5.4 What stays open (exact next step each)

1. `h`/`bp+0x84`/`bp+0x364`/MODE-2/`62ade0` refits: need MA-side `S+0x7b38`
   (phase-1 rear) + `T+0x8e08` sheet bytes + `T+0xa53c` press timer in the
   gated sample. MA relocation is blocked on perf watchpoints (all 4 held
   by peers at sample time — `mem:0x14045cf30:x` → "No space left on
   device"); retry under `flock` on the gf-perf lock when one frees.
2. Fewer-than-two line case: add `MI+0x1748+22` to `ko60b.py`, re-run.
3. Whistled offside: OPEN — `flag1.jsonl` final: 16 rows over ~7.7 min to
   match end, 5 kick-offs caught (frames 35785, 38568, 41108, 42738, 45581),
   `m1588 = 0` throughout (no offside call; also `m15a0/m15a8/m15b4`
   never armed). Ready sampler: `flagwatch.py` (logs on flag/restart
   transitions + kick-off + 5 s heartbeat); re-run next live match.
4. `pitch+0x18`: read `*(X+0x10)+0x18` once `X` is relocated (see item 1).

## 6. Second-match note (DefenceLiveRE — new match window)

- A new match appeared (~frame 12891, `k119c` 884, OBS) but no kick-off
  arrived in a 15-min 1 Hz-gated wait (OBS: `k119c` only grew, 1463 at
  last state-1 read). By sampler time the game had left state 1 again
  (OBS: `MI` null 15 s; `ko_new1.jsonl` v3 sample: 453 rows, 359 `st = 2`
  + 94 `st = 0`, zero state-1 frames — no usable rows).
- Ready sampler for the next window: `ko60c.py` (gated 20 Hz, 60 s; adds
  `MI+0x1748` active flags, `T+0x8e08` sheet 64 B, `T+0xa53c` press timer,
  `T+0xa298`) + `flagwatch.py` for the whistle. Per the orchestrator rule
  (stop when no state-1 frames), sampling stops here; items in §5.4 stand
  with these samplers named.

## 7. Third-match window (DefenceLiveRE — kick-off-fresh sample)

Sample: `ko_kick.jsonl` (sampler `ko60c.py`: gated 20 Hz + `MI+0x1748`
active flags, `T+0x8e08` sheet 64 B, `T+0xa53c` press timer, `T+0xa298`):
637 rows, ALL `st = 1`, dt med 0.071 s / p90 0.157 s, frames 313–3729,
`k119c` 145→3561 (true kick-off straddle, OBS). All `m1554 = 0`, level 2
both teams, `m1588 = 0` throughout. Team dirs this match: side 0 `+1`,
side 1 `−1` (OBS — flipped sides from §5). F null 12, ball null 9,
roots all-null 4 rows. `T+0xa53c = 0`, `T+0xa298 = 0` throughout (OBS).
`flag2.jsonl` (10-min `flagwatch.py` run, 10 rows): 3 kick-offs caught,
`m1588 = 0` throughout — still no whistled offside (OPEN).

### 7.1 `62ade0` slider ramp — CLOSED (OBS sheet + gate arithmetic)

- Sheet decode (OBS): both teams plan 0; team 0 Defensive-Line slider
k2 = 10, Defensive Style 0 (fore-check); team 1 k2 = 8, style 1
(retreat). Predictions: team 0 `20 + (52.56−20)·1.0 = 52.56`;
team 1 `−8 + 30·0.8 = 16.0`.
- Gate outcome (OBS, 506 good rows × 2): team 0 (lane start 52.56): ball
in our half (`bx ≤ 0`) 39.2% of rows, beyond lane start 0.0% — gate
passes on the our-half arm whenever tested. Team 1 (lane start 16.0):
our-half 60.8%, beyond lane start 31.5% — the slider ramp is the binding
arm in ~1/3 of rows (OBS). bx ranges ±38 m both teams.
- The `> 7 s` (`k > 378`) zone is fully covered (`k` to 3561); level 2
throughout so no ±30/±40 offsets confound the ramp (OBS).
- GF: port the `62ade0` ramp as closed in §1.1 (team 0 → 52.56 m,
team 1 → 16.0 m at these sliders).

### 7.2 `h` zones in the phase-0 edge (OBS, still phase-0 only)

`h` coverage: 26 / 31 / 571 rows (`h` = 1 / ramp / 0). fb6f0 model
(`base` 12.0, `span` 31.14): team 0 predicts 12.0 → 21.8 → 30.0;
team 1 predicts 40.4 → 32.9 → 28.4. Sampled `T+0x875c`: team 0
−27.47 → −12.83 → −15.24; team 1 −10.0 → −23.22 → −21.38. The phase-0
edge moves with `h` in the same direction as the model but with ~15–40 m
offset (OBS) — confirming again it is NOT the phase-1 rear (INF(high)).
`bp+0x364` dip and MODE-2 −6 m still unobservable (MA-side `S`; perf
watchpoints still exhausted — two `flock` attempts this window both
failed with "No space left on device", OBS).

### 7.3 Line cases + PRESS in this sample

- Ball-limited: 40/1012 team-rows (OBS), e.g. frame 856 team 0
(`s = +1`): `s·ball.x = −31.21` < second-rearmost −31.07 → ball binds
(by 0.14 m — inside the 0.30 m tolerance band, so the call would hinge
on body-point refinement, INF(med)). Halfway cap: 0 rows this sample
(play never parked both rears behind halfway). Fewer-than-two:
`MI+0x1748` bytes are all 1 in every row (OBS) — `count < 2` never
occurs in open play; needs a red-card / injury window (next step).
- PRESS reasons (OBS u32 pairs): 4 ×16, 5 ×15, 1 ×5, 7 ×1. Reason-7 at
frame 2600 (team 1, slot 10, `k119c` 2432): presser 3.11 m from a low
ball (y 0.12) — third independent 3–6 m enrolment sighting (§5.3 + two
there). Reason 7 stays rare (1/37 words here vs 2/35 in §5.3).

### 7.4 Whistled offside — CLOSED (OBS flag words + geometry)

`flag2.jsonl` final: 27 rows, 9 kick-offs, and one whistle (OBS): frame
20246, `st = 2`, `m1554 = 5` (free kick), `m1588 = 1`, flag frame
`m15a0 = 20076`, line `m15a8 = −18.668`, category `m15b4 = 2`.
Ball at whistle (−24.452, 0.502, 4.674) — dead ball after a free-kick
award against the offside (INF(med): the FK-stoppage shape, `st = 2`
with `m1554 = 5`, matches the foul-choreography free-kick path).
Attack was team 0 (`dir = +1`): own-frame rears
[−49.42, −29.87, −24.89, −19.78, …] → second-rearmost −29.87;
`s·ball.x = −24.45` → ball-limited line would be −29.87 (defender),
while the stored whistle line is −18.67 — 11.2 m upfield of the
root-approximated second-rearmost (OBS gap). Readers: (a) roots read
~170 frames after the flag frame (stale by a full stoppage — the
defence has stepped up, INF(high)); (b) the MA line uses rear body
surfaces, not roots (±0.5 m, not 11 m). So the gap is staleness, not a
model miss (INF(med)). Category 2 = the mid band per the §4 replay
(OBS code meaning; this live row is consistent: flag raised, FK given).
GF: `offsiderule.cpp` needs no change (flag → FK path already the
behaviour; the 170-frame root lag is a sampler artefact, not PES).
