# Offside detection and AI awareness port (OffsidePort + OffsideAwarenessPort)

What of PES 2021's offside detection is ported into GodotFootball, which PES rule each piece implements, and where it
cuts a corner. Sources: `pes-re-offside.md` (detection: trigger, line, rule, categories, and §4.1 the AI readers),
`pes-re-duels-fouls.md` §5.1–5.2 (call path, involvement, pending commit) and `pes-re-carrier-ai.md` §4.3 (the
receiver enumeration's offside status). Commits on main: see the port's commits.

## 1. Ported

|GF code|PES rule|Source|
|---|---|---|
|`pes::JudgeConstants` + `pes::LoadJudgeConstants`|The nine offside constants: the body-point margins (head/torso 0.15, arm 0.06, toe 0.10), the tolerance when an arm leads (0.30), the defender rear points' signed shifts (−0.15/−0.06/−0.10) and the category bands (1.0/4.0). Read from `.rdata` at 0x1425c48a0, 0x1425cc238, 0x142588200, 0x142590d98, 0x14259d64c, 0x14259d644, 0x14259d648, 0x14256ae1c, 0x1425656c0. The head/torso float serves both the margin and the tolerance, as in the image.|offside §3/§4|
|`OffsideRule::OffsideLineX` (pure)|The line in the defending team's own attack frame: `L = min(0, second-rearmost defender's rear surface, ball.x)`, both running minima starting at 0 (a defender beyond halfway never deepens the line), fewer than two eligible defenders → the goal line, clamped to the goal line.|offside §3|
|`OffsideRule::OffsideCategory` (pure)|The attacker test `s*P.x + m > s*L + tol` and the category from `d = |P.x − L|`: 2 when `d < 1`, 1 when `d ≤ 4`, 0 beyond that, 3 for a near-level attacker who is not beyond, no bit otherwise.|offside §4|
|`OffsideRule::CanCreateOffside`|The staged-restart set of the gate: kick-off, throw-in, goal kick and corner never arm a flag; a free kick or a penalty can.|offside §2.2|
|`AI_GetOffsideLine` (was the second-deepest-root rule with a 240 ms prediction and a 0.01 m halfway offset)|Built on `OffsideLineX` over the defending team's mental-image centres, on the pitch laterally, with the **current** ball centre (not the prediction). PES's line is not a prediction, so `futureSim_ms` is gone.|offside §3, §6.1|
|`Referee::BallTouched`|Detection once per ball event (never per frame): clear the phase (`TouchResetsPhase`), skip the toucher, no evaluation while a restart is staged (`ShouldSnapshot`), then judge every other active attacker of the touching team against the line and store categories 0..2 with the position at the touch.|offside §2.3/§4|
|`Referee::CommitPendingOffside` (new)|A flagged player who plays the ball sets a pending offside (the involvement test, duels-fouls §5.2 step 2); the whistle waits the offside whistle hold (`gs/2` frames) and a category-2 flag additionally waits while the ball player is shooting (`DelayWhistle`). On commit: PES's 0xb stop, the free kick at the involvement spot, the stats.|duels-fouls §5.1|
|`Referee::GetOffsideCategory` (new, OffsideAwarenessPort)|The per-player category 0..2 (-1 when unflagged) exposed to the AI, so PES's two readers can compare it: the ball list's `c < 2` and the key-player selector's `c < 3`.|offside §4.1|
|`AI_GetOffsideLine` / `Referee::BallTouched` frame (OffsideAwarenessPort)|The sign the line and the attacker test are read with. PES's `s` is the team's **attack** direction (`team+0x254`); this engine's `Team::GetSide()` points at the team's **own goal** (the reads `elizacontroller.cpp`'s `attackDir` and `referee.cpp`'s `attackDirSign` both take), so the attack direction is its **negation** — `OffsideLineX(-GetSide())` and `OffsideCategory(…, -GetSide(), …)`. With the negation wrong the line lands on the ball and every attacker behind the ball at his team's touch is offside: 23 whistles in a 5-minute match instead of 2.|offside §3/§4; carrier-ai §4.3|
|`OffsideRule::OffsideBlocksChase` + `Team::GetBestPossessionPlayer` (OffsideAwarenessPort)|FUN_14045e0d0's ball list: a player offside-flagged in category 0 or 1 gets the impossible score (1000000.0 @0x14259d640 against the 3600.0 @0x142598590 seed), so he is never the one who goes and gets the ball. GF's picker is that list — the men sorted by how soon they reach the ball, feeding the designated possession player and the possession side. Categories 2 and 3 stay on it.|offside §4.1|
|`OffsideRule::ReceiverIsOffside` + `AI_ReceiverIsOffside` + the carrier's pass loop (OffsideAwarenessPort)|The receiver enumeration's status 6 (`LAB_14211818f`): a team-mate whose current position **or** his position `int(fps x 0.3) = 16` frames ahead is beyond the carrier's own line — the deeper of the five last defenders (predicted 16 frames ahead) and the ball (predicted the same) — is never offered the ball. This is not the referee's line: no second-last-defender rule, no goal-line clamp, and a halfway floor. The 16 PES frames convert to this engine's seconds as 16 x 10 ms / 0.54, the same conversion the carrier's lane time takes for its 11 frames.|carrier-ai §4.3|

## 2. Approximations (`// ponytail:` in code)

- **No per-limb points; arms and toes not modelled.** GF has centre positions only, so the torso point stands in for
  every body point: a defender's rear surface is his centre plus the torso shift, and an attacker's leading point is his
  centre plus the torso margin, with no arm ever leading (the 0.30 m tolerance cannot apply). Ceiling: PES takes the
  rearmost of the root and nine body points for the line and the most advanced point for the test, so tight calls that
  PES resolves with a toe (0.05 m), an elbow (0.24 m) or a hand tip (0.30 m) this engine calls the other way. Upgrade
  path: per-limb points from the body model.
- **PES's `second > 0 → 0` clamp is not written as a step** — both running minima start at 0 and only ever go down,
  so it is unreachable. Pinned by the halfway test.
- **The offside whistle hold is a named constant (500 ms), not a runtime read.** PES's `gs/2` frames is a frame count
  rather than a `.rdata` float; with gs = 54 frames per second it is half a second.
- **`DelayWhistle` is only "the ball player is in `e_FunctionType_Shot`".** PES also holds the whistle for a ball event
  younger than 5 frames, for an event of type 5, and for its own key-player conditions, none of which GF models.
- **The involvement test is duels-fouls §5.2 step 2 only** (the flagged player made the newest ball event). The keeper
  -interference and near-to-ball race branches are not ported, so an offside that PES calls on a passive flagged
  attacker is not called here.
- **The key-player selector FUN_14050f580 is not ported** (OffsideAwarenessPort): PES's advantage test compares its
  two teams' ball candidates, PES's candidate being the team's key player `team+0x78` unless he is flagged, else the
  first non-flagged ball-seeking entry of the nearest-to-ball list. GF has no key player, so only the ball-list rule
  (`FUN_14045e0d0`, `c < 2`) is ported; the advantage decision keeps GF's own team-distance comparison.
- **The carrier's offside line is read over centre positions and is not clamped to the goal line** — PES's is a
  running maximum from 0 over five last defenders and the ball, all predicted 16 frames ahead, so a defence pushed
  up the pitch can put it beyond the goal line and it is still the line used. GF's version is the decoder's, one for
  one, over the mental image's centres. Upgrade path: the referee's goal-line clamp, only if PES turns out to apply
  one there too (not visible in the decompile).
- **No run rule ported, and none decoded** (OffsideAwarenessPort): the run selectors' stop tests do not read the
  offside line or the flags (§4.2 of the detection report), so GF's own run/line handling is not in conflict with
  anything decoded. PES runs players offside and lets the referee's flag stop the play.

## 3. Verification

- `pes_offside_test`: 19 tests — the line (second-rearmost, ball cap, halfway cap, keeper as a defender, fewer than
  two defenders, goal-line clamp, mirrored direction), the beyond test at the tolerance edge, the four categories and
  no-bit, no evaluation while a restart is staged, the chase rule's category thresholds (`OffsideBlocksChase`: 0 and
  1 block, 2 and 3 do not, −1 is unflagged), the receiver enumeration's offside test (five last defenders, the ball
  deepening the line, the halfway floor, the receiver's and the defenders' forward predictions, the mirrored
  direction, and no attack side meaning no refusal), and the install loader's relations. Mutation-checked: dropping the
  leading-point margin red-dens the beyond tests, flipping `d <= clearBand` to `<` red-dens the category edge,
  widening the chase threshold to `c < 99` red-dens the chase test, and reading only the rearmost defender instead of
  the five last red-dens five receiver tests.
- `sim_match_test`'s `OffsideRateStaysInPessBand`: a whole 5-minute match on the synthetic fixture, pinned by the
  frame read — with the defending team's own-goal side passed to `OffsideLineX` instead of its attack direction the
  same match whistles 23 offsides instead of staying inside the band.
- Full gate: see the OffsideAwarenessPort commit.

## 4. Open

1. PES's ball-event type table is still open (offside §9 item 1), so `TouchResetsPhase` keeps GF's own
   deliberate-play split.
2. **`FUN_14050f580`'s key-player branch** (§2) and the receivers' record B ("into space", carrier-ai §4.3) are not
   ported.
3. `spaceRun.o`'s `checkOffsideLineDistX 12` key has no identified reader (off-ball §1.5 O3), so no run rule is
   derived from it.

## 5. AI-map row

`|Offside line keeping and detection|**ported**: the PES line (second-rearmost rear surface, ball and halfway caps,
goal-line clamp), the attacker test with the torso margin and 0.15 m tolerance, categories 0-3 (only 0-2 punishable,
2 via the delayed whistle), once per ball event with the staged-restart gate, the frame read in each team's attack
direction, the AI's offside readers (a category 0-1 flag keeps a player off his team's ball list, the carrier's
receiver enumeration's status 6 excludes an offside receiver); no per-limb points, no key-player reader, no run rule
(none decoded - PES's run stop tests read no offside input)|pes-re-offside.md; pes-re-duels-fouls.md §5.1-5.2;
pes-re-carrier-ai.md §4.3; pes-re-offside-port.md; pes-re-off-ball.md §2.2|offsiderule.cpp, AI_GetOffsideLine,
AI_ReceiverIsOffside, Referee::BallTouched, Referee::CommitPendingOffside, Referee::GetOffsideCategory,
Team::GetBestPossessionPlayer|`
