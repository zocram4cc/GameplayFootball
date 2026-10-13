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
