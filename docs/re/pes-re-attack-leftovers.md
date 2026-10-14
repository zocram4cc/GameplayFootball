# PES attack leftovers: off-ball + carrier + live (AttackLeftoversRE)

Read-only RE of the live image (base 0x140000000; `dec.sh` + `annot.py`, disk
`pdis.py`). Scratch: the AttackLeftoversRE scratch dir (`atk1..atk8.c`). Tags:
**OBS** = decompile/raw bytes/data read; **INF(x)** = inference with confidence.
Degrees throughout. Builds on `pes-re-off-ball.md` (OffBallRE) and
`pes-re-carrier-units.md` (CarrierUnitsRE); does not re-derive them. Item numbers
below are the two reports' open lists (O1–O9 off-ball, carrier open list).

Live state at write time: match at menu (MI null at first read, mode 2 /
frame 53080 when non-null; team pointers null outside the tick). Per the ticket,
the match was not resumed. Live checks that needed open play are listed as open
with the exact next step. No player or team names.

## 1. Off-ball

### 1.1 O1 — Chance target builder sub-functions (OBS atk1.c/atk2.c)

Dispatch order in `FUN_140602600` (Chance target builder, OffBallRE b1a.c:255–470
re-read here) per candidate slot, first success wins; pin bits in `sel+0x280..`
record which lane was taken:

1. `FUN_14060a4d0`/`FUN_14060a750` pre-gates pass → `FUN_14059ed10(...,0x46,1,1)`
   lane search (returns target directly).
2. `sel+0x2b8` pinned → `FUN_14059f9a0` copy (bit `+0x28c`).
3. `sel+700/0x2c0/0x2c4` pinned → keep stored target (no write).
4. Count of slots already in 0x1e ≥ `ctx+0x24` (cap) → `FUN_14059ba60(...,100,0xff,1)`
   generic search; fail → `y = −100` ("none").
5. `sel+0x2a4` pinned → `FUN_1406067c0` (fixed-point target, below).
6. `sel+0x2a8` pinned → `FUN_14059ed10(...,0,4,1)` from a ±6 m pushed point
   (sign from `MA+0x24834` vs runner x; `6.0 @0x14259bdec`).
7. Runner in PULL_AWAY (0x2f) → `FUN_140606860` conversion.
8. `FUN_140602b20` (needs carrier valid + `FUN_140603370/0x140602f20`) → bit `+0x294`.
9. `_good` (FUN_1405c0f00, §1.3) clear → `FUN_14059ba60(...,chanceScore,0xff,0)`;
   fail and not PULL_AWAY and `MA+0x24d38 == 0` → `FUN_140603d00` gate, then
   `FUN_14059dc20` (bit `+0x2b4`), `FUN_140673ad0` (bit `+0x288`),
   `FUN_140608430` + `FUN_1406049b0` (bit `+0x28c`), line-side test
   (`MA+0x24834` vs pitch `+0x1c`):
   `FUN_1406718f0` (bit `+0x290`) or `FUN_1406725f0`; then `FUN_140603830`
   (slot `+0x2ac`), `FUN_140604490` (slot `+0x2b0`); else `y = −100`.
   When `MA+0x24d38 != 0`: only `FUN_140603d00`, then `FUN_140608430` +
   `FUN_1406049b0`, else `y = −100` (bit `+0x298`).

Sub-function roles (all OBS signatures + head logic; full bodies in scratch):

- `FUN_14059ba60(ctx,tm,id,out,arg,scoreLim,0xff,flag)` — the generic target
  search. Score accumulator starts at 100 (`uVar35=100`); each accepted lane
  adds `fVar58·fVar40` (ushort-clamped); commit needs
  `FUN_1405a59d0` + score ≥ limit (`param_6 <= uVar35`) + `FUN_140595cb0`,
  plus the `FUN_140592fd0(...,scoreLim)` lane-budget check. So the `100` /
  `chanceScore` argument is the **minimum target score** (OBS).
- `FUN_1406067c0(ctx,id,out)` — 6 lines OBS (atk1.c): `out.x = MA+0x44+side·0x14`
  (the offside-line x for the opposite side), `out.z = MA+0x24834 ± 10.0`
  (`10.0 @0x142588210`, sign = which side of the line the runner is on).
  I.e. the pinned-`0x2a4` lane runs to **the offside line, 10 m to the runner's
  side of the ball-line z**. Port-ready.
- `FUN_14059ed10(ctx,id,from,out,slotId,sectorN,flag)` — radial sector sweep:
  sectors `bVar21..bVar8` = `gridClass(from) ± param_7`, clamped to 0x20;
  per sector reads the offside-line table `MA+0x25378` row, runs the
  `FUN_1405a6de0/60f0/73c0/65a0/7960/7520` veto battery (`bVar15 = 100` =
  vetoed); winner = min angular deviation (`FUN_140439e50` bearing − runner
  bearing, ±180/360 fold) with score `< 45°` gate (`45.0 @0x14259be30`),
  tie-break by lower line distance. **Reads the runner's own stored target
  `T+0x26c+0x40·s` (`param_5`) only for the `param_7` hysteresis test**
  (`|cur − best| < 6 m → keep`, `6.0 @0x14259bdec`; OBS atk1.c:1526) — not
  other runners' targets (see §1.2).
- `FUN_1406065b0(ctx,slot)` — sub-kind 0xe scorer (OBS full body, §1.1.1).
- `FUN_140602b20` — carrier-geometry gate: needs a valid carrier
  (`FUN_140a627e0 < 0x16`), carrier ahead of the runner line
  (`FUN_140439ef0` bearing dev ≤ `0x1425a2004`), receiver valid
  (`FUN_1405969b0`), then `FUN_140603370`/`FUN_140602f20` per-lane writers.
- `FUN_14059dc20(ctx,id,out)` — long corridor builder (≈300 lines; OBS
  skeleton): `15 m` reach gate (`@0x14259be0c`), `−10°` ball-line test
  (`@0x14259be84`), `10 m`/`1 m` corridor gates, `90°` turn gate, `60 m` range,
  `0.6944444/fps` velocity prediction, then `5 m` stepping
  (`@0x14259222c`) with `+5 m`/`+4 m` extensions. Port needs the full body;
  constants OBS.
- `FUN_140673ad0` — box-corridor search (≈430 lines; OBS skeleton): `8 m`
  (`@0x14259bdf4`)/`−7 m` (`@0x14259d62c`) gates, `0.6944444/fps` prediction,
  `100/−10` angle window, `MA+0x2444`-family depth test, `6.5 m`
  (`@0x1425a2828`)/`70 m` (`@0x14259be4c`)/`30°` (`@0x142591140`) gates,
  `FUN_1405a3080/0x1405a0ec0/0x1405a1310/0x14066f200/0x1406613f0` commit
  battery. Constants OBS; full body open.
- `FUN_1406718f0` — near-side lane test (≈330 lines; OBS skeleton): keeper-rate
  gates (`12.0 @0x14259be04`, `0x14259ce3c`, `−8 @0x142565604`), `45°`
  (`@0x14259be30`)/`25 m` (`@0x14259be20`) gates, `20 m` (`@0x14259be1c`)
  commit, `+7 m`/`−7 m` window, `0.6944444/fps` prediction, instruction-2/3
  reads, the same commit battery as `673ad0`. Constants OBS; full body open.
- `FUN_1406725f0` — far-side variant (≈380 lines; OBS skeleton): instruction-9
  reads (vs 2/3 for `6718f0`), `20→15 m` ramp (`@0x14259be20/0x14259be0c`),
  `18 m` (`@0x14259be18`)/`8 m` (`@0x14259bdf4`) windows, `6.5 m/70 m/100 m`
  gates. Constants OBS; full body open.
- `FUN_140603830(ctx,id)` — line-side predicate (OBS full body ≈90 lines):
  `FUN_140607bc0` nonzero; ball-line z ≥ pitch `+0x1c`; runner z below the
  line; runner and line on the same x-side; `FUN_140a4c720(ball.x, x, s, 8.0)`
  (`8.0 @0x14259bdf4`); `s·runner.x ≤ pitchHalf − 12.0`
  (`12.0 @0x14259be04`); `|line.z − runner.z| ≤ 24.0`
  (`24.0 @0x1425c48c4`); `s·line.x ≤ limit`. I.e. **runner is on the
  ball-line side and within 24 m of the line in z**. Port-ready.
- `FUN_140604490(ctx,id)` — cross-plan predicate (OBS head ≈60 lines):
  concept 8 set on the runner (`FUN_140a41b30`), instruction 0, a valid
  `FUN_140a63020` target, concept handed to a non-carrier
  (`FUN_140a42170`, `FUN_140a627e0` not carrier), both line coords within the
  pitch box, ball event is a pass (types 1–3/0x10 with the `6 m`
  `@0x14259bdec` reach test). Port-ready at this level.
- `FUN_140603d00(ctx,id)` — role-class gate (OBS head ≈80 lines): runner's
  `T+0x4c00` class ≥ 4; not instruction 0xd; carrier/receiver concept-4
  handshake when the receiver is known; last ball event not (type 3, sub ∉
  {3,4}); concept `FUN_14060a0b0(MA+0x24d20)` clear. I.e. **midfield-or-better
  runner, no conflicting instruction/concept, last event not a through ball**.
  Port-ready at this level.

#### 1.1.1 Sub-kind 0xe scorer `FUN_1406065b0` — closed (OBS full body)

```
a = FUN_1405988f0(ctx, slot, &pt, 0)          # base rating 0..~100
w1 = a ≤ 45 → 1; 45 < a < 100 → (100−a)/−55 (i.e. (100−a)/55); else 0
score = 100 − 10·w1                            # 100 @0x14253ee30, 10 @0x142588210
d1 = |runner.z − line.z|;  w2 = d1 ≤ 8 → 0; 8<d1<15 → (d1−8)/7; else 1
d2 = |pt.z − line.z|;      w3 = same ramp
d3 = |pt.z − runner.z|;    w4 = d3 ≤ 4 → 0; 4<d3<10 → (d3−4)/6; else 1
score −= 5·w2 + 5·w3 + 5·w4                    # 5 @0x14259222c
if roleClass(slot) > 6: score += −3            # −3 @0x14259d654
if human-controlled: score += −5               # −5 @0x14259be7c
```
Constants raw-verified (§1.1 constants table). So the 45/−55/10/8/15/7/5/4/6 of
the prior report are: 45 = rating knee, −55 = rating divisor, 10 = rating
weight, 8/15/7 = line-distance ramp, 5 = per-term weight, 4/6 = separation
ramp. The −3/−5 are role/human penalties. **O1 sub-item closed.**

GF replaces: `SelectAttackingRunPlayer` Chance branch — the dispatch order
above + `6067c0`/`6065b0` are port-ready; `59ba60/59ed10/59dc20/673ad0/6718f0/
6725f0` need bodies (skeletons + constants here).

### 1.2 O2 — corridor battery vs other runners' targets: NO cross-read (OBS)

- The only `T+0x26c`-region read in the whole battery is
  `FUN_14059ed10` atk1.c:1526: `T+0x26c+0x40·(slot%11)` field `+8` (the
  **runner's own** stored target z) for the 6 m hysteresis test. OBS.
- `FUN_140608570/87a0/9350/8ac0`, `FUN_140602b20`, `FUN_140602f20`,
  `FUN_140603370`, `FUN_1405a7fe0/7be0` contain **no** `0x26c` read (OBS grep
  over atk1.c/atk2.c/atk3.c/atk6.c). What they read instead:
  - `FUN_140608570` (OBS full body ≈60 lines): last ball event must be a pass
    **to this runner** (`FUN_140a62ea0 == id`) kicked ≤ `fps·0.5+0.5` frames
    ago (`0.5 @0x142565600`); runner z within `line+2 m` of the offside line
    (`2.0 @0x142565604`); `s·runner.x` within `pitchHalf−2 m`; line ahead of
    runner; `|Δx| ≥ 2 m`; `|Δz| ≤ 15 m` (`15 @0x14259be0c`); same-side test;
    `2.5 m` (`@0x14259bde4`) floor; `FUN_140a4c9a0(·,−0.2)`
    (`−0.2 @0x1425c8210` = tight onside margin); no instruction 10.
    I.e. **a just-played pass-to-me lock**, not a lane reservation.
  - `FUN_1406087a0` (OBS head): concept-3 handshake
    (`FUN_140a41b30(T,3,slot)`), instruction-5 veto, instruction-10 veto,
    `line − 5 m` gate (`5.0 @0x14259222c`), same-side test,
    `FUN_140a4c9a0(·,−4.0)` (`−4.0 @0x1425c7678`), carrier-lane geometry
    (`−12.0 @0x14259be04`, `2.0 @0x142565604` margins).
  - `FUN_140608ac0` (OBS head, atk6.c): last ball event must be type 1
    (pass) **to this runner** within `fps·0.5+0.5` frames; `FUN_1408c75c0 ≥ 1`
    (undecoded); concept-0 test on the team.
  - `FUN_140609350` (OBS head, atk6.c): `FUN_140a50250 ≥ 1` (attack
    instruction present); carrier predicted `0.3·fps+0.5` frames ahead
    (`0.3 @0x142590d98`); `FUN_140a4c720(x,−8.0)` (`−8.0 @0x1425c7ba4`);
    `|Δ| ≤ 30 m²` (`900 @0x1425c65f4`); `FUN_14043c430(22.5)`-scaled
    (`22.5 @0x14259d618`) facing window `< 60°` (`60 @0x14259a764`).
  - `FUN_1405a7fe0(ctx,id,flag)` (OBS full body, atk3.c): keeper-case
    (`record+0x48 == 0x18`): carrier's `MA+0x24710` rating entry for the
    receiver ≤ 4.0 (`4.0 @0x1425656c0`) and `FUN_140a4c9a0(·,2.0)` clear →
    lane free (returns 1 = **blocked**, callers keep the run when nonzero…
    polarity: callers `if (cVar6 != 0) return 0/keep`; OBS). Else the
    `G+0x180` event-table scan (stride 0x74, 0x18 entries): finds the
    receiver's entry; blocked unless the carrier-record body angle
    (`+0x45`) is a pass (2/3) with bearing ≥ 50° (`50 @0x14259be34`).
    Reads **the receiver table and the event table — never `T+0x26c`**.
  - `FUN_1405a7be0(ctx,id)` (OBS full body, atk3.c): receiver record
    `+0x1494 == 2`, `+0x14 == 8`, `+0x1c == id` (this runner is the
    designated receiver); then the `MA+0x23f7c` angle table vs the runner's
    `+0x11c` facing: free unless `angle ≥ 45°` (`45 @0x14259be30`) with the
    `15 m` (`@0x14259be0c`)/`64°` (`@0x14259be44`)/`2025 m²`
    (`@0x1425cb494`) corridor gates. Same: no `T+0x26c` read.
- **O2 closed with the hypothesis killed**: runners into the same lane are
  NOT prevented by reading other runners' targets. The actual exclusivity
  mechanisms are (OBS): one action per player (rank table), per-run caps,
  one-per-tick takes, the one-runner-per-special-lane pin slots
  (`sel+0x2a0..0x2c4`), the pass-to-me locks above, and the concept/instruction
  handshakes. INF(high): multiple Chance runners can and do share a lane
  (consistent with live: team 0 ran 2 concurrent Chance vs cap 3 with no
  lane arbitration observed).

GF replaces: nothing (no GF lane-reservation code to delete; the GF
single-runner model is still wrong per §1.3 caps, but not for lane reasons).

### 1.3 O3 — `spaceRun.o` (0xc3) per-key use (OBS)

Blob = 60 keys alphabetical, scalar-first (matchlogic §4.1 rule confirmed:
offsets match alphabetical order exactly). Full map (OBS dump, int keys shown
as int, float keys as float):

| offset | key | value | type |
|---|---|---|---|
| +0x00 | angleDiff_chance | 100.0 | f32 |
| +0x04 | checkZ | 125.0 | f32 |
| +0x08 | angleDiff_chance_good | 70.0 | f32 |
| +0x0c | angleDiff_second | 95.0 | f32 |
| +0x10 | inOut | 1 | bool32 |
| +0x14 | angleDiff_second_good | 102 |int-ish (raw 102; INF: enum/count, not float) |
| +0x18 | backwardCurve | 88 | int-ish |
| +0x1c | **chanceScore** | 40.0 | f32 |
| +0x20 | **chanceScore_good** | 69.0 | f32 |
| +0x24 | checkAngleWidthSector | 3 | int |
| +0x28 | checkAngleWidthSector_good | 5 | int |
| +0x2c | checkAreaNum | 13.0 | f32 |
| +0x30 | checkAreaNum_good | 19.0 | f32 |
| +0x34 | checkBackDist_second | 27.5 | f32 |
| +0x38 | checkBackDist_second_good | 33.0 | f32 |
| +0x3c | checkBallDist_chance | 19.0 | f32 |
| +0x40 | checkBallDist_chance_good | 25.0 | f32 |
| +0x44 | checkBallDist_counter | 29.5 | f32 |
| +0x48 | checkBallDist_counter_good | 40.5 | f32 |
| +0x4c | checkBallDist_second | 84.0 | f32 |
| +0x50 | checkBallDist_second_good | 105.0 | f32 |
| +0x54 | checkBodyAngle | 2 | int |
| +0x58 | checkBodyAngle_good | 3 | int |
| +0x5c | checkCount | 30.0 | f32 |
| +0x60 | checkCount_good | 35.0 | f32 |
| +0x64 | checkFreeDist | 97.0 | f32 |
| +0x68 | checkFreeDist_good | 105.0 | f32 |
| +0x6c | checkInterceptScore | 24.0 | f32 |
| +0x70 | checkInterceptScore_good | 30.0 | f32 |
| +0x74 | checkLastLineDist_chance | 6.0 | f32 |
| +0x78 | checkLastLineDist_chance_good | 11.0 | f32 |
| +0x7c | checkMarginDist | 90.0 | f32 |
| +0x80 | checkMarginDist_good | 111.0 | f32 |
| +0x84 | checkMoveAngle | 12.0 | f32 |
| +0x88 | checkMoveAngle_good | 2.25 | f32 |
| +0x8c | checkOffsideLineDistX | 2.25 | f32 |
| +0x90 | checkOffsideLineDistX_counter | 12.0 | f32 |
| +0x94 | checkOffsideLineDistX_counter_good | 41.0 | f32 |
| +0x98 | checkOffsideLineDistX_good | 23.0 | f32 |
| +0x9c | checkPlayPointDist | 36.0 | f32 |
| +0xa0 | checkPlayPointDistX | 42.0 | f32 |
| +0xa4 | checkPlayPointDistX_good | 92 | int-ish |
| +0xa8 | checkPlayPointDist_good | 98 | int-ish |
| +0xac | checkScore | 1 | bool32 |
| +0xb0 | checkScore_good | 2 | int |
| +0xb4 | checkSpaceRunCount | 41.5 | f32 |
| +0xb8 | checkSpaceRunCount_good | 14.5 | f32 |
| +0xbc | checkZ_counter | 20.0 | f32 |
| +0xc0 | checkZ_counter_good | 44.0 | f32 |
| +0xc4 | checkZ_good | 24.0 | f32 |
| +0xc8 | checkZ_second | 32.0 | f32 |
| +0xcc | checkZ_second_good | 16843009 (0x01010101) | pad/bool-array, not a float |
| +0xd0 | createPassCourse | 5 | int |
| +0xd4 | defenceGap | 7 | int |
| +0xd8 | roundTest | 4 | int |
| +0xdc | searchCount | 3 | int |
| +0xe0 | searchCount_good | 1 | int |
| +0xe4 | supportTime | 0.0 | f32 |
| +0xe8 | supportTime_good | 0.0 | f32 |
| +0xec | vitalSupportPrior | 0.0 | f32 |

Corrections to the prior report's VFS guesses (§1.5: "chanceScore 102 /
_good 88, checkBallDist_chance 27.5/33.0, checkLastLineDist 24/30, checkZ
41.5/44"): the dumped `o_spaceRun.o` gives **chanceScore 40/69**,
checkBallDist_chance 19/25 (27.5/33.0 are checkBackDist_second),
checkLastLineDist_chance 6/11 (24/30 are checkInterceptScore), checkZ 125/24
(41.5 is checkSpaceRunCount). The earlier numbers were real blob values under
wrong names (OBS). The `+0x1c/+0x20` read in `FUN_140602600` is therefore
**chanceScore 40.0 / chanceScore_good 69.0** as the `59ba60` score limit
(OBS b1a.c:289–295 + `FUN_1405c0f00` switch below).

`FUN_1405c0f00(ctx,id,run)` (OBS full body, atk3.c): the `_good` selector.
run 0x1e (Chance): true iff instruction 0 set, or instruction 9 set with
`FUN_140a4c9a0(·,−10.0)` clear (`−10.0 @0x14259be84`); 0x1d/0x1f → 9;
0x20/0x2f → 0; 0x21 → 1; 0x27 → 0x10; 0x28 → 0xf; 0x30/0x34 → instruction
0xc test; default → fallthrough. Return value selects the `_good` variant
(OBS `602600`: `cVar2 ? +0x20 : +0x1c`). **O3 closed for the Chance path and
the key map; the other 11 readers' per-key use remains open** (12-reader list
in OffBallRE §1.5 stands).

GF replaces: `GetAttackingRunThreshold` constants — chanceScore 40/69 (not
102/88).

### 1.4 O4 — common stop test `FUN_140688c00` tail (OBS b3a.c:534–1284)

Structure of the previously-unreduced cascade (return 1 = stop; selected
gates with verified constants; the ~40 `FUN_14068xxxx` leaf predicates remain
unreduced — listed at the end):

1. Lane-free keep: `FUN_1405a7fe0/7be0` nonzero → keep (return 0).
2. Clip window: human-opponent animation race (`+0x1c/+0x1a` frame counters
   vs `fps·0.15+0.5` = 8 f; `0.15 @0x1425c48a0`): if the window is open and
   `FUN_1405a7d10` true → stop.
3. Mark/pressure test `FUN_140a4c9a0(·,0.7)` (`0.7 @0x14257a194`);
   `FUN_14068f600`/`FUN_14068bd90` → stop.
4. Pass-event branch (`*ev ∈ {1,2,3,0x10}` and target = me): through-pass
   delay `fps/3` (= 18 f); non-through → stop now (unchanged from §2.2).
5. Concept/record branch: record kind byte `T+0x29c+0x40·s` set with concept
   `T+0xb748 == 0x30` or kind 10 → `cVar6` armed; distance ramp 4→10 m
   (`4.0 @0x1425656c0`, `10.0 @0x142588210`, `(d−10)/−6`
   `@0x1425a200c`); blocker byte `player+8>>0x15&1` → keep.
6. Ball-line branch: `|ball−line|² ≤ (15+40)²` (`15 @0x14259be0c`,
   `40 @0x14259be2c`) → `FUN_140692810` → stop; else
   `FUN_140a4c720(ball.x,x,s,−1.8)` clear → stop (non-0x1f runs).
   `FUN_140692a00/92580/692580` → stop.
7. Opponent-scan loop (11 opponents × 2 sides): `T+0xc48/0x4c00` duel entries;
   `FUN_14069cd40` lane value ≥ 0 with the `0x41a6aaab` (= 20.33) seed →
   `uVar15 = 1` (lane blocked).
8. Far-run branch (carrier valid): needs `iVar11·2 < age` (frames, `fps·2`
   window) with `FUN_14068a730/8a8c0` clear, `FUN_1405a4b90` clear, carrier
   facing race (`FUN_140439ef0`), speed race (`FUN_140481b00`), then:
   `angle > 130°` (`130 @0x1425a9bf0`) with `|Δ|² > 169 m²`
   (`169 @0x1425c6788`) and `FUN_140a4e720(·,70.0,30.0,·)` (`70 @0x14259be4c`,
   `30 @0x142591140`, `6.5 @0x1425a2828`) or `|Δ|² > 1156 m²` (`34²
   @0x1425d0240`) → stop.
9. Near-run branch (`age ≤ fps+0.5` or run == 0x1f/Counter): `FUN_14068dba0/
   de30/e300` → stop; `|Δz| > 43 m` (`43 @0x1425cfa38`, non-Counter) → stop.
10. Settle branch (non-0x2f, not class-2): `age > fps·0.8+0.5` (= 43 f;
    `0.8 @0x142572348`) arms; receiver-valid + `FUN_1405951b0(·,1.5/0.7)`
    (`1.5 @0x14259bddc`, `0.7 @0x14257a194`) → stop.
    `FUN_140a53460` → stop.
11. Angle branch (`age > fps·4`): `FUN_14068a730/8a8c0` clear with
    `FUN_1405951b0(·,−2.5)` (`−2.5 @0x1425c74d0`) set, bearing-vs-facing race
    (`FUN_140439fe0`), `100·ramp35→15 + base` test (`100 @0x14253ee30`,
    `35 @0x14259a760`, `−20 @0x1425a946c`): `100·(d−35)/−20 + base < |Δangle|`
    → stop.
12. Counter-only branch (0x1f, `age ≤ fps·5`): teammate-cone scan
    (`FUN_1405a4830`, `FUN_1404c8ea0 ≤ 56.25 m²` = 7.5² `@0x14259be3c`,
    `FUN_140439fe0` bearing ≤ base angle): found → `bVar24` (keep);
    else fall through to stop (`LAB_14068a00b`).
13. Mark-pass branch: kind 8 + `FUN_140a4c9a0(·,−8.0)`
    (`−8.0 @0x1425c7ba4`) set → `bVar24`; kind ∉ {0x12,0x15} (else two more
    tests) with class-mask `0x2264020` and `FUN_1405a40a0` → stop.
14. Duel-interception branch: `FUN_1405a1cc0` clear, carrier class-mask
    `0x22080000200` with the `fps·0.1+0.5` (= 5 f) frame race, receiver match
    (`+0x14 < 0x2a`, class-mask, `+0x1c == id`) → stop.
15. Final battery (all must be clear to keep; any set → stop unless noted),
    with the age gate shown: `FUN_140694320`, (`694320` clear and)
    `FUN_14068efc0/e900/ec50` (non-Counter), `age ≤ fps·3` or
    `FUN_14068d920` clear, `FUN_14068b1a0` (`age > fps/2`), `FUN_14068b070/
    692c50/68b920/691ef0`, (non-Counter) `age > fps·2+0.5` (= 108 f) with
    `FUN_140692fe0`, `FUN_140690b90/90630/90400/90cf0/94650/93a20/
    693730(uStack_157)/68b6e0/(Counter: 68c680)/68e660/68b440/68c050/
    693340/692ea0`, `age ≤ fps+0.5` or `FUN_140694bc0` clear,
    `FUN_140691a80/91d10`, `age > fps+0.5` with `FUN_14068fa10`,
    `FUN_14068d0b0(cVar6)`, `FUN_140691300/90960`,
    `age > fps·2+0.5` with `FUN_14068aeb0`, kind-0x14/0x15 special
    (`s·x ≤ line−10`, `|Δz| ≤ 18 m`, `FUN_14068fd90/f00` clear), kind-0x14
    sub-kind-0x13/line tests (`pitch+4 − 7 m`, `−7.0 @0x14259bdf0`),
    `FUN_140596c30` + `FUN_14068ac40(·,0)` / `FUN_1406900a0` / `FUN_140693ef0`,
    `FUN_14068c1a0/8d730/8fc70/8cde0/8cc40/8baa0/6910c0`, sub-kind-1
    `s·x` test; else keep (return 0). The function ends `return 1`.
    Age gates use `fps·0.5/1/2/3/4/5` frames (27/54/108/162/216/270 f).

Unreduced leaves (names only, no semantics claimed):
`68f600 68bd90 692810 692a00 692580 68a730 68a8c0 68dba0 68de30 68e300
68b1a0 68b070 692c50 68b920 691ef0 692fe0 690b90 690630 690400 690cf0
694650 693a20 693730 68b6e0 68c680 68e660 68b440 68c050 693340 692ea0
694bc0 691a80 691d10 68fa10 68d0b0 691300 690960 68aeb0 691660 68fd90
68ff00 68ac40 6900a0 693ef0 68c1a0 68d730 68fc70 68cde0 68cc40 68baa0
6910c0 694320 68efc0 68e900 68ec50 68d920 5a4b90 5a1cc0 5a40a0` (+
`5a7d10 5a4f40 694d10 694e10` in §1.4-9). **O4 reduced to a leaf list; the
leaves stay open** (each is a small predicate; batch-decompile on demand).

GF replaces: `ApplyAttackingRun` end conditions — the age gates (43/108/162/
216/270 f), 18 f through-pass delay, 169/1156/289 m² and 43 m/24 m/18 m/15 m
distances, 130°/110°/100°-ramp angles are port-ready; leaves are not.

### 1.5 O5 — ball-holder block ints `+0xd4/+0xd8/+0xdc` (OBS atk3.c)

`FUN_1405e0010(ctx,k)` reads them (k = 0,1,2 → f0,f1,f2 of the support-cost
report §4.3):
```
carrier = FUN_140a627e0(ctx,0); if invalid: carrier = T+0x78 (spawn point id)
if carrier valid and MI+0x116c == 1 (open play):
    holderPos = FUN_1405dfc70(carrier); holderAim = FUN_1405dfe40(carrier)
    laneAngle = FUN_140439e50(holderPos, holderAim, ballPos XOR (s≠1 ? 0x102...))
        # ball bytes XORed with 0x102... when attack sign != 1 (side flip)
    fk = FUN_140a50250(ctx, side, k)     # team instruction slot k (0,1,2)
    f0: if ballHeight > 1.5 m (1.5 @0x14259bddc): fk = 0
    f1/f2: if ballHeight > 1.5 m: return 0
    return fk
else return 0
ball-above-1.5 m forces all three flags off (OBS branches).
```
`FUN_140a50250(ctx,side,k)` is the team-instruction reader (OBS atk5.c full
body ≈868 B: indexes the team's instruction store by (side,k)). So
**f0–f2 are the team's instruction slots 0/1/2 evaluated for the
ball-holder's side** (labels open — the instruction-enum mapping lives in
tactics §6; candidates from use: f0 = a possession/build-up flag — forces
full passer weight `V·(11+5(1−t3))` and the 0.8 time ramp; f1 = a directness
flag — light weight `V·(1+2(1−t3))` with +15/+30 m reach; f2 = a width flag —
`1000×` crowd penalty beyond 30 m i.e. forces far support). INF(med) on
labels; mechanism OBS. Ball-holder block = the carrier's team AI object
(`param_2[0x1f]`-side team); `+0xd4+4k` = instruction slots. **O5 mechanism
closed, labels open.** Live logging still open (match at menu; §3).

GF replaces: `GetSupportPosition_ForceField` flag inputs — wire team
instruction slots 0/1/2 (holder's side) with the above weights.

### 1.6 O6 — the three off-parity Chance starts (OBS static)

Immediate-state scan (`statesites.py`, full 0x140400000–0x140b00000 sweep):
**no** `mov r8d,0x1e` → `FUN_140a429a0/0x140a42940` site exists anywhere.
All Chance starts go through the shared select's register-valued
`FUN_140a429a0` call (b1a.c take path) — which the scan cannot see. The three
off-parity starts are therefore INF(high): **stale timer-block reads** (start
frame sampled one tick late across the parity boundary at 20 Hz), not a
second writer. Evidence: 198/201 on-parity; the 3 off-parity starts were all
Chance (the only selector taking up to 11/tick, hence the only one likely to
straddle a sample). **O6 closed as a sampling artefact; no second writer.**
No GF impact.

### 1.7 O7 — Chance ordering live: still open (match at menu)

The mask bits (`sel+0x280..0x298`), pin slots (`sel+0x2a0..0x2c4`), scores
(`sel+8..`) live read was not captured — needs open play. Exact next step:
at 20 Hz log the Chance selector object (manager+0x1820, vtable 0x1425c61b0)
`+0x280..0x2c8` + `+8..0x68` with `T+0xa2a0[11]`; compare take order vs
`100+s·x` score order. **O7 open.**

### 1.8 O8 — live support object: FOUND statically, not yet logged (half-open)

`FUN_1405db800` param_1 derivation (OBS raw): at the `0x14067b8ee` call site,
`rcx = rsi+0x2d4` (`lea rcx,[rsi+0x2d4]` @0x14067b8e1), `rdx = rdi` (team-AI),
`r8 = [rsi+0xba8]` (slot targets), `r9 = rbx` (tracked object). The other
caller `0x1405f4ce8` passes the same shape. So **the support object is at
`rsi+0x2d4` where `rsi+0xba8` is the 11-slot target array** — rsi is the
attacking-team shape object (its identity beyond that is open; `rdi` is the
team-AI object T). The `+0x7b4/+0x40c/+0x4ec/+0x48f4` offsets are relative to
this object (OBS b6a.c). Exact next step when a match is running: from each T,
scan T±0x4000 for the object whose `+0xba8` points at 11 live slot targets
(changing each tick), then log `+0x2d4`-relative `+0x7b4/+0x40c/+0x4ec/+0x48f4`
vs `FUN_1405e0c60`. The heap fingerprint scan (718/7b4/40c < 22 + zone floats)
is written (`live_sup.py`) but untested live. **O8 half-closed (address rule
found, values unlogged).**

GF replaces: `forwardSupportPlayer`/support-web wiring (no GF object holds
this; new state keyed off the shape object +0x2d4).

### 1.9 O9 — who starts LineBreak/PullAway; CenteringGet count; GoalGet select

- LineBreak (0x20) / PullAway (0x2f): full-sweep immediate scan finds **zero**
  `mov r8d,0x20` / `mov r8d,0x2f` → state-setter sites (OBS). The shared
  select cannot start them (null masks, §1.1). **No other immediate writer
  exists** → both states are entered only via the register-valued
  `FUN_140a429a0` path or never in the shipped image. Live capture: 0 starts
  in 11.4 min (OBS §5). INF(high): **these states are vestigial in open play**
  (their stop tests + caps are maintained, but no starter calls them).
  PullAway's 12-lane picker `FUN_14061e2e0` (OBS full body, atk8.c) is the
  would-be starter logic: needs `param_2+0x30` nonzero, open play, a valid
  `FUN_1405984d0` target, `FUN_14061de20` clear, concept slot free
  (`T+0x87d0`), runner within 4 m² (`4.0 @0x1425656c0`) of the line,
  `FUN_140a4c720(·,−8.0)` (`−8.0 @0x1425c7ba4`), within 30 m²
  (`30 @0x1425c65f4`) and 18 m (`18 @0x14259be18`) of the line, `22.5 m`
  (`@0x14259d618`) facing window `< 60°` (`60 @0x14259a764`); writes the lane
  id to `*param_4`. But **no caller passes 0x2f through the select** (OBS).
- CenteringGet count `FUN_140642d00` (OBS head, atk7.c): needs possession,
  `s·line.x > 16 m` (`16 @0x14259be10`), valid carrier-or-line reference,
  `line.z ≤ 1.5 m` (`1.5 @0x14259bddc`) with `s·line.x ≤ pitchHalf`,
  `FUN_140a4e250(·,30.0/1.5 m)` pressure gates, `FUN_140a4e720(·,70,·)`
  (`70 @0x14259be4c`). Returns the count used by the CenteringGet select
  (`bStack_226`, atk7.c:111). Port-ready at this level.
- GoalGet select `FUN_14061f110` (OBS structure, atk7.c ≈11 KB): three
  candidate lanes (`dil` = 0..2); per lane `FUN_14061e690` test; writes via
  `FUN_140a42940(...,0x2a)` (the sibling setter — OBS @0x14061f6fc with
  `r8d = 0x2a` immediate). Second/third lanes add the `fps`-scaled
  frame-budget gates (`141e532e0` draws vs `ebx/8`, `ebx/4` windows — OBS
  @0x14061f737/0x14061f76c). I.e. GoalGet starts are **frame-budgeted lane
  takes through `FUN_140a42940`** (not the shared select's `a429a0`).
  Port-ready at this level; `61e690` internals open.
- CenteringGet select `FUN_14061ccc0` (OBS structure, atk7.c ≈30 KB): needs
  possession + valid `FUN_140a63020` target + `FUN_140a4ca60`; pre-passes
  `FUN_14061c120/61bc50`; count from `FUN_140642d00`; ≤ 4 existing 0x26
  runners; per-slot `FUN_1405a4830` + `FUN_140617ca0` + pressure-count
  (`FUN_140a50250(·,2)`) + `FUN_140644f10/44210` + `FUN_14061b590` gates;
  take via `FUN_140a42940(...,0x26)` (OBS @0x14061cc… `mov ecx,0x26`…
  `call 140a40050` rank check… `call 14061b590`). Port-ready at this level.
- Overlap select `FUN_140601580` (OBS structure, atk7.c ≈9 KB): needs concept
  0x1d enabled (`FUN_140ca1a50`), open play or set-play-2, possession,
  `ctx+0x24 ≥ 1`, no existing 0x27 runner (or `< cap`), per-slot rank check
  `FUN_140a40000(·,0x27)`, zone match (`FUN_140a4d2c0` vs `T+0xb824`),
  `T+0x8d40 == id`, `FUN_140641690/41f80` + `FUN_140640cc0` corridor gates,
  facing/distance windows (120/130°, 100 m, `F[1]−6 m`); take via
  `FUN_140a42940(...,0x27)` (OBS tail). Port-ready at this level.
- `FUN_1405a4830(ctx,id,a,b)` eligibility (OBS full body, atk8.c): carrier
  states 4/5 (PASS_GET-ish, `+0x5d0−4 < 2`) or 0x59 → eligible (returns
  nonzero); else defers to `FUN_1405a4790(state,record,a,b,aiClass)`.
  **O9 closed** except `61e690/61b590/617ca0/640cc0/641690/641f80` leaves.

GF replaces: `PrepareSetPiece`/overlap logic + GoalGet/CenteringGet starters
(GF has none — new branches; `FUN_140a42940` sibling setter needs a GF
equivalent or a merged setter).

### 1.10 The conflicting awareness byte `sub+0x24` — RESOLVED (OBS)

The Chance mask reads `*(byte *)(FUN_141b59e20(FUN_140a3af90(pms,slot))+0x24)`
(runs.c:463), +10 if skill-bit 0xd of `+0x48` is set, then the
75→0 / 75..95→(a−75)/20 / >95→1 ramp × 18 m (`75 @0x1425aa1bc`,
`95 @0x1425c594c`, `20 @0x14259be1c`, `18 @0x14259be18`; OBS). Chain:
`FUN_140a3af90(pms,slot)` = `roster + idx·0x128 + 0xA8` (OBS carrier-ai §6.1
correction row); `FUN_141b59e20` adds 0x30 → sub-object; `+0x24` = **roster
byte `idx·0x128 + 0xA8 + 0x30 + 0x24` = block offset +0xC8+…** — but the stat
rule (matchlogic §1.3: "stat = sub + 8 + id") is stated for the *live-stat
copy*, not this path. Direct evidence for the label: matchlogic §1.2 accessor
table maps **stat id 0x24 → Ball Winning** (`0x141f0f7f0`, bit 312, +40 bias,
40..99). The "stat = sub+8+id" rule with id 0x1C gives sub+0x24 → **stat index
0x1C = Speed** (0x141f0f7c0) — the OffBallRE §1.5 guess "Lofted Pass" used the
wrong id base (0x14 vs 0x1C is itself inconsistent in that note). Both
derivations are second-hand; the **direct** read is a roster byte at a fixed
offset, and the only authoritative label for index 0x24 in the accessor table
is Ball Winning — but that table indexes Player.bin bits, not roster bytes.
Honest status: **the roster byte at this offset is OBS; its semantic label is
INF(low)** — candidates: Ball Winning (index-number match), Speed (sub+8+id
rule), Offensive Awareness (usage match: 75–95 attacking bonus). Usage
(a 0..18 m attacking-reach bonus with a +10 skill-13 WEIGHTED_PASS kicker —
skill 13 = Weighted Pass per carrier-ai §6.1 engine order, **not** Goal
Poacher; the attack-decisions "0xd = Goal Poacher" label is wrong, corrected
here) favours an attacking stat, but no evidence pins which one. The
"conflict" is therefore resolved into: **byte OBS, label INF(low), three named
candidates**. Killed: "skill-bit 0xd = Goal Poacher" (it is WEIGHTED_PASS =
skill 13 in engine order; `>>0xd&1` = bit 13). The Counter-mask `+0x14` byte
(40/99 ramp) has the same status (byte OBS, label INF(low)).

GF replaces: `PlayingStyles::GetAttackingRunAffinity` weight input — wire the
roster byte + skill-13 bonus + 75/95 ramp; label as RE-UNRESOLVED in code.

## 2. Carrier

### 2.1 `0x14212e970` + receiver-helper cascades (OBS atk4.c/atk5.c)

`FUN_14212e970(ctx,angle,slot)` (OBS full body): the sub-kind gate's room
check. (1) If carrier speed > 15.0 (`15 @0x14259be0c`) with ≥ 3 marks
(`FUN_140a58320(·) > 2`, OBS body = `+0x12a4 < 5 ? +0x12a4 : 0` — a mark-count
byte): angle-vs-carrier-facing `FUN_14043a0a0` must be ≤ 30°
(`30 @0x142591140`), else return (fail). (2) If unowned (`ctx4+0x6c == 0`) or
`ctx4+0x3c ≤ fps·0.5+0.5` (= 27 f): build corridor `FUN_14063d3c0(...,6)`,
reach `FUN_140a8f860`, room `FUN_140a4ddc0`; need room ≥ `reach·1.2`
(`1.2 @0x14259d5d4`); step `reach·1.2` along the angle
(`FUN_140a8f7c0`); then per-teammate loop (11): skip self slot, skip dead
(`G+0x1748` alive), project teammate pos by `frames·vel`
(`frames = ctx4+0x3c` as float): if any teammate's projected point is within
`reach·1.2` of the stepped point (`FUN_140a8f860 < reach`) → fail (return).
I.e. **the target lane must be clear of teammates' predicted positions**.
Port-ready (pure function of ctx4, carrier, teammates).

Receiver helpers (OBS atk5.c; all port-ready at the stated level):
- `FUN_1421265c0(ctx,out)` — goal-front receiver picker (≈5 KB): scans the
  `FUN_140507740` candidate table (sorted by `+4` distance83); per candidate:
  receiver status must be 0xb (`FUN_142118d20`), record valid
  (`FUN_142118c90`), body-block (`FUN_140a53df0`-family via `14063cd90` lane
  + `0x1421b7d40` commit), pressure-entry scan (`FUN_14069a3e0`), skill-12
  (ONE_TOUCH_PASS) awareness bonus. Writes best `{slot, score}`. Full body
  open (loop nests); head/contract OBS.
- `FUN_142126a80(ctx)` — wall-pass picker (OBS full body ≈1.2 KB): iterate the
  `FUN_140a714e0(line,+0x14)` sorted mate list (`FUN_142124f90` length);
  first mate passing `FUN_142124950` wins; else 0xff. Port-ready.
- `FUN_142126b70(ctx,a,b,c)` — safety/back picker (≈23 KB; OBS contract):
  two-phase scan over the `FUN_140507740` table with the `FUN_142118d20/
  8d40/8c90` record battery, `FUN_140a4e720` pressure races, side/angle
  windows; returns best slot or 0xff. Structure OBS; full body open.
- `FUN_142128490(ctx,flag)` — safety-lane picker (≈6 KB; OBS contract): entry
  gates (pressure-count `FUN_14069a3e0` vs `fps·3+0.5` window; line-class
  `FUN_140a714c0` + `0x14259ce40` gate; carrier-side test), then the
  `0x140507740`-table scan with per-candidate speed (`5.0 @0x14259222c`),
  facing (`FUN_14043a0a0` vs `70° @0x14259be4c`), and record-0xb tests.
  Structure OBS; full body open.
- `FUN_142124cc0(ctx,slot,flag)` — lead-angle validator (OBS full body ≈3.5
  KB): needs `6 m` (`6 @0x14259bdec`, or `3 m` `@0x14258c264` with COM 6
  LONG_PASS) ≤ mate-lead distance ≤ `2025 m²` (`2025 @0x1425cb494`);
  folds both bearings ±180/360; needs `|Δlead| ≤ 289°`… (literal:
  `289 @0x1425c48d4` — INF(low): unit mismatch, likely scaled degrees or a
  different quantity; flagged); then `|Δ| ≤ 90°` (`90 @0x14259be5c`) with the
  sign rule; writes the lead point via `FUN_140470d60`. Port-ready modulo the
  289-unit flag.
- `FUN_1421240b0(kind,ctx,a,b)` — long-pass validity (OBS contract ≈11 KB):
  the sub-kind-7 runner veto skip (carrier-ai §4.3 reconfirmed in body:
  `player+0x5d0 ∈ {0x1e,0x1f}` with `T+0x28c+0x40·s == 7` skips the
  proximity/pressure vetoes), `FUN_14063cd90` corridor + `FUN_1421b7d40`
  commit, `fps·ramp+…` frame budget. Structure OBS; full body open.
- `FUN_142124950(ctx,slot)` — receiver-side validator (OBS full body ≈3.5
  KB): corridor `FUN_14063c560`, `FUN_1404c8ea0` duel distance,
  record-0xb + kind-0x25 exclusion, `15 m` (`@0x14259be0c`)/`110 m`
  (`@0x14259d628`) gates, ownership/phase tests, speed-gated angle window
  (`120° @0x14256ae30` / `80° @0x142596e6c`, `15→130°` ramp
  `@0x14259be0c/@0x1425a9bf0` divisor `@0x1425c4900`), `5/10 m`
  (`@0x14259222c/@0x142588210`) vs `3 m` (`@0x14258c264`) reach window,
  `FUN_140439fe0` bearing races. Port-ready.

GF replaces: `AI_GetPassRatings`/receiver enumeration in
`AIsupport/AIfunctions.cpp` — the veto-skip, wall-pass, and validator chain
above.

### 2.2 `0x142133ed0` / `0x142133cd0` — OnePattern path (OBS atk4.c)

- `FUN_142133ed0(ctx,ans,plan)` precondition switch on
  `FUN_14234e8e0(G+0xec) − 4` (pattern index 0/5/6/7/0xc, i.e. set-piece
  pattern ids; OBS): case 0: needs `FUN_140a610b0 == carrier`-family, writes
  `{type 3, sub 5/6, target}` via `FUN_140474c40`; case 5: writes
  `{type 3, sub 4}`; case 6: writes `{type 2}` when matching `*plan`;
  case 7: **RNG pick** (`0x141b34aa0/0x141b34ad0` generator, xorshift observed
  inline) among alive mates, writes `{type 2, sub = rand, flag = rand&1}`;
  case 0xc: nearest-mate (`≤ 6 m²`, `6 @0x14259bdec`) → `{type 2}`, else
  `{type 3}` to the pattern point. Returns nonzero = precondition holds.
  The case-7 RNG is the only RNG draw in the OnePattern path (slot open —
  generator not the match RNG; INF(med)).
- `FUN_142133cd0(ctx,ans,plan)` emitter (OBS full body ≈120 lines): needs
  `plan[4]+0x14` age > `fps·4` (= 216 f); pass 1: scan the `T+0x22e4c`
  per-player sorted distance lists (carrier-ai §6.2's `+0x22e4c` note
  reconfirmed in use) for the nearest alive non-self mate within `0x1425c5258`
  (raw value unread — open) with record `FUN_142118c90 > 2` → `{type 2,
  sub 0/1}`; pass 2: scan the `FUN_140a714e0(line,attackSign)` sorted list
  the same way; else if `FUN_140a4c8e0` → `{type 5}` (clear). Port-ready
  modulo the `0x1425c5258` constant.
- **OnePattern item closed** at the dispatch level; leaves: `14234e8e0`
  pattern-index source, `0x1425c5258` distance cap, case-7 RNG slot.

GF replaces: new OnePattern branch (set-piece-pattern-gated; currently no GF
counterpart).

### 2.3 Lane primitives (OBS atk4.c/atk6.c)

- `FUN_14063de20(...)` — thin packer (OBS full body ≈30 lines): copies the
  from/to points + `len/wid/h/flags` into stack slots, calls
  `FUN_14063dc10`. No geometry itself.
- `FUN_14063dc10(ctx,id,lane,...)` (OBS head, atk6.c): zeroes two 11-entry
  `0xff` arrays, flips side (`bVar5^1`), calls `FUN_14063cd90`. I.e.
  lane-build = `63cd90` with side flip.
- `FUN_14063cd90(out,ctx,id,side,...)` (OBS head ≈5 KB in atk6.c; full body
  open): writes `{u32 best, u32 score}`; scans opponents/records with the
  `FUN_140698cc0/14063c560/140698d10/14069a3e0` battery (same battery as
  `1265c0`). Contract OBS; body open.
- `FUN_140a53df0(ctx,a,b,cls,lo,hi)` (OBS full body ≈20 lines): true iff
  `lo² < |a−b|² < hi²` (xz) **and** the bearing race
  `FUN_14043a0a0(FUN_140439ef0(b),cls) < 0.5·angleParam`
  (`0.5 @0x142565600`). I.e. **annular sector test** (between two radii,
  within half-angle of the class direction). Port-ready.
- `FUN_140a91120(out,a,b,t)` (OBS full body ≈25 lines): segment parameter
  `t = ((b−a)·t̂ − (b−a)·a)/(|b−a|²)` (projection of point t onto segment
  a→b, xz); returns `(t, flag)` with flag 0 when `|b−a|² < 0.667²`
  (`0.667² @0x14259bdc4` — degenerate segment). I.e. **closest-approach
  parameter**. Port-ready.
- `FUN_140a4ddc0(ctx,pt,angle)` (OBS head, atk6.c ≈80+ lines): builds the four
  pitch corners (x-sign/z-sign flipped by `0x102...`-style XOR — same side
  flip as §1.5), computes the four corner bearings `FUN_140439e50`, then
  classifies `angle` into quadrants with progressive half-pitch scaling
  (`·0.5`, `@0x142565604`). Contract: **room-to-edge along a bearing**
  (returns free distance). Body open past the head.
- `FUN_140a58320(p)` (OBS full body, 5 lines): `return +0x12a4 < 5 ?
  +0x12a4 : 0` — **mark-count byte** (caps at 5, returns 0 when ≥ 5).
  Port-ready.

GF replaces: `AI_CalculateFreeSpace`/corridor helpers — `53df0/91120/58320`
are drop-in; `63cd90/4ddc0` need bodies.

### 2.4 Record field meanings (OBS; closes most)

Think-unit record = the per-kind answer struct (BallPlayer+0x3E20+0x84,
0x1C B/kind; carrier-ai §6.1). Field uses OBS from atk4.c/carunits*.c:
- `+0xC8` (BallPlayer-relative, adopter rule 3): **held-answer stickiness
  flag** — `if (+0xC8 set) and held answer (BallPlayer+0x41e4) is Dribble
  (3) and k is Dribble: skip k` (carunits5.c:163–172: `*(param_1+200)` =
  +0xC8; `0x14063c9a0` dribble-class filter both sides; `0x142119df0`
  ripe-check on the held record; same kind → skip). I.e. +0xC8 = "dribble
  answer held, don't re-take dribble this walk". No live sample had it set
  (tier-5 instant ripeness). Meaning OBS (code), live value open.
- Think-unit instance fields (unit base +off, OBS carunits1.c):
  `+0x40/+0x48` predicted carrier point + speed; `+0x50` opponent list ptr;
  `+0x264` mark count (< 0x16 gate); `+0x298` desired angle (written by the
  pre-bend); `+0x29c` pre-bent flag (0/1); `+0x2a0` answer direction
  (init −1.0); `+0x280/0x28c` corridor endpoints; `+0x2b0` shield/fallback
  direction; `+0x2b8` shield-feint flag (0 = may feint).
- `ctx4+0x18` (carrier-ai open item): threshold `≥ 0.5` gates the
  PassRandomTest debug unit (carunits2.c:196: `ctx4+0x18 ≥ 0.5 or owned`);
  still INF(med): a debug-enable scalar. `+0x58/+0x64/+0x68/+0x70`
  (BallPlayer): `+0x58` nonzero → Long plan restricted to 10+10 skill terms
  (carrier-ai §6.2 row, OBS); `+0x64` = foot side INF (CurveShot side match);
  `+0x68` = Breakthrough mode (0/1/2 → 20/40/70); `+0x6D/0x6E/0x6F`:
  unread in this session — open. `+0x70` = team long instruction (0/1/2 →
  +0/20/40 Long score).

GF replaces: `BallPlayer`-equivalent state comments (no GF struct yet).

### 2.5 `w_spd` divisor at 0x14259d668 — closed (OBS raw)

`@0x14259d668` = **−15.0** (OBS disk read; neighbours −6.5/−12.0/−26.0).
Use (carunits1.c:547–549):
```
if (15.0 < fVar16) {                       # fVar16 = dribble value (speed-ish)
    if (fVar16 < 30.0)                     # 30 @0x142591140
        fVar15 = (fVar16 − 30.0) / −15.0   # = (30 − fVar16)/15
    else fVar15 = 0.0
}
```
So `w_spd = clamp01((30−spd)/15)` for 15 < spd < 30 — a **15-wide ramp**,
not 30-wide. The prior report's "`(spd−15)/30`, exact divisor needs one raw
re-read" is corrected: the divisor is −15.0 and the ramp is **(30−spd)/15**
(falls with speed: higher dribble-value → lower w_spd weight). The
companion `w_ang = (|Δ|−10)/40` (`(|Δ|>10) ? (|Δ|<60 ? … : 0) : 0`,
10 @0x142588210, 40 @0x14259be34, 60 @0x14259a764) stands (OBS).
`keep = dist + 6·w_box + 6·w_plan + 5·w_shoot`-family,
`dribble = 50·w_face + 60·w_spd` (50/60 @0x14259be10/0x14259a764) stand.

GF replaces: dribble economics in `pescarrier.cpp` — change `w_spd` to
`(30−spd)/15` clamped 0..1.

## 3. Live

Background sampler `live_wait.py` (sudo pread) is waiting for open play
(mode 1); at write time the match sits at menu/mode 2 (frame 53080, team
pointers null outside the tick — expected per the ticket's MI note).
When it fires it dumps: both teams' `+0x254/0x255/+0xb8a4/a2a0[11]`, the
support-object guess (`T+0x2d4` → `+0x718/+0x7b4/+0x40c/zone0`), and all 17
BallPlayers' idx/active/e4/adopted. Two items ride on it:
- Support-target check (O8 values): compare `+0x7b4` supporter distance with
  `FUN_1405e0c60`, zones `+0x4ec` vs live positions. Exact step in §1.8.
- Per-carrier Finishing byte read-back (carrier §6 strict-open): for each
  adopted-Shoot carrier, read `FUN_14124dad0(record)+0x1a` (Finishing id 0x18
  → block byte +0x1a; OBS carunits7.c:40) and compare with the Rn-implied
  integer (Rn = 18+3·clamp01((F−70)/29)). Note the shoot-zone actually reads
  `+0x1a` labelled Finishing in the accessor table (0x141f0fac0 → id 0x18);
  the sampler must resolve `0x14124dad0` (a PMS-record accessor) live — its
  address is known; the call itself cannot be made read-only, so walk: PMS
  record = `FUN_140a489f0(i)+0x70`-region copy? NO — the proven path is the
  shoot-zone's own input `uVar5 = FUN_140a489f0(...)`, then
  `FUN_14124dad0(uVar5)+0x1a`. Both helpers are pure readers; reimplement
  their pointer chases in the sampler (open: `14124dad0` chase not yet
  traced — first dereference its disassembly). **Both live items open,
  unblocked, waiting on open play.**

## 4. GF code each finding replaces (summary)

| finding | GF code (src/sim) |
|---|---|
| §1.1 Chance dispatch + 6067c0/6065b0/603830/604490/603d00 | `SelectAttackingRunPlayer` Chance branch (new dispatch; chanceScore 40/69) |
| §1.2 no cross-runner target reads | none (documents that no lane-reservation port is needed) |
| §1.3 spaceRun key map + 5c0f00 | `GetAttackingRunThreshold` table |
| §1.4 stop-test tail gates/constants | `ApplyAttackingRun` end conditions |
| §1.5 f0–f2 = instruction slots 0–2 (holder side) | `GetSupportPosition_ForceField` flag inputs |
| §1.6 no second 0x1e writer | none |
| §1.9 LineBreak/PullAway vestigial; GoalGet/CenteringGet/Overlap starters via `a42940` | new starter branches + merged setter |
| §1.10 roster byte + skill-13 + 75/95 ramp (label INF-low) | `GetAttackingRunAffinity` weight input |
| §2.1 12e970 + receiver cascade | `AI_GetPassRatings`/receiver enumeration |
| §2.2 OnePattern dispatch | new set-piece-pattern branch |
| §2.3 53df0/91120/58320 drop-ins; 63cd90/4ddc0 bodies | `AI_CalculateFreeSpace`/corridor helpers |
| §2.4 +0xC8 + unit fields | `BallPlayer`-equivalent state comments |
| §2.5 w_spd = (30−spd)/15 | dribble economics in `pescarrier.cpp` |

## 5. Open (exact next step each)

1. O7 Chance-order live log (needs open play; §1.7).
2. O8 support values (address rule §1.8; sampler written, needs open play).
3. Finishing byte read-back (§3; needs `14124dad0` chase + open play).
4. Chance sub-bodies `59ba60/59ed10/59dc20/673ad0/6718f0/6725f0` full
   reduction (skeletons + constants here).
5. Stop-test leaves (~50 `68xxxx`, §1.4 list; batch-decompile on demand).
6. spaceRun other-reader keys (11 readers; offsets now mappable via §1.3).
7. Receiver-helper bodies `1265c0/126b70/128490/1240b0` full reduction.
8. `63cd90/4ddc0` lane bodies; `124cc0` 289-unit flag; `133ed0` RNG slot +
   `0x1425c5258` cap; `61e690/61b590/617ca0/640cc0/641690/641f80` starter
   leaves; `+0x6D/0x6E/0x6F`, `ctx4+0x18` live values.
9. Awareness-byte semantic label (INF-low; needs a labelled roster or the
   tactics-loader decompile) + Counter `+0x14` byte ditto.

## 6. Live outcome 2026-10-14 (OBS)

The `live_wait.py` sudo-pread watch ran 295 s: first read mode 2 / frame 53080
(post-match menu — team pointers already null outside the tick), then MI
collapsed to 0x1 (mode/frame 0 = no match object) for the rest of the window.
A same-day re-read confirmed G = 0x0 (process at menu). Per the ticket the
match was never resumed. Consequence: §3's live items (O7 Chance-order log, O8
support values, Finishing byte read-back) stay open with the steps stated; the
support-object address rule (§1.8, `rsi+0x2d4`) and the `14124dad0` chase plan
(§3) are the handoff. A re-run needs a live open-play match and the two
scripts in the AttackLeftoversRE scratch dir.

## 7. Live section 2026-10-14 p.m. (match running, mode 1) — AttackLiveRE

Read-only sudo pread of the running match (`G = 0x2d4d9570`; MI tick-gated:
null between ticks, polled). Scratch: the AttackLiveRE scratch dir
(`live_main.py` + `live_main.json`: 47,216 MI-hit records over frames
16,302–22,085; `o7*.py`, `sup_scan*.py`). Tags: **OBS** = bytes/data read;
**INF(x)** = inference with confidence. No player or team names.

### 7.1 O7 — Chance mask/score live log: CLOSED (OBS)

Log: `live_main.json` (47,216 MI-hit records, frames 16,302–22,085, modes
1/2/3) + targeted watch `o7take.py`. Selector attribution OBS: `0x1a9a5b70`
→ team 0, `0x1a9ac970` → team 1 (score burst coincides with that team's take).

- Score-array layout corrected (OBS disasm `0x1406021a0–0x14060223c`): entries
  are 8 B `(i32 key, f32 score)`, i.e. `sel+8+8·i`, NOT 11 packed floats.
  The report §1.7's "`+8..0x68`" read as `<11f` was a mis-framing: even words
  are keys (≈0.0 as float), odd words are scores. The prior ticket's "score
  order" analysis must use stride 8.
- Key encoding (OBS disasm `0x1406021a0`: `key = [r13+0x18]·11 + slot`).
  Live: take keys equal the take slot (`key = slot`, Cls-0 tick, e.g. fr 30553
  `(key 9, 134.2)` → slot 9) or `slot+11` (Cls-1 tick, e.g. `(key 20, 131.8)`
  → slot 9; `20−11 = 9`). Take slot = `key mod 11` (OBS: all key-linked
  takes). The Cls word varies per tick (AI class of the evaluating context;
  team-0 takes seen with both Cls 0 and Cls 1). INF(med) on the Cls source
  (`r13+0x18` — the register, not the raw `T+0x18` word: live `T0+0x18 = 1`
  / `T1+0x18 = 17` do NOT match the per-tick Cls).
- Sort-then-take (OBS disasm + live): `qsort(sel+8, 11, 8B, cmp)` @0x140602298
  sorts best-first; the take loop @0x140602260 takes the first entry whose
  `FUN_140a85aa0` gate passes. Live: every take = the entry-0 (best-score)
  candidate — 14 takes in `live_main.json` (12 team-1, 2 team-0, all mode 1,
  all `poss = 01`, L = 2), plus 5 key-linked takes in `o7take.py`
  (e.g. fr 30553: entry `(key 9, 134.2)` → slot 9 takes; rest 0.0).
- One take per (frame, team) (OBS): no multi-take tick in 47,216 records.
  Multi-candidate ticks show DESC scores (fr 20546: 107.9/103.7/101.8 at
  entries 0/1/2) with a single take — the cap-release step (§1.1 step 4)
  confirmed live: max concurrent Chance 1 (team 0, cap 3) / 2 (team 1,
  cap 1 → 1,048 over-cap records = the one-past-cap take released next tick).
- Score magnitude (OBS): 86.9–141.4, consistent with `100 + s·x`
  (sub-kind-E −3/−5 adjustments explain < 100). Mask word `sel+0x284`
  (`mask[1]`): live values `0xec59936d` (team 0) / `0x4fc47f50` (team 1);
  bit semantics NOT per-slot-candidate (take slot 2 takes with bit 2 clear)
  — mask[1] is the veto battery output, not the candidate set. INF(med):
  candidacy is decided inside the mask call, mask[1] records vetoes.
- Parity (OBS): 11/12 team-1 takes on even frames, team-0 takes 1 even/1 odd
  — consistent with per-team parity ticks (O6's stale-timer explanation
  stands; no second writer needed).

GF replaces: `SelectAttackingRunPlayer` Chance branch — sort 8 B
`(key = Cls·11+slot, score)` entries best-first, take entry 0, one take per
tick per team, cap-release of the excess next tick.

### 7.2 Finishing byte read-back per carrier: CLOSED (OBS)

- Chase verified end to end (OBS disasm): `FUN_140a489f0(G, id)` =
  `*(G + (id+0xf3)·5·8)` (guard `id < 0x16`), `FUN_14124dad0(pms)` =
  `pms+0x70` (address), Finishing = `*(pms+0x70+0x1a)` = `*(pms+0x8a)`
  (carunits7.c:40 `140698d10` shoot-zone read). No call needed — pure
  pointer chase, read-only safe.
- Live (OBS, 42,393 hits): carrier `= 0` in 90.3% of MI-hits
  (`FUN_140a627e0(G,0)` = `*(G+0x18)+0x2601c`); PMS `= 0x1ccaf118` stable;
  byte `*(pms+0x8a) = 0x4d = 77` constant. I.e. carrier slot 0's
  Finishing = 77. The Rn cross-check (`Rn = 18+3·clamp01((F−70)/29)` →
  `18+3·(7/29) ≈ 18.7 → 18`) needs the shoot-zone's live Rn, which is
  per-tick scratch (not logged) — the byte itself is confirmed, the
  formula comparison stays INF(med) until an adopted-Shoot tick is caught
  with both values.
- GF replaces: nothing new — the accessor path is confirmed for the port's
  stat reader; wire `pms+0x8a` with the chase above.

### 7.3 Off-ball Chance dispatch live (cheap check): CLOSED (OBS)

- Run-start census over frames 16,302–22,085 (OBS `live_main.json`, starts
  dated from `T+0xa2cc` timer blocks): team 0 — Chance 2, Counter 2, Diagonal
  1, misc 236 (mostly 50/52/54/58/61-state churn + set-piece states);
  team 1 — Chance 12, misc 155. Zero LineBreak (0x20) / PullAway (0x2f)
  starts — corroborates §1.9 (vestigial in open play).
- Chance takes fire only in mode 1 with `poss = 01`, L = 2 (OBS: 14/14).
- Which run types fire and how often is dominated by non-Chance state churn
  (50/54 = base/mark states); among attacking runs Chance is the most
  frequent starter for the possessing team (12 vs 2/1 for Counter/Diagonal
  on team 1; team 0 was mostly out of possession in the window).

### 7.4 O8 — support target values: OPEN (address rule advanced, values unlogged)

Statically since §1.8 (all OBS disasm this session):

- `FUN_1405db800` callers pass `rcx = rsi+0x2d4`, `rdx = rdi` (team-AI),
  `r8 = [rsi+0xba8]` (slot-side object P), `r9 = tracked` — at `0x14067b8ee`
  and the whole `0x14067b640`-family (`mov rdx,[rsi+0xba8]` + `lea rcx,
  [rsi+0x2d4]` repeated 8× in that function alone). So the support object
  really is at shape+0x2d4 (the §1.8 rule, now with 8 call sites, not 2).
- The enclosing `0x14067b640(rcx = team, rdx = shape, r8 = tracked)` is called
  once per tick from `0x1405ecc2a` with `rcx = r15` (the shape register).
- Zone rects confirmed support-relative (OBS `0x1405ea896`:
  `[rsi+slot·0x14+0x4ec..0x4fc]`, `rsi` = param_1 = support, inside
  `FUN_1405e9f50`); stored cells are `[support+0x48f4]` in open play but
  `[P+0x3f04]` otherwise (OBS `0x1405ea3b3/0x1405ea3bb` branch on
  `MI+0x116c == 1`); carrier/passer live at support `+0x718/+0x40c`
  (OBS `FUN_1405db800` head writes via `FUN_140a627e0`).
- `T+0xba8` is NOT the slot array (OBS live: reads back `0xff000000ff` =
  pin-pattern u32 data, not a pointer) — rsi (shape) is a different object
  from T. T-neighbourhood scans (T±0x2000/0x4000 snapshot in-tick) find no
  X with `[X+0xba8]` pointing at live pitch points; full-heap back-pointer
  scans for the fingerprinted P object (`+0x7e24` per-slot u32s) find no
  referrer at `+0xba8` — P's content is stable for 20+ min (role tables,
  not per-tick data), so the live P address set goes stale between scans.
- Heap fingerprint scans for the support object itself
  (`+0x718/+0x7b4/+0x40c < 22`, `+0x7b8`/`+0x7e4` near current frame, zones
  sane) return only dead-pool objects (zones zero) — the live object's zones
  were never observed nonzero in any scan window.

Remaining step (exact): in open play, break the staleness by doing ONE
atomic snapshot per tick — `T`, `P` (`+0x7e24` fingerprint), full-heap
`[X+0xba8]==P` back-pointer, and `X+0x2d4` support read in the SAME MI-hit
(`sup_wide.py` + `sup_back.py` do the pieces but across different ticks;
merge them into one loop). If P still has no referrer, rsi is stack-passed
per call (not heap-linked) — fall back to logging `rsi` at the
`0x14067b8ee`-family call via a code breakpoint under a debugger (needs a
writable-debug session, not read-only pread). The match has now ended, so
this waits for the next live match. Ready samplers: `sup_wide.py`,
`sup_back.py`, `sup_inv4.py` in the AttackLiveRE scratch dir.

### 7.5 Status close-out (two live windows)

Live items 1–3 of the ticket: O7 CLOSED (§7.1), Finishing byte CLOSED (§7.2),
off-ball Chance dispatch CLOSED (§7.3). Support values OPEN (§7.4). §5 items
1–3 are superseded by §7.1–§7.4; §5 items 4–9 (static bodies) untouched.

Second window (new match, `G = 0x2d3115f0`, mode 1 frame 15,422 → 42,039):
the atomic per-tick snapshot (`sup_atomic.py`/`sup_atomic2.py`: T + P
fingerprint + `[X+0xba8]==P` back-pointer + `X+0x2d4` support read in ONE
MI-hit) ran ~10 min + ~6 min with zero wins — partly sampler bug (first
version re-read MI between phases so the tick lapsed; fixed version
`sup_atomic2.py` was killed when the match ended). No support values logged.
The match ended again before a third window; O8 waits for the next live
match with `sup_atomic2.py` ready.
