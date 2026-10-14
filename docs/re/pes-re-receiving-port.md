# Receiving port (ReceivingPort), state at close-out

Worktree `the worktree`.
- Commits: `1c79cf8` (the first port) plus the second commit from this pass.
- Source report: `docs/re/pes-re-receiving.md`.
- New decompiles: in `the scratch dir` (`routes.c`, `slots.c`, `carrier.c`, `touch.c`, `ctors.c`).

## 1. The pass-accuracy regression (66/60% → 27/26%): root cause found and fixed

**How I tested it.** A/B runs, each a 20-minute match, seeds 7, 8 and 9. Accuracy is the average over the three seeds:

| variant | accuracy |
|---|---|
| base (pre-port) | ~70% |
| first port | ~35% |
| arbitration off | ~42% |
| PASS_GET off | ~45% |
| old duel gate restored | ~40% |
| lockout off | ~39% |
| kind-0 command keeping its input direction | ~35% |
| own-trap scan fixed | ~36% |
| **reaction order reverted (receive before tackle/slide)** | **~58%** |

**Cause.** The defender port's `ElizaController::_Carrier()` fell back to `_oppPlayer` (the best-possession opponent) for any ball. That included the player's own team's pass. Once PES's order put tackle and slide ahead of the receive, a receiver's slide or tackle predicate fired on his own team's incoming pass and pre-empted the trap.

**PES evidence.** `FUN_140a50e00` (decompiled into `carrier.c`) picks the challenge target this way:
- the newest event's owner, if he is an opponent;
- **nobody (0xff) when the ball is my own team's**;
- for a loose ball, the other team's T+0x78.

It then checks the candidate is valid: his 0.2 s path capsule (1 m) holds the ball, or the ball is nearer him than me + 0.64, or within 3.5 m of him, or within 4.2 m when I am in PRESS.

**Fix.** Ported as `pes::ChallengeCandidate` / `pes::ChallengeTargetValid`. The constants are read from the exe, and there are tests plus an install relation test.

**After the fix,** with PES's order kept:

| seed | accuracy | clean |
|---|---|---|
| 7 | 77% / 54% | 61% / 40% |
| 8 | 76% / 75% | 68% / 55% |
| 9 | 59% / 81% | 31% / 77% |

With the BallFollow approach added on top: seed 7 76/64, seed 8 66/71, seed 9 56/72. Forced-stalemate ticks fell from 60–150k to 6.7–9.5k.

The final code (with the arbitration correction below) has run seed 7 only: 80/70, clean 60/58, forced 45.6k. Seeds 8 and 9 were not run because of the stop order.

## 2. Second-pass corrections and new ports (this commit)

- **Arbitration was wrong in `1c79cf8`.**
  - Raw `0x140931d98..e6b` shows it is not a defender-interception rule.
  - It runs only for **my own team's pass target**. If the other team's stored T+0x78 faces the oncoming ball (more than 135° off its travel) and my target stands inside the 3 m × 5 m capsule ahead of him, my target takes PASS_GET and skips the team-mate trap-owner scan.
  - GF's "every outfield man, nearest wins" interceptor is deleted. Only the own pass target gets PASS_GET.
- **Trap-owner scan.** A team-mate among the 3 nearest the ball counts only if he is trapping; holding possession no longer counts. The passer had been blocking his own pass's receiver.
- **Ball-distance table.**
  - `MA+0x24710` is the per-team sorted `{id, distance²}` list, consistent with the offside, duels and defender reports.
  - The receive gate is now distance² ≤ 100 (`0x14253ee30`). GF's 2 m/s and `Predict(200)` gates are deleted.
- **BallFollow approach.**
  - Receiving point: the predicted ball point nearest the receiver, searched one PES frame at a time over frames 0..0xf8 (`FUN_140a4d340`).
  - The point is clamped onto the pitch along the line from the receiver when it lies more than 0.163 m outside (`FUN_14094e3a0`).
  - The PASS_GET receiver now runs `_PassGetApproach` instead of GF's magnet `_MovementCommand`.
- **PRESS/DELAY → plain control (kind 0).** GF has no movement state for these. The team's pressure player stands in, the same stand-in the defender port uses.

## 3. Not done (stop order), with exactly what I tried
- **The other six routes** (ThrowIn, NextPlay, BallAvoid, MovePositionAdjust, MoveStraightTrapStop, TargetEnemy) and BallFollow's facing and speed slots.
  - What I have: the vtables decoded (`1426339d8`, `14262ff60`, `14262fee0`, `142630060`, `1426300e0`, `142630160`, `142633e58`); the slot functions at 0x40–0x70 decompiled into `slots.c` (6.2k lines); the walk order read from `FUN_140945ec0`.
  - Not reduced. The speed is a GF distance ramp, marked `ponytail:`.
- **`GetTrapVector`.**
  - PES applies the touch in the anime layer, not as a velocity formula.
  - What I found: the readers of trap.o (registry 0x3e), 13 call sites. `FUN_14084bbd0` reads offsetPos_Dash/Idle/Run, `FUN_140857840` reads `+0xd5`, `FUN_140862530` reads ballControlRate, `FUN_140862370` reads `+0x40`.
  - The ball push itself was not traced. GF's touch physics stays.
- **The 22-player conflict loop** (`FUN_1409fa720` tail) and **`FUN_1409f9600`**: not reduced.
- **The throw-in stall.**
  - The open-play limit on PASS_GET is still in place.
  - Facts: the stall sat at a throw-in with the ball 8 m outside the touch line, inPlay 1, setPiece 1, the taker forced from 0.3 m.
  - `Match::ResetSituation` clears the event ring and the pass targets, so a stale pass cannot be the cause.
  - I suspect the kick scatter on throw-ins that Main fixed [INFERENCE]. That run with the gate removed was killed by the stop order, so the cause is unconfirmed.
- **Reaction order docs.** The defender port's map row still says "receive" comes first; it should read tackle, slide, then receive (entry 3).

## 4. Next steps
1. Rebase onto main (the scatter fix), then rerun seed 7 with the set-piece gate removed. If it doesn't stall, delete the gate.
2. Reduce the BallFollow speed and facing slots, then the other six routes, from `slots.c`.
3. Trace the anime-layer trap push and replace `GetTrapVector`.
4. Run the 3-seed matrix on the final build.
