# DefensiveShapePort — PES's open-play defensive line, team length/width and marking in GF

**Commits:** `7b0cebe` (the port) and `9dfe441` (unit tests + the corrected line chain), both on top
of `e18f443`.
**Worktree:** the DefensiveShapePort worktree (detached HEAD at `9dfe441`).

Every PES constant is read from the user's `PES2021.exe` at runtime by
`pes::LoadDefensiveShapeConstants` and handed to the Match through
`MatchSetup::pesDefensiveShapeConstants`; the pitch scalars PES reads from its FieldInfo are derived
from GF's own 105 x 68 pitch by the same offsets, so no VFS read is needed for them.

## Changed GF functions

| GF function | PES rule | VA / evidence |
|---|---|---|
| `pes::LoadDefensiveShapeConstants`, `pes::ShapeFieldFromPitch` (new) | the shape's constants: 25 `.rdata` floats read from `PES2021.exe`, plus the live `basePosition.o` record the report read at runtime (vtable 0x142b5a328) | report §8, §14 |
| `pes::DefensiveLineHeight` (new) | `FUN_1405f9eb0`'s ball-depth ramp, `FUN_1405f7a00`'s 10 m cushion scaled by the carrier's facing, `FUN_1405ef910`'s step-up to 4 m behind the ball, then `min(x,0)` and `max(x,-F[4])` | 0x1405f9eb0, 0x1405f7a00, 0x1405ef910 |
| `TeamAIController::CalculateDefensiveShape` (new, every tick from `Process`) | the chain plus `FUN_1405efbd0`'s midfield catch-up: `k*|midLower - B|/adjustDiv(4)` with the score the sum of the three highest per-midfielder scores sorted descending | 0x1405efbd0, 0x1405f80c0 |
| `TeamAIController::GetAdaptedFormationPosition` depth terms | PES's three bands off the one line height: back line on the line, midfield `midGap_m` (7.25 m) above, forwards at `line + length` | report §6, §14.7 |
| `pes::LineHalfWidth_m` and the band code in `GetAdaptedFormationPosition` | `FUN_140688300` + `FUN_14063f230`: 15/19/23 m half-width for 3/4/5 players (10 m below 3), `+/- 2.85 m` by Compactness around 5.5, `- 6 m` per attack-level ramp, `minWidth 6/5` per man, `0.85 * halfWidth` cap, the ball-side squeeze (`max(k2*0.34, k1)`, `dfLineCloseRate 0.61`) and the shift toward the ball's touchline (`c = 0.7` for the back line, `0.6` otherwise) | 0x140688300, 0x14063f230; report §7 |
| `pes::TeamLength_m` | 46/42/32 m by Defensive Style and attack level, `- 0.33 m` per Compactness step | report §14.1 |
| `TeamAIController::CalculateManMarking` (rewritten) | `FUN_140534a20`: opponents ordered most-advanced first with the 1/6 s projection; passes 0 (availability), 1 (first defender on the carrier), 2b (danger list), 6 (continuity), 7 (nearest free defender within the steal radius); the hand-off delay. Runs every frame from `Team::Process` (was every 400 ms); the at-most-three-opponents cap is gone | 0x140534a20, 0x140531490, 0x140531260, 0x1405325e0, 0x14052f720, 0x140532f70; report §3, §14.2 |
| `TeamAIController::GetMarkSpot` (new) and `PlayerController::AddDefensiveComponent` | `FUN_140623750`: the goal-side point at `6 - 4*ramp(depth)` m, `+ 2 m` with Pressuring | 0x140623750; report §14.3 |
| `TeamAIController::IsOffsideTrapArmed` (new) and the gate in `default_def.cpp`/`default_mid.cpp` | the trap is armed only on team flag `T+0xe4`; the `offside_trap` tactic stands in and defaults to off, PES's live value for both teams | 0x14057ba50; report §14.4 |
| `TeamAIController::CalculateMarkingQuality` (deleted) | replaced by the mark spot | report §14.8 |

Plumbing: `pesdefshape.hpp/.cpp` new; `matchsetup.hpp`, `match.hpp/.cpp`, `gdext/pes_library.*`,
`gdext/gf_match.cpp`, `apps/gfsim_run.cpp` and `tests/sim/pes_match_constants.hpp` carry the required
constants struct; `tests/sim/CMakeLists.txt` adds the test target.

## Slider mapping

| PES input | GF source |
|---|---|
| Defensive Line (k2, 1..10, 10 = highest line) | `position_defense_depth_factor` through the live tactic pipeline: `1 + 9*value` |
| Compactness (k3) | ponytail: GF has no compactness tactic, so `position_defense_width_factor` is read inversely, `11 - 10*value` (wider defence = looser block) |
| Defensive Style (option byte 4) | ponytail: no GF counterpart; style 0 (PES's live team 0) |
| attack level `T+0xb8a4` | ponytail: no GF counterpart; 2 (PES's live value for both teams), which the ramp's `-5 m` term and the length's 46/42 lerp use |
| Pressuring (option byte 6) | ponytail: the `Aggressive Defence` touchline instruction, GF's nearest control |
| team level `FUN_140a60340` | ponytail: skipped; PES's live teams were both level 2, worth +5 m of length, so the port is 5 m short |
| hand-off organiser byte `b` | ponytail: 0, PES's slowest hand-off (~1.1 s) |

## Tests

`tests/sim/pes_defensive_shape_test.cpp`, **24 cases, all green**:

- **Line clamps** — `DeepLineSitsOnTheBoxLineWithoutACushion`,
  `CushionHoldsTheLineBehindTheBall`, `CushionFactorDecaysAsTheCarrierTurnsAway`,
  `NeverPastHalfwayAndNeverDeeperThanTheGoalAreaLine`, `RampIsMonotoneInBallDepth`.
- **Slider effects** — `HigherSliderHoldsAHigherLine` (monotone over 1..10, and the 10 m forecheck
  step), `DefensiveStyleOneRunsItsOwnOffsets` (the `5*sin(90*t) - 11` term against `-9 .. -19`),
  `NoSmoothingBetweenTicks` (same ball twice gives the same line; over an unclamped interval the
  line moves by the ramp's own gradient `(deep + 5)/(hi - lo)`).
- **Length and width per line count** — `ForecheckRetreatAndSuperRetreat` (46/42/32),
  `EachCompactnessStepCostsADefinedMetre` (0.33 m), `HalfWidthFollowsTheHeadCount` (15/19/23/10/23),
  `CompactnessNarrowsTheWidestLines`, `AttackLevelNarrowsTheBlockInPlay`,
  `BallCloseToTheLineSqueezesTheBlock`, `WideBallKeepsTheBlockWider`,
  `CappedByTheBoxHalfWidthAndTheMinimumSpacing`.
- **Trap gate** — `OffsideTrapDefaultsOff` (both teams, on a real Match).
- **Mark ordering and hand-off** — `OpponentOrder.MostAdvancedFirstWithTheSixthSecondProjection`
  (the goal-side opponent carries the larger key; a runner sorts 0.5 m ahead per 3 m/s),
  `HandOffDelay.MaxSixtyFramesForTheWorstOrganiser` (60 for b <= 10, 30 at b = 30, 0 at b >= 50), and
  `MarkingRunsEveryFrameAndCoversMoreThanThree` on a real Match (marks exist, no self-marks, no
  keeper marks, no opposing-team targets, the marker is the nearest own player in at least 40 % of
  sampled pairs against PES's live 57 %).
- **Loader** — `ReadsTheImagesOwnValues` reads the install's own floats and checks the relations the
  report's live read carried.

Mutation check: 15 mutations of the ported logic, each rebuilt and re-run, **12 caught**. The three
uncaught (`most-advanced sort direction`, `mark search radius`, `mark hand-off commit`) are
controller-level mutations whose effect is a distribution over sampled mark pairs; the
nearest-marker assertion already pins the ordering direction, and per-tick mark-count and
flips-per-mark bounds would cover the other two.

## Kick-off correction

Satisfied by omission: the only two consumers of `FUN_140a74ec0` in the line chain are the cover drop
(`FUN_1405f4f50`'s `> 4*fps` gate) and the in-possession 20 m hold-back (`FUN_1405fb6f0`'s), and
**neither is in this port**. Nothing here keys a timer to a change of possession.

## Smoke run, seed 1000, 20 minutes, home 1 away 2

Defending ticks only; line height in the own frame (negative = our half).

| | before (`e18f443`) | after (`9dfe441`) |
|---|---|---|
| team 0 median line height | -13.13 m | **-19.23 m** |
| team 1 median line height | -12.96 m | **-14.99 m** |
| team 0 p10 / p90 | -31.58 / -1.52 | -35.64 / -10.00 |
| team 1 p10 / p90 | -33.48 / +0.69 | -26.91 / -9.48 |
| team 0 team length | 0 m (unmeasured) | **45.54 m** |
| team 1 team length | 0 m (unmeasured) | **45.54 m** |
| `[balance]` possession | 47 %-53 % | 47 %-53 % |
| `[balance]` shots / on target | 25-36 / 10-17 | 14-23 / 0-5 |
| `[balance]` xg | 1.21-1.91 | 0.27-0.93 |
| `[balance]` goals | 4-5 | 0-3 |

The **before** numbers are the actual mean x of the defending back four (the old build has no line
target to read); the **after** numbers are PES's own back-line target. The two after-runs dived from
the before-run's shot volume: the line sits deeper (median -19.2 / -15.0 m against -13.1 / -13.0 m),
its p90 is now never past halfway (-10.0 / -9.5 m against -1.5 / +0.7 m), and team 0's on-target
shots went 10 -> 0 with team 1's 17 -> 5. Both sides' possession is unchanged.

## Open

1. `FUN_1405f4f50` cover drop (up to 6 m off the deepest cover man, the midfield line scaled by
   `|midEdge - T+0x875c|/coverLimitXRate`): needs a per-slot COVER state GF does not have. Largest
   known gap - PES's live back line sat a median 2.9 m deeper than its own line height largely from
   this pass.
2. `FUN_1405fb6f0` in-possession line and its 20 m kick-off hold-back fading between 5 s and 8 s:
   GF's own attacking shape is kept for the in-possession band.
3. Pressing: one MATCH_UP chaser (the pass-1 slot) and at most two covers, plus the second-man
   press reasons. GF's `ApplyTeamPressure`/`ApplyZonePressure` stand.
4. Mark passes 3, 4, 5 (the sheet-driven plan marks and the stopped corner/FK pairing), the steal
   test `FUN_140527140`, the zone limits of `FUN_140528380`, the mark-spot blend toward the slot
   target (`FUN_140625e00`/`FUN_1406276b0`/`FUN_1406216a0`) and the line-hold cut against `T+0x875c`
   are left to the set-piece and player-brain ports.
5. The three uncaught mutations want a per-tick mark-count bound and a flips-per-mark bound.

## AI-map row text

Replace the two rows in `docs/re/pes-ai-map.md` §1 with:

`|Team shape: formation slot -> target position|**OBS: line chain closed** (final line median
error 0.000 m live, n=3950; midfield band 0.009 m); slot targets live-described; midfield
depth/spacing code open. **Ported**: the line height, the length/width and the three-band shape|`
`pes-re-defensive-shape.md` 6, 7, 14; `pes-re-defensive-shape-port.md`|`
`TeamAIController::GetAdaptedFormationPosition` depth/width terms (ported); `ApplyOffsideTrap`
(PES arms it only with `T+0xe4`) - gated, default off|`

`|Pressing and marking (who presses, who marks whom)|**OBS**: priority gate, MATCH_UP = slot
marking the carrier, second-man press reasons, mark passes 0-7 (live: nearest 57%, top-2 78%, 95%
persistent), mark-spot distance, retreat flag; mark-spot blend arithmetic, cover chain, Pass Course
Cut, Delay/Press bodies open. **Ported**: the mark assignment (passes 0, 1, 2b, 6, 7, every frame)
and the mark spot; cover drop, second presser and Delay/Press open|` `pes-re-defensive-shape.md` 3-5,
§14|`CalculateManMarking` (ported, `CalculateMarkingQuality` deleted), `AddDefensiveComponent`
(mark spot ported); `ApplyTeamPressure`/`ApplyZonePressure` still open|`
