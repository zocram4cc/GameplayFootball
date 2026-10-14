# PES 2021's ball-carrier decision layer, ported to GameplayFootball

Port of `docs/re/pes-re-carrier-ai.md` (the decision core) and
`docs/re/pes-re-attack-decisions.md` §2 (the cross/through units and their choosers)
into GameplayFootball's carrier AI. §1 of the latter report is **not** used: it is
retracted there by §9.

Tags: **OBS** = decoded and observed at the given address; **INF** = inferred.
Every constant is read from the user's PES2021.exe at run time
(`pes::PeImage::ReadVa`); the few that could not be re-read carry `ponytail:` and
their report section.

## 1. What the port covers

| PES decision | GF function | Report section |
|---|---|---|
| Think cadence: default dribble/keep every tick; a full think when `frame − busyStamp > 5` | `ElizaController::GetOnTheBallCommands`'s `rethink` gate + `carrierBusyStamp` | carrier-ai §2 |
| Shoot zone with all its constants | `pes::ComputeShootZone` | carrier-ai §4.1 |
| "Just shoot" (`d < 20`, angle < 40), the box rule, the header rule | `pes::ShootSubKind` | carrier-ai §5.1 |
| Receiver enumeration, the openness/interception formula with its class thresholds | `ElizaController::GetOnTheBallCommands`'s lane loop, `pes::LaneOpennessMargin`, `pes::OpennessClassFromMargin` | carrier-ai §4.3 |
| Receiver difficulty term | `pes::ReceiverDifficulty` | carrier-ai §4.3 |
| PassForward's three tiers (short / through / lofted) | `pes::PassForward` | carrier-ai §5.2 |
| Plan selection order and the rolled start probabilities | `pes::kPlanPriorityOrder`, `pes::PlanStartProbability`, `pes::PlanRoll` | carrier-ai §4.4 |
| The adopter's shoot-before-pass order | `GetOnTheBallCommands` (shoot, then pass, then dribble) | carrier-ai §3.2 |

## 2. Where it lives

- `src/sim/onthepitch/pescarrier.hpp` / `.cpp` — the decision core: the constants
  struct, the loader, the shoot zone and its five predicates, the shoot decision,
  the receiver records and the openness ladder, PassForward, the plan table and
  the roll. Every function is pure, so `tests/sim/pes_carrier_test.cpp` exercises
  the whole layer against synthetic constants without an install and without a
  Match, alongside one install test that reads the constants out of the user's own
  exe and checks their relations (not pinned numbers).
- `src/sim/onthepitch/player/controller/elizacontroller.cpp` — the wiring. The
  carrier's state (the running plan, the two frame stamps) lives on the controller
  and is reset by `ElizaController::Reset`, like `lastDesiredDirection`.
- `MatchSetup::pesCarrierConstants` / `Match::GetPesCarrierConstants` — required
  like the clock's, the ball's, the judge's, the keeper's and the stamina
  model's: every number in the layer lives in the image, so a match built without
  them is refused. `PesLibrary` loads them, `gfsim_run` and the match tests pass
  them through the same `SharedCarrierConstants()` gate the stoppage constants
  use.

## 3. Each ported decision, with its inputs, constants and VAs

### 3.1 Think cadence (report §2)

PES's MainEval (`0x142110d30`) resets the think timer every tick the carrier is
inside a committed action (`state+8 bit0` and the action's `0x30000` property
bits, `0x140a6e320`), otherwise the Dribble unit answers every tick and a full
think runs once `frame − BallPlayer+0x4224 > (int)(gameSpeed×0.1 + 0.5)` = 5
frames (0.1 @0x142588200, 0.5 @0x142565600, OBS `0x142110fab..0x142110fc7`).

- **GF**: `carrierBusyStamp` is reset whenever the carrier's current function type
  is a committed one (pass, shot, trap, ball control — the closest set GF has to
  PES's `0x30000` property class). `nowFrame` is the carrier's possession
  converted from GF ticks to PES frames by `pes::PesFramesToGfTicks`, the same
  conversion `pesballphysics.hpp` applies to the ball's per-tick factors: one GF
  tick is 0.54 of a PES tick, so 5 PES frames (92.6 ms) is 9 GF ticks. The
  conversion is `1/(0.01×54) = 1.8519` GF ticks per PES frame, truncated.
- **Constants**: none read; the ratio is the tick rates, already carried by
  `pesball::kPesTick_s`.
- **RNG**: none.

### 3.2 Shoot zone (report §4.1, `0x140698d10` + `0x140699610`)

`pes::ComputeShootZone(ballX, ballZ, attackSign, finishing, longShotSkill,
halfLen_m, goalHalfWidth_m, k)`.

The ten range/angle immediates of the init, read with their opcode checked
(`c7 81 <disp32> <imm32>`, the immediate 6 bytes in):

| field | value | VA |
|---|---|---|
| `shootR0_m` | 11.0 | 0x14069964c |
| `shootR1_m` | 12.0 | 0x140699656 |
| `nearCentral_m` | 18.0 | 0x140699660 |
| `farCentral_m` | 24.0 | 0x14069966a |
| `nearSide_m` | 16.0 | 0x140699674 |
| `farSide_m` | 22.0 | 0x14069967e |
| `centralHalfAngle_deg` | 15.0 | 0x140699688 |
| `sideAngle_deg` | 40.0 | 0x140699692 |

and the .rdata floats of the builder (.rdata VA → value):

| field | value | VA |
|---|---|---|
| `finishingBoost_m` | 3.0 | 0x14258c264 |
| `longShotBoost_m` | 3.0 | 0x14258c264 |
| `finishingFloor` | 70.0 | 0x14259be4c |
| `finishingTop` | 99.0 | 0x1425c48cc |
| `finishingSpan` | 29.0 | 0x1425a9460 |

The ramp (`0x140698e0d`): below 70 the near range stays, at 99 or above it is the
full gain, in between `(x−70)/29` — which reproduces the live samples of report §8
(Rn 19.03 at Finishing 71, Rn 20.90 at 89). The far range gains 3.0 with skill
39 | 33 | 8 (`shr rcx, 0x27` / `0x21` / `8` at `0x140698dad..0x140698ddc`).

Outside the posts the distance is to the near post and the bearing of ball→post
against the attack direction decides the sector (`0x140698f65..0x140698fa4`);
inside them the distance is to the goal line and the bearing is 0
(`0x140698ec3`). Behind the goal line both band and sector go wide
(`0x140698fbf`). The band: the central range for the central sector, the side
range for the side sectors (`0x140698fd5..0x140699029`).

The five predicates, each read literally from its address:

| predicate | VA | rule |
|---|---|---|
| `InShootRange` | 0x1406997e0 | `d >= 0 && (bit0 ‖ (band<3 && sector<3))` |
| `IsCentral` | 0x140699710 | in range && `sector == 0` |
| `IsMid` | 0x1406997a0 | in range && `band <= 1` |
| `IsClose` | 0x140699820 | in range && `band == 0` |
| `InHeaderRange` | 0x140699750 | in range && `R1 >= d` |

`bit0` (+0x16C) is the ball inside the 180° half-disc of radius 11 m centred on
the goal centre: `0x1406990a3` writes the first of the zone's 13 boxes as a
kind-2 disc (radius `+0x188`, half-angle `+0x198` at `0x140698950`'s kind-2
branch) whose centre is `(s·halfLen, 0)`.

**GF inputs**: the ball's position (this engine's `y` is PES's `z`), the team's
attack sign, `technical_shot` for stat 0x1A Finishing, the skill mask of
`LongRangeShooting | KnuckleShot | DippingShots` for PES's 39/33/8, and this
engine's own Law-1 marks `pitchHalfW` and `goalHalfWidth` for PES's FieldInfo
half-length and goal half width.

### 3.3 Shoot decision (report §5.1, `0x142121920`)

`pes::ShootSubKind(in, k)` returns 1 (shoot) or 0 (no shot).

The gates, all read at their addresses:

| rule | VA | constant | VA of constant |
|---|---|---|---|
| first-time: incoming ball vs facing over | 0x142121c4d | 120.0 | 0x14256ae30 |
| first-time: ball→goal over 20 m (400) | 0x142121a3d | 400.0 | 0x1425a2670 |
| first-time: over 15 m (225) & body angle over & path over | 0x142121c7f / cc3 / cda | 225.0 / 70.0 / 7.0 | 0x14259be70 / 0x14259be4c / 0x14259bdf0 |
| "just shoot": `d < 20` | 0x142121ebd | 20.0 | 0x14259be1c |
| "just shoot": `angle < 40` | 0x142121ecf | 40.0 | 0x14259be2c |
| keeper present and settled | 0x142121ed8 | `> 21` skip | immediate |
| box | 0x1406996f0 (call) | — | — |
| above head: `ballY > 0.8 × height/100` | 0x142121a5e | 0.8 / 100 | 0x142572348 / 0x14253ee30 |
| header range | 0x140699750 (call) | 12.0 | imm 0x140699656 |
| plan 8 (ShotPassFromGoal) | 0x142121f4a | 8 | immediate |

**Ponytail approximations** in this decision:
- the lane test `0x14211faf0`, `central_eval` (`0x14211fe20`) and `wide_eval`
  (`0x142120e60`) are not decoded (report §9.1); without them a shot in range with
  the goal within 90° of the carrier's facing is the answer. The sub-kinds 2–5
  (chip, finesse, knuckle, volley) are gated on capability rows 0x15/0x16 and
  skill bits this engine does not carry, so the plain shot stands in and the
  action layer picks the animation from the ball's height.
- the keeper's "settled" test is GF's own (his current action is a movement),
  and the aim point is the goal centre biased to the post the ball's sector
  favours — PES's aim function `0x14211f090` is not decoded (report §9.1).

### 3.4 Receiver enumeration and openness (report §4.3, `0x1421174f0` / `0x142118700`)

The enumeration lives in `GetOnTheBallCommands`: the team's active players minus
the carrier and the keeper (PES's status codes 2 and 5), each with a record whose
target is his position a lane-time ahead (PES's record A, "to feet"; record B,
"into space", is not ported). Status codes 4 (predicted off-pitch), 6 (offside),
7–10 (busy) are approximated: a target off the pitch is skipped, the offside line
and the busy-action classes are not.

The openness formula's scored tail, `pes::LaneOpennessMargin`, is read literally
from `0x142118b76..0x142118c53`:

| constant | value | VA |
|---|---|---|
| `openAlongMin_m` (need along ≥) | 0.2 | 0x142598578 |
| `openAlongGain` (`T <= 0.6·along`) | 0.6 | 0x14257a190 |
| `openAlongPenalty` (`T <= 0.3·max(along−1,0)²`) | 0.3 | 0x142590d98 |
| the k² factor for `s > 1` (45°/90° ramp) | 45.0 / 90.0 | 0x14259be30 / 0x14259be5c |
| `T *= diff + 1` when diff > 1 | 1.0 | 0x14256ae1c |
| `openMarginScale` (×100) | 100.0 | 0x14253ee30 |
| `openLaneMargin_m` (`max(perp−1,0)`) | 1.0 | 0x14256ae1c |
| the opponent's look-ahead budget, `clamp01((d−1)/29)` | 1.0 / 29.0 | 0x14256ae1c / 0x1425a9460 |
| "in front": `|bearing − passAngle| <= 90°` | 90.0 | 0x14259be5c |
| the body-block corridor half width | 1.5 | 0x14259bddc |
| the sliding opponent's margin | 1.5 | 0x14259bddc |

The class ladder (`0x142117b81..0x142117bd6`) is read as the binary's
comparisons, which put the top boundary one step past the report's prose:
margin `<10 → 0`, `<40 → 1`, `<60 → 2`, `<100 → 3`, `>=130 → 4`, else 5. Live
validation matched it on 23,617 of 23,642 records (report §8).

The difficulty term, `pes::ReceiverDifficulty`, from `0x142117784..0x1421179db`:

| constant | value | VA |
|---|---|---|
| moving ball: `|v|·3.6 − 10) / 50` | 3.6 / 10.0 / 50.0 | 0x14259ce38 / 0x142588210 / 0x14259be34 |
| distance: `(dist − 10)/20` | 10.0 / 20.0 | 0x142588210 / 0x14259be1c |
| incoming angle: 1 at ≤90°, `(150−a)/60` to 150° | 90.0 / 150.0 / 60.0 | 0x14259be5c / 0x14259be68 / 0x1425c48fc |
| one-touch halves the moving-ball term | 0.5 | 0x142565600 |
| running: `(speed − 16)/12`, cap 28 | 16.0 / 12.0 / 28.0 | 0x14259be10 / 0x14259be04 / 0x1425a2004 |
| turn: `(|turn| − 60)/70`, cap 130 | 60.0 / 70.0 / 130.0 | 0x14259be4c / 0x14259be4c / 0x1425a9bf0 |
| high: `(y − 0.10869)/0.89131` | both | 0x142598958 / 0x142bef908 |
| weights 2 / 1.5 / 1.5 | — | 0x142565604 / 0x14259bddc / 0x14259bddc |
| `g(LowPass)`: 1 ≤ 80, `(99−x)/19` to 99 | 80.0 / 99.0 / 19.0 | 0x142596e6c / 0x1425c48cc / 0x1425c80e0 |
| the multiplier `0.7 + 0.3·g` | 0.7 / 0.3 | 0x14257a194 / 0x142590d98 |

The direction/turn classes (report §4.3): forward at ≤60°, square at ≤110°, then
back/turn (60.0 @0x14259a764, 110.0 @0x14259d628).

**Ponytail approximations**:
- the two body-block helpers `0x140a53df0` and `0x140a91120` are not decoded
  (report §9.1); without them no opponent is dropped from a lane, so openness is
  conservative — fewer lanes come out fully open. The lane look-ahead is the
  owned-ball value, 11 frames (`int(gameSpeed×0.2+0.5)`), not the ball-travel
  budget of the unowned case.
- the per-player pressure entry's distance to opponent slot 0 (INF the keeper) is
  the nearest opponent's distance; the report notes the lofted tier compares it
  against a squared 100–225, which never passes on a full pitch, so it stands in
  for that comparison.
- the offside line (deepest of the five last defenders and the ball) and the busy
  action classes are not enumerated: a keeper is excluded and an off-pitch target
  is skipped, nothing else.

### 3.5 PassForward (report §5.2, `0x14212a700` / `0x142125c80` / `0x142127ff0`)

`pes::PassForward(mates, opponents, in, k)` — the gate `(own ‖ press.cls < 2) &&
!close` (`0x14212a700`'s head), then the three tiers in order.

**Tier 1, short forward** (`0x142125c80`). The openness ladder of
`0x142125f9b..0x14212608b`, read literally:

```
if !danger && difficulty > 1.0: reject
state 0          -> need = fwdAngle > 60 ? 4 : 2
state 3 or 4     -> need = 2
else             -> need = fwdAngle > 120 ? 4 : (fwdAngle <= 30 && !0x140a4c8e0 ? 2 : 3)
dist2 < 900      -> need = 4
require openClass >= need
```
(60.0 @0x14259a764, 120.0 @0x14256ae30, 30.0 @0x142591140, 900 = 30²
@0x1425c65f4.) The pass-angle gate `|passAngle − planAngle| <= 60`
(0x14259a764), the `|ball.x − target.x| <= 35` advance gate (35.0 @0x14259a760),
the lane test's width `clamp(0.25·dist, 1.5, 6.0)` with a 220-frame budget
(0.25 @0x142592228, 1.5 @0x14259bddc, 6.0 @0x14259bdec, 220 @0x1425ca310), the
nearest-opponent windows 7/3 m (7.0 @0x14259bdf0, 3.0 @0x14258c264), and the turn
limit 80° (80.0 @0x142596e6c) are all ported. The winner is the most advanced
survivor.

**Tier 2, through** (`0x142127ff0`, attack-decisions §2.6). Gated on the
carrier's team-table free space over 40 (0x14259be2c's 40.0), then the distance
window `>= 5` and `<= 20` (15 when openness >= 100 and class < 4), and the most
advanced survivor. 5.0 @0x14259222c, 20.0 @0x14259be1c, 15.0 @0x14259be0c,
100.0 @0x14253ee30. Only tried in image states 1 and 2 (`0x14212a8c0`).

**Tier 3, lofted forward** (`0x14212a700`'s third branch). Distance 15–25
(15.0 @0x14259be0c, 25.0 @0x14259be20), `turnClass == 3`, `dirClass != 1`, a
closed ground lane (`openCls == 0`), `press.cls > 2`, and the keeper distance
`(10 + 5·clamp((dist−15)/10))² < press.dist(opp 0)` (10.0 @0x142588210,
5.0 @0x14259222c), then the most advanced.

**Ponytail**: the one-touch flag (`0x142124950`/`0x142128c10`) and the lane tests
`0x14063dab0`/`0x140587e70` are not decoded; `0x140a4c8e0` (the ground-lane test
that tightens `need` at a forward angle ≤ 30°) is replaced by the caller's
`mateLaneOk`.

### 3.6 Plan order and the roll (report §4.4)

`pes::kPlanPriorityOrder` is the priority order itself (9, 8, 2, 3, 1, 4, 5, 6,
table @0x142bf02c0) with Basic as the fallback. `pes::PlanStartProbability` is the
P table and `pes::PlanRoll` the roll.

The roll, read at `0x14211a89..0x14211aac2`: PES builds a throw-away generator
(`0x140a923f0`), seeds **slot 2** with `int(carrierClock × K)` (`0x140a928a0`,
`mov edx, 2` at `0x14211aaa5`), and draws once through RangeRaw(slot 2, 100)
(`0x140a924e0`, `mov edx, 0x64` and `lea r8d, [rdx−0x62]` = 2 at
`0x14211aab4..0x14211aab9`), then starts the plan when `P >= roll`
(`cmp ebx, eax; jae` at `0x14211aacd`). GF's `pes::Random::RangeRaw(2, 100)` is
exactly that draw. The carrier clock is the carrier's possession, which is what
PES's `ctx4+0x18` is (INF in the report).

The K seeds, all .rdata:

| plan | K | VA |
|---|---|---|
| Breakthrough | 1100.0 | 0x142bf02b4 |
| MiddleShot | 1200.0 | 0x14260ba7c |
| CurveShot | 1300.0 | 0x14260b388 |
| Cross | 1500.0 | 0x1425e9094 |
| Long | 1600.0 | 0x14259ce40 |

The P table's bases, read as the immediates of each plan function
(`mov r32, imm32`, the immediate behind the opcode):

| plan | base | VA |
|---|---|---|
| Breakthrough (skill 37 / instruction) | 70 | 0x14211a956 |
| Breakthrough (COM run) | 20 | 0x14211a991 |
| Breakthrough cap | 70 | `cmp ebx, 0x46` at 0x14211aa7e |
| MiddleShot (skills 39/8/33/34) | 70 | 0x14211b628 |
| MiddleShot (state 2) | 40 | 0x14211bcf9 |
| MiddleShot cap | 80 | 0x14211b716 |
| Cross (style 7 / COM 3 / skill 14) | 50 | 0x14211ac86 |
| Cross floor | 40 | 0x14211ad5e |
| Cross cap | 80 | 0x14211ad57 |
| Long, pressure class 0–1 | 30 | 0x14211b289 |
| Long, pressure class 3 | 80 | 0x14211b2a9 |
| Long, pressure class 2 | 60 | 0x14211b2c5 |

Every rolled plan adds the same stat term `N × clamp01((x − 70)/29)` (70.0
@0x14259be4c, 29.0 @0x1425a9460, the gains 15/15/20/10 at 0x14259be0c,
0x14259be0c, 0x14259be2c and report §4.4), with MiddleShot floored at 5 (the −5
comparison at 0x14259be7c).

**GF inputs**: Breakthrough's stat is `technical_dribble` (PES's Dribbling 0x17),
its skill bit Double Touch (37), its COM bits SpeedingBullet/MazingRun/IncisiveRun
(1/2/4); MiddleShot's stat is `technical_shot` (PES's Kicking Power 0x2A, INF per
report §9.5), its skill bits the long-shot trio; Cross's stat the same shot stat
(INF per report §9.5), its skill PinpointCrossing (14) and COM EarlyCross (3);
Long's stat `technical_highpass` (PES's Lofted Pass 0x1C) and its base is the
pressure class.

**Ponytail**: the plans' own start conditions (report §4.4's condition column)
are only partly ported — Breakthrough and MiddleShot need their band, Cross the
close band, Long nothing — and CurveShot, PassThrough, SideChange,
ShotPassFromGoal and BackDFLine are not rolled at all, so they never run. Their
desired angle `+0xC0` and target `+0xC0`/`+0xCC` (report §9.3) have no
counterpart, so PassForward's plan-angle gate is read against the attack
direction.

## 4. What was not ported, and why

| PES piece | Why not |
|---|---|
| 20 of the 25 think units (Clear, Feint, BackSpace, GoalFront, OneTwo, SideChange, Target, Special, RespondRequest, DirectHighBall, the four GK units, OnePattern, SetPlay, EarlyCross, Cross, PassSafety) | the assignment asks for the decision core in one pass; each undecoded unit is on report §9.1's open list, and the decoded ones (Cross, BackSpace, Special, PassSafety, SideChange, Target, GoalFront, OneTwo) are listed here for the next pass |
| the Dribble unit's 72×15 direction cost search (report §5.4) | **decoded and ported as a pure function, wiring reverted** (`pes::DribbleDirection`, CarrierUnitsPort 2026-10-14): the cheapest of 72 headings out to 15 radial steps with the touchline band and goal-axis terms, and threat from the 5 nearest opponents' reach (keepers doubled). The wiring into `GetOnTheBallCommands` (which retired GF's `AI_GetBestDribbleMovement`/`veloExpFactor`) broke the sim: 2-minute matches never reached the whistle inside the 30-minute sim cap, the full-match test failed, and the challenge-count test counted 0 slides. The base commit passes all three. The dominant stall after it was a throw-in whose taker never kicked (taker 0.3 m from a ball at rest, `piece 5`). **Next**: fix the throw-in taker, then re-wire the search. |
| the shield branch and the sub-kind chooser (attack-decisions §2.7) | both on report §9.2's open list |
| the reaction-delay tables T1/T2 and the difficulty capability table (report §2/§7) | this engine has no difficulty tier; the re-think gate alone is ported |
| the first-time gate of capability 0x14 (report §3.2 rule 2) | needs the tier and the skill bits this engine does not carry |
| the adopter's veto/override walk (report §3.2) | PES's answer kinds do not survive translation one-to-one; the order shoot → pass → dribble is what the port implements, which is the walk's dominant rule; the in-play ball-at-rest stalemate (a carrier with possession queuing 0 commands) is **open** - the last smoke showed 59640 → 69912 forced ticks, with the dominant stall moving to a restart whose kick has already been taken |
| the kicker/penalty `0x14211f090` aim function, the shot lane test, the wide evaluator | report §9.1, not decoded |
| the offside line and the busy action classes of the receiver status codes | no GF counterpart for the offside line; the mate's action class is not read |
| the "into space" record B, the pressure table's radii/classes as PES computes them | the pressure class is derived from the nearest opponent's distance with the report's own third-split radii (4 m own third, 2 m attacking third) |
| PES's `0x140a6e450` action classes, `0x140a4c8e0`, `0x14063dab0`, the body-block tests | report §9.1, not decoded |

## 5. AI-map row text

For `docs/re/pes-ai-map.md` §2, the **Carrier think units** row:

```
|Carrier think cadence, shoot zone + "just shoot"/box/header, receiver openness & PassForward tiers, plan roll|**OBS, partially ported** (cadence, shoot decision, openness ladder, PassForward's three tiers, plan order + Breakthrough/MiddleShot/Cross/Long rolls ported; the other 20 units, the dribble cost search, the shield branch and the delay tables not)|`pes-re-carrier-ai.md` §1–5, `pes-re-attack-decisions.md` §2|`elizacontroller.cpp` ball-carrier branches, `pescarrier.cpp`|
```

## 6. Verification run

`tests/sim/pes_carrier_test.cpp` (22 tests): the shoot zone's bands and ranges
with the Finishing and long-shot ramps, the five zone predicates and the box
half-disc, the just-shoot gate and the first-time gates, the openness class
ladder's boundaries, the margin arithmetic including the difficulty multiplier,
the difficulty term's components, PassForward's gate / tier order / three tiers,
the plan priority order and the P table, the roll's slot and modulus, and the
frame conversion. Mutation-checked: the just-shoot distance, the class ladder's
top boundary, the tier order, the roll modulus and MiddleShot's state-2 term each
make a named test fail.

### 20-minute smoke run, same seed, before and after

`build/apps/gfsim_run --db-sql assets/fixtures/teams.sql --home 1 --away 2 --seed 7
--minutes 20 --pes21-root <install>`:

| | before (93d808e) | after (this port) |
|---|---|---|
| shots | 15 – 17 | 23 – 28 |
| on target | 5 – 11 | 6 – 10 |
| saves | 4 – 5 | 8 – 8 |
| xg | 0.85 – 1.23 | 0.94 – 1.48 |
| goals | 3 – 6 | 3 – 4 |
| crossings (in frame) | 5 – 10 (3 – 6) | 8 – 9 (2 – 4) |
| possession | 49 % – 51 % | 48 % – 52 % |

The gate the shoot zone adds is a band gate, not a distance gate, so the shot
count rises (25-21 on the intermediate build, 23-28 on the final) while the xg
per shot falls: shots now come from the 20-24 m band that the old 16 m window
excluded, and the box rule fires inside the 11 m half-disc without a lane test.
Possession is unchanged, which is the point — the carrier now *releases* the
ball rather than holding it through the pass ratings.



## Orchestrator review (2026-10-14, at merge)

- **Open: the shoot-zone live sample does not fit the ported ramp.** carrier-ai §8 read Rn 19.03 for a
  Finishing-71 carrier and 20.90 for one rated 89. The ramp `near + boost·(F − floor)/span` with the image's
  values gives 18.10 and 19.97. That is a 0.9 m gap at both points, which reading resolution does not explain.
  Either the sample's Finishing values were not those carriers' live (fatigue-adjusted) stat, or the zone has
  a term the port lacks. Next: re-sample the live Rn alongside each carrier's live stat byte (`PMS+0x70` block,
  stamina-drain report) and the A5 factor.
- The install test now checks relations (near < far, floor + span = top, ordered seeds), not the game's numbers.
- Merge seams fixed at merge: sim_stamina_test supplies the carrier constants. The first port commit had also
  committed the worktree's godot-cpp symlink over the submodule; it was restored on main, and check_pii now
  rejects it.


> **Settled (CarrierUnitsRE, `pes-re-carrier-units.md`):** the shoot-zone 0.9 m mismatch is not a missing term. The live Rn values imply integer Finishing values (19.03 → 80; 21.00 → 99, capped); the earlier comparison paired roster stats with live ones. No port change.
