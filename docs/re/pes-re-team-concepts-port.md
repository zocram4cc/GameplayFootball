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

Every number is read from the install (`LoadTeamConceptConstants`); the code holds VAs, opcode checks and names only.
Heading convention as PES: 0 = +lateral, 90 = +x (`FUN_140a92d40` = atan2(dx, dz), `FUN_140a8f7c0` = (sin, cos)).

## GF code deleted

- `teamphilosophy.*` (Balanced/Gegenpressing/TikiTaka/ParkTheBus presets, `PressesOnPossessionLoss`,
  `PrefersShortPassing`, `GetTeamPressureDurationBonus_ms`, `AdaptOffsideTrapX`, the unused `IsSliderTactic`).
- `TeamInstructions` instruction bit-mask, its `Apply` slider offsets, presets, quick instructions, `Describe`,
  `Save`; only the mentality (= attack level) and `Load` remain.
- `TeamAIController` zone pressure (`ApplyZonePressure`, `ApplyTeamPressure`, `teamPressurePlayer` and timers), the
  on-win counter roll (GF `random()`), the dead "DISABLED" team-pressure block, the philosophy preset step of
  `UpdateTactics`, the ParkTheBus trap clamp.
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

## Not ported, and why

- The 46 − 2 other SubConcepts (attack patterns, ForeCheck*, Retreat*, NetDefence*, SealOff*): ForeCheckPress's effect readers
  (`FUN_140536fc0`, `FUN_1406350c0`) are not reduced; ForeCheckLineDown/RetreatBlockCreate act only through the band push-up of
  `FUN_140687fb0`, which GF's shape does not have; the attack patterns belong to attack-decisions. 11 ids are dead in PES anyway.
- CounterAfterGkCatch: its area search `FUN_14053f9f0` walks the analysis object's free-area rectangle list (`MA+0x1cb10`), which GF
  does not build. ReverseSideCounter: its counter-window latch `FUN_14057a580`/`FUN_14057ab30` and vetoes are only partly reduced.
- Support Range §4.1's PES support-distance formula: belongs to the off-ball port (agreed with OffBallPort; it reads `GetTeamSheet()`).
- The ActionPress state: GF's `BlockRadius_m(pressing)` was fed by GF's zone-pressure man; with zone pressure gone it is `false`.
- The 5-byte sliders k1/k4 (no AI effect in PES).
- `assets/fixtures/teams.sql` still carries inert `team_pressure`/`counter_attack`/`support_distance` keys (no reader).

## Verification

- `pes_team_concepts_test`: synthetic rules (gates, every TransitionChase/SideCounter start veto, trap point both ends, run target,
  supporters, end test) + install relations (loader against the user's exe and VFS). Mutation: removing the "inside" angle test fails
  `PesTransitionChase.EveryStartRuleVetoes`.
- Full gate: 465/465, 2 standing skips; `SimMatch.SameSeedPlaysTheSameMatch` passes.
- Smoke, 20 min, seed 7, home 1 v away 2 (one seed; this sim is noisy and both runs show ~70k forced stalemate ticks):

| | before | after |
|---|---|---|
| score | 3-1 | 1-0 |
| passes | 44-38 | 25-17 |
| shots (on target) | 14 (7) - 6 (2) | 11 (4) - 2 (1) |
| tackles / slides | 32-57 / 57-104 | 26-34 / 84-51 |
| fouls | 19-28 | 8-18 |
| possession | 42%-58% | 22%-78% |
| forced stalemate ticks | 72303 | 68285 |
| concept starts (probe, not committed) | — | SideCounter 4 (home), 1 (away); TransitionChase 0 |

TransitionChase is narrow by design (wide carrier in our attacking third within 3 s); PES's live capture showed 1 episode in a half.
