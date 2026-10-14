# PES 2021 — the dash reserve (`B` / "breath") end to end: writers, readers, resets — DashReserveRE (2026-10-14)

Read-only RE, static only. Image: the flat live image `pes_full.bin` (base `0x140000000`); decompile
path unused (every site here is small enough to read in raw disassembly), constants resolved straight
out of the image. Scratch: the DashReserveRE scratch dir (`calls.py` = exhaustive `call rel32` scan
with `.pdata` owner mapping, `pdisf.py` = flat-image capstone, `rips.py` = rip-relative operand
values, `cval.py` = raw constant read; outputs `bw.txt`, `cap.txt`, `ramp.txt`, `fad0.txt`, `set.txt`,
`ht.txt`).

Tags: **OBS** = read at the named VA. **INF(c)** = inference, confidence c. Addresses are image VAs.
`PMS` = the per-player match-state block, `MI` = `*(ctx+0x270)`, `gs` = `GetGameSpeed()`
(`0x141e532d0`, live 54.0). `bp` = the reserve percentage = `FUN_140a581c0()`, an **integer 0..100**.

Carried in: `pes-re-stamina-drain.md` (SD), `pes-re-anim-select-2.md` (AS2) §4, `pes-re-anim-select.md`
(AS) §3.5/§3.6/§4.2, `pes-re-locomotion.md` (LOC), `pes-re-tactics-clock.md` §5.1 (state byte).

---

## 0. Headline

1. **Item 1 closed. `B` has exactly one writer routine** — the FNV encoder `FUN_140a58f60`, reached
   from two places only: kick-off init (`0x140922fab`, `B = 1920` → clamped to cap) and the per-tick
   breath step (tail jump `0x14092badc`, from `FUN_14092b950`). Exhaustively verified: the encoder's
   FNV immediates (`0xcbf29ce484222325` / `0x100000001b3`) occur in the whole image in exactly five
   functions, and only one of them touches the `PMS+0x108` block — so there is no hidden writer, not
   even an inlined copy. §2.
2. **The per-tick rates are exact integers at gs 54: +5 standing/walking, +3 with the ball, −3 above
   22 km/h, −4 with the ball while above 22 km/h**, capped at `max(stamina%,20)·1920/100`. SD §3.2 is
   confirmed; nothing to correct. Full inputs and the float-exact `48/gs` form in §2.2.
3. **Item 2 closed with two distinct thresholds, which the earlier reports had merged:** the **top-speed
   cap** (`FUN_1406f9a2c`) bites from **bp < 20** and removes up to **20 Speed points**; the
   **acceleration ramp** (`FUN_1406eab00`) bites from **bp ≤ 10** and removes up to **10 Acceleration
   points**. AS2 §4.2's table is right about both magnitudes but lists them as one "fatigue". §3.
4. **Quantitatively, the reserve is a top-speed mechanic, not an acceleration mechanic.** At
   `G = 1.0` an empty reserve costs a Speed-99 runner 29.8 → 27.6 km/h (−7.4 %), a Speed-77 runner
   27.4 → 25.9 km/h (−5.5 %), a Speed-60 runner 26.1 → 25.3 km/h (−3.1 %); the ramp constants move by
   ≤ 2.7 % (§5). **This corrects the emphasis in AS2 §4.2 / the AS2 headline**: a reimplementation
   that ports only the ramp gets almost nothing.
5. **Item 3 closed negatively for half-time and positively for stoppages.** `B` is **never reset at
   half-time** (the break function calls only the stamina getter/setter), **and it does not need to
   be**: the breath step sits *outside* the "ball in play" gate, so `B` refills at the ordinary
   +5/+3 per tick through every dead ball, including the half-time break, until it saturates at cap.
   `cur` (stamina) is frozen in the same dead balls — the two tanks are frozen under opposite rules.
   §4.

---

## 1. The value (recap, OBS)

```
PMS + 0x108   guarded u32 "ring" block (hash@0, key@8, slot@0xc, ring[4]@0x10)   // SD §1 layout
B     = FUN_140a580b0(PMS)          // 1 call site in the image: 0x14092ba96
bp    = FUN_140a581c0(PMS)          // B*100 / (max(pct,20)*1920/100), integer division; 14 call sites
B set = FUN_140a58f60(PMS, v)       // encoder; also clamps v to the cap (see §2.3)
```
`FUN_140a581b0` returns the constant `0x780 = 1920` (1 call site, the kick-off init).

---

## 2. Item 1 — the writers of `B`

### 2.1 The complete writer set (OBS, exhaustive)

| writer | site | what it writes |
|---|---|---|
| `FUN_140a58f60` via `call` | `0x140922fab` (owner `FUN_140922dd0`, the kick-off tank builder) | `B = 1920`, clamped to cap |
| `FUN_140a58f60` via `jmp` | `0x14092badc` (tail of `FUN_14092b950`) | `B = max(0, B + d)`, clamped to cap |

Exhaustiveness argument (OBS): the encoder is identifiable by the two 64-bit FNV immediates. Scanning
`0x140001000..0x142530000` for `49be25232284e49cf2` (basis) and `48beb301000000010000` (prime) yields
**five** functions — `0x1408c9ec0` (a block at `+0x59530`), `0x141127369`, `0x1411d0640`, `0x1413f87e0`
and `FUN_140a58f60` — and only the last uses `rcx+0x108`. The two accessor families that *read*
(`FUN_140a580b0`/`0x140a581c0`/`0x140a58550`) and the stamina setters (`0x140a592c0` at `+0xc8`,
`0x140a595c0` at `+0xe8`) live in the same `0x140a5xxxx` cluster and are consistent with SD §1.
So: **one routine, two entry points.** No reset hook exists for `B`.

### 2.2 The per-tick step — `FUN_14092b950` (OBS, raw `0x14092b950..0x14092badc`)

```
r  = 48.0f / gs                                    // 48 @0x142588214 ; gs = 0x141e532d0()
d  = (int)(6.0f * r)                               // 6 @0x14259bdec        ; ebx
v  = |velocity| from FUN_140a6c6d0(motion, &out, min = 0.694444f/gs)   // floor @0x14259bdd0
     // v is metres per tick; the floor is 0.694444 m/tick = 2.5 km/h at any gs
if |v| * (gs*60*60/1000) > 22.0f:   d = (int)(d - 9.0f * r)     // 22 @0x1425c48c0, 9 @0x14259d60c
if idx == FUN_140a627e0(team, 0):  d = (int)(d - 2.0f * r)     // ball holder; team = [rsi+0x7e0]
B' = max(0, B + d)                                // cmovns at 0x14092baa7
tail-jump FUN_140a58f60(PMS, B')                   // 0x14092badc
```
Inputs, exactly: `gs`; the player's 3-D velocity (motion object from `FUN_140a48a10(ctx, idx)`); the
global ball-holder id `*(*(team+0x18)+0x2601c)` via `FUN_140a627e0(team, 0)` compared against
`Player+0x18`. **No fatigue, no stamina %, no decision state enters `d`.**

Integer values at gs = 54 (OBS; `6·48/54 = 5.33… → 5`, `9·48/54 = 8.0` exactly in float, so the dash
delta is exactly −3):

| state | d per tick | 0 → cap 384 (empty tank at 20 % stamina) takes |
|---|---|---|
| ≤ 22 km/h | **+5** | 77 ticks ≈ 1.43 s |
| > 22 km/h | **−3** | cap → 0 in 128 ticks ≈ 2.37 s |
| ≤ 22 km/h, ball holder | **+3** | 128 ticks ≈ 2.37 s |
| > 22 km/h, ball holder | **−4** | 96 ticks ≈ 1.78 s |

Sanity check against SD §8's time scale: the numbers there ("0→1920 in 7.1 s") assume the full 1920
cap, i.e. a 100 %-stamina player; the table above is the same rate against the smallest legal cap.
Timing: 1 tick = 1/54 s of game time (SD §7), so a dash empties a full reserve in ≈ 12 s of
continuous sprinting above 22 km/h and refills in ≈ 7 s of standing. **A sprint burst is a ~2 s
resource, not a ~10 s one** — INF(high) reading, the rate is OBS.

### 2.3 The clamp inside the setter (OBS, `0x140a59077..0x140a59174`)

```
pct = FUN_140a58550(PMS)                 // stamina %
cap = max(pct,20) * 1920 >> 5            // 20 imm 0x14 @0x140a5907f ; 0x780 @0x140a5908c
encode(v); if (decoded > cap) re-encode(cap)     // cmp @0x140a5915f, jle skips the re-encode
```
So `B ≤ cap` always, and the cap **falls as stamina falls** (`cap = max(pct,20)·19.2`): a tired
player's reserve is *both* smaller in absolute terms *and* full at 100 % only when his tank is. OBS.
Nothing else clamps `B`.

---

## 3. Item 2 — what reads the percentage

The complete reader set (OBS, exhaustive `call 0x140a581c0` scan): **14 call sites**

| site | owner (.pdata) | consumer |
|---|---|---|
| `0x1406f9ac0` | `FUN_1406f9a2c` | **top-speed / rate cap** (§3.1) |
| `0x1406eabe2` | `FUN_1406eab00` | **acceleration ramp** (§3.2) |
| `0x1406ea04f` | `FUN_1406e9f90` | clip playback-rate helper (§3.3) |
| `0x14092fbd8` | `FUN_14092fad0` | the stamina drain's breath term (SD §3.3, already ported) |
| `0x14092b105` | `FUN_14092af4c` | the breath stat penalty `p` (SD §4, already ported) |
| `0x1406b9522` `0x1406e5a95` `0x1406f8636` `0x140798861` `0x1407b4afa` `0x1407c29b9` `0x1407d7337` `0x1407e8c61` `0x14081e514` | various | not traced here (§8) |

All 14 are inside the anime/action range `0x1406b..0x14092f`; **there is no reader in the AI/velocity
code** (`0x1404xxxx`, `0x1405xxxx`). OBS: the reserve reaches the player's speed only through the
animation rate domain, never as a metres/second clamp.

### 3.1 The top-speed cap — `FUN_1406f9a2c` (OBS, raw `0x1406f9a2c..0x1406f9b65`)

Runs once per frame per AnimePlayer (stored into `P+0x377c`, AS §3.5's `rateMax`):

```
u        = clamp((G - 0.9166)/0.1667, 0, 1)                  // FUN_1406eae00, OBS call 0x1406f9a4a
base     = 1 - 0.085*u                                        // 0.085 @0x1425e4a0c
S        = ability 0x27 (Speed) of PMS[idx]                    // 0x1406f9a94
bp       = FUN_140a581c0(PMS[id from vtable+8])                // 0x1406f9ac0
f        = bp <= 0 ? 1.0 : (bp >= 20 ? 0.0 : (bp - 20)/(-20))  // 1.0 @0x14256ae1c, 20 @0x14259be1c, -20 @0x1425a946c
S'       = S - 20*f                                            // 0x1406f9afe
q        = (S' - 20)/79  clamped to [0,1]                      // 99 @0x1425c48cc, 79 @0x1425cb300
P+0x377c = base - 0.1*(1 - q*q)                                // 0.1 @0x1425e4a10
```
Thresholds (OBS): **bp ≥ 20 → no effect**; **bp ≤ 0 → the full 20 Speed points**; linear in between,
integer `bp` so the penalty moves in 1-point steps. Note `bp = 0` is reachable (the drain floors at 0).

Composed with the group-80 rate table `0x1425de010` (OBS knots `(m/s, rate)` = `3.056/0.295`,
`3.85/0.484`, `4.3/0.555`, `5.0/0.66`, `5.8/0.75`, `6.5/0.82`, `7.1/0.871`, `7.3/0.887`, `8.3/0.96`,
`9.16/1.0`), the steady top speed is `v_top = G · PL⁻¹(rateMax)`. At `G = 1.0` this reproduces AS's
published figures **exactly** (Speed 77 → 27.4 km/h, Speed 99 → 29.8 km/h), which validates the
composition; the table with the reserve drained is §5.1.

### 3.2 The acceleration ramp — `FUN_1406eab00` (OBS, raw `0x1406eab00..0x1406ead3f`)

Returns a **signed rate delta** for one tick. Arguments (INF med-high, inherited from AS §3.6's live
fit: target rate, current rate, cap; the function itself is only reachable through vtable slot +0x50 —
zero direct `call` sites, OBS):

```
bp   = FUN_140a581c0(PMS[idx])                       // 0x1406eabe2
acc  = ability 0x2b (Acceleration)
acc' = acc - (bp <= 10 ? 10 - bp : 0)                 // 10 @0x142588210 ; cmp al,0xa @0x1406eabe7
a    = clamp((acc' - 40)/59, 0, 1)                    // 40 @0x14259be2c, 99 @0x1425c48cc, 59 @0x1425cb2fc
d    = rate1 - rate2                                 // signed; zeroed when |d| < 1.19e-07 (0x14259bdc4)
if (d > 0 and rate2 >= 0.6):   delta = d * 40*(1.9 - 0.15*a*a) / ((arg3 - 0.6) * gs)   // 1.9 @0x1425e4a60, 0.15 @0x1425c48a0
elif (d > 0):                   delta = d * (1.4 - 0.15*a*a)                          // 1.4 @0x14259d5d8
else if (rate2 >= 0.6 and motion[+0x5d0] == 2): delta = d * (-3.0)                     // -3 @0x14259d654
else:                           delta = d * (0.2*a*a - 1.25)                           // 0.2 @0x142598578, 1.25 @0x1425ab128
```
Threshold (OBS): **bp ≤ 10**, i.e. the ramp penalty starts 10 points later than the speed-cap penalty,
and it is 10 Acceleration points, not 20 Speed points. The three regimes match AS §3.6 (including the
`motion[+0x5d0] == 2` snap-back case, which AS did not list — new).

Magnitude of the effect (§5.2): because `a` enters **squared** and the coefficients are 0.15/0.2, an
exhausted reserve lengthens every τ by **≤ 2.7 %**. That is the reason the cap (§3.1) is the mechanic
that actually matters.

### 3.3 The clip playback helper — `FUN_1406e9f90` (OBS)

Same 10-point Acceleration penalty, same `(x−40)/59` clamp, at `0x1406ea04f` — this is AS §4.2's
`0.95 + 0.10p² + 0.05t` helper. **Address correction:** AS §4.2 cites the read at `0x1406e9fb9`, which
is a `mov [rsp+0x40], rbx`; the real site is `0x1406ea04f`. AS §4.2's formula stands.

---

## 4. Item 3 — half-time and stoppages

### 4.1 No reset at half-time (OBS)

`FUN_140923060` (the break bonus, SD §3.4) contains exactly three calls: `0x140a58350` (stamina cur),
`0x140a48a30` (team), `0x140a592c0` (stamina cur setter). **No breath write.** The kick-off builder
`FUN_140922dd0` (the only `B = 1920` site) has a single caller `0x14041a628` in `FUN_14041a390`, the
match-setup path — once per player per match, not per half.

### 4.2 But the reserve refills at every stoppage (OBS)

`FUN_14092fad0` (the per-player per-tick driver) orders the steps:

```
... gate (see below) ...
0x14092fba1  call FUN_14092b950        <- the breath step, UNCONDITIONAL after the gate
0x14092fba6  eax = MI[0x116c]
0x14092fbac  cmp eax, 1 ; jne -> dead-ball/break branch
             ... the stamina drain (cur) only here ...
```
The gate (OBS, `0x14092fb49..0x14092fb95`): the two sub-mode objects (`ctx+0x400`, `ctx+0x3b0`) must
be inactive, `MI[0x1775]` must be **0** (state 0 = live; 8 and 10 also return early, and per
`pes-re-tactics-clock.md` §5.1 those are the frozen replay/demo states), `idx < 22`, and the player must
be on the pitch.

So: **at every dead ball — throw-in, free kick, goal kick, corner, goal celebration, half-time — `B`
gains +5/tick (+3 for the ball holder) while `cur` does not move at all.** A routine stoppage of a few
seconds restores the whole reserve; the half-time break restores it many times over, so in practice
**every restart of the second half starts with a full `B`** even though nothing wrote it. Replay/demo
(8/10) and the penalty shoot-out freeze both tanks.

This is the mechanic SD §8 step 2 states wrongly ("each sim tick **while the ball is in play**: §3.2
then §3.3") — the breath step is outside the in-play gate. §5.3 gives the GF consequence.

---

## 5. Numbers for the port

### 5.1 Top speed vs reserve (OBS formula, `G = 1.0`, `PL⁻¹` from the group-80 table)

| Speed stat | bp = 100 | bp = 20 | bp = 10 | bp = 0 | loss at bp 0 |
|---|---|---|---|---|---|
| 60 | 26.1 km/h | 26.1 | 25.6 | **25.3** | −3.1 % |
| 77 | 27.4 km/h | 27.4 | 26.6 | **25.9** | −5.5 % |
| 99 | 29.8 km/h | 29.8 | 28.6 | **27.6** | −7.4 % |

The two right-hand columns of AS §3.5's live check (27.4 at 77, 29.8 at 99) are reproduced by this
formula at `G = 1.0`, i.e. the AS figures already are the "reserve full" case.

### 5.2 Ramp constants vs reserve (OBS formula)

| Acc stat | bp = 100 (a, τ_rise, τ_low, τ_fall) | bp = 0 | change |
|---|---|---|---|
| 55 | 0.254 → 1.890 / 1.390 / 1.237 | 0.085 → 1.899 / 1.399 / 1.249 | +0.5 % |
| 70 | 0.508 → 1.861 / 1.361 / 1.198 | 0.339 → 1.883 / 1.383 / 1.227 | +1.2 % |
| 85 | 0.763 → 1.813 / 1.313 / 1.134 | 0.593 → 1.847 / 1.347 / 1.180 | +1.9 % |
| 99 | 1.000 → 1.750 / 1.250 / 1.050 | 0.831 → 1.797 / 1.297 / 1.112 | +2.7 % |

### 5.3 Reserve lifetime (OBS rates, §2.2)

Full reserve (cap 1920 at 100 % stamina): **−3/tick above 22 km/h → 12.0 s of sprinting**, +5/tick
standing → 7.1 s to refill. Smallest legal cap (stamina ≤ 20 %, cap 384): 2.37 s of sprinting,
1.43 s standing. Holding the ball while sprinting is the worst case (−4/tick).

---

## 6. GF counterpart — what each finding replaces

GF already has the tank; two things are missing and two are wrong.

| this report | GF code today | action |
|---|---|---|
| §2.2 the per-tick step (+5/+3/−3/−4, cap) | `src/sim/onthepitch/pesstamina.cpp:366-386` `UpdateBreath` | **already correct**, no change |
| §2.1 the 1920 kick-off fill | `pesstamina.cpp:354-355` + `src/sim/onthepitch/team.cpp:102-103` | already correct |
| §4.1/§4.2 the reserve refills at dead balls and half-time, `cur` does not | `src/sim/onthepitch/player/player.cpp:488-500` gates **both** `UpdateBreath` and `DrainTick` on `match->IsInPlay() && !match->IsInSetPiece()` | **wrong**: move `UpdateBreath` + the `breathPct` computation out of the in-play gate; keep `DrainTick` inside it. `src/sim/onthepitch/match.cpp:399-413` (the half-time bonus) then needs no change — the refill does the work |
| §3.1 the top-speed cap (bp<20, ≤20 Speed points, ≤ −7.4 %) | `src/sim/onthepitch/player/playerbase.cpp:103-109` `GetMaxVelocity() = sprintVelocity * GetVelocityMultiplier() * GetFatigueFactor() * GetSlipFactor()` — **A5 fatigue only, no reserve term**; consumed by `humanoid_utils.cpp:306` `StretchSprintTo(..., player->GetMaxVelocity())` and the debug print at `humanoid.cpp:141` | **missing**: add a reserve factor beside `GetFatigueFactor()`. GF has no `bp` accessor (`playerbase.hpp:109-118` exposes `GetFatigueFactor`, `GetBreathPenalty`, `GetStaminaPct` only) — one `GetBreathPct()` from `stamina.breath*100/BreathCap` (the same expression `player.cpp:496` already builds) is enough. Port the exact form (Speed stat → `q`, `rateMax = base − 0.1(1−q²)`, `v_top = G·PL⁻¹(rateMax)`) only if GF wants PES Speed numbers; the cheap faithful alternative is a multiplicative cap on the existing `GetMaxVelocity()` whose ratio matches §5.1 |
| §3.2 the acceleration ramp (bp ≤ 10, ≤10 Acc points, ≤2.7 %) | GF's acceleration is the per-tick `maximumOverdrive_mps` step and `StretchSprintTo` in `humanoid_utils.cpp:147-158, 306-311` | optional; see §5.2 — a ≤2.7 % τ change, lowest-value item on this list |
| §2.3 the cap tracks stamina % | `pesstamina.cpp:361-365` `BreathCap` | already correct |
| SD §4 breath stat penalty `p` (bp ≤ 20 → ≤10 points) | `playerbase.hpp:112` `GetBreathPenalty()` → `pesstamina.cpp:451-457` `BreathPenalty`, used at `player.cpp:576` | **defect**: `StaminaState::breath` holds the **raw** `B` (0…cap ≈ 1920), but `BreathPenalty` treats it as a percentage and returns 0 for anything ≥ 20 — which is always. The penalty is dead, so the four breath-sensitive stats are never reduced. Fix by passing the percentage the way `DrainTick`'s caller computes it (`player.cpp:496`) |

---

## 7. Corrections to earlier reports

| earlier | correction |
|---|---|
| AS §4.2: the breath read in the playback helper is at `0x1406e9fb9` | it is at **`0x1406ea04f`** in `FUN_1406e9f90`; `0x1406e9fb9` is a spill store |
| AS2 §0.4 / §4.2: "the reserve caps top speed **and** slows the rate ramp" (equal weight) | the ramp effect is ≤ 2.7 % (§5.2); the cap is ≤ 7.4 % and is the mechanic to port (§5.1) |
| AS2 §4.2's table gives one "breath penalty" | **two different thresholds**: < 20 % for Speed (20 points), ≤ 10 % for Acceleration (10 points) |
| SD §8 step 2: "each sim tick **while the ball is in play**: §3.2 then §3.3" | the breath step runs at **every** live tick, dead balls included; only §3.3 is gated on in-play (§4.2) |
| AS §3.5 calls `FUN_140a581c0(player)` in the rate cap | the cap reads the percentage of the id returned by **vtable slot +8** of the `rsi` interface (OBS `0x1406f9aa2`), while the Speed byte comes from `byte[P+0x2458]`. Same player in every observed case (INF high); the ramp and playback helpers use their own `byte[P+0x1998]` index |
| AS §3.6 lists three ramp regimes | a fourth branch exists: falling above rate 0.6 with `motion[+0x5d0] == 2` gives `Δ·(−3)` (§3.2), i.e. a fast snap-back, not the 1.25 form |

---

## 8. Open (with the exact next step)

1. The nine untraced breath readers (`0x1406b9522 0x1406e5a95 0x1406f8636 0x140798861 0x1407b4afa
   0x1407c29b9 0x1407d7337 0x1407e8c61 0x14081e514`) — SD §9 guesses "tired clip selection and action
   success rolls". Next: `dec.sh out/bp9.c 0x1406b93a0 0x1406e4874 0x1406f8518 0x1407987f0 0x1407b4a60
   0x1407c1f40 0x1407d72c0 0x1407e8c10 0x14081e470`, then `annot.py`, and classify each by what it does
   with the byte. Two of them (0x1406ea04f, 0x1406eabe2) are already known stat-penalty sites, so
   expect more of the same rather than new mechanics.
2. `FUN_1406eab00`'s argument identities and the `40`/`gs` normalisation — the caller is a vtable slot
   (`+0x50` per AS §4.2) and was not located. Next: find the interface whose vtable holds
   `0x1406eab00` (search `.rdata` for the pointer) and read its call site.
3. `motion[+0x5d0] == 2` in the ramp snap-back branch — no meaning assigned. Next: find that field's
   writers (scan `0x5d0` stores on the motion object) or check whether it is a state enum.
4. Whether the sim-side velocity is ever clipped by `rateMax`, or only the animation rate — OBS says no
   reader outside `0x1406b..0x14092f`, but LOC's claim that the player is "clip-limited at ≈8.96 m/s"
   implies the clip is what actually limits him. Next: a live trace of `P+0x377c` against the player's
   measured ground speed over a dash (the AS §7 harness already reads `P+0x377c`).
5. Live confirmation of §4.2 (breath refilling at dead balls) — SD's live sampler only tracked `cur`.
   Next: sample `B` with `stam.py` through a stoppage; expect +5/+3 per tick at `MI+0x116c == 2`.

---

## 9. Ledger

| ID | claim | evidence | method | conf |
|---|---|---|---|---|
| DR-01 | `B` has one writer routine and two entry points (init `0x140922fab`, per-tick `0x14092badc`) | FNV-immediate scan → 5 functions, only `FUN_140a58f60` uses `+0x108`; call scan → 1 call + 1 tail jump | static | OBS |
| DR-02 | per-tick delta `+5 / +3 / −3 / −4` at gs 54, inputs = 3-D velocity vs 22 km/h, ball-holder id, `gs` | `0x14092b950..0x14092badc` + constants 48/6/22/9/2/0.694444 | static | OBS |
| DR-03 | the setter clamps `B` to `max(stamina%,20)·1920/100`, evaluated inside the setter | `0x140a59077..0x140a59174` | static | OBS |
| DR-04 | the reserve reaches speed only via the anime rate cap: `rateMax = base − 0.1(1−q²)`, `q = (Speed − 20f − 20)/79`, `f = clamp((20−bp)/20,0,1)` | `0x1406f9a2c..0x1406f9b65` + 0.085/0.1/0.9166/0.1667/20/−20/79/99 | static | OBS |
| DR-05 | no breath reader exists outside `0x1406b..0x14092f` | 14 call sites of `0x140a581c0`, whole image | static | OBS |
| DR-06 | the ramp penalty is 10 Acceleration points below bp 10 and enters `1.4/1.9/1.25 − c·a²` | `0x1406eab00..0x1406ead3f` | static | OBS |
| DR-07 | the two consumers use different thresholds (Speed < 20 %, Acc ≤ 10 %) | `0x1406f9ac5` (20) vs `0x1406eabe7` (`cmp al,0xa`) | static | OBS |
| DR-08 | no half-time write to `B`; the break function touches only `cur` | `FUN_140923060` call list | static | OBS |
| DR-09 | the breath step runs outside the in-play gate, so `B` refills at every dead ball | `0x14092fba1` before the `MI[0x116c]` test at `0x14092fba6` | static | OBS |
| DR-10 | top-speed loss at an empty reserve: −3.1 % (Speed 60) … −7.4 % (Speed 99) at `G = 1.0` | DR-04 composed with the table at `0x1425de010`; reproduces AS's 27.4 / 29.8 | static + arithmetic | OBS (formula), INF high (the `G·PL⁻¹` composition is AS's, it reproduces AS's numbers exactly) |
| DR-11 | the ramp's τ changes by ≤ 2.7 % between a full and an empty reserve | DR-06 arithmetic over Acc 40..99 | arithmetic | OBS |
| DR-12 | GF's `pes::BreathPenalty` always returns 0 (raw `B` read as a percentage) | `pesstamina.cpp:451-457` vs `:354-355`/`:361-365`, `playerbase.hpp:112` | source read | OBS |
| DR-13 | GF freezes the reserve at dead balls, PES refills it | `player.cpp:488-500` vs DR-09 | source read + static | OBS |