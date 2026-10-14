# PES 2021 match AI: the map

The index of every PES AI system, how far it is reverse-engineered, where the evidence lives and which
GodotFootball (GF) code it replaces. Player and team AI (positional, tactical and decisional) are the owner's
documentation priority: a system is not ready to port until its section here points at a report that names
every input, constant and data file it uses.

Status words: **OBS** = decoded and observed (decompile and/or live); **partial** = structure known, pieces open;
**open** = not found; **wrong** = an earlier identification was retracted (see the report).

## 1. Team AI (the team brain)

|System|Status|Evidence|GF counterpart (to replace)|
|---|---|---|---|
|Team-brain class inventory|OBS|`pes-re-team-plans.md` §1|`teamAIcontroller.cpp`|
|Team shape: formation slot → target position|**OBS: line chain closed** (final line median error 0.000 m live, n=3950; midfield band 0.009 m); slot targets live-described; midfield depth/spacing code open|`pes-re-defensive-shape.md` §1–2, §14|`TeamAIController::GetAdaptedFormationPosition` depth/width terms, `ApplyOffsideTrap` (PES arms it only with `T+0xe4`)|
|Pressing and marking (who presses, who marks whom)|**OBS**: priority gate, MATCH_UP = slot marking the carrier, second-man press reasons, mark passes 0–7 (live: nearest 57%, top-2 78%, 95% persistent), mark-spot distance, retreat flag; mark-spot blend arithmetic, cover chain, Pass Course Cut, Delay/Press bodies open|`pes-re-defensive-shape.md` §3–5, §14|`CalculateManMarking`, `CalculateMarkingQuality`, `AddDefensiveComponent`, `ApplyTeamPressure`/`ApplyZonePressure`|
|Line/marking order (re-sort when players overtake)|OBS|`pes-re-attack-decisions.md` §9 (`FUN_14045e3db`)|none (GF has no explicit order list)|
|Team plans / tactics sliders → behaviour|**OBS**: sheet layout, slider formulas, toggle→plan gates and the plan selector, instruction storage (17-value enum, 4 per-player) and timing; live 7,298 samples, no contradiction; ~30 read sites and a few writers open|`pes-re-tactics.md`; `pes-re-team-plans.md` §5–6|`baseTeamTactics`/`teamTacticsModMultipliers` in `teamAIcontroller.cpp`, `teamphilosophy.cpp`, `teaminstructions.cpp`|
|Attributes and playing styles in team decisions|partial|`pes-re-team-plans.md` §5–6; `pes-re-tactics-clock.md`|`teaminstructions.cpp`, `teamphilosophy.cpp`|
|Offside line keeping and detection|**ported**: the PES line (min of halfway, the second-rearmost defender's rear surface and the current ball, clamped to the goal line), the attacker test with the torso margin and 0.15 m tolerance, categories 0-3 (only 0-2 punishable, 2 via the delayed whistle), evaluated once per ball event with the staged-restart gate; no per-limb points; the AI's offside discipline untuned|`pes-re-offside.md`; `pes-re-duels-fouls.md` §5.1–5.2 (involvement, commit); `pes-re-offside-port.md`|`offsiderule.cpp`, `AI_GetOffsideLine`, `Referee::BallTouched`, `Referee::CommitPendingOffside`|
|Set-piece AI (placement, marking at corners/FKs, walls)|**ported**: taker waterfall, jog-to-marks budgets, box push, post men, box-attacker count, wall placement; wall jump decision tested but not wired; zone template, marker assignment, corner variant open|`pes-re-setpiece-choreography.md` §11, `pes-re-setpiece-kicker.md`; `pes-re-setpiece-port.md`|`pessetpieceteam.cpp`, `TeamAIController::PrepareSetPiece`|
|Set-piece take timing|**ported** (fps multiples: corner 3.5, FK 3.0, penalty 1, read from the exe; kick-off/throw-in/goal kick ungated)|`pes-re-setpiece-choreography.md` §12; `pes-re-setpiece-port.md`|`SetPieceLaws::MayTakeRestart`, `referee.cpp`|
|Tactics sliders / advanced instructions → behaviour|**OBS** (see the row above); defensive instructions only out of possession, Gegenpress on 0–3 frames after a loss for 0.7–5.7 s|`pes-re-tactics.md`|`teaminstructions.cpp` (7-bit mask: 4 of its bits are toggles in PES)|
|Transitions (counter-press, regroup, quick counter)|**partial, OBS live**: possession timer `MI+0x11a0`, kick-off timer `FUN_140a74ec0`, TransitionChase/fore-check (5 s limit), regroup concepts RetreatBlockCreate/ForeCheckLineDown hold the block deep; counter-attack trigger (`T+0xa508`) open|`pes-re-transitions.md`|`teamAIcontroller.cpp` `ShouldStartZonePressureOnLoss`, `ApplyZonePressure`, `ShouldLaunchCounter` (random)|
|In-match management (mentality by score/clock, substitutions)|partial: attack level `T+0xb8a4` writer `FUN_1404f8350`, five levels map 1:1 to GF mentality, no 80′/85′ rules in PES; in watch mode a pad branch fixes it at 2; substitutions open|`pes-re-tactics.md`|`matchmentality.cpp`, `substitutions.cpp`|

## 2. Player AI (the individual brain)

|System|Status|Evidence|GF counterpart (to replace)|
|---|---|---|---|
|Carrier think units (25 kinds), cadence, adoption|OBS; **partially ported**: cadence, shoot zone + just-shoot/box/header, openness ladder, PassForward tiers, plan order + Breakthrough/MiddleShot/Cross/Long rolls; 20 units, dribble cost search, shield branch, delay tables not; shoot-zone live sample off by 0.9 m (open)|`pes-re-carrier-ai.md` §1–5; `pes-re-carrier-port.md`|`pescarrier.cpp`; `elizacontroller.cpp` ball-carrier branches|
|Perception layer (image units every unit reads)|OBS|`pes-re-carrier-ai.md` §4|`AIsupport/mentalimage*`|
|Cross / early cross / through-ball units and choosers|OBS|`pes-re-attack-decisions.md` §2|`elizacontroller.cpp` pass selection|
|Dribble sub-kind and shielding|OBS|`pes-re-attack-decisions.md` §2.7|`elizacontroller.cpp` dribble|
|Off-ball runs and support positioning|**OBS**: one run manager, 11 selectors in fixed order on the team's alternate frame, caps from `selectorVision.o` by attack level (Tiki-Taka cuts some), no skill/style weighting, minimum run times, support grid (32 layers, cost search, designated supporter); live caps and cadence match. Chance target builders and support-target live check open|`pes-re-off-ball.md` (corrects attack-decisions §3); `pes-re-team-plans.md` §3|`elizacontroller.cpp` support runs, `AIsupport/`, `teamAIcontroller.cpp` attacking positioning|
|Attribute, skill and style bindings in decisions|partial|`pes-re-carrier-ai.md` §6; `pes-re-attack-decisions.md` §4|`playerskills.*`|
|Difficulty → AI capability table|OBS|`pes-re-carrier-ai.md` §7|none|
|Fatigue effect on AI work rate|OBS, **ported**|`pes-re-stamina-drain.md`; `pesstamina.cpp`|`pesstamina.cpp` (tank/drain/breath/half-time/A5/injury; deletes GF fatigue)|
|Goalkeeper: positioning, shot response, catch/punch/parry|**OBS** positioning (live median 0.005 m defending, 0.000 m in possession), rush race and limits, one-on-one zone, pre-save set; save choosers decompiled not reduced; deflect point ported|`pes-re-goalkeeper.md` §1–3, §10; `pes-re-keeper-port.md`|`GoalieDefaultStrategy::RequestInput`, keeper branch of `elizacontroller.cpp`, `ApplyKeeperRush`, `peskeeper.cpp`|
|Goalkeeper: crosses, distribution, one-on-ones|**OBS** crosses (claim gates), one-on-ones; distribution kind partial; `+0x22e4c` is per-player sorted distance lists, not a keeper table; **the shipped after-catch port has 4 errors** (§10.6)|`pes-re-goalkeeper.md` §4–6, §10; `pes-re-keeper-port.md`|`peskeeper.cpp`|
|Defensive individual decisions (tackle, slide, block, intercept)|**OBS + live** (§12: tackle 6/6 gates, slide, blocks 8/10); one 17-entry first-true reaction table `FUN_140a06dd0`; tackle ≤3.0 m/22.5°; slide `4+0.8·L`; block 1.3/3.0/7 m; no RNG; **stock and the 4cc mod differ sharply** (`check_SaftyRate` 0.4/0.5 vs 2.5/2.6): the last-man veto is the mod's, not a rule|`pes-re-defender-decisions.md`|`elizacontroller.cpp` tackle/duel branches, `AIsupport/`|
|Advantage attack-chance test (`FUN_14050f7e0`)|partial (structure OBS, three tables INF)|`pes-re-duels-fouls.md` §3.1|`pesjudge.cpp` distance-only shortcut|
|Receiving the ball (first touch, turn, let it run)|partial: 95 live first touches (directional, median 58° off the body); interception touch and re-touch lockouts need per-frame capture|`pes-re-transitions.md`|`ElizaController::_TrapCommand`, `humanoid.cpp` trap path, receiver lead|
|Aerial duels and headers|open|—|`humanoid.cpp` jump/header|
|Animation selection and blending (which clip per action)|partial (anim-blend, locomotion: clip-driven speed)|`pes-re-anim-blend.md`, `pes-re-locomotion.md`, `pes-re-sim-motion-mapping.md`|`animcollection.cpp`, `humanoid.cpp` SelectAnim|
|Player-vs-player contact (shoulder, balance, falls)|open (only the foul judgement is ported)|`pes-re-duels-fouls.md` §2|`match.cpp` collision, `humanoid.cpp`|
|RNG slot per system|partial (generator ported; slots by convention)|`pes-re-rng.md` §3|`pesrandom.cpp` callers|

## 3. What a report must contain before a port starts

For each decision: the trigger and cadence; every input (perception unit, player/team field, data-file field)
with its address; every constant with its source (exe `.rdata` VA, instruction immediate, or VFS `.o`/`.json`
field); the random draws and which generator slot; the outputs (command, target, timing); the live check that was
run; and the killed hypotheses. "Partial" rows list the missing items in their report's open list.
