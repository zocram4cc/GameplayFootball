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
|Team shape: formation slot → target position|OBS; **line height, team length, width/lateral shift decoded** (live line median 0.38 m); line adjust passes open|`pes-re-team-plans.md` §2 (correction in `pes-re-defensive-shape.md` §11); `pes-re-defensive-shape.md` §1–2|`TeamAIController::GetAdaptedFormationPosition` depth/width terms, `ApplyOffsideTrap`|
|Pressing and marking (who presses, who marks whom)|partial: state priority gate, one MATCH_UP chaser (8/10 m keep, 10–15 m lane search), ≤2 covers, mark order and hand-off delay OBS (live); mark passes 2–7, mark-spot, PRESS/SAND/DELAY writers open|`pes-re-team-plans.md` §4; `pes-re-defensive-shape.md` §3–5|`CalculateManMarking`, `CalculateMarkingQuality`, `elizacontroller.cpp` defensive branches|
|Line/marking order (re-sort when players overtake)|OBS|`pes-re-attack-decisions.md` §9 (`FUN_14045e3db`)|none (GF has no explicit order list)|
|Team plans / tactics sliders → behaviour|partial|`pes-re-team-plans.md` §5–6; `pes-re-tactics-clock.md`|`teaminstructions.cpp`, `teamphilosophy.cpp`|
|Attributes and playing styles in team decisions|partial|`pes-re-team-plans.md` §5|`teamphilosophy.cpp`|
|Offside line keeping and detection|**OBS** (writer `FUN_140a7b7a0`, line `FUN_14045cf30`; live 2,498 checks, 0 contradictions); flag→whistle live call open|`pes-re-offside.md`; `pes-re-duels-fouls.md` §5.1–5.2 (involvement, commit)|`offsiderule.cpp`, `AI_GetOffsideLine`, `Referee::BallTouched`|
|Set-piece AI (placement, marking at corners/FKs, walls)|partial; taker/delivery/jog-to-marks/wall/post-men OBS; zone template, marker assignment, corner variant open|`pes-re-setpiece-choreography.md` §11, `pes-re-setpiece-kicker.md`, `pes-re-throwin-choreography.md`|`teamAIcontroller.cpp` `PrepareSetPiece`|
|Set-piece take timing|OBS (fps multiples: corner 3.5, FK 3.0, penalty 1; kick-off by KickoffReceiver, 28 f); port in SetpiecePort|`pes-re-setpiece-choreography.md` §12|`referee.cpp` restart release|
|Tactics sliders / advanced instructions → behaviour|partial (Defensive Line, Compactness feed the line; other sliders and instructions untraced)|`pes-re-defensive-shape.md` §8; `pes-re-team-plans.md` §5|`teaminstructions.cpp`, `teamphilosophy.cpp`|
|Transitions (counter-press, regroup, quick counter)|open|—|`teamAIcontroller.cpp`|
|In-match management (mentality by score/clock, substitutions)|open|—|`matchmentality.cpp`, `substitutions.cpp`|

## 2. Player AI (the individual brain)

|System|Status|Evidence|GF counterpart (to replace)|
|---|---|---|---|
|Carrier think units (25 kinds), cadence, adoption|OBS|`pes-re-carrier-ai.md` §1–5|`elizacontroller.cpp` ball-carrier branches|
|Perception layer (image units every unit reads)|OBS|`pes-re-carrier-ai.md` §4|`AIsupport/mentalimage*`|
|Cross / early cross / through-ball units and choosers|OBS|`pes-re-attack-decisions.md` §2|`elizacontroller.cpp` pass selection|
|Dribble sub-kind and shielding|OBS|`pes-re-attack-decisions.md` §2.7|`elizacontroller.cpp` dribble|
|Off-ball run scorers (chance, counter, second line, diagonal, pull-away, overlap, post play)|OBS|`pes-re-attack-decisions.md` §3; `pes-re-team-plans.md` §3|`elizacontroller.cpp` support runs|
|Attribute, skill and style bindings in decisions|partial|`pes-re-carrier-ai.md` §6; `pes-re-attack-decisions.md` §4|`playerskills.*`|
|Difficulty → AI capability table|OBS|`pes-re-carrier-ai.md` §7|none|
|Fatigue effect on AI work rate|OBS, **ported**|`pes-re-stamina-drain.md`; `pesstamina.cpp`|`pesstamina.cpp` (tank/drain/breath/half-time/A5/injury; deletes GF fatigue)|
|Goalkeeper: positioning, shot response, catch/punch/parry|partial; **deflect point ported**|`pes-re-goalkeeper.md` §1–3; `pes-re-keeper-port.md`|`elizacontroller.cpp` keeper branches; `peskeeper.cpp`|
|Goalkeeper: crosses, distribution, one-on-ones|partial; **after-catch carry/step ported**, per-keeper 11-entry table (`team+0x22e4c`) open|`pes-re-goalkeeper.md` §4–6; `pes-re-keeper-port.md`|`peskeeper.cpp` (GF nearest-player pick stands in)|
|Defensive individual decisions (tackle, slide, block, intercept)|**OBS** structure: one 17-entry first-true reaction table `FUN_140a06dd0` per frame; tackle ≤3.0 m/22.5° into the carrier's touch, no tackle as last man; slide only COM ≥300, `4+0.8·L` m; block 1.3/3.0/7 m; no RNG; jockey = DELAY state; no AI shoulder/shirt-pull. Live tackle check open|`pes-re-defender-decisions.md`|`elizacontroller.cpp` tackle/duel branches, `AIsupport/`|
|Advantage attack-chance test (`FUN_14050f7e0`)|partial (structure OBS, three tables INF)|`pes-re-duels-fouls.md` §3.1|`pesjudge.cpp` distance-only shortcut|
|Receiving the ball (first touch, turn, let it run)|open|—|`elizacontroller.cpp`, humanoid touch|
|Aerial duels and headers|open|—|`humanoid.cpp` jump/header|
|Animation selection and blending (which clip per action)|partial (anim-blend, locomotion: clip-driven speed)|`pes-re-anim-blend.md`, `pes-re-locomotion.md`, `pes-re-sim-motion-mapping.md`|`animcollection.cpp`, `humanoid.cpp` SelectAnim|
|Player-vs-player contact (shoulder, balance, falls)|open (only the foul judgement is ported)|`pes-re-duels-fouls.md` §2|`match.cpp` collision, `humanoid.cpp`|
|RNG slot per system|partial (generator ported; slots by convention)|`pes-re-rng.md` §3|`pesrandom.cpp` callers|

## 3. What a report must contain before a port starts

For each decision: the trigger and cadence; every input (perception unit, player/team field, data-file field)
with its address; every constant with its source (exe `.rdata` VA, instruction immediate, or VFS `.o`/`.json`
field); the random draws and which generator slot; the outputs (command, target, timing); the live check that was
run; and the killed hypotheses. "Partial" rows list the missing items in their report's open list.
