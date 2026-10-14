# PES 2021 goalkeeper → GameplayFootball port, round 2 (corrections + positioning + rush)

Scope: fix the shipped `peskeeper.cpp` port's four errors, port PES's open-play
positioning and the loose-ball rush, and run the gate. Sources:
`docs/re/pes-re-goalkeeper.md` §10 (session-2, live-checked) and
`docs/re/pes-re-keeper-port.md`. Tags as in those reports: **OBS** = observed in
bytes/decompile (VA given), **INF** = inferred. No byte copies of game data; every
constant is read from the user's own PES2021.exe at run time.

Code: `src/sim/onthepitch/peskeeper.{hpp,cpp}` (all the arithmetic),
`src/sim/onthepitch/player/controller/strategies/offtheball/goalie_default.{hpp,cpp}`
(`GoalieDefaultStrategy::RequestInput` and the new `InterceptTarget`),
`src/sim/onthepitch/player/controller/elizacontroller.{hpp,cpp}` (the retainer
branch, the keeper rush branch, `Reset`),
`src/sim/onthepitch/match.hpp`/`.cpp` (`matchSeed`, `GetMatchSeed`),
`src/sim/onthepitch/pesrandom.{hpp,cpp}` (`kRange0Slot`),
`src/sim/onthepitch/setpiecelaws.hpp` (`kGoalAreaDepth`, `kGoalAreaHalfWidth`).
Tests: `tests/sim/pes_keeper_test.cpp`, 23 tests, all green (one of them gated on
`PES21_ROOT`).

Commits: `29681aa` (the four corrections and the new arithmetic), then this
round's wiring, deletions and fixes.

---

## 1. The four errors in the shipped port (§10.6, all corrected)

All from `ActionKeeperAfterCatchMove::exec` `0x1409e8880`, own frame (`dir` the
attack sign, `A = F6 + 1`).

| error | the port had | PES (OBS raw) | now |
|---|---|---|---|
| carry branch inverted | `if (-A <= dir*x+4) x = dir*4 + x; else x = -A*dir` | `comiss xmm12(-A), xmm0(dir*x+4); jbe → x = -A*dir`, i.e. **x = min(x+4, −A) in his own frame** | `own_carried < own_held ? carried : held`, with the carry +4 in the own frame |
| sideways correction | `z -= sign(z)*2*B` (two goal-area half widths) | `addss xmm0,xmm0` on ±1.0 = **2 m** | `z -= sign(z)*distributionLateralCorrection_m` (2 m, `0x142565604`) |
| bearing arguments swapped | `atan2(dz, dx)`, so the step went across the pitch | the call passes **Δx first** (`0x1409e8c9b`); `FUN_140a92d40(a,b) = atan(a/b)`, so θ is from +z and `p += 4·(sin θ, sin(θ+90))` is **4 m toward the mate** | `atan2(dx, dz)`; `x += sin θ*4`, `z += sin(θ+90)*4` |
| final clamp missing | — | every branch ends `|z| ≤ F7 − 1` (`0x1409e8d3e`–`0x1409e8d70`) | `z_limit = boxHalfWidth − 1`, applied last |

`AfterCatchMove` is also **not the pass target** (§10.0's kill of the port's
reading, and §10.6.2's header): it derives from `ActionMove` and slot 4 is the
point the keeper **walks to while holding the ball**. The receiver comes from the
carrier brain's GK think units (§10.6.3), which are not read. So the retainer
branch now uses `PesAfterCatchWalk` as the keeper's walk target and keeps GF's own
nearest-teammate rule for the pass. `PesAfterCatchStep` was renamed
`PesAfterCatchWalk` for that reason.

The old tests pinned the wrong behaviour and were rewritten:
`HoldsTheCarryOnThePenaltyAreaLineWhenHeIsOnHisOwnLine` and
`CarriesTheBallUpFieldWhenHeIsFurtherOut` (both the branch and the boundary),
`ReflectsTheCarryLateralByOneGoalArea` → `CorrectsTheCarryLateralTwoMetresTowardTheMiddle`,
`StepsTowardTheChosenTeammate` (its first two cases asserted the swapped
bearing), and a new `StepsAreClampedInsideTheBoxSide`.

## 2. Open-play positioning (§10.2)

`PesKeeperBasePosition` ports `FUN_1409de8c0`'s core `FUN_1409e0390` (out of
possession) and `FUN_1409df7d0` (in possession).

Out of possession (OBS `0x1409e0390`, re-read against the raw disassembly):
- `R = (−(F0+1)·dir, 0)`, 1 m behind his own goal centre (`0x1409e0442`).
- `θ = bearing(R → B)` from +z (`FUN_140439e50` = `atan2(Δx, Δz)`).
- `w = clamp(|θ − a2| / 50, 0, 1)`. **`ponytail:`** `a2 = MA+0x228d4/d` has no
  writer in the report (open §10.8 item 4), so the port uses `θ` itself:
  `w = clamp(|θ|/50, 0, 1)`. **INF(med): since `R` is on the goal line, `θ` is 90
  for any ball ahead of it, so `w` is 1 in the dominant case and the floor takes
  its far end `F2 + 0.5`; the 2 m end needs the ball level with `R` and far up
  the pitch, which the pitch does not allow.**
- `dmin = (F2 + 0.5)·w + 2·(1 − w)` (2 m … 4.2 m).
- `L_m = clamp(offside line, L_t − 10, L_t + 10)`;
  `dline = max(−F4, (L_t + L_m)/2 − 25) + F0`.
- `cap = bit20 ? 4 : bit19 ? 20 : 15` (`0x1409e044a`–`0x1409e0468`).
- `d = max(dmin, min(cap, dline))`, then
  `d = min(d, 4 + 9·ramp(|B.x − K.x|; 40→0, 70→1))` (`0x1409e047d`–`0x1409e04a8`),
  then the 15 m max radius (`min r, |B−R|, 15`).
- `P = R + r·(sin θ, cos θ)` (`FUN_140a8f7c0`), with `dir·P.x ≥ −(F0 − 0.8)`.
- The shuffle (`0x1409e0590`): `Range0(rng, 6)·0.1·(1 − ramp(GKAw; 40→0, 99→1))`,
  every `2·gs` frames, sign flipped when the angle is ≥ 90°, applied
  perpendicular to `R → B`. **Slot 1**: the move reads `Range0`, whose only
  compiled call is slot 1's generator.
- The dispatcher's 2.25 m dead zone is kept in `RequestInput` (`0x1425a9be0`).

In possession (§10.2.3, `0x1409df7d0`): 6 m deeper than 3 m inside the box line,
then `6+3·ramp` to the halfway of his half, `9+4·ramp` to the centre line,
`13+7·ramp` to the opponents' half; floored at 25 m behind the team line, capped
13/15/17 by style, `dir·Δx ≥ 3`. No shuffle (the spot is latched).

**Not ported, each marked `ponytail:` in place:** the `wide` ball-state branch
(`0x81a1`, the 42 m caps, the `7 + cap·ramp` limiter, the goal-line pivot —
the word's meaning is open §10.8 item 3 and only readable live); the on-the-line
airborne mode and the near-post/threat shifts and no-backpedal (§10.4/10.5
layers); the 1v1 rush and the `T+0xb6de` release exceptions (open §10.8 item 1).

The bisector and `GameplayTuning::GetKeeperComeOutBias`/`GetKeeperComeOutMargin_m`,
`PlayingStyles::GetKeeperComeOutBias` and `TeamAIController::ApplyKeeperRush`/
`GetEndApplyKeeperRush_ms`/`endApplyKeeperRush_ms` are **deleted** (nothing else
called them). GK Coverage (`gk_coverage`) stays: it feeds the dive-reach tables,
which are not positioning.

## 3. The rush (§10.3)

`PesKeeperRushFactor`, `PesKeeperRushLateralLimit_m`, `PesKeeperRushRaceWins`
port `FUN_1421162c0`'s ground race (OBS raw, all three constants confirmed in the
instruction stream):
- the factor: Defensive **1.3** (`0x14259bdd8`) | none **1.2** (`0x14259d5d4`) |
  Offensive **1.1** (`0x1425a3590`), chosen at `0x142115e6`–`0x142115f6` by
  style-word bits 19/20 (`r13d = r15d>>0x13`, `r15d>>0x14`);
- the lateral limit on the meeting point: `F5` (Defensive) | `(F5+F7)/2`
  (none) | `F7` (Offensive), at `0x142116d21`;
- the race: `gs·rushing·0.71 + of ≤ k·kf` is the *opponent wins* branch, so he
  goes when `of > k·kf − 0.71·rushing`, with `kf > 3.3·gs` checked first
  (`0x1425c4c10` at `0x142115d06`). `gs·0.71` frames is 0.71 s at any rate, so
  the port compares seconds (GF's tick is 10 ms vs PES's live 54 Hz).

`elizacontroller.cpp`'s keeper branch used to chase on a flat
`GetTimeNeededToGetToBall_ms() < 1000 && designated possession`. That is replaced
by the race, plus the lateral limit on the point where he meets the ball, plus
the area rule (`0x14210565c`): a claim outside his own penalty area is an
**outfield presser**, so the keeper only leaves his box through the outfield
branch. `ElizaController::keeperRushing` carries the hysteresis byte between
ticks and is cleared with the rest of the controller's state in `Reset()`.

## 4. The RNG instance (a cross-match leak, now closed)

The shuffle is PES's **only** keeper draw. A follow-up (KeeperSavesRE,
`docs/re/pes-re-goalkeeper.md` §11) confirmed it is **slot 1 of the per-clip
Random at `anim+0x19c0`**, and that there is **no RNG in the save choosers or
the distribution converter** — their `0x141e532e0`/`d0` are game-speed
constants, not a Random. Nothing in this port drew a number for a save, a
catch, a deflect or a distribution decision, and nothing was replaced.

GF has no per-clip generator. Each keeper therefore holds one `pes::Random` of
its own, **created lazily and seeded from this match's seed** folded with its own
player id (`keeperShuffle`, a `std::unique_ptr` member of the strategy, seeded in
`KeeperShuffleDraw()` from `match->GetMatchSeed() + id*2654435761`). `Match` now
keeps `setup.seed` in `matchSeed` and exposes `GetMatchSeed()` for exactly this.
The slot is `pes::kRange0Slot` (1), exported from `pesrandom.hpp` with a
`static_assert` tying it to `Range0`'s internal constant, instead of a bare 1.

No static or surviving instance: a static would make a second match in the same
process replay the first one's stream, which is exactly the cross-match leak
being chased.

`ponytail:` this is a stand-in for PES's per-clip instance. The slot and the draw
are PES's own; the stream is per-keeper and per-match here, not per animation
clip. The shuffle is lateral noise under 0.6 m, so the divergence from PES's exact
stream cannot change a decision.

## 5. Changed GF functions

| GF function | PES rule it implements | VA |
|---|---|---|
| `pes::PesAfterCatchCarry` | the carry `min(x+4, −A)`, the 2 m lateral | `0x1409e8971`–`0x1409e8a85` |
| `pes::PesAfterCatchWalk` | the 4 m step toward the mate, both clamps | `0x1409e8c52`–`0x1409e8d70` |
| `pes::PesKeeperBasePosition` (out of possession) | the R-line core: caps, line term, ramp, floor, max, guard, shuffle | `0x1409e0390` |
| `pes::PesKeeperBasePosition` (in possession) | the sweeper step-up | `0x1409df7d0` |
| `pes::PesKeeperRushFactor` | the style factor 1.1/1.2/1.3 | `0x142115e6` |
| `pes::PesKeeperRushLateralLimit_m` | `F5 \| (F5+F7)/2 \| F7` | `0x142116d21` |
| `pes::PesKeeperRushRaceWins` | `of > k·kf − 0.71·rushing`, `kf > 3.3·gs` | `0x142115d06`, `0x142116200` |
| `GoalieDefaultStrategy::RequestInput` | calls the positioning; the 2.25 m dead zone | `0x1409dc040` dispatch |
| `GoalieDefaultStrategy::InterceptTarget` | the unchanged bound-for-goal intercept, lifted out of `RequestInput` | — |
| `GoalieDefaultStrategy::KeeperShuffleDraw` | the shuffle's `Range0(6)` on this keeper's own generator, slot 1 | `0x1409e0590`, `0x140a92530` |
| `elizacontroller.cpp` retainer branch | `AfterCatchMove` as the walk target; the pass is GF's own nearest-mate rule | `0x1409e8880` |
| `elizacontroller.cpp` keeper branch | the race + lateral limit + area rule replaces `< 1000 ms` | `0x1421162c0`, `0x14210565c` |
| `Match::matchSeed` / `GetMatchSeed` | the per-match seed any per-match generator needs | — |
| `pes::kRange0Slot` | `Range0`'s slot, exported instead of a bare literal | `0x140a92530` |
| `SetPieceLaws::kGoalAreaDepth/Width` | Law-1 goal-area marks (PES F4/F5) | — |
| deleted: `GameplayTuning::GetKeeperComeOutBias/Margin_m`, `PlayingStyles::GetKeeperComeOutBias`, `TeamAIController::ApplyKeeperRush/GetEndApplyKeeperRush_ms/endApplyKeeperRush_ms` | the come-out bias and the 300 ms rush window are not PES rules | — |

## 6. Verification

- `pes_keeper_test`: 23/23 green (the install-loader test checks every value
  through relations, not pinned numbers, and pins the rush factors as monotone in
  style order: Offensive 1.1 < none 1.2 < Defensive 1.3).
- Mutation checks on the four corrections: deleting the carry's own-frame `min`
  (2 tests fail), the 2 m lateral (1), the step's bearing order (1), the
  `F7 − 1` clamp (1). All restored green.
- Positioning term edges pinned directly: the 4/15/20 caps, the line term at a
  deep line (5.5 m), the 4→13 ramp at 40/55/70 m, the floor at both ends, the
  15 m max radius, and the sweep bands 6/9/13/20 with the 13/15/17 caps.
- Gate: `cd build && PES21_ROOT=… PES21_EDIT_SAVE=… ctest -j3 --timeout 2400`
  and `tools/check_pii.sh`.
- Smoke (20 min, seed 7, before and after; both `Full match complete`, exit 0):
  median/mean keeper distance from his own goal centre while his team did not
  have the ball.

| | mean while defending | max |
|---|---|---|
| before (baseline `c4648b0` + probe only) | 6.04 m / 7.82 m | 20.84 m / 28.82 m |
| after | 7.57 m / 7.41 m | 12.41 m / 12.40 m |

The after-numbers are PES's model working as specified: he never drifts past
~12.5 m, which is the 15 m R-line cap reduced by the `dir·P.x ≥ −(F0 − 0.8)`
guard. The mean rises because the old come-out bias pushed him to the backline on
some plays and 10 m in on others, while PES's ramp holds him at 4 m while the
ball is within 40 m of him lengthwise. The probes used to measure this were
removed before the commit.

`SimMatch.SameSeedPlaysTheSameMatch`: **run and reported honestly below** — the
test plays two matches in one process and compares, so it is the direct check on
the cross-match leak. Its result is in the gate output, not assumed.

## 7. Open

**`SimMatch.SameSeedPlaysTheSameMatch` FAILS, and it is not yet explained.** The
test plays two matches in one process with the same seed and compares both the
determinism hash and the whole report. The baseline (`c4648b0`, plus only the
measurement probe) passes it — I ran it and got the same hash twice. So the
divergence is exposed by this port.

Bisect (each step: run the same test, in this order):

| step | result |
|---|---|
| positioning disabled (fixed 10 m-off-line target), everything else in place | **deterministic** |
| rush disabled, positioning live | fails |
| shuffle draw replaced by a stateless tick-derived value | fails |
| shuffle draw replaced by a constant | **deterministic** |
| both line lookups (`GetOffsideTrapX`, `AI_GetOffsideLine`) and the draw removed | **deterministic** |
| trap restored, offside line and draw still removed | fails |

So the trigger is **`TeamAIController::GetOffsideTrapX()`** — reading it at all
makes the two runs diverge, even when the value it produces is then discarded
and a fixed direction returned. That value is a plain float written once per
tick in `TeamAIController::Process()` and only while `IsInPlay() &&
!IsInSetPiece()`, so during a stoppage it keeps its previous value. It is not
obviously order-dependent, which is why this reads as the **latent cross-match
leak** the orchestrator is chasing rather than a bug in the port: the keeper's
new depth follows the trap, that changes the ball, and whatever leaks across the
two matches in the process is then amplified into a different match.

The offside-line read was corrected anyway (it now uses the current mental image,
as every other caller does), which is right on its own merits, but it does not
fix the determinism.

The test was **not** weakened. The remaining work is to find why reading the
offside trap makes the two same-seed runs diverge; `GetOffsideTrapX()` is the
place to start, and the leak is in the shared state it (or what it aggregates)
carries across matches.
- §10.5.1 `KP_SAVING_MOVE`, §10.4's cross claim, §10.5.2 `KP_PRE_SAVE_OPERATION`
  and §10.6.3/4's GK think units are not ported (see §2 and §3 for what that
  leaves: the `wide`/`0x81a1` branch, the on-the-line airborne mode, the
  near-post/threat shifts and no-backpedal, the 1v1 rush and the `T+0xb6de`
  exceptions).
- The save choosers (`FUN_142113390`, `FUN_1421139a0`) remain decompiled but
  unreduced.
