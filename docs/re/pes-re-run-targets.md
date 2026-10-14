# PES 2021 — Chance-run target (O1/O3 slice) and common run stop test (O4)

Static only, 2026-10-14. Decompiles + raw disassembly of `pes_full.bin`; files in the RunTargetsRE scratch dir
(`b.c` = 602600/59ba60/688c00/5c0f00, `h2.c`/`g2.c` = helpers, `l68.txt` = linear disassembly of 688c00 that skips past `ret`,
`lin.py` = that sweeper with rip-constant folding). Tags: OBS = read at the given VA; INF(level) = inferred.

Conventions (all OBS unless tagged):
- `F` = `*(G+0x2aa8)`, **`F[k]` at byte `+4k`** (OBS `thunk 0x14059f9a0` reads `**F` as F0 and `F[1]` as `+4`). So `+0x04` = F1 34.06
  half-width, `+0x10` = F4 47.06, `+0x14` = F5 9.16, `+0x18` = F6 36.14, `+0x1c` = F7 20.12 (values: `pes-re-fieldinfo.md`).
  (The off-ball report's `F[4]+2.5 = 49.56` used the same convention and is right.)
- `s` = team attack sign `T+0x254`; runner pos `P = player+0x55c` (x,y,z); vel `+0x574`; facing `+0x554`; ball `B = MA+0x2482c`.
- `fps` = `FUN_141e532d0()` = 54. `a4c720(a,b,s,k)` = `(s·k + a)·s < s·b` ("a is more than k m behind b", attack frame).
- "pred(p, n)" = `p.pos + n·p.vel`, `n = int(fps·0.3+0.5) = 16` frames (OBS cvttss2si 0x14059bbe4).

## 0. Correction: `spaceRun.o` key map (O3) — the leftovers §1.3 table is wrong

The registry record returned by `FUN_141e5d4c0(·,0xc3)` has an **8-byte header**; blob offset = record offset − 8 (same rule as
defensive-shape's "+4 rule from +8"). The key **order is the PES19 `spaceRun.txt` order, not alphabetical** (OBS: the 51 keys
before the packed byte word line up 1:1 with the 2021 dump `o_spaceRun.o`, and every int/float type matches). Record offsets:

| rec off | key | value | | rec off | key | value |
|---|---|---|---|---|---|---|
| +0x08/0c | angleDiff_chance /_good | 100.0 / 125.0 | | +0x74/78 | checkLastLineDist_chance /_good | 24.0 / 30.0 |
| +0x10/14 | angleDiff_second /_good | 70.0 / 95.0 | | +0x7c/80 | checkMarginDist /_good | 6.0 / 11.0 |
| +0x18 | backwardCurve | 1 | | +0x84/88 | checkMoveAngle /_good | 90.0 / 111.0 |
| **+0x1c/20** | **chanceScore /_good** | **102 / 88 (int)** | | +0x8c/98 | checkOffsideLineDistX /_good | 12.0 / 12.0 |
| +0x24/28 | checkAngleWidthSector /_good | 40.0 / 69.0 | | +0x90/94 | checkOffsideLineDistX_counter /_good | 2.25 / 2.25 |
| +0x2c/30 | checkAreaNum /_good | 3 / 5 | | +0x9c/a8 | checkPlayPointDist /_good | 41.0 / 42.0 |
| +0x34/38 | checkBackDist_second /_good | 13.0 / 19.0 | | +0xa0/a4 | checkPlayPointDistX /_good | 23.0 / 36.0 |
| +0x3c/40 | checkBallDist_chance /_good | 27.5 / 33.0 | | +0xac/b0 | checkScore /_good | 92 / 98 |
| +0x44/48 | checkBallDist_counter /_good | 19.0 / 25.0 | | +0xb4/b8 | checkSpaceRunCount /_good | 1 / 2 |
| +0x4c/50 | checkBallDist_second /_good | 29.5 / 40.5 | | +0xbc | checkZ | 41.5 |
| +0x54/58 | checkBodyAngle /_good | 84.0 / 105.0 | | +0xc0/c4 | checkZ_counter /_good | 14.5 / 20.0 |
| +0x5c/60 | checkCount /_good | 2 / 3 | | +0xc8 | checkZ_good | 44.0 |
| +0x64/68 | checkFreeDist /_good | 30.0 / 35.0 | | +0xcc/d0 | checkZ_second /_good | 24.0 / 32.0 |
| +0x6c/70 | checkInterceptScore /_good | 97.0 / 105.0 | | +0xd4 | packed bytes 0x01010101 | |

Last 8 words (`5,7,4,3,1,0,0,0`) = createPassCourse…vitalSupportPrior; 2021 has 3 more keys than PES19 there, so those 8 are
**not** mapped 1:1 (INF low). Evidence that the score is an int: the builder loads the raw dword and passes **its low byte**
(`movzx eax, byte [rsp+0x68]` 0x14060291e → `mov [rsp+0x28], al`); a float 40.0 would give 0. So limit = **102** (88 `_good`).

Only keys used by this slice: `+0x1c/+0x20` (602600). `59ba60` itself reads no 0xc3 key; its callee `FUN_140664ee0` does
(not decoded, below). The other readers' keys are now mappable with the table above (still open, O3 rest).

## 1. ChanceSpaceRun target builder `FUN_140602600(sel, out, ctx, id)` (OBS 0x140602600–0x140602b1f)

`ctx = {ctx0, T, reg, team, …, cap @+0x24}`; `out = {x, y, z}` init 0; `y = −100` (0xc2c80000) means "no target".

```
good  = FUN_1405c0f00(ctx0, id, 0x1e)       # 0x1e: style flag 0 set → true; else flag 9 set and !a4c9a0(id, −10) → true
limit = low_byte(reg0xc3[good ? +0x20 : +0x1c])   # 88 : 102
clear bit slot%11 in sel+0x288..0x298
g60a4d0 = FUN_14060a4d0(ctx, id)            # "lane in front of line" gate, below
if FUN_14060a750(ctx, id):  if FUN_14059ed10(ctx0,id,P,out,0xff,0x46,1,1) return    # sector sweep
if sel+0x2b8 == id: copy FUN_14059f9a0(…), set sel+0x2a0 = id, bit +0x28c; return
if id in {sel+700, sel+0x2c0, sel+0x2c4}: return (keep stored)
n = count(T+0xa2a0[0..10] == 0x1e)
if n >= ctx+0x24: if !FUN_14059ba60(ctx0,reg,id,out,sel+0x280, 100, 0xff, 1): out.y = −100; return
if sel+0x2a4 == id: FUN_1406067c0 (offside line x, ball z ± 10; leftovers §1.1); return
if sel+0x2a8 == id: FUN_14059ed10 from (lineX(FUN_140599d90), 0, ±6·sign(ball.z)), 0, 4, 1; return
if state(id) == 0x2f and FUN_140606860: return
if FUN_140602b20: bit +0x294; return
if !g60a4d0 and FUN_14059ba60(ctx0,reg,id,out,sel+0x280, limit, 0xff, 0): return     # <- the chanceScore use
…fallbacks exactly as leftovers §1.1 items 9 (OBS here, unchanged)
```
OBS correction to leftovers §1.1 item 9: the gate before the score search is **`FUN_14060a4d0 == 0`** (r12b, 0x1406026db /
0x140602919), **not** the `_good` flag; `_good` only picks the limit.

`FUN_14060a4d0` (OBS g2.c:1–65) true = "go straight for the line": `a4c720(lineX, P.x, s, −4)` and `!FUN_1405a0d20`;
then true if `!FUN_1405a5c90`, else needs receiver `r = FUN_140a63020` ≠ carrier, no opponent `FUN_140a4e250(r,2.0)`,
no cone `a4e720(r, T.dir, 90°, 6.5)`, (`|angle(r→id) − r.facing| ≥ 95°` or `|T.dir − that| ≤ 24°` or dist² ≥ 1024),
`a4c720(r.x, P.x, s, 5)`, and `!a4c720(MA+0x40+0x14·opp, r.x, s, −7.5)`.

`lineX = FUN_140599d90(ctx, team)` (OBS): `MA+0x44+0x14·opp` (opponents' offside-line x); with no carrier it is advanced to
the ball x if the ball is past it, and to the receiver's `FUN_140a76240` x if `T+0x78` is in state 5.

## 2. Target search `FUN_14059ba60(ctx, reg, id, out, sel, limit, p7, p8)` (OBS 0x14059ba60–0x14059dab0)

**Where the point comes from (port formula).** Every candidate is `C_k = (X, 0, z_k)`:
```
z_k = F1 − 2 − 2k,  k = 0..32                       # FUN_140a71990(F1, k), OBS 0x14059c4e7 xmm0 = [F+4]
k0  = FUN_140a71560(F1, P): nearest lane to P.z (first k where |P.z − z_k| stops shrinking)
window k ∈ [k0 − 8, k0 + 8] ∩ [0, 32]          # 8/10 then min(·, byte 0x143449d88 = 8) → always ±8 (OBS)
Ppred = pred(runner, 16)
X  = FUN_140599e90(ctx, team, id, 0)            # max over s of: ball.x and the two 'last' opponents' FUN_140597ff0 x
     (players from FUN_14063fbc0(ctx, opp)); INF(med): the deepest-defender line
if s·X <= s·Ppred.x: X = Ppred.x + 0.2·s        # 0.2 @0x142598578
```
So **the runner targets the defensive line (or ball, whichever is deeper) at one of 17 lanes 2 m apart around his own z**;
the search picks the lane.

**Pre-pass "lane blocked" mask** (runs only if `!FUN_1405aa480 && !FUN_1405aa6b0 && !FUN_1405a8df0 && !dangerFlag`, or
`MA+0x24d38 != 0`; `dangerFlag` = team instruction 1/2 set and `FUN_140a4c9a0(id, class>4 ? −12 : −7)`):
```
for k in window: d_k = |C_k − P|, b_k = bearing(P→C_k) (deg)
for each active opponent o (MI+0x1748), skipping o in state 0x36 whose mark target is valid, ≠ runner and
    (FUN_1405a4830(target) or a4c9a0(target, −6.5)):
  for k not blocked (and team state at that slot ≠ 0x3a):
    if sideFlag and (sign(z_k) ≠ sign(ball.z) or |z_k| < F2 + 4): skip
    a = |fold180(b_k − T.dir(+0x250))|
    w = a ≤ 55 → 1; a < 88 → (88 − a)/33; else 0
    r = (0.6 + 0.4·w)·7                          # 0.6/0.4/7 @0x14257a190/0x14259b89c/0x14259bdf0
    R = d_k ≥ r ? (d_k − r)² : 0
    if |o − C_k|² < R: blocked |= 1<<k
```
`sideFlag` = `FUN_14059fd70(id)` (OBS: state 0x1e/0x1f ≥ int(fps·0.2+0.5)=11 f, `T+0xb748 == 0x30`, sub ≠ 3, a4c9a0(−15),
carrier a4c9a0(−12), carrier facing within 135°, same-side z test vs F7, two `a4e720` cones 90°/(max(5,|carrier.x−line|)+5) and 180°/7).

**Per-lane scoring** (k ascending; first-found best kept with ties below):
```
skip if blocked(k)
center = sideFlag and (sign(z_k) ≠ sign(ball.z) or |z_k| < F2 + 4)
if runner moving (|vel·(0.6944/fps)·…| > 3000/(fps·3600)), in state ≥ int(fps·0.1+0.5)=5 f, class ≠ 3, !p7flag, !center:
    skip if |fold180(bearing(Ppred→C_k) − velHeading)| > 130
skip unless T+0xb824 == 3 or FUN_140a4d2c0(C_k) ≠ T+0xb824        # region code
S = byte MA+0x25378[opp·0x21 + k]                                # per-lane value table (writer not found, §4)
if any FUN_1405a7840/6de0/60f0/73c0/65a0/7960 (C_k) or
   (S < 100 and T+0xb8e8 and T+0xb8d4 < 22 and |z_k − T+0xb8e4| ≤ 7)  [this one also sets center-like flag] or
   FUN_1405a7520/6910/7270 (C_k):  S = 100
if recv exists (FUN_140644fe0) and d=|z_k − recv.z| > 2 and |z_k − ball.z| < |recv.z − ball.z| and |z_k| < F5:
    S += int(min(1,(d−2)/3)·50) + 100                             # OBS decompile 0x14059cd42 block
accept only if (FUN_1405a59d0(C_k) or S ≥ limit) and FUN_140595cb0(C_k)
FUN_140594470(…) → laneBudget byte
if !FUN_1405a5b20 and !FUN_140a64030(id,9) and T+0xa508 ≠ 2 and !moving-flag and !FUN_1405aaf00 and !center-flag
   and !dangerFlag:  require FUN_140592fd0(ctx,id,k,limit,laneBudget)
if FUN_140439ef0-based lane-angle test (only when the 'pinned' flags cStack_3a8/cStack_387): a=|bearing(Ppred→C_k) − T.dir|;
   if a < 65: t=|… − carrierFacing+180|; w = t<35 ? (35−t)/35 : 0, or 1 if a ball-line/corridor test passes
   (a4c720, FUN_140a90d80 hit with |z| < F5+1.5); S += int(0.3·w·S)
pen = 0; if a4e720(id, bearing(P→C_k), 20, 12): pen = −50 elif a4e720(…, 40, 12): pen = −25
for 3 nearest opponents (MA+0x22e4c list, stop at dist² > 225):
    if |o.z − C_k.z| ≤ 3 and |bearing(Ppred→o) − bearing(Ppred→C_k)| ≤ 90 and o projects onto P→C_k with t ≥ 0 and perp² < 1:
        d = |…|; w = d ≤ 1 → 1; d < 2.5 → (2.5 − d)/1.5; else 0;  pen = min(pen, int(−50·w))
S += pen
if center and |ball.z − z_k| < 8 + 8·u (u = ramp(s·ball.x, F6−25 → F6−5)): S = min(S − 80, 90)
if |z_k| > 14: S −= 50·ramp(s·ball.x, 10 → F6)·ramp(|ball.z − z_k|, 15 → 30)
if FUN_1405a4830(id) and FUN_1405aaba0(C_k): S −= 50
sec = FUN_14059a1f0(C_k); require FUN_140595ab0(C_k, sec)
cnt = T+0xbab4[slot·0x21 + k]; if cnt < 45 require FUN_1405ab180; if p8 require 5abcc0 or 5ab380 or 5ab990
if cnt ≥ bestCnt (init −10000):
    if S == bestS: tie → d5 = FUN_1405973b0(C_k); prefer |refZ − z_k| < 5 with d5 < bestD, else need |refZ−z_k| ≤ bestDz
                   and FUN_140595260 + FUN_1405955e0 (+ side test when moving)
    elif S < bestS: skip
    require FUN_140664ee0(ctx, reg, …, flags) ; commit best = C_k, bestS = S, bestCnt = cnt, bestDz = |refZ − z_k|
out = best (if any)                                               # refZ = FUN_14059b180 output +8
```
Interceptability flag (passed to 664ee0, OBS 0x14059c5xx loop): any active non-GK opponent with
`FUN_14069cd40(ball, Ppred, Ppred, o.pos, 18.0556 m/s, 1) ≥ 0` (= 65 km/h).

## 3. Common stop test `FUN_140688c00(ctx, id, run, arg)` — full cascade (OBS; 1 = stop, 0 = keep)

Head (b3a.c 438–530, already in off-ball §2.2) unchanged. Rest, in order (`t` = frames in current state, ring `T+0xa2cc+0x34·slot`;
`c` = carrier `FUN_140a627e0`, or `MA+0x2483c` when none; `d2` = |P − ball|²; `cls` = AI class `FUN_140a580a0`):
```
 1 last ball event pass to him → (stop now | through: stop at ≥ fps/3 = 18 f after kick)          [head]
 2 if c valid: FUN_1405a7fe0(id,1) → KEEP; FUN_1405a7be0 → KEEP
   if c is controlled (a6e340) and (clip +0x1c < +0x1a or +0x1a ≥ int(0.15·fps+0.5)=8 and ≠) and FUN_1405a7d10 → STOP
   if c's pass target (+0x1490) == id and c controlled: clip not yet 8 f in → KEEP
 3 if c valid and !a4c9a0(id,0.7) and (MI+0x1198 − AI+0x14a8) < int(0.3·fps+0.5)=16 → KEEP
 4 FUN_14068f600 → STOP;  FUN_14068bd90 → STOP
 5 ball event type 4 and d2 > 15 and event-team ≠ ours → STOP
 6 F = slot flag T+0x29c+0x40s set and (T+0xb748 ≠ 0x30 or cls == 6), or sub +0x28c == 10  ("pinned run")
   k = |P.x − ball.x| ≤ 4 → 10; < 10 → 10·(10−|dx|)/6; else 0;  if pinned: k = 15 else if !FUN_140596240 → STOP
 7 player flag bit 21 (+8) → KEEP
 8 if d2 > (k+40)²: STOP unless a4c720(P.x, ball.x, s, run==0x1f ? 13 : 8)    # far runner must be ≥13/8 m behind ball
   if run ≠ 0x1f and FUN_140692810 → STOP
 9 FUN_140692a00 → STOP; run ≠ 0x1f and FUN_140692580 → STOP
10 intercept = any active opponent ≥ 2.5 m (6.25 m²) from ball with FUN_14069cd40(ball, pred(id), P, o, 20.83 m/s (75 km/h), 1) ≥ 0
11 if c valid and t > 2·fps and !FUN_14068a730(run,intercept) and !FUN_14068a8c0(intercept) and run ≠ 0x1f and !pinned
   and !FUN_1405a4b90(team):  a = min(|c.facing − bearing(ball→AItarget)|, …) ; if a > 130 and d2 > 169 and
   (a4e720(c, dir, 70°, 6.5) or d2 > 1156) → STOP
12 if c valid and t > fps+0.5 and run ≠ 0x1f and !pinned:
     if d2 ≥ 289 and |P.z − ball.z| > 0.5 and a4c720(ball.x, P.x, s, −5) and !FUN_1405a4f40:
        if run ≠ 0x1d: [angle tests 45°/24 m/80° on carrier→target, lineX-relative 45°/20 m/5 m/10 m, a4c720(ball,P,4),
           a4c720(lineX, ball, −5), 90°/18 m/10 m same-side z test, a4c720(ball,P,0), |ball.x−P.x| < 15 → skip to 13]
        a = min(bearing dev of carrier to P);  L = 95 + 35·w, w = dist ≤ 15 → 1; < 27 → (27−dist)/12; else 0
        if a > L and !FUN_140694d10: STOP if !a4c9a0(id,−10) or carrierFacingDev > 80
13 if !pinned (or the t/run gate in 12 failed): FUN_14068dba0 → STOP; FUN_14068de30 → STOP; FUN_14068e300 → STOP;
   run ≠ 0x1f and |P.z − ball.z| > 43 → STOP
14 if run ≠ 0x2f and !(run ∈ 0x1d..0x1f and cls == 2):
     r = t > int(0.8·fps+0.5)=43 ? 0.7 : 1.5
     q = FUN_1405969b0 (receiver); if q valid and (q == c or dwell(q) ≤ 3·fps): FUN_1405951b0(id, r) → STOP
     else a4c9a0(id, r) → STOP
15 FUN_140a53460 → STOP
16 if t > 4·fps and c valid and !FUN_14068a730 and !FUN_14068a8c0 and FUN_1405951b0(id, −2.5):
     dev = |fold(bearing(c→id) − c.facing)|; w = dist(P,ball) ≤ 15 → 1; < 35 → (35−d)/20; else 0
     if dev > 100·w + 70 → STOP
17 run == 0x1f and t ≤ 5·fps: keep-alive if a teammate (FUN_1405a4830) within 56.25 m² is within 70° → skip to 19
18 sub == 8 and !a4c9a0(id,−8) → noSkip;  if !pinned and (cls > 25 or bit cls of 0x2264020 clear) and !noSkip and
   T+0x2a3 flag == 0 and FUN_1405a40a0 → STOP
19 if !FUN_1405a1cc0:
     run ≠ 0x1f and c valid and c action ∈ {9, 0x23, 0x29} (mask 0x22080000200) with clip < int(0.1·fps+0.5)=5 f and
        c's AI action ∈ same set targeting id → STOP
     if !FUN_140694320 and (run == 0x1f or (!FUN_14068efc0 and (pinned or (!FUN_14068e900 and !FUN_14068ec50)))):
       if t ≤ 3·fps or !FUN_14068d920:
         if !pinned: (t > fps/2 and FUN_14068b1a0) | FUN_14068b070 | FUN_140692c50 | FUN_14068b920 | FUN_140691ef0 |
                     (run ≠ 0x1f and t > int(2·fps+0.5) and FUN_140692fe0) → STOP
         if none of [FUN_140690b90, (!pinned: 690630, 690400), 690cf0, 694650, (!pinned: 693a20), 693730(intercept),
                     (!pinned: 68b6e0, run==0x1f: 68c680, 68e660(int), 68b440(int), 68c050(arg), 693340, 692ea0)] :
           if t ≤ fps+0.5 or !FUN_140694bc0:
             if !pinned: 691a80 | 691d10 | (t > fps+0.5 and 68fa10) → STOP
             if !FUN_14068d0b0(pinned):
               if !pinned: 691300 | 690960 | (t > 2·fps and 68aeb0) → STOP
               if cls ∉ {0x12,0x15} or (!691660 and s·P.x ≤ F0−10 and |P.z−ball.z| ≤ 18 and !68fd90 and !68ff00):
                 if cls == 0x14: STOP if sub ≠ 0x13 and |ball.z| < |P.z|; STOP if |P.z| > F1 − 7
                 if !pinned: FUN_14068ac40(FUN_140596c30 out) | 6900a0 | 693ef0 → STOP
                 if none of 68c1a0, 68d730, 68fc70, 68cde0, 68cc40, 68baa0, 6910c0 and
                    (sub ≠ 1 or s·P.x ≤ s·ball.x): KEEP (return 0)
20 otherwise STOP (fallthrough return 1, 0x14068a6xx)
```
Verified against raw (l68.txt): the 13/8 constants and arg order at 0x140689345/0x14068935d (`xmm0=[r12]`=runner x,
`xmm1=[rbx]`=ball x); −5 at 0x140689741 (ball, runner); 4.0 (xmm15) and −5 (xmm8, lineX vs ball) at 0x1406898a7/8c0;
0 (xorps xmm7) at 0x1406899a9; 130/169/6.5/1156 at 0x1406895fc–0x140689644; 45/24/80/20/18/90/15/27/−12/35 at
0x1406897ca–0x140689a4f. The remaining items are decompile-order OBS (b.c 1784–2500), not each re-walked in raw.

## 4. Pass 2 (2026-10-14): base score table, search predicates, return value

### 4.1 Writer of `MA+0x25378` — FOUND (OBS)
Every one of the 21 `FUN_140a7b2c0` callers only reads (byte-scan of `call 0x140a7b2c0` + the next instruction); the only
writer is `FUN_140462470` (store `mov [param_1 + row·0x21 + k], al`, 0x140462470–0x140462695), called by `FUN_1404623d0`,
which `0x140457620` calls right after the grid builder `FUN_140459ce0` (same update, so the table is always fresh vs the grid).
```
# occupancy grid, one per team t (MA+0x78 + t·0x1bb0), FUN_14045a080 (OBS)
grid_t: header {L, W, cw = 2L/104, ch = 2W/68}  (FUN_140a783d0; 104 = 0x1425cb308, 68 = 0x1425cf878)
        104×68 bytes at +0x10, reset to 0xff (= 100) by FUN_140a77780 (memset 0x1ba0)
        cell (i,j) centre = (−L + (i+0.5)·cw, −W + (j+0.5)·ch);  index = j·104 + i
for each active player p of team t (MI+0x1748):                         # FUN_140a77130 (OBS)
    for cells within the box around p with |cell − p|² < 16:            # 16 @0x14259be10
        v = cell (0xff → 100);  if v > 0: cell = max(0, v − (4 − |cell − p|)·100·0.25)   # 4 @0x1425656c0, 0.25 @0x142592228
# lane table row t, lane k (FUN_140462470, OBS)
X_t = MA+0x44+0x14·t  (team t's line x);  if s_t·ball.x > s_t·X_t: X_t = ball.x      # s_t = team t +0x254
z_k = W' − 2 − 2k,  k = 0..32           (W' = the 6th arg, *( *(G+0x10) + 4) [INF(med): F1, matches FUN_140a71990 lanes])
S[t][k] = mean over grid_t cells with centre within 1 m of (X_t, z_k), 0xff counted as 100; no cell → 100
          (FUN_140a77b80 disc r ∈ [0, 1], no angle filter, x clamped to [−100, 100]; FUN_140a78320 integer mean)
```
So **S = "how free of team-t players the 1 m disc on team t's line is", 0 (packed) … 100 (empty)**. The search reads row
`opp` (the defending team) at its own line. Open: `L, W` passed to `FUN_140a783d0` come from `[rbx]`,`[rbx+4]` at
0x140459e94 in `FUN_140459ce0` (INF(med): F0, F1); the radius term `|cell − p|` is the decompile's sqrt argument, not re-walked
(the raw linear sweep desyncs inside 0x140a77130 — decode next from 0x140a773xx with a proper disassembler start).
`FUN_1404621d0` (kind-7 ball event) calls `FUN_140a7b7a0/140a7bee0` on the same base; they did not show as `a7b2c0` writers
but were not decompiled (INF(med): they write the `+0x50`+ flag words of the same struct, not the 66 lane bytes).

### 4.2 Search predicates (OBS decompile p2.c; in score order; true = effect shown)
Force `S = 100` ("treat lane as open", not a veto; all need `S < 100`):
- `5a7840(C, flagA, flagB)`: flagA && !flagB, runner–C² ≤ 42.25, a4c9a0(id,−5.5), |runner.z| < F5+4, runner–ball² < 289.
- `5a6de0`: (one of 3 flags) && !flag4 && FUN_140593bd0; |P−C| ≤ 4 (6.5 with flag2); route via FUN_1405976d0/596560;
  if speed > FUN_1404c8dc0(10) need |heading − bearing(P→C)| ≤ 20; no opponent with bearing dev < 26° and |Δz| < 3, nor
  FUN_14069cd40(ball, route, P, o, 18.06 m/s) ≥ 0.
- `5a60f0`: a4c9a0(id,−5); |C.z−P.z| ≤ 5.5; if runner not in own half-region (a4f1e0): s·C.x ≤ F8, (dev(T.dir,P→C) ≤ 65 or
  |Δz| ≤ 3), instr-2 set or a4c720(line,ball,−10), angle 35° tests vs ball, |P.x−ball.x| ≤ 8 when behind, distance-rank
  < 3: dist² ≤ 144/256 (or ≤ 1024 with instr 2), C nearer than 4 nearest opponents, no a4e720(40°,15).
- `5a73c0`: the teammate nearest C (excluding opponent-receiver `FUN_140a62960`) is the runner and no other within 5 m.
- `5a65a0`: C and ball on the same z side of P, dev(T.dir, ball→C) ≥ 40, nearest teammate to C on the other side and its
  16-f predicted pos ≥ 1.5 m from C and no nearer than P, dev(T.dir, ball→mate) ≥ 32.
- `5a7960`: dev(T.dir, P→C) ≤ 65, nearest teammate q to C; per-player angle table MA+0x228d4+0x58·id+0x16a8 vs T.dir ≥ 95.
  (Decompile returns a tautology `d ≤ d` — **raw check needed**, INF: compares runner vs q distance.)
- `5a7520`: |P.x−C.x| ≤ 10, (dev ≤ 65 or |Δz| ≤ 3), |P−C|² ≤ 225; nearest teammate within 90° per table;
  FUN_140655660(id) < FUN_140655660(mate) on the FUN_140599ff0 path.
- `5a6910`: |C.z| ≤ 12, class ≥ 7 or style 9, s·C.x ≤ F6+5, |P−C|² ≤ 64, dev ≤ 35, 6 ≤ |C.z−ball.z| ≤ 18,
  a4c720(line, ball, −8), dev(T.dir, ball→C) ≥ 55, C between the nearest unmarked defenders on each side (gap ≥ 1.5, < 500).
- `5a7270`: every opponent within 4 m (16 m²) of C is a marker (state 0x36) of a valid player ≠ runner who fails a4c9a0(6.5).
- Inline: `S < 100 && T+0xb8e8 && T+0xb8d4 < 22 && |z_k − T+0xb8e4| ≤ 7` → S = 100 and the 'held' flag.

Accept/budget:
- `594470(…, out allClear, out anyHit)`: steps 0..4 × 3.5 m from C along bearing(P→C) while s·x ≤ F0; per opponent with
  bearing-from-ball dev ≤ 65, t = FUN_14069cd40(ball, step, P, o, 22.22 m/s = 80 km/h); t ≥ 0 → anyHit; t ≥ sel+0xc → keep
  stepping, else stop with allClear = 0.
- `592fd0(ctx, id, k, limit, allClear)` (OBS raw 0x14059cec6: r9 = limit byte r14b, [rsp+0x20] = allClear):
  need limit ≥ 15 (30 if `FUN_1405a5eb0` or `FUN_1405a5240`); pass early if class > 6, (!allClear or kickoff state
  MI+0x11d4 = 2), s·line.x > F6 − 5 and runner and ball both within 15 m of (line.x, z_k); else reject if a neighbour lane
  k±1 has S < limit − 15 and < 85, or (budget 2 only) k±2 has S < limit − 15 and < 70; else pass.
- `14059a1f0(id, C)` → **sec** (float 0..100, max over probe points): probe (lineX + 4s, C.z) and ±3.5 m, stepping 4 m while
  `FUN_140a4c750` (onside/in-pitch) holds, ≤ 3 steps; value = (w_z·(20r+30) + 100 − (20r+30)) · ramp(dist(ball,p), 20→60)
  +20 if |p.z| < F2+2 and s·x > F6, +20 if F2 < |p.z| < F7 and s·x > F6+4, +20 if no a4e720(60°,50) cone; clamp 0..100;
  × FUN_14059b630; r = ramp(s·ball.x, 13 → F6−10), w_z = ramp(|p.z|, F7 → F2) (1 inside the goal width, 0 outside the box
  width). Offsets OBS: `+0x08` = F2 3.66, `+0x18` = F6, `+0x1c` = F7.
- `595ab0(sec)`: pass unless instruction 0 set and 1, 2 clear; then pass if sec > 30 (15 with style 0), or
  `FUN_140a3e840` > 4·fps, or (a4c720 and P within 11 m (16 with style 0) of ball), or s·ball.x > F6 and |ball.z| < F7.
- Teammate-claim gates, used when `cnt = T+0xbab4[slot·0x21+k] < 45` (`5ab180`) or with `p8` (`5abcc0` or `5ab380` or
  `5ab990`): pass only if a Chance-running teammate (state 0x1e, ≥ 11 f in) is found that the lane outranks; each sets that
  teammate's bit in `sel+0x280` (the Chance pin mask). `5ab180`: sec ≥ 50 and mate's `T+0xc064` value ≤ min(60, sec) and
  mate's AI z within 10 m of C.z. `5abcc0`: sec ≥ 70 (or runner in 0x2f), receiver geometry 90°/100 m, mate value ≤ 55 …
  ≤ sec. `5ab380`: |C.z| ≤ F5+2, same z side as ball, s·ball.x ≥ 15, receiver→C dev ≤ 60, mate ≥ 7 m away in z.
  `5ab990`: sec ≥ 10, |P.z| ≤ F7 and |C.z| ≤ F5, mate and mate's AI target both at |z| ≥ F7.
- Tie-break (equal S, OBS 59ba60 1498–1601): `5973b0(C)` = max over opponents of min(ramp(|o−C|, 5→8.5),
  ramp(s·(line.x − o.x), 1.8→5)) ("defender close to C and on the line"); `595260(C, best)` and `5955e0(C, best)` only act
  for runs 0x1d–0x1f with AI class 1: prefer the lane closer in bearing to the 11-f predicted runner (70°, a4c720(−0.2)),
  and reject a lane that crosses ball z behind a defender within 75° (225 m² list cut).
- Flags into the search: `5a5b20` true unless style 0 set and the slot state ∈ {0x1d…0x21,0x24,0x25,0x27,0x2c} for
  ≤ 2.5·fps+0.5 = 135 f (or FUN_140a62d90 == 0x3cc → true). `5a0d20`: style 0x11 clear, last event a pass (1/2) to him
  within 1 s, ball past line or not in own region, |P−ball| ≤ 18 (14 in region). `5948d0`: FUN_140a62d90 == 0x3cc,
  T+0x8828[slot] == 2, s·P.x ≥ F6−5, |P.z| ≤ F5, FUN_1404c8e60 ≤ 64. `5a9950`: receiver valid and FUN_140a63680, s·ball.x ≤
  F6−2, class > 1 and ≠ 3. `5aaf00`: open play, carrier (or MA+0x2483c, dist ≤ 49) on team, instr 2 set, MI+0x11a0 ≥ 65 f,
  FUN_140a57800+4 ≤ 1, no Chance runner yet, carrier facing within 130° of T.dir, !a4c9a0(c,−12), carrier x ≤ F6−10 and not
  past team line +0x38.
- `5aaba0(C)`: nearest opponent within 7.5 m, runner speed ≥ 16000/(fps·3600) m/f (16 km/h) and heading within 80° of
  T.dir → builds a 3-point path (16-f pred, C, C + 30 m along it) and returns `FUN_140594a10(path, 3)` (not decoded).
- `a4d2c0(C, s)` region: 0 if |C.z| < F1/3 + 2 (central), else 2 if sign(C.z) == s else 1.
- X source: `63fbc0(opp)` = the two deepest outfield opponents by s·x (sorted by FUN_140a79640, index 1 and 2 skip the GK);
  `597ff0(p, n)` = p's position advanced along its velocity heading by n·speed (n = FUN_1405997f0 frames), or, when the
  player is controlled (a6e6b0), stepped `n/2` times through FUN_140597e90 with speed tiers 5/11/16/28 km/h·1000/(fps·3600).

### 4.3 59ba60 return (OBS raw)
`al = 1` only at 0x14059da1d after copying `best` to `out` (path from 0x14059d9f1 `[rsp+0x7f]` = found ≠ 0); `xor al, al`
at 0x14059dbbb for not-found and for an empty lane window (0x14059c8f6). Epilogue 0x14059dbbd–0x14059dc00.

### 4.4 Still undecoded (exact next step)
- `FUN_140664ee0` (final commit, ~660 lines; reads 0xc3 at 0x140664f6x): first gates are |dev| ≤ 90, |ball.z − C.z| ≤ 48,
  then ~30 sub-tests (`FUN_14066f080, 6697b0, 596470, 66cc20, 661550, 6705d0, 66af40, 5a0ec0, 66f200, 6683a0, 6693e0,
  667b90, 6680e0, 66ce40, 676210, 6749c0, 6612b0, 66bde0, 5a2590, 66d100, 668620, 643a30, 593e30, 660ea0, 660220, 65ff20,
  660cc0, 596010, 66a5a0, 66a200, 66dad0`). Next: `dec.sh 0x140664ee0` already in h2.c; decode its 0xc3 key reads first
  (that is where `checkBallDist_chance`/`checkLastLineDist_chance` likely apply).
- `FUN_140594a10`, `FUN_14059b630` (sec multiplier), `FUN_140655660`, `FUN_1405976d0/596560`, `FUN_140a4c750`, `FUN_140a79640`.
- `5a7960` tautology and `14059a1f0`'s `+0x08` field: re-walk in raw.
- Stop-test leaves: unchanged from pass 1 (all `FUN_14068xxxx`/`FUN_14069xxxx` in §3, `FUN_1405a7fe0/7be0/7d10`,
  `FUN_140596240`, `FUN_1405a4f40`, `FUN_1405a40a0`, `FUN_1405a1cc0`, `FUN_140a53460`, `FUN_1405951b0`, `FUN_140a4c9a0`).

Correction to §2: the predicates listed as "S = 100" there are **promotions** (lane counted as fully open), not vetoes; the
leftovers' "`bVar15 = 100` = vetoed" reading in `FUN_14059ed10` should be rechecked against this.

Killed hypotheses: (a) "`_good` gates the second Chance search" — killed by r12b = `FUN_14060a4d0` at 0x1406026db.
(b) "chanceScore = 40.0 float" — killed by the byte load at 0x14060291e and the header/ordering check in §0.
(c) "`F+0x04` is F0 52.56 so lanes run along x" — killed by `**F` = F0 in 0x14059f9a0; lanes are z = 32.06 … −31.94.

## 5. Pass 3 (2026-10-14): final commit `FUN_140664ee0(ctx, reg, sel, id, C, flags[10], arg)` (0x140664ee0–0x1406665c7)

Verdict: **skeleton decoded, not port-ready.** It is a gate tree of ~95 boolean leaves (1 = lane accepted); the key-dependent
part is tiny. `flags` = the 8-byte pack built in 59ba60 (0x14059da45–0x14059da71): [0] laneBudget(594470 anyHit),
[1] 594470 allClear, [2] interceptable(59ba60 pre-pass), [3] 2d4 (a4f720 ball-near-line flag), [4] 5a9950, [5] aaf00,
[6] 2-pin flag cStack_3cf (5a8df0), [7] 'held' (cStack_39e), [8] 5aa480, [9] 0 (OBS stack order; names INF(med)).

**0xc3 key use (OBS raw):** the record is fetched at 0x140664fb7 (`mov edx,0xc3; call 0x141e5d4c0`). Only two uses:
1. `cmp byte [rec+0xe8], 0` at 0x140665da2: if nonzero (and not a Chance-family run, flags[8] clear) the lane must also
   pass `FUN_140670230`. rec+0xe8 = blob 0xe0 = **1** in the 2021 dump, so the test is live; its key name is one of the
   unmapped trailing 8 words (§0).
2. passed to `FUN_140662d60` (call 0x140665d08) — whose decompile never reads that argument (INF(high): unused; raw not
   re-walked). So **no `checkBallDist_chance` / `checkLastLineDist_chance` / `checkZ` read here**; the remaining 0xc3 readers
   (0x1405aca50, 0x140605570, 0x1406071df, 0x14060acfa, 0x140612660) are where to look next.

**Context `cx = FUN_140591b70(id, C, arg)` (OBS):** `cx[0]` team, `[1]` bearing(P→C), `[2]` bearing(ball→C),
`[3]` bearing(ball→FUN_140599ff0 point), `[4]` |P−C|², `[5]` |ball−C|², `[6]` |P−ball|², `[7]` arg.

**Order (OBS decompile h2.c 362–957; inline constants raw-consistent):**
```
reject if |fold(cx.ballBearing − T.dir)| > 90  or |ball.z − C.z| > 48
require 66f080, 6697b0, 596470, 66cc20, 661550
chance = state(id) ∈ 0x1d..0x1f;  if !chance require 6705d0
require 66af40;  reject if last event kind 3 and |ball.z − C.z| ≥ 7.5
if !flags[3]: if a4fea0(opp, ball, cx.ballBearing, 100, 5.5) and cx[5] > (sel+4)² → reject; else flags3' = 1
reject if 5a0ec0(C); require 66f200, 6683a0, 6693e0, 667b90, 6680e0(chance); chance → require 66ce40
(team T+0xa508 == 2 with 6612b0 & …) else if !chance and !5a0a70 and !flags[4] and !(flags[5]&&6749c0 or flags[8])
   and instr 1,2 clear: require 66bde0
if |C.z| > F7 and 5a2590: strong = false-path skipped ...   (5a0d20 && a4c9a0(−6.5) && cx[6] ≥ cx[5] → strong)
!strong: require 66d100;  require 668620(flags[0]); box-side tests: |C.z| > F5, |C.z − ball.z| < 12 → reject if
   |P.z| < |ball.z| or |C.z| < |ball.z| or < 3.5; reject crossing behind ball with min angle > 22
require 593e30 and (chance&&660ea0 | flags[7] | 660220&&65ff20) and (a508==2 | 596010) and 66a5a0, 66a200 and
   (chance | 66dad0)
count opponents q (≠ held marker) with |P−q| ≤ |P−C|+1.5, bearing dev ≤ 19, and (!5a0d20 or |P−q|² ≥ 10.24):
   if n>0 and (!flags6&&!flags7 or n>1) and !675de0 and !676010: reject if n>1 && !flags[9]; else need the
   receiver FUN_140a62960 to pass a53da0(…, 38°, |P−C|+1.5)
then ~45 more require-leaves (a53df0 60/7 ×2, 66d250, 6711d0, 670e20, 663750, 663440, 664200, 664320, 664ca0, 663970,
   66b980, 667480, 667310, 66f450, [!chance: 662d60, 662a10], 663e70, 676530, 66f6e0, [rec+0xe8: 670230], 66fd10, 667cb0,
   chance: 661be0, 6622d0, 666b20 | else 671440, 670cb0, 670a10, 670b80, 663c50, 668d00, 66e900, 66a880, 669940, 66ca20;
   66c920, 66b6e0/66b590, 666f20, 66e130, 6665d0, 66cd50, 66e470, [!chance 6679e0; no carrier → reject if dev ≥ 65 and
   cx… ≥ 9], a72180(MA+0x24c6a), 66e5f0, 66ed80, 66b210, 6676c0, 66c7f0, 66b070; reject if |C.z| ≥ F7 and s·P.x ≥ F6−5;
   66a130, !68ac40(C,1), (chance ? 669080 : 666710), !5a1310, !5a3080(C,1,1), !674f00, !5a2190, chance extras 6615e0/6627c0,
   66d720, 66d980)
final receiver test (FUN_140a63020 receiver r, skill byte r+0x26 ∈ 0..2 → w = min(1, b/2)):
   if cx[6] > (8w+20)² and (flags[1] or cx[6] > 1024): speed ramp u from FUN_1404c8dc0 pair;
     if angle > 15u + 10 + 10w or cx[6] > (15w+22)²: return sign test of a43a0a0 (side of r's facing, +0x34 flag picks which)
   return 1
```
`FUN_140662d60` (OBS, uses cx): accepts at once with style ≠ 1 in own region or instr 2; rejects s·P.x > F6−2 with
cx[5] > 324 and both bearings within 22° of T.dir; then a 75°/F7−7/14/20 m/−7 pass-lane window, 1600/2025/2809 distance
cuts, a carrier-side cone (4 m, 140°/7, 80°/100) for the non-allClear case, and a 24(+16)-m / ramp(3→F6−10)·10+22 (≤ 42)
radius cap on cx[5]/cx[6].

### 5.1 Next steps (exact)
- Decompile the ~95 leaves in tree order: `dec.sh` the list above in batches of 25 (they are all 0x14065f…–0x140676…).
  They are mostly small "require" predicates like 662d60; expect two more passes.
- Raw re-walk of 662d60 to confirm the 0xc3 argument is unused.
- Name rec+0xe8: match the 2021 key list (11 more names than PES19) by dumping the key-name table next to `o_spaceRun.o`.
