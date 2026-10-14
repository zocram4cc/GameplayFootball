# PES 2021 receiving the ball — ReceivingRE

Tags: **OBS** = decompile/raw at the VA or a live read; **INF(conf)** = inference.
Notation: `G = *(0x1436F3DC8)`, `MI = *(G+0x270)`, `T` = team record from
`FUN_140a48a30(G, side)`, `P(i) = *(G+(i+0xbf)·0x28)` (pos `+0x55c`, heading
`+0x554`, AI action `+0x5d0`), `hu(i)` anime (`+1` id, `+0x1a` clip frame,
`+0x1c` contact frame), `ai(i)` published AI record, `MA` = analysis object
(`ctx+0x18`), `F = *(G+0x2aa8)` (FieldInfo), `fps = FUN_141e532d0()` (54).
Scratch: the ReceivingRE scratch dir (`r1.c`–`r8.c` decompiles, `liveR.bin`
54 Hz capture + `recv.py`/`pg.py`/`lock2.py`/`icept.py`/`duels.py`).
Decompiles are hypotheses; branch senses checked against `pdis.py` raw where noted.

Prior work this extends: `pes-re-transitions.md` §4–§5 (receive = reaction
entry 3, request kinds, PASS_GET routes, trap.json lockouts), `pes-re-defender-decisions.md`
§1 (reaction chooser `FUN_140a06dd0`, 17-entry first-true table).

## 0. Summary

1. **`FUN_140933ad0` (reaction entry 0) is NOT the receive.** OBS raw + decompile
   (`r1.c`): gate `pc+4 > 0x15 → false`, null ctx → false, then
   `FUN_140844cb0(anime,…)` (contact/stagger-fall/dodge chooser); false → return
   false. On true it writes `cmd+0x874` = the contact result kind, `+0x878` via
   `FUN_140a6d420`, and either (anime 0x15 DODGE) the dodged player's position
   into `cmd+0x8b0`, or the contact push data into `cmd+0xff8…0x101c`. It never
   reads the ball, never issues TRAP/DRIBBLE-touch. TransitionsRE §4.1's
   correction of DefenderDecisionsRE §1 stands; DefenderDecisionsRE's table row
   ("entry 0 = ball receive") is wrong.
2. **The receive is reaction entry 3 `FUN_140933ad0`'s table neighbour
   `FUN_1409f8220`** (OBS `r1.c`): refusal gates, then a data-driven
   DRIBBLE-touch (AI 3 / anime 0x5d) or the TRAP request path (§1).
3. **Who gets PASS_GET: the player movement selector `FUN_140933450`**
   (OBS `r6.c`), called from the per-player movement builder `FUN_1409307c0`
   every logic frame. It picks one of 8 movement states (0 clear … 7); **state 2
   = go to the ball as the receiver**. It selects state 2 when the team's pass
   target `T+0x78` is this player (`FUN_140931c60`), else falls through
   kick-chase / loose-ball / carrier-approach tests (`FUN_1409319a0`,
   `FUN_140931360`, `FUN_1409316e0`). The `ActionPassGet` object then runs the
   seven-route walk (§2).
4. **Approach (ActionPassGet `FUN_140945ec0`, OBS `r4.c`):** per-frame precompute
   (seconds-since-event, pass-target latch, receiving point, arrival frame),
   then the route walk in fixed priority: ThrowIn → NextPlay → BallAvoid →
   MovePositionAdjust → MoveStraightTrapStop / TargetEnemy → BallFollow.
   Receiving point = ball prediction at the arrival frame
   (`FUN_140a4d340`), clamped 0.163 m inside the lines (`FUN_14094e3a0`).
   Live (§5): 40 PASS_GET episodes, 0.02–2.94 s; **15 of 16 traps start 1 frame
   after PASS_GET ends** (1 × 2 frames).
5. **First-touch choice (`FUN_1409f8a50`, OBS `r1.c` + helpers `r3.c`):** kind in
   `R[0]`: **3** = one-touch into the carrier brain's pre-decided kick (target =
   the kick's target); **2** = running trap (dead for AI: needs speed ≥
   `neutralTrapAutoFront` = 100 km/h live); **4** = pad-forced; else the
   interception rule (`FUN_1409fa0b0`) or the planned direction (5 m touch along
   `cmd+0x5d4`), the let-it-run order (kind 0xb → kind 0), DELAY/PRESS → kind 0;
   then the turn-limit clamp (`FUN_1409fbce0`): out-of-`busyTime` or slow ball →
   no change; fast non-own-team ball → turn allowance 90° when running /
   **−1° when stationary, i.e. a stationary interceptor's touch is forced to
   kind 0 (control, no turning touch)**; kind 4 → 3 within the allowance.
6. **Interception touch (OBS live, §5):** the defender who reaches a loose ball
   issues **the same TRAP (anime 7, contact frame 18)** — not a tackle, slide,
   or block. 5 duel traps show the receiver in PASS_GET → TRAP while the
   opponent stands in PRESS (52) or chases (51); one trap lasts a single frame
   ( immediately dispossessed). No separate interception-touch clip path was
   found in the receive chain.
7. **Re-touch lockouts (`FUN_1409fa720`, OBS `r2.c`):** base `L` from trap.json
   (live): offence 0.05 / defenceKeep 0.26 / defence 0.16 + 0.1·(1−s²) /
   shoot 0.5 / shootSpeedBall 0.75, s = clamp((BallControl+0x1e−40)/59);
   after own kick ×3, shortly after own touch ×2; shot-through rule (own-team
   shot ≥ 35 km/h within 30°+½ goal-mouth → no trap). **Not live-settled:**
   `liveR` ring-0 records are empty (stale history handle), no two traps by one
   player within 400 frames, so no minimum-interval observation (§5.4).
8. **No RNG** in any receive body decompiled here (OBS for the listed
   functions; INF(med) for undecoded callees).

## 1. Entry 0 vs entry 3 (OBS)

Reaction table @0x14263ec60 (OBS image read, 17 entries `{fn, flagA, flagB}`):
`0: 140933ad0 / 1: 14096f870(TACKLE) / 2: 140971b60(SLIDE) / 3: 1409f8220 /
4–7: dodges / 8: 140a08ea0(BLOCK) / 9–16: over-taken, clear, jostle, restarts,
walls, throw-ins`. Entry 0 runs before entry 3 every frame.

### 1.1 `FUN_140933ad0` — contact reaction, not receive (OBS `r1.c` + raw)

```
if (pc+4 > 0x15) return false                    // 0x140933adc cmp, jbe
if (ctx == 0) return false
if (!FUN_140844cb0(anime, tmpA, tmpB)) return false
cmd+0x874 = tmpB.kind; cmd+0x878 = FUN_140a6d420(ctx, pc+4, kind)
if (animeOut == 0x15 DODGE): cmd+0x8b0 = dodged player pos; cmd+0x1098 = id
else: cmd+0xff8 = 1/2, +0xffc = kind, +0x1000/+0x100c = push vectors, +0x1018 = id
return kind != 0
```

`FUN_140844cb0` (TransitionsRE `t2a.c`, OBS): refuses when not in open play,
when the anime action is 0x11/0xf; issues 0x58 STAGGER / 0x59 FALL_DOWN
(opponent within 1.5 m, speeds, stat 0x28 thresholds) or 0x15 DODGE. No ball
input anywhere in either body. KILLED (again): "entry 0 = ball receive".

### 1.2 `FUN_1409f8220` — the receive (OBS `r1.c`)

Refuse if: `P+0x5d0 == 5 PASS_GET` with `ai+0x1457 == 3` or `ai+0x1458 == 3`;
in a kick clip before its contact frame (unless order byte 0x3a or
`cmd+0x19b9`); `FUN_1409fa400` false (§3); anime 0x5d already. Then:

- **Data-driven touch** (DRIBBLE 3 / anime 0x5d): only when `FUN_1409fbb40`
  (order kind 0x4a or `ai+0x14 == 5`) AND `FUN_1409fbba0` (ball ≤ 0.217 m high
  10 frames ahead — `0x1425a9bd4`; player 0.694/fps-s prediction within
  `38000/(fps·3600)` m of the ball next frame — `0x14263e398/0x142598590`;
  OBS `r3.c`); refused if planned direction `cmd+0xe48` differs from the
  analysis heading `FUN_140439f90` by ≥ 157.5° (`0x1425cca3c`). Live: **zero**
  DRIBBLE-touch starts in 5896 frames (§5) — this path needs a pad order.
- Else **TRAP**: `FUN_1409f8640` records the trap contact frame
  `R[9] = FUN_1406b3d60(anime, 7)`, builds the request `FUN_1409f8a50` (§4),
  asks the anime layer `FUN_1406b4580(anime, 7, …)`; on success
  `cmd+0x874 = 4 TRAP`, `+0x878 = 7`, target `cmd+0x8b0`, `cmd+0xe6c = 7`.
  Anime-refuse codes {3,7,8} with sub-kind 4 retry once with sub-kind 0.

## 2. Who gets PASS_GET — `FUN_140933450` (OBS `r6.c`)

Called at the end of the movement builder `FUN_1409307c0` (which itself runs
after the kick/restart/dodge branches), every logic frame. Output
`param_1+4` = movement state:

| state | meaning (INF) | selector |
|---|---|---|
| 0 | clear / no ball movement | fall-through, `FUN_140931c60` false etc. |
| 1 | kick chase / restart | `FUN_140931c60` true path (`*param_1 = 1; iVar14 = 2` → then state 2? see below), throw-in branch |
| 2 | **go to the ball as receiver (PASS_GET)** | `FUN_140931c60` true; ball within 1 m (`iVar14 = 3` is the arrive variant); `FUN_1409319a0` kick-chase true also lands state 2 via `*param_1 = 1` |
| 3 | arrive / within 1 m | ball-to-player < 1.0 m (`0x14256ae1c`) |
| 4 | default movement | fall-through |
| 5 | kick-class chase (`0x38–0x3d, not 0x3c` with `param_1+0x888 > 0`) | `FUN_1409319a0` adjacent |
| 6 | wait / restart hold | `FUN_140931af0` true |
| 7 | loose-ball race win | `FUN_1409316e0` true (`cmd+0x19b4` = target) |

Key selectors (OBS):

- `FUN_140931c60(pc, cmd, flag)` (`r7.c`): true when I am the team's pass
  target (`T+0x78 == me`, `lVar7+0x78`), not the kicker (`MA+0x2600c`), the
  newest ring-0 event is a pass to me (`FUN_140a60530` false = not my own
  kick), my anime can trap, and no team-mate owns the trap (the
  `MA+0x24840` 3-slot scan). The 135° (`0x14259be60`) / 3 m capsule test
  against the other target is the two-receiver arbitration.
- `FUN_1409319a0` (`r7.c`): kick-chase state 0x45 (ball flying): asks the anime
  `FUN_1406b4580(…, 0x45, …)`; refused → keep chasing.
- `FUN_140931360` / `FUN_1409316e0` (`r7.c`, not reduced): loose-ball and
  race tests feeding states 4/7.
- `FUN_140930fa0` (`r6.c` tail): the pre-empt gate — pad-receive orders,
  `pc+0x54` busy, movement states 0x16/0x12/0x1b/0x40 with kick/possession
  tests. CIS pad branches (order 0x3a etc.) are dead for AI-vs-AI.

The team-level writer of `T+0x78` (the pass target the carrier's kick aims at)
is the kick path, not decoded here (open §6.4). `FUN_140a42940/0x140a429a0`
(`r4.c`) are team-order writers (concept orders into `T+0xb748`), not the
player-level PASS_GET assigner.

## 3. Who may touch now — `FUN_1409fa400 → 1409fa520 → 1409fa720` (OBS `r2.c`)

- `FUN_1409fa400`: ball-distance table entry `MA+0x24710[…] ≤ 100`; carrier may
  touch only from anime 0x44/0x10/0x15; same-side conflict loop (another
  player's pending kick/trap contact within `param_4−2` frames, opponent
  0.2·fps from contact and closer, etc. — 22-player loop, not fully reduced).
- `FUN_1409fa520`: anime can start TRAP (`FUN_1406b4c10(anime, 7)`),
  `FUN_140a53f90` (keeper hold) false, not the kicker (`MA+0x2600c`); out of
  play only at listed restarts; newest ring-0 event must be ownable
  (`FUN_140a62f20`/`FUN_140a60300`).
- `FUN_1409fa720` — the **re-touch lockout** (frames since the newest ring-0
  ball event `+0x10` must be ≥ `fps·L`):
  - `*param_3` (my pending kind) < 0x16, same team as the event owner → base
    `L = offence` **0.05** (`+0x1bc`); else `L = defenceKeep` **0.26** (`+0x1b8`).
  - Loose ball (event owner ≥ 0x16): `L = defence` **0.16** (`+0x1b4**) +
    `0.1·(1−s²)`, `s = clamp((stat+0x1e − 40)/59)` (INF(high): Ball Control;
    `0x14259be2c/0x1425c48cc/0x1425cb2fc` = the 40/99/59 clamp ladder).
  - Event 5 (shot), or event 3 with ball height > 0.75 m at release
    (`F+0x18`): `L = 0.1·(1−s²) + shoot` **0.5** (`+0x1c0**) if ball ≤ 80 km/h
    (`0x142596e6c`), else + `shootSpeedBall` **0.75** (`+0x1c4`).
  - Event 1–3/0x10 by **me**: `L × 3` (`0x14258c264`); shortly after my own
    touch (ring-1 record, `FUN_140a3e150(H,1,1)` age < `fps·2L`): `L × 2`.
  - Shot-through: own-team shot, ball ≥ 35 km/h (`0x14259a760` = 35 with the
    3.6× `0x14259ce38` scale) within 30° (`0x142591140`) + ½ goal-mouth angle
    of the goal → return 0 (let it run to goal).
  - Then the 22-player conflict loop and the pad/jostle tail (not reduced).

Live values (OBS `cvals.py` 0x3e/0x35): lockouts as above; `busyTime` 0.5,
`checkMyBallPass` 0, `runTrapStopTurnAngle` 90, `stopTrapStopTurnAngle` −1,
`use` 1; passget `circleDist` 9, `ballArrivalFastSec` 1.0, `ballSpeedMin/Max`
4/42, `secMin/Max` 0/0.35, `paraMin/Max` 20/120, `ballHeight` 1.5,
`targetDist` 3, `openBodyMaxValue` 45, `passGetThroughNoTrap` 1,
`neutralTrapAutoFront` 100, `nextPlayEnable` 1, `positionAdjustEnable` 1 /
`Side` 0.3 / `SubMax` 35°.

## 4. First-touch choice — `FUN_1409f8a50` (OBS `r1.c`, helpers `r3.c`)

`R[0]` = kind (anime trap type; clips `trap_<kind>_<dir>_y<height>_<variant>`),
`R[1]` sub-kind, `R+8` target (y = −1 none), `R+0x14` ball at contact,
`R[8]` touch heading, flags `+0x35` (forced 4), `+0x36` (no direction),
`+0x37/+0x38/+0x39` (stop dead), `+0x3a` (open-body). Default kind 0, target =
own pos + 1 m along body heading.

1. **Pre-decided kick** (next-cmd anime, `cmd[1]` or `ai+0x14` is a kick class
   `FUN_140a6e340`): target = kick target `cmd+0xe8c`, **`R[0] = 3`**
   (one-touch / touch-into-kick).
2. **Interception** (`FUN_1409fa0b0`, OBS `r3.c`): pad-player cases;
   `P+0x5d0 == 1` → keep kind; ball event < 0.5 s old (`fps·50/100`) and ball
   > 8.33 m/s (`69.44 = v²`, `0x1425f1f24`) from the other team → kind =
   planned trap type `cmd+0x5c` if the carrier brain already holds a pass/shot
   whose target is within 45° (`0x14259be30`) of the predicted heading, else 0
   (or `FUN_140a75be0` by order byte: `P+0x3f4` set → facing bands 13.5/8/22°
   give kinds 0/1/3/4 — `0x1425a1ffc/0x14259bdf4/0x1425c48c0`).
3. **Own-team receive**: planned direction (`cmd+0x5d4 ≥ 0`) → target = own pos
   + **5 m** (`0x14259222c`) along it, kind from `cmd+0x5c`, flags
   `+0x38/+0x3a`; kind 4 forced by `FUN_1409fa330` (pad only, `cmd[0] ==
   0x1b`); order 0xb (let it run) → kind 0 along the order heading, `+0x36`;
   DELAY/PRESS (`P+0x5d0 ∈ {0x33,0x34}`) → kind 0.
4. **Running trap**: kind 2 (target 5 m along the 0.694/fps-s — `0x14259bdd0` —
   velocity prediction) only in anime MOVE (2) with speed ≥
   `neutralTrapAutoFront` 100 km/h → **dead for AI** (OBS live value).
5. Touch-line guard: ball within 25° (`0x14259be20`) of touch heading, body >
   135° (`0x14259be60`) away, 1.5 m-ahead point within 1 m of a line → kind 0.
6. **Turn clamp** (`FUN_1409fbce0`, OBS `r3.c`, gated on trap.json `use`): for a
   non-own-team ball (`checkMyBallPass` 0), within `busyTime` 0.5 s, ball >
   3.75 m/s (`0x14263e390` = 14.06 (m/s)²): allowed turn body→touch =
   `runTrapStopTurnAngle` 90° (running: facing within 45° of ball path and
   fast) else `stopTrapStopTurnAngle` **−1°** → **stationary touch forced to
   kind 0**; within the allowance kind 4 → 3. `FUN_1409f8870` (order-0xf
   touch-plan fix-up) and `FUN_1409f9600` (teammate/opponent conflict: same
   team → refuse; kick-clip progress, heading and distance arbitration —
   structure OBS, branch senses INF) feed the same decision.
7. Target == own pos with kind 0 and no `+0x36` → `+0x39` stop-dead.

There is no separate turn/shield/lay-off chooser: one-touch = kind 3 with the
brain's kick; turn = 5 m directional touch; shield/stop = kind 0. INF(high).

## 5. Live (OBS `liveR.bin`, 5896 records @ 54 Hz, frames 408–5963)

### 5.1 Traps: 16 starts, all TRAP/anime-7/contact-18

All with `P+0x5d0 = 4` at start (the entry-3 command). Touch direction (ball
motion contact→contact+12 fr vs body heading at start): own-team receives (n =
10 with a measurable touch): median |dir| 47.5°, 30 % within 30°, out-ball
median 3.4 m/s — directional turning touches. Opp-side receivers (n = 5):
median 10.0°, 60 % within 30°, out 2.3 m/s. Ball heights 0.11–0.37 m (all
ground traps). Mover speeds 0.3–7.8 m/s (receivers moving to meet it, §5.3).
No DRIBBLE-touch (anime 0x5d) start in the window.

### 5.2 PASS_GET → TRAP handoff: 1 frame

40 PASS_GET episodes (0.02–2.94 s, anime MOVE 2 / STEP 3). **15 of 16 traps
start 1 frame after the receiver's PASS_GET ends; 1 starts 2 frames after.**
The receiver steps to the ball in PASS_GET and the reaction fires the trap on
arrival — no gap, no overlap.

### 5.3 Interception touch: same TRAP clip

5 duel traps (nearest opponent 0.4–1.7 m at the start):
- f800 (p8 vs opp 0.6 m): receiver PASS_GET → TRAP (contact 18), opponent in
  PRESS (52) through the touch; receiver keeps it, passes at f817 (act 0x22).
- f2669 (p17, 1-frame TRAP, contact 6/18 at start): dispossessed on arrival —
  opponent (p5) traps it at f2683 instead. The interception touch is
  contestable in the same frame window.
- f2683 (p5): wins the ball off p17 with the same TRAP-18 clip, then dribbles.
- f2727 (p8, opp 1.5 m): holds off the chaser, keeps the directional touch.
- f2810 (p17, opp 1.7 m): receives under chase (opp act 51), keeps it.
No tackle/slide/block anime by either party around these touches; the JOSTLE
entry-11 path did not fire. The interception touch IS the plain TRAP.

### 5.4 Lockouts: NOT settled

`liveR` ring-0 records are empty (the reused history handle is stale for this
match: `ev = 0 × 14` at every probe frame), so frames-since-event cannot be
measured and no lockout minimum is observable. No player traps twice within
400 frames (min re-trap interval: p5 257 fr; p17 141 fr; all others none), and
no kick→trap within 400 frames (nearest: p9 kick 226 fr before his trap).
Per-frame capture around a goal-kick/corner restart with a fresh history
handle is still needed; the `samp.py` + `findhist2.py` path is verified
working (5896 contiguous 54 Hz records this pass).

## 6. GF counterpart (replaces)

| finding | GF code |
|---|---|
| §1 receive = entry 3 + §3 gates + §4 kinds (no RNG, no stat roll) | `PlayerController::_TrapCommand` (playercontroller.cpp ~425: 2 m/s ball gate, `Predict(200)` range gate, retainer gate, designated-possession gate) — GF has no per-kind choice, no one-touch-into-kick kind 3, no interception/turn-clamp rule |
| §2 PASS_GET assignment + route walk | Eliza receiver movement (`_GetPassingOdds` lead in elizacontroller.cpp ~1571 is the passer's aim, not the receiver's run); GF has no receiver-state machine — the `GetReceiverLeadTime_sec` lead is applied by the passer only |
| §4 5 m directional touch / stop-dead / turn clamp | `GetTrapVector` (humanoid_utils.cpp ~422: `GetBallControlVector` + height Factor, `ballMovementFactor` blend) + the touchable gate (`GetTrapTouchableDistance` 0.95 m, `GetTrapAcceptGateScale` 1.15 + 0.2 speed term, 1 m height gate, humanoid.cpp ~517–551) — GF's gate is wider and stat-free where PES forces kind 0 for stationary interceptions |
| §3 lockouts (0.05/0.26/0.16+0.1(1−s²)/0.5/0.75, ×3/×2, shot-through) | nothing in GF (no re-touch timer; GF's trap fires whenever the gate passes) |
| §5.2 1-frame PASS_GET→TRAP handoff | GF has no handoff (trap is a continuous command, not a state change) |

## 7. Open list

1. **Re-touch lockouts live** (needs: fresh `findhist2.py` handle for the
   current match, per-frame `samp.py` around restarts; ring-0 `+0x10` age at
   each TRAP start, min ≥ fps·L per kind).
2. **Stationary-interception kind-0 clip choice**: the anime-layer Trap row
   selection (`FUN_1406b4580` with 7; sim-motion-mapping scope) — which
   `trap_0_*` clip a pressed interceptor gets.
3. **`T+0x78` (pass-target) writer**: the kick path that aims a pass at a
   receiver (attack-decisions scope).
4. **Route bodies** (`MoveStraightTrapStop`, `TargetEnemy`, `BallFollow`,
   `ThrowIn`, `NextPlay`, `BallAvoid`, `MovePositionAdjust` slot-2 tests) and
   `FUN_140944d10` tail / `FUN_140931360/0x1409316e0` reductions.
5. **`FUN_1409f9600` branch senses** (teammate/opponent arbitration) and the
   §3 22-player conflict loop reduction.
6. **Trap target heights**: all 16 live traps ground balls; the aerial receive
   (`checkBallHeight` 0.3, `checkBallLimitRange` 0.5, reactionBallSpeed/Time
   ladder `0x130–0x170`) untested live.

## 8. Killed hypotheses

- "Entry 0 `FUN_140933ad0` = ball receive" (DefenderDecisionsRE §1 table row):
  it is the body-contact stagger/fall/dodge reaction (OBS raw + decompile).
- "The interception uses a tackle/slide/block/jostle path": 5 duel traps all
  use TRAP-18 with no duel anime (OBS live).
- "The data-driven DRIBBLE touch is the normal receive": 0 starts in 5896
  frames; it needs a pad order 0x4a (OBS live + code).
- "Running-trap kind 2 matters for AI": `neutralTrapAutoFront` = 100 km/h live
  (OBS) — unreachable.

## 9. Ledger entries

| ID | Claim | Conf. | Evidence |
|---|---|---|---|
| EV-RCV-0001 | Entry 0 = contact reaction (STAGGER/FALL/DODGE), never the receive | Confirmed (raw + decompile) | 0x140933ad0, 0x140844cb0; table @0x14263ec60 |
| EV-RCV-0002 | Receive = entry 3 `FUN_1409f8220` (DRIBBLE-touch or TRAP paths) | Confirmed (code + 16 live TRAP = action 4) | 0x1409f8220, 0x1409f8640; `liveR`/`recv.py` |
| EV-RCV-0003 | PASS_GET assigned by `FUN_140933450` state 2 via `FUN_140931c60` (T+0x78 = me) | High (code; T+0x78 writer open) | 0x140933450, 0x140931c60; 40 live episodes |
| EV-RCV-0004 | Approach = 7-route walk, receiving point at arrival frame, 0.163 m clamp | High (code structure + live handoff) | 0x140945ec0, 0x14094e3a0, 0x140a4d340 |
| EV-RCV-0005 | PASS_GET→TRAP handoff 1 frame (15/16; 1 × 2 fr) | Confirmed (live, n = 16) | `liveR`/`lock2.py` |
| EV-RCV-0006 | First-touch kinds: 3 = one-touch into pre-decided kick; 2 dead (100 km/h); 4 pad-only; else interception/planned-direction/let-run/DELAY-PRESS | High (code + live constants) | 0x1409f8a50, 0x1409fa0b0, 0x140a75be0 |
| EV-RCV-0007 | Stationary interception forced to kind 0 (stopTrapStopTurnAngle −1°) | High (code + live constants) | 0x1409fbce0; trap.json live |
| EV-RCV-0008 | Interception touch = plain TRAP-18, no duel anime | Confirmed (live, 5 duels) | `liveR`/`duels.py` |
| EV-RCV-0009 | Lockout formula (0.05/0.26/0.16+0.1(1−s²)/0.5/0.75, ×3/×2, shot-through) | High (code + live constants; live interval open) | 0x1409fa720; trap.json live |
| EV-RCV-0010 | No RNG in receive bodies | High (listed bodies; callees INF) | r1–r8.c |
| EV-RCV-0011 | 16-trap live mix: own-directional (median 47.5°), opp-control (median 10°), all h ≤ 0.37 m | Confirmed (live) | `liveR`/`recv.py`/`icept.py` |
