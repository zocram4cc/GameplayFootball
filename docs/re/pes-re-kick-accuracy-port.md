# Kick execution error port (KickAccuracyPort)

## Ported (constants read from PES2021.exe at runtime, `pes::LoadKickErrorConstants`)
`src/sim/onthepitch/peskickerror.{hpp,cpp}`, test `tests/sim/pes_kick_error_test.cpp`.

| piece | report | VAs |
|---|---|---|
| Kick-type stat: Finishing / Low Pass / Lofted Pass / Heading (header by `touch_bodypart` "head") | accuracy 2 | `FUN_14071d770` (`0x1a/0x1b/0x1c` @`0x14071d8de..e5`, `0x1d` @`0x14071dc1e`) |
| Quality curve q(S), three segments (constants re-derived from raw disasm of `FUN_14071d770` tail `0x14071dc93..dd35`) | accuracy 2, contact 3.3 | 40 @`0x14259be2c`, 60 @`0x14259a764`, 95 @`0x1425c594c`, 0.1 @`0x142588200`, 20 @`0x14259be1c`, 0.85 @`0x1425ab120`, 35 @`0x14259a760`, 0.05 @`0x1425a8a28`, 0.25 @`0x142592228`, 0.95 @`0x1425ab124` |
| Skill +5: Heading on a header shot (OBS), Pinpoint Crossing on a cross (kick byte 0x13, INF) | accuracy 2 | 5.0 @`0x14259222c` |
| Soft-min of 4 (`FUN_1408413c0`) | scatter 1 | - |
| Records Y/P/S `{(1-q)*22.5|0.3, 0.5, (1-q)^2*4.25+0.75}`, `A = 0.75a+0.25`, `Qk`, `0.5Q+0.5` | scatter 1, 6 | 22.5 @`0x14259d618`, 0.3 @`0x142590d98`, 4.25 @`0x1425ef9f8`, 0.75 @`0x14259bdd4`, 0.5 @`0x142565600` (pSign init `0x14083d211`) |
| I, non-SHOOT: km/h window `[60+10a, 85|80+10a]`, `w = max(0,1-0.95a)*{0.3,0.375}`, elevation ramp 20..30 deg | scatter 6 | `0x14083dd32..df70` (85 @`0x14259be58`, 80 @`0x142596e6c`, 0.375 @`0x1425e4a30`, 30 @`0x142591140`) |
| I, SHOOT: `1 - 0.25*ramp(power, 0.2..1)` | scatter 6 | `0x14083db8a..dd27` |
| R: nearest opponent within 2 m, `D = (0.8 - 0.4*clamp((PC-oppPC+20)/40))*distRamp`, `R = 1 - 0.4*1.5*D` | scatter 6 | `0x14083ec70..ee46` (-20 @`0x1425a946c`, 1.5 @`0x14259bddc`) |
| `Normal(rng, amp)`: two slot-2 steps, 513-entry log table (read from `0x1426548c0`), Box-Muller, rejection | rng 1.4/1.5, scatter 2 | `0x140a92590` |
| Draw path B (mode 3): yaw, pitch (halved when negative on pass/shoot), speed; each `Normal` then `0x1406ebf30` sign roll | scatter 3 | `0x14083a280`, `0x14083a5f2`, `0x14083a7fe` |
| Per-player generator `Player::GetPesRandom()` (slot 2 of `player+0x32d0`), general accessor for other per-player draws | rng-slots 6/9 | - |

Wiring: `MatchSetup::pesKickErrorConstants` (required, `Match` throws), `PesMatchConstants`/`FillMissingPesConstants`, `gfsim_run`, `PesLibrary::kick_error_constants` + `gf_match.cpp`.

## GF code deleted
- `humanoid.cpp` shot aim scatter (`sigma_m`, `random(±1)` aim jitter, range growth, vertical bias/share, skill scale).
- `GetBestPossibleTouch`: stat×0.80 difficulty, keeper `gk_clearing`, skill multiplier, philosophy multiplier, the difficulty term of `randomRotation`, `GetPassErrorLoft` loft.
- `GameplayTuning::GetPassErrorLoft`, `kShotAimScatter_m`, `kShotAimScatterNearRange_m`; `TeamPhilosophy::GetPassErrorMultiplier`; `PlayerSkills::GetPassDifficultyMultiplier`.

## Ponytail approximations (all marked `// ponytail:` in code)
- Per-player seed: match seed + player id (PES derivation `0x1406eb670` unproven).
- Situation product Q and motion factors F1/F2 = 1 (they key on PES kick kinds / motion ids GF animations do not carry). Consequence: the kicker's quality only moves the spread (amp); the scale is driven by I (launch power/elevation) and R (pressure).
- Pass kick byte: ground passes use the 1..3 window (85 km/h, w 0.3), lofted the default (80, 0.375).
- SHOOT I leaves out the `0.02*g` pitch-sin term (upper bound in an untraced register; at most 0.02 on I).
- R: opponent frame-counter ramp (byte+0x2c) taken as 1; the running term `(1-0.5a)*1.3*FUN_140723320` is 0 (`FUN_140723320` is a nearby-player threat sum over the `0x14344ef78` weights, not running speed as the report guessed).
- Normal uses `std::sin` instead of the 512-step sin table `0x142655950`.
- The yaw redraw loop (`0x14083a550`, ≤6 draws) is skipped.

## Not ported (why)
- `FUN_1408414a0` branches: every branch keys on PES kick kinds, motion ids or the foot side (`vtbl+0x30`); GF has none, so all pSign stay 0.5 and no record is post-scaled. The jostle flag (`ap+0x2d14&1`, Y.scale = 0) has no GF equivalent.
- Path A (mode 1/2, uniform `Range(1000)` yaw): mode is set by kinds GF lacks; mode 3 is PES's default.
- Weak foot: the `0x14071d770` tail (`stat 0x34` vs foot side, `stat 0x26` penalty `int(3+(12|22)*ramp)`) needs the kicking foot, which GF animations do not expose.
- Other +5 bonuses (Long Range Drive, Knuckle, Control Loop, Middle Shot, Through Pass, Heel) gate on kick bytes with no GF mapping.
- Predictor `FUN_1407324b0` (logistic errScale): it is the AI's reachability predictor, not the execution error (scatter 4).
- Pass assist curl `FUN_14083c590`: separate system.
- `humanoid_utils.cpp` `GetShotVector` worst-case/curve randomness (outside this brief's list).
- GF's distance/height/ball-movement touch factors in `GetBestPossibleTouch` are kept: they are GF's contact geometry, the role PES's unported Q plays.

## Before / after (seed 7, 20 min, teams 1 v 2)
| | before | after |
|---|---|---|
| score | 1-0 | 4-0 |
| passes / accuracy | 15-10, 66%-60% | 29-31, 72%-61% |
| shots / on target | 3-1 / 2-0 | 18-2 / 7-0 |
| shots wide / over / short | 0-1 / 0-0 / 1-0 | 9-1 / 1-0 / 1-1 |
| stalemate forced | 92330 | 74078 |
| determinism | ccca02cb7ca71f00 | 8367f4aa8b5f1c3a |

The baseline run spent most of its time in a stalled set piece (`[stalemate]` forcing), so its counts are small; one seed is not a calibration. Shots now miss wide far more (9 of 18) where the old band-plus-sigma kept most in the frame.
