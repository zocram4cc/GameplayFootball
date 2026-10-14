# Offside detection port (OffsidePort)

What of PES 2021's offside detection is ported into GodotFootball, which PES rule each piece implements, and where it
cuts a corner. Sources: `pes-re-offside.md` (detection: trigger, line, rule, categories) and `pes-re-duels-fouls.md`
§5.1–5.2 (call path, involvement, pending commit). Commits on main: see the port's commit.

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

## 3. Verification

- `pes_offside_test`: 12 tests — the line (second-rearmost, ball cap, halfway cap, keeper as a defender, fewer than
  two defenders, goal-line clamp, mirrored direction), the beyond test at the tolerance edge, the four categories and
  no-bit, no evaluation while a restart is staged, and the install loader's relations. Mutation-checked: dropping the
  leading-point margin red-dens the beyond tests, and flipping `d <= clearBand` to `<` red-dens the category edge.
- Smoke (seed 7, 20 min, home 1 away 2): before the port 1 offside (0-1), after the port 23 (15-8) — the PES test
  flags a player level with the line, where the old rule needed 0.20 m of slack. The score moved 3-4 to 2-3 with the
  extra free kicks. The defensive trap line and the attackers' run logic both read the new line, so the AI's offside
  discipline is what it is.
- Full gate: 368/368 pass after the rebase onto the set-piece port, `tools/check_pii.sh` clean. Commit `40eae92`.

## 4. Open

1. **The AI's offside discipline is tuned against the old rule.** The new tolerance doubles down on level attackers;
   GF's attackers overrun the line far more often than PES's do. A follow-up should measure the AI's run/pass line
   handling against the new detection (owner priority: team AI).
2. **The category is stored but not exposed to the AI helpers** (PES's `FUN_14050f580`/`FUN_14045e0d0` "do not chase"
  on `c < 3`/`c < 2`).
3. PES's ball-event type table is still open (offside §9 item 1), so `TouchResetsPhase` keeps GF's own
   deliberate-play split.

## 5. AI-map row

`|Offside line keeping and detection|**ported**: the PES line (second-rearmost rear surface, ball and halfway caps,
goal-line clamp), the attacker test with the torso margin and 0.15 m tolerance, categories 0–3 (only 0–2 punishable,
2 via the delayed whistle), once per ball event with the staged-restart gate; no per-limb points; the AI's offside
discipline untuned|pes-re-offside.md; pes-re-duels-fouls.md §5.1–5.2; pes-re-offside-port.md|offsiderule.cpp,
AI_GetOffsideLine, Referee::BallTouched, Referee::CommitPendingOffside|`
