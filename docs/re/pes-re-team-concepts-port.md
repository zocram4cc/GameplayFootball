# Port: team sheet, concept selector, counter-press and counter-attack (TeamConceptsPort)

Sources: `pes-re-tactics.md` §1, §2, §2.1, §4.1; `pes-re-transitions.md` §1.1, §1.2, §2.1, §3.1;
`pes-re-counter-trigger.md` §1.3; `pes-re-team-ai-leftovers.md` §2.3; `pes-re-constant-team.md` §2.
Code: `src/sim/onthepitch/pesteamconcepts.*`, `TeamAIController::RunConcepts` and friends.
Tests: `tests/sim/pes_team_concepts_test.cpp`.

## Ported

| what | PES | GF |
|---|---|---|
| Team sheet: Support Range, Defensive Line, Compactness, the 7 toggles | `S = T+0x8e08`; ctor `FUN_1408c5390` defaults read from its `mov byte` immediates (0x1408c5418/54/71), toggles 0 (`xor esi,esi` 0x1408c53bc, opcode checked) | `pes::TeamSheet`, `TeamAIController::GetTeamSheet()`; a team's tactics may set `support_range`, `defensive_line`, `compactness`, `attacking_style`, `build_up`, `attacking_area`, `positioning`, `defensive_style`, `containment_area`, `pressuring` |
| Sheet → defensive shape | Defensive Style, Defensive Line, Compactness into `FUN_1405f9eb0`/width; Pressuring = Conservative +2 m mark (`FUN_140623750`) | `CalculateDefensiveShape` reads the sheet bytes (was GF sliders mapped to 1..10, style hard-coded 0, Pressuring from the "Aggressive Defence" instruction) |
| Pressuring in slide aggression | `FUN_140a62180(ctx, side, 6) == 0` | `_SlideAggression` reads the sheet toggle |
| Per-concept enables | `team/subConcept.json` (registry 0xc4) `[reg+8+4·id] <= 0` skip | read from the VFS `constant_team.bin` → `subConcept.o` (blob u32 0 = list offset, 48 i32); the installed mod's all-1 list is what the loader returns |
| Concept selector rules | `FUN_140579840`: enable skip, attacking ids ≤ 0x20 only in possession and defending ids only out of it, one attacking concept at a time (every slot 6 is the false stub 0x141863350), gate (slot 1) fail → stop, start (slot 2), end test (slot 3) once older than 0.1 s (0x142588200), execute (slot 5) every tick; stop (`FUN_14057a3e0`) releases the concept's men | `RunConcepts`/`StopConcept` over the ported ids |
| Possession timer `MI+0x11a0` | frames since the possessing team changed, counted through stoppages | `possessionChange_ms` |
| Kick-off timer `FUN_140a74ec0` | in-play time since the newest ring-5 restart if it was a kick-off, else 0xffffffff | `inPlaySinceRestart_ms` reset on every restart but the throw-in (`PrepareSetPiece`) |
| TransitionChase (0x24), gate Defensive Style 0 | start `FUN_140575510` (3·fps lea, F6−10, F7−2, d² ≤ 100, goal-side ≤ 100°, inside ≥ 90°), second man `FUN_140575930` (list walk to d² 400, widest beyond \|u.y\|−15), continue `FUN_140575410`, execute `FUN_140575b60`: **from the chaser** (record +0x1c, not the carrier as the report text says) 8 m along own attack heading ±160° | `pes::TransitionChaseStart/Target`; the order is a run to the point |
| SideCounter (3), gate Attacking Style 0 | start `FUN_140540780` (a508, 3 s window, 6 s kick-off guard, facing ≤ 55°, ball own-frame −F6..−12 and \|z\| ≤ F7, goal-side count ≤ 2 (`cmp al,2`) with the −10/−14/F7−4 tests), runner target `FUN_14053f510` (opp line, ±(F7 + 50/100·(F1−F7))), supporters `FUN_140541010` (#0 ball+15 m ahead, ≥ 8 m upfield; #1 ball+12 m at 180°→A by ≤ 22°, men with y ≤ ball; #2 0°→A, men with y ≥ ball; −5 m keep bonus; re-picked every tick), end `FUN_1405404a0` (runner off the order, >2·fps: √d² > 1369 **as written** or \|Δx\| > 30; > 6·fps) | `pes::SideCounterStart/RunTarget/Supporters/End` |
| `T+0xa508 != 0` | master gate `FUN_1405807d0`: in play and in possession | `attackAllowed` |
| ForeCheckPress (0x21), Defensive Style 0 | start `FUN_1405747f0` (in play, ≤ 5·fps since the change, ball own-frame ≥ 0, 0xd not armed, press count); continue `FUN_140574710` (stop out of play, > 5 s, ball < −10; always on ≤ 2 s, then the press count); press count `FUN_140574200` decoded here: bearing ball→mate against the heading to our goal (`FUN_140a4db20`), radius 8 + 4·w, w = 0 ≤ 100°, 1 ≥ 170°, linear between; ≥ 3 (`cmp r12b,3`) inside with one within 70°; ball not held by the opponents → on | `pes::ForeCheckPress*`; it is PES's PRESS-state source: `IsPressing` (MATCH_UP man while it runs) feeds `BlockRadius_m` again |
| ForeCheckLineDown (0x22), Style 0 | start `FUN_140574ea0` / end `FUN_140574b00`, decoded here: no keeper hold, ball own-frame in [F0/−2.5, 27] (`FUN_140574a30`), \|T+0x89a0 − T+0x875c\| ≥ 3 (`FUN_140574ab0`, world vs own frame **as written**), and (level 0 or `FUN_140574940`: holder in LONG_PASS facing within 70° of our attack heading) | `pes::ForeCheckLineDown*` |
| RetreatBlockCreate (0x25), Style 1 | start `FUN_140575ea0` = in play and not end; end `FUN_140575db0` (both lines within 5 m of their bands, or back < 8 − F6 and mid < 18 − F6) | `pes::RetreatBlockCreateEnd` |
| Measured lines `T+0x89a0/a4` | `FUN_140687820` decoded here: mean world x of the line's men, each clamped ±4 m about a reference (line 0: own offside line; line 1: measured back + dir·\|band gap\|); none → the band | `TeamAIController::MeasuredLine` |
| Band push-up (built into GF's shape) | `FUN_140687fb0` decoded here: in play and neither 0x22 nor 0x25 running → x = min(measured back − 4, ball point − 6, −5) own frame; if x > back band B, every band += x − B (slot targets only) | `pes::BandPushUp_m` in `CalculateDefensiveShape`; `previousLineHeight` stays unshifted as PES's `T+0x875c` |

Every number is read from the install (`LoadTeamConceptConstants`); the code holds VAs, opcode checks and names only.
Heading convention as PES: 0 = +lateral, 90 = +x (`FUN_140a92d40` = atan2(dx, dz), `FUN_140a8f7c0` = (sin, cos)).

## GF code deleted

- `teamphilosophy.*` (Balanced/Gegenpressing/TikiTaka/ParkTheBus presets, `PressesOnPossessionLoss`,
  `PrefersShortPassing`, `GetTeamPressureDurationBonus_ms`, `AdaptOffsideTrapX`, the unused `IsSliderTactic`).
- `TeamInstructions` instruction bit-mask, its `Apply` slider offsets, presets, quick instructions, `Describe`,
  `Save`; only the mentality (= attack level) and `Load` remain.
- `TeamAIController` zone pressure (`ApplyZonePressure`, `ApplyTeamPressure`, `teamPressurePlayer` and timers), the
  on-win counter roll (GF `random()`), the dead "DISABLED" team-pressure block, the philosophy preset step of
  `UpdateTactics`, the ParkTheBus trap clamp. The dead slider keys are gone from `assets/fixtures/teams.sql`.
- `AITactics::ShouldStartZonePressure*`, `GetZonePressureDuration_ms`, `GetCounterTendency`, `ShouldLaunchCounter`,
  `GetCounterWindow_ms`, `GetSecondaryPressureRolePenalty`, `GetAttackingTerritory`; `PlayingStyles::GetPressingDistanceBias`.
- The `team_pressure`, `counter_attack`, `support_distance` sliders (base, multipliers, `TeamData` injection, fixture test).
  The Support Range byte now feeds the two former `support_distance` readers as `(SR−1)/9`.

## Ponytail approximations (each marked in code)

- SideCounter runner (`FUN_14053e9a0`, 728 insns): kept the spine — free (nearest opponent d² > 42.25) wide/forward man,
  distance to his channel point, Attacking Area factor 0.7/1.2 for men beyond 17 m — and lowest wins. Not reduced: run-style
  ×0.7/×1.3/×1.5 multipliers, `FUN_1405951b0` vetoes, the covered-channel flags (−17/−20) and the "within 11 m of the closest
  channel" slack (loaded, unused).
- SideCounter start: `FUN_140a4e720` forward-cone occupancy (150, 4 m) not reduced. End: the trailing overlap test
  (`FUN_1405a4830`/`FUN_140596c30`) not reduced. Execute: the carrier pass plan (`T+0x6f4`) is not fed to GF's carrier.
- Position categories `T+0x4c00` (INF in the reports) mapped onto GF roles: > 4 = LM/RM/LW/RW/AM/CF/SS, > 2 = not GK/CB/FB.
- MATCH_UP man = GF's designated team possession player. "Falling" = GF slide only. Carrier "kicking" = GF pass/shot function types.
  BASE_POSITION/MARK/PASS_COURSE_CUT = not keeper, not the engaging man, not on another concept's order.
- `T+0xa508`: in play and in possession only (stale-ball veto, keeper-hold restart path not modelled). Keeper holding = GF keeper is
  the ball retainer. KP_PUNTKICK has no GF state (never true).
- Instruction 0xd (GEGEN_PRESS) veto in TransitionChase: GF has no advanced instructions (their in-match writer is unfound), so it is
  never armed.
- Selector: the registry-15 stop flag, the difficulty row 0x21 cap, `MI+0x116c == 2` early-out and the member-state stop
  (`FUN_14057a160`, states 0x25/0x2c) have no GF counterpart.
- Times in seconds, not `⌊k·fps + 0.5⌋` frames.
- ForeCheckPress corridor veto (pass 3, decoded): `MA+0x3c+0x14·side` is written by `FUN_14045bfe0` = mean of the team's most
  advanced man and its deepest outfielder (`+0x38`/`+0x40`); GF takes the extremes of positions instead of PES's depth list.
- Press count (pass 3, decoded): candidates `FUN_140639830` (within 21.5 m, PRESS state or an assignment) and `FUN_140639a20`
  (sector assignment within 18–25 m) both reach past the count's largest radius (17 m), so only their one-man-per-sector rule
  (`FUN_140639600`) is left out. Lane bonus `FUN_140a53da0` → `FUN_140a53df0` ported: MATCH_UP man within 6.5 m, goal-side within 35°.
- ForeCheckLineDown uncovered carrier (pass 3, ported): `FUN_140574bb0` with the `FUN_140a4fbc0` cone (80°, 4 + 3·ramp m, own men incl.
  keeper). The holder's 4 m ball-list check is GF's ball retainer; PES's carrier→presser bearing table (`MA+0x228d4+0x16a8`) is a
  live bearing.
- Push-up: the −15 m variant behind `ctx+0x138` (writer not found) not taken; `FUN_14063fdc0`'s kick-prediction point = the ball.
- Measured line: PES's slot line `T+0x7b88` = GF's role line, keeper excluded (INF).

## Not ported, and why (each with the blocking function)

- ForeCheckLineUp (pass 3): **ported**. Start `FUN_140575300` / end `FUN_1405752a0`, `FUN_140575210`, containment `FUN_140574f40`
  (with the lane test), effect in `FUN_140682770` (after the push-up: back edge raised to min(ball + 5, B + 7, −5)). PES raises only
  the back band's lower edge; GF's bands hang off the line height, so the block follows (ponytail). Its other reader 0x140958b07
  skips `FUN_1409510d0` in PES's per-player action-candidate list, which GF does not have.
- ReverseSideCounter: still out. Pass 3 decompiled `FUN_14057ab30` (latch clear: possession, no concept index 2 running, and > 6 s
  (Counter) / 5 s since the change), `FUN_14057a580` (latch set), `FUN_140576f60`, `FUN_1405773f0`, `FUN_140576a60` (`d1.c` in the
  scratch dir). Blocked by `FUN_14057a580`'s callees `FUN_1405a58b0`, `FUN_140599d90`, `FUN_140a540a0`, `FUN_1404c8e60`,
  `FUN_1404f6110` (not reduced) and the vetoes' bodies (decompiled, not reduced in this pass).
- CounterAfterGkCatch: `FUN_14053f9f0` walks the free-area rectangles `MA+0x1cb10` (count `MA+0x1cb0c`), whose builder is not decoded.
- RetreatPressBack/BlockForwardPass/CrossBlockCreate, NetDefenceCentralSurroundingPress, PassCut, DoublePress, FastCounter,
  SupportBackOneTwo, SideLongitudinalOneTwo, DiagonalPostPlay, PossessionMfJoinDefenceLine: dead in PES (start = `0x141863350`).
- NetDefenceInducementIntoTheCenter, SealOffInducementToTheSides, SealOffSideSurroundingPress and the 20 live attacking patterns:
  start/execute bodies (`0x1405781d0`, `0x140578ef0`, `0x1405794e0`, the 0x1405465b0…0x140572000 family) not decoded; not reached
  in pass 3.
- Support Range §4.1's support-distance formula: OffBallPort (it reads `GetTeamSheet()`).
- The 5-byte sliders k1/k4 (no AI effect in PES).

## Verification

- `pes_team_concepts_test`: synthetic rules (gates, every TransitionChase/SideCounter start veto, trap point both ends, run target,
  supporters, end test, press count, fore-check start/continue, LineDown/Retreat gaps, push-up) + install relations. Mutations:
  removing the "inside" angle test fails `PesTransitionChase.EveryStartRuleVetoes`; removing the push-up −5 m cap fails
  `PesForeCheck.PushUpLiftsTheBandsToFourMetresBehindTheMenCapped`.
- Full gate (pass 3): 472/472, 2 standing skips; `SimMatch.SameSeedPlaysTheSameMatch` passes.
- Smoke, 20 min, seed 7, home 1 v away 2 (one seed, noisy):

| | base (pre-port) | pass 1 (68fed8e) | pass 2 | pass 3 |
|---|---|---|---|---|
| score | 3-1 | 1-0 | 4-1 | 1-0 |
| passes | 44-38 | 25-17 | 32-30 | 29-26 |
| shots (on target) | 14 (7) - 6 (2) | 11 (4) - 2 (1) | 22 (13) - 3 (2) | 13 (6) - 1 (0) |
| tackles / slides | 32-57 / 57-104 | 26-34 / 84-51 | 28-22 / 88-58 | 44-29 / 40-62 |
| fouls | 19-28 | 8-18 | 13-20 | 13-20 |
| possession | 42%-58% | 22%-78% | 34%-66% | 30%-70% |
| forced stalemate ticks | 72303 | 68285 | 63562 | 59640 |
| concept starts (probe, not committed) | — | SideCounter 5 | SideCounter 17, ForeCheckPress 134, TransitionChase 2, LineDown 0, Retreat 0 (both fixture sheets are Style 0) | not probed |
