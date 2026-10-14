# PES 2021 goalkeeper behaviour — evidence report

Scope: read-only RE of the shipped image (`<image>/pesmem.bin`, base `0x140000000`) plus the
constant bins over the game VFS. Goal: formulas, constants and code addresses for keeper
positioning, shot response, catch/punch/deflect, crosses, distribution and 1v1/penalties.
Tags: **OBS** = observed in bytes/disassembly (address given), **INF** = inferred (confidence),
**EXT** = third-party write-up, unverified unless stated. No byte copies of game data; only
individual constants and layouts. Live process: read-only (`STATE=OPEN_PLAY` confirmed during
the session); no writes, no input, no ptrace.

Prior docs this builds on: `pes-re-sim-motion-mapping.md` §8 (keeper state machine
`FUN_140791ec0`, writers `FUN_14078c820`/`FUN_14078ce90`, deciders `FUN_1406ec260`/`FUN_1406ec5f0`),
`pes-re-matchlogic.md` §3.5 (pesSmart/GK numbers), `pes-re-team-plans.md` §6 (GK open item),
`pes-re-collision.md` (GK capsules), `pes-re-kick-launch.md` §7.5 (keeper launch, not re-traced).

---

## 0. Where the keeper lives in code (OBS)

Sim-side AI actions (`match::player::ActionKeeper*`, RTTI in image, resolved via `rtti.py`):

| class | vtable | exec (slot 4/7) | CanEnter |
|---|---|---|---|
| `ActionKeeperCatching` | `0x142631e50` | `0x1409d6180` | `0x1409d58b0` |
| `ActionKeeperPunching` | `0x142631e98` | `0x1409d69f0` | shared `0x140920b10` |
| `ActionKeeperDeflect` | `0x142631ee0` | `0x1409d75d0` (stub, see §3.5) | `0x1409d6c10` (stub) |
| `ActionKeeperBlock` | `0x142631f28` | `0x1409d82f0` | `0x1409d8000` |
| `ActionKeeperBasePosition` | `0x142632090` | `0x1409dc040` (+ subs `0x1409de8c0`, `0x1409df7d0`, tail `0x1409e0390`–`0x1409e0796`) | movement |
| `ActionKeeperPKSaving` | `0x142632150` | `0x1409e5570` (+ `0x1409e5600`) | `0x1409e4bc0` (stub) |
| `ActionKeeperPreSaveOperation` | `0x142632258` | `0x1409e6870` | shared |
| `ActionKeeperThrow` / `PuntKick` / `ThroughThrow` | `0x142631dc0` / `0x142631d78` / `0x142631e08` | anime `0x140748b20` | `0x1409d0950` (throw, OBS below) |
| `ActionKeeperSavingMove` | `0x142632528` | `0x1409e77e0` | movement |
| `ActionKeeperPickupBall` | `0x142632360` | movement | `0x140ca1a50` = true |

Anime-side save classes (`match::anime::action::goal_keeper::*`): `SavingBase` vtable
`0x1425e7360` with shared `CanEnter 0x1407556c0`, `Update 0x1407506b0` (dispatcher, OBS via
GhidraMCP), `Enter 0x14074d9e0`, `OnEvent 0x14074cf30`. `Update` switches on the action-kind
byte (`*(P+0xab8)->slot0x20`): 1→`0x14074ad00`, 2→`0x140755c50`, 3→`0x140757fb0`,
4→`0x14074dd30`, 5→`0x14074dac0`, 6→`0x1407706a0`, 7→`0x1407715d0`, default→`0x14076ecb0`.
Reach/weight helper `0x140750b50` (OBS, full decompile in hand). Catch-timing decider
`FUN_1406ec260` → `FUN_1406ec2a0` (OBS, full decompile). `FUN_1406ec5f0` = "motion finished"
(`slot8 == FUN_140a627e0(u,0)`), `FUN_1406ebf80` = `(P+0x2460 & 0x60) != 0`.

Contact layer (from `pes-re-collision.md`, not re-traced): keepers use the 15-capsule
`Atari_Ball_Hit` table at `0x1437f1d50` (outfield 11 = same minus `_11/_12/_14/_15`);
GK-only extras are the forearms (r 4 cm, len 37 cm) and hands (r 5 cm, len 8 cm).
`goal_keeper::PickupBall` arms legs+forearms+hands; hand bones take a separate branch gated by
flag `P+0x2460` bit 8 (`FUN_1406edce0`).

---

## 1. Positioning (base position + sweeper rush)

### 1.1 Entry (`ActionKeeperBasePosition::exec`, `0x1409dc040`, OBS static)

Signature `(action, ctx, ballinfo)`. Verified instruction facts:
- `edx = [ballinfo+4]`; side flag `r14d = (0xb <= edx < 0x16)` (the `test/js; cmp 0xb/jl; cmp 0x16/setl`
  idiom, `0x1409dc0d4`–`0x1409dc0f8`). Passed to registry accessor `FUN_140a48a30`
  (OBS decompile: `param2<2 ? *(base+(param2+0xa1)*0x28) : *(base+0x1928)`), i.e. own-side select.
- Player struct from `FUN_140a48a10` (OBS: `param2<0x1a ? *(base+(param2+0xa5)*0x28) : …`);
  target triple read at `+[0x55c/0x560/0x564]` (`0x1409dc10a`–`0x1409dc111`).
- Bearing: `ang = atan2(dx,dz)` via `FUN_140a92d40`, wrapped `if (ang<0) ang += 2π` then
  `ang *= 57.29578` (OBS: `addss 2π [0x1425795e4]`, `mulss 57.29578 [0x14259be40]`).
  Constants: `2π = 6.2831855`, `57.29578`, `360.0 [0x14259858c]`, `-1.0 [0x142588218]`,
  `1.0 [0x14256ae1c]`, `0.5 [0x142565600]`, `60.0 [0x14259a764]` (= game speed,
  `FUN_141e532d0`), `0.6944444 [0x14259bdd0]` (= 25/36, INF high: per-tick factor at 54 Hz…
  OBS value only), `3.0 [0x14258c264]`, `20.0 [0x14259be1c]`, `11.0 [0x14259bdfc]`.
- Mode branches read match fields `+0x116c` (mode), `+0x1554` (situation), `+0x1168`, `+0x1558`,
  `+0x11a8`, `+0x173c`, `+0x1198` (match clock), `+0x11a4`, `+0x119c`, `+0x11d4`
  (OBS offsets, semantics INF: same fields the PK/presave code uses for mode/situation/clock).
- Output target triple staged at `[rsp+0x38/0x3c/0x40]`, committed to the action struct;
  yaw staging at `[rsp+0x48]` via angle-wrap helper `FUN_140a8f7c0` with rate `3.0`.
- Tail (`0x1409dc933`–`0x1409dc9c7`, OBS): final target = `f(ball, keeper)`:
  `dist = sqrt(dx²+dz²)` (`mulss/addss/sqrt 0x1415a1780`), then `FUN_140439e50` blends toward it;
  style/role byte `[rax+1]` vs `0x47`/`0x36` gates (see §1.3).

### 1.2 Sweeper/rush subroutine (`0x1409de8c0`, OBS static)

Called from base-position for the loose-ball/rush case. Verified structure:
- Keeper factors: `xmm11 = ballX + 1.0`, `xmm0 = 8.0 - ballZ` (`[rbx+0x18]`, const `8.0
  [0x14259bdf4]`); `xmm11 *= keeper Seniority?` — `mulss xmm11, xmm14` where
  `xmm14 = (float)[styleCtx+0x254]` (role/position byte), then `xorps` with `-0.0` mask
  (`0x1425656d0`) i.e. negate → `xmm11 = -(ballX+1)*seniority`-shaped term (INF medium on naming;
  OBS on ops). Keeper reach term `xmm1 = seniority * [action+0x74]`.
- Distance gate: `d = sqrt(dx²+dy²)` vs `xmm13` (sqrt result compared at `0x1409deb08`
  against `18.0 [0x14259be18]`, INF: 18 m rush radius cap).
- Claim gates (all OBS comparisons): predicted-ball height vs `250.0 [0x1425ab4c0]`;
  ball-x vs goal-box terms; `xmm0 /= 12.0` (`divss [0x14259be04]`);
  time windows `(matchClock - actionStart)` vs `tickCount/16` (`mul 0xaaaaaaab; shr 4`;
  OBS idiom, = divide by 16, INF: frames→seconds at ~54 Hz… reported as ops only),
  and `(matchClock - actionStart)` vs `tickCount/4` (`shr 2`).
- Ball-kind gates: kind bytes must be in `{1,2,3}` (`lea eax,[rcx-1]; cmp eax,2; jbe`) or
  `0x10`, else bail (`0x1409debc0`–`0x1409debd1`, `0x1409ded63`–`0x1409ded70`).
- Style gate inside rush (`0x1409ded4a`, OBS): `ecx = [styleObj+0x48]; shr ecx,0x13; test cl,1;
  je bail` — bit **19** (`0x13`) of the keeper's style word must be set to take the
  contested-ball rush branch that follows (ball within `xmm8` of `[r12+8]`-scaled point and
  arrival-time wins scaled by `0.4 [0x14259b89c]` and `0.6 [0x14257a190]`).
- Rush target write (`0x1409dedcd`–`0x1409dedec`, OBS): copies `[r12]` triple to output —
  i.e. the rush target is the contested-point triple, not a fixed depth (INF medium).
- Fallback aim math (`0x1409dee2f`–`0x1409dee91`, OBS): `xmm6 = min(xmm6, …)` clamp,
  `xmm6 *= 0.5`, angle via `FUN_140a92c80` twice, `xmm9 += 90.0`, `xmm0 += xmm8`,
  `xmm11 += xmm7`; commit rule `if ([action+0x94] >= xmm8) keep else call 0x1409da470`.
  Gate `xmm0 vs 1.5 [0x14259bddc]`.

### 1.3 Offensive/Defensive GK style terms (OBS addresses, INF mapping)

- Engine style word at `[X+0x48]` (matches prior report's style bitmask location; the word is
  read at `0x1409ded4a`, `0x1409dfa69`, `0x1409e048a`).
- `0x1409dfa69`–`0x1409dfa7c` (rush helper `0x1409df7d0` region, OBS): `r15d=[rax+0x48];
  r13d = (r15d>>0x13)&1; r15d = (r15d>>0x14)&1` — bits **19** and **20** extracted together;
  they feed the branch at `0x1409dfa80` (`[rdi+0x58]==2` set/keep) and the target choice at
  `0x1409dfdcd` vs `0x1409deebc`.
- `0x1409e048a`–`0x1409e04a8` (positioning tail, OBS): `ecx=[rax+0x48]; ecx>>0x14 &1 → bit20;
  eax>>0x13 &1 → bit19`; both stored and consumed by the depth/width blend that follows
  (`xmm7 *= seniority; xorps` negate; `FUN_140439e50`; targets compared with `17.0
  [0x14259d614]` vs `15.0 [0x14259be0c]` at `0x1409dfd92`/`0x1409dfd9c` — INF medium: the two
  constants are the style-dependent line depths, assignment to bits open).
- Prior EXT (matchlogic §2.6, live spot check): engine bit 19 = Offensive GK on the observed
  keeper. So bit19 = Offensive GK (EXT, one sample), bit20 = Defensive GK (INF low: the paired
  extraction; which branch is "defensive" not established — open).

### 1.4 Line depth vs ball distance (partial, OBS pieces)

- Depth output field `[action+0x44]` written in the rush helper (`0x1409dfc8a`,
  `0x1409dfd83`) from the lerp chain of `0x1409dfb92`–`0x1409dfc8a` (OBS): two candidate
  depths from `[rbx]`-table terms scaled by `×100/100` (`0x14253ee30`), `+6.0 [0x14259bdec]`,
  `×4.0 [0x1425656c0]`, `+9.0 [0x14259d60c]`, second `×100, ×7.0 [0x14259bdf0], /100`, then
  `min/max` select vs keeper-position terms (`[rax+0x875c]`, `[r14+rcx*4+0x40]` index by
  keeper slot). Exact closed form not reduced — open (need a worked trace).
- Angle-bisector hypothesis: NOT confirmed. The bearing uses ball→keeper atan2 only; no
  post-bisector computation was found in the traced paths (bounded negative: no second atan2
  or post-coordinate reads in `0x1409dc040`–`0x1409dc9c7`, `0x1409de8c0`–`0x1409e0319`).
  The `[r15+0x24840]` stride-`0x58` table reads in the rush helper are goal-area indexed
  (INF low) but not decoded.

---

## 2. Shot response

### 2.1 Reaction delay (OBS: per-action countdowns, not one global delay)

- Punch (`0x1409d69f0`, OBS): `[action+8]` phase; first entry snapshots target from traj slot
  `[player+0x1995]`; later entries decrement `[action+0x1c]` and test arrival
  `dist2D(target, traj[slot]) <= 0.1` (`comiss vs 0.100000 [0x142588200]`); sets
  `[player+0xfd9]=1` when inside 0.1 m. The countdown start value comes from `[rsi+4]`
  (frames, source open — INF: reaction+travel budget).
- Block (`0x1409d82f0`, OBS): entry gate `[action+8]==0`; `[action+0x1c] = matchClock
  (+0x1198)` snapshot. No frame countdown; the block commits on the height/angle gates below.
- Catch (`0x1409d6180`, OBS head): shared gate `FUN_140518fe0`; penalty branch
  (`+0x116c==3 && +0x1554==6`) or ball-kind `==6` (`FUN_140a3e190(...,0) → +0x1c`);
  phase `[action+8]`.
- Anime-side catch-frame test `FUN_1406ec5f0` (OBS): true iff current motion index
  (`slot8`) equals `FUN_140a627e0(u,0)`; keeper state machine advances 2→3 on it and sets
  `P+0x2460 |= 0x300`.

### 2.2 Catch-possible timing window (`FUN_1406ec2a0`, OBS full decompile)

Inputs: `(P, count, t, rate)` where the caller (`FUN_1406ec260`, OBS) passes
`count = FUN_1406b03d0(...)` (predicted-frame count) and `rate = *(P+0x3758)`.
Player table: entries at `P+0x354`, stride `0x14`, `{u32 actionId,…}`; count at `P+0x420`;
per-entry floats at `P+0x35c` and scale at `P+0x41c`. Current action id from
`slot0x18`, must be `< 0x1125`; linear search (max 10, `uVar8` sentinel) for the matching entry.
Formula (OBS, constants resolved):
```
t'  = t * P[0x41c]
h   = t' * 0.05                         # 0.05 [0x1425a8a28]
r   = count / (rate ? rate : gameSpeed) # gameSpeed 54.0 [0x141e532d0]
b   = P[entry].base                     # P+0x35c+idx*0x14
catch = (t'*0.5 + b < r) && (r - h <= t'*1.5 + b)      # 0.5 [0x142565600], 1.5 [0x14259bddc]
tooFar = (t'*1.5 + b < r) && (r - h <= t'*2.5 + b)     # 2.5 [0x14259bde4]; forces catch=0
return (actionId & 0xff00) | catch
```
INF medium: `r` is ball-frames-to-contact scaled to the keeper's clock; `b` is a per-action
reach bias; the `0.5/1.5/2.5` bands are catch / edge / out-of-reach with 5 % hysteresis.

### 2.3 Dive selection: reach, height, side (OBS pieces)

- Dispatcher `0x1407506b0` (OBS): kind byte → branch (listed in §0). Side/height enter via the
  row id (`uVar28`, e.g. `0x63c/0x564/0x5ea/0x644–0x646/0x692` in `0x14074ad00`) and the column
  from `FUN_1406de990(row, angleDeg)`; height-blend parameter `N/11` selects the parametric
  member (prior report §8.2(b), not re-traced).
- Reach test (`0x14074ad00`, OBS lines ~100–170): candidate clip accepted iff ball-to-hand
  distance `< 0.1` (`fVar43 = 0.100000 [0x142588200]`) after shifting the candidate contact by
  `*(P+0x3b18) * [-0.??] * 0.5`; scan over `FUN_140845bc0()` records at `0x1434955b8`
  (stride 16 B: `{float actionId, mirrorFlag, lo, hi}` — INF medium on field names).
- Playback-speed (dive urgency) formula (`0x14074ad00`, OBS):
```
w = (clipFrame - curFrame) / gameSpeed * 1.5     # 1.5 [0x14259bddc]
w = max(w, 0.3)                                  # 0.30 [0x142590d98], or 0.12 [0x1425cb294]
                                                 #   when (P+0x380e - 10) not in {0,1}
w = max(w, 0.5) for rows 0x63c/0x564/0x5ea/0x644-6/0x692 or when P+0x37cc > 1  # 0.5 [0x142565600]
if (distXZ(ball, keeper) > 0.4 && w <= 0.25) w = 0.25   # 0.4 [0x14259b89c], 0.25 [0x142592228]
SetMotion(P, row, col, mirror, speed=w, 0.1 /*xfade*/)  # 0.1 [0x142588200]
```
- Where "save is possible" is decided: the anime branch commits via `FUN_1406eebb0(P,2|3,0x5e)`
  (kind 2 = dive-save vs 3, gated on row id ranges `0x7b5/0x7b7/0x7d8…`, OBS) and records
  `P+0x2808 = matchClock`, then `FUN_1406ee640(P)`.
- Attribute→reach (`0x140750b50`, OBS full decompile): reads abilities `0x24` and `0x25`
  via `FUN_1406e6660` (ability indices; mapping INF medium from matchlogic §1.2 order:
  `0x24` = GK Reflexes, `0x25` = GK Reach — see note below):
```
w = R<=40 ? 0 : R>=99 ? 1 : (R-40)/59      # 40.0 [0x14259be2c], 99.0 [0x1425c48cc], 59.0 [0x1425cb2fc]
budget = round(gameSpeed * (w*0.1 + T[0x2f].x20))  # 0.1 [0x142588200]; T from FUN_141e5ca20(0x1437f4808,0x2f)
codes: returns 0/1 (save/no-save path), 3/4/5/7/9 by kind/flag sub-branches (OBS; exact
  semantic of each code open — callers in the packed region)
lottery bits: (matchClock/100 vs ability%100), (uVar3 < clock%100), tick/60*0x11/0x3c vs param_9
```
  Bounded negative (OBS): no `FUN_1406e6660` calls with `0x22/0x23` (Catching/Clearing) were
  found in the scanned keeper execs (`0x1409d69f0`, `0x1409d82f0`, `0x1409d58b0` — all
  `mov edx,imm → call` sites checked); Catching/Clearing do not enter the sim-side save
  decision in these paths. They may enter the anime-side row choice (open).
  Ability-index note: matchlogic doc §1.2 lists field ids `0x26–0x2a` (GK Aware/Catch/Clear/
  Reflex/Reach) while the text at §251 and the `0x1406e6660` index space use `0x22–0x25` for
  four GK abilities; `0x24/0x25` = the last two = Reflexes/Reach under an in-order mapping
  (INF medium — the consumer order `0x24 then 0x25` with `0x24` driving the weight matches
  "Reflexes scales time, Reach scales distance", but the index→stat binding is not proven).

---

## 3. Catch / punch / deflect / parry (open item: threshold — PARTIALLY CLOSED)

### 3.1 Catch gate (`ActionKeeperCatching::CanEnter`, `0x1409d58b0`, OBS static)

- Pre-gates: role byte `[playerinfo+1] == 0x36` (54, OBS at `0x1409d5940`); action phase
  `[action+8]` state machine (0/1/2/5 branches, OBS).
- Trajectory scan (OBS): ball future table via `FUN_140a6cf70(base, i)` = 
  `i<250 ? base+((i*5+0x11)*8) : base+0x2770` (OBS decompile — 250-entry table, 5 qwords/frame
  + 0x88 header). For `i` in `0..250` (`cmp esi,0xfa`): predicted point `p = T[i]`;
  height gate `p.z - keeper.z` vs `1.2 [0x14259d5d4]` / `0.8 [0x142572348]` shaping;
  found index stored to `[rsp+0x54]`.
- Decision call `FUN_14058bbe0(...)` (OBS call site; semantics INF: the catchability evaluator,
  args include the found index, axis flags `[rbp+0x70/0x88]` from `[rbx+0x1996/0x1998]`, ball
  extents). On true: writes catch target `[action+0x10]`, `[rbx+0x8b0]`, sets action kind `0xa`
  via `FUN_1406f0590`, phase `[action+8]=5`.
- Fallback: `FUN_14058eee0` re-predict + `0x1409d6630`-family aim when the direct catch fails.

### 3.2 Punch (`ActionKeeperPunching::exec`, `0x1409d69f0`, OBS static)

- Arrival gate 0.1 m (see §2.1). Punch target `[player+0x874+0x3c]` via helper `0x1409d6940`:
  sets kind `9`, `FUN_1406f0590(...,4,...)`, aim from `0x1409d6630`, commit in `0x1409d6510`.
- Aim helper `0x1409d6510` (OBS): `d² = dx²+dz²` (keeper→aim); if `d² > 625.0 [0x1425ab4c4]`
  (i.e. aim > 25 m away) recompute via `FUN_140439ef0`, wrap `±360`, scale `25.0
  [0x14259be20]`. So punches are capped/redirected at 25 m (OBS; direction = away-from-goal
  blend, INF medium).
- Aim tables in `0x1409d6630` (OBS constants): `1600.0 [0x14259ce40]`, `625.0`, `45.0
  [0x14259be30]`, wrap `±360`, per-kicker table at `[r15+slot*0xc8+0x22e4c]` indexed by
  kicker slot (`imul rsi,0xc8`), 11-wide sub-table (`imul 0xb`) gated by `0x15`.
- No ability-index reads in the punch path (bounded negative, §2.3).

### 3.3 Block (`0x1409d82f0`, OBS static)

- Height bands (OBS): `45.0 [0x14259be30]` selects the low/high branch; `120.0 [0x14256ae30]`
  the second band; crossfade adds `90.0 [0x14259be5c]`; idle branch rate `1.0`, scale `2.0
  [0x142565604]` / `3.0 [0x14258c264]`; frame quantum `60.0 [0x14259a764]`.
- Writes: target `[action+0x10]`, `[rbx+0x8b0]`, `[rbx+0xe8c]`, kind `0xa` at `[rcx]`,
  `FUN_1406f0590(...,4,...)`, flags `[rbx+0xfdc/fdf]=1`.

### 3.4 The pesSmart speed numbers (open — attribution corrected)

- matchlogic §3.5 lists (as OBS/Aoba): GK advantage `1.7/1.5/1.3`, catchable `85/70/60`,
  `frontAdjustRate 1.1`, punchable `110/80`. Tried this session (OBS):
  - Decoded `gk.o` (128 B, `constant_player.bin` off `0x1302a`): words (floats)
    `[64, 1.12, 1.54, 0, 9, 10, 40, 18, 36, 30, 27, 4, 25.15, 4.85, 0, 0 | 48, 30, 40,
    0.77, 56, 10, 0.44, 38, 0.295, 0, 44.5, 0, 46, 46, 18, 10]` — contains NONE of
    85/70/60/110/80/1.1/1.7/1.5. Single sub-offset `64`. Key strings confirm the
    `catchable{catchable,SpeedDefaultMax/Min,SpeedMin,frontAdjustRate,punchableMax/Min}`,
    `gkPress{advantageousRate ×3, isAutoKickAfterRun}`, `isShouldTurnBeforePass{angle ×3,
    checkKickPointDist, checkDistMiddle/Short.dist, dist}` structure, but the value→key
    binding is not recovered (layout-rule check against team `support.o`/`defenceMark.o`
    shows this install's values differ from the doc's vanilla table — possible 4cc override
    of the VFS winner; open).
  - Searched both constant bins for the exact floats: `85/70/60/110/80` occur only in
    `ballplayer.o` regions (distances, not GK); `1.1` and `1.7` occur nowhere; `1.5` in six
    places (none GK-attributed); `1.3` once inside `pesSmart.o +0x54` (key unmapped).
  - Scanned keeper code for immediates/comparisons against 85/70/60/110/80/1.1: none found
    in `0x1409d58b0/0x1409d69f0/0x1409d82f0/0x14074ad00/0x140750b50` (bounded).
- Conclusion: the catch-vs-punch-vs-deflect decision **as implemented** is the timing-window
  test (§2.2) + trajectory-scan gate (§3.1) + ability weight (§2.3), NOT a direct
  ball-speed-vs-85/70/60 compare in any traced path. The speed table, if it exists, lives in
  an unmapped pesSmart/gk key or the packed think-unit region. The "threshold" open item is
  therefore re-scoped: the *mechanism* is closed above; the *tuning numbers* stay open.

### 3.5 Parry direction (partial)

- Punch parry: §3.2 (25 m cap, recompute via `0x140439ef0`). OBS.
- Deflect parry: sim-side `ActionKeeperDeflect::exec 0x1409d75d0` and its CanEnter
  `0x1409d6c10` are **stubs** (`jmp` out of the mapped image to `0x1440f35f0`/`0x1440ecf40`;
  OBS bytes `e9…`), i.e. the deflect decision runs in a packed/relocated region the static
  image does not contain. Anime-side `Deflect::Update 0x140772260` (OBS decompile) only moves
  contact flags (`0x1406fb3b0/0x140764760/0x1406fa9f0/0x1406faaa0/0x1406e6620/0x1406fb440`).
  Tried: direct disassembly (stub), GhidraMCP (returns the flag-mover only). Open: deflect
  side/angle selection.

---

## 4. Crosses and high balls (partial)

- `ActionKeeperSavingMove::exec 0x1409e77e0` (OBS static): cross-claim path. Verified ops:
  ball speed `[rbx+0x12e4] × gameSpeed / 48.0 [0x142588214]` → `cvttss2si` index, clamped to
  `249 (0xf9)` → predicted point `FUN_140a6cf70(base, idx)`; keeper-ball compare with ball
  radius `0.10868593 [0x142598958]` and `0.21737187 [0x1425a9bd4]`; angle gates with
  `0.01/100/180/360`, `0.2 [0x142598578]` blend, `-1.0` clamp; ball-kind check `== 0x81a1`
  gates the stay-vs-go branch (`sete`, `0x1409e7937`).
- Claim-or-stay reduces to (OBS): stay if kind gate fails or predicted point outside the
  `0.10869/0.21737`-shaped box after the `0.6944444 [0x14259bdd0]` scaling; else go, target =
  predicted point. Exact box orientation (which axis is "high") not reduced — open.
- `ActionKeeperPickupBall` CanEnter = always-true (`0x140ca1a50`); the loose-ball claim
  decision is in the (packed) think unit — open, nothing traced.
- High punch: no separate high-ball branch in `0x1409d69f0`; high balls run the same arrival
  gate (the trajectory table carries z). OBS (bounded function scan).

---

## 5. Distribution (partial)

- Throw choice gate (`ActionKeeperThrow::CanEnter 0x1409d0950`, OBS static): reads the
  pending-request kind object `[rdx+0x28]`; `kind = FUN_140a65db0(kindByte&0x7f)`:
  `kind==3` → long/over-arm path (`FUN_140518fe0` + target via `FUN_140a65a00`, writes
  `[action+0x24]`); `kind==2` → short/under-arm path (target via `FUN_140a65b30`, writes
  `[action+0x28]`); else keep. So throw vs short-throw-out is a request-kind switch, OBS.
- Anime side (`goal_keeper::Throw/PuntKick Update 0x140748b20`, OBS via GhidraMCP): post-catch
  phase machine driving `gkoverthrow/gkunderthrow/gksidethrow/gkpuntkick` rows (§8.2 of the
  motion-mapping report); aim X clamp `±1.5 [0x14259bddc]`, Y `−0.65 [0x14259d5d0]`.
- Targets: `[action+0x24/0x28]` triples (OBS offsets); the up-field target selection
  (which teammate/zone) is upstream in the packed think region — open.
- Throw vs punt-kick vs roll-out choice: open. `ActionKeeperAfterCatchMove::exec 0x1409e8880`
  is a stub in this image (GhidraMCP "Function not found"; bytes not in mapped range —
  same packed class as deflect). The launch itself is `pes-re-kick-launch.md` §7.5 (not
  re-traced).

---

## 6. One-on-ones and penalties (partial)

- 1v1 = the sweeper rush (§1.2) — no separate 1v1 branch found (bounded search over the
  base-position function and its two subs for a dedicated 1v1 gate: none; the rush IS the 1v1
  response).
- Penalty gate `FUN_1409d5860` (OBS decompile): true iff `(match+0x116c==3 && match+0x1554==6)`
  (set-piece-penalty, INF medium on enum names) OR current ball-kind `==6`.
- `ActionKeeperPKSaving::exec 0x1409e5570` (OBS static): phase machine on `[action+8]`:
  phase 0 = wait (checks `+0x116c==1` kickoff-phase or `==3` + ball-flag `+0x2d` with clock
  window `(clock-0x11a4) < limit+?`); phase→4 on the penalty gate; dive target staged at
  `[action+0x28]` (init `-1.0 [0x142588218]`), side byte `[player+0xfe5] = tickCount/32-ish`
  (`mul 0xaaaaaaab; shr 5` after `×5<<2`, OBS ops); kicker ability `[rdx+0x173c]<=0x15` and
  role `[rax+1]==0x36` gates.
- `ActionKeeperPreSaveOperation::exec 0x1409e6870` (OBS static): pre-dive aim. Verified:
  `[action+0x2c] = -1.0` init; `[action+8] 0→1`; kicker slot `[rdx+0x173c]<=0x15`;
  `[rax+1]==0x36`; aim triple `[player+0x894]` vs keeper `[r13+0x55c]` via `FUN_140470eb0`
  (yaw blend); commit sets `[player+0xfdb]=1` when
  `|aim| > threshold (1.0 [0x14256ae1c])` after `FUN_14043a0a0` normalisation — else
  `[player+0xfdb]=0` (no-dive). The dive *side* in a live penalty (which way) comes from the
  sign of the aim blend (INF medium); the kicker-read (`[rcx]` kind `0x19`/`0x4c` shot-kind
  checks at `0x1409e69a4`/`0x1409e58b9`) selects near/far variants (INF low).
- Penalty dive choice (which side for a placed kick): open at the level of "reads kicker
  animation vs reads ball" — the branch exists (`0x1409e58f0`–`0x1409e5a2d`) but the input
  (kicker pose `0x140a65b30`/`0x140a65a00` request objects) is not decoded.

---

## 7. Live validation (attempted, incomplete)

- `STATE=OPEN_PLAY` confirmed repeatedly (`ctx=0x35a01530`, ball `0x1a820a68`
  moving, e.g. `pos=(0.00,1.00,-0.36) vel=(-0.10,1.34,-0.08)`), read-only probe.
- Registry decoded: 247-entry cobra registry; entry+0 = Ref vtable, +0x10 = Copy object.
  Mapped via RTTI: idx15 `MatchInfo` Copy (`0x142596918`), idx17 `MatchEnv` Copy
  (`0x142596aa0`), BallInfo idx1, etc. The Copy objects wrap small heap structs; the
  dump-era `match+0x116c/+0x270` offsets do NOT hold live (same finding as team-plans §6).
- AnimePlayer vtable scan (`0x1425e34d8/0x1425e3508/0x1425e35b0` over writable heap) was
  started as a background job; no keeper `P+0x245c`/`P+0xac8` read completed this session.
- No shot was observed live (rams, no control input by rule), so no around-a-shot keeper
  sample exists. Open.

---

## 8. Open list (with what was tried)

1. **pesSmart/gk tuning numbers** (85/70/60, 1.1, 110/80, 1.7/1.5/1.3): decoded `gk.o`,
   `ballplayerGk.o`, `penaltykick.o`, `goalKick.o`; float-searched both constant bins;
   scanned keeper code for the immediates. Only `1.3` located (`pesSmart.o +0x54`, key
   unmapped). The §3.5 attribution looks partly wrong (strings live in `gk.o`, values don't
   match) — needs the loader or a runtime read of the loaded struct.
2. **Deflect side/angle** (`0x1409d75d0` packed stub): tried static disasm + GhidraMCP; only
   the anime flag-mover found. Needs a live breakpoint-free trace or the packed-region base.
3. **Distribution choice** (throw vs punt vs roll-out; `0x1409e8880` packed): CanEnter gates
   done; the picker is upstream. Same need as (2).
4. **Line-depth closed form** (§1.4) and **post-bisector question**: needs a worked numeric
   trace (log ball/keeper/target triples live, fit the lerp).
5. **Style-bit assignment** (bit19 vs bit20 = Offensive vs Defensive): needs a keeper with
   the opposite style observed live, or the style-bit table in the exe.
6. **Ability-index binding** (`0x24/0x25` = Reflexes/Reach): needs the `FUN_1406e6660`
   index table readout (compare against known-rated players live).
7. **Live keeper-around-a-shot sample**: needs a shot to happen during OPEN_PLAY while
   sampling `P+0x245c`, `P+0xac8`, ball traj table — attempted, scan incomplete.

Scratch kept: `<scratch>/KeeperRE/` = `constant_player.bin(.dec)`, `constant_match.bin(.dec)`,
`constant_team.bin(.dec)`, `kdis.py`, `krips.py`, `kscan.py`, `klive*.py`, `14074ad00.c`.
All paths under the shared scratch dir; `/tmp` additions under 1 MB (image itself pre-existing).

---

## 9. Live validation results (appended; read-only `/proc/<pid>/mem` while OPEN_PLAY)

### 9.1 Keeper identity and static-field confirmation (OBS live)

- 31 `match::Player` objects found by vtable scan (`0x1426329a8/0x142632a50`); each
  `player+0x7d8` → AnimePlayer (confirms the fox-registry `+0x7d8` layout live).
- Keeper = player `0x1fa22f00` → AnimePlayer `0x36666630`: stays `x ∈ [-52,-37]` by the
  `-52.56` goal over ~150 samples across three watches; all outfielders roam.
- `P+0x41c = 0.0185185 = 1/54` (OBS live) — the tick duration, confirming the
  `FUN_1406ec2a0` `param_3 *= P[0x41c]` scaling in §2.2. `P+0x3758` (rate) ≈ `0.98–1.15`
  per player; `P+0x420` (table count) = 1–3 live.
- Correction to §7: the `P+0x245c` phase byte is per-action-class. Values logged under
  outfield aids (`aid` 2/3 = MOVE/STEP_MOVE) are NOT keeper state-machine phases; only
  readings under keeper aids would be. No keeper-aid (`goal_keeper::*`) phase sample was
  captured — no shot occurred.

### 9.2 Positioning vs the angle-bisector hypothesis (OBS live, ~60 samples)

Method: every 3–4 s, keeper `(kx,kz)` + ball `(bx,bz)`; `depth_t = (kx∓52.56)/(bx∓52.56)`
(fraction of goal→ball distance), `lat = kz − depth_t·bz` (lateral off the goal–ball line).
Ball stayed on the `z≈0` ground-pass line the whole session, so `lat ≈ kz` directly.
- Keeper holds a **shallow line**: `depth_t` mostly `0.01–0.30` (0.3–12 m off the line),
  retreating to `0.007–0.05` (on the line) when the ball is central/near, coming out to
  `0.26–0.30` (9–12 m) when the ball is far upfield. Outside-in: `off = |kx−goal|` ranges
  0.3–12 m and grows with ball distance — a **line-depth-vs-ball-distance curve**, OBS shape.
- **No bisector tracking**: keeper lateral `kz` drifts `±6 m` independently (e.g. ball
  `(-21.4, 0)` → keeper `(-43.7, +5.4)`; ball `(-13.9, 0)` → keeper `(-52.0, −1.4)`).
  Lateral does not scale with ball `z` (ball `z` constant) and does not sit at the
  line midpoint — it wanders, consistent with the style-gated width blend of §1.3 rather
  than a geometric bisector. Bounded: ball never left `z≈0`, so the lateral response to a
  wide ball is untested (open).
- Set-piece anchors (OBS): goal-kick `aid 62→63`, keeper `(-51.6, 0.1)`, `depth_t=0.020`,
  `lat=0.08`, flag `P+0x2460 = 0x8000000` at `aid 63`; out-of-play `aid 79` freezes the keeper
  (`depth_t` constant `0.049–0.052` over 20 s, `ph=3`).
- No shot/save occurred in ~4 min of open play, so §§2–4, 6 have **no live dynamic sample**.
  Open items 4 (closed-form depth) and 7 (around-a-shot sample) remain open; item 4 now has
  the curve shape above to fit against.

---

## 10. Session 2 (GoalkeeperAIRE): the keeper's decision layer, positioning, rush, high balls, distribution

Sources: Ghidra headless on the unpacked live image (`~/ghidra-projects/scratch/Unpacker`, PESLive),
`annot.py` constant folding, raw capstone on the on-disk exe for every quoted constant chain
(scratch `rdis.py`, rip-relative operands resolved). Live: read-only `/proc/<pid>/mem` reads of the running
AI-vs-AI match (OPEN_PLAY, `MI+0x116c == 1`). Tags as above: **OBS** = seen in bytes/decompile at the VA or in
a live read; **INF(conf)** = inferred. `G = *(0x1436f3dc8)`, `MI = *(G+0x270)`, `F = *(G+0x2aa8)` (FieldInfo,
floats `F[i]` at `+4i`: `F0` 52.56 half-length, `F2` 3.66 half-goal, `F3` 2.44 bar, `F4` 47.06 goal-area line,
`F5` 9.16 goal-area half-width, `F6` 36.14 box line, `F7` box half-width, `F8` 41.56 spot — values from
the other reports, re-read live in §10.2.4), `T(s) = *(G+(s+0xa1)*0x28)` team, `pl(i) = *(G+(i+0xbf)*0x28)`,
`ai(i) = *(G+(i+0xf3)*0x28)` (OBS getters `0x140a48a30/a10/9f0`), in-match block `blk(i) = ai(i)+0x70`
(`FUN_14124dad0` = `lea rax,[rcx+0x70]`), `MA = ctx+0x18` (match analysis), `gs` = fps (54 live; constant
on disk 60). `dir = (s8)T+0x254` = the team's attack sign. **`R = (−(F0+1)·dir, 0)`** = a point **1 m behind
the centre of the keeper's own goal** — the origin of every keeper geometry below (OBS: `0x1409dea5d`,
`0x1409e0442`, `0x1409df81a`, `0x1409da4ad`).

### 10.0 Corrections to §§1–6 and to `pes-re-keeper-port.md` (killed hypotheses)

| earlier claim | verdict | evidence |
|---|---|---|
| §1.1: `0x1409dc040`–`0x1409dc9c7` is one exec | **killed**: `.pdata` splits it (`0x1409dc040-0x1409dc18d` + chunks); the exec is a dispatcher that calls one of four position functions (§10.2.1) | OBS `.pdata` (scratch `pdata.py`), decompile |
| §1.2: `0x1409de8c0` = "sweeper/rush subroutine" | **killed**: it is the **out-of-possession base position**; the rush is a separate AI action `KP_PRESS (0x3e)` chosen by the keeper think (§10.3) | OBS caller `0x1409dc8d3` gated on `T+0x255 == 0` |
| §1.3: bit19 = Offensive GK (EXT), bit20 = Defensive GK (INF low) | **closed**: bit 20 caps the defending depth at **4 m**, bit 19 raises it to **20 m**, neither = 15 m; in possession 13 / 17 / 15 m (§10.2) — monotone in exactly the Defensive < none < Offensive order | OBS raw `0x1409e074a`–`0x1409e0768`, `0x1409e0018`–`0x1409e0040`; live: both keepers style word `0x80000` (bit 19) |
| §1.4 / §9.2: "no bisector, lateral wanders ±6 m" | bisector **killed again, now positively**: he stands **on the line R → ball** (one atan2 from R), so he is never on the post-angle bisector; the "wander" is a ≤0.5 m deliberate jitter plus the 1v1/threat shifts (§10.2.2) | OBS `0x1409e0390`, live match §10.2.4 |
| §1.2: "18 m rush radius cap" | **reassigned**: 18 m (`0x14259be18`) is the R→ball distance under which the defending position switches to the **on-the-line** mode (§10.2.2 step 2) | OBS `0x1409de9f7` |
| keeper-port §2.2/§4.4: "a per-keeper 11-entry distribution table at team+0x22e4c, install data" | **killed**: the base is **MA** (`ctx+0x18`), not the team; `MA+0x22e4c + 200·id` is the engine's **per-player neighbour list**, refreshed every tick (§10.6.1). Nothing is install data | OBS `0x1409e8a8e` (`mov r13,[r13+0x18]` = ctx→MA), live §10.6.1 |
| keeper-port §2.2: `FUN_140a92d40(Δz, Δx)`; §4 #7 "the 4 m step is lateral, not toward the teammate" | **killed**: the call passes **Δx first** (`subss xmm0,xmm2` = target.x − keeper.x at `0x1409e8c9b`, xmm1 = Δz); `FUN_140a92d40(a,b) = atan(a/b)` folded, so θ is measured from +z and `p += 4·(sin θ, sin(θ+90))` = **4 m straight toward the teammate** | OBS raw `0x1409e8c52`–`0x1409e8d1b`, `0x140a92d40` |
| keeper-port §2.1: `|z| > B → z −= sign(z)·2·B` | **killed**: it is `z −= sign(z)·2.0` (**2 m**, `addss xmm0,xmm0` on ±1.0) | OBS raw `0x1409e8a6d`–`0x1409e8a85` |
| keeper-port §2.1: `if (−A ≤ dir·x+4) x += 4·dir else x = −A·dir` | **inverted**: raw is `comiss xmm12(−A), xmm0(dir·x+4); jbe → x = −A·dir`; i.e. **x = min(x+4, −A) in own frame** | OBS raw `0x1409e8a1b`–`0x1409e8a3b` |
| keeper-port §2: final clamp | **missing in the port**: every branch ends with `|z| ≤ F7 − 1` (`0x1409e8d3e`–`0x1409e8d70`) | OBS raw |
| keeper-port §3.2: AfterCatchMove output = the pass target | **killed**: `ActionKeeperAfterCatchMove` derives from `ActionMove`; slot 4 returns the point the keeper **walks to while holding the ball**. The pass target comes from the carrier think units (§10.6.3) | OBS RTTI bases, vtable slot 4 |
| §5: "throw vs punt vs roll is a request-kind switch only" | **partly superseded**: the request kind comes from the `bp::ThinkUnitGK*` answer (§10.6.3) | OBS |

### 10.1 The decision layer: `PlayerGKAuto` think `FUN_142104c90` (OBS)

- **Trigger/cadence.** Called from the per-player AI tick `FUN_14092c9f0` (call site `0x14092caf4`,
  OBS `argrdx.py`); the AI tick runs every logic frame (54 Hz, defender report §1). Output = the AI action id
  written to `think+8` (`*piVar1`); 0 = keep base position (`KP_BASE_POSITION 0x3f`). Ids (table
  `0x1434a4d50`, OBS): `0x3e KP_PRESS`, `0x43 KP_CATCH`, `0x47 KP_BLOCK`, `0x48 KP_BLOCK_LATE`,
  `0x4c KP_MAKESHIFT`, `0x4d KP_SAVING_MOVE`, `0x50 KP_SEEN_OFF`, `0x55 KP_PRE_SAVE_OPERATION`, outfield
  `0x15 CLEAR`, `0x34 PRESS`, `5 PASS_GET`.
- **Order of tests** (first that fires wins; OBS):
  1. Keeper and ball on different halves (`sign(keeper.x) != sign(ball.x)`) → return, nothing.
  2. Penalty/shoot-out (`MI+0x1168 == 9`, event kind 6, or `116c==3 && 1554==6`) → `FUN_142114940` (PK).
  3. **Shot heading for goal → `KP_SAVING_MOVE`** (§10.5.1).
  4. **Pre-save set → `KP_PRE_SAVE_OPERATION`** (§10.5.2).
  5. In play (`116c == 1`): `FUN_142112a50` (save choosers: catch/block/block-late/clear/makeshift/seen-off) then
     **`FUN_142115100` (the rush, §10.3)**. If the rush fires but its point lies outside the box or the
     `MA+0x251fa[side]` flag is clear, `KP_PRESS` is **rewritten to outfield `PRESS 0x34`** (or `PASS_GET 5` if the
     newest ball event is a pass to him) — the keeper leaves his area as an outfielder (OBS `0x14210565c`–`0x1421056b6`:
     `|claim.x| < F6` or `|claim.z| > F7` with `claim = ai+0x1414/+0x141c`).
- **Inputs used everywhere below:** `ai(i)+0x1438` = frames for player i to reach the ball (int), `ai+0x1430`
  frames-to-ball variant, `MA+0x24840` = per-team list `{u32 id, f32 frames}` × 11, **sorted by ball-arrival
  frames** (used as frames here: compared to `gs`-multiples and loop bounds, OBS `0x14210508c`, `0x140508b3a`),
  rank byte `MA+0x24840+0xb2+id`; `MA+0x2482c/+0x24834` = MA ball x/z; `blk+0x16` = GK Awareness (INF high,
  matchlogic §1.3); `blk+0x31` = height cm (OBS); `blk+0x48` = playing-style word.
- **No RNG** in the think or in any rush/claim decider read here (bounded: no call to `0x140a92470/4e0/530/590`
  in `0x142104c90`, `0x142115100/620`, `0x1421162c0`, `0x14210aad0`, `0x1405083a0`; OBS decompiles).

### 10.2 Open-play positioning (CLOSED)

#### 10.2.1 Dispatcher `ActionKeeperBasePosition::exec 0x1409dc040` (OBS)
In play, not a set piece, ball not in the special restart states (`FUN_140509890/a40`, kick-off/goal-kick
windows → `FUN_1409de4d0` fixed spots):
```
if T+0xb6de != 0:            pos = FUN_1409dd470(...)      # team flag, INF(low): "danger" mode; not seen set live
elif T+0x255 == 0:           pos = FUN_1409de8c0(...)      # OUT of possession   (action+0x58 = 1)
else:                        pos = FUN_1409df7d0(...)      # IN possession       (action+0x58 = 2)
pos.x clamped to |x| <= F0 − 0.5; then a 1.5 m dead-zone (2.25 = d², 0x1425a9be0): inside it he stands still,
else the steering point is 5 m (0x14259222c) along the blended heading (facing-the-ball turn logic, 90/135/180°).
```
`T+0x255` = "team in possession" — **OBS live**: the team with the ball read `T+0x255 = 1`, the other 0, and the
keepers' action objects showed `+0x58 = 1` / `2` accordingly (§10.2.4); matches the tactics report's INF(high).

#### 10.2.2 Out of possession: `FUN_1409de8c0` → core `FUN_1409e0390` (OBS, constants raw-verified)
Inputs: `B = action+0x74` (the ball reference the action is fed; live it is the predicted ball, not the raw ball),
keeper `K`, style word `S = blk+0x48`, `L_t = T+0x875c` (team line x, own-frame, INF med: the team's defensive-line
reference written by the shape builder), `L_m = dir·MA[+0x40 + s·0x14]` (MA line for side s = the offside-line
x of the defensive-shape report), `a2 = MA+0x228d4` (if `−dir == 1`) else `MA+0x228d8` (an MA bearing per goal,
INF(low): the opponents' attack bearing).
```
# 1) the core spot (FUN_1409e0390)
θ    = bearing(R → B)                       # degrees, from +z (FUN_140439e50)
w    = clamp(|wrap180(θ − a2)| / 50, 0, 1)                         # 50 @0x14259be34
dmin = (F2 + 0.5)·w + 2·(1 − w)                                    # 2.0 m … 4.16 m from R
wide = (FUN_140a62d90(keeper) == 0x81a1) and |B − R| >= 0.6·F0     # 0x81a1 = a ball-state word, INF(low); 0.6 @0x14257a190
L_m  = clamp(L_m, L_t − 10, L_t + 10)                              # 10 @0x142588210
back = wide ? 10 + 15·ramp(dir·MA.ball.x; F4→0, F6→1) : 25         # 25 @0x14259be20, 15 @0x14259be0c
dline= max(−F4, (L_t + L_m)/2 − back) + F0                         # line-derived depth, ≥ F0−F4 = 5.5 m
cap  = wide ? 42 : (S bit20 ? 4 : S bit19 ? 20 : 15)               # 4 @0x1425656c0, 20 @0x14259be1c, 42 @0x1425cd830
d    = max(dmin, min(cap, dline))
if T+0x255 == 0:                                                   # always true on this path
    d = min(d, 4 + 9·ramp(|B.x − K.x|; 40→0, 70→1))               # 4..13 m; 40/70/9 @0x14259be2c/be4c/d60c
    if wide: d = min(d, 7 + cap·ramp(|B.x − K.x|; 30→0, 45→1))     # 7 @0x14259bdf0
r    = min(d, |B − R|, wide ? ∞ : 15)
if |θ − 0| > 70 (vs the goal axis): θ clamped to 70 off it         # 70 @0x14259be4c, FUN_140a92950
P    = R + r·(sin θ, cos θ);  dir·P.x >= −(F0 − 0.8)               # at least 0.8 m off the line (0x142572348)
jitter (every 2·gs frames, 0x1409e0590…): j = Range0(rng@cmd+0x19c0, 6)·0.1·(1 − ramp(GKAw; 40→0, 99→1))
        sign flipped when the ball's predicted bearing differs ≥ 90°; 0 when FUN_140a63090()+0x30 == 6
P   += j along (θ + 90)                                            # ≤ 0.5 m lateral, perpendicular to R→B
```
```
# 2) FUN_1409de8c0 around the core
hold: a committed target (action+0x30, state +8 = 2) is kept for gs/24..gs/6 frames (2..9 frames; 9 when
      |K.x| >= F6) — the re-plan cadence is 6–27 Hz (OBS 0x1409de9b0…).
if FUN_1405083a0(ball contested in the air, §10.4) and |B − R| < 18:                       # 18 @0x14259be18
    if (S bit19) and ball event ∈ {1,2,3,0x10} and the ball moves toward the centre (v.z·B.z < 0)
       and the opponents' best arrival > 0.4·gs and the event is younger than 0.6·gs:
        P = B                                       # Offensive GK goes to the ball (0.4/0.6 @0x14259b89c/0x14257a190)
    else:                                           # ON-THE-LINE mode
        P = R + r'·(sin θ, cos θ),  r' = min(|B.z|, F2)/2, with dir·Δx >= 1.5   # 1.5 @0x14259bddc
else:
    P = core(...)                                   # step 1
    pivot: c.x = lerp(R.x, goal-line x, ramp(|B − R|; 22→0, 30→1)); P re-radiused from c by r − |c.x − R.x|,
           θ nudged by ≤ 7° (0x14259bdf0) toward the ball side when the carrier is ≥ 35° off-axis (35/55/80 ramps)
    near-post/threat shifts: owner within F7+3 laterally → ±0.8 m (kick anime 8/9) or ±1.3 m (0x14259bdd8)
    no-backpedal: within 2 m of the old spot and ball in the box → keep current depth (−0.1 m)
commit when the new P is > 0.2 m from the keeper (0x142598578).
```
**In words:** the keeper stands on the straight line from a point 1 m behind his goal centre to the (predicted)
ball, at a distance that is the smallest of (a) the style cap 4/15/20 m, (b) 25 m behind the midpoint of the
team line and the offside line (never deeper than the 6-yard line), (c) **4 m while the ball is within 40 m of him
lengthwise, growing linearly to 13 m when it is 70 m away**, (d) the ball distance, (e) 15 m; never less than
2 m (ball straight in front) to 4.16 m (ball ≥ 50° off the bearing `a2`). With the ball close (< 18 m from R) and
in the air he goes back to his line, shading half the ball's lateral offset (≤ 1.83 m).

#### 10.2.3 In possession: `FUN_1409df7d0` (OBS)
```
θ    = bearing(R → B)  (pulled toward the team angle T+0x250 when FUN_140a62d90 == 0x81a1)
L    = |MA[+0x40 + s·0x14]|                     # own offside-line x
on entry (state 0):
  if F6 < L < F0: P = R + (F0 − L)/2 along θ;  done          # own line inside the box: halfway
  bx = dir·B.x
  d  = bx < 3 − F6   ? 6
     : bx < −F0/2    ? 6 + 3·ramp(bx; −F6, −F0/2)            # 6..9
     : bx < 0        ? 9 + 4·ramp(bx; −F0/2, 0)              # 9..13
     :                 13 + 7·ramp(bx; 0, F0/2)              # 13..20   (6/3/9/4/13/7 @0x14259bdec/c264/d60c/56c0/c5948/bdf0)
  d  = max(d, F0 + T+0x875c − 25)                            # never deeper than 25 m behind the team line
  d  = min(d, S bit20 ? 13 : S bit19 ? 17 : 15) (+5 if T+0xb8a2, 42 if 0x81a1)   # 13 @0x1425c5948, 17 @0x14259d614
P    = R + d·(sin θ, cos θ),  dir·Δx >= 3                     # 3 @0x14258c264
if a team-mate owns the ball, the ball is beyond F6 − 20 in our half and no opponent can reach it within 3·gs:
  P moves 2 m toward the intersection of the threat lines (FUN_140a90fd0) — cover for a back-pass/clearance
re-plan: d is latched; the spot is re-evaluated every gs/6 frames.
```
**In words:** with the ball, the keeper steps up as a sweeper: 6 m off R deep in his own third, 9 m at the
half-way of his half, 13 m at the centre line, up to 20 m when the ball is in the opponents' half — capped 13/15/17
by style.

#### 10.2.4 Live check (OBS, this session)
One AI-vs-AI match, both keepers style word `0x80000` (**Offensive GK**, bit 19), GK Awareness 70 and 77, height
189 cm. Read: `pl+0x5d0` (AI action), the keeper's `ActionKeeperBasePosition` object (found by vtable scan
`0x142632090`; 22 instances, 2 active) `+0x08` state, `+0x30` committed target, `+0x58` mode, `+0x60` jitter,
`+0x74` ball reference.
- Snapshot (clock 10240): keeper A (team out of possession, `T+0x255 = 0`) `+0x58 = 1`, target
  `(−50.07, −1.75)`, ball ref `(−15.43, −27.23)`, jitter −0.147. Reconstruction: `|B.x − K.x| = 23.1 < 40` →
  `d = 4`; `|B − R| = 46.9 ≥ 30` → pivot on the goal line, radius `4 − 1 = 3` along θ → **(−50.12, −1.74)**,
  observed (−50.07, −1.75): **0.05 m**.
- Keeper B (team in possession, `T+0x255 = 1`) `+0x58 = 2`, target `(37.19, −4.59)`, ball ref `(−19.38, −20.49)`:
  `bx = 19.38 ≥ 0` → `13 + 7·0.737 = 18.2` → Offensive cap **17** → `R + 17·(−0.963, −0.270)` =
  **(37.19, −4.60)**: **0.01 m**.
- 20 Hz run: §10.2.5.

#### 10.2.5 Live 20 Hz run (OBS; scratch `samp.py`/`ana.py`, `s1.jsonl`)
240 s, 3,416 frames polled at ~14 Hz effective (`MI` is null for part of each tick), live FieldInfo
`F = (52.56, 34.06, 3.66, 2.44, 47.06, 9.16, 36.14, 20.12, 41.56)`. Every **distinct committed target**
(`action+0x30`) was compared with the reconstruction fed with that frame's live inputs (`+0x74`, keeper position,
`T+0x875c`, `MA+0x40/+0x54`, `MA+0x228d4/8`):

| mode | targets | median error | within 0.2 m | outliers (attributed) |
|---|---|---|---|---|
| 1 out of possession | 566 | **0.005 m** | **534 (94 %)** | 32: ball < 4 m from R (the on-the-line / go-to-ball branch, not modelled in `ana.py`) and the ball ~100 m away (observed radius 14.4 vs 13: the ≤0.1 m·ramp(55..80) radius add and 7° nudge of step 2) |
| 2 in possession | 292 | **0.000 m** | **240 (82 %)** | 52: a cluster with the ball 16–19 m into his own half and no opponent near (observed 11.7 m vs 10.2 m — the "cover 2 m toward the threat-line intersection" branch fires exactly there, `bx < −(F6 − 20) = −16.1`), and ball deep in own half (own-line-in-box branch) |

The style cap is visible directly: in possession every target with the ball in the opponents' half sits at
**exactly 17.0 m** from R (Offensive GK cap), e.g. `bx = 21.5, 19.6, 34.0, 46.5 → r = 17.00`. Out of possession,
with the ball within 40 m lengthwise, every target sits at **4.00 m** from R (3 m off the line, the limiter);
with the ball 70–75 m away, 9.5–10.1 m (limiter `4 + 9·ramp`, observed 9.8 vs 9.97 predicted). This closes
§8 item 4 (depth closed form) and §9.2's curve.

### 10.3 The rush (`KP_PRESS 0x3e`) — trigger, limits, style (OBS structure; semantics INF where marked)

Decider `FUN_142115100` (from the think, in play only, and only while the current action ∈ {0, `KP_PRESS`}):
```
if presentation mode 0x14, or 0x17 with the ball in the box and an owner → KP_PRESS (replay/demo path)
if the keeper is pad-controlled (G+0x180 list) → no
owner = FUN_140a627e0(ctx, 0)
rushing = (current action ∈ {1, 5, 0x3e}  [mask 0x4010000000000020 on +0x5d0]) and think+0x13a2 and anime not busy
         and the newest ball event is 2 frames old or a repeat              # hysteresis byte
fire = owner < 22 ? FUN_142115620(owner, rushing)   # 1v1 against a carrier
                  : FUN_1421162c0(rushing)          # loose ball: through-ball, cross, back-pass, rebound
fire → think+8 = KP_PRESS, ai+0x13a2 = 1
```

**(a) 1v1 against a carrier, `FUN_142115620` (OBS constants):** only when the carrier is an opponent and the
defending team's flag `T+0xb6de` is set (writers `0x1404161e0`, `0x1404f6c30`, `0x14215cf10`; live it was set on the
defending team in 94 of 3,416 frames, only while the opponents held the ball in their own half — INF(low): a
team "press high / exposed" phase; this gate is why no live 1v1 rush was seen). Then:
- carrier receiving (anime 10): rush only if the reception point is within **6 m** of the keeper (36 = d²,
  `0x1425a9464`) and the angle gates (15°, `30 + 10·rushing`) pass;
- carrier kicking (anime 8/9, contact pending): **no rush** while the contact is ≤ `0.25·gs·aw` frames away and the
  kick clip is past `(0.17 − 0.5)·aw + 0.5·gs` frames (aw = ramp(GK Awareness; 40→0, 99→1)) and the carrier faces > 40°
  away — i.e. aware keepers stand still for the shot instead of charging into it;
- otherwise the carrier must face within `30° (+10 when rushing)` of goal and the ball must be inside the box depth
  (|x| ≥ F6) and within F4 laterally;
- **zone:** predicted ball point must satisfy `|x| ≥ F6 + 3` (within 13.4 m of the line) and
  `|z| ≤ (F5 + F2)/2 = 6.41 m` — widened to **`F7 − 3 = 17.1 m`** for an **Offensive GK** unless the carrier
  is at a wide angle (bearing to goal within **48°** of the z-axis; **43°** for Offensive GK — `0x142588214`,
  `0x1425cfa38`);
- **race:** keeper frames `kf = ai+0x1430`, carrier frames `of` (his `ai+0x1438`, or the remaining kick/contact
  frames): go if `of > kf`, or `of > gs` (×1.1 for Offensive) and `kf < 0.3·gs`; a team plan bit 2 adds
  "go if `of > lerp(2·gs, gs, ramp(kf; gs, 2gs))`" (×0.25 Offensive);
- **reach scan:** walk the ball trajectory up to `of` frames; the first point inside the zone whose distance to the
  keeper is < `0.22·|carrier − goal|² + 12.25·rushing` (d²) and needs a turn < `(1 − GKAw/99)·10 + 10 (+10 rushing)` °
  → **rush**. GK Awareness lowers the allowed turn (aware keepers commit only when already facing the play).

**(b) Loose ball / through ball / back-pass, `FUN_1421162c0` (OBS constants):**
```
kf = ai(keeper)+0x1438  (frames to the ball);  no rush if kf ≥ 250 or kf > 3.3·gs (0x1425c4c10) or the ball is a shot
of = ai(nearest opponent to the ball, MA+0x24840 other side [0])+0x1438
hyst = rushing ? 0.65·kf : 0                                   # 0x14259d5d0
high = FUN_1405083a0()  → the high-ball branch, §10.4
k = Defensive GK 1.3 | none 1.2 | Offensive GK 1.1              # 0x14259bdd8 / 0x14259d5d4 / 0x1425a3590
if gs·0.71·rushing + of <= k·kf:            # an opponent gets there first (0.71 @0x1425eb848)
    rush anyway only if: the point at kf is inside the goal area (|x| > F4−1, |z| < F5) and 1.2·gs < of + 0.4·gs·rushing;
                      or (T+0xb6de) and |z| < F5+1 and 0.8·gs < of+… and the ball crosses the line at of;
                      or (T+0xb6de) and k'·kf < of + 0.71·gs·rushing (k' = 1.1);
                      or ball state 0x81a1 and T+0xb6de and of + 0.4·gs·rushing > kf
    else no
limits on the point P(kf) (ball where the keeper meets it):
    |P.x| ≤ F0 + 3·rushing (+2 if the newest event is his team's)      # not chasing over the line
    |P.z| ≤ F5 (Defensive) | (F5+F7)/2 = 14.6 (none) | F7 = 20.1 (Offensive); outside it only if he is his
          team's first man to the ball and not 1.5·gs slower than the opponent (1.6·hyst)
    if a team-mate gets there ≥ 0.5·gs (·1.5/×2 by style, see code) before him → no; and when the team-mate is
          close in time the point must be inside |x| ≥ F6 (+3 Defensive, −5 Offensive) and |z| ≤ F5
    back-pass guard: if MA says the ball is the team's own pass to him (MA+0x24838 = team-mate, aim error ≤ 10°) → no
→ rush; ai+0x139b = 1; if kf > 2·gs and the point is in the pitch: cmd+0x8d4 = 0x19 (INF: sprint)
```
**Then the area rule (OBS, think `0x14210565c`):** a `KP_PRESS` whose claim point `ai+0x1414/+0x141c` is outside
the penalty area (`|x| < F6` or `|z| > F7`) is executed as outfield **`PRESS 0x34`** (or `PASS_GET 5` if the newest
event is a pass to him). The keeper never "rushes" as a keeper outside his box — he becomes an outfield presser.

**Rush execution** `ActionKeeperPress::exec 0x1409d2010` (OBS head): if the ball is contested in the air
(`FUN_1405083a0`), target = the keeper's claim record `ai+0x143c` and, when `FUN_14210aa30(…,3,…)` (frames to that
point for kind 3) is below `gs/3 + gs·rushing/3 + FUN_140a4d350` it switches to the box-edge intercept
`FUN_1409d16a0` (the first trajectory point beyond `F6 + 2`); otherwise the press target is the predicted ball
`FUN_140a6cf70(ball, 10·gs/48)` with the turn rules of `0x1409d1860` (90/120/45/75°, 10 m look-ahead). Continue
(slot 15 `0x1409d42d0`): while the hysteresis bytes `+0x3c/+0x3e` are set or the ball stays contested.

### 10.4 Crosses and high balls (OBS)

**"Is this a keeper's ball in the air?"** `FUN_1405083a0`:
```
standing reach  hs = (height_cm/100/1.8)·2.585 + 0.1          # 2.585 = reach table [11] @0x142648668+0x2c, 1.8 @0x14259bde0
high0 = any trajectory point, sampled every gs/6 frames up to 1.1·gs, above hs     # FUN_1405082d0
first = whoever (either team, MA+0x24840 [0] of each side) has the smaller ai+0x1438 frames
contact frame = that player's pending kick contact, else the ball's arrival at the MA ball point
if ball height at that frame ≤ (tbl[6]+tbl[7])/2 · first.height/1.8   (≈ 1.57 m for 1.80 m)    # head-height-ish
    high = high0, but forced 0 if keeper's drop frame (FUN_140507770, kind 0x43) + 0.4·gs < first's frames
else high = 1                                                  # the first man cannot head it: aerial ball
```
Reach table `0x142648668` (OBS, × height/1.8 m): `0.15, 0.335, 0.56, 0.785, 1.01, 1.235, 1.46, 1.685, 1.91, 2.135,
2.36, 2.585` (step 0.225 m), indexed by `FUN_140a6d400(k, h)`.

**Come or stay** (the `high` branch of `FUN_1421162c0`):
```
requires MA+0x251fa[side]   (team flag, INF(low); not the ball-in-box test — live it does not correlate with it)
drop = FUN_140507770(…, 0x43): frames until the ball comes down through the keeper's hand reach hs−0.1
if not rushing (or kf ≥ 3·gs, or a new opponent target): need drop < 1.3·kf − hyst, else stay
                                                     (and the ball must cross the goal line within 250 frames if
                                                      the event is his own team's)
claim point Q = ai+0x143c (x) / +0x1444 (z) must lie INSIDE the box by 1 m: |Q.x| > F6 + 1 and |Q.z| ≤ F7 − 1
goal-area flag g = |Q.x| > F4 (−1 for Defensive GK) and |Q.z| < F5
if he is not his team's first man and an opponent beats him (of < kf, ×1.3 inside the goal area) and
   drop < kf + 0.5·gs (0.6·gs for event kind 4): stay
if of + hyst ≤ kf and drop ≤ kf: stay
→ COME: KP_PRESS with ai+0x139b = 1; the claim itself is the catch/punch chooser of §§2–3 when the ball arrives.
```
**In words:** the keeper comes for a cross only if (1) the first player to the ball cannot play it below ~1.6 m
(scaled by that player's height), (2) the drop point is inside his penalty area with a 1 m margin on both lines,
(3) he reaches it no later than 1.3× (Defensive 1.3, none 1.2, Offensive 1.1 on the ground variant) the frames
the race allows, and (4) no opponent clearly beats him. Claim vs punch is decided later by the save chooser
(`FUN_142112a50` → `FUN_142113390/1421139a0`, not re-read here) on the catch window of §2.2.

### 10.5 Shots and one-on-ones: stand, set, or go (OBS)

**10.5.1 Shot heading for goal → `KP_SAVING_MOVE 0x4d`** (think `0x142105218`…): when keeper not mid-save
(action ∉ {0x46, 0x47, 0x4b}), `MA+0x251fa[side]`, no owner, his team has fewer than 2 pending orders, ball inside
the pitch length, its 249-frame prediction beyond the goal line, keeper inside his box (`|x| > F6`, `|z| < F7`), and
no opponent intercepts first (`FUN_140508600`): walk the prediction up to the opponents' best frames (+gs/10 when
already moving); at the first frame past the line, if `|z| ≤ F2 + 3 − 1.5·aw` and `y ≤ F3 + 0.5 + (3 − 1.5·aw)/2`
(aw = ramp(GK Awareness; 50→0, 99→1); i.e. on target with a 1.5–3 m margin) → the claim solver `FUN_14210aad0`
writes the intercept record `ai+0x12c8…0x1300` (point `+0x12d8`, frames `+0x12e4·gs/48`, kind `+0x12fd`) and the
think returns `KP_SAVING_MOVE` while frames remain. Keepers with **GK Awareness < 50** that are already in another
order skip this and only react through the save choosers. `ActionKeeperSavingMove::exec 0x1409e77e0` then walks
to the record point, moved 0.2 m along his facing if the ball will be above `tbl[8]·h/1.8` (1.91 m), kept 0.217 m
inside the post line, and switches to tracking the trajectory point (state 2) once he is within 0.3 m and the ball
is > `2·gs/3` frames away. Run tier (`slot 5 0x1409e80f0`, OBS): kind 2 → walk 1/2 within 0.8–1.5 m, 3/4 beyond 3 m;
kind 3 → sprint unless the ball crosses the line > `gs·ramp(heading error; 0→90°)` frames later; kind 5 → sprint
unless < 2 m.

**10.5.2 Set before the shot → `KP_PRE_SAVE_OPERATION 0x55`** (think `0x142105580`…), this is the 1v1 "stand up
and set" decision: carrier = owner (or MA's nearest), carrier's goal distance (MA, `FUN_14057d930`) ≤ 15 m or the
keeper's ≤ 7 m, carrier facing within 45° (both angle tests), carrier in a shooting anime (`0x140a6e340`, '6', or 10)
and the frames to kick contact `c` satisfy `c ≤ 27·gs/48 − gs/8 − (aw−1)·gs/8` (the lower bound divides by a 0.0
constant at `0x142bede30`, i.e. no lower bound in practice — OBS, INF med on intent); the keeper is more than
6.5 m (7.5 when in `KP_PRESS`, 9.5 when in `KP_BASE_POSITION` and > 0.7 m off its spot) but less than 30 m from the
reference of `FUN_1404c8e60` (d² vs 42.25/56.25/90.25 and 900, INF med: from the ball), his yaw and pitch rates
(`FUN_140481b00` on `+0x554/+0x558`) are below 50 and 60, he is not in a block anime ('G' = 0x47); the contact point
must be below `tbl[10]·h_carrier/1.8` (2.36 m) — then
`KP_PRE_SAVE_OPERATION` if the ball-to-goal angle error < `clamp(angle/(3 + 1.7·ramp(90..160)), 2, 10)` or the
distance² ≥ 225 (15 m); for keepers within 0.85 m of their spot a second angle test. The dive/no-dive and side are
then §6's `0x1409e6870` (`|aim| > 1.0 → dive`).

**10.5.3 One-on-one summary (INF high, from 10.3a + 10.5):** he **rushes** a dribbler only when he wins the race
to a ball point inside 13.4 m × 6.4 m (17 m wide for an Offensive GK) with < (10–20)° of turning (GK Awareness);
he **stands and sets** (`PRE_SAVE`) when the carrier is ≤ 15 m out, facing goal and about to strike; he does not
charge a player whose kick contact is imminent if aware. Going down vs spreading is the anime layer (§2.3), not
decided here. No RNG in any of it.

### 10.6 Distribution (OBS unless marked)

#### 10.6.1 `MA+0x22e4c` is the neighbour list, not a distribution table
Layout (OBS readers `0x1409e8880`, `0x142133120`, `0x140457750`, `0x140a23c10`; defensive-shape §14.2):
`MA + 0x22e4c + 200·id` = for player `id`, two sub-lists (sub = team side 0/1, stride `0x58`) of 11 ×
`{u32 playerId, f32 value}` sorted ascending, then a rank byte per player at `+0xb2+pid`. The value is the
**squared distance** to `id` (thresholds read against it: 9.0 = 3 m, 100 = 10 m, 225 = 15 m, 400 = 20 m,
900 = 30 m — `0x1409e8adf`, `0x1409e8b61`, GKPassLong's 225 `0x14259be70`, transitions §2 `0x1425a2670`). Live check: §10.9.
"11 entries per keeper" = the 11 players of a side; nothing in it is per-keeper data or install data.

#### 10.6.2 `ActionKeeperAfterCatchMove::exec 0x1409e8880` = where he walks with the ball (corrected)
Own frame (`dir` = attack sign, `A = F6 + 1` = 37.14, raw-verified `0x1409e8971`–`0x1409e8d70`):
```
mode MI+0x116c == 1 (in play), on entry or every gs/2 frames:
  P   = keeper.pos
  P.x = min(P.x + 4, −A)                         (own frame)  # 4 m up-field, never closer than 1 m inside the box line
  if |P.z| > F5: P.z −= 2·sign(P.z)                            # 2 m toward the middle when wider than the goal area
  nearOpp = MA list(keeper)[other side][0]
  if nearOpp.d2 < 9 (3 m) and T+0xb6ea == 0:                   # an opponent within 3 m and no counter concept
      P = keeper.pos;  P.z ±= 2 away from that opponent        # side-step to shield
  else:
      if T+0xb6ea == 0:  mate = first team-mate in his own list (nearest first; the keeper is not in his own list,
                                OBS live §10.9) whose own nearest opponent is ≥ 10 m away (d2 ≥ 100); none → P unchanged
      else:              mate = the CounterAfterGkCatch runner (FUN_1409e8840 → T+0xb7d8 list, kind < 2)
      P = keeper.pos + 4 m straight TOWARD mate   (θ = atan2(Δx, Δz) from +z, p += 4·(sin θ, cos θ))
      if dir·P.x > −A: P.x = −A·dir
  |P.z| ≤ F7 − 1                                                # stay 1 m inside the box side
else (not in play):    P = FUN_140a76240(keeper)  (set-piece spot);   shoot-out / cutscene: P = keeper.pos
```
`T+0xb6ea` = `T+0xb6e8 + 2` = running flag of concept 2 `CounterAfterGkCatch` (transitions §3.3–3.4 index rule).
**In words:** holding the ball he walks toward the nearest free team-mate (first one, nearest first, with 10 m of
space), staying inside his box (1 m from the front line, 1 m from the side lines); with an opponent within 3 m he
steps 2 m sideways away from him; while the counter-after-catch concept runs he walks toward its runner.

#### 10.6.3 Who he passes to and how: the GK think units (OBS)
The keeper with the ball runs the carrier brain (`bp::BallPlayer`, carrier-ai §1) with the GK unit list
(GKClear 18, GKDribble 19 fallback, GKPassShort 20, GKPassLong 21, GKCoaching 22; constant registry `0x22`):
- **GKClear `0x142132b40`**: nearest opponent to the ball (`FUN_140a4ee40`) within **15 m** (12 m when the
  team's spread metric `FUN_140a8f890` < 400) → answer 5 (clear) with angle `FUN_142108630`; online-sync special
  case; else with the hold flag and `MA+0x251fc[side]` and that opponent < 20 m and moving > 1 m off his spot and
  turned > 10° → answer 6.
- **GKPassShort `0x1421333f0`**: if the keeper's hold list `+0x2124` has > 1 entry it waits until he has held the
  ball ≥ gs frames (1 s) with the hold flag; target `FUN_142132660` → answer 2, sub 0.
- **GKPassLong `0x142133120`**: **urgent** when his nearest opponent (MA list `[other][0]`), both projected
  `gs/4` frames ahead, is within **15 m** (225 = d²) or he has held the ball > **3 s**; otherwise only after > 1 s
  of holding and (> 2.5 s or flag `+0x73`); target `FUN_142132020(urgent)` → answer 2, sub 1.
- **GKDribble `0x142132e20`** (fallback): only when he is ≥ 26.1 m from the centre line in his half
  (`dir·x ≤ 10 − F6`) and his nearest opponent is within 30 m (900); scans 32 directions for ≥ 15 m of space
  (`FUN_140a4ddc0`) and steps 3 m where no opponent arrives within gs frames and ≤ 1 team-mate → answer 3.

#### 10.6.4 Throw vs kick vs roll (OBS mechanism; anime-row naming INF)
- `ActionKeeperThrow` slot 4 `0x1409d0950` / `ActionKeeperPuntKick` slot 4 `0x1409cef10` read the request kind
  `FUN_140a65db0(kind & 0x7f)`: kind 3 carries a power `+0x20` and angle `+0x24`, kind 2 only the angle.
- **Throw exec (slot 7 `0x1409d04a0`)** picks the throw variant `cmd+0xff0` from the distance to the receiver
  `d` and whether he is running (speed ≥ 13.5 units and heading within 45° of motion):
  `no power: d ≤ 19 m → 3, 19–22 m → 4, running → 7; > 22 m: running → 5 (6 if < 26 m), else 1`;
  `with power: d < 10 m, or d < 14 m and not running → 9; running → 5; else 1` (361/484/676/100/196 = d²).
  INF(med): 9 = roll/bowl, 3/4 = under-arm, 1/5/6/7 = over-arm/side throws by run state.
- **Punt exec (slot 7 `0x1409ce630`)**: receiver `ai+0x13a4` (from the think unit) or `FUN_140507880`; a
  receiver-less punt aims `FUN_1409784d0` and is cut to **50 m** from the ball; punt type `cmd+0xff4`:
  without a power input: `ai+0x1399 → 1`, `ai+0x139a → 3`, else **skill bit 35 (GK High Punt) → 3**, else 0;
  with power: request byte 3 → 3, else **skill bit 18 → 1** (INF(med): GK Low Punt). If the keeper is within
  **3 m of his goal line** he does not punt yet: the command becomes a 5 m walk out (`cmd+0x878 = 3`).
- **Which of throw / punt** = which request the GK unit answer becomes: short (`GKPassShort`, sub 0) vs long
  (`GKPassLong`, sub 1, urgent under 15 m pressure or after 3 s). The converter from the bp answer to the request
  kind byte was not read (open, §10.8).

#### 10.6.5 Live check
(see §10.9)

### 10.7 What each finding replaces in GodotFootball

| finding | GF code it replaces |
|---|---|
| §10.2.2 out-of-possession spot (R-line, 4/15/20 caps, `4 + 9·ramp(40..70)` limiter, 2–4.16 m floor, 15 m max, 0.5 m Awareness jitter, 2.25 m dead zone, re-plan 2–9 frames) | `GoalieDefaultStrategy::RequestInput` non-bound-for-goal branch (`goalie_default.cpp` ~77–238): the post-to-post bisector, `GetKeeperComeOutBias(coverage)·styleBias`, the opponent/helper "shooting threshold" come-out code, `awayFromGoalOffset_m` 0.7 m, the `(pitchHalfW−0.7)` back line; `GameplayTuning::GetKeeperComeOutBias/Margin_m`, `PlayingStyles::GetKeeperComeOutBias` (1.35×) |
| §10.2.3 in-possession sweeper spot (6/9/13/20 m by ball x, 13/15/17 caps, 25 m behind the team line) | same function — GF has no in-possession keeper position (the bisector runs only when the ball is in his half, `ballPos.x·side > 0`) |
| §10.2.2 step 2 on-the-line mode (< 18 m from R, aerial ball) | the `IsBallBoundForGoal` intercept branch (`goalie_default.cpp` ~240–282) only for the positioning part; the shot response itself is §10.5.1 |
| §10.3 rush deciders (race with style factor 1.1/1.2/1.3, zone 13.4 × 6.4/14.6/17/20 m, turn limit by Awareness, PRESS-outside-the-box rule) | `elizacontroller.cpp` keeper branch ~524–527 (`GetTimeNeededToGetToBall_ms() < 1000` and designated possession → on-the-ball commands); `TeamAIController::ApplyKeeperRush` (300 ms window, `teamAIcontroller.cpp:1577`) and its `applyRushOut` use in `goalie_default.cpp:201` |
| §10.4 high-ball claim (reach table, first-man-can't-head-it test, claim point 1 m inside the box, 1.3·kf drop race) | none in GF (keepers only react through `_KeeperDeflectCommand` when the ball arrives) |
| §10.5.1 `KP_SAVING_MOVE` (on-target test with 1.5–3 m margin by Awareness, intercept walk/sprint tiers) | `GoalieDefaultStrategy::CalculateIfBallIsBoundForGoal` + the bound-for-goal branch, and `manualMovement` forcing in `elizacontroller.cpp` ~497–505 |
| §10.5.2 `KP_PRE_SAVE_OPERATION` (set when the carrier ≤ 15 m, facing, about to strike) | none (GF keepers never "set") |
| §10.6.2 walk-with-the-ball target | `pes::PesAfterCatchCarry` / `PesAfterCatchStep` (`peskeeper.cpp`) — **the port has four errors**: carry branch inverted, 2·B instead of 2 m, bearing args swapped (the step is toward the mate, not lateral), missing `|z| ≤ F7−1`; and it is wired as the **pass target** in `elizacontroller.cpp` ~306–355 while PES uses it as the walk target |
| §10.6.1 neighbour list | the "per-keeper 11-entry table" `ponytail:` comment in `elizacontroller.cpp` ~314–316 and keeper-port §4.4/§7 — GF's nearest-player search *is* the equivalent of PES's list |
| §10.6.3/4 GK think units, throw/punt variant | `PlayerSkills::PickDistribution` remnants and the `e_FunctionType_ShortPass/LongPass/HighPass` mapping in the retainer branch; the keeper `_AddPanicPass` at `possessionAmount < 3.0` (`elizacontroller.cpp` ~1195–1198) |

### 10.8 Open (what was tried, what would close it)
1. **`T+0xb6de`** (gates the 1v1-carrier rush and the alternate base position `FUN_1409dd470`): writers known
   (`0x1404161e0`, `0x1404f6c30`, `0x14215cf10`), not decompiled; live set only on the defending team while the
   opponents hold the ball deep (94/3,416 frames). `FUN_1409dd470` not read.
2. **`MA+0x251fa[side]`** (required for the high-ball claim and `KP_SAVING_MOVE`, and for keeping `KP_PRESS`
   as a keeper action): live it does not track "ball in that side's box" (tested, §10.2.5 run); writer not found.
3. **Ball-state word `0x81a1`** (`FUN_140a62d90`, roster block `+0xe8`): switches the "wide" 42 m caps; meaning open.
4. **`a2 = MA+0x228d4/8`** bearing used for the 2–4.16 m floor: writer not read (fit works with the live value).
5. **bp answer → request kind** converter (which unit answer becomes `KP_THROWING` vs `KP_PUNTKICK`), the target
   pickers `FUN_142132660` (short) / `FUN_142132020` (long) / `FUN_1409784d0` (receiver-less punt): not read.
6. **Save choosers** `FUN_142113390` / `FUN_1421139a0` (catch vs punch vs deflect per §§2–3), `FUN_14210be70`/
   `FUN_14210b890` (the claim solvers' two outcomes): decompiled (`gk_think2.a.c`), not reduced.
7. **RNG instance** of the positioning jitter (`Range0` slot 1 on the generator at `cmd+0x19c0`): instance not
   identified (rng report §3 open).
8. **Defensive GK (bit 20) live**: both live keepers are Offensive GKs; the 4 m / 13 m caps are static-only.

### 10.9 Live results, second run (OBS; scratch `ev.py`, `ev1.jsonl`; 600 s, second half, sides swapped)
- **Neighbour list `MA+0x22e4c`**: 362 dumps × 2 sub-lists: **721/724 sorted ascending**, value − (live d²) median
  **0.00**, p90 1.12 (one-tick staleness); **no player appears in his own list**. Closes §10.6.1.
- **Ball list `MA+0x24840`**: 6,996 entries, **6,057 (87 %) equal `ai(id)+0x1438` within 2 frames** (rest = update lag),
  720/724 sorted. So its value is **ball-arrival frames**, not a distance — corrects the duels-fouls §5 / defensive-shape
  wording "{id, dist}" for this list (OBS).
- **Keeper decisions observed** (`pl+0x5d0` transitions, 558 events, both keepers Offensive GK):
  - `KP_BASE_POSITION ↔ WAIT_TIMER (0x11)` 204× (the think's idle/hold cycle), `→ KP_PRE_SAVE_OPERATION` 6×,
    `→ KP_PRESS` 6×, `KP_SAVING_MOVE → KP_BLOCK_LATE` 2×, `→ KP_BLOCK_LATE` 3× more, `KP_PRESS → PASS_GET` 2×
    (the rewrite rule of §10.1), `KP_PRESS → CLEAR` 2× (save chooser), `KP_PRESS → KP_PUNCH (0x44)` 1×.
  - **Cross claimed and punched** (clock 34718→34746): ball rising to 2.5 m at `(49.3, 5.0)`, claim point
    `(49.4, 2.5, 4.7)` inside the goal area; keeper frames 50 vs nearest opponent 58 (he wins the race by 8 frames,
    i.e. inside the Offensive factor 1.1) → `KP_PRESS` → `KP_PUNCH` 28 frames later. Consistent with §10.4.
  - **1v1 sequence** (keeper of the second team, 25117→25168): carrier with the ball at `(−33.9, −2.7)`, claim
    `(−38.4, −3.4)` inside the box (|x| 38.4 > F6), keeper `KP_PRESS` twice, then `KP_PRE_SAVE_OPERATION` at
    25141 with the ball 12 m out (`(−36.5, −2.9)`), then `KP_BLOCK_LATE` when the shot came (ball at 1.0 m height,
    25168). Rush → set → block, as §10.5.3 describes.
  - **Shots on target → `KP_SAVING_MOVE`** (17717, 30427): ball at 1.4 m, opponents ≥ 142 frames away, record point
    `ai+0x12d8 = (−49.17, 1.39, −3.36)` / `(50.17, 1.22, 3.29)` (on the goal line, inside the posts), followed 3–4
    frames later by `KP_BLOCK_LATE`.
  - **Pre-save without a shot** (17686, 20609, 30379, 37407): carriers 15–20 m out (`(−35.2, 2.3)`, `(32.5, 3.0)`,
    `(30.1, −5.5)`) facing goal; keeper returns to base 1–5 frames later when the kick does not come.
- Not observed live: a rush through `FUN_1421162c0`'s ground branch where the keeper leaves the box (the
  `PRESS` rewrite fired only for passes to him), a Defensive GK, any distribution sample (`KP_AFTER_CATCH_MOVE`
  not entered in 600 s — no catch). Those stay static-only (§10.8).
