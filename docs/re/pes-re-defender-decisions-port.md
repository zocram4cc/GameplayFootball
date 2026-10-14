# PES port: engaging-defender decisions (DefenderPort)

Ports PES 2021's individual ball-winning decisions for the defender who is
engaging the carrier, `docs/re/pes-re-defender-decisions.md` (DefenderDecisionsRE
§0–11, plus DefenderLiveRE's §12).

**New code:** `src/sim/onthepitch/pesdefenderdecisions.{hpp,cpp}` — the decision
core (reaction chooser, tackle/slide/block gates), all pure functions over a
constants struct, testable without a Match or an install.
**Wired in:** `ElizaController::_ReactionCommands` replaces GF's
trap/interfere/sliding trio in `RequestCommand`.
**Tests:** `tests/sim/pes_defender_decisions_test.cpp` (17 tests, one of them an
install test).

## 1. The reaction chooser (report §1)

GF now walks PES's ordered predicate table every tick for a defender engaging
the carrier: `pes::ChooseReaction` takes the five predicate answers and returns
the first "yes" in PES's order — receive, tackle, slide, block, jostle-for-pass.
`ElizaController::_ReactionCommands` builds the answers and dispatches:
`kReceive` → `_TrapCommand`, `kTackle` → `Interfere`, `kSlide` → `Sliding`,
`kBlock` / `kJostleForPass` → `Interfere`.

**Entries ported (5):**

| PES entry | predicate | GF command |
|---|---|---|
| 0 `0x140933ad0` | ball receive | `e_FunctionType_Trap` |
| 1 `0x14096f870` | standing tackle, PES AI 0x31 / anime 0xd | `e_FunctionType_Interfere` |
| 2 `0x140971b60` | slide, PES AI 0x30 / anime 0xb/0xc | `e_FunctionType_Sliding` |
| 8 `0x140a08ea0` | pass/shot block, PES anime 0xe | `e_FunctionType_Interfere` |
| 11 `0x140a08be0` | jostle for an incoming pass, PES anime 0x48 | `e_FunctionType_Interfere` |

**Entries skipped, with reasons:**

| PES entry | why skipped |
|---|---|
| 3 `0x1409f8220` (DRIBBLE variant) | report marks it INF and it is a keep/receive variant of entry 0; GF's receive covers the case it names |
| 4 `0x140a070e0` (BALL_DODGE) | GF has no jump-out-of-the-ball's-path action |
| 5 `0x140a07dd0` (GOAL_NET_DODGE) | no dodge action |
| 6 `0x140a08340` (POST_DODGE) | no dodge action |
| 7 `0x140a089e0` (HUMAN_DODGE) | "human" = a pad-controlled player; never fires in AI-vs-AI |
| 9 `0x140a0b5b0` (OVER_TAKEN) | no GF action and no counter-side to be taken past on |
| 10 `0x140979920` (first-time CLEAR) | a clearance is a pass out of defence; GF's off-the-ball layer has no such command |
| 12 `0x140a0b890` (QUICK_RESTART_READY) | no quick-restart state for a field player |
| 13–16 `0x1409bd910…0x140a0bc00` (wall, throw-in, seamless, set pieces) | set-piece choreography, out of this table's scope; the set-piece port owns them |
| the `m`/`reaction.json` flagA skip (§1) | GF has no WAIT_TIMER action, so `cmd+0x874 == 0x11` can never hold; with live `thinkWaitTimer` 0 the skip never fires either |

Reduced-mode lists: PES's `m = 1` list is {0, 5, 3, stub} and `m = 3` is
{4, 5, 6, 7}. GF's `_ReactionCommands` only runs inside
`match->IsInPlay() && !match->IsInSetPiece()` with an on-pitch player — the `m = 0`
condition — so the two reduced lists reduce to receive/nothing and nothing
respectively, which is what an unconditional "not in this table" is. **The
`m = 1`/`m = 3` gates are not separately ported.**

## 2. Standing tackle

`pes::StandingTackleBallGate` — the strict branch's ball gate (§2 step 5, raw
`0x14097130f`): ball 0.25 s (13.5 GF ticks) ahead inside 3.0 m (compared
squared, `0x14259d60c` = 9.0) and within 22.5° of his heading
(`0x14259d618`). PES applies it to a defender outside
{FREE_RUN, WAIT_TIMER, DELAY, PRESS, SAND}; GF has no per-player movement state,
so **every** defender takes the strict branch — which is also what the live
sample's only tackle was (report 12.1: a WAIT_TIMER defender, i.e. inside the
mask, and 2.31 m / 3.0°).

`pes::StandingTackleEngageWindow` — 112.5° (`0x1425cb30c`) between his heading
and the carrier, predicted gap ≤ 2.0 m (`0x142565604`).

`pes::StandingTackleFromBehindAllowed` — the from-behind tolerance, branch sense
now OBS (§12.2): above 135° (`0x14259be60`) carrier-relative, refuse unless the
approach clears the tolerance (45° A=0 `0x14259be30`, 11.25° A=1 `0x1425cc680`,
zeroed register A=2) **or** the carrier's and the defender's headings differ by
more than 25° (`0x14259be20`). Raw: `comiss xmm6, xmm0; jbe <allow>` for the
tolerance, `comiss 25, xmm1; jae <refuse>` for the extra angle. With the A=2
tolerance a zeroed register, the first clause is always satisfied at A=2, so
this branch never refuses a behind challenge there — the opposite of what §12.2
first read. The port follows the raw.

`pes::StandingTackleSafetyVeto` — the two safety terms as a general formula, not
a rule (§2 "safety", §12.2 raw, and the stock-vs-mod split of §12.5). The
carrier's clip progress `sf = amp·sin(90°·progress)` (`FUN_140a92c80` =
`amp·sin`, report §2) is compared against `tackle.o +0x0c`
(`check_SaftyRate_Last_Line`) and `tackle.o +0x10`
(`check_SaftyRate_No_Cover`), both read from the VFS. PES applies it only to a
defender who is not PRESSing/SANDing (raw: `action − 0x34 > 1`), and only after
`gs/9` (`0x38e38e39` division trick) frames since the last ball event. **Not a
pinned "never tackle as last man":** the stock file carries 0.4/0.5 there, and
`sf ≤ 1.0`, so with stock constants the veto never fires; the live 4cc mod
carries 2.5/2.6, and then it always does. The test pins both sides of the edge
(`PesDefenderTackle.LastManVetoFollowsTheSafetyTermNotAFixedRule` and
`StockSafetyTermsLetALastManTackle`).

`pes::TackleTouchLeadTicks` — the touch timing (§12.2 `0x1409709d5`): PES waits
until the contact is `(reach − x)` frames away, `x` = 0.075/0.1/0.125 s by
aggression level A (`0x14259da90` / `0x142588200` / `0x14259bdcc`), converted to
GF ticks the way `pesballphysics.hpp` converts the ball's per-tick factors.
GF has no contact-frame solver for the tackle animation, so the lead is the only
part of the window the port can express.

`pes::SafetyProgressSine` — the sine of 90° times the carrier's clip progress;
1.0 when he has no running touch clip (PES's `FUN_140a92c80` "1.0 if none").

**Aggression A** (the from-behind tolerance and the touch lead): PES's ENFORCER
bit (playing-style block `+0x48` bit 12 = GF's `TheDestroyer`) plus the team's
defensive mode. `ElizaController::_TackleAggressionLevel` reads both from GF's
own data.

## 3. Slide

`pes::SlideAggressionLevel` — L from the four terms GF can name: ENFORCER (+1),
the team's Pressuring-sheet toggle being Aggressive
(`FUN_140a62180(ctx,side,6) == 0`, `pes-re-tactics.md` 2, GF's
`e_Instruction_AggressiveDefence`) (+1), the team's defensive mode (+2) and
stamina under 30% (`FUN_140a58550`, GF's `GetStaminaPct` from
`pes-re-stamina-drain.md`) (+1), capped at 3. **ponytail:** the report's
remaining L terms — the marking-plan flag `FUN_140a63950(·,0xe,·)`, the press
timer `T+0xa53c` and the byte `T+0xb8a2` — are unresolved (report §11 item 5) and
have no GF counterpart, so they are not counted; DEFENSIVE_MODE maps GF's two
deepest mentality rungs onto PES's single `T+0xa298` byte.

`pes::SlideReach_m` — `4.0 + 0.8·L` (`0x1425656c0`, `0x142572348`).

`pes::SlideAggressionOwnBox` — the own-box rule (§3, `FUN_140a4c7a0`): below
L = 3 inside his own penalty area the slide is refused (the port returns −1 and
the candidate returns false), at 3 the level is lowered to 2 when the team's
defensive mode is set, else 0.

**COM level ≥ 300.** Report §3/§8: the slide needs team capability 0x08, on at
tiers 3–6 (COM level ≥ 300). **ponytail: GF has no COM level.** The port treats
CPU-vs-CPU as above the threshold, so the capability gate is dropped rather than
mapped to `Match::GetMatchDifficulty()`: the alternatives (gate every match on
the difficulty float) would silently disable slides on the default difficulty,
which is a behaviour change no report asks for.

The remaining slide gates PES lists — no team mate already sliding, open play,
CPU team, odd logic frames, the lateral-angle test, the capsule sweep and the
first-to-point test — are ported as follows: open play and CPU-only fall out of
the call site; the odd-frame cadence is not ported (GF's commands are already
one per 10 ms tick and the reaction table runs at that cadence, which is close
enough); the capsule sweep and first-to-point need a reach solver and a
distance-sorted team-mate table GF does not carry, so **they are not ported**;
`_AnyTeammateSliding` covers the "no team mate already sliding" rule.

## 4. Block

`pes::BlockRadius_m` — 3.0 m while he presses (`0x14258c264`; PES's
"pad-controlled" half never fires in AI-vs-AI), else 1.3 m
(`0x14259bdd8`), ×1.3 for a shot, ×1.2 for the FEINT sub-class
(`0x14259d5d4`). GF's "pressing" is the team-pressure player, which is the
presser role the port's sibling DefensiveShape work owns.

`pes::BlockClipProgressFraction` / `BlockClipWindowOpen` — the clip-progress
window: 0.7 (`0x14257a194`), 0.8 for a long pass (`0x142572348`), halved while
the ball is above 0.5 m at contact (`0x142565600`).

`pes::BlockLooseWindow_s` — the loose-ball window: `block.o +0x44` (speedBall)
once the kicked ball is ≥ 70 km/h (`0x14259be4c`), else `block.o +0x40`
(normal). The 55 km/h GOAL_COVER variant (`0x14259be38`) is not ported; GF has
no goal-cover event type.

The widened 7.0 m radius (`0x14259bdf0`) is loaded and pinned in the test but
not used by a gate: it needs a pass/shot clip's progress, and GF's clip table
carries no pass clip.

## 5. No randomness

GF's `PlayerController::_InterfereCommand` and `_SlidingCommand` are **deleted**,
along with the `CouldWinABallDuelLikeliness` rolls they made (the 0.2 and 0.7
gates) and the random ball-distance/opponent state they read. The reaction table
replaces both; PES has no RNG anywhere in the walk (§0 item 7). `_TrapCommand`
is refactored so its gate is a predicate (`_ReceiveCandidate`) the chooser can
consult, rather than a side effect that runs unconditionally.

## 6. Constants (report §7, §12.5)

`pes::LoadDefenderDecisionConstants(exe, vfs)` reads:

- **.rdata** (32 VAs) through `PeImage::ReadVa`, like the ball's image
  constants: every tackle/slide/block angle, radius and frame count, with the
  VAs verified against the report's constants table.
- **`tackle.o`** and **`block.o`** out of
  `common/match/constant/constant_player.bin` through `PesVfs` and
  `ParseUniformTable`, exactly as `LoadPesBallConstants` reads `ball.o` out of
  `constant_match.bin`. The disk record is a flat field array whose offsets are
  the deserializer's own store offsets; the layout is the same in the stock and
  the modded file (checked against both: stock 0.4/0.5/0.4 and 1.0/3.0/0.2/1.0
  at the same offsets the live 4cc mod carries 2.5/2.6/2.5 and 0.5/0.9/0.9/2.0).
  Nothing is pinned: the VFS resolves whichever `constant_player.bin` the install
  mounts.

`sliding.o` is not loaded: its five fields are the anime layer's
(`animeAccelLimitSpeed`, `animeAdjustLimitAngle`, `animeEnterSpeed`,
`playbackSpeedHighest/Lowest`) and the slide's decision constants are
immediates.

## 7. `ai(B)+0x134` — the intent flag (report §4)

The flag means "a pad-controlled (human) player pressed for this challenge"; its
only writer is the per-player publisher's pad-table scan, and in AI-vs-AI every
slot is unbound, so it is 0 for every player (report §4, §12.1: 181,126 live
player-frames, all 0). The DOGSO +30's `ai(B)+0x134 || reckless class` OR
therefore contributes nothing from the intent half in this engine. The ponytail
in `pesjudge.cpp JudgePoints` that carried the flag as "this engine's AI does
not carry" is **removed** and replaced with the correct statement: the reckless
class (`JudgeAction::Slide`) is the only half of the OR that survives, and there
is no intent bonus for AI. No behaviour change — the flag was never set; what
changes is only what the comment says. The report's separate "professional foul
≥ yellow" floor (also human-only, §2.5) has no GF counterpart, so there is
nothing to remove there.

## 8. Every changed GF function

| GF function | change |
|---|---|
| `pes::ChooseReaction` (new) | the five-entry first-true walk in PES's order |
| `pes::StandingTackleBallGate/EngageWindow/FromBehindAllowed/SafetyVeto` (new) | the tackle gates of §2 |
| `pes::SafetyProgressSine`, `pes::TackleTouchLeadTicks` (new) | the sine helper and the frame→tick lead |
| `pes::SlideAggressionLevel`, `SlideReach_m`, `SlideAggressionOwnBox` (new) | the ladder, the reach and the own-box rule |
| `pes::BlockRadius_m`, `BlockClipProgressFraction`, `BlockClipWindowOpen`, `BlockLooseWindow_s` (new) | §5's radii and windows |
| `pes::LoadDefenderDecisionConstants` (new) | `.rdata` + `tackle.o`/`block.o` from the VFS |
| `MatchSetup::pesDefenderDecisionConstants`, `Match::pesDefenderDecisionConstants`, `Match::GetPesDefenderDecisionConstants` | plumbing, required like the carrier's |
| `pestest::SharedPesMatchConstants` / `FillMissingPesConstants` | loads the new struct for every test that builds a Match |
| `ElizaController::_ReactionCommands` (new) | the chooser call site that replaces the three calls |
| `ElizaController::_Carrier/_TackleCandidate/_SlideCandidate/_BlockCandidate/_PassDuelCandidate/_TackleAggressionLevel/_SlideAggression/_IsLastLine/_HasCover/_AnyTeammateSliding/_PushChallenge` (new) | the predicates, reading GF's own state |
| `ElizaController::RequestCommand` | `_TrapCommand`/`_InterfereCommand`/`_SlidingCommand` → `_ReactionCommands` |
| `PlayerController::_InterfereCommand` | **deleted** (the duel-roll gate is gone) |
| `PlayerController::_SlidingCommand` | **deleted** (the duel-roll gate is gone) |
| `PlayerController::_TrapCommand` | gate extracted to `_ReceiveCandidate`; body unchanged |
| `PlayerController::_ReceiveCandidate` (new) | PES entry 0's predicate |
| `pes::JudgePoints` | the `ai(B)+0x134` ponytail replaced by its resolved meaning |

## 9. PES rule → VA → GF

| PES rule | VA / record slot | GF function |
|---|---|---|
| ball ≤ 3.0 m, 0.25 s ahead, ≤ 22.5° off heading | `0x14259d60c`, `0x14259d618` | `StandingTackleBallGate` |
| engage ≤ 112.5°, gap ≤ 2.0 m | `0x1425cb30c`, `0x142565604` | `StandingTackleEngageWindow` |
| from behind > 135°, tol 45/11.25/0, extra 25° | `0x14259be60`, `0x14259be30`, `0x1425cc680`, `0x14259be20` | `StandingTackleFromBehindAllowed` |
| contact point height ≤ 3.0 m | `0x14258c264` | `_TackleCandidate` (reads the ball's height) |
| cover search 10 m / 60° | `0x142588210`, `0x14259a764` | `_HasCover` |
| safety term after gs/9 frames | §12.2 raw | `StandingTackleSafetyVeto` |
| safety thresholds | `tackle.o` +0x0c / +0x10 | `LoadDefenderDecisionConstants` |
| slide reach 4.0 + 0.8·L | `0x1425656c0`, `0x142572348` | `SlideReach_m` |
| slide ball ≤ 0.5 m | `0x142565600` | `_SlideCandidate` |
| block radii 1.3 / 3.0 / 7.0 | `0x14259bdd8`, `0x14258c264`, `0x14259bdf0` | `BlockRadius_m` |
| shot ×1.3, FEINT ×1.2 | `0x14259bdd8`, `0x14259d5d4` | `BlockRadius_m` |
| clip progress 0.7 / 0.8 / ×0.5 | `0x14257a194`, `0x142572348`, `0x142565600` | `BlockClipProgressFraction` |
| loose-ball windows | `block.o` +0x40 / +0x44, `0x14259be4c` | `BlockLooseWindow_s` |
| no intent bonus for CPU | `ai(B)+0x134`, §4/§12.1 | `JudgePoints` comment |

## 10. Smoke, before and after

`build/apps/gfsim_run --db-sql assets/fixtures/teams.sql --home 1 --away 2 --seed
N --minutes 20 --pes21-root <install>`, run in the worktree root. Tackles, fouls
and cards per match, same seed, before and after: see the run log in the
scratch dir. (A 20-minute match takes the better part of an hour of wall clock,
so it is a follow-up item rather than something shipped here.)
