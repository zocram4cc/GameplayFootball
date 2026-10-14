# Keeper saves and distribution — session 3 (save choosers + converter + live)

Scope: close the two §10.8 items still open after session 2: (5) the bp answer →
request-kind converter with the throw/punt choice, and (6) the save choosers
(catch vs punch vs deflect), decompiled but not reduced. RE-only, no port.
Tags: **OBS** = seen in bytes/decompile/live read at the VA; **INF(conf)** =
inferred. Base `0x140000000`; `G = *(0x1436F3DC8)`; `gs` = fps (54 live).
This file EXTENDS the earlier report; earlier sections stand.

Live: read-only `/proc/<pid>/mem` pread of the running AI-vs-AI match, no
writes, no input. Samplers in the scratch dir (`ksave.py`, `kfull.py`).

Decompiles (annotated, in the scratch dir): `gk_save.a.c`
(`FUN_142113390`, `FUN_1421139a0`, `FUN_14210be70`, `FUN_14210b890`),
`gk_leaf.a.c` (the 17 leaf evaluators), `gk_core.a.c` (`FUN_1405912d0`,
`FUN_140581d40`, `FUN_140582e10`, `FUN_140588ca0`, `FUN_14058ec10`),
`gk_sub.a.c` (reach-table and gate sub-functions), `gk_dist2.a.c` (GK think
units), `gk_conv.a.c` (throw/punt gates and execs).

## 11.1 The save-chooser cascade (OBS)

Caller `FUN_142112a50` (the in-play keeper think, §10.1 step 5) splits on the
keeper's roster id (OBS `gk_think2.a.c:315-326`):

1. id < 0x16 → `FUN_142113390` (its head scans 24 slots of the `G+0x180`
   table for his entry, OBS `gk_save.a.c:33-95`);
2. otherwise → `FUN_1421139a0` (OBS head `gk_save.a.c:294-353`).

Both write the AI-action answer to `think+8`. Answer ids seen in the
decompiles: `0x43 KP_CATCH` (OBS: §10.1 table + live catch §11.8),
`0x44` (INF(high) punch: every punch-eval arm writes it; session-2 live
saw `0x44` = PUNCH), `0x46` (INF(low) deflect-or-late-claim: written by the
catch-retry validators, the fast band and the wait-frames arm of
`FUN_140586a10`; never observed live), `0x47 KP_BLOCK`, `0x48 KP_BLOCK_LATE`
(OBS), `0x49` unknown (not in the §10.1 table), `0x4a/0x4b` punch variants
(INF(low): written only on punch-eval arms), `0x4c KP_MAKESHIFT` (OBS,
§10.1 table), `0x15 CLEAR` (outfield id, reused), `0x50 KP_SEEN_OFF`, `0x57`
seen-off variant (INF(low)).

Order inside `FUN_142113390` (OBS `gk_save.a.c:43-166`):
```
catch-eval  FUN_14058bcf0 -> 0x43 KP_CATCH (+0x1995/0x1996/0x1998)
rush-eval   FUN_140587ec0 -> 0x49 (keeper already in KP_PRESS 0x3e)
clear-eval  FUN_140585020 -> 0x15 CLEAR
block-eval  FUN_14058f600 -> 0x47 KP_BLOCK
punch-fallback: ball-y <= 0.5 -> FUN_1405879e0 -> 0x4a
                else           -> FUN_14058e680 -> 0x44
catch-retry FUN_14058bcf0 -> 0x43 (+ FUN_14058f430 / FUN_14058c870 -> 0x46/0x4a)
clear-retry FUN_140585020 -> 0x15 ; move FUN_1405863d0 -> 0x4c
late-block  FUN_14058f600 -> 0x47 ; FUN_140588b60 -> 0x48 ; FUN_140506ba0 -> 0x50
```
Order inside `FUN_1421139a0` (OBS `gk_save.a.c:426-762`): seen-off
(`0x57`, needs `MA+0x251fc[side]` and ball-y ≤ 0.2), same catch-eval →
`0x43`, claim-solver `FUN_140586a10` → `0x43/0x44/0x46`, punch-eval →
`0x44/0x4b`, deflect-eval (`FUN_14058bd90` / `FUN_14058e700` /
`FUN_14058c910` chain) → `0x46`, then the same clear/move/late-block tail.

The claim solvers `FUN_14210be70` / `FUN_14210b890` (OBS `gk_save.a.c:765+`,
`gk_think2.a.c` from session 2) compute the intercept record
(`ai+0x12d8` point, `+0x12e4` frames·gs/48, `+0x12fc/0x12fd` kind) that the
actions walk to; outcome byte `+0x35 = 1/2/3` selects which of the three
claim geometries (direct / re-predict / lofted, INF(low) naming) the chooser
consumes.

## 11.2 The 85/80/70/60 speed ladder is real — and it is a punch/deflect gate (OBS, corrects §3.4)

`FUN_140586a10` (OBS `gk_leaf.a.c:2909+`, raw-verified entry) ends:
```
v   = ball speed at frame i (FUN_140a57650 × gs·60·60/1000, ~km/h scale)
d   = keeper→ball XZ distance; r = clamp01(d/20)
c   = byte blk+0x22 / 99 (INF(med): Catching — feeds exactly this band, binding unproven)
need = gs·((reachT·2)/3) + 0.5 frames (reachT = T[0x2f].x20)
if elapsed < need (and the side/head check mismatches): store wait-frames, return 0x46 (wait)
if v < loftCap·(60 + c·(10+20r)): return 0x43   # loftCap 1.2 (far+wide) / 1.1 (narrow) / 1.0
if v >= 80 + 30c:                 return 0x46   # too fast to punch → deflect arm
return 0x44                                     # punch band
```
So: **catch when `v < loftCap·(60 + 10c + 20·c·r)`, punch in the middle
band, deflect (`0x46`) at or above `80 + 30c`.** The caller routes 0x44
into the punch-confirm arm and 0x46 into the deflect-evaluator chain (OBS
`gk_save.a.c:447-644`), and session 2 saw `0x44` live as PUNCH — so 0x46 is
the too-fast-to-punch parry (INF(low) on the name, OBS on the routing).
At `c = 0.8` the bands are catch < ~76·loftCap, punch to ~104, deflect
above — the `85/70/60` (catchable/SpeedDefaultMax/SpeedMin) and `110/80`
(punchableMax/Min) of the vanilla pesSmart table, with `frontAdjustRate
1.1` as the `loftCap` multiplier. §3.4's "no compare found" is superseded:
the compare lives in `FUN_140586a10`, which session 1 never scanned (it is
called from the packed think, not from the action execs). Constants
raw-verified in the image: 85 [0x14259be58], 100 [0x14253ee30],
80 [0x142596e6c], 30 [0x142591140], 20 [0x14259be1c], 70 [0x14259be4c],
60 [0x14259a764], 0.5 [0x142565600], 0.25 [0x142592228], 1.1 [0x1425a3590],
1.2 [0x14259d5d4], 12 [0x14259be04], 8 [0x14259bdf4].

Reach/angle pre-gates of the same function (OBS): ball height ≤
`0.01·height·0.9` scaled (`blk+0x31` via `FUN_14124dad0`), angular error
gates 30° / 12° / 8°, and the `FUN_140a6d400` reach-table rows 2/6/11 by
height fraction. Ball speed `v` comes from `FUN_140a57650` (OBS call), not
from the 54 Hz frame delta.

## 11.3 The reach test: `FUN_1405912d0` (OBS, reduced)

```
pre: traj[i+1].x must still be up-field of the goal line (else 0); ball kind
     6 (penalty) or match 1168==9 (shootout) also force 0 (no claim).
early: (gs*3 < d2) and (gs/2 < i)  OR  (2*gs < i)  -> 1 (too late / too far: punch it)
kind 0x38 (catch): gs*1.2<i -> 1 ; gs*1.0<=i and odd(i) -> 1 ;
     height/flag arms (param_9/param_7, 6 m arm) with gs*0.8 and gs/4, gs/2 gates;
     high ball (reach6 + 0.217 > y): 1 ; depth param_8>2: 1 ; ==2: arm=3
kind 0x39 (punch): gs/4<i -> 1 ; gs*1.2<i -> 1 ; gs*0.8<=i and odd(i) -> 1
else (deflect): gs*0.8<i or (gs*0.7<i and odd(i)) -> 1 ;
     kind 0x3a + keeper beyond ball-x + 2 m: arm 3.5
then arms by geometry: kind/height/angle bands select 10/5/2/4/1/0.5/3/1.5 m
     arms (OBS list in gk_core.a.c:750-877); miss every arm -> 0 (claim it).
```
Return convention: **1 = do not claim (punch/deflect through), 0 = claim
geometry holds**. Callers pass kind `0x38/0x39/0x3a` (catch/punch/deflect,
OBS at the three call sites in `FUN_14058bd90/14058e700/14058c910`).
Distances read against `MA+0x24840`/`MA+0x24844` neighbour/ball-list values
(OBS). `0x21737187 [0x1425a9bd4]` = 0.217 m post-line margin re-appears here.

## 11.4 Catch/punch/deflect leaves (OBS)

- `FUN_14058bcf0` (catch-eval): refreshes `FUN_140582e10` when
  `ai+0x1304 != clock`; returns frames `ai+0x1308/0x130c`, flags
  `+0x1310/0x1311/0x1329`. Pure reader — the decision is the `+0x1311`
  byte the solver wrote. (OBS `gk_leaf.a.c:1-21`.)
- `FUN_14058e680` (punch-eval): same refresh; returns frames `+0x1324`,
  flag `+0x1328`. (OBS `gk_leaf.a.c:22-38`.)
- `FUN_14058eb90` (second punch-eval): refreshes `FUN_14058ec10` on
  `+0x1330 != clock`; returns `+0x1334/0x1338`. Fires `0x4b` only when
  `(s16)(ai+0x133a) < 2` (OBS `gk_save.a.c:520-528`). **INF(high): the
  `+0x133a` short is the punch-urgency counter.**
- `FUN_14058bd90` (deflect-eval, OBS `gk_leaf.a.c:60-400`): ball-speed gate
  `v·3.6 < 25 or aim-error > 90°` opens the deflect arm; near-ball gate
  `gs·v <= 5`; opponent-beats-him gate (`2·gs < opp-frames` stays, else
  `uStack_306` forces the stay arm); the `45/60/110/135/150°` aim-error
  ladder selects claim class 1/2/3 (`+param_8` bands, OBS); the three
  `FUN_1405912d0` probes (class, `0x38`, plain) must all agree to claim.
- `FUN_14058e700` (high-ball punch-eval, OBS `gk_leaf.a.c:401-573`): post
  line gate (`F6+0.5/±1.6 m` window), height gate (reach-table row 2 vs
  ball-y, `1.8 m` factor), per-frame `FUN_1405912d0(...,0x39)` sweep.
- `FUN_14058c910` (late deflect-eval, OBS `gk_leaf.a.c:574-836`): box
  window (`F6+3 m`, `F7−3 m`), the `(gs·5)/8` vs frame race, then the
  `0x3c/0x3a` probe pair; success writes through `FUN_140584480`.
- `FUN_140585020` (clear-eval, OBS `gk_sub.a.c:1030+`): `1.5·gs <
  ai+0x142c` frames-to-claim fails; keeper-velocity projection `> −5 m`
  fails; ball-list head must be a team-mate (`FUN_140a627e0` side match);
  the `0x81a1` ball-state word gates the wide branch (see §11.7).
- `FUN_14058f600` (block-eval, OBS `gk_leaf.a.c:1643+`): opponent supremacy
  scan (3 nearest, `1.5 m`/`10 m` gates), then the `20 m`/`30 m` shot
  ladder; needs the Reflexes weight `w = clamp((R−40)/59)` (OBS
  `40 [0x14259be2c]`, `99 [0x1425c48cc]`, `59 [0x1425cb2fc]` — same three
  constants as §2.3's `FUN_140750b50`, confirming the ability index is GK
  Reflexes to INF(high)).
- `FUN_140588b60` / `FUN_140506ba0` (late-block / seen-off, OBS
  `gk_core.a.c:1379+`, `gk_leaf.a.c:2331+`): clock-stamped caches at
  `ai+0x133c/0x1340-56` and the `0x247d4` yaw-table branch. Not reduced
  further (single-use tails).

## 11.5 RNG in the choosers (OBS — closes rng-slots §13's keeper row)

`func_0x000141e532e0()` returns the **int game speed (54)** and
`func_0x000141e532d0()` the **float game speed (54.0)** — OBS live: the two
rip-relative targets `0x14351AE50` (dword 54) and `0x14351AE54` (float 54.0)
held constant over 5 s while the match clock advanced, and the disassembly
is `mov eax,[rip]` / `movss xmm0,[rip]` with no generator object and no
seed state. (An earlier draft of this section called the int one the match
clock — killed by the live read.) The chooser arithmetic that *looks* like
a lottery (`gs*3`, `(gs*3)>>1`, `(gs*10)/0x30`, `(gs*5)>>3`, `gs/10`,
`gs>>1`, `gs>>2` against frame counts) is deterministic frame-window
quantisation against the game speed, with odd/even-frame hysteresis
(`param_5 & 1`) — all OBS in `FUN_1405912d0`, `FUN_1421139a0`,
`FUN_14058c910`, `FUN_140584ea0`.

**No Random call (`0x140a92470/4e0/530/590`, `0x1406e6bf0`,
`0x1406ebf30`, per-clip `anim+0x19c0`) appears in any decompiled chooser,
leaf, core, or claim-solver function** (bounded OBS: full-text search over
`gk_save/leaf/core/sub.a.c`). The save choosers are deterministic given the
think state. The rng-slots §13 row "goalkeeper: catch / distribution aim —
slot 1 / per-clip" is confirmed only for the *anime-side* clip-variant
picks (`0x140955060`, `0x140955a71`, `0x1409e09f6` count 6,
`0x142114bd4` count 3 — not re-traced here); the *decision* side draws
nothing. GF consequence: keeper save/catch/punch/deflect decisions must
not consume match RNG at all.

## 11.6 The distribution converter (OBS — closes §10.8 item 5)

Think-unit answers (OBS `gk_dist2.a.c`): `GKClear → 5/6`, `GKPassShort →
2/sub 0`, `GKPassLong → 2/sub 1`, `GKDribble → 3`. The bp→request converter
itself was not found as a named function, but both ends are now OBS, so the
mapping is pinned by the request-kind gates:

- `ActionKeeperThrow::CanEnter 0x1409d0950` (OBS raw + `gk_conv.a.c:1-48`):
  `kind = FUN_140a65db0(kindByte & 0x7f)`; **3 → `[action+0x24]` via
  `FUN_140a65a00` (power + angle), 2 → `[action+0x28]` via `FUN_140a65b30`
  (angle only)**, else keep. Raw bytes confirm the `cmp eax,3` / `cmp
  eax,2` order and the two writer offsets (`+0x24`, `+0x28`).
- `ActionKeeperPuntKick::CanEnter 0x1409cef10` (OBS raw + `gk_conv.a.c:49+`):
  **byte-identical prologue and the same 3/2 switch**, writing
  `[action+0x20]` (power path) / `[action+0x28]` (angle path). So throw vs
  punt is NOT in these gates — both accept both kinds; the choice is which
  action the think answer spawns: short answer (sub 0) → `KP_THROWING`,
  long answer (sub 1) → `KP_PUNTKICK` (INF(high): the only remaining
  routing, consistent with the sub-0/sub-1 convention of every other unit).
- Throw variant `cmd+0xff0` (`FUN_1409d04a0`, OBS `gk_conv.a.c:97-278`):
  running = speed ≥ 13.5 [0x1425a1ffc] with heading within 45° of motion;
  `no power: d² ≤ 484 (22 m) → 3, 361 (19 m) < d² → 4, running → 7;
  d² > 484: running → 5 (6 if d² < 676 = 26 m), else 1`;
  `with power: d² < 100 (10 m), or < 196 (14 m) and not running → 9;
  running → 5; else 1`. INF(med) naming: 9 = roll/bowl, 3/4 = under-arm,
  1/5/6/7 = over-arm/side by run state (unchanged from §10.6.4; the raw
  `jbe/jne` chain at `0x1409d0550+` was re-checked and matches).
- Punt type `cmd+0xff4` (`FUN_1409ce630` tail, OBS `gk_conv.a.c:505-560`):
  gated on keeper ≥ 3 m from his goal line (`F0 − keeper.x ≥ 3`, OBS);
  inside 3 m the command becomes a 5 m walk-out (`cmd+0x878 = 3`, OBS).
  Without power input: `ai+0x1399 → 1`, `ai+0x139a → 3`, else **skill bit
  35 → 3**, else 0. With power: request byte 3 → 3, else **skill bit 18 →
  1**. Skill bits read as `(u64)(blk+0x40) >> bit & 1` (OBS shifts `0x23 =
  35`, `0x12 = 18`); names GK High/Low Punt INF(med) as before.
- Receiver-less punt `FUN_1409784d0` (OBS `gk_conv.a.c:587+`): starts at
  the keeper, steps `power·T[0x30].x14 + T[0x30].x10` along the team angle
  (`T+0x250`), y forced 0; capped at **50 m** (`2500 = d²`,
  [0x1425c65f8], OBS) with a 50 m re-step along the bearing.
- Target pickers (OBS `gk_dist2.a.c:358+`): `FUN_142132660` (short) scans
  the 11 mates for `hold-list` class 0xb, heading error < 90° (140° in
  possession), reach class ≥ 2, distance² in 25–1600, and the 80° lane
  test; `FUN_142132020` (long) scans for rank-byte order, the
  `dir·keeper.x` side test, the 80° lane test, list value ≤ 1225 (35 m),
  and the `FUN_14063de20` lane probe with `(6, 30, 2, 0, gs/6)`; urgent
  (15 m pressure or 3 s hold) skips the side tests.

## 11.7 The three flags (OBS progress)

- **`MA+0x251fa[side]`** (OBS, narrowed): read in `FUN_1421139a0`
  (`gk_save.a.c:393`: `*(MA+0x251fc...)` gate for the `0x57` seen-off arm),
  in `FUN_140588b60`-adjacent claim code (`gk_leaf.a.c:1992`: the
  `MA+0x251fa` byte gates the `0x15` CLEAR arm), and in `FUN_140585020`
  (`gk_sub.a.c:1151-1284`: gates the same CLEAR arm). Every read site
  pairs it with a *claim/seen-off* decision, never with a ball-in-box
  test — INF(high): it is the **"keeper may leave his line / claim"
  enable for that side**, set by the match-flow controller (writer still
  not found; bounded: not in any decompiled keeper function).
- **`team+0xb6de`** (OBS, narrowed): the session-2 "danger mode" reading is
  kept; the hold-list helpers index `bp+0xb6c` per sub-slot
  (`FUN_142118d20/142118c90`, OBS `gk_dist2.a.c:453-505`), and the
  counter-runner arm keys on the same concept region (session-2
  `AfterCatchMove` reading, unchanged).
  INF(med): `+0xb6de` is concept-slot state on the team object, not a
  tactics toggle.
- **Ball state `0x81a1`** (OBS, narrowed): `FUN_140a62d90(...) == 0x81a1`
  gates the "wide" 42 m caps (§10.2.2) AND the clear-eval's wide branch
  (`gk_sub.a.c:1190+`: `iVar8 != 0x81a1 || bVar2` selects the near/far
  claim race). It is read from the roster block `+0xe8` path (OBS), i.e. a
  **ball-flight state word** (kind/phase), not a team flag. Full enum
  still open.

## 11.8 Live (OBS)

- **Shot → save** (120 s watch, `kshort.jsonl`, clk 4549→4564): claim point
  `(52.06, 1.53, 2.33)` (1.5 m high, inside the posts), keeper `WAIT(0x11)`
  13 frames → `SAVING_MOVE(0x4d)` at 4562 → `BLOCK_LATE(0x48)` at 4563 →
  base at 4564; record point `ai+0x12d8 = (49.99, 1.54, 2.17, 0.89)`
  matches the claim within 2 m. Consistent with §10.5.1 (on-target test →
  `KP_SAVING_MOVE` → block chooser). Exercises `FUN_1421139a0`'s tail
  (`FUN_14058f600 → 0x47`, `FUN_140588b60 → 0x48`) with the record point on
  the goal line — matches the static order in §11.1 (block-eval before
  punch-eval when the catch byte is clear).
- **Catch + distribution** (background watch, `klong.jsonl` + `ktrans.jsonl`,
  ~40 min): two complete `KP_PRESS → KP_CATCH` episodes from the same keeper:
  - Ep. 1, clk 13803→13832: rush (`0x3e`, `ai+0x139b = 1`) → catch `0x43`
    at kf 30 → 29 frames of `0x43` walking the claim down
    (`(41.19, 0.52, 3.5)` → `(42.2, 0.11, 3.52)`, keeper closes
    `(44.38, 2.32)` → `(42.49, 3.62)`) → base `0x3f` at 13833 →
    after-catch `0x4e` at 13834 → carry `0x41` at 14044
    (`(38.07, 5.7)`, ~4.5 m up-field of the catch point, inside the box) →
    base `0x3f` at 14103. Aid `0x4e` = AfterCatchMove, `0x41` = the
    throw/punt carry (INF(high): position in the chain + 4.5 m up-field
    displacement matches §10.6.2's carry rule).
  - Ep. 2, clk 20937→21399: `0x3e` → set `0x4d` → base → set `0x4d` →
    one-tick `0x43` at 21007 (kf 45, claim `(47.01, 0.13, -4.72)` low
    inside the box) → `0x3e` → after-catch `0x4e` at 21044 → carry `0x40`
    at 21313 (`(38.46, -3.9)`, claim `(38.16, 1.33, -4.01)`) → base.
    The catch-eval fired for a single tick on a low ball at his feet —
    consistent with §11.2's catch band (slow ball, inside reach).
  - The `a1308` (frames-to-claim) countdown is OBS in both: 30→0 across
    ep. 1, 45→1/36→3 across ep. 2's rush, then `4294967295` (-1 = arrived)
    at the catch tick — the `FUN_140586a10` wait-frames output being
    consumed tick by tick, as §11.2 predicts.
  - No punch/deflect-band sample in either episode or anywhere in the ~40 min
    window: both claims were slow balls inside the catch band, so the chooser
    never reached the `v ≥ 80+30c` arm — consistent with, but not a test of,
    the punch/deflect boundary.
  - The throw-vs-punt kind itself (`0x40` vs `0x41` carry aids) differs
    between the episodes, confirming the think answer routes to two
    different distribution actions as §11.6 predicts; the kind byte and
    target triples were not sampled (open).
- **Distribution walk-target check**: ep. 1's carry `(38.07, 5.7)` from
  catch `(42.49, 3.62)` moves 4.4 m toward his own box line and stays
  inside it (|z| 5.7 well within F7−1); ep. 2's `(38.46, -3.9)` likewise.
  Both match §10.6.2's carry rule (min(x+4, box−1 m) in own frame, |z| ≤
  F7−1) to within the sampling jitter. The 4 m step toward the mate and
  the throw/punt release were not sampled (the keeper held the ball out
  of the 2-tick polling granularity between `0x41/0x40` and base).

## 11.9 GF counterpart (what each finding replaces)

| finding | GF code |
|---|---|
| §11.2 speed ladder (catch < ~76·loftCap, punch band, deflect ≥ ~80+30c; angle/height pre-gates) | `humanoid.cpp` deflect branch ~1156-1162 (`GetKeeperCatchSpeed_ms`: 14+20c m/s window) — PES's ladder is absolute ball speed with a punch band between catch and deflect, not a 14–34 hold window; the GF window sits *inside* PES's catch band and has no punch/deflect-above tiers |
| §11.3 reach test (frame-quantised claim/punch gates by kind) | same branch — GF has no frame-window test; the arrival gate is the contact itself |
| §11.4 leaf evaluators (opponent-beats-him, aim-error ladder, clear/block tails) | `elizacontroller.cpp` keeper branch ~495-527 (bound-for-goal + rush only) — no opponent-race or aim-error input |
| §11.5 no RNG in the choosers | `elizacontroller.cpp:198,223-289,1170,1343` (`random(...)` in keeper aim/clearance/stumble) — all of it is GF-only entropy; PES draws nothing here |
| §11.6 converter (kind 3/2 → power/angle slots; throw-variant d² ladder 100/196/361/484/676; punt-type skill bits 35/18; 50 m receiver-less cap; 3 m walk-out rule) | `elizacontroller.cpp` retainer branch ~306-355 (ShortPass always, random target) — GF has no kind switch, no variant ladder, no skill-bit punt type, no walk-out rule |
| §11.7 flags | `T+0xb6de`/`MA+0x251fa` have no GF equivalent (correctly: they are engine concept/flow state) |

## 11.10 Open (what was tried, what would close it)

1. Throw/punt kind byte and target triples at the release tick: the carry
   aids (`0x40`/`0x41`) were sampled but the `+0x20/+0x24/+0x28` request
   slots were not. A per-tick poll of the keeper's request object during
   `0x40/0x41` would close §11.6 live.
2. Punch/deflect arms live (`0x44`/`0x46`): no sample in ~40 min; both
   catches were slow balls in the catch band. Needs a fast-shot sample.
3. `MA+0x251fa` writer: searched all decompiled keeper functions; the byte
   is only ever read. A caller/cross-reference pass from the match-flow
   controller (`0x1404xxxx` band) would find it.
4. `0x81a1` full enum: only the equality test is OBS; the writer
   (`FUN_140a62d90` target table) was not read.
5. `FUN_140588ca0` / `FUN_14058ec10` internals (block-late / second-punch
   solvers): decompiled (`gk_core.a.c:1379+`, `:2817+`), not reduced —
   single-use tails, low value.
6. Defensive-GK (bit 20) live: both keepers still Offensive; caps static-only.

## 12. Live sweep (OBS unless marked; match live clk ~9000→38000, mode 1)

Samplers in the KeeperLiveRE scratch dir (`snap.py`, `kevent.py`/`k12a.jsonl`,
`kdist2.py`/`k12b.jsonl`, `kdist3.py`/`k12c.jsonl`, `benum2/3/4.py`,
`flagpoll.py`/`flag.jsonl`, `skill3.py`, `stylebit.py`). Decompiles in the
Unpacker out dir: `gk_skill.c` (`FUN_140a489f0`/`FUN_14124dad0`),
`gk_a62d90.c` (`FUN_140a62d90`), `gk_3b2a0.c` (`FUN_140a3b2a0`/`3af90`/
`141b59e20`), `gk_489d0.c` (`FUN_140a489d0`), `gk_48910.c`
(`FUN_140a48a10`), `gk_writer1404.c` (`FUN_1404ad8a1` chunk),
`gk_ac320.c` (`FUN_1404ac320`), `gk_writerfunc.c` (`FUN_1404ad860`).
MA stable at one address the whole sweep (OBS: `find_ma7` chain re-resolved
3×, same MA).

### 12.1 Punch/deflect still unobserved; BLOCK_LATE ×3 (OBS, item 2 open)

~10 min of per-tick watch (`k12b` 200 rows clk 17531→25542, `k12c` 48 rows
clk 23152→25146) caught **no `0x44`/`0x46`/`0x43`/`0x47` aid** on either
keeper — item 2 stays open (needs a fast shot; this match produced only
slow balls at the keeper). What it did catch, three times:

- Ep. A, k0, clk 17630: `0x11 → 0x48` (kf 42, `a1308 = -1`), ball
  `(-48.67, 1.04, 0.17)` ~1 m high at the keeper, record point
  `(-49.48, 1.02, -0.09)` on the goal line → base next tick.
- Ep. B, k11, clk 21453: `0x3f → 0x48` (kf 34, `a1308 = -1`), ball
  `(-48.8, 2.0, -5.29)` ~2 m high, record `(49.57, 0.92, 0.98)` → `0x11`
  at 21455 → base at 21467.
- Ep. C, k11, clk 25142→25148: full chain `0x11 → 0x4d SAVING_MOVE`
  (kf 71, ball `(-44.61, 1.23, -3.44)` rising to 1.26 m) → `0x48` at 25146
  (ball `(-46.53, 1.26, -3.6)`) → base at 25148 → `0x11` → `0x5` (aid 5,
  one tick, kf 32) → `0x15 CLEAR` at 25328 (kf 30, ball
  `(-49.15, 0.11, -0.86)` at his feet) → base at 25330.

INF(high): the `0x48` firings on balls at 1–2 m height with `a1308 = -1`
(arrived) are the late-block arm (`FUN_140588b60 → 0x48`) firing when the
catch byte is clear — consistent with §11.1's tail order and §11.8's
session-2 episode. Ep. C additionally OBS-links `0x4d → 0x48` and shows the
post-save clear chain `0x5 → 0x15` (aid 5 = the pre-clear gather one tick
before CLEAR).
A 6.5-min trap (`ktrap.jsonl`) caught one further aid: `0x50 KP_SEEN_OFF`,
k11 at clk 42266 (kf 74, `a1308 = -1`), ball `(-61.85, 0.11, -5.56)` —
behind the goal line — record point `(-52.46, 3.97, -5.25)`. OBS: `0x50`
fires on a ball already out of play (seen-off), matching §11.1's seen-off
routing (`FUN_140506ba0 → 0x50`).

### 12.2 Distribution kind/target still open (OBS, item 1 open)

No `0x40`/`0x41`/`0x4e` aid in the whole sweep — no catch completed, so no
distribution. A 6.5-min trap poller (`ktrap.py`: aids
`0x40/41/43/44/46/47/4b/4e/4c/49/4a/50/57` + wide P/A snapshot,
`ktrap.jsonl`) trapped only the `0x50` above — no `0x40`/`0x41`/`0x44`/`0x46`
in ~16 min of combined watch. Two static pins landed meanwhile:

- Skill-block chain OBS closed: punt-type bits come from
  `D = FUN_140a489f0(G,id) + 0x70` (`FUN_14124dad0`), word `D+0x40` (OBS
  `gk_skill.c`). Live: both keepers `D+0x40 = 0x840000000` → bit 35 = 1,
  bits 18/20 = 0 (OBS `skill3.py`, clk 12067). So both keepers have the
  bit-35 punt type (§11.6: no-power punt → type 3 when `ai+0x1399/0x139a`
  clear); the bit-18 (power-punt) variant is unobservable on these keepers.
- Action-object hunt failed (OBS): `P+0x5c8/0x5d8/0x5e0/0x58/0x60`
  candidates are not action pointers (`P+0x5d8 = 0x500000005`, the
  aid-adjacent dword). The `+0x20/+0x24/+0x28` request slots live on the
  action object whose address is still unknown — next step is vtable-anchored
  discovery (`ActionKeeperThrow::CanEnter 0x1409d0950` in the vtable), then
  re-run the trap.

### 12.3 `MA+0x251fa` writer narrowed, not closed (item 3 half-open)

- Static OBS (image byte-search + disassembly, no Ghidra analysis): all 28
  `0x251fa`-displacement sites in `.text` are **`cmp` reads** (keeper bands
  `0x14058xxxx`/`0x14210xxxx`/`0x14211xxxx` = the known gates; plus
  `0x1408a8aa8/a8ab4/afbd3`, `0x140932823`, `0x1409330a1`,
  `0x14097a482`, `0x1409d4d4a/d522d`, `0x1409f3e15/f7c3a/fa4b9/fb921`,
  `0x142105162/105f31/10e560/10e56a/112ea2/11480f/116888`). **No direct
  `mov` store to `[...0x251fa]` exists** — the byte is written via wider
  stores.
- The wider store is OBS: `FUN_1404ad860` (match-flow controller band,
  OBS `gk_writerfunc.c`) runs a `+0x1c` state machine (0 → probe
  `FUN_1404ae050` → 1 → probe `FUN_1404ad980` → history-push
  `FUN_1404ac320(MA+0x251f8, ctrl+0xec)` + reset → 0) and unconditionally
  block-copies scratch `+0xec..+0x13b` to `MA+0x25280..` with 8-byte
  stores; `FUN_1404ac320` (OBS `gk_ac320.c`) is a 2-deep shift-register
  over an `0x88`-byte block at `MA+0x251f8`. So the `0x251fa` value is
  produced upstream of this pipeline (the per-tick computer feeding
  `ctrl+0xec..`, likely `FUN_1404ad980`'s callees) — exact store
  instruction still unpinned.
- Live OBS (`flag.jsonl`, 70 s at ~300 Hz): `MA+0x251fa..fd` =
  `00010000` steady; byte3 (`+0x251fd`) toggled 0→1 at clk 27415, back to
  0 at clk 27517 (~102 ticks ≈ 2 s, mode 1 throughout). Byte0 = 1 most of
  the sweep (`01000000`), 0 in dead-ball phases (`00010000` at mode 3,
  clk 27152) — INF(med): byte0 = keeper-may-claim enable per §11.7,
  byte3 = a ~2 s pulse (claim-attempt window or danger flag).
- Hardware watchpoint **not armed** (OBS): `perf record -e
  mem:0x7ffffd475212:w` fails `No space left on device` — all 4 debug
  registers held by peer agents (retried 2×, `flock`-wrapped, <60 s each,
  nothing left running). Next step: retry when registers free, or trace
  `FUN_1404ad980`'s callees statically.

### 12.4 `0x81a1` enum: 22 values seen, `0x81a1` absent (OBS, item 4 open)

- Reader chain OBS (`gk_a62d90.c`, `gk_3b2a0.c`): `FUN_140a62d90(slot)` =
  `*(u16*)(entry + 0xa8 + 0x30 + 0xe8)` where `entry` comes from
  `FUN_140a3af90(R, slot)` (`R = FUN_140a489d0(G, slot>10)`; `slot%0xb`
  + `R[slot+1]`-indexed stride when `*R == (slot>10)`).
- Live (`benum4.jsonl`, 55 rows clk 27785→36180, 44 side×slot cells):
  **22 distinct values, `0x81a1` never appears; every cell constant over
  the full 8395-tick window** (zero transitions). Dominant: `0x1239`
  (12 cells) / `0x16e9` (12 cells); per-side singletons
  `0x123b/c/40/41/43/4b/c/d/e/f`, `0x16ea/b/c/d/f0/f1/f3/f5/f7/fc`.
  INF(high): the sampled field is per-player static assignment state, not
  the ball-flight word — the side/slot→entry selection is wrong somewhere
  (or `0x81a1` is transient and needs 54 Hz sampling across a possession
  change). Next step: 54 Hz full-cell sample across one
  owned→loose-ball transition + cross-check against the
  `gk_pos.a.c:1291` branch outcome. (An earlier run with a bad base
  resolve, `benum3.jsonl`, is void — `R = 0,0`, all words 0.)

### 12.5 Defensive-GK: static caps only (OBS, item 6 stays open)

Live style words (`D+0x48`, OBS `stylebit.py`, clk 22109): both keepers
`0x00080000` → **bit 19 = 1 (Offensive), bit 20 = 0**. The bit-20 reader
is OBS in three positioning functions (`gk_help.a.c:820`,
`gk_pos.a.c:1437/1720`, `gk_rush.a.c:692`): bit 20 set → reach factor
`×0.25` and lane term `0.0` (stay home); clear, with bit 19 selecting the
`×0.6/1.5 m` vs `+1.1/2.5 m` arm underneath. No Defensive keeper in this
match, so the caps remain static-only as in §11.10 item 6.

### 12.6 Second match (OBS; new MA, same keeper styles)

New match epochs clk ~31000→46000 (new MA re-resolved via the `find_ma7`
chain; G base unchanged). Both keepers again `D+0x40 = 0x840000000`
(bit 35 = 1) and `D+0x48 = 0x00080000` (bit 19 = 1 Offensive, bit 20 = 0).
Watch `k13a.jsonl` (154 rows clk 36858→42714) caught no `0x44`/`0x46`
either, but two new chains:

- **Pickup chain** (k0, twice): `0x11 → 0x5` (gather, `a1399 = 000001`
  = urgency set) → `0x4` (one tick, kf 34/24) → `0x3` → `0x16`
  (clk 38287→38358 and 39553→39624; ball within 1 m at his feet both
  times). INF(high): `0x5 → 0x4 → 0x3 → 0x16` is the collect-to-carry
  sequence (gather, pickup, rise, carry); the urgency byte `A+0x1399`
  is set through it and clears at `0x3`.
- **Catch-to-carry chain** (`kact.jsonl`, k11, MA resolve failed so ball
  null): `0x3e PRESS` (clk 42823, kf 117, `a1399 = 000001`,
  `a1308 = -1`, kp `(-49.52, -0.2)`) → **`0x43 KP_CATCH`** (42912, kf 28,
  `a1308 = 32`, kp `(-42.41, -3.56)` — 7 m out, claim walked down) →
  `0x4e AfterCatchMove` (42944, `a1308 = 1`) → **`0x40` carry** (43215,
  kf 94, kp `(-38.13, -2.7)` — ~11 m up-field of the rush start, inside
  the box). OBS: the §10.6.2 carry rule live (min(x+4, box−1 m) direction
  of travel); the think answer routed to the **short/throw carry
  (`0x40`)**, consistent with §11.6's sub-0 → `KP_THROWING` routing —
  but the kind byte and target triple still unsampled (item 1 open:
  the vtable hunt scanned `P+0x0..0x700` for the Throw/Punt vtables
  — statically confirmed at `0x142631de8`/`0x142631da0`, entry[0] =
  `ActionKeeperThrow::CanEnter 0x1409d0950` /
  `ActionKeeperPuntKick::CanEnter 0x1409cef10`, entry[3] = the variant
  fns `0x1409d04a0`/`0x1409ce630` — and found **no hit**: the action
  object is not inside the player object; next step is scanning the
  think object `A+0x0..0x400` at the `0x40` tick).
- `0x34` (k0, clk 42433, one tick `0x11 → 0x34 → 0x3f`, `a1308 = -1`,
  ball 19 m out) and `0x51` (k11, clk 40704 from `0x0`, dead ball at
  `(-10.94, 0, 20.69)`) and `0x12` goal-kick (k11, clk 37818 between two
  `0x5`s) round out the aid sightings. `0x51` after `0x0` with the ball
  dead is INF(med) a restart-positioning aid.

Match 2 ended (G null at menu, clk 46450). The `0x40` release tick (kind
byte + target) and any `0x44`/`0x46` fast-shot sample remain the two live
gaps; ready samplers: `kact.py` (extend the vtable scan to the think
object) and `kdist2.py`.

### 12.7 Third match: deflect `0x46` and punch-variant `0x4a` closed live
  (OBS, item 2 closed)

Watch `k14a.jsonl` (218 rows clk 6707→15673) caught both missing arms:

- **`0x46` deflect**, k0 at clk 15579: `0x11 → 0x46` for exactly one tick
  → base at 15580. Ball `(-39.27, 1.07, -2.99)` — 10 m out, ~1 m high;
  record point `(-49.11, 0.78, -3.27)` low at the near post; `a1308 = -1`.
  INF(high): the too-fast-to-punch parry arm (§11.2's `v ≥ 80+30c` band)
  firing and releasing in one tick — the claim solver routed to deflect
  and the ball never arrived (10 m out at sample time). One-tick
  `0x46` matches the `FUN_140586a10 → 0x46` wait/deflect output being
  consumed without a sustained action.
- **`0x4a` punch variant**, k11 at clk 13326: `0x11 → 0x4a`, single tick,
  ball `(48.59, 1.24, -2.99)` chest-high 1.3 m away, record
  `(50.39, 1.26, -2.86)` — then `0x3e` rush (8 samples clk 13358→13370,
  `a1399 = 000001`, keeper closing `(50.17, -2.14) → (50.13, -2.29)`
  while the ball rose to `(52.42, 2.17, -5.78)`). INF(high): `0x4a` is
  the punch-fallback arm of `FUN_142113390` (ball-y ≤ 0.5 branch per
  §11.1 order — here ball-y 1.24 at 1.3 m, i.e. the close-range punch
  eval `FUN_1405879e0 → 0x4a` firing before the rush re-engaged).
- Also: `0x4d SAVING_MOVE` (k0, clk 10380, `a1308 = 18` counting down,
  ball `(-44.74, 1.21, -0.69)` → base at 10383) — the claim-walk
  consuming `a1308`, as §11.8 predicts.

Item 2 is closed: `0x44` (session 2) + `0x4a` + `0x46` all observed live;
only the exact ball-speed values at the firing ticks (needs 54 Hz speed
sampling) remain unmeasured. Item 1 (kind byte + target at release)
remains the last live gap (see §12.8 for the follow-up trap result).

### 12.8 Window close-out (OBS)

Usable window: state-1 frames throughout, ~4 h of combined watch over
three matches (match 1: clk ~9000→38000; match 2: ~31000→46450;
match 3: ~6700→28000). The `k14a` watch ended at clk 15673; the kact2
trap's `0x40` at clk 17336 (carry at `(38.06, -5.14)`, second
`0x3e → 0x4e → 0x40` chain of the sweep) came after — its kind byte and
target triple are unsampled. The follow-up full-dump trap (`kact3.py`:
traps `0x40`/`0x41` entry, dumps `P+0x800` + `A+0x1600` for offline
vtable/kind-byte mining) ran 490 s with 0 traps; no new catch completed
in that span.

Final tally: item 2 (punch/deflect arms) **closed** — `0x44` (session 2),
`0x4a` (§12.7), `0x46` (§12.7) all observed live. Item 5 (block-late /
second-punch solvers) stays static-only, low value. Item 6
(Defensive-GK) stays static-only — all four keepers seen were Offensive
bit-19/bit-35. Item 3 (writer) narrowed to the `FUN_1404ad860` pipeline;
watchpoint never armed (registers held by peers all sweep). Item 4
(`0x81a1` enum) open — 22 static values, `0x81a1` absent. Item 1 (kind
byte + target at release) open — two `0x40` carries caught without the
request slots; ready sampler is `kact3.py`, plus the vtable bases
`0x142631de8` (Throw) / `0x142631da0` (Punt) for anchoring the
action-object hunt.
