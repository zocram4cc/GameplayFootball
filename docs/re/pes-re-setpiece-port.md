# Set-piece team AI port (SetpiecePort, finished by the orchestrator)

What of PES 2021's set-piece team AI is ported into GodotFootball, which PES rule each piece implements, and where it
cuts a corner. Sources: `pes-re-setpiece-kicker.md` (taker, delivery, wall), `pes-re-setpiece-choreography.md` §11
(placement, wall jump, post men, box attackers) and §12 (take timing). Commits on main: d6d5849, b56316d, ffdbb0d,
3bb13d2, 7edf8ea.

## 1. Ported

|GF code|PES rule|Source|
|---|---|---|
|`SetPieceLaws::MayTakeRestart`, wired on top of `buffer.startTime` in `referee.cpp`|The taker waits fps·3.5 (corner), fps·3 (free kick) or fps (penalty) logic frames once the restart is ready. Kick-off, throw-in and goal kick are not gated.|choreography §12 (`ThinkUnitSetPlay` `FUN_142135c50`); `pessetpiecetaker.*`|
|`pes::LoadSetPieceTakerConstants`, required by `Match`|The three multipliers are read from `.rdata` (0x14259bde8, 0x14258c264, 0x14256ae1c). The first version's fallback to built-in values when no exe is present was removed: a Match refuses to build without them, like every other PES loader.|choreography §12|
|`pessetpieceteam.*` (pure, `LoadSetPieceTeamConstants` from the exe)|Taker waterfall, 16 km/h jog clamp and per-kind time budgets, wall size/placement/spacing table, wall jump/press split (at most one presser), box-attacker count `4 + min(attack level, 2)`, attack-level rungs.|kicker §1, §3; choreography §11.3, §11.5–11.7|
|`TeamAIController::PrepareSetPiece`|Free kick: keeper (near his own goal) → own-half back within 65 m → long range → suitability. Corner: best suitability, never the keeper or a centre-forward. Penalty: suitability. Goal kick: the keeper. Players move toward their marks only as far as a 16 km/h jog covers within the kind's budget, instead of teleporting. Law-16 box push at goal kicks and own-box free kicks. Near-post man, far-post man at attack level 0, box-attacker count. Free-kick wall placed with PES's size and 0.5 m spacing at 9.15 m.|kicker §1; choreography §11.2–11.7|

Constants: every value is read at runtime from the VAs listed at the top of `src/sim/onthepitch/pessetpieceteam.cpp`
(jog 16000/3600, the budgets 20/300/7/10/13/8/12/3/2, wall 9.15/0.5/1.3, the wall-size distance bands 25/29/32/36/40).

## 2. Approximations (`// ponytail:` in code)

- **Wall-size cap and its branch thresholds** are instruction immediates that cannot be read at runtime; named
  constants with provenance (`pessetpieceteam.hpp`). Ceiling: a build of the exe with different immediates is not
  followed.
- **Attack level** ignores PES's plan-command shift (`T+0x210`); only the level itself is used. Ceiling: corner
  attacker counts can differ while a plan command is active.
- **Corner spots**: PES's authored `positionCK_N_V` spots are not shipped in the data, so GF's own corner positions
  stand in for the attackers outside the decoded roles.
- **Suitability**: PES rates six set-piece kinds per player (`playerdata+0x96+kind`); GF's nearest stats stand in.

## 3. Verification

- `pes_setpiece_team_test`: 13 tests, all green; mutation-checked (the jog clamp, the jump/not-jump split, the single
  presser, the wall-size table, the attacker count, the attack-level rungs and the keeper-first free kick each go red
  when broken). `pes_setpiece_taker_test`: 8 tests.
- Smoke (agent): 20+ restarts across all six kinds with takers listed; goal kicks to the keeper. Smoke on main (seed 7,
  20 min): match completes, 0–3, shots 19–29.
- Full gate on main: see the merge commit.

## 4. Open

1. **The wall's jump/press decision is not wired.** It is tested in `pessetpieceteam`, but the wall men's behaviour
   does not call it yet: GF's wall still uses its own jump. Needs a per-wall-man command hook in the free-kick
   defence (choreography §11.5's trigger frame `⌊fps·22/60+0.5⌋ − ⌊9.15/v⌋` before the kick).
2. Zone template and marker assignment at corners (choreography §11 open list) are not decoded, so not ported.
3. The corner variant (which corner routine runs) is not decoded.

## 5. AI-map row

`|Set-piece AI (placement, marking at corners/FKs, walls)|**ported**: taker waterfall, jog-to-marks budgets, box push,
post men, box-attacker count, wall placement; take timing; wall jump decision tested but not wired; zone template,
marker assignment, corner variant open|...|pessetpieceteam.cpp, TeamAIController::PrepareSetPiece|`
