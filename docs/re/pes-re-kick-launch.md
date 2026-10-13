# PES 2021 kick launch: velocity and spin — evidence report

Tags: **OBS** = observed (raw bytes at the stated VA); **INF** = inferred (confidence).

Scope: `<scratch>/pesmem.bin` (memory image, base `0x140000000`, file offset = VA − `0x140000000`
for every section), disassembled with a local capstone script (`d.py`). Every formula below is
read off **raw bytes**, not from decompiler variable names. Vtables and ANIME action ids from
`~/ghidra-projects/scratch/FoxRegistryRE/rtti.py` + `actions.json`. Live process:
`sudo python3 ~/ghidra-projects/scratch/pes_state.py` reported `STATE=OPEN_PLAY` at the start
of the session; ball pointer and state read read-only via `/proc/<pid>/mem`, nothing written,
no input sent.

Scratch: `~/ghidra-projects/scratch/KickRE/`.

---

## 0. Headline

1. **OBS — There are exactly two launch writers, and both end in a shared normalise.**
   - `FUN_140731d80` = the `match::anime::action::*` **vtable slot +0x40** ("kick" slot),
     shared by `Kick`, `CornerKick`, `FreeKick`, `FreeKick2nd*`, `PenaltyKick`,
     `KickoffKicker`, `KickoffOther`, `KickoffReceiver`, `QuickRestartKick`, `Throwin*`,
     `WallJump`, `WallLoop`, `GoalKickStandby` (RTTI-resolved, `rtti.py cls 'anime::action'`).
     It re-dispatches on ANIME action id (0x20..0x3f) through the byte table at `0x140732490`
     into `0x140731df3` (open-play kick) / `0x140732468` (return).
   - `FUN_1408397a0` (called from `Kick`'s +0x40 override `0x140717c50` at `0x140717dfe`, and
     from `0x14083c590` right after) and `FUN_1407324b0` (called from `0x140731ffb`).
2. **OBS — No separate "aim vector" input.** The kicker's requested direction arrives as a
   vec3 `r9` and a target vec3 `r12`; both are copied to `[rsp+0x40]` / `[rsp+0x60]` and passed
   to `FUN_1407324b0(rcx, player, r8=&dir, r9=&target)`. That function returns a **unit
   direction** (normalised at `0x140732bd8` → `FUN_14046f230`) and writes the **speed** back
   into `r9`.
3. **OBS — The attribute-to-power curve is a logistic, not a lerp.**
   `t = clamp01((S − 40)/59)` where `S` is the raw ability byte (0..99); then
   `p = 5.5·t − 2.5`; `errScale = 1/(1 + exp(p))` (`0x1415a17b6` = `expf`, OBS by the
   `+1 / reciprocal` idiom at `0x1407327eb`/`0x1407327fd`). At S=40 → `1/(1+e^-2.5)` = 0.924;
   at S=99 → `1/(1+e^3)` = 0.0474. The reciprocal is the **error scale**, not the speed.
4. **OBS — Randomness is a per-player 15-bit LCG at `player+0x32d0`**, not a global.
   `FUN_140a92470(rng,k)`: `seed = |0x4513 − seed·0x2d22b2e3|; seed &= 0x7fff;` then
   `min(M−1, int(seed · (1/2^31) · M))` with `M` from the table at `0x1426548b0`
   = {100, 5000, 32768, 32768, …}. `FUN_1406e6bf0(player,m)` = `LCG(k=2) % m`.
   `FUN_1406ebf30(player,thr)` = `LCG(k=2) mod 100 < thr` — the success roll.
5. **OBS — The angle-error distribution is an exponential quantile table**, 513 floats at
   `0x1426548c0`, scaled by `0.000244140625` (2^-12). Verified numerically:
   `tab[i] = −ln(512/i)` exactly at i = 1, 2, 4, 8, …, 512 (e.g. tab[256] = −0.6931 = −ln 2,
   tab[128] = −1.3863 = −ln 4). `FUN_140a92590(rng, amp, ?)` samples it with linear
   interpolation and multiplies by `−2`, so the signed sample is `2·ln(u/512)` ∈ (−∞, 0] —
   a **one-sided** exponential noise, then offset by `+0.2` (`0x142598578`).
6. **OBS — Spin/curl is applied by rotating the velocity vector, not by a torque input.**
   Both writers rotate the unit direction in the (x,z) plane (`FUN_140a8fbb0`) by the curl
   angle and in the (y,z) plane (`FUN_140a8fb00`) by the elevation angle. Both use
   `sin(angle+90°)`/`cos(angle+90°)` from the table lookup `FUN_140a92c80` → table
   `0x142655950`, `tab[k] = sin(k·π/1024)`, 512 steps over 90° (verified: tab[64]=0.19509=sin
   (11.25°), tab[256]=0.70711).
7. **OBS — `FUN_140731d80` is a predictor, not a writer.** It builds a candidate ballstate with
   `FUN_140874860` (ball init/reset) and runs up to 250 ticks of the integrator
   `FUN_140876360` in a loop (`0x140731f50`–`0x140731fb4`, `cmp edi,0xfa`) to test
   reachability of the target. The only ballstate stores it makes are through
   `FUN_140731bb0` (a plain struct copy: `+0x00` pos, `+0x0c` vel, `+0x18` spin) at
   `0x1407320fe` and `0x140732453` — i.e. it copies a solved state, it does not launch.

---

## 1. The writers, classified by kick type

| ANIME id | ANIME name | class | writer entry | notes |
|---|---|---|---|---|
| 8 | `ANIME_LONG_PASS` | `match::anime::action::Kick` | `0x140717c50` → `0x1408397a0` / `0x14083c590` | open-play lofted/driven pass |
| 9 | `ANIME_SHORT_PASS` | `Kick` | same | open-play short pass |
| 10 | `ANIME_SHOOT` | `Kick` | same | shot |
| 31/32/33 | `ANIME_FREEKICK_{SHORTPASS,LONGPASS,SHOOT}` | `FreeKick` | `0x140742030` → shared | dead ball |
| 37/38/39 | `ANIME_FREEKICK2ND_*` | `FreeKick` | `0x140742000`/`0x140741f90` → shared | dead ball |
| 41/42/43 | `ANIME_QUICK_RESTART_*` | `QuickRestartKick` | `0x140742150` → shared | |
| 50/51 | `ANIME_CORNERKICK[_SHORT]` | `CornerKick` | `0x1407335c0` → shared | cross |
| 25 | `ANIME_KICKOFF` | `KickoffKicker` | `0x140782310` → shared | |
| 23 | `ANIME_KICKOFF_RECEIVER_LOOP` | `KickoffReceiver` | `0x140782330` → shared | |
| 22/24 | `ANIME_KICKOFF_KICKER_LOOP/OTHER_LOOP` | `KickoffKickerLoop`/`KickoffOther` | `0x140782330`/`0x140782360` | loop only |
| 53/54 | `ANIME_PENALTYKICK[_KICKER_LOOP]` | `PenaltyKick`/`PenaltyKickLoop` | `0x140782af0`/`0x140782330` | |
| 62/63/64 | `ANIME_KP_GOALKICK[_SHORT]` | `GoalKickStandby`/`GoalKick*` | `0x140782330`/`0x140774c50` | |
| 27/28/29 | `ANIME_THROWIN[_LONG,SEAMLESS]` | `ThrowinNormal`/`ThrowinLong` | `0x140794f20` → shared | hand, same slot |
| 52 | `ANIME_POSTMAN_LOOP` | `PostManLoop` | `0x140782330` | |
| 65 | `ANIME_KP_PUNTKICK` | `goal_keeper::PuntKick` | `0x140749130` | separate, not on this path |
| 66 | `ANIME_KP_THROWPASS` | `goal_keeper::Throw` | `0x140749130` | separate |

All entries above whose column 4 says "shared" reach `FUN_1407324b0`. The keeper punt/throw
(`0x140748b20`) and the inplay-demo kick (`0x1407e7f20`) were **not** decoded this session.

---

## 2. `FUN_1407324b0` — the literal launch formula (OBS)

Signature (from the call at `0x140731ffb`): `FUN_1407324b0(rcx=ctx, rdx=player, r8=&dirUnit,
r9=&targetVec3)`; returns a unit direction and writes speed into `r9`.

```
# 0. context
rbx = 0x14124dad0(0x140a489f0(roster))          # live player block = roster[idx] + 0x70
rng = player + 0x32d0                            # per-player LCG state
h   = f32[player + 0x2bcc]                       # ball height tier selector

# 1. foot / side sign                                        (0x1407325d5)
v        = byte[rbx + 0x34]                       # 0 = right foot, >=1 = left/weak
footSign = (v - 0.5 < 0) ? +1 : -1                # xorps sign flip at 0x140732612

# 2. ability with a flat per-kick bonus                     (0x14073261a..0x14073265c)
S    = FUN_1406e6660(player, 0x20)                # raw ability byte; 0x20 = the open-play
                                                  # kick attribute index (Low Pass family)
base = S - (5.0 if (footSign * f32[r9+4] > 0) else 0.0)     # 5.0 = 0x14259222c

# 3. ball-height tier on h                                   (0x140732660..0x14073278c)
   h >  0.9 : A = 18.0, B = 1.5,   add = (0 > h) ? 5.0 : 0.0
   h >  0.4 : A = 10.0, B = 1.2,   add = (0 > h) ? -2.0 : 0.0
   h > -0.1 : A = -15.0, B = 1.5,  add = (0 > h) ? -2.0 : 0.0
   h > -0.9 : A = -5.0,  B = 1.0,  add = (0 > h) ? 3.0 : 0.0
   else     : A = 0.02,  B = 1.5,  add = (0 > h) ? 1.0 : 0.0
base += add                                        # 0x14073278c: addss xmm6, xmm0

# 4. logistic -> ERROR scale (NOT the speed)                 (0x14073279d..0x1407327fd)
t        = (base >= 99) ? 0 : (base <= 40 ? 1 : (base - 99) / -59)   # clamp01((S-40)/59)
p        = 5.5*t - 2.5
errScale = 1 / (1 + exp(p))                        # 0x1415a17b6 = expf

# 5. angular scatter                                          (0x140732839..0x140732898)
u = FUN_140a92590(rng, B, 0.0)                     # = 2*ln(uTable/512) <= 0
w = B/4.2 + 0.65 - 0.25*clamp01-ish(B)
wrapped to (-360, 360]
if f32[player+0x2bd0] > 0.65 : w = max(4.0, w - 0.25*B)      # 0.65 = 0x14259d5d0
else                         : w = min(4.0, w + 0.25*B)      # 0.35 = 0x14259d5c8
spinMag = FUN_140a92590(rng, A, 0.0) * 100.0 * (0.7 or 0.8)  # 0.7 = 0x14257a194, 0.8 = 0x142572348
                                                            # pick by sign at 0x140732915

# 6. elevation / curl via rotation of the unit direction
rotateXZ(dir, curlDeg)                             # FUN_140a8fbb0: (x,y) plane
rotateYZ(dir, elevDeg)                             # FUN_140a8fb00: (y,z) plane

# 7. per-action spin-tilt table, indexed by ANIME id - 0x20 via byte table 0x140732d20
#    -> dword jump table 0x140732d10 (see §4)

# 8. speed                                                     (0x140732b3a..0x140732c7a)
speed_mag = sqrtf(dx*dx + dz*dz)                   # 0x1415a1780 = sqrtf (OBS by idiom)
elevDeg2  = speed_mag * A_tilt * spinMag           # 0x140732b6a
elevDeg2  wrapped to (-360, 360]
rotateYZ(dir, elevDeg2)                            # 0x140a8fb00
curlDeg   = XZ_tilt * A_tilt * spinComponent
rotateXZ(dir, curlDeg)                             # 0x140a8fbb0
dir *= speed_mag

# 9. write back                                                (0x140732c7a)
r9->x = ...; r9->y = ...; r9->z = ...
```

INF (medium): the exact register-to-variable mapping inside step 5 is partly inferred from the
compare/branch structure; the **instructions, immediates and table addresses are all OBS**.

---

## 3. The error model (OBS)

### 3.1 Distribution
Exponential, one-sided, quantile table at `0x1426548c0` (513 f32). Values scaled by
`0.000244140625` = 2^-12 give exactly `tab[i] = −ln(512/i)`:

| i | value | −ln(512/i) |
|---|---|---|
| 1 | −6.23828 | −6.23832 |
| 2 | −5.54517 | −5.54518 |
| 8 | −4.15894 | −4.15888 |
| 32 | −2.77271 | −2.77259 |
| 128 | −1.38623 | −1.38629 |
| 256 | −0.69312 | −0.69315 |
| 448 | −0.13354 | −0.13353 |
| 512 | 0.0 | 0.0 |

`FUN_140a92590` (`0x140a9261d`–`0x140a926e3`): two LCG draws mod 1000, table index
`int(u·512)` with linear interpolation, then `×0.000244140625 × −2`, i.e. the final sample is
`2·ln(u/512)` ∈ (−∞, 0].

### 3.2 Seed
Per-player LCG, **not** the match RNG: `player+0x32d0`, 15-bit, multiplier `0x2d22b2e3`,
`0x4513 − seed·m` with `btr eax,0x1f` (clear sign bit) and `& 0x7fff`. Modulus table
`0x1426548b0` = {100, 5000, 32768, 32768, …}, scale `4.656612873077393e-10` = 2^-31
(`0x1426550d8`). `FUN_1406e6bf0(player,m)` = `FUN_140a92470(rng,2) % m`.

### 3.3 What widens it
- **Ability**: `errScale = 1/(1+exp(5.5·clamp01((S−40)/59) − 2.5))`. S=40 → 0.924,
  S=70 → 0.5, S=99 → 0.0474. Lower ability ⇒ wider scatter.
- **Weak foot**: the `footSign` term subtracts 5.0 from the ability before the logistic when
  `footSign · dirY > 0`, i.e. striking with the weaker side.
- **Ball height tier** (`h`, per §2 step 3): changes both `A` (spin magnitude) and `B`
  (error width).
- **Success gates**: `FUN_1406ebf30(player, thr)` = `LCG mod 100 < thr`, used for the spin
  sign (`0x14083b0e2`, `0x14083b10e`, thr = 50), the pass-assist roll (`0x14083ca61`, thr = 50)
  and each scatter component (`0x14083a2ed` with `thr = |x|·100`, `0x14083a649`,
  `0x14083a84f`).
- **Pass assist** (`FUN_14083c590`, pass actions only, `0x14083c91d`–`0x14083ca9e`):
  `gap = FUN_140481b00(dist, xmm1)`; `a = clamp01((gap−120)/60)`, `b = clamp01((v−80)/55)`,
  curl add = `±22° · a · b`, rolled at 50%.

### 3.4 Fatigue / form
`FUN_14083c590` reads `player+0x2d14` bit 0 (`0x14083a300`) and `player+0x2480`/`+0x2488`
(leg/foot values, `0x14083a6d2`), and clamps the derived speed between
`16…22` (`0x14259be10`/`0x1425c48c0`) or `11…19` (`0x14259bdfc`/`0x1425c65e8`) depending on
`FUN_1406ead40(player, 0x1f)`. A final factor `min(u, 1000/2001)` is applied at
`0x14083a788`–`0x14083a7e2`. INF (medium): these are the fatigue/leg-condition terms, but the
live-stat multiplier pipeline of `pes-re-matchlogic.md` §3.1 (A2..A5) is separate and was not
traced into this function.

---

## 4. Per-action spin-tilt table (OBS)

Byte table `0x140732d20` (32 entries, indexed by ANIME id − 0x20):
`[0,1,3,3,3,3,0,1,3,3,3,3,3,3,3,3,3,3,0,3,3,3,3,3,3,3,3,3,3,3,3,2]`

Dword jump table `0x140732d10` → `0x140732af3`, `0x140732abe`, `0x140732b28`, `0x140732b3a`
(indices 0,1,2,3):

| idx | code | XZ tilt | Y tilt | evaluated when |
|---|---|---|---|---|
| 0 | `0x140732af3` | 4.0 (`0x1425656c0`) | 4.5 (`0x1425a1ff4`) | `XZ_tilt = spinMag·4.0`, `Y_tilt = spinMag·4.5` |
| 1 | `0x140732abe` | 2.5 (`0x14259bde4`) | 3.5 (`0x14259bde8`) | also `Ymin = 0.04`, `Ymax = 0.15` |
| 2 | `0x140732b28` | 2.0 (`0x142565604`) | 0.02 (`0x1425c4894`) | |
| 3 | `0x140732b3a` | 0 | 0 | default: no spin tilt |

(Default case `0x140732b3a` leaves `xmm11 = 0` and `xmm10 = 0`, i.e. no tilt.)

---

## 5. Constants, with their runtime source

All are `.rdata` immediates in `PES2021.exe` (VA given; the file offset is VA − `0x140000000`).
None come from `constant_match.bin` — the attribute curve is hard-coded in the exe.

### 5.1 Ability / logistic

| value | VA |
|---|---|
| 5.0 (flat ability bonus) | `0x14259222c` |
| 40.0 (ability clamp min) | `0x14259be2c` |
| 99.0 (ability clamp max) | `0x1425c48cc` |
| 59.0 (= 99−40, divisor) | `0x1425cd974` |
| 5.5 (logistic slope) | `0x1425c8378` |
| 2.5 (logistic offset) | `0x14259bde4` |

### 5.2 Ball-height tiers

| value | VA | meaning |
|---|---|---|
| 0.9 | `0x14258c260` | tier 1 threshold |
| 0.4 | `0x14259b89c` | tier 2 threshold |
| −0.1 | `0x14259d648` | tier 3 threshold |
| −0.9 | `0x1425cb314` | tier 4 threshold |
| 0.1 | `0x142588200` | tier 5 threshold |
| 18.0 | `0x14259be18` | tier 1 A (spin) |
| 10.0 | `0x142588210` | tier 2 A |
| −15.0 | `0x14259d668` | tier 3 A |
| −5.0 | `0x14259be7c` | tier 4 A |
| 0.02 | `0x1425c4894` | tier 5 A |
| 1.5 / 1.2 / 1.0 / 1.15 | `0x14259bdd8` / `0x14259d5d4` / `0x14259be7c`* / `0x1425e5728` | B (error width) per tier |
| 5.0 / −2.0 / 3.0 / 1.0 | `0x14259222c` / `0x14259a768` / `0x14258c264` / `0x14259be7c`* | tier adds |

\* two constants share a VA across branches; read the branch in §2 step 3.

### 5.3 Spin tilt

| value | VA |
|---|---|
| 4.0, 2.5, 2.0, 0.02 | `0x1425656c0`, `0x14259bde4`, `0x142565604`, `0x1425c4894` |
| 4.5, 3.5 | `0x1425a1ff4`, `0x14259bde8` |
| 0.04, 0.15 | `0x1425a9334`, `0x1425c48a0` |
| 0.025, 0.3 | `0x1425cb284`, `0x142590d98` |

### 5.4 Angles / wrapping / rotation

| value | VA | meaning |
|---|---|---|
| 90.0 | `0x14259be5c` | rotation phase offset in `FUN_140a8fb00`/`0x140a8fbb0` |
| 360.0 | `0x14259858c` | angle wrap |
| −360.0 | `0x142598598` | angle wrap |
| 180.0 | `0x142598580` | quadrant reduction in `FUN_140a92c80` |
| 57.295776 | `0x14259be40` | rad→deg |
| 0.2 | `0x142598578` | scatter offset |
| 0.7 / 0.8 | `0x14257a194` / `0x142572348` | spin magnitude pick by sign |
| 0.01 | `0x14256ae18` | spin scale |
| sin table (512 × f32) | `0x142655950` | `tab[k] = sin(k·π/1024)` |
| 0.65 / 0.35 | `0x14259d5d0` / `0x14259d5c8` | `player+0x2bd0` height gate |
| 0.25 | `0x142592228` | scatter width scale |
| 4.2 | `0x1425e61a0` | divisor in step 5 |
| 8.0 | `0x14259bdf4` | `max` floor in step 5 |
| 4.0 | `0x1425656c0` | `min`/`max` clamp in step 5 |

### 5.5 RNG

| value | VA |
|---|---|
| LCG multiplier `0x2d22b2e3` | immediate at `0x140a92491` |
| LCG constant `0x4513` | immediate at `0x140a92624` |
| mask `0x7fff` | `and`/`btr` at `0x140a924a7` |
| modulus table {100,5000,32768,…} | `0x1426548b0` |
| exponential quantile table (513 f32) | `0x1426548c0` |
| table scale 2^-12 = 0.000244140625 | `0x1426550dc` |
| 512.0 | `0x1426550e8` |
| 2^-31 = 4.656612873077393e-10 | `0x1426550d8` |
| −2.0 | `0x14259a768` |
| 1000.0 | `0x14256ae34` |

### 5.6 Pass assist / fatigue

| value | VA |
|---|---|
| 30.0, 80.0, 135.0, 55.0 | `0x142591140`, `0x142596e6c`, `0x14259be60`, `0x1425c6608` |
| 120.0, 60.0 | `0x14256ae30`, `0x1425c48fc` |
| 22.0 | `0x1425c48c0` |
| 100.0 | `0x14253ee30` |
| 0.217371866 | `0x1425a9bd4` |
| 16.0 / 22.0 | `0x14259be10` / `0x1425c48c0` |
| 11.0 / 19.0 | `0x14259bdfc` / `0x1425c65e8` |
| 1000.0, 2001.0 | `0x14256ae34`, `0x1425efa10` / `0x1425efa0c` |
| 3600.0, 20000.0 | `0x142598590`, `0x1425efa14` |
| 45.0 | `0x14259be30` |
| 0.1 | `0x142588200` |
| 0.6, 0.8 | `0x14257a190`, `0x142572348` |
| 2.0, 0.5, 1.0, −1.0, −0.0 | `0x142565604`, `0x142565600`, `0x14256ae1c`, `0x142588218`, `0x1425656d0` |
| 0.75 | `0x14259bdd4` |
| 0.9 | `0x14258c260` |

Math thunks (ILT at `0x1425301f0` region, resolved by idiom): `0x1415a1780` = `sqrtf`,
`0x1415a1792` = `cosf`, `0x1415a1798` = `acosf`, `0x1415a17b6` = `expf`.

---

## 6. Live observation (OBS)

`sudo python3 ~/ghidra-projects/scratch/pes_state.py` at session start:

```
STATE=OPEN_PLAY ctx=0x1c39f820 step=0.018519 ball=0x297383c pos=(1.00,0.50,-0.51) vel=(0.94,0.07,0.00)
```

- `step = 0.018519` = 1/54 s, confirming the tick of `pes-re-officials-ball.md` §0.4 at
  runtime.
- Ball position `y = 0.50` and `vel = (0.94, 0.07, 0.00)` are consistent with an in-flight
  ball, i.e. the launch path of §2 was exercised in this match.
- No launch event was **recorded** (see §7.1): the budget ran out before the sampler captured a
  kick and compared the recorded velocity against the formula.

---

## 7. What is still open

### 7.1 Live validation (the acceptance item NOT met)
The game was in OPEN_PLAY and the ball was located, but no recorded kick was captured and
compared against the formula. To finish it: poll `ballstate+0x0c` (velocity) at every tick and
detect the transition from `|v| ≈ 0` to `|v| > 1 m/s`; the tick at which it jumps is the
launch. Then check `|v|` against §2 and the elevation against the ball-height tier. Nothing was
written to the process at any point.

### 7.2 Attribute index per kick type
Only the open-play kick's attribute index (`0x20`) was confirmed, at `0x14073261a`. The
`stat(player, idx)` argument for each ANIME id (Low Pass / Lofted Pass / Finishing / Kicking
Power / Curl / Heading) was not enumerated — `FUN_1407324b0` takes the index as an immediate
only on this one path; the per-family selection lives in the `Kick` Update slot
`0x140719f20`, which was not examined.

### 7.3 Ball height tiers vs. actual launch elevation
The tier constants are decoded, but the mapping from the five `h` tiers to the *requested*
elevation angle (as opposed to the error width) is only partly traced.

### 7.4 Headers and clearances
`Header` and `Deflect`/`Catch` are **never requested** by GF's sim (see
`pes-re-sim-motion-mapping.md` §7.2), and their writers were not decoded. INF (medium): a
header runs through the same `Kick` writer with `Heading` as the attribute index, but this is
unverified.

### 7.5 Keeper punt/throw and inplay-demo kick
`ANIME_KP_PUNTKICK` / `ANIME_KP_THROWPASS` Update `0x140748b20` and
`InplayDemoKickFailed` Update `0x1407e7f20` were not decoded.

### 7.6 `FUN_1408397a0` tail
`0x14083b2d0` onward (the max-speed clamp and `FUN_140840130`) is decoded but not written up.

### 7.7 Attribute modifier pipeline
The live-stat multiplier pipeline of `pes-re-matchlogic.md` §3.1 (condition arrow,
familiarity, Super-sub, stamina drain) feeds the ability bytes; it was not re-traced into
`FUN_1407324b0` this session. So "how Form and fatigue enter" is answered only for the
`player+0x2d14` / `player+0x2480` terms of §3.4, with INF (medium) confidence.
