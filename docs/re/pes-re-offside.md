# PES 2021: how offside is DETECTED (RE report, OffsideRE)

Closes `pes-re-duels-fouls.md` §5.3 ("Detection — OPEN") and the "Offside line keeping and detection" row of
`pes-ai-map.md` §1. Evidence labels: **OBS** = seen in the decrypted image (decompile checked against the
disassembly) or live; **INF(h/m/l)** = inferred, confidence high/medium/low. All addresses are PES2021.exe VAs.
Tools: the PESLive Ghidra project (`dec.sh`), capstone over the decrypted image dump, read-only `/proc/<pid>/mem`
reads, and **perf hardware breakpoints** (`perf record -e mem:ADDR/4:w` / `mem:ADDR:x` on the game's threads).
A perf breakpoint is a kernel debug-register event: it does not ptrace, stop, write to or send input to the
game; the game ran on throughout. Scripts live in the OffsideRE scratch dir (`watch_bits.py`, `dispscan.py`,
`calls.py`, `pdx.py`, `find_ma*.py`).

---

## 0. Summary

- **Writer found.** The flag bits are written by **`FUN_140a7b7a0`** (the rule), called only from
  **`FUN_1404621d0`** (the update), which is the ANALYZE component's handler for **message kind 7 (a ball
  event)**, dispatched by `FUN_140458a10` (vtable slot `0x14259d5c0`, next to the string `ANALYZE`).
  The **offside line** is written by **`FUN_14045cf30`** into `MA+0x44+0x14·t`.
- **Why every earlier scan missed it (OBS).** The update builds `S = MA+0x25378` with two separate immediates,
  `add rbx,0x24d70` / `add rbx,0x608` (0x1404621f6, 0x140462203), and then uses only `this`-relative
  displacements: `or [rdi+rdx*4+0x4c]` (0x140a7bdea), `mov [rbx+r14*4+0x44]` (0x140462376),
  `movss [rdi+0x174+…]`. No instruction carries 0x253c4/0x25378. The line store is `[rax+rdx+0x44]` with
  `rax = *(this+0x508)`.
- **Live proof (OBS).** perf write-watchpoints on `MA+0x253bc/+0x253c0/+0x254fc` during open play (45 s,
  47 hits) trapped **only** at `0x14046237b` (after the frame store at 0x140462376, `rbx = MA+0x25378`) and
  `0x140a7bead` (inside the snapshot writer `FUN_140a7bee0`, `rdi = S`). `AX` at the frame store was the
  current `MI+0x1198` frame each time.
- **The rule (OBS):** at each ball event by an unflagged player of team `a`, for every other active player of
  `a`: take his **most advanced body point** `P` (root and 9 body points, each with a surface margin `m`);
  he is **beyond** if `s·P.x + m > s·L + tol` with `tol = 0.15 m` (**0.30 m** when the leading point is an arm
  point), `L` = the defending team's line. Category from `d = |P.x − L|`: beyond and `d < 1 m` → **2**;
  beyond and `1 ≤ d ≤ 4 m` → **1**; beyond and `d > 4 m` → **0**; not beyond and `d < 1 m` → **3**
  (onside, near-level); else no bit.
- **The line (OBS):** in the defending team's own attack frame, `L = min(0, second-rearmost defender rear
  surface, ball.x)`; fewer than two eligible defenders → the goal line; clamped to the goal line.
  `0` = halfway: an attacker in his own half can never be beyond.
- **No RNG** anywhere in detection. **Live cross-check (OBS):** in a later match, ~220 logged evaluations
  matched the category rule exactly (2 498 player-evaluations agree, 0 contradict, 6 inside the 0–0.30 m
  body-point ambiguity band, 4 explained by the ball player's skipped point); §7.

---

## 1. Objects and fields (OBS unless marked)

| name | how to reach | notes |
|---|---|---|
| analysis object `A` | `this` of `FUN_1404621d0` / `FUN_14045cf30` | `A+8` = context `X`, `A+0x508` = `MA` |
| context `X` | `*(A+8)` | `X+0` = MI, `X+0x10` pitch, `X+0x20` ball, `X+0x38` ball-event list, `X+0x40+8·id` players, `X+0x1a0+8·id` player body objects, `X+0x250+8·t` / `X+0x260+8·t` team objects |
| pitch | `*(X+0x10)` | `+0` = half-length (goal line x), `+4` = half-width, `+0x18` = INF(m) a depth (see §3 step 3) |
| team sign `s_t` | `char *(X+0x260+8t) + 0x254` | ±1, the team's attack direction along x (same field the GF port already reads as `team+0x254`) |
| ball-event list `EV` | `*(X+0x38)` | newest event `EV+0x180698` (0x64 bytes: `[0]` type, `[1..2]` player ref `{team, slot}`, `[4]` frame, `[7]` sub-kind); last event *with a player* `EV+0x1806fc` (ref at `+0x180700`, returned by `FUN_140a3e880`). Both are filled by `FUN_140a3dc40` when it receives message kind 7. |
| active flag | `MI+0x1748+id` (byte) | player on the pitch and taking part |
| **line** `L_t` | `MA+0x44+0x14·t` (float, world x) | written by `FUN_14045cf30`; the block `MA+0x38+0x14·t` = {`+0x38` front x, `+0x3c` mid, `+0x40` back x, `+0x44` **offside line**, `+0x48` id} (front/mid/back/id by `FUN_14045bfe0`) |
| `S` | `MA+0x25378` | `+0x00` 2×33-byte grid (`FUN_140462470`, not offside) |
| eval frame | `S+0x44+4·a` = `MA+0x253bc[a]` | `MI+0x1198` at the last evaluation of attacking team `a` |
| **bits** | `S+0x4c+0x10·a+4·c` = `MA+0x253c4…` | bit `id` (global 0..21; team 1 uses bits 11..21), category `c` 0..3 |
| leading points | `S+0x6c+0x84·a+0xc·i` = `MA+0x253e4…` | `P` (vec3) of slot `i` at the last evaluation |
| line used | `S+0x174+4·a` = `MA+0x254ec[a]` | `L` of the defending team at that evaluation (→ `MI+0x15a8` on a call) |
| snapshot | `S+0x184` = `MA+0x254fc` | 22-player state copy (`FUN_140a7bee0`), taken when any category 0..2 bit of `a` is set (→ `MI+0x15ac`) |

Live `MA` was found with a perf execute-breakpoint at `0x1405b64c5` (`rsi = *(*(judgeCtx)+0x18)`) and at
`FUN_14045cf30` entry (`MA = *(rcx+0x508)`); the trailing block read back with frames, the 22×vec3 leading
points (heights 0.03–1.64 m, i.e. feet to head) and `MA+0x254ec` exactly where §1 places them.

---

## 2. Trigger and cadence

### 2.1 Message dispatch (OBS `FUN_140458a10`)
```
msg.kind == 7                     -> FUN_1404621d0(A)          (ball event)
msg.kind == 0 && msg[+0x14] in {2,3} -> S.bits[0..1][0..3] = 0 (both teams cleared; INF(m): stoppage / restart set-up)
```
Message kind 7 is the ball-event message: `FUN_140a3dc40` copies its payload into `EV+0x180698` and, when it
names a player, into `EV+0x1806fc` (OBS). So the line test runs **once per ball event**, not per frame.
Live: frame stores at consecutive frames during one 16-frame stretch (events every frame, INF(m): a dribble /
contact sequence) and otherwise sporadically (OBS perf, `AX` = 0x90b5…0x90c4, then 0x9227, 0x9518, 0x95e0 …).

### 2.2 Gate `FUN_140457de0(A)` (OBS decompile + disassembly 0x140457e0c–0x140457f6f)
Evaluation happens only if all hold:
1. `MI+0x116c == 1` (in play) **or** the staged restart `MI+0x1554 ∉ {1,2,3,4}` (1 kick-off, 2 throw-in,
   3 goal kick, 4 corner; meanings from `pes-re-throwin-choreography.md` / duels-fouls §6.1). ⇒ the delivery
   of a kick-off / throw-in / goal kick / corner **never arms flags** (Law 11 exemptions; kick-off trivially).
2. The newest event has a valid player, and not (`type == 0x10 && sub == 2`).
3. `type != 0xe` (type 14 never evaluates).
4. For `type ∈ {10, 13}`: only if the player's team flag `team+0x4bd4+4·slot != 0` **or**
   `s·player.x ≥ −pitch[+0x18]` (comiss at 0x140457f35; ja → skip). INF(l): events of these types deep in the
   player's own end (e.g. a keeper's) do not re-evaluate.
5. **The event's player is not already flagged** in categories 0..2 of his team (`bt` loop 0x140457f50).
   ⇒ when a flagged attacker plays the ball, nothing is re-evaluated; the Judge's `FUN_1405b6490` then sees a
   flagged + involved player and calls offside (duels-fouls §5.1–5.2).

### 2.3 Update `FUN_1404621d0` (OBS 0x1404621d0–0x14046238b)
```
e = newest event; a = e.team
S.bits[a][0..3] = 0
if e.type in {1..8, 11, 12, 16}:   S.bits[1-a][0..3] = 0       (jump table 0x14046238c)
                                    -- types 9, 10, 13, 14, 15 leave the other team's flags alone
FUN_140a7b7a0(S, MA, EV, X+0x260, X+0x250, MI, X+0x40, X+0x1a0)        (§4)
if S.bits[a][0] | S.bits[a][1] | S.bits[a][2]:  FUN_140a7bee0(...)       (snapshot)
S.frame[a] = MI+0x1198
```
INF(m): types 1..8/11/12/16 are deliberate plays (passes 1..3 and 0x10, dribble/feint 3/4, shot 5 …), which
end the opponents' offside phase; 9/10/13/15 are contact kinds (deflection/save/ball-out family) that do not.
The exact type names are still open (duels-fouls §7 item 2).

### 2.4 Line update `FUN_14045cf30` (OBS)
Called from `FUN_140458880` (0x14045889b, the analysis update that also runs `FUN_140a0c3e0` and
`FUN_140461180`). INF(m): once per analysis tick; its live write cadence was not measured (§7).

---

## 3. The offside line `FUN_14045cf30` (OBS 0x14045cf30–0x14045d80b)

For each team `t` (the defenders), `s = s_t`; work in `t`'s own attack frame (`v = s·x`; own goal at
`v = −half`).

1. `s == 0` → `L_t = 0`.
2. Eligible defenders: slot `i` of `t` with `MI+0x1748+id ≠ 0`, body byte `+1 ≠ 'X'` (0x58), and when body
   byte `+3 == 'X'` only while `|z| ≤ pitch+4` (inside the touch lines). INF(m): `'X'` = an off-the-pitch
   action state. `count` = number of eligible defenders.
3. Each defender's **rear point**: start at the root `player+0x55c`, then replace it by any body point lying
   further back (`FUN_140a4c6e0(cand, cur, s)` = `s·cand.x < s·cur.x`), each shifted toward the own goal by
   its radius `r` (`x + s·(−r)`):

| body point | r (m) | constant |
|---|---|---|
| `body+0xc4`, `body+0x70` | 0.15 | −0.15 @**0x14259d64c** |
| `body+0x94`, `body+0xa0`, `body+0x88`, `body+0x7c` | 0.06 | −0.06 @**0x14259d644** |
| hand tips `FUN_140a6c360(body, 0/1)` | 0 | — |
| toe points `FUN_140a6c270(body, 0/1)` | 0.10 | −0.10 @**0x14259d648** |

4. Keep the **smallest** and **second-smallest** `v`, both starting at **0.0** (so values ≥ 0 never count:
   halfway cap). `count < 2` → `second = −half` (`−*(pitch+0)`).
5. Ball: `b = s·ball.x` (`FUN_140a57800(X+0x20)`, the ball centre); `second = min(second, b)`.
6. `|second| > half` → `−half`; `second > 0` → `0`.
7. **`L_t = s·second`** (world x) → `MA+0x44+0x14·t`.

Rule mapping: second-last opponent ✓ (keeper counts like anyone, rule-faithful); ball ✓; halfway ✓;
**arms count for defenders** (game-specific; Law 11 excludes arms for everyone); the rear *surface* is used,
not the centre (game-specific body model).

---

## 4. The rule `FUN_140a7b7a0(S, MA, EV, …)` (OBS decompile; compare block checked in disassembly
0x140a7bd66–0x140a7bdea, constants from the `movss` loads 0x140a7b884–0x140a7b8bd)

```
(team a, slot k) = *(EV+0x180700)          -- the last event-with-player ref
if a > 1: return;  s = s_a;  if s == 0: return
L = *(MA + 0x44 + 0x14·(1-a))              -- the defenders' line, world x
for each slot i of team a (id = 11a+i):
    skip if (a,i) == (a,k)                  -- the player who played the ball
    skip if MI+0x1748+id == 0
    P = root(player+0x55c); m = 0; arm = false
    for each body point q in order, if s·q.x > s·P.x:  P = q and set (m, arm):
        body+0xc4  -> m = 0.15                (arm unchanged)
        body+0x70  -> m = 0.15                (arm unchanged)
        body+0x94  -> m = 0.06, arm = true
        body+0xa0  -> m = 0.06, arm = true
        toe(0), toe(1) (FUN_140a6c270) -> m = 0.10, arm = false
        hand(0), hand(1) (FUN_140a6c360) -> arm = true  (m unchanged: quirk, OBS 0x140a7bc45/0x140a7bca8)
        body+0x7c  -> m = 0.06, arm = true
        body+0x88  -> m = 0.06, arm = true
    tol = arm ? 0.30 : 0.15
    d   = |P.x - L|                          (double-precision fabs, mask @0x142565d30)
    if s·P.x + m > s·L + tol:   c = d < 1.0 ? 2 : (d <= 4.0 ? 1 : 0)
    elif d < 1.0:               c = 3
    else:                       c = none
    if c: S.bits[a][c] |= 1 << id
    S.P[a][i] = P
S.line[a] = L
```

| constant | value | address | role |
|---|---|---|---|
| head/torso margin | 0.15 | **0x1425c48a0** (xmm8) | `m` for `+0xc4`, `+0x70`; also `tol` for non-arm |
| arm-point margin | 0.06 | **0x1425cc238** (xmm7) | `m` for `+0x94/+0xa0/+0x7c/+0x88` |
| toe margin | 0.10 | **0x142588200** (xmm10) | `m` for the toe points |
| arm tolerance | 0.30 | **0x142590d98** (xmm12) | `tol` when the leading point is an arm point |
| near band | 1.0 | **0x14256ae1c** (xmm11) | category 2 / 3 band |
| clear band | 4.0 | **0x1425656c0** (xmm13) | category 1 / 0 split |
| toe offsets | 0.20428, 0.019675, ∓0.10508 (× body scale `+0x58`) | 0x142647da8, 0x142647da0, 0x142647dac/0x142647da4 | toe point = `body+0xd0` + R(`body+0xec`)·offset |
| hand offsets | 0.21, 0.05, 0/−0 | 0x1425c593c, 0x1425a8a28, 0x14259be78 | hand tip = `body+0x94/+0xa0` + R(`body+0xfc/+0x10c`)·offset |

Effective strictness (beyond by more than `tol − m`, measured at the leading point's **centre**):
root 0.15 m; head/torso point 0.00 m (its surface must pass the line); toe 0.05 m; elbow/wrist-type point
0.24 m; hand tip 0.30 m minus the previous margin. INF(m) body-point names: `+0x70`/`+0xc4` head and
torso/hips (radius 0.15, live heights up to 1.64 m), `+0x7c/+0x88/+0x94/+0xa0` elbows and wrists (flagged
"arm"), `FUN_140a6c270` toes, `FUN_140a6c360` hand tips.

**Categories (OBS consumers, meaning INF(h))**

| c | condition | consumers |
|---|---|---|
| 0 | beyond, `d > 4 m` | Judge (punishable), AI "do not chase" (`FUN_14045e0d0` uses `c < 2`) |
| 1 | beyond, `1 ≤ d ≤ 4 m` | as 0 |
| 2 | beyond, `d < 1 m` | Judge (punishable) but the pending commit routes it through **DelayWhistle** (a tight call waits for the move to finish, duels-fouls §5.1); `FUN_14050f580` treats `c < 3` as offside |
| 3 | **onside**, `d < 1 m` | never punished (`FUN_1405b6490` scans `c < 3`). Corrects duels-fouls §5.1's "offside position but not punishable". |

### 4.1 The AI readers of the flags (OBS decompiles, added OffsideAwarenessPort 2026-10-14)

Three readers were decompiled to close "how does an attacker know he is offside". All three index the flag
words the same way: `MA+0x253c4 + 0x10·team + 4·category` (`(0x94f1 + 4c)·4` for team 0, `0x94f5·4` base
for team 1), bit `id & 0x1f`, each gated by the per-side enable `*(G+0x2c0)+0x6e+team`.

**`FUN_14045e0d0` (0x14045e0d0, the per-team ball list).** Builds each team's arrival list
(`MA+0x24840`, 11 × `{id, frames}`) over its own 11 slots. For a slot whose player is flagged, the loop
walks categories 0..3 and, if the bit is set in category `iVar20 < 2` (0 or 1), jumps to `LAB_14045e914`
with `fVar58 = fVar53` = **1000000.0** @0x14259d640 — the "impossible" score the loop seeds with (the
normal seed is 3600.0 @0x142598590). ⇒ **a player offside-flagged in category 0 or 1 is never on his
team's ball list: he does not chase the ball.** Category 2 or 3 keeps his computed score. The same list
is the team's presser (defensive-shape §12), the involvement walker (duels-fouls §5.2 step 6), the
key-player fallback below, the keeper's decision (goalkeeper §7) and the camera target
(`[this+0x1e8] + 0x24840`, pes-re-camera-target.md).

**`FUN_14050f580` (0x14050f580, the team's ball candidate).** Takes the team's key player `team+0x78`
when he is not flagged in a category `iVar5 < 3` **and** `param_3 == 0`; otherwise walks the
nearest-to-ball list `MA+0x2470c + team·0x58` (11 × `{id, dist}`) and returns the first entry that is
active, not flagged in 0..2 (with `param_3 == 0`), `team+0x4bd4[slot] == 0`, in a ball-seeking AI state
(`player+0x5d0` ∈ {3, 4, 5, 0x12, 0x14, 0x16, 0x18, 0x34, 0x3e}) and not falling/sliding
(`human+1` not 0x11/0x57). Used by the attack-chance test `FUN_14050f7e0` → the Judge's advantage
(duels-fouls §3.1) — the caller passes `param_3` to relax the flag test.

**`FUN_142117c30` (0x142117c30, the receiver enumeration).** `table+0x3c+4·slot` carries the status code
(carrier-ai §4.3); the offside code is `LAB_14211818f`:

```
fVar30 = s * table[+0x34]                       -- the carrier's offside line (below)
if  fVar30 < s * (0*vel + pos)      -> status 6   -- his position now
 or fVar30 < s * (T16*vel + pos)     -> status 6   -- his position 16 frames ahead
```
with `table+0x30` = `int(gameSpeed × 0.3)` = **16** frames (`(int)(uVar7 * 0.30 @0x142590d98)`, `uVar7`
from the fps getter) and `table+0x34` = the running **maximum**, from 0, of the attack-signed x
(predicted 16 frames ahead) of the first **five** entries of the depth-sorted defenders' table
`FUN_140a714e0(MA, s)` (team block of the carrier's opponents), then of the ball (also 16 frames ahead).
⇒ **a team-mate whose current or near-future position is beyond the deepest of the five last defenders
and the ball is never offered the ball.** Note this is the carrier's own line, not the referee's
`MA+0x44`, and it is a *maximum* (deeper of the two) floored at halfway.

### 4.2 What does NOT read the flags (OBS decompiles, 2026-10-14)

The off-ball run selectors' stop tests were checked for the same block and read none of it:

| function | role | offside reads |
|---|---|---|
| `FUN_140688c00` (0x140688c00) | common run stop test | none (open-play, possession, carrier/PASS_GET, loose-ball, target reached, `s·x > F[4]+2.5`, last pass) |
| `FUN_140607a60` (0x140607a60) | Chance stop test | none (pad-controlled, carrier, PASS_GET, 11-frame minimum, then the common test) |
| `FUN_140612830` (0x140612830) | LineBreak stop test | the ball line only: `FUN_140a4c720(MA+0x2482c, runner.x, team+0x254, −1.8 @0x1425cdbe0)` |
| `FUN_140620900` (0x140620900) | PostPlay stop test | the ball line only: `FUN_140a4c720(runner.x, MA+0x2482c, team+0x254, −2.5 @0x1425c74d0)` |
| `FUN_14059ba60` (0x14059ba60) | the Chance target search behind the score of `spaceRun.o` | no `MA+0x44` / no flag words |

⇒ **PES's attackers do not hold a run because of offside.** A run is stopped by the ball line
(±1.8–2.5 m), not by the offside line; PES runs a player offside and lets the referee's flag stop the
play. The `spaceRun.o` key `checkOffsideLineDistX 12` is listed in pes-re-off-ball.md §1.5 but no reader
among the run selectors uses it — open item O3 of that report still stands.

---

## 5. Inputs, outputs, RNG

- Inputs: newest ball event (`EV+0x180698` type/ref, `EV+0x180700` last player ref), `MI+0x116c`, `MI+0x1554`,
  `MI+0x1748[id]`, `MI+0x1198`, player root `+0x55c` and z `+0x564`, body points (§3/§4), team sign `+0x254`,
  team flag `+0x4bd4[slot]`, pitch `+0/+4/+0x18`, ball position.
- Outputs: `S.bits[a][c]`, `S.frame[a]`, `S.P[a][·]`, `S.line[a]`, snapshot `S+0x184`; consumed by the Judge
  (`FUN_1405b6490` → `FUN_1405b6660`, duels-fouls §5.1) and the AI readers (`FUN_14050f580`, `FUN_14045e0d0`,
  `FUN_140457de0`, `FUN_1404b43d0`, `FUN_1405afca0`, stats observer `FUN_140a23c10`).
- RNG: **none** (no generator call in `FUN_1404621d0`, `FUN_140a7b7a0`, `FUN_14045cf30`, `FUN_140457de0`).

---

## 6. What GF must change (`src/sim/onthepitch/offsiderule.cpp`, `referee.cpp` `BallTouched`,
`AIfunctions.cpp` `AI_GetOffsideLine`) — no implementation done

GF today (read 2026-10-14): `AI_GetOffsideLine` = second-deepest opponent **root** x, the **predicted** ball
x if deeper, capped at halfway with a 0.01 m offset, clamped to `pitchHalfW`; `BallTouched` flags a team-mate
of the toucher when `x·side < line·side − 0.20` ("relax"); one boolean per player, no category.

1. **Line** (`AI_GetOffsideLine`, used by the referee): `L = s·min(0, second-rearmost, s·ball.x)` from the
   **current** ball centre (not the prediction); second-rearmost starts at 0 (no 0.01 offset); fewer than two
   eligible defenders → goal line; clamp to the goal line (`pitch+0`, read at runtime, not `pitchHalfW`).
   Eligible = active and on the pitch laterally. GF has no body points: use the root with the 0.15 m torso
   radius as the rear surface — `// RE-APPROX` (PES takes the rearmost of root and 9 body points).
2. **Attacker test**: replace the 0.20 m relax with `s·x + m > s·L + tol`, `tol = 0.15`; with roots only,
   `m = 0` (root) — `// RE-APPROX` for the body-point model (PES: head/torso surface strict, toes 0.05,
   arms 0.24–0.30).
3. **Categories**: store `c ∈ {0,1,2,3}` per flagged player from `d = |x − L|` with 1.0 / 4.0 m; punish only
   0..2; route category 2 through the Judge's delayed whistle; expose 0..1 (or 0..2) to the AI "do not chase"
   helpers.
4. **Cadence / phase**: evaluate at every ball event of the toucher's team unless the toucher is flagged
   (GF already re-snapshots on own-team touches); clear the opponents' flags only on deliberate-play types
   (GF's `TouchResetsPhase` ≈ PES's type split; keep it until the type table is named); clear both teams'
   flags at a stoppage; do not evaluate the delivery while a kick-off/throw-in/goal-kick/corner is staged
   (GF's `ShouldSnapshot` matches).
5. **Constants from the exe at runtime** (`pes::PeImage::ReadVa`, the `LoadJudgeConstants` pattern): the nine
   `.rdata` floats in §3/§4 (0x1425c48a0, 0x1425cc238, 0x142588200, 0x142590d98, 0x14256ae1c, 0x1425656c0,
   0x14259d64c, 0x14259d644, 0x14259d648).

---

## 7. Live check

- **OBS (perf):** the only writers of `MA+0x253bc..0x253c0` and `MA+0x25504` during 45 s of open play were
  `0x140462376` and `FUN_140a7bee0` (§0); the frame written was always the current frame.
- **OBS (memory):** the block decodes as §1 (frames, zero bits after a dead-ball phase, 22 leading points with
  foot-to-head heights, lines 43.07 / −0.14 m). One consistency check on the stored evaluation: in team 0's
  attack frame (`s = −1`, its keeper stood at x = +49.8) its 11 leading points all lay ≥ 5.8 m behind its line
  (`s·L = +0.14`) → no bit expected, none set.
  After full time the line block held ±52.56 for both teams = the goal-line clamp of §3 step 6
  (INF(m): `pitch+0` = 52.56 m).
- **OBS (flag transitions, a later match, `watch_bits.py` → `bits_log0.jsonl` + `bits_log.jsonl` in the
  OffsideRE scratch dir).** `MA` was relocated with the perf execute-breakpoint at `FUN_14045cf30`; the watcher
  logged every change of `S.frame`/`S.bits` (3 ms poll) with the stored `P`, `L`, the team block and the 22 roots
  (roots are read up to ~0.3 s later, because `G`'s player table is filled only ~3 % of each tick).
  `check_bits.py` replays §4 on the stored `P` and `L`:
  - **2 498 player-evaluations agree, 0 contradict.** 6 fall inside `0 < s·(P.x − L) ≤ 0.30` (the band where
    `tol − m` decides) and are consistent with some body point (e.g. +0.297 → cat 2, +0.164 → cat 3 needs an
    arm-type leading point).
  - 4 apparent misses (no bit although `P` is 28–36 m beyond) all have `P` **unchanged since the previous
    evaluation** while the player had moved — the ball player, whose point the loop skips (`FUN_140a60300`
    match). At frames 38197/38221/38240 that player's root was 0.3 m from the ball.
  - Example (team 1 attacking +x, `s = +1`; team 0 defending, keeper root x = 50.43):

    | eval frame | `L` = `MA+0x44` | second-rearmost defender root x | leading points `P.x` → expected / logged category |
    |---|---|---|---|
    | 41049 | 37.625 | 37.23 (`MA+0x40` = 37.231 = that root) | slot 8 39.42 (+1.80) → 1/1; slot 9 38.51 (+0.885) → 2/2; slot 10 37.614 (−0.011) → 3/3 |
    | 41600 | 38.184 | 37.56 | slot 7 38.92 (+0.74) → 2/2; slot 8 38.87 (+0.69) → 2/2; slot 9 39.46 (+1.28) → 1/1 |
    | 41624 | 37.195 | 36.72 | slot 7 36.95 (−0.25) → 3/3; slot 8 38.83 (+1.63) → 1/1; slot 9 37.99 (+0.80) → 2/2; slot 10 36.24 (−0.95) → 3/3 |
    | team 0 event at 41691 | — | — | **team 1 bits cleared** with `S.frame[1]` still 41624: the other-team clear of §2.3 |

  - Line vs roots (195 evaluations): PES's `L` lies a median **0.19 m deeper** (toward the defenders' goal)
    than the second-rearmost root, 152/195 within 0.5 m — the rear-surface + body-point model of §3
    (outliers ±9–18 m are late root reads during fast transitions). The ball-limited and halfway-capped
    cases were not isolated.
  - No offside was called in the window (`MI+0x1588` never 1), so the flag → call path (duels-fouls §5.1)
    was not re-observed live.

---

## 8. Killed hypotheses

- "The flag block is bulk-copied" — killed: direct `or` stores at 0x140a7bdea, perf-confirmed writers.
- "No instruction writes the flags with a recognisable base" — the base is built by `add 0x24d70` + `add 0x608`
  (two immediates outside every scanned range), then `this`-relative displacements 0x44/0x4c/0x6c/0x174/0x184.
- "Category 3 = offside position, not punishable" (duels-fouls §5.1 INF) — killed: category 3 requires *not*
  beyond the line; it is the near-level onside band.
- "The level tolerance is 8 m / 0.2 m" — killed: 0.15 m (0.30 m arms) at 0x1425c48a0 / 0x142590d98, applied
  per body point with surface margins.
- "`FUN_14045e3db` / `FUN_14045e030` / `FUN_140a8ff10` write the flags" — remains killed (attack-decisions §9).
- "`MA+0x44` is a formation-shape value" — killed: it is the offside line (§3); front/mid/back are
  `+0x38/+0x3c/+0x40` (`FUN_14045bfe0`).

## 9. Open

1. Ball-event type names (which of 9/10/13/15 are deflection/save/ball-out) — needed to name §2.3's split.
2. Kind-0 message sub-codes 2/3 (the all-clear) — poster not traced.
3. `pitch+0x18` and `team+0x4bd4[slot]` semantics in the gate (§2.2 step 4).
4. Body-point names (INF(m)) — a live dump of the body object would settle them.
5. A live offside **call** (flag → `MI+0x1588 = 1`) with the flag frame, and isolated live cases of the ball-
   limited / halfway-capped / fewer-than-two-defenders line (§7).
