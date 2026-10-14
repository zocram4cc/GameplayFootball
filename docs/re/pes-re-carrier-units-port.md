# PES carrier units port: report

## Current state
- Branch: worktree `the worktree`, detached from main `0f41af0`.
- Commits kept: `aae95ad` (pure dribble search and threat, tests), and now `1ace4b5` (throw-in ball hold, regression test). The dribble wiring stays reverted (main `f9e9e48`).
- Gate: 609 tests, 608 pass, 2 standing skips, **1 failure**: `SimMatch.AIManagerSwitchGovernsTactics` (see Decision).

## Throw-in taker never takes the throw: root cause
Signature: `piece 5` (throw-in), `buf active 1 prepared 1 started 1`, ball at rest, taker 0.3 m away, forced every 3 s.

1. The referee exits the restart only when `taker->TouchAnim() && !TouchPending()` (referee.cpp, set-piece block).
2. The taker's `ShortPass` is requested every tick (elizacontroller.cpp throw-in branch) but never starts a touch.
3. `Humanoid::GetBestCheatableAnimID` (humanoid.cpp ~3100–3106) refuses any touch whose predicted ball lies beyond `pitchHalfH + lineHalfW + 0.11 = 34.17 m` on either axis.
4. During the restart hold, the ball is bumped off its spot. Probe at restart start (temporary, removed):
   - 19.8 s: ball = spot (22.25, 34.00). Throw works.
   - 38.6 s: ball = spot (33.45, −34.00). Throw works.
   - 106.7 s: spot (39.25, 34.00), ball (36.31, 38.28), 4.3 m outside. Stall.
   - 173.6 s: spot (48.50, 34.00), ball (47.59, 34.51). Stall.
5. A rejected touch means no touch anim, so the restart never ends.

Also seen (probe, removed): at 107 s the taker faced about 180° from his throw aim. I added a turn to the aim (taker look along the throw); with the hold in place the test passes without it (ablation), so it was removed. PES's yaw-warp (pes-re-throwin-choreography.md §4.2) is not ported; the difference stays open.

## Fix (commit `1ace4b5`)
`referee.cpp`: while a throw-in is prepared and not yet started, return the ball to the restart spot whenever it has strayed more than 0.05 m (2-D). `ponytail:` GF tolerance and teleport, not PES's ball hold. Other restarts are untouched: applied to all restarts, it created a completed-restart stall (below).

## Test (commit `1ace4b5`)
`SimMatch.AThrowInTakerTakesTheThrow` (tests/sim/sim_match_test.cpp): seed 7, 20-minute setup, 200 s window; asserts the longest wait of a started throw-in ≤ 10 s (ponytail: GF bound; PES CPU taker throws ~1.5 s after ready, EV-TI-0015).
- RED (no hold): longest wait 26390 ms.
- Mutation (hold disabled, turn removed): 93320 ms.
- GREEN (hold on): passes, ~25 s.
- `MakeSetup` extracted from `Play` so both use one setup. Behaviour-neutral.

## Smoke: seed 7, 20 minutes, `[stalemate] forced N`
- Pre-change baseline `143c0fd`: 59640 (finishes).
- Base `0f41af0`: does not finish within 900 s; throw-in stall at 107 s never ends.
- Hold for all restarts: 90715 (finishes; a goal-kick stall class appears, see below).
- Hold for throw-ins only (committed): **59520**, finishes in 240 s, exit 0.

## Gate
Command as specified. 609 tests, 2 standing skips, 1 failure:
- `SimMatch.AIManagerSwitchGovernsTactics` (sim_match_test.cpp:356, `home must still trail`: 3 vs 3).
- Ablation: with the hold disabled the test passes (55.6 s). So the hold alone changes that match's path.
- The test loops `Process()` until the AI attacks or the game ends (not the 1-minute window its comment describes), so its outcome depends on the path. Home equalises in the new path.
- Other tests I was told to watch pass: `SameSeedPlaysTheSameMatch`, `FullMatchRunsToTheWhistle`, `SlidesAreCountedPerCommitmentNotPerContact`.

## Decision for Main
1. Keep the throw-in hold and change `AIManagerSwitchGovernsTactics` to match its documented 1-minute window (test edit, needs approval), or
2. Revert `1ace4b5`, which brings the throw-in stall back (gate red on the stall instead).
I recommend 1 after checking that the test's intent holds under the 1-minute bound; I have not run that check.

## Not done / out
- Completed-restart stall (restart `started`, ball left at rest, forced stalemate): same class as the pre-change baseline's dominant stall (kick-off, piece 1, 177 lines). Goal-kick variant (piece 2, 162 lines) appears after the change. Not diagnosed; may be downstream of a changed path.
- PES's throw-in yaw-warp and aim loop (THROWIN_LOOP phase 1) not ported.
- Dribble search wiring stays reverted (per Main); not re-wired.
- `PlayingStyles` dribble helpers stay in place, now uncalled (unchanged from earlier decision).
