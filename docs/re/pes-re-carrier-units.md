# PES 2021 ball-carrier brain, rest decoded (CarrierUnitsRE)

Read-only RE of the live image (base 0x140000000). Tags: **OBS** = raw bytes/decompile at the
stated address; **INF(conf)** = inference with confidence. Degrees throughout. Builds on the
three known reports (carrier-ai = the 25 kinds/cadence/adoption/perception; carrier-port = what
is ported; attack-decisions §2 = cross/early-cross/BackSpace/Special/through choosers, §2.7 =
shield skeleton); does not re-derive them. Scratch: the CarrierUnits scratch dir
(`carunits*.c` decompiles, `carsample.py`, `live_carriers.jsonl`: 892 carrier snapshots, 130 s,
live match, all levels 500/tier 5).

GF counterparts: `src/sim/onthepitch/pescarrier.{hpp,cpp}` (decision core) and
`src/sim/onthepitch/player/controller/elizacontroller.cpp` `GetOnTheBallCommands` (wiring:
cadence/plan/shoot/pass branches, then `AI_GetBestDribbleMovement`).

## 1. Dribble direction search `0x14212ec20` — port-ready (OBS)

Entry pre-pass (raw `0x14212ec20..0x14212ef10`): runs only if unit `+0x29c == 0`,
`*(unit+0x264) < 0x15`, owned (`ctx4+0x6c`), carrier speed (`0x140477130`) **> 15.0**
(@0x14259be0c), and `|wrap(facing−desired)| < 60.0` (@0x14259a764). Then the desired angle
(`unit+0x298`) is bent once by **2/3 of the escape angle**: predicted carrier point
(`+0x40` stepped 5.0 m @0x14259222c along desired) vs marker-predicted point (marker vel ×
frames from `141e532d0`, i.e. 54) tested by `0x140a900e0`; if the marker's path crosses, and
the bearing difference is in **(30°, 150°)** (30 @0x142591140, 150 @0x14259be68), desired +=
2·Δ/3 (`(f+f)/3`, `fVar1 = 3.0` @0x142598598), rewrapped to [0,360), flag `+0x29c = 1`.

Cost search (raw `0x14212ef11..0x14212f169`), literal from the disassembly:

```
u = clamp01((W − 0.5)·2)              # W = xmm11 = preference weight param
                                      # 0.5 @0x142565600, 1.0 @0x14256ae1c
best = +inf (0x7f7fffff @0x142565d00); answer init *(unit+0x2a0) = −1.0
for k in 0..71:                       # 72 dirs; step = 360/72 (div @0x1425cb47c = 72.0)
    dir = k·360/72, wrapped [0,360)
    cost = 0
    for j in 1..15:                   # r_j = j·2/3 m (0.666667 @0x14258d10c) → 10 m
        p = ball + r_j·dir
        edge = 100.0 (@0x14253ee30) if min(halfLen−|p.x|, halfWid−|p.z|) ≤ 0.5 (@0x142565600) else 0
        pref = 0
        if W ≠ 0:
            if r_j > ~1.19e-7 (@0x14259bdc4, i.e. always for j ≥ 1):
                pref = 0.6 (@0x14257a190)·W·(1 − r_j·sin(Δ+90°)/10)
                # sin via 0x140a92c80(Δ + 90.0 @0x14259be5c, r_j), /−10.0 (@0x14259be84:
                # negative divisor, so pref = 0.6·W·(1 − r_j·cosΔ/10)); Δ = wrap(dir−desired),
                # rewrapped ±180. 0.6·W is precomputed (mulsd/mulss @0x14212f050).
        threat = FUN_14212e490(ctx, ballpos, p, W, listptr)   # §1.1
        cost += max(edge, threat + pref)·pow(base, e), base = (10 − (r_j − 2/3))/10
        (10.0 @0x142588210, 2/3 @0x14258d10c), e = 3 + 3u in the prior report's notation —
        INF(med): the raw call @0x14212f11e carries (u·1.0+1.0) in its low dword (u·1.0
        @0x14256ae1c) but the callee's signature/encoding was not re-verified this session;
        the curve shape is the prior report's.
    if cost < best: best = cost; *(unit+0x2a0) = dir
```

Corrections to the prior prose: the preference term uses **sin(Δ+90°) = cosΔ** (OBS raw, not
cos directly); the falloff exponent encoding is INF(med) per the note in the listing above;
the edge margin is **0.5 m**
(@0x142565600, OBS — the prior report's 0.5 stands); step is 360/72 with the 72.0 divisor
immediate (OBS). Wiring: W comes from image state (carrier-ai §5.4's 0/0.3/0.5/1.0 ladder —
reconfirmed at the call sites in `0x14212fe70`); u is its linear remap, so state ladders with
W ≤ 0.5 (states 0/1 unowned) search with u = 0, i.e. exponent 3, preference halved.

### 1.1 Threat `0x14212e490` (OBS `carunits4.c:1`)

Per listed opponent entry (stride 0xC u32s, count `*list`; entry skipped if slot ≥ 0x16):
- Reach gate: `R_o` = entry field `[12]` (float); `R = max(5.0, 2·R_o)` (5.0 @0x14259222c);
  skip if `|p−o|² > R²` (`0x140a8f890` + compare).
- `o*`: if entry field `[2] ≤ 1.19e-7` (@0x14259bdc4) use o.pos; else interpolate
  o.pos→o.pred by `t = clamp01(param)` via `0x140a91120` (`t ≤ 1`, `< 0 → 0`); if
  `|o* − ball|² < 0.444444` (@0x142bf0a88 = 0.667²) push o* 2/3 m (@0x14258d10c) away from
  the ball along the bearing (`0x140474c40`).
- `d_term = max(R_o − |p − o*|, 0)/R_o`, exponent `e = 1 + w`, `w = 1` if `R_o < 1`
  (@0x14256ae1c), `(R_o − 5)·−0.25` (@0x1425c48ec) if `1 < R_o < 5`, else 0 — i.e.
  `w = clamp01((5−d)/4)` with d = carrier→o distance (OBS arithmetic; the prior report's
  (5−d)/4 stands).
- `a_o`: 1 when not owned; when owned, `γ` = wrap(bearing(p)−bearing(o)) folded ±180:
  `a = 1` if `|γ| ≤ 90°`, else linear `(L−|γ|)/(L−90)` with `L = 100 + 60·g` (100 @0x14253ee30,
  60 @0x14259a764), `g = clamp01(|facing−o−135|/−55)` (135 @0x14259be60, −55 @0x1425c6608,
  folded at 80° @0x142596e6c: `g = 1` if facing within 80° of o, → 0 at 135°). So
  L ∈ [100°, 160°] (OBS raw — the prior report's formula stands, constants confirmed).
- Keeper (`0x140a638c0(slot)` true): `×2.0` (@0x142565604).
- Sum over entries; return value is the threat (float, xmm0).

R_o itself (= `10/f` ladder, prior report) was not re-derived here; the list pointer is
`unit+0x50` (opponent list built upstream).

### 1.2 Live check (OBS)

The per-direction costs are not stored, so the search is not directly validatable live.
Indirect: adopted dribble (324/892 snapshots, sub-kinds below) runs under plans 0 (201) and 4
(111) at image states 1–3 — exactly the W > 0 states — and every adopted shot (44) sits at
sector/band (0,0). No adopted dribble contradicts the search (none points off-pitch in the
logged positions), but this is weak: INF(low) that the search aims the carrier.

GF replaces: `AI_GetBestDribbleMovement(...)` in the `GetOnTheBallCommands` tail — the whole
§1 + §1.1 (pre-bend, 72×15 loop, edge/pref/threat, exponent) ports into `pescarrier.cpp` as a
pure function of (ball pos, desired, W, opponent list, pitch); the threat helper ports with
it. Wiring needed: image state → W ladder, opponent list with R_o, desired = plan `+0xC0`.

## 2. Shield/hold branch + dribble sub-kinds (OBS `carunits1.c:204`, `:677`)

### 2.1 Sub-kind chooser `0x14212f1c0` (OBS; AttackDecisions §2.7's skeleton confirmed + filled)

Gate order (raw): skip everything (→ `0x14212fdd3`, no sub-kind change) if
`|wrap(teamAngle − facing)| ≥ 30°` or `|wrap(desired − facing)| ≥ 30°` (30 @0x142591140), or
`s·x ≤ 0` (behind halfway — `*ctx+0x14 × carrier.x ≤ 0`), or `0x14212e970` fails. Then:
- Not turning (`0x140a6e300` false) and plan ∉ {1,2,3} (`2 < plan−1`): need
  `|wrap(desired − teamAngle)| ≤ 20·100/100 + 40` — raw: `(param5·100·20)/100 + 40`
  (20 @0x14259be1c, 100 @0x14253ee30, 40 @0x14259be2c), i.e. **≤ 40° + 20°·param5** where
  param5 is the caller's pressure-ish scalar; else keep.
- Turning: need `|wrap(desired − facing)| ≤ 60°` (60 @0x14259a764), else keep.
- Box-pressure gate: `imageA+0xb54 ≤ +0xb3c` or sector `+0xb44 ≠ 0`; then opponent-pressure
  entry test (`0x14069a3e0`, `+0x10 < 0` or image-state < 2), velocity-sample heading
  (`0x140a6c6d0` with `0.6944444/fps`, 0.6944444 @0x14259bdd0), the 10 m (`@0x142588210`) /
  40° (`@0x14259be2c`) keep-vs-turn test, the `(speed ≤ 15)` (`@0x14259be0c`) gate, and the
  `0x140a4ddc0` room test (`≥ 12.0`, @0x14259be04; lookahead `1.2·dist`, @0x14259d5d4).
- Keep-vs-dribble economics (raw @`carunits1.c:526`): with `w_ang = clamp01((|Δ|−10)/40)`
  (10 @0x142588210, 40 @0x14259be34 — OBS: `(|Δ| > 10) ? (|Δ| < 60 ? (|Δ|−10)/40 : 0) : 0`),
  `w_spd = clamp01((spd−15)/30)` (15 @0x14259be0c, 30 @0x142591140, divisor −45.0?? raw
  @0x14259d668 — INF(med): 30-wide ramp; exact divisor needs one raw re-read),
  keep = `dist + 6·w_box + 6·w_plan + 5·w_shoot`-family (`fVar20·6 + dist + w·6 + w·5`,
  6 @0x14259bdec, 5 @0x14259222c, 10/50/60 weights @0x14259be10/0x14259be5c/0x14259a764 —
  OBS immediates), dribble = `50·w_face + 60·w_spd` (+0.0 base). Ball-zone test
  (`0x140698950`) adds keep bonus 10 (@0x14259be10) when set. Corridor lane
  `0x14063cd90` with the budget/width immediates; per-candidate `0x140a53df0` body-block
  loop. Output: dribble direction = face ± clamp(dribble_val·0.5 …) (0.5 @0x142565600,
  0.3 @0x142590d98 in the clamp), written to the answer; keep = no direction.


### 2.2 Shield branch `0x142130ee0` (OBS; confirms AttackDecisions §2.7)

Decision order confirmed verbatim from the decompile: (1) last-two-events duel case → plain
dribble (type 3, sub = `0x14212f1c0`, dir `+0x2b0`); (2) `unit+0x264 > 0x15` (no marked
opponent) → plain dribble; (3) marker intercept `d² > 16.0` (@0x14259be10 = 4²) → plain
dribble; (4) carrier rating `≤ 6.25` (@0x1425a9be4) → plain dribble; (5) corridor geometry
(5.0 m steps @0x14259222c, `0x140a90d30`) then **shield-feint**: if `unit+0x2b8 == 0` and
`0x14063e960(ctx, carrier, 0x25)` → **answer type 4, sub 0x11**; (6) tackle-like event or
progress < 2 → **type 3 sub 3** toward `facing + 2·corridor/3` (3.0 @0x14258c264);
(7) else **type 3 sub 3** plain. **Sub 0/1 (MOVE_STOP_FRONT/GOAL) are never stored by any
think unit** — re-confirmed over all newly decoded units: the only stored subs are 3/4/5,
type-4 0x11, and the `0x14210e880` downstream mapping that turns stored 0/1/2 into stop
answers. So shield = sub-3 dribble-in-place or the 0x11 feint; hold/stand = the keep path's
downstream mapping, not a unit answer.

GF replaces: `elizacontroller.cpp` dribble tail (`AI_GetBestDribbleMovement` call + clamp) —
the §2.1 gates + economics choose keep/turn/beat, §2.2 chooses shield-feint vs shield-dribble;
no GF shield branch exists today.

## 3. Undecoded think units, by match frequency (OBS decompiles; live frequencies from 892)

Prior live ranking (2,454 thinks): Dribble-fallback 890, Safety 329, DirectHighBall 284,
SetPlay 279, OneTwo 143, Forward 89, Clear 44, Long 31, Shoot 23, Cross 16. This session
(892 carrier snapshots, tier 5): Dribble 324, DirectHighBall 155, Safety 78, Forward 48,
Shoot 44, OneTwo 41, SetPlay 37, Cross 30, Long 27, BackSpace 20, Clear 19.

### 3.1 KeepBall/hold — there is no such unit (OBS, closes the item)

§1 of the known report stands: keep/hold/shield is the Dribble unit's answer (sub 0/1/2 via
the §2 path, sub 3 in place). No kind answers "hold". INF(high): at low tiers the T2 delay
(§5) is what makes carriers visibly hold — a found pass sits unpublished up to 1 s while the
per-frame dribble/keep answer executes.

### 3.2 Clear — kind 1, `0x142109eb0` (OBS, known trigger + added detail)

Trigger `imageA+0x31C1` (danger) → answer {type 5, a = `0x142108630(ctx, carrier)`}; usually
replaced by a pass (§4 rule 6). **Clear angle `0x142108630`** (`carunits3.c:714`, OBS
structure): builds candidate analysis (`0x14063c560`, `0x14063a290`), reads height byte
`+0x31` as `h/100` (`/100` @0x14253ee30, `×2/3` @0x14258c264), loops 11 opponent slots with
the 60° (@0x14259a764)/120° (@0x14256ae30)/180° (@0x142598580) bearing gates, the 25.0 m
(@0x14259be20) and half-length range gates, and validates each candidate corridor with
`0x14063d2f0` + `0x1421b7d40 == 0`; RNG draws via `0x141e532e0()/6` for tie-breaks. Live: 19
adopted (type 5). Port-ready except the `0x14063c560/0x14063a290/0x14063d2f0` corridor
helpers' internals (open).

### 3.3 Shot sub-kinds (OBS `carunits4.c:265`, `carunits3.c:1,397,680`)

- **wide_eval `0x142120e60`** (`carunits3.c:1`, OBS): returns 0/1 (shoot/no). Gates in order:
  keeper-valid + busy checks (`0x140a62690`, `0x140a6e3f0`/`0x140a6e320`-family); above-head +
  header-range → 0 (defer to the a=1 header path); `|aim−facing|` vs 90° (@0x14259be5c) → 0;
  pressure-entry scan (`0x14069a3e0`, counts `+0x30/+0x34`, per-opponent dist `+0x4c+k·0x1c`
  vs 6.25 @0x1425a9be4 with the min-angle race); unowned + pressured → 0; close-band path
  with the `+0xb3c/+0xb54/+0xb58/+0xb60` box floats, the 60° (@0x14259a764) / 30°
  (@0x142591140) / 80° (@0x142596e6c) / 15° (@0x14259be0c) / 25° (@0x14259be20) gates, the
  attribute term `(stat0x1a + (0x21/0x22 skills ? +15 : 0))` (15 @0x14259be0c, skills 0x21/0x22
  via `0x14063e960` — OBS skill ids), and the composite
  `w_stat < w_far·0.5 + w_near·0.5 + w_ang·margin` test (0.5 @0x142565600, margin
  @0x142598578); then the aim-vs-body `fVar32·0.5` test and the box-reach test
  (`+0xb3c > 15` @0x14259be0c → need owned and `keeper-dist² ≥ 25` @0x14259be20); every
  surviving path ends in a `0x14211faf0` lane call + the `0x140a8f820` reach test
  (`< 400`?? raw `0x1426444ec` — INF(low): units unclear, likely frames; open).
- **aim `0x14211f090`** (`carunits3.c:397`, OBS skeleton): two candidate aim points seeded
  from the goal mouth (`±(halfWidth−0.16)` (0.16 @0x1425a9bd4), height 0.75 (@0x14259bdd4),
  adjusted by `count/2 · 1` (@0x14256ae1c) and the `width/3` (@0x14258c264) pressure term);
  each candidate scores `w_lane·w_ang·w_open` with a `0x14063de20(len=dist, wid=60, h=1)`
  lane call plus an 11-opponent `0x140a90fd0` intercept race; candidates sorted by
  `0x141e53540(., 2, 0x10, ...)`; winner written as the answer's aim xyz. Constants:
  2.0 @0x142565604, 3.0 @0x14258c264, 60.0 @0x14259a764, 100.0 @0x14253ee30,
  135.0 @0x14259be60, 90.0 @0x14259be5c, 0.3 @0x142590d98, 4.0 @0x1425656c0.
  Port: structure + constants OBS; the `0x14063de20/0x140a90fd0` helpers are shared lane
  primitives (open internals, same as the port doc's ponytail).
- **lane `0x14211faf0`** (`carunits3.c:680`, OBS, tiny): bearing ball→aim (`0x140439e50`),
  dist (sqrt), single `0x14063de20(len=dist, wid=3.0 @0x14258c264, h=2.0 @0x142588210)`
  call. Returns void; caller treats the lane struct (built by `0x1421085b0` +
  `0x141587aa0` + `0x14063d2f0`, checked by `0x1421b7d40`) as pass/fail.
- **finesse_ok `0x14211fbf0` / chip_ok `0x142120bd0`** (OBS `carunits4.c:748,824`):
  finesse needs plan ∈ {2,3} or skill 7, owned, zone bits 11|12, aim within 90° and outside
  post-to-post, curl side = stronger foot (`stat 0x34`) unless `stat 0x26 == 3`; chip needs
  aim ≤ 60°, band < 2, d ≥ 10, keeper ≥ 8 m and keeper-value > 81 and > (d/2)². Both
  OBS-confirmed as gating subs 3/2 (structure matches the known report).

### 3.4 OneTwo — kind 10, `0x14212aeb0` (OBS `carunits2.c:526`, fully decoded)

```
closing = 0x140a4e720(ctx, carrier, facing, 180.0 @0x142598580, 6.0 @0x14259bdec,
                       frames = fps·0.3+0.5, 1, 0)     # opponent within 6 m in 16 frames
turnErr = wrap(plan/desired − facing)                    # ±180 fold
if (|turnErr| ≤ 90.0 (@0x14259be5c) or closing) and 0x142128c10 (one-touch ok)
   and (tgt = 0x142126a80(ctx)) != 0xff:
    answer = pass(tgt, b=0 short) with f10 = 1 (wall-pass flag)
```
Trigger: pressed (6 m/16 f) or facing upfield (±90°); score: none (first-valid from
`0x142126a80`); output: short pass + wall flag. Live: 41 adopted. Port-ready.

### 3.5 SideChange — kind 5, `0x14212b8b0` (OBS `carunits2.c:668`, fully decoded)

Plan 9?? No — raw: `plan+0xa0 == 7` (SideChange) and owned, target = `plan+0xcc` valid;
need `dist(ball→target) ≥ 20.0` (@0x14259be1c, `≥` — the `|| ==` is a float-equality
epsilon idiom) and `|passAngle − facing| ≤ 60°` (@0x14259a764, via `0x1404583b0`) →
pass(t, b=1 long). Live: never adopted in 892 (plan 7 never started — consistent).
Port-ready (3 lines).

### 3.6 GoalFront — kind 2, `0x14212aba0` (OBS `carunits2.c:464`, fully decoded)

Not `central` (`0x140699710` false); corridor straight ahead free means NO answer:
`0x14063de20(along attack angle, len 20.0 @0x14259be1c, wid 80.0 @0x142596e6c, h 1.0
@0x14256ae1c)` true → return; else `tgt = 0x1421265c0(ctx)` → pass(t, 0). (The 20/80/1.0
immediates match attack-decisions §2.3's corridor.) Live: adopted 0 in 892 (mostly non-central
carriers with the corridor free — consistent). Port-ready.

### 3.7 RespondRequest — kind 14, `0x14212b230` (OBS `carunits1.c:985`)

Set-piece mode (`MI+0x116c == 3`) with event types 1/5/6 gates on the pass-target ids
(`+0x1554/+0x173c/+0x1740` + the `0x140a6e470` busy checks), else the open-play path: scan 11
opponent slots (`0x140507740` table), keep mates with receiver status 0xb (`0x142118d20`),
in set-piece also accept statuses 3/4/6 (not 5) with `0x140479470`; collect valid
(`0x140a489f0` block alive) into the answer array; write `{type 7, a = first, b = record or
0x142124fc0(...)}`, f11/f13 flags from the `+0xc` bit0. So RespondRequest answers **type 7**
(pass-to-requester), not type 2 — the only unit answering type 7 in open play (OBS). Live:
listed 6th always; adopted 0/892 at tier 5 in open play (consistent: request table empty).
Port: trigger/scan/answer OBS; the request-table writer is open.

### 3.8 OnePattern — kind 23, `0x142133c80` (OBS `carunits1.c:1102`, tiny)

`0x142133ed0(ctx)` (pattern precondition, `carunits5.c:966` — reads team bytes + plan state;
not reduced here) then `0x142119df0(answer)` ripe-check: if unripe → `0x142133cd0` (the real
emitter, `carunits5.c:1376` — stationary-pattern pass/dribble writer; not reduced here). I.e.
OnePattern is a thin guard around `0x142133cd0`. Live: listed 1st always, adopted 0/892
(precondition false in open play). Open: `0x142133ed0/0x142133cd0` internals.

### 3.9 GKCoaching — kind 22, `0x142133950` (OBS `carunits1.c:1119`, fully decoded dispatch)

Keeper's team-table mode `T+0x6d4` (copied to `unit+0x40` if nonzero) selects: 1 → pass to the
keeper-index mate (`0x140a62690`, type 2 b=0); 2 → `0x140a40fd0` mate else `0x142125c80` else
`0x142126b70` else `0x142133790`, type 2 b=0; 3 → cross-picker `0x142125ab0` / through
`0x142127b90` / long `0x142128a20` / `0x142133600`, type 2 b=1; 4 → cut-back `0x142125140` /
`0x142133790`, type 2 b=3; 5 → shoot (type 1, aim from `0x14211f090`); 6 → clear (type 5,
angle from `0x142108630`). I.e. GKCoaching replays the outfield answers from the keeper with
the long-ball bias (b=1 default). Live: listed 2nd always, adopted 0/892 (mode 0 in open
play). Port-ready as a dispatch table; helpers shared with outfield units.

### 3.10 The rest (OBS status)

- **PassRandomTest — kind 15, `0x14212afd0`** (`carunits2.c:196`, OBS): debug unit — if
  `ctx4+0x18 ≥ 0.5` or owned: for each valid mate, score = class-base (0→60, 1→40, 2→30,
  3→−20, 4→−55.??, 5→60.?? — raw rdata) + `RangeRaw(slot,10000)/100`, collect via
  `0x14063cd70/0x14063e9d0`, commit best via `0x1421b7d40/0x14063ea30`; answer type 2 b=0.
  Never listed (OBS list builder). No port (debug).
- **PassTarget — kind 13, `0x14212b9e0`** (`carunits2.c:280`, OBS): plan 9, target ≠ carrier:
  unowned or target record invalid → type 3 sub 4 (dribble toward target); else pass(t, 0).
  Matches the known report. Port-ready.
- **PassSafety — kind 6, `0x14212b520`** (`carunits2.c:322`, OBS head + known skeleton):
  `k = (n1≠0 ? 2 : n2>1) + (facing vs plan > 135°)` (135 @0x14259be60); free lane along
  attack+180° (`0x14063de20`, 80 m, 3.0) → no answer; `i = 0x142126b70(ctx,1,1)`; `k ≥ 1` →
  `j = 0x142128490` → pass(j,3,f12) else pass(i,0). Live top-3 (78 adopted). Port-ready at
  this level; `0x142126b70` full cascade per attack-decisions §2.6.
- **GKClear/GKDribble/GKPass*/SetPlay**: not on this ticket's frequency cut (keeper answers
  0/892 outfield snapshots; SetPlay 37 adopted but set-piece-scoped). Open.

## 4. Adopter veto/override walk — full rule set (OBS `carunits5.c:1`, raw-checked)

Walk order = list order (§3 of the known report; live lists reconfirmed: Basic
`23,22,0,1,17,14,11,12,2,3,10,4[,7,6]`, plan-4 `…,3,2,4,10`, plan-5 tail, keeper `14,24`,
human `14,24`). Per kind with a ripe answer (type ≠ 0), in walk order:

1. **Kind 22 instant win** outside set pieces (`MI+0x116c ≠ 3` → return 22). (Known rule 1;
   live: never fires — GKCoaching never ripe in open play.)
2. **Kind 16 skip**: never listed → dead (known rule 8; reconfirmed — the
   `0x14210fe80` dribble-vs-pass overrides are unreachable in open play, OBS list builder +
   892 lists without 16).
3. **First-time gate** (known rule 2, raw-confirmed with polarity): if `!owned` and
   capability 0x14 off (tier < 2): need `[ballY ≥ height/100 (stat 0x31 via 0x14063d8a0) or
   (t ≠ 1 or skill 11) and t ≠ 5] and [t ≠ 2 or skill 12]`. Raw: `0x14210f31f call
   0x14063e960` (skill 11/12 checks) — polarity checked against the disassembly (skip = jump
   to next kind), not just the decompile. Live: all adopted DirectHighBall (155) and Clear
   (19) had `e4 = 0` — first-time answers adopted, consistent with tier 5 (cap on, gate off).
4. **Held-answer stickiness** (`+0xC8`, known rule 3): if set and held answer is Dribble and
   k is Dribble: keep/skip. (Still INF on the field meaning; no live sample with +0xC8 set.)
5. **Restart vetoes** (known rule 4): `MI+0x1775 ∈ {3,9,10}` skips t ∈ {2,5}; mode 2 accepts
   only t = 2. Raw immediates confirmed.
6. **Cut-back override** (known rule 5): shot (t=1) + `!close` → if kind 3 ripe, return 3.
   Live: 20 BackSpace adopted, all outside close (consistent).
7. **Clear-replaced-by-pass** (known rule 6, now fully decoded raw `carunits5.c:330`):
   forced-clear test counts opponents from the pressure table with the **90°/135° sectors**
   (90 @0x14259be5c, 135 @0x14259be60), **1.5 m / 2.0 m** distances (1.5 @0x14259bddc,
   2.0 @0x142565604), team floats `+0x2482C/+0x24830/+0x228D4/+0x2444`-family; if forced
   (counts ≥ 3/2 with the `0x140a4c820` inside-PA test deciding 2-vs-3): keep the clear;
   else walk kinds **3 → 4 → 9 → 6** (`0x14211ef80` ripe-checks in that order, raw) and
   return the first ripe one. Safety accepted only when owned or keeper within 90° of body
   (`0x140a638c0` + `0x140439fe0` bearing < 90°, raw).
8. **Pass-kind vetoes** (known rule 7 + decoded extensions): Safety dropped in close
   (`0x140699820`) and in image state 4 unless back-pass within 100° of dribble dir
   (`+0x3380`, 100 @0x14253ee30 — raw `carunits5.c:393`); Forward dropped on instant-return
   (receiver == passer < 2·54 f ago — the `0x140a3e940` event-table scan + `fps·2` window,
   raw `carunits5.c:421`); non-conforming dribble/pass kinds dropped by the `0x14063c9c0`
   (pass-class) / `0x14063c9a0` (dribble-class) filters; the `0x14063de20` corridor + lane
   battery (`carunits5.c:480`) drops answers whose lane fails, with the 3/4/6/10/0/1/18/20/21
   ripe-fallback chain (raw).
9. **Cross override** (known rule 9): skill 14 + t ∈ {2,3} from kind ∉ {7,8,23} (or 24) →
   return 7 if listed+ripe. Live: 30 Cross adopted (consistent with pin-cross carriers).
10. **Final ownership check** (known rule 10): accept if owner unknown/teammate or t = 3;
    set-piece branch (kicker test `T+0x7C`, t ∈ {1,5}, `0x14063e3a0` for passes) — raw
    `carunits5.c:120`.
11. **Arming gate `0x142111610`** (OBS `carunits5.c:613`, new detail): team caps 0x1E/0x1C/0x1D
    for kinds 17/3/6 (OBS `0x140a604d0` calls); the 11-frame (`fps·0.2+0.5`) / 22-frame
    (kick) touch window, 27/54-frame windows, 5 m ball-distance, 60° turn veto, restart-taker
    veto, nearest-opponent skip, ball-height veto — all as the known report lists
    (reconfirmed, no change).

GF replaces: `GetOnTheBallCommands`' shoot→pass→dribble order becomes the §4 walk (rules
1–10 in list order with the ripe-checks); the port doc's "dominant rule" comment is superseded —
rules 6/7/9 change outcomes (cut-back over long shots, clear→pass, cross override all observed
live: 20/19/30 adoptions).

## 5. Reaction-delay tables by difficulty tier (OBS `carunits6.c:1`, raw)

`0x142110700(BallPlayer, answerType)`:
```
if human (+0x78 ≠ 0): 0
if type ∈ {2, 5}: T = T1[tier]   @0x142bef098 = {60, 40, 30, 20, 10, 0, 0}
if type == 3:     T = T2[tier]   @0x142bef0b8 = {40, 30, 20, 10, 5, 0, 0}
else: 0
return fps · T / 60.0            # fps = 141e532e0(), 60.0 @0x14259a764
```
tier = `clamp(0..6, 0x140a60340(BallPlayer+0x60))` (OBS raw — the capability-table reader,
not a plain /100; matches tier = level/100 for the observed level 500 → tier 5 → 0/0).
Ripeness `0x142110ac0` (OBS `carunits5.c:909`): ripe at once if kind ∈ mask `0x1C40800`
(bits 11, 18, 22, 23, 24 — OBS `bt` @0x142110b09) or type ∉ {2,3,5} (shots immediate —
OBS: the `param_2 == 2/5 → T1, == 3 → T2` cascade, else 0); else need
`now − evalFrame ≥ delay`; overrides: `BallPlayer+0x8C > fps` (1 s possession → immediate);
type 3 also immediate if no answer held (`0x142119df0(+0x4204)` false) or ball within 3.0 m
(@0x14258c264) of a touch/goal line (`|x| ≥ halfLen−3` or `|z| ≥ halfWid−3`, raw
`carunits5.c:909`). In frames @54 fps: T1 = {54,36,27,18,9,0,0}, T2 = {36,27,18,9,4,0,0} —
the known report's table reconfirmed byte-for-byte from the .rdata (no change).

GF replaces: nothing yet (the engine has no difficulty tier — port doc §4). When wanted: two
7-entry tables + the ripeness predicate; tier from a new match option.

## 6. Shoot-zone Rn mismatch — SETTLED (OBS live + static)

The port's ramp `Rn = 18 + 3·clamp01((F−70)/29)` reproduces every live Rn in this session
exactly: observed Rn values {18.62, 18.72, 19.03, 19.24, 19.34, 19.86, 20.28, 20.59, 21.00}
imply Finishing {76, 77, 80, 82, 83, 88, 92, 95, 99} — all integers, 21.00 = capped 99 with
Rf = 27.0 (long-shot skill, OBS on 4 carriers). The prior 0.9 m gap (19.03 @ "71", 20.90 @
"89") was a **stale-stat read**: the samples paired the live Rn against the roster Finishing,
not the live (fatigue-adjusted) copy the zone actually reads (`0x14124dad0` block byte
`+0x1a`, decompile `carunits7.c:40`). 19.03 ⇒ live F = 80, not 71; 20.90 ⇒ live F ≈ 95.6
(≈96 after fatigue), not 89. No missing term: the builder (`carunits7.c:1`) contains exactly
the ported ramp (`(F−70)/29`, 70 @0x14259be4c, 99 @0x1425c48cc, ÷29 @0x1425a9460, ×3.0
@0x14258c264) plus the skill-39/33/8 far-range +3.0 (bit tests `>>0x27/>>0x21/>>8`, OBS raw).
**No port change needed**; the orchestrator's "re-sample with the live stat byte" check is
done in the weak sense (integer-fit proof), still open in the strict sense (per-carrier live
byte read-back was not captured — sampler lacks the PMS read).

## 7. Live summary (OBS, 892 carrier snapshots)

- Adoption ranking: Dribble-fallback 324, DirectHighBall 155 (all `e4 = 0`, first-time —
    tier 5 gate off), Safety 78, Forward 48, Shoot 44 (all sector/band (0,0)), OneTwo 41,
    SetPlay 37, Cross 30, Long 27, BackSpace 20, Clear 19 (all `e4 = 0`).
- Lists match the known builder exactly (top: Basic+tail `23,22,0,1,17,14,11,12,2,3,10,4,7,6`
    251×; Basic 217×; plan-4 `…,3,2,4,10` 105×; keeper/human `14,24` 104×).
- Plans: Basic 574, PassThrough 179 (targets 9/19/20/21), Cross 30, Long 38, CurveShot 23,
    Breakthrough 22. Image states 1/3/2/4 seen; plan angles live (e.g. 144.4° CurveShot,
    76.7° PassThrough) — the `+0xC0` desired angle the port stands in with the attack
    direction.
- Dribble search: not directly checkable (costs unstored) — indirect consistency only (§1.2).

## 8. GF replacement map (what each finding replaces)

| Finding | GF code it replaces |
|---|---|
| §1 dribble 72×15 search + §1.1 threat | `AI_GetBestDribbleMovement(...)` tail call in `GetOnTheBallCommands` → pure `pes::` search in `pescarrier.cpp` (+ tests) |
| §2.1 sub-kind gates + economics; §2.2 shield-feint/dribble | new keeper of the dribble tail (no GF counterpart; the clamp/stop stays) |
| §3.4 OneTwo, §3.5 SideChange, §3.6 GoalFront, §3.10 PassTarget | new branches between the Forward tier and the dribble fallback |
| §3.7 RespondRequest (type 7) | new branch (needs the request-table writer — open) |
| §3.2 clear angle, §3.3 wide/aim/lane/finesse/chip | `pes::ShootSubKind` extensions (gates, not the ActionShoot mapping) |
| §3.9 GKCoaching dispatch | keeper branch of `GetOnTheBallCommands` (after-catch distribution) |
| §4 adopter rules 6/7/9 (cut-back, clear→pass chain 3→4→9→6, cross override) + rule 3/8 details | the shoot→pass→dribble order becomes the full walk |
| §5 delay tables + ripeness | new (needs a difficulty-tier match option first) |
| §6 Rn | no change (port ramp confirmed) |

Open (strict): per-carrier live Finishing byte read-back; `0x14212e970` corridor internals;
`0x1421265c0/0x142126a80/0x142126b70/0x142128490/0x142124cc0/0x1421240b0/0x142124950`
helpers' full cascades; `0x142133ed0/0x142133cd0` (OnePattern path); SetPlay/GK units;
`0x14063de20/0x140a53df0/0x140a91120` lane primitives; w_spd divisor re-read (@0x14259d668);
`+0xC8/+0x58/+0x64/+0x68/+0x70/ctx4+0x18/+0x6D/+0x6E/+0x6F` field meanings; sub-5 "let it run"
`0x142131810` (matches known report, not re-read); tiers 0–4 live validation.
