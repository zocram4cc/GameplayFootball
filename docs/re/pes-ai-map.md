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
|Team shape: formation slot → target position|OBS|`pes-re-team-plans.md` §2 (`basePosition.json`)|`teamAIcontroller.cpp` formation/position code|
|Pressing and marking (who presses, who marks whom)|OBS|`pes-re-team-plans.md` §4|`elizacontroller.cpp` defensive branches, `AIsupport/`|
|Line/marking order (re-sort when players overtake)|OBS|`pes-re-attack-decisions.md` §9 (`FUN_14045e3db`)|none (GF has no explicit order list)|
|Team plans / tactics sliders → behaviour|partial|`pes-re-team-plans.md` §5–6; `pes-re-tactics-clock.md`|`teaminstructions.cpp`, `teamphilosophy.cpp`|
|Attributes and playing styles in team decisions|partial|`pes-re-team-plans.md` §5|`teamphilosophy.cpp`|
|Offside line keeping and detection|**open** (the earlier writer is **wrong**)|`pes-re-duels-fouls.md` §5.3; `pes-re-attack-decisions.md` §9|`offsiderule.cpp`|
|Set-piece AI (placement, marking at corners/FKs, walls)|partial; taker/delivery/jog-to-marks/wall/post-men OBS; zone template, marker assignment, corner variant open|`pes-re-setpiece-choreography.md` §11, `pes-re-setpiece-kicker.md`, `pes-re-throwin-choreography.md`|`teamAIcontroller.cpp` `PrepareSetPiece`|
|Set-piece take timing|partial (kick-off 28 f OBS, corners 3.5 s OBS; FK/corner writers open)|`pes-re-setpiece-choreography.md` §9.2; `pes-re-setpiece-kicker.md` §2|`referee.cpp` restart release|

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
|Fatigue effect on AI work rate|OBS|`pes-re-stamina-drain.md`|none (port running: StaminaPort)|
|Goalkeeper: positioning, shot response, catch/punch/parry|partial; **deflect point ported**|`pes-re-goalkeeper.md` §1–3; `pes-re-keeper-port.md`|`elizacontroller.cpp` keeper branches; `peskeeper.cpp`|
|Goalkeeper: crosses, distribution, one-on-ones|partial; **after-catch carry/step ported**, per-keeper 11-entry table (`team+0x22e4c`) open|`pes-re-goalkeeper.md` §4–6; `pes-re-keeper-port.md`|`peskeeper.cpp` (GF nearest-player pick stands in)|
|Defensive individual decisions (tackle choice, jockey, intent flag `ai(B)+0x134`)|**open**|`pes-re-duels-fouls.md` §2|`elizacontroller.cpp` tackle/duel|
|Advantage attack-chance test (`FUN_14050f7e0`)|partial (structure OBS, three tables INF)|`pes-re-duels-fouls.md` §3.1|`pesjudge.cpp` distance-only shortcut|

## 3. What a report must contain before a port starts

For each decision: the trigger and cadence; every input (perception unit, player/team field, data-file field)
with its address; every constant with its source (exe `.rdata` VA, instruction immediate, or VFS `.o`/`.json`
field); the random draws and which generator slot; the outputs (command, target, timing); the live check that was
run; and the killed hypotheses. "Partial" rows list the missing items in their report's open list.
