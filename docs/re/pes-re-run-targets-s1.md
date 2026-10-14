# Stop-test leaves, batch s1 (static, 2026-10-14)

Source: `dec.sh` + `annot.py` of the 22 VAs (scratch `s1/b.c`). Constants were read from the annotated literals. The
a4c720 args and the lost args were re-checked in raw with `lin.py`. Conventions follow main §0/§3. "true" = STOP unless the
entry says otherwise. The common head below is shared by 692810/692a00/692580/692c50.

Notation (helpers; INF(med) unless OBS):
- `pl(id)` = a48a10; `team(t)` = a48a30; `st(id)` = a48910 (+1 = state byte); `ai(id)` = a489f0.
- `tm` = attacking team index of id (0 for slots 0..10, 1 for 11..21); `slot` = id%11.
- `T.dir` = team+0x250 (attack heading, deg).
- `brg(p→q)` = FUN_140439ef0 (deg). `ad(a,b)` = FUN_14043a0a0 (fold180(a−b)); callers always take |·|.
- `atan2deg(dx,dz)` = a92d40·57.29578, wrapped to [0,360).
- `tgt(id)` = FUN_140596c30 out (AI target point x,_,z). `ev` = last ball event `a3e190(ctx+0xe0,0)`:
  [0] kind, [4] frame, +0x50 pos via a6cbd0.
- `now` = MI+0x1198 (`*(G+0x270)+0x1198`). `t` = state frames = ring `a401c0(T+0xa2cc+0x34·slot)`: [+8]−[+4].
- `fpsI` = FUN_141e532e0 (int fps). `velH(p)` = FUN_140458740(p, 0.69444/fps) (velocity heading).
- `vel(p)` = FUN_140a6c6d0(p, out, 0.69444/fps) (scaled velocity vector). `moveH(p)` = FUN_1404ab8e0 (heading).
- `aiT(p)` = a76240, `aiT2(p)` = a76210(p,0xff) (AI target positions).
- `cone(id, dir, half, r)` = FUN_140a4e720 (opponent within cone) — open, shared.
- `mates/opps lists`: MA+0x22e4c+200·id = per-player sorted {id, d²} lists, 8-byte entries, 2 teams × 11 (0x58 B/team).
- `brgTab[a][b]` = MA+0x23f7c+4·(a·22+b) (bearing a→b).
- `d2ab(a,b)` = FUN_1404c8ea0(MA,a,b) (dist² between players). `brgAB` = FUN_140439fe0(MA,a,b).

Common head `H` (692810/692a00/692580):
```
if carrier(a627e0) valid: return 0                     # loose ball only
dtEv = |now − ev.frame|
require dtEv ≤ t  and  ev.kind ∈ {1,2,3,0x10}  and  dtEv ≤ fpsI     # kicked during this run, ≤ 1 s ago
```

## 1. FUN_14068f600(ctx, id) — 0x14068f600 (§3 step 4, true = STOP)
```
s = T.s; P = pl(id).pos; F4 = F[4] (47.06)
if s·P.x > F4 + 1.5: return 1                                   # 1.5 @0x14259bddc
if T+0x28c+0x40·slot (sub) == 10: return s·P.x ≥ F4 − 2
if s·P.x < F4 − 3: return 0
if !a4c720(ball.x, P.x, s, 0.2): return 0                        # ball ≥ 0.2 m behind runner (0x142598578)
a = |ad(T.dir, atan2deg(tgt.x − ball.x, tgt.z − ball.z))|        # 0 if tgt == ball
if a < 22 or cone(id, T.dir, 50, 20): return 1                    # 22 @0x1425c48c0, 50/20
if a4c720(ball.x, P.x, s, −0.1): return 1
w = |ad(T.dir, brg(P→ball))|
for i in 0..10: o = a71500(MA, oppTeam.s)[opp][i]  (sorted opponent list)
    if o valid:
        if s·o.x < s·P.x: break                                   # remaining opponents are behind runner
        if |o.z − ball.z| ≤ |P.z − ball.z| and FUN_140a53da0(ctx, o, P, T.dir, 2w, 30, 0): return 1
return 0
```
OBS. Open: `a53da0` (lane/cone between o and P, 30 = range).

## 2. FUN_14068bd90(ctx, id) — 0x14068bd90 (step 4)
```
require T+0xb748[slot] == 0x30                                    # team slot sub-state
reg = T+0xb824; require a4d2c0(P, s) == reg or a4d2c0(aiT(id), s) == reg
if T+0xb6f6 != 0 and |tgt(id).z| > |ball.z|: return 0
j = FUN_1404587b0(id)
for i in 0..10:
   if T+0xb748[j] != 0x30 and 5a4830(tm·11+i,0,1) and |tgt(id).z − tgt(tm·11+i).z| < 10:
       if !5a4830(id,0,1) or !a4c9a0(id, −4): return 1
       v = vel(pl(id)); return |v| < (100 / (fps·22.5))·0.8
return 0
```
The raw re-check confirmed that the second 596c30 call (0x14068bf35, r8 = ebx = loop id) is the mate's target. The loop
tests `T+0xb748[j]` with j fixed, not i (decompile; raw not re-walked, INF(med) that this is real).
Open: FUN_1404587b0 (slot partner index), 1405a4830, a4d2c0 region (main §4.2).

## 3. FUN_140692810(ctx, id) — 0x140692810 (step 8, run ≠ 0x1f)
```
H
if s·ball.x ≤ s·P.x: return |ev.pos − ball|²_xz ≥ 400           # 400 @0x1425a2670
return 0
```
In plain terms: a kick in the last second has sent the ball ≥ 20 m and the ball is not ahead of the runner.

## 4. FUN_140692a00(ctx, id) — 0x140692a00 (step 9)
```
H
if !a4c720(ball.x, P.x, s, 8): return 0                          # raw 0x140692b63: xmm0 = ball.x, xmm1 = P.x, k = 8
e = ev.pos
if s·ball.x > s·e.x: return 0                                    # the ball went backwards from the kick point
if |e.x − ball.x| > 7 and a4c9a0(id, −4): return 1
return |e.x − ball.x| ≥ 12
```

## 5. FUN_140692580(ctx, id) — 0x140692580 (step 9, run ≠ 0x1f)
```
H
if |ev.pos − ball|²_xz < 100: return 0
if |ad(T.dir, velH(pl(id)))| > 90: return 1                      # raw 0x140692742: xmm6 = T+0x250
v = vel(pl(id)); return sqrt(v.x² + v.z²) < FUN_1404c8dc0(10)    # 10 @0x142588210 (INF: 10 km/h → units)
```

## 6. FUN_14068a730(ctx, id, run) — 0x14068a730 (exemption: caller STOPs only if it is false)
```
if run ∉ 0x1d..0x1f or !a4c9a0(id, −7): return 0
dz = P.z − ball.z
if |P.z| ≥ 15:  if dx² + dz² > 484: return 0                     # 15/484 @0x14259be0c/0x1425c74cc
else:           if |dz| > 25: return 0
                if |P.x − ball.x| > 12 and !a4c720(P.x, ball.x, s, 0): return 0   # raw 0x14068a7f1..822
if FUN_140a4e250(ctx, id, 3.2, 0): return 0                      # an opponent within 3.2 m (open)
return s·P.x ≤ F6 + 2
```

## 7. FUN_14068a8c0(ctx, id, intercept) — 0x14068a8c0 (exemption)
```
if intercept: return 0
if |P.z| > F5 + 2 or !a4c9a0(id, −2): return 0
A = tgt(id); if |A − ball|² > 729: return 0                      # 729 @0x1425c8384 (27 m)
q = FUN_1405969b0(ctx, tm); require q valid and FUN_1404c8e60(MA, q) ≤ 12.25   # INF: q's dist² to ball
b = atan2deg(A − ball)
if cone(q, b, 80, 6.5, 0,1,0): return 0
oppT = FUN_140456ea0(&tm)
for i in 0..3: o = list(id)[oppT][i]
   if o valid:
      if d2ab(id, o) > 10.24: return 1                           # sorted → rest are farther
      if |ad(T.dir, brgAB(id, o))| ≤ 82 and |o − A|² ≤ |P − A|²: return 0
return 1
```
True means the runner is near the ball, the target is close and his path is clear.

## 8. FUN_1405a4b90(ctx, team) — 0x1405a4b90 (exemption in step 11)
```
c = carrier, else MA+0x2483c; if invalid c = team(team)+0x78; if invalid: return 0
s = team.s; mode = team(team==0 ? 1 : 0)+0xa50c (opponent defensive mode)
if mode ∈ {3,4}:                                                  # loose-ball branch
   if |ball.z| > F6 − 5 or s·ball.x < 0: return 0
   ev = last event (index chosen by a62ea0(ev0+4) == c); require ev.kind == 1
   require now − ev.frame ≤ 2·fpsI
   require |ball − ev.pos|² ≥ 49;  ref = ev.pos, from = ball
else:
   require st(c).state == 9 (0x5c0 '\t') and ai(c)+0x1c valid
   require FUN_140a510f0(ctx, team, pl(?).pos, mode) == 3
   require |p.z| ≤ F6 − 5 and s·p.x ≥ 0
   g = a57800(G+0x40) (INF: goal point); require |g − p|² ≥ 49;  ref = g, from = p
dirA = a4f210(s)                                                  # attack angle 0/180
a = |fold180(brg(from→ref)′ − dirA)|   (FUN_140439e50 out, wrapped to [0,360))
return a ≤ 50
```
Note: the decompile gives `a48a10(G)` with no id in the else branch. INF(med): it is `ai(c)+0x1c`, the target id.
Open: FUN_140a510f0, a62ea0, a57800.

## 9. FUN_140694d10(ctx, id) — 0x140694d10 (step 12: blocks the STOP when true)
```
c = carrier or MA+0x2483c; if invalid: return 0
if |P.x − c.x| > 8: return 0
a = |ad(T.dir, brgTab[c][id])|; if a ∉ [60, 120]: return 0
side = P.z > c.z ? 0 : 180
if cone(c, side, 70, 7, 0,1,0): return 0
return |P.z − c.z| < 20 or !a4c9a0(id, −6)
```
True means the runner is level with the carrier, square to him and unpressured.

## 10. FUN_14068dba0(ctx, id) — 0x14068dba0 (step 13)
```
require a4c9a0(id, −3)
q = FUN_1405969b0(ctx, tm); require q valid
require |P − ball|² ≥ 100 and a4c720(ball.x, P.x, s, 5)
h = moveH(q)[0]; f = pl(q)+0x554; b = brg(ball→P)
m = min(|ad(h, b)|, |ad(f, b)|)
if m < 135 and (|ad(h, T.dir)| < 90 or |ad(f, T.dir)| < 90) and |ad(b, T.dir)| < 36: return 0
return m ≥ 120
```

## 11. FUN_14068de30(ctx, id) — 0x14068de30 (step 13)
```
c = carrier; require valid
nearest-opponent entry of id for c's team: list(id)[c team][byte list+0xb2+c]; require d² ≥ 144
A = tgt(id); b = atan2deg(A − P)
fwd = P.z ≤ A.z ? 0 : 180
require |fold180(b − fwd)| ≤ 65                                   # target roughly along z, away from the side
require a4c720(c.x, P.x, s, 3) and a4c9a0(id, −4)
back = fwd + 180 (wrap)
require |ad(brg(c→P), back)| ≥ 85                                 # a43a020 out
require |fold(velH(c) − back)| ≤ 65 and |fold(moveH(c) − back)| ≤ 65
return |ad(c+0x554, atan2deg(A − c))| ≥ 95
```
Open: a43a020 (signed angle variant), list byte +0xb2 (rank index).

## 12. FUN_14068e300(ctx, id) — 0x14068e300 (step 13)
```
if AI class(ai(?)) == 5: return 0             # a489f0 called with no id in the decompile; INF: ai(id)
c = carrier; require valid and a4c9a0(id, −6)
require t ≥ int(0.1·fps + 0.5) = 5
side = P.z < c.z or P.z == c.z ? 0 : 180      # angle toward the runner's side
require |ad(side, velH(c))| ≤ 25 and |ad(side, moveH(c))| ≤ 25 and |ad(side, c+0x554)| ≤ 25
A2 = FUN_140599ff0(ctx, id, tgt(id))
require |c − A2|² ≥ 144 and |c − P|² ≥ 25
return |ad(c+0x554, atan2deg(A2 − c))| ≥ 105
```
In plain terms: the carrier runs across toward the runner's side while facing away from the runner's target.

## 13. FUN_140a53460() — 0x140a53460 (step 15)
```
st = st(?)+1 (state byte; the decompile shows no id — INF: the runner)
return st ∈ {0x0f, 0x11, 0x50..0x53, 0x57}
```

## 14. FUN_14068efc0(ctx, id) — 0x14068efc0 (step 19: keep-path gate, true = STOP via !…)
```
dA = a4f210(s); bb = atan2deg(P − ball)
a = |fold180(dA − bb)|; if a > 22: return 0                       # runner in front of the ball, within 22° of attack dir
if FUN_140a58320(ai(id)) > 2: return 0
if sub(T+0x28c+0x40·slot) == 1: return 0
if t < 2·fpsI: return 0
if !FUN_1405951b0(ctx, id, −3): return 0
if a > 4:
   r = brg(P→ball); A = tgt(id); ta = atan2deg(A − P)
   if sign(fold(r − dA)) == sign(fold(r − dA)) [decompile tests the same value twice; INF(med): one should use ta]:
      R = max(5, |P − A|)
      if !FUN_140a4f720(ctx, A, opp, 5, 0, 0) and !cone(id, ta, 60, R, 0,1,0): return 0
if |P.x − ball.x| > 8 and FUN_140a4fea0(ctx, opp, ball, dA, 90, 3, 0, 1): return 1
if |P.x − ball.x| ≥ 15 and list(id)[opp][0] valid and !FUN_140695110(ctx, id):
   d = list(id)[opp] first d²
   return (|P.x − ball.x| > 15 and d < 12.25) or d ≤ 25
return 0
```
Open: FUN_140a4f720, a4fea0 (shared, b1), FUN_140695110, a58320. Raw check needed on the sign test (0x14068f1xx).

## 15. FUN_140694320(ctx, id) — 0x140694320 (step 19, true = skip the keep branch)
```
if !FUN_1405a8290(id,1) and !FUN_1406940f0(id): return 0
require FUN_140643a30(ctx, tm, id) and FUN_140642810(ctx, id)
if T+0xa2a0[slot] == 0x1e and sub(slot) == 10: return 0
n = 0
for i in 0..10 (pid = tm·11+i):
   if T+0xa2a0[i] == 0x26: n++
   elif rank byte a714e0(MA,s)+0xb2+tm·11+i < 3 and |pl(pid).z| < F5 and 5a4830(pid,0,1): n++
if n < FUN_140642d00(ctx, tm): return 1
for i (pid ≠ id) with T+0xa2a0[i] == 0x26:
   o = 0xff; if !FUN_1406431e0(ctx, pid, &o, 0) and o == id: return 1
return 0
```
In plain terms: too few players are holding the 0x26 (support) state or are near-centre, or a 0x26 player names id as
his partner. INF(med).

## 16. FUN_14068e900(ctx, id) — 0x14068e900 (step 19)
```
bb = atan2deg(P − ball)
require t ≥ fpsI and AI class(ai(id)) ≠ 9
require |ad(bb, T.dir)| ≤ 22 and a4c9a0(id, −0.8) and |P.x − ball.x| ≥ 8.5
require list(id)[opp] first valid and its d² ≤ 16
q = FUN_1405969b0(ctx, tm); if q invalid or st(q) controlled (a6e340): return 1
if |ad(q+0x554, bb)| < 40: return 0
return |ad(brg(q→aiT(q)), bb)| ≥ 40
```

## 17. FUN_14068ec50(ctx, id) — 0x14068ec50 (step 19)
```
require a4c9a0(id, −3)
require a4c720(ball.x, P.x, s, 14)                                # raw 0x14068eccf
require |ad(T.dir, brg(ball→P))| ≤ 22 and |P − ball|² ≥ 144
require |ad(T.dir, velH(id))| ≤ 22 and |ad(T.dir, brg(P→aiT2(id)))| ≤ 22
o = FUN_140a62850(ctx, id) (marker); require valid
require s·P.x ≤ s·o.x and |P.x − o.x| ≥ 0.5 and |P.z − o.z| ≤ 2
require |ad(T.dir, velH(o))| ≤ 22
vr = |vel(id)|_xz; vo = |vel(o)|_xz
return vr ≤ vo or |vr − vo| ≤ FUN_1404c8dc0(10)
```
In plain terms: the runner is far ahead of the ball, chasing straight with a goal-side marker who keeps pace.

## 18. FUN_14068d920(ctx, id) — 0x14068d920 (step 19: true → skip the stop branch)
```
q = FUN_1405969b0(ctx, tm); require valid
f = pl(q)+0x554
require |ad(f, brgTab[q][id])| ≥ 90
dA = a4f210(s); require |fold180(dA − f)| ≥ 100 and cone(q, dA, 100, 5, 0,1,0)
for each active teammate m (MI+0x1748) ≠ id, ≠ q:
   if |ad(f, brgAB(q, m))| ≤ 45 and d2ab(q, m) ≤ 100: return 1
return 0
```

## 19. FUN_14068b1a0(ctx, id) — 0x14068b1a0 (step 19, t > fps/2)
```
if a4f1e0(P, s) != 0: return 0                                    # INF: only outside own-half region
m = FUN_140a62850(ctx, id); if m invalid: m = list(id)[opp][0]; if invalid: return 0
require d2ab(id, m) ≤ 56.25 and |P − ball|² < 400 and a4c9a0(id, −7)
require AI class(ai(id)) == 2
for i in 0..10 (pid = tm·11+i, active, ≠ id, 5a4830):
   if T+0xa2a0[i] == 0x1e and class(ai(pid)) ≠ 1: continue
   if |P − pl(pid)|² < 144: return 1
return 0
```

## 20. FUN_14068b070(ctx, id) — 0x14068b070 (step 19)
```
require s·ball.x ≥ F4 − 3
require |P.x − ball.x| ≤ 6 and |P.z − ball.z| ≤ 7
require |P.z| ≥ F5
return s·P.x ≥ F8 + 1.5                                           # F8 = F+0x20 (41.56)
```
Note: F8 = 41.56 < F4 = 47.06, so the last test means the runner is past F8+1.5.

## 21. FUN_140692c50(ctx, id) — 0x140692c50 (step 19)
```
require no carrier; ev kind ∈ {1,2,3,0x10}
require |P − aiT(id)|² ≥ 16
a1 = ad(T.dir, brg(P→aiT2(id))); require |a1| ≥ 75
g = a57800(G+0x40); a2 = ad(T.dir, brg(g→ball)); require 45 ≤ |a2| ≤ 135
require sign(a1) ≠ sign(a2)
if |ball.z − g.z| ≥ 6.5 and sign(g.z − P.z) == sign(ball.z − P.z): return 0
return 1
```
In plain terms: the runner is heading sideways away from the line of the loose ball. INF: g = ball target/landing point.

## 22. FUN_14068b920(ctx, id) — 0x14068b920 (step 19)
```
require a72e70(F, ball.x, ball.z, &s) bit 12                      # raw 0x14068b974/97c
require a72e70(F, P.x, P.z, &s) bit 12                            # INF: both in the penalty box
require |P − ball|² ≤ 144 and s·ball.x ≥ F6
require !a4c720(ball.x, P.x, s, 3)                                # raw 0x14068ba2b: ball not ≥ 3 m behind runner
return FUN_140643f50(ctx, tm) or s·ball.x ≥ F8
```

## Open sub-functions
- Shared helpers: a4c9a0, cone a4e720, a4e250, a4fea0, a4f720, 5951b0, 5a4830, 5969b0.
- Region and position helpers: a4d2c0, a4f1e0, a72e70 (bit 12 meaning), a57800 (G+0x40 point), a510f0, a62ea0, a62850.
- Small helpers: a53da0, 4587b0, 456ea0, 439e50, 43a020, 4c8dc0 (speed unit), 4c8e60, a58320.
- Also open: 6431e0, 642d00, 643a30, 642810, 6940f0, 5a8290, 695110, 643f50, 599ff0.
- Raw re-walks still owed:
  - 68efc0 sign test (same value tested twice).
  - 68bd90 loop index j vs i.
  - 5a4b90 else-branch player id.
  - 68e300 and a53460 a48910/a489f0 implicit id (INF: the runner).
