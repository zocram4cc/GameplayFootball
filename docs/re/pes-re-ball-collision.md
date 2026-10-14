# PES 2021 ball vs goal frame / nets / boards / players — collision response (BallCollisionRE)

Read-only RE. OBS = observed at the named VA (decompile of PESLive `pes_live.exe` via `dec.sh`, confirmed against
capstone where stated); INF(conf) = inferred. Image base 0x140000000. Scratch: `~/ghidra-projects/scratch/BallCollisionRE/`
(`tick.a.c`, `coll.a.c`, `h.a.c`, `c3.a.c` = annotated decompiles; `callscan.py` = e8/e9 caller scan of the full image).

## 0. Headline (status: response function FOUND, literal)

1. **OBS — The whole non-ground ball response is one ordinary `.trace` function, `FUN_14086d6f0(simBall)`**
   (not protected, not in fox::ph). It is `match::BallControl` vtable 0x1425988b0 **slot 9** (0x140424c90:
   `rcx=[rcx+0x20]; test; jne 0x14086d6f0`), and slot 9 (+0x48) is what `match::PostCollisionListener`
   (vtable 0x142596de8) slot 4 `FUN_140433570` calls on every registered object after the physics step
   (`call [vtbl+0x48]` loop over the vector at listener+0x30). Static callers by e8/e9: none (OBS, `callscan.py`); the
   only entry is that conditional tail-jump. This is why earlier searches (direct calls, touch-kind string xrefs,
   `FUN_140876360`) never found it.
2. OBS — it walks the **ball's own contact list** (`*(simBall+0x2c0)` = ball `collision::Object`, list head +0x20,
   node = {+0 record*, +8 owner(+0x10 of A), +0x10 next}, writer `FUN_141b970e0`) and dispatches on the **other
   object's collision kind** (record +4). Same record layout as pes-re-collision §2.4 (+0x30 = B velocity × dt; the
   response multiplies it back by 54).
3. OBS — **There is a restitution/friction pair here** (unlike the ground integrator): normal restitution `E` and
   tangential retention `T`, per collider class (§2). The goal frame's contact normal is **recomputed analytically
   from FieldInfo** (cylinders), not taken from the mesh (§3).
4. OBS — the `0x15b227450` / `+0x1a0 & 0x4000` lead is a dead end for this question: the stub runs only for
   colliders of kind 5 (PlayerVsPlayer) whose id is 0x17/0x18 (23/24: non-player humans, INF med — pes-re-collision §5
   lists officials at 22 and 24), never for the goal/net/board objects (kinds 6/9/10, ids 28–30, §7.1). The response
   is item 1 and needs no physics-body flag.
5. **OBS — the goal net is a separate film model** (`match::GoalnetFilm`, BallControl slot 4 → `FUN_140425e20` →
   `FUN_1404aa790`): 48 "inside" / 32 "outside" triangles from exe tables scaled by FieldInfo, each adding a push
   `P/10·1.2` and a per-axis velocity scale `S` (or a hard reflection), plus its own `g/54` and a −0.7 floor bounce
   (§3.5). It writes the SIDENET/UPPERNET/BACKNET(_IN) touch kinds.
6. **Validated live, all literal** (§7): 9 single-contact events (thigh block, header, arm, crossbar, two posts, board,
   two stand hits; positions to 1 mm, spins to 0.1 rad/s, velocities to print precision for boards/stands/bodies and
   ≤ 0.09 m/s for the frame) and 25 consecutive net-capture ticks (to print precision).

---

## 1. Where it runs (dispatch; OBS)

Per 1/54 s tick:

1. `BallControl` slot 2 `FUN_140424bc0` → `FUN_140870e90` (pulls the next state from the BallInfo future buffer, which
   is produced by `FUN_140876200 → FUN_140873ee0 → FUN_140876360`, the ground/air integrator of
   pes-re-ball-contact). **`FUN_140876360` has no object branch**; `FUN_140873ee0` only steps it with the
   `BallInfoUpdate*` callbacks (vtables 0x142608ab0…0x142608bc8).
2. fox::ph step → contact callback `FUN_141b970e0` appends a record to A's list when `A.id != B.id` and bit `B.kind`
   of `mask[A.kind]` (0x142ad1ec0) is set.
3. `PostCollisionListener` slot 4 `FUN_140433570` → `BallControl` slot 9 0x140424c90 → **`FUN_14086d6f0(simBall)`**.
   It rewrites `simBall+0x64` (pos), `+0x70` (vel, m/s), `+0x7c` (raw spin = ω/492.181) and sets `+0x298 = 2`,
   `+0x294 |= 4` (response) / `|= 1` (touched) / `|= 8` (goal object), `+0x291` (object touch kind), `+0x28c`
   (player id, 0xff for objects), `+0x290` (bone). The new state persists (live §7: the following ticks advance
   from it), the same way as after a state set: the setter 0x140872240 writes the same three fields with
   `+0x298 = 3`, `+0x294 |= 1` (OBS raw). `FUN_140871da0` (start of slot 2) clears `+0x294` and `+0x291..0x292`.

Entry gates (OBS):
- `MI+0x11b0 < 20` (MI = `*(G+0x270)`) → nothing (INF: match phase not live yet).
- `simBall+0x293 != 0` → only scans 22 player slots at `simBall+0x5f8 + 0x28·i` for bit 5 of `[slot+8]` and records
  that player as toucher; no physics (INF high: ball held).

## 2. Per-contact dispatch on B.kind (OBS: jump table 0x14086f8b8 for kinds 2..16; `+0x291` writes 0x14086dd05…de30)

| B.kind | object (live id) | class | `+0x291` touch kind (enum 0x1434a5c20) |
|---|---|---|---|
| 2 | bone set to sensor by an action (mode 2) | **intended touch, no physics** (§5 end) | — |
| 4 | `Atari_Ball_Hit_*` capsule | body (§4) | — |
| 7 | ragdoll/root part (bone via table 0x142b45df0) | body (§4); records with `rec+6 == 3` dropped | — |
| 6 | id 30 | static (§3) | 5 BOARD |
| 9 | id 28 | static | 12 STAD |
| 10 | id 29 (both goals, one mesh) | static + **analytic frame** (§3.2) | 0x144038e80: 4 POLE if `|B.pos.x| > F0 + 1`; else 2 CROSSBAR if `|ball.z| < F2 − F16/2` and `contact.y > F3 − F15/2`; else 1 POST |
| 11 | id 31 ×4: capsules r 0.015, y 0…1.558 at (±52.5, ±34) | static + flag shake (§3.4) | 3 CFLAG |
| 13 | — | static | 13 CONE |
| 14 | — | static; records with `n.y ≥ 0.99999` (0x142608838) ignored; if **every** record is kind 14: `v' = 0.1·v' + 0.9·v` | 14 PANEL |
| 16 | — | static | 15 PANEL_STAND |
| 1, 3, 5, 8, 12, 15 | — | skipped | — |

Touch-tag rule (OBS raw 0x14086dad4): any record with `rec+6 ∈ {1, 2, 6}` is an **intended touch** (sets
`+0x28c/+0x290`, suppresses the velocity response). Live `rec+6` on ball contacts (§7.1): kind 2 → 1, kind 4 → 0,
kind 7 → 0/5/6. INF(med): `rec+6` is the B shape's tag from the ph contact info (`info+0x38`).
SIDENET/UPPERNET/BACKNET(_IN) (6–11) are written only by the **net** path (§3.5) through the setter 0x140872210
(OBS raw 0x140426263…0x140426316); `FUN_14086d6f0` itself writes 1–5 and 12–15.

Record selection (OBS first loop of `FUN_14086d6f0`): with `P` = BallInfo current position, `dG = min |P − rec.pt|²`
over kind-10 records (init 10000 @0x14259d634), `dO` = same over the rest (init 2500 @0x1425c65f8). If a goal record
exists and `dG ≤ dO` only kind-10 records are used; if `dG > dO` kind-10 records are dropped. Every used record needs
`(v − vB)·n ≤ 0`, with `v` = ball velocity, `vB = 54·rec+0x30`, `n = rec+0x18`.

Kicker mute (OBS 0x14086db5a, writer 0x140870ab3/0x140870aba in the kick application `FUN_140870a40`): the kick
sets `simBall+0x40 = 1`, `simBall+0x44 = kicker id` (only for ids ≤ 21, i.e. players); kinds 2/4/7 records from that
id are skipped while `+0x40 != 0`, and `FUN_14086d6f0` decrements `+0x40` once per call (raw 0x14086f674). So the
kicker's own body is ignored for exactly **one** response pass after the kick.

## 3. Static objects (boards, stands, goal, corner flags, cones, panels)

### 3.1 Coefficients (OBS)
`E0 = 0.6` (@0x14257a190) normal restitution, `T0 = 0.9` (@0x14258c260) tangential retention, for every static kind.
Blend with the recent-hit memory `b = simBall+0x50`: `E = (1 − b)·E0 + b`, `T = (1 − b)·T0 + b`.
`b` update (end of `FUN_14086d6f0`): if the call lowered the speed (`|v_out|² < |v_in|²`) `b = 1`, else
`b = max(0, b − 5/54)` (5 @0x14259222c; `FUN_141e532d0` returns 54 @0x14351ae54). A second contact within ~0.2 s is
therefore elastic and frictionless (live: `b` 1.000 → 0.259 → 0.167 between two deflections, §7.2).

### 3.2 Goal frame = analytic cylinders from FieldInfo (OBS; validated §7.2 event B)
FieldInfo (`simBall+0x540 → +0x10 → +4`, live floats): F0 52.56, F2 3.66, F3 2.44, F15 0.12 (crossbar thickness),
F16 0.12 (post thickness); F13 54.32 / F14 54.43 INF net depth. No goal dimension is an image constant.
For each kind-10 record (`c` = record point, `sx = sign(c.x)`, `sz = sign(c.z)`, `r = 0.10868593` @0x142598958):
```
P1 = ball pos, P0 = P1 − v/54                                   (the last tick's segment)
post axis (x, z) = ((F0 − F16/2)·sx, (F2 + F16/2)·sz)            = (±52.50, ±3.72)
bar  axis (x, y) = ((F0 − F16/2)·sx,  F3 + F15/2)                = (±52.50, 2.50)
R = F16/2 + r
t_post: FUN_140a907c0((P0.x,0,P0.z), (P1.x,0,P1.z), post, R); accept 0 ≤ t ≤ 1 and y(t) ≤ F3 + F16/2
t_bar : FUN_140a907c0((P0.x,P0.y,0), (P1.x,P1.y,0), (bar.x,bar.y,0), R); accept 0 ≤ t ≤ 1 and |z(t)| ≤ F2 + F16/2
earliest accepted t wins (post first, then bar)
```
`FUN_140a907c0(A, B, C, R)` (OBS raw): `a = (A.x−B.x)² + (A.z−B.z)²`, `b = (B−A)·(A−C)` (3-D), `c = |A−C|² − R²`
(3-D), `disc = b² − c·a`; `|disc| < 1.1920929e-7` → one root `−b/a`; `disc > 0` → `(−b − √disc)/a`, `(√disc − b)/a`.
**Quirk (OBS):** the crossbar call packs (x, y, 0), so its `a` omits Δy² — reproduce literally.
With the earliest `t`: `Pt = P0 + t(P1 − P0)`; axis point `C` = (post.x, Pt.y, post.z) or (bar.x, bar.y, Pt.z);
surface point `S = C + (Pt − C)·(F16/2)/R` (F16 for both); `n = normalize(S − C)` with y (post) or z (bar) zeroed.
If no `t`, or `n·(v − vB) ≥ 0`, the record's own mesh point/normal/depth are used. `+0x294 |= 8`.
Front/back split: `|ball.x| ≤ (F0 + 55.475)/2 = 54.0175` (55.475 @0x142608844) → front (frame) hit; deeper → back.

### 3.3 Normal tilt for non-body contacts (`FUN_14086fc60`, OBS)
Only when `n·v < 0`, with `w = −normalize(v − vB)`:
```
a  = acos(clamp(n·w, −1, 1))  [deg]          (57.2957764 @0x14259be40)
f  = (1 − |90 − a|/90)·c,   c = 0.1 (@0x142588200) for kinds 6, 9, 10, 11, else 1.0
a' = (1 − f)·a + 90·f
n' = rotate(w, axis = normalize(w × n), angle a')            (a' = a gives back n; checked numerically)
```
Otherwise `n' = n`. Also used for body parts other than 3/6/7/8 (§4.2), there with `c = 1`.

### 3.4 Side effects (OBS)
- Front frame hit and `MI+0x116c == 1` → `FUN_140a4c410(FUN_14042a660(simBall+0x580), id)`, id 0x13/0x14/0x15 for
  `54·3.6·|v_in|` ≤ 70 / ≤ 90 / > 90 (70 @0x14259be4c, 90 @0x14259be5c; 60·60/1000 @0x14259a764/0x14256ae34).
  INF(med): frame-hit sound/event band; the speed term's unit is not resolved.
- **Net cloth impulse**: back hit → `FUN_140a5b430(GoalPostInfo data + 0x120, ball pos, ball y, J)`,
  `J = −(v_out − v_in)·0.43·0.5` (0.43 @0x1425e5ac8). Slot = 3/2 by `x ≥ 0` (z ≥ 0) or 1/0 (z < 0). INF(high):
  feeds the rendered net cloth (GoalPostInfoRef at `simBall+0x990`).
- **Corner-flag shake**: kind 11 → same writer on the `simBall+0x968` ref (data + 0xe4), height 1.5 (@0x14259bddc),
  same `J`.

### 3.5 Goal net = "film" sheets, a separate per-tick path (OBS decompile + raw; replayed live §7.3)

The net is **not** a collision object. It is `match::GoalnetFilm` (vtable 0x1425aad90) with two shared triangle
sets, `GoalnetFilmSheafIn` (table @0x1434465f0, 48 triangles) and `GoalnetFilmSheafOut` (table @0x1434472d0,
32 triangles), built once by `FUN_1404be8c0` and owned per goal by `FUN_1404aa610(goalIdx, FieldInfo)`.

**Call chain (OBS):** `BallControl` slot 4 `FUN_140424f70` → `FUN_140424d70` → `FUN_140425e20(this)`
(`this+0x20` = simBall, `this+0x28` = net manager) → `FUN_140456290(net, state, inState)` → `FUN_1404aa790`.
If it returns true, the setter 0x140872240 writes pos/vel/spin (`+0x298 = 3`, `+0x294 |= 1`) and then 0x140872210
writes the net touch kind (`+0x298 = 2`, `+0x294 |= 4`, `+0x28c = 0xff`, `+0x291` = kind).

**In/out state `simBall+0x3c` (OBS `FUN_140425e20`):** with `P` = BallInfo position, `P0` = previous position:
`|P.x| ≤ F0` and `|P0.x| ≤ F0` → state 0 (no net). When `|P.x| > F0` and the state is 0, the crossing of
`P0→P` with the goal line `x = ±F0` (`FUN_140a90d80`, segment z ∈ [−1000, 1000]) gives `(y, z)`; state 2 ("outside")
if `|z| ≥ F2 + F16/2` or `y ≥ F3 + F15/2`, else state 1 ("inside", entered through the mouth). The state sticks until
the ball is back inside `|x| ≤ F0`.

**Geometry (OBS `FUN_1404aa610`, `FUN_1404bece0`):** `H = F9 = 2.5`, `W = F2 + 0.046875 = 3.706875`
(0.046875 @0x1425a966c), `X0 = F0 − 0.1 = 52.46` (0.1 @0x142588200), `XT = F13 = 54.32031`, `XB = F14 = 54.43359`
(cached in .data 0x14344644c…0x14344645c). Each table vertex `(X, Y, Z)` is in percent units:
```
fy = (Y + 100)/200,  fx = (X + 100)/200
y = fy·H;   z = Z/100·W;   x = ((XB − X0)(1 − fy) + (XT − X0)·fy)·fx + X0
```
Element record (0x44 B from table+0x14): 3 vertices, `P` vec3 (+0x24), `S` vec3 (+0x30), flags u32 (+0x3c); the
triangle normal `N = normalize((V1 − V0) × (V2 − V0))` (sign confirmed by the replay). Tables dumped to
`<scratch>/BallCollisionRE/net_tables.txt`. Flag bit 0x20 = "hard" sheet, 0x10 = only when `3.6·v.y ≥ −9`
(−9 @0x1425a9680); low bits 1/2/4/8 = back / −z side / +z side / roof (by the In table's layout, INF high).

**Per tick (`FUN_1404aa790`, OBS; goal at −x is handled by negating x and z of pos, vel, spin before and after):**
```
if state == 0: nothing
R = state 1: ((XT + X0)/2, H/2, 0)                                    = (53.39, 1.25, 0)
    state 2: C = (X0 − (XT − X0)/1.5, H/−1.5, 0);  R = C + normalize(P − C)·2W        (1.5 @0x14259bddc)
for each triangle of the state's table (In for 1, Out for 2) hit by the segment R→P at 0 ≤ t < 1
(FUN_140a93a70; i.e. the ball has passed through that sheet as seen from R):
    if !(flags & 0x20):  f = (N·v > 0) ? 0.25 : 1          (0.25 @0x142592228)
                         v = (v + (P/10)·1.2·f) ⊙ S          (10 @0x142588210, 1.2 @0x14259d5d4)
    else if N·v < 0 and (!(flags & 0x10) or 3.6·v.y ≥ −9):
                         v_snap = v;  d = v·P;  if d < 0: v += (−2d)·(P ⊙ S)·1.2
if ≥ 1 triangle hit and v != v_snap (v_snap = v at entry, or before the last hard sheet):
    spinRaw = (spinRaw + normalize(v × v_snap)·|v|·0.001)·0.9          (0.001 @0x1425a9668, 0.9 @0x14258c260)
v.y −= 9.80665/54                                                        (@0x1425a9678)
if P.y ≤ 0.11868593 (double @0x1425a9670) and v.y < 0: v.y *= −0.7      (@0x1425a967c)
return (≥ 1 triangle hit)
```
When it returns true the ball moves that tick by `p += v_prev/54` with **no** integrator gravity/drag (OBS live: position
and velocity reproduce to print precision, §7.3) — the net's own `g/54` is the only gravity.
Touch kind written (OBS raw 0x140426263…0x140426316, `ax = |x|`, ±F from FieldInfo, r = ball radius):
state 1: `|y| ≥ F3 − r` and `ax ≤ F13` → 10 UPPERNET_IN; else `|z| > F2 − r` and `ax ≤ F14` → 9 SIDENET_IN; else
11 BACKNET_IN. State 2: same tests on `|y − v.y|` and `|z − v.z|` (literal: velocity, not velocity·dt) → 7 UPPERNET /
6 SIDENET / 8 BACKNET.
So the "capture" is: the back sheet layers at fx = 1, 1.3, 1.6 (In #14–19) each add −0.96 m/s along x per tick
(`P = (−8, 0, 0)`) and scale `v` by (0.95, 0.98, 0.98), (0.87, 0.98, 0.99), (0.80, 0.98, 1.0); side/roof layers do
the same along their axes; the "hard" sheets (#0–13, 26/27, 34/35, 42–47) reflect with gain `2·1.2·(P ⊙ S)`; the
net's extra `g/54` plus `−0.7` floor bounce drop the ball. The visible billow is the render cloth fed by §3.4.
`match::GoalnetFilm*` is this physics (my earlier reading of it as an overlay was wrong).

## 4. Player bodies (unintended contacts)

### 4.1 Coefficients by bone (OBS byte table 0x14086f8fc + jump table 0x14086f8f4, raw 0x14086db91…dbd5)
`part` = record +5 = skeleton bone (HIP 0, THIGH_L 1, LEG_L 2, FOOT_L 3, THIGH_R 4, LEG_R 5, FOOT_R 6, BELLY 7,
CHEST 8, NECK 9, HEAD 10, SHOULDER_L 11, UPPERARM_L 12, FOREARM_L 13, HAND_L 14, SHOULDER_R 15, UPPERARM_R 16,
FOREARM_R 17, HAND_R 18).
```
s = clamp((3.6·|v_in| − 40)/20, 0, 1)                (40 @0x14259be2c, 20 @0x14259be1c: 40→60 km/h ramp)
hard {LEG_L, FOOT_L, LEG_R, FOOT_R, HEAD, UPPERARM_L, FOREARM_L, UPPERARM_R, FOREARM_R}:
      E0 = 0.45 − 0.05·s  (0.45 @0x14256b140, 0.0499999821 @0x142608834)    T0 = 0.75 (@0x14259bdd4)
all other bones (thighs, belly, chest, hands, hip, neck, shoulders):
      E0 = 0.4 (@0x14259b89c)                                               T0 = 0.7 (@0x14257a194)
```
then the §3.1 `b` blend.

### 4.2 Contact normal for bodies
- Parts 3, 6, 7, 8 (OBS 0x14086e4xx): `q = Q_slot ⊗ q_c` (`FUN_14086d210`), `Q_slot` = a unit quaternion in the
  player record `*(G + (id + 0xa5)·0x28)` (`FUN_140a48910`): part 3 → +0xfc, 6 → +0x10c, 7 → +0xdc, 8 → +0xec (live:
  unit quaternions); `q_c` = (−0.5982, 0.3764, 0.5964, 0.3805) @0x142608880 for parts 3/7/8 and
  (−0.5926, 0.3746, 0.6019, 0.3823) @0x142608890 for part 6 — the `Atari_Ball_Hit_03`/`_06` foot capsule quats.
  `FUN_14086f9b0`:
  ```
  a = rotate(q, (1,0,0));   m = normalize(n − a·(n·a))
  β = acos(clamp(n·m)) [deg];   ψ = (1 − |90 − β|/90)·(90 − β)
  n' = rotate(n, axis = normalize(n × m), angle ψ)
  ```
  INF(med): +0xfc/+0x10c are the feet's world rotations; what +0xdc/+0xec mean for belly/chest is open.
- Every other part: §3.3 with `c = 1` (validated on a thigh, §7.2).

## 5. The response itself (OBS `FUN_140875170` raw 0x140875170…0x140875541 + accumulation in `FUN_14086d6f0`)

`R(p5, p6, n, n', T, E)` with `k = 1.1` (@0x1425a3590) and incoming-spin weight 0:
```
d = p6·n
if d > 0:  rel = p5, s = 1
else:      s = 0; rel = p5 − p6; if rel·n' ≥ 0: rel = p5
u_n   = (rel·n')·n'
u     = T·(rel − u_n) − E·u_n
v_out = u + 1.1·d·s·n
w1    = s·(p6 − d·n)
ω_out = (n' × u)/(2r·1.1) + 1.1·(w1 × n)/(2r)
```
Per tick over the used records `c` (weight `w_c = (v − vB_c)·n_c ≤ 0`, `W = Σ w_c`; response only if `|W| > 1e-6`,
double @0x1425e4a28):
```
(Pv_c, Pω_c) = R(0, vB_c, n_c, n'_c, T_c, E_c)                  push by a moving body
n̄ = normalize(Σ −w_c n_c),  n̄' = normalize(Σ −w_c n'_c),  T̄ = Σ w_c T_c / W,  Ē = Σ w_c E_c / W
(V, Ω) = R(v, 0, n̄, n̄', T̄, Ē)                                  the ball's own reflection
v' = Σ w_c Pv_c / W + V
if every record is kind 14: v' = 0.1·v' + 0.9·v
if front goal-frame hit or 3.6·|v'_xz − v_xz| ≥ 10:                       (|·|² ≥ 100 @0x14253ee30)
      p = Σ w_c (S_c + n'_c·(depth_c + r)) / W         (S = record point; analytic frame: S of §3.2, depth 0)
      p.y = max(p.y, floor);  floor = r, or −2.55 (@0x142608848) when *(*(G+0x2c0)+0x1a) == 10 and |x| > 60 or |z| > 39.7
v'.y −= (9.80665/54)·b                                    (9.80665 @0x1425a9678; b before its update)
if p.y + v'.y/54 < floor: vy* = 54·(floor − p.y − v'.y/54) + v'.y;
      v'_xz = normalize(v'_xz)·√max(0, |v'|² − vy*²); v'.y = vy*            (keeps |v'|)
if b > 0 and |v'|² > |v|² and |v'|² > 69.444458 (@0x1425f1f24): v' *= |v|/|v'|     (raw 0x14086ed75…edb9)
if |v'| ≥ 19.444445 (@0x1425c48bc, 70 km/h):   v' *= 19.444445/|v'|
if |v'.y| ≥ 13.888889 (@0x14260883c, 50 km/h): v' *= 13.888889/|v'.y|
λ  = 0.24 + 0.6·clamp((3.6·|v·n̄| − 2)/4, 0, 1)           (0.24 @0x1425c4bf4, 0.6 @0x14257a190, 2 @0x142565604, 0.25 @0x142592228; raw 0x14086eef5…ef4d)
ω' = ω + Σ w_c Pω_c / W + λ·(Ω − ω)
if |ω'|/(2π) > 8 (@0x14259bdf4): ω' *= 8·2π/|ω'|                    (cap 50.27 rad/s)
raw spin = ω'·0.00203177286 (@0x14349f740 = 1/492.181)
```
If the tick holds an intended touch (kind 2, or tag `rec+6 ∈ {1,2,6}`) none of this runs: only
`simBall+0x54 = Σ w_c (S_c + n'_c(depth_c + r))/W` and the toucher id/bone are stored.

**Intended vs unintended.** Intended touches never use this physics: the action switches the touching bones to
sensors (kind 2, mode 2 via `FUN_1406f02f0`, pes-re-collision §2.5) or the shape tag is 1/2/6; the contact is only
recorded, and the new ball state comes from the clip's kick through the kick application `FUN_140870a40` (OBS raw
0x140870d8c…0x140870e4b: `+0x294 |= 0x13`, `+0x298 = 1`, or `= 2` with bit 4 when its stack flag `[rsp+0x258]` is
set; `+0x288` = kicker; plus the mute above). Live dribble touches show `+0x294 = 0x12`, `+0x298 = 1`, never a
response. Unintended contacts (kinds 4/7 with tag 0/5) get §4–§5.

## 6. Implementable now (GF port checklist)

1. Colliders: ball sphere r 0.10868593; goal frame as two analytic cylinder sets per goal from the pitch constants
   (posts at (±52.50, ±3.72), radius 0.06, up to y 2.50; crossbar axis (±52.50, 2.50), radius 0.06, |z| ≤ 3.72);
   corner-flag capsules r 0.015 from y 0 to 1.558 at (±52.5, ±34); boards/stands/panels need meshes (not decoded
   here: `hit_stand_0001.geom` etc.); body capsules from pes-re-collision §2.
2. Contact gathering as in §2 (approach test, goal-vs-other nearest selection, kicker mute, intended-touch suppression).
3. Response §5 with E/T from §3.1/§4.1, normal tilt §3.3/§4.2, the `b` memory, the snap, the floor guard, the three
   caps and the spin blend. Units: m, m/s, rad/s (raw spin = ω/492.181), Fox axes (Y up), tick 1/54 s.
4. Net §3.5: in/out state on goal-line crossing, then per tick the sheet loop over the In/Out tables. The tables are
   PES data and must not be committed. OBS disk vs live: in `PES2021.exe` the counts (48/32), transform pointer, `S` and
   flags are present in `.data`, but the vertices and `P` are **zero on disk** and are filled at start-up by the
   dynamic initializers `FUN_1401baba0` (In) and `FUN_1401bc8a0` (Out) from a few `.data` constants (±100, 0.95, …;
   rip-relative store census: 1,617 / 834 refs into the tables). A runtime port therefore has to evaluate those two
   initializers from the user's exe (e.g. emulate them like `emu.py` does) — reading the file alone is not enough.
   layout: VA 0x1434465f0 / 0x1434472d0, count at +0, transform fn at +8, 0x44-byte records from +0x14.
5. Order (INF med): integrate (pes-re-ball-contact) → ph contacts → `FUN_14086d6f0` (slot 9, after the physics step);
   the net runs from slot 4 in its own pass and, on a hit tick, replaces that tick's flight integration (§7.3).
6. Reference implementation + replays: `<scratch>/BallCollisionRE/model.py` (single-record form + net; reproduces the
   events of §7).

## 7. Live validation (read-only `/proc/<pid>/mem`, OPEN_PLAY)

Scripts: `live_objs.py` (census), `live_contacts.py` (all lists, 20 s → `lc1.txt`), `live_ball.py` (ball list +
state, 20 min → `lb1.txt`). Located via `findvt.py 0x1425988b0` (BallControl → +0x20 simBall).

### 7.1 Census (OBS)
Static colliders: id 27 kind 8, id 28 kind 9, id 29 kind 10, id 30 kind 6, id 31 kind 11 ×4 (capsules r 0.015,
segment (0,0,0)–(0,1.558,0), at (±52.5, 0, ±34)). Ball object id 26 kind 1 r 0.10868593. Ball contact tags seen in
20 s: (B.kind 2, tag 1, bones 3/6/8), (4, 0, bones 1/8), (7, 0/5/6, bones 3/5/6).

### 7.2 Events reproduced by the §5 model (pred vs obs)

| event | input | predicted | observed |
|---|---|---|---|
| A shot blocked by thigh_l (kind 4, bone 1, tag 0, b 0); v_in ≈ (26.07, 5.48, −5.53) (logged 2 ticks earlier + 2 ticks of g/drag) | n (−0.942, 0.278, 0.190), E 0.4 T 0.7, tilt c 1 | p (36.538, 0.513, 1.067), v (2.197, **13.889**, −1.374), ω (−9.39, −2.92, −47.30) | p (36.538, 0.514, 1.067), v (2.247, **13.889**, −1.415), ω (−9.47, −3.05, −47.61) — the 50 km/h vy cap is hit exactly |
| B **crossbar** (kind 10, 3 mesh records, `+0x291 = 2`, `+0x294 = 0xd`, `+0x28c = 0xff`); p_in (52.440, 2.420, 2.338), v_in (11.208, −1.804, 6.340) | analytic bar hit t = 0.533, n (−0.925, −0.380, 0), E 0.6 T 0.9, c 0.1 | p (52.345, 2.433, 2.286), v (−2.719, −7.283, 6.106), ω (−8.85, 12.86, 20.18) | p (52.345, 2.433, 2.286), v (−2.676, −7.311, 6.105), ω (−8.91, 12.83, 20.27) |
| C header (kind 4, bone 10 hard, b = 0.167 at entry, body moving) | n (0.957, 0.102, −0.270), E0 0.45 T0 0.75 blended | p (19.595, 1.675, −7.891), v (−1.783, 4.624, −3.498), ω (1.76, 12.77, 13.80) | p (19.595, 1.675, −7.892), v (−1.782, 4.624, −3.497), ω (1.76, 12.77, 13.80) |
| D upper arm (kind 7 ragdoll part, bone 12 hard, |v| 19.2 m/s → s = 1) | n (−0.805, −0.308, −0.508), E 0.40 T 0.75 | p (20.765, 0.922, −7.491), v (−7.374, 4.926, −2.250), ω (6.23, 2.50, −14.17) | p (20.765, 0.921, −7.491), v (−7.374, 4.933, −2.246), ω (6.23, 2.50, −14.19) |
| E **post** (outer face, ball beyond the line, `+0x291 = 1`) p_in (52.727, 1.715, −3.970), v_in (23.736, −1.243, −13.103) | analytic post n (−0.980, 0, −0.198); hits the 70 km/h cap | p (52.336, 1.735, −3.758), v (−7.377, −1.143, −17.955), ω (−0.11, −50.02, 4.96) | p (52.336, 1.736, −3.758), v (−7.289, −1.142, −17.990), ω (−0.11, −50.02, 4.95) |
| F **post** then goal (inner face, `+0x291 = 1`) p_in (−52.544, 0.615, −3.576), v_in (−18.672, −1.987, −11.900) | analytic post n (−0.032, 0, 1.000) | p (−52.509, 0.619, −3.552), v (−17.872, −1.846, 5.176), ω (5.78, −49.93, −0.81) | p (−52.510, 0.619, −3.552), v (−17.911, −1.846, 5.091), ω (5.77, −49.93, −0.79) |
| G **board** (kind 6, `+0x291 = 5`) | mesh n (0, 0, 1), depth 0.005 | p (49.149, 0.109, −37.789), v (0.154, 0.019, 1.688), ω (−4.21, −0.46, −0.22) | p (49.149, 0.109, −37.789), v (0.154, 0.019, 1.689), ω (−4.21, −0.46, −0.22) |
| H stand (kind 9, `+0x291 = 12`) | mesh n (0, 0, 1) | p (31.127, 0.325, −58.588), v (−2.125, −1.650, 4.166), ω (−0.59, −7.38, 1.84) | p (31.126, 0.325, −58.588), v (−2.126, −1.650, 4.166), ω (−0.60, −7.38, 1.84) |
| I stand (kind 9) | mesh n (0.984, 0, −0.180), depth 0.010 | p (−91.030, 0.119, 8.164), v (6.821, −0.280, 0.434), ω (1.93, −5.49, 5.34) | p (−91.029, 0.118, 8.164), v (6.821, −0.280, 0.430), ω (1.93, −5.48, 5.34) |

Each response tick shows `+0x298 = 2`, `+0x294` bit 2, `b = 1`, then `b` decays 1 → 0.259 → 0.167 over the next
ticks (−5/54 per tick, OBS lb1.txt). The frame events' 0.04–0.09 m/s velocity residuals equal a 0.15° normal error,
which is what the logged position's 1 mm print rounding gives for the analytic normal (OBS: a free normal fit lands
0.15° away); positions and spins are exact. A foot (bone 6) deflection was also logged (lb1.txt t = 3.066) but not
replayed: its normal needs the per-player quaternion `Q_slot` (§4.2), not captured at that tick.

### 7.3 Net capture replayed (lb1.txt t = 463.585…464.041, event F's ball after it went in; OBS)
The ball got 27 consecutive net ticks (`+0x291 = 11` BACKNET_IN, `+0x294 = 4`, state 1). Model: `p_{k+1} = p_k + v_k/54`,
`(v, ω)_{k+1}` = §3.5 at `p_{k+1}`. Max error over the 25 airborne ticks: position 0.002 m, velocity 0.001 m/s,
spin 0.009 rad/s (print precision), e.g. v (−15.778, −2.827, 4.185) → pred (−11.412, −2.897, 4.060) = obs
(−11.412, −2.897, 4.060) through back sheets #15 and #17. The 2 ticks where the ball touched the ground
(t 463.655, 463.936) differ (0.48 m/s, 16 rad/s): there the ground contact of pes-re-ball-contact also acts (not
replayed). Replay code: `model.py` (frame/body/static) and the net replay in this report's §3.5 pseudocode.

## 8. Open items (with what was tried)

1. **Net state 2 ("Out" table)** — decoded statically, not observed live (no ball hit the outside of a net during
   the 20 min). State 1 is fully replayed (§7.3).
2. Board/stand/panel/cone geometry (`goal_hit.geom`, `hit_stand_0001.geom`, …) not decoded; the response for them is
   fully specified (§3), only the mesh normals are missing.
3. Player record quaternions `+0xdc/+0xec` (used for belly/chest normals with the foot capsule constant): meaning open;
   `+0xfc/+0x10c` INF feet. Next: compare with the AnimePlayer bone world rotations at the same tick.
4. `rec+6` tag semantics (INF: ph shape tag); the frame-hit event id unit (§3.4).

## 9. Net tables at runtime: portable rebuild recipe

### 9.1 What is static on disk and what is computed (OBS)

File bytes of the user's exe vs the live dump at the two tables:

- Layout (confirmed live): VA `0x1434465f0` (In, count 48 at +0, transform fn `0x1404bece0` at +8, records from
  +0x14) and `0x1434472d0` (Out, count 32, same fn, records from +0x14). Record `0x44` B: verts +0x00–0x23
  (9 floats), `P` +0x24–0x2B (3 floats), `S` +0x30–0x3B (3 floats), flags u32 +0x3C, pad u32 +0x40.
- OBS — `S` and flags of **all 80 records are byte-identical on disk and live** (0/80 mismatches): static
  `.data`, readable from the user's exe file at load time (`.data` section RVA `0x339b000`, raw `0x339a200`;
  file offset = raw + (VA − base − RVA)).
- OBS — verts are **zero on disk, filled at start-up** (all 80 records' +0x00–0x23 differ disk-vs-live); `P`
  is split (next bullet). Writers are the CRT dynamic initializers `FUN_1401baba0` (In, body
  `0x1401baba0–0x1401bc89d`) and `FUN_1401bc8a0` (Out, body `0x1401bc8a0–0x1401bdca3`), decompiled with `dec.sh`.
- Refinement of the earlier §6 item 4 (OBS): not all of `P` is computed. **Out `P` (all 32) is static on disk**
  (e.g. rec0 `(6,0,0)`, rec12 `(3,0,10)`, rec24 `(1.5,2,0)`, rec28 `(4,4,0)` — identical live). **In `P` is
  split**: recs 14–19 (back, `(−8,0,0)`), 20–25 (side, `(0,0,∓15)`), 36–41 (roof, `(0,−8,0)`) are static; recs
  0–9, 26–27, 34–35, 42–47 (the hard sheets) are computed (zero on disk). GF reads Out `P` + In soft-sheet `P`
  from the file and computes the rest.

### 9.2 Closed formula (prefer over emulation; OBS decompile)

Every computed float is `base × K`, a copy, or (Out only) a midpoint `(a+b)/2 × K`:

- Bases are `.data` ±100 constants: In inputs `0x143446570–0x1434465ec` (32 floats, immediately before the In
  table); Out reuses the same In-region bases plus its own scratch.
- Multipliers are `.rdata` floats (read them from the user's exe too):
  - In verts: `0.5 (@0x142565600)`, `2.0 (@0x142565604)`, `0.8 (@0x142572348)`, `0.6 (@0x14257a190)`,
    `−0.5 (@0x14258d694)`, `1.3 (@0x14259bdd8)`, `1.5 (@0x14259bddc)`, `1.8 (@0x14259bde0)`, `6.0 (@0x14259bdec)`,
    `1.2 (@0x14259d5d4)`, `1.4 (@0x14259d5d8)`, `1.1 (@0x1425a3590)`, `1.6 (@0x1425ab12c)`,
    `2.2 (@0x1425ab130)`, `2.3 (@0x1425ab134)`, `2.6 (@0x1425ab138)`. Coverage OBS: all 432 vertex slots
    (48 × 9) are written by `FUN_1401baba0`.
  - In hard-sheet `P` (20 recs: 0–9, 26–27, 34–35, 42–47): unit-ish vectors over `sqrt(1.25) (@0x1425ab128)`
    or `sqrt(2) (@0x142565604)`. E.g. rec0 `P = (−0.5, 1, 0)/sqrt(1.25) = (−0.4472, 0.8944, 0)`; rec2
    `(0, 1, −0.5)/sqrt(1.25)`; rec6 `(−1, 0, −1)/sqrt(2)`. Decompiler note: the `CONCAT44(hi,lo)` store puts
    the *second* argument at the lower address (`[a]=lo, [a+4]=hi`). The `sqrt == 0` branches never take.
  - Out verts: all 288 slots (32 × 9) written by `FUN_1401bc8a0`; `.rdata` multipliers `0.5`, `0.8`, `0.9
    (@0x14258c260)`, `0.7 (@0x14257a194)`, `0.85 (@0x1425ab120)`, `0.95 (@0x1425ab124)`, plus midpoint forms
    `(a+b)/2 × K` where `a,b` are earlier table slots used as scratch (e.g. the slot holding 72 from two
    slots holding 90). One extra store `0x143447b64 = 0` (scratch terminator, not a record).
- World transform at load (OBS `FUN_1404bece0`, `FUN_1404aa610`): with FieldInfo `F0,F2,F9,F13,F14` (live
  `52.56, 3.66, 2.5, 54.32031, 54.43359`; cached at `.data 0x14344644c–0x14344645c` as `W 3.706875 =
  F2+0.046875 (@0x1425a966c)`, `H 2.5`, `X0 52.46 = F0−0.1 (@0x142588200)`, `XT F13`, `XB F14`):
  `fy=(Y+100)/200, fx=(X+100)/200; y=fy·H; z=Z/100·W; x=((XB−X0)(1−fy)+(XT−X0)·fy)·fx+X0`.
- Verification (OBS, read-only pread of the live match): live In rec0 verts `(120,−100,−600,…)` + `P`
  `(−0.4472,0.8944,0)` + `S (0.5,0.5,0.5)` + flags `0x31`; Out rec0 `(100,−100,−100,…)` + `P (6,0,0)`; header
  counts 48/32 and fn `0x1404bece0` — all equal the formula outputs and the earlier `net_tables.txt` dump.
  GF rebuild = read bases/multipliers/`S`/flags/static-`P` from the user's exe, evaluate the products, apply
  the transform. No PES bytes committed. Emulation (`emu.py` on the two initializers) is the fallback.

### 9.3 GF counterpart

Replaces `ApplyGoalNettingCollision` (`ballphysics.cpp`, a spring clamp PES never does) with the §3.5 sheet
loop over exe-loaded tables + the `FUN_1404bece0` world transform. The goal-frame part (`pesballcollision.*`)
is already ported and unchanged.

## 10. Net response per tick: port checklist

OBS `FUN_1404aa790` (re-read against the earlier §3.5; the loop stands, additions only):

1. Mirror: goal at −x negates x and z of pos/vel/spin on entry and exit (literal).
2. Reference point `R` (OBS `FUN_1404aa3d0`): state 1 `((XT+X0)/2, H/2, 0)`; state 2 `C + normalize(P−C)·2W` with
   `C = (X0 − (XT−X0)/1.5, H/−1.5, 0)` (`1.5 @0x14259bddc`, `W` = cached `0x14344644c`).
3. Hit collection (OBS `FUN_1404beda0` + `FUN_140a93a70`): test **every** triangle of the state's table for the
   segment `R→P`; keep those with `0 ≤ t < 1` (strict: `t == 1` excluded).
4. Soft sheets (`!(flags & 0x20)`, OBS order): `f = (N·v > 0) ? 0.25 (@0x142592228) : 1`;
   `v = (v + (P/10)·1.2·f) ⊙ S` (`10 @0x142588210`, `1.2 @0x14259d5d4`); the `1.0 (@0x143446460)` factor is
   literal. Then `v = FUN_140a8f8b0(v, R, Ŝ)` is applied (OBS decompile: component-wise product
   `v[i] = v[i]·Ŝ[i]` with the per-sheet world-space direction; the earlier replay without this step already
   matches to print precision, so it is a no-op on the replayed ticks — INF(high): axis-rescale guard).
5. Hard sheets (`flags & 0x20`, OBS): only if `N·v < 0` **and** (`!(flags & 0x10)` or `3.6·v.y ≥ −9
   (@0x1425a9680)`); `v_snap = v` saved *before each hard sheet*; `d = v·P`; if `d < 0`: `v += (−2d)·(P ⊙ S)·1.2`.
6. Spin (OBS): if ≥1 hit and `v != v_snap`: `spinRaw = (spinRaw + normalize(v × v_snap)·|v|·0.001)·0.9`
   (`0.001 @0x1425a9668`, `0.9 @0x14258c260`).
7. Gravity + floor (OBS): `v.y −= 9.80665/54 (@0x1425a9678)`; if `P.y ≤ 0.11868593` (double `@0x1425a9670`) and
   `v.y < 0`: `v.y *= −0.7 (@0x1425a967c)`. Return value = (≥1 triangle hit).
8. Flag-bit correction to the earlier §3.5 (OBS live tables): low bits are `1` back / `2` −z side / `4` +z side
   / `8` roof (the `0x10` gate above is separate). State 2 (Out table) flags use only low bits (no `0x20`/`0x10`):
   all 32 Out sheets are soft.
9. Net state `simBall+0x3c` (OBS live: 0 in open play) and the setter path are unchanged from the earlier §3.5.
   GF counterpart: same as §9.3 — the whole `FUN_1404aa790` replaces `ApplyGoalNettingCollision`.

## 11. Player-body contact normals

### 11.1 `Q_slot` source and runtime readability (OBS)

- `FUN_140a48910(G, id)` (decompiled): `id < 0x1a` → `*(G + (id + 0xa5)·0x28)`, else a fallback record. The
  body-normal branch reads a unit quaternion `Q_slot` at slot `+0xfc` (part 3 FOOT_L), `+0x10c` (part 6
  FOOT_R), `+0xdc` (part 7 BELLY), `+0xec` (part 8 CHEST) and multiplies `q = Q_slot ⊗ q_c` (`FUN_14086d210`,
  quaternion multiply — decompile OBS, form-checked against the Hamilton product).
- `q_c` (OBS `.rdata`): parts 3/7/8 `Q_03 = (−0.5982, 0.3764, 0.5964, 0.3805) @0x142608880`; part 6
  `Q_06 = (−0.5926, 0.3746, 0.6019, 0.3823) @0x142608890`. These are exactly the `Atari_Ball_Hit_03/_06` foot
  capsule quats in the pes-re-collision capsule table (bone-frame orientations).
- OBS live (read-only, `MI = *(G+0x270)` polled until non-null — it is null most of each tick): all four slots
  hold live unit quaternions differing per player and slot (e.g. id 0: `+0xdc (−0.1222,−0.3342,0.3554,0.8643)`,
  `+0xec (−0.1103,−0.3703,0.2539,0.8867)`, `+0xfc (−0.08982,0.1999,0.2698,−0.9376)`,
  `+0x10c (0.145,−0.5211,−0.3068,0.7831)`, all `|q| = 1.0000`).
- INF(high): `Q_slot` is live orientation state (rotates the bone-frame capsule quat into the world frame).
  Readable yes, at `human(id)+0xdc/0xec/0xfc/0x10c` while `MI` is up; which bone/object it is copied from is
  **open** (resolves the earlier §8 item 3 for readability, not source).
- Rest of the normal (OBS `FUN_14086f9b0`, decompiled): `a = rotate(q,(1,0,0))`; `m = normalize(n − a(n·a))`;
  `β = acos(clamp(n·m))` in degrees (`57.2957764 @0x14259be40`); `ψ = (1 − |90−β|/90)·(90−β)` — the `c = 0.1`
  static factor of §3.3 is **absent** here (the `0.5/π` factor is the deg→rad/2 of the final rotate).

### 11.2 Capsule → GF geometry map (OBS pes-re-collision §2.1 + earlier §4.1)

- Kind 4 (`Atari_Ball_Hit_*`, live radii: thighs/heads `0.095`, belly/chest `0.16`, legs `0.06`, upper arms
  `0.055`, feet `0.0475`, GK hands `0.05`/forearms `0.04`) and kind 7 (ragdoll/root, bone via table
  `0x142b45df0` = `{0,7,11,12,13,15,16,17,9,10,1,2,3,4,5,6}`) both run the §5 response with bone coefficients
  (hard bones `E0 = 0.45 − 0.05·s, T0 = 0.75`; others `E0 = 0.4, T0 = 0.7`; `s = clamp((3.6·|v|−40)/20)`).
- GF map: GF has no capsules — `Match::CheckBallCollisions` (`match.cpp`) tests the ball against
  player-geometry AABBs with a `boundingBoxSizeOffset` fudge. A port keeps its own broadphase but must use, per
  contacted bone: the PES radius above as contact distance, the §11.1 normal for parts 3/6/7/8 (else the §3.3
  tilt with `c = 1`), and the §4.1 E/T by bone. Kind 7 records with `rec+6 == 3` are dropped (OBS dispatch).
- GF counterpart: the response core of `CheckBallCollisions` (the `bounceVec`/`bias` heuristic and
  `resultVector *= 0.7` damping) is replaced by the §5 `R()` + accumulation with the PES normal and E/T; the
  AABB gathering stays as broadphase.

## 12. When a body contact is ignored

Exactly (OBS raw dispatch + `FUN_140870a40` kick application):

1. **Kicker mute** — the kick writes `simBall+0x40 = 1`, `+0x44 = kicker id`, but **only when `id < 0x16`
   (22)** (unsigned compare; other ids leave the fields untouched). Kind 4/7 records whose `B.id == +0x44`
   are skipped while `+0x40 != 0`. The response fn decrements `+0x40` once per call at the end — so the mute
   lasts exactly **one** response pass. OBS live: after a kick `+0x40/+0x44 = (0, 18)` (consumed, last kicker
   18). GF counterpart: `match.cpp`'s `lastBodyBallCollisionTime_ms + 150 ms` global gate and the
   `lastTouchBias` heuristic are replaced by this per-kick one-pass mute keyed on the kicker id.
2. **Intended-touch suppression** — when any record has tag `rec+6 ∈ {1, 2, 6}` (`(tag−1 & 0xfa) == 0 && tag
   != 5`) or is kind 2 (sensor bone, mode 2 via `FUN_1406f02f0`), the toucher (`+0x28c` / `+0x290`) is recorded
   and `bVar8` is set; at the end `bVar8` selects the record-only path (`+0x54` touch point, no velocity
   response) over the `R()` response. Kind 7 with `rec+6 == 3` is dropped entirely. GF counterpart: the
   `TriggerControlledBallCollision` branch (`CheckBallCollisions` → designated-possession-player path) is the
   intended-touch path — gate it on the PES conditions (sensor-kind or tag 1/2/6) instead of the
   possession-player heuristic.
3. **Held ball** (`+0x293 != 0`, OBS raw): no physics — scan the 22 slots at `simBall+0x5f8 + 0x28·i` for bit 5
   of `[slot+8]` and record that player as toucher. GF has no held-ball state; add the gate.
4. **Approach gate** (unchanged, restated for the port): every used record needs `(v − vB)·n ≤ 0`; kind 14
   records need `n.y < 0.99999`; goal-vs-other nearest selection (`dG ≤ dO` keeps goal records only) per §2.
   GF counterpart: `ball.cpp`'s predict-step contact test gains these gates.

## 13. Open items

1. `Q_slot` semantic source (which bone/object quaternion is copied to `+0xdc/+0xec/+0xfc/+0x10c`): readable
   live, origin open. Next: sample the slots alongside the AnimePlayer bone world rotations in one tick.
2. `FUN_140a8f8b0` step in the soft-sheet update (§10 item 4): no-op on all replayed ticks; needs a tick where
   it moves `v` to characterise.
3. State-2 (Out table) live replay: no ball hit the outside of a net while sampling; loop decoded (§10) but not
   replayed.
4. `rec+6` tag semantics and the frame-hit event id unit: carried over from the earlier §8 item 4.
