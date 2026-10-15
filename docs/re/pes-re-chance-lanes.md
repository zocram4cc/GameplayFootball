# Chance lanes (OffBallPort pass 3)

Scope: the remaining Chance-run gates named in `docs/re/pes-re-off-ball-port.md` §5.1, to bring run
starts from 5-6x PES toward PES's live ~93 per team per 20 min. Branch: the ChanceLanes worktree,
pushed as `chance-lanes` off `1d91833` (main). Code:
`src/sim/onthepitch/pesoffball.{hpp,cpp}`, `TeamAIController::BuildRunWorld`.
Tests: `tests/sim/pes_offball_test.cpp` (30 tests, 29 green, 1 skipped without `PES21_ROOT`).

Tags: **OBS** = raw disassembly read (VA given), **INF** = inferred. No PES number table is
transcribed; every literal in code carries its VA or a named constant with provenance.

---

## 1. What landed

### 1.1 FUN_140654e60, the time-to-reach (`FramesToReach`)

The report's pass-2 blocker: "FUN_140654e60 (open) ... its two implicit 43a0a0 angle arguments need a
raw walk at 0x140654e8x". Both are now read (OBS raw `0x140654e60`-`0x14065565c`):

- the first is `FUN_140439e50(routePoint, X)` = a bearing; the second is the same bearing folded
  through `FUN_14043a0a0` against the player's facing (`+0x554`) and against his velocity heading
  (the second use at `0x140655561`).
- `FUN_140a92c80(x, scale)` is `sign(fold180(x))·scale·sin(inner/2)` where `inner` folds to
  [0,180]; its 512-entry table at `0x142655950` is `sin(i·90/512 deg)` (verified:
  `tbl[128]=sin(22.5°)=0.3827`, `tbl[256]=sin(45°)=0.7071`, `tbl[512]=1.0`).

The algorithm, ported literally: a two-frame loop. Per-frame distance starts at
`sin(fold(dev+90)/2)·speed·align` where `align = ramp(dev; 30+35(1-u) → 1, +25 → 0)` and
`u = ramp(3.6·speed, 11 → 22)` km/h. While the player has not yet turned
`2·int(fps+0.5)/48·2` deg toward the target the accel step is skipped. The accel climbs
`gain += 2·span/n` with `span/n = (t20−t225 or t225−t327 or t327−t72) / int(fps·(1.3 or 0.3 or
0.15)+0.5)`, gated by `gain < t129`. The loop ends when `walked >= dist` or `frames > int(fps·20+0.5)`,
in which case the answer falls back to `dist / t129`. Then
`+ int(int(fps·0.25+0.5)·ramp(dev, 90→155)) · ramp(dev2, 65→120) + int(int(fps·0.4+0.5)·u)`,
with the last term halved when the player record's `+1` byte is 3.

Frame counts come out even because the loop steps by 2.

**ponytail**: `FUN_140a75740` (the path/route API) is the straight segment. GF has no obstacle path,
so the waypoint list is the destination itself. Named in code.

### 1.2 FUN_14068b1a0, the crowded-mate stop (`CrowdedMateStop`)

OBS raw `0x14068b1a0`-`0x14068b43b`. An SB (`FUN_140a580a0 == 2`) runner, no more than 7 m behind
the opponents' line (`a4c9a0(id, -7)`), and within 20 m of the ball-line point (MA+0x2482c/0x24834):
**release** when a teammate on a run (`FUN_1405a4830`) who is not a class-1 Chance mate stands
within 12 m (`144 m²`). Wired at step 19, `t > fps/2`, `!pinned`.

GF stands in `FUN_140a580a0`'s AI class (player `+0xc0`, a copy of `+0x40`) with the role class
(`PlayerFacts::aiClass`, wired from `RoleClass`). **ponytail**: GF carries no AI behaviour class; the
role class is the closest analogue. Named in code.

### 1.3 FUN_14068aeb0, the pass-to-me stop (`PassToMeStop`)

OBS raw `0x14068aeb0`-`0x14068b06d`. **Polarity correction**: the report's §3 cascade lists it as a
disjunct before the `→ STOP` arrow, which reads like a keep. The raw at `0x14068a43a`
(`call 0x14068aeb0; test al,al; jne 0x14068a4da`) shows true jumps to the stop path: **true = STOP**.

With a kick in flight (`a3e190` kind 1/2/3/0x10) whose recorded target is not this runner
(`a62e40 != id`), the runner's AI class neither 2 nor 0x13, at least 4 m behind and not more than
1.2 m past the opponents' line (`a4c9a0(id, -4)` true and `-1.2` false), an opponent within 5 m
(`a4e250(id, 5)`), the receiver's lead `FUN_140653480` from his distance to the ball (196/676 m² →
0.28/0.35/0.38 s) is compared against the runner's frames to his **own** route point
(`FUN_140596c30` + `FUN_140655660`): release when `lead >= mine + int(fps·0.15+0.5)`.
Wired at step 19, `t > 2·fps`, `!pinned`.

### 1.4 Measured effect (pieces 1 and 2)

| stage | `[offball] chance runs` (seed 7, 20 min) |
|---|---|
| pass 2 (main, from the report) | 576–445 |
| this branch, before the piece | 433–769 |
| this branch, after the piece | **585–760** |
| piece 2 (rank angle test) | **478–774** |
| PES live | ~93 per team |

**No material reduction, and none should be expected from this piece.** Both leaves gate on
conditions that are rare in the current match: `68b1a0` needs a full-back (roleClass 2) whose running
teammate is inside 12 m and who is pressed to within 7 m of the opponents' line; `68aeb0` needs a
kick in flight, an opponent within 5 m, the runner pinned 1.2–4 m behind the line, and a receiver who
cannot beat him to his own route point. The 433→585 movement is inside match-to-match noise. The
overrun stays where pass 2 left it; this piece removes three stand-ins rather than moving the number.

The same reading holds for the whole pass so far, and it is worth stating plainly: three decoded
gates and one argument recovery have produced `576–445 → 585–760 → 478–774`, which is noise around
~600, against PES's live ~93. **The overrun is not in the gates that remain undecoded.** It is
somewhere structural - most plausibly in the layer that decides *when the Candidate mask is walked
at all* (the manager's cadence and `FUN_1405938a0`, which the port itself notes reads the ball's z at
the snap), or in the fact that GF's `runs` map only ever releases a run through the stop cascade.
The next pass should instrument mask walks and candidates per tick rather than port more leaves
blind.

### 1.5 Mutation check

Eight mutations applied one at a time, each caught by a named test:

| mutation | caught by |
|---|---|
| crowd radius `144` → `14400` | `PassToMeStopFollows...`, `ACrowdingMate...` |
| jog tier `12.9` → `6.45` | `OffBallReach.FramesToReachGrowsWithDistanceAndFallsWithSpeed` |
| jog tier `12.9` → `25.0` | same |
| near lead `0.28` → `0.08` | `PassToMeStopReleasesInsideTheNearLeadBand` |
| near lead `0.28` → `0.45` | `...ReleasesInside...`, `...KeepsOutside...` |
| mid lead `0.35` → `0.45` | `...ReleasesInTheFarAndMiddleLeadBands` |
| far lead `0.38` → `0.60` | `PassToMeStopFarBandKeepsARunnerTooFarFromHisPoint` |
| mid threshold `676` → `576` | `...ReleasesInTheFarAndMiddleLeadBands` |

**Known blind spot**: the near-band threshold `196` m² cannot be pinned by any fixture. The reach
loop only produces even frame counts, so a runner's `mine + 8` is even and can never land strictly
between the 15- and 19-frame leads. Any mutation that only moves a receiver across that boundary is
invisible. Recorded rather than papered over.

### 1.6 Verification

- `ctest --test-dir build -j3 --timeout 2400` with `PES21_ROOT=the PES21 install` and the edit save:
  run on the committed tree; standing skips 59 and 115 apply.
- Smoke `build/apps/gfsim_run --db-sql assets/fixtures/teams.sql --home 1 --away 2 --seed 7
  --minutes 20 --pes21-root the PES21 install`: `[offball] chance runs 585-760`,
  `[stalemate] forced` unchanged from before the piece.
- `bash tools/check_pii.sh` before the commit: clean.

### 1.7 Piece 2 - the mask's rank > 1 angle sub-test (`RankAngleOk`)

The report's other "arguments dropped" blocker. Raw `0x140607741`-`0x140607814` gives the whole
chain; the dropped arguments are recoverable from the surrounding stores: `r13` = `MA+0x2482c` (the
ball-line point, written at `0x140607002`), `rbx` = player `+0x55c`, `[rsp+0x6c]` = the runner's
facing `+0x554` (`0x140606fd8`) - **the second `43a0a0` argument the report listed as needing a raw
walk** - `[rsp+0x70]` = `T+0x250` (the cone centre), `[rsp+0x74]` =
`MA+0x40+0x14·(team == 0)` (the a4c720 operand), `[rsp+0x4d]` = `FUN_1405aaf00(ctx, team)` (the
instruction-2 flag, which GF does not carry).

The rule: on the mask's generic path, with more than three attackers ahead of the ball band (or a
forward without the LB style bit), a runner on the **second line or later** whose facing agrees
within 45 deg with the bearing from the ball-line point to him, and with no opponent in the
80 deg / 30 m cone about the attack direction, is **rejected** at more than 20 m from that point
(`400 m²`, `0x1425a2670`) - and every runner is rejected when the point is not ahead in the attack
frame, because GF's `a4c720` arguments are `a = 0` and `k = 0`.

**Polarity correction worth recording**: the 20 m window is a **reject** (`jae 0x1406078d6` at
`0x1406077c5`), not a skip, while an opponent inside the cone is the opposite - it lets the runner
through. Reading either backwards inverts the gate.

Mutations caught: rank gate `1 -> 3`, facing window `45 -> 13`, the cone's polarity, the half
test's removal, and the window as both direction and magnitude (`400 -> 100`). The rank gate's
exact value (`1` vs `0`) is not distinguishable: a first-line runner is exempt either way, and no
fixture puts a second-line runner where the two thresholds differ.

### 1.8 Piece 3 - the lead-band and jog-tier fixture hardening

The first run's mutation check had four uncaught mutants in the time-to-reach's neighbourhood. The
cause was fixture geometry, not missing coverage: the receivers sat at 256 m² from the ball (past
the 196 m² near band) so the 0.28 s branch was never taken, and the middle-band receiver sat at
exactly 676 m², on the `d2 < 676` boundary. Re-tuning both (receivers at 49/169/522/625/729 m² and
a runner whose margin is 12 frames) pins all three lead seconds: 0.28, 0.35 and 0.38, plus the
676 threshold. Caught by named tests: `0.28 -> 0.08`, `0.28 -> 0.45`, `0.35 -> 0.45`,
`0.38 -> 0.60`, `676 -> 576`, and the jog tier in both directions.

One remaining blind spot is now **named and structural**: the reach loop steps by 2 frames, so a
runner's `mine + 8` is always even and can never land strictly between the 15- and 19-frame leads.
No fixture can pin the `196` m² threshold; recorded rather than papered over.

### 1.9 Verification status

- Full gate on the piece-1 commit: **631/631 passed**, total 535 s. The two standing skips are
  `59 - VfsInstall.NestedOriginalsSurviveWithoutDownloads` and
  `131 - EditSaveInstall.DecryptsAndMatchesTheRunningGame` (the contract's "115" does not exist in
  this suite).
- `bash tools/check_pii.sh` clean before each commit.
- Pushed as `chance-lanes` off `1d91833`: `74d3d9f` (piece 1), `fa7dcba` (piece 2).
- Gate re-run after piece 2: **636/636 passed** (total 354 s; the suite grew from 631 to 636 with
  the new fixtures), same two standing skips (`59`, `131`).
- Seed-7 smoke for piece 2: `[offball] chance runs 478-774`, `[stalemate] forced` unchanged. Note
  the smoke's stdout must be read from a full log, not piped through `grep`: the pipe closes early
  and the run aborts before the summary.
- `691300` was also walked this pass (raw `0x140691300`): its first two gates are
  `FUN_140593430`'s Chance sibling - `T[slot+0xa2a0] == 0x1e` and `T[slot+0x28c] == 2` (the **run
  sub-kind**). GF's `RunRecord` carries no sub-kind, so this gate is inert until one is added; it is
  listed under the blocked section rather than guessed at.

---

## 2. Decoded this pass but NOT ported (next candidates, in order)

All bodies read from raw; each needs a check of what GF can actually observe.

1. **`FUN_1405a8290` / `FUN_1405a5400`** — the mask's rank > 1 sub-tests. OBS raw
   `0x1405a8290`-`0x1405a8da9` and `0x1405a5400`-`0x1405a5700`. `5a8290` reads T+0xb8b8 and the
   per-slot state ring `a401c0(T+0xa2a0 + 0x34·slot, 0/1)` and compares
   `(end - cur) > int(fps·0.2 + 0.5)`; its tail is a lane sweep over the 11 slots of the team with
   `a4e2...`-style cones (`5.0`, `−14.0`, `10.0`, `70.0`, `2.0`) and a `b748 == 0x30` gate, plus
   `655660` on the pair (now available). `5a5400` is a 5 m cone test over `a63950`-style receiver
   geometry (`10.0`, `−14.0`, `2.0`) with `144`, `400` and `625`-style cutoffs - **full bodies were
   walked but not yet reduced to a GF-observable form**. `a63950` (the sector table) and
   `FUN_140a401c0` (the state ring reader) are both available. This is the highest-value next piece.
2. **`FUN_1405a1cc0`** - step 19's duel-interception gate, OBS raw `0x1405a1cc0`-`0x1405a2160`,
   **body fully walked**. Order, with the meanings resolved:
   1. `c = FUN_140a627e0(ctx, 0)` (the carrier); `c > 0x15` -> false.
   2. The runner's **own** distance to the snap: `[c·0xc8 + 0x22e4c + 0x22e50]` (a per-team
      rating/list pair) `> 16` (`0x14259be10`, 4 m) -> false. So the runner must be inside 4 m of
      the snap point.
   3. `FUN_140a58320(ctx, c) == 5` -> false; `== 0` -> false. `a58320` is the marks byte
      `T+0x12a4` clamped to 5, so the carrier must have 1-4 marks.
   4. The carrier's speed: `|a6c6d0(ctx, c, 0.694444/fps)|`; if it is at or below
      `FUN_1404c8dc0(4.0)` = `4000/(fps·3600)` m/frame -> false; the ramp
      `w = ramp(|speed|; FUN_14043c430(12.9)·0.8 -> 0, FUN_14043c430(32.7) -> 1)` (so `4c8dc0(k) =
      k·1000/(fps·3600)` m/frame and `43c430(k) = 100/(fps·k)` m/frame), then
      `v = 6·w + 2.5` (`0x14259bdec`) - the mate-proximity radius.
   5. Over the team's 11 slots, skipping the carrier: `FUN_1405a4830(ctx, j, 0)` (the run-state
      predicate, `5a4790(state, mode, class)` after `a580a0`'s class-4/5/0x58 guard); a mate in
      Chance with `a580a0 == 1` is exempt; `|P_j - P_c|² < v²` -> **true** (the keep).
   6. Otherwise `FUN_140a76210(rec, 1)` and `(rec, 0)` give two route points (the second and the
      newest), `FUN_140a90d80` their crossing with the carrier's route, then
      `FUN_140655660` on both compared with `+int(fps·0.2+0.5)`.
   All its sub-leaves are now available - see the `a90d80` / `a76210` entries below - so this is a
   straight port. Note `FUN_140a58320` (the marks count) is the one input GF has only partially
   (`marker` is per-player, not a count); treat it as 1 unless a count can be derived.

3. **`FUN_140691300`** — the run sub-kind 2 stop, OBS raw `0x140691300`-... needs
   `FUN_140a62850` (the marker reader, present as `marker` in `PlayerFacts`) and `FUN_140a620f0`
   (the sub-kind 2 selector).
4. **`FUN_140694320`** — needs `FUN_1405a8290` (item 1) and `FUN_1406940f0`.
5. **The over-cap swap gates `FUN_1405ab180/5abcc0/5ab380/5ab990`** — described in run-targets §4
   with their constants (`cnt < 45`, `sec >= 50/70/10`, `mate value <= min(60,sec)/55`, `10 m` z
   proximity, `F5+2`, `15`). GF's `runs` map gives the Chance-running mates; the `T+0xc064` value has
   no GF counterpart.
6. **The commit-tree leaves of `FUN_140664ee0` beyond `66d720`** — run-targets §5.4 has ~45 of them
   decoded; the GF-observable subset is `66d100`, `668620`, `593e30`, the `19°` opponent-count
   branch, and the final receiver test.

## 3. Blocked (need a live poke; not guessed)

1. **`FUN_1405a7fe0` / `FUN_1405a7be0` / `FUN_1405a7d10`** — step 2's carrier pass-aim keeps and the
   carrier-controlled stop. They read PES **action records** (`+0x1490`, `+0x1494`, `+0x14`, `+0x1c`),
   the `MA+0x23f7c` angle table and the `G+0x180` event table. **GF has no action records at all**,
   so these cannot be reduced to GF-observable inputs from the exe alone; they need a live poke to
   see what the records contain in play.
2. **`FUN_14060a750` / `FUN_14060a4d0`** — the special-lane pre-gates. Both bodies were walked, but
   `60a750`'s first gate is `FUN_140a54130(id, 3)` = "this player's action record `+0x1494` is 3",
   which is the same action-record wall as item 1. Wiring them without the record changes nothing.
3. The receiver-side pin writes (`sel+0x280`...) need PES's selector state (`sel+0x2a0..0x2c4`), which
   GF's `runs` map only partly models.
4. **`FUN_140691300`** (the sub-kind 2 stop) - walked this pass; its first gate is
   `T[slot+0x28c] == 2`, the **run sub-kind**, which GF's `RunRecord` does not carry. Needs a
   sub-kind field in `RunRecord` (or an explicit decision that GF never sets one) before it can be
   ported rather than stubbed.

## 4. What is not in scope here

Off-ball run mechanics that already match (cadence, rank, caps), the press layer, everything outside
the Chance candidate/stop path.
