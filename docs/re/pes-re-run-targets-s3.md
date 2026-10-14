# PES 2021 run stop-test leaves — batch s3 (static, 2026-10-14)

Source: decompile `the RunTargetsRE scratch dir /s3/o2.c`; raw checks via `lin.py`. Conventions per `pes-re-run-targets.md` §0/§3.
Common shorthands: `T(id)` = team struct `a48a30(G, side(id))`, side = id∈[11,21]; `slot = id%11`; `s = T+0x254`; `P = pl(id)+0x55c` (x,y,z);
`B = MA+0x2482c` (MA = ctx+0x18); `t(id)` = frames in current state = ring `a401c0(T+0xa2cc+0x34·slot,0)` → `[+8]−[+4]`;
`st(id)` = `T+0xa2a0[slot]` (run state, 0x1e Chance etc); `sub(id)` = `T+0x28c+0x40·slot`; `cls(id)` = `a580a0(a489f0(id))`;
`aimPt(id)` = `a76240(pl(id))` (AI target point); `active(j)` = `MI+0x1748[j]`; `lineX(opp)` = `MA+0x44+0x14·opp`;
`mates(id)` = j over own side 0..10, active, ≠ id. `|·|`= fabs. All return 1 = STOP condition true (callers per §3 item 19) unless noted.
Angle fold `fold(a)` = `a43a0a0` (signed diff, deg); bearing = `439ef0(from,to)`; `c8ea0(MA,a,b)` = dist² a↔b (INF med, used vs 25/2025);
`MA+0x23f7c+(a·0x16+b)·4` = bearing a→b table (OBS usage).

## 1. FUN_140691300(ctx, id) — "Chance runner overtaken by teammate on same wing" (OBS 0x140691300)
```
if st(id) != 0x1e or sub(id) != 2: return 0
A = aimPt(id)
if |P.z| > |A.z| or |A.z| < F7: return 0            # aim deeper wide than runner, at |z| ≥ F7 (20.12)
q = 5969b0(ctx, side)                                # receiver
sgnA = sign(A.z) (0 → +1)
for j in mates(id), j != q:
   Pj = pos(j)
   if sign(Pj.z)==sgnA and |Pj.z| >= F7 and |Pj.z − A.z| <= 15
      and a4c9a0(ctx, j, −12)                         # j ahead/near ball (see helpers)
      and !a53da0(ctx, id, &Pj, T+0x250, 50, 50, 0):  return 1
return 0
```
Constants: 15 (0x14259be0c), −12 (0x14259d664), 50 (0x14259be34), F7 (+0x1c). Open: `a53da0` (cone/area test), `a4c9a0`.

## 2. FUN_140690960(ctx, id) — "Chance runner too far from ball, behind-line" (OBS 0x140690960)
```
if st(id) != 0x1e: return 0
if t(id) < int(0.6·fps+0.5)=33 or !a4c9a0(ctx,id,−7): return 0
L = lineX(opp); k = −16
c = a627e0(ctx,0)
if c valid and !a4c9a0(ctx,id,−3): return 0          # when carrier valid, need a4c9a0(−3); then k = −20
if c valid: k = −20
if |P−B|²(x,z) < 484: return 0                        # ≥ 22 m
if a4c720(L, B.x, s, k): return 0                     # line not >16/20 m behind ball
if a4c9a0(ctx,id,0): return 1
if 594a10(ctx,id,pl(id),3,−1) and !695110(ctx,id): return 1
return 0
```
Constants 0.6/0.5, −7, −16 (0x1425c7fa0), −3, −20 (0x1425a946c), 484 (0x1425c74cc). Open: `594a10` (path test, already open §4), `695110`.

## 3. FUN_14068aeb0(ctx, id) — "pass target is someone else & receiver gets there first" (OBS 0x14068aeb0)
```
ev = a3e190(ctx+0xe0,0)                               # last ball event
if ev.type ∉ {1,2,3,0x10}: return 0
if id == a62e40(a489d0(side), a3e900(ev)): return 0   # he is the event target
q = 5969b0(ctx, side); if q invalid or q == id: return 0
c = cls(id); if c == 2 or c == 0x13: return 0
if !a4c9a0(id,−4) or a4c9a0(id,−1.2): return 0
if !a4e250(ctx,id,5,0): return 0
tq = 653480(ctx,q)                                    # receiver arrival frames
596c30(&tgt,ctx,id)
ti = 655660(ctx,id,&tgt,0,0)                          # runner frames to tgt
return ti + int(0.15·fps+0.5)(=8) <= tq
```
Constants −4 (0x1425c7678), −1.2 (0x1425c7ba0), 5, 0.15 (0x1425c48a0). Caller gates t > 2·fps. Open: `653480`, `a4e250`.

## 4. FUN_140691660(ctx, id) — "teammate ahead/near on same line" (OBS 0x140691660)
```
q = 5969b0; sgn = s; ang = (MA-table a71260(MA,s)[id] + 180) wrapped 0..360
role(id) = T+0x4c00[slot]
A = aimPt(id); (computes bearing P→A, unused result)
for b in 0..10: j = a714e0(MA,s)[side·11+b].id   # line-ordered list of own players
   if j invalid/inactive/==id/==q: continue
   if s·pos(j).x < s·P.x: return 0               # list sorted: first mate behind runner ends test
   if !5a4830(ctx,j,0,1): continue
   if role(id) ∈ {3,4} and role(j) ∈ {3,4}: return 1
   if dist²(id,j) (c8ea0) < 25: return 1
   if |fold(bearing(id→j) − T.dir)| <= 45:
      if a53da0(ctx,j,P,ang,40,8,0): return 1
      if |P.z−Pj.z| < 6 and |P.x−Pj.x| < 8: return 1
return 0
```
Constants 180/360, 25 (0x14259be20), 45, 40, 8, 6. INF(med): `iVar21-3 < 2` ⇒ role ∈{3,4}. Open: `a71260` (per-player angle table), `a714e0` list order (INF: by s·x).

## 5. FUN_14068fd90(ctx, id) — "another mate is Chance-running sub 0x10 on same side, outside" (OBS)
```
A = aimPt(id)
if sign(B.z) != sign(A.z) or |A.z| > |B.z|: return 0
for j in mates(id): if st(j)==0x1e and sub(j)==0x10: return 1
return 0
```

## 6. FUN_14068ff00(ctx, id) — "a cls-0x19 Chance mate is wider on same side" (OBS)
```
A = aimPt(id)
for j in mates(id) with st(j)==0x1e:
   if cls(j) == 0x19:
      dA = A.z − B.z; dj = Pj.z − B.z
      return sign(dA)==sign(dj) and |dA| <= |dj|      # first such mate decides (break)
return 0
```

## 7. FUN_1406900a0(ctx, id) — "cls 1/0x12 runner with blocked lane" (OBS)
```
if cls(id) ∉ {1,0x12}: return 0
if t(id) < int(fps+0.5)=54: return 0
if s·B.x > s·P.x: return 0                            # runner level or ahead of ball
596c30(&tgt); 599ff0(&p,ctx,id,&tgt); bearing(B→p) (a92d40)
if a72180(MA+0x24c6a): return 0                       # flag set → no
h = bearing(P, aimPt(id))
if a72180(...): return 0
for k in 0..9: p = P advanced along wrap360(h) by 5k m (a8f7c0); a92d40(p−B); if a72180(MA+0x24c6a): return 0
return 1
```
INF(low): `a92d40` writes a direction into `MA+0x24c6a` state and `a72180` reads a "blocked" flag; i.e. ball→point line clear for all 10 probes. Open: `a92d40`, `a72180`, `599ff0`.

## 8. FUN_140693ef0(ctx, id) — "wide deep runner, mates outside him" (OBS; raw F+0x20 at 0x140693f70)
```
if cls(id) == 0x13: return 0
if s·P.x < F8: return 0                               # F+0x20 = 41.56
if |P.z| < F2 + 1.5: return 0
if |B.z| > |P.z| or |P.z − B.z| < 6: return 0
for b in 0..10 (line-ordered list a714e0(MA,s), side index a456ea0):
   j = list[b]; if j > 21: break
   if s·Pj.x < s·P.x: break
   if |Pj.z − B.z| <= |P.z − B.z|: return 1
return 0
```
Constants 1.5 (0x14259bddc), 6 (0x14259bdec).

## 9. FUN_14068c1a0(ctx, id) — "controlled runner's path crosses ball line, mate better placed" (OBS)
```
if cls(id) ∈ {5,0x19}: return 0
if a4c720(B.x, P.x, s, 0): return 0                   # ball behind runner → no
n = a6e6b0(pl)                                        # path point count (controlled)
find first i<n with s·B.x < s·path[i].x; if i==0 or none: return 0
X0 = intersect(path[i-1..i], segment (B.x,0,−1000)-(B.x,0,1000)) via a90d80, else return 0
dR² = |P − X0|²
for j in mates(id) with 5a4830(j,0,1):
   if a4c720(B.x, Pj.x, s, 0): continue
   [same path search on pl(id)'s path → X1]       # decomp re-uses lVar12 (runner) — INF(med): bug-faithful, X1==X0
   if sign(X0.z−B.z)==sign(X1.z−B.z) and |X0.z−X1.z| <= 6 and |Pj−X1|² <= dR²: return 1
return 0
```
Constants ±1000, 6. Open: `a90d80` (seg intersect), `a76210` (path point i).

## 10. FUN_14068d730(ctx, id) — "ball deep wide in box-side channel and runner goes there" (OBS)
```
if s·B.x < F8: return 0
if !(F5 <= |B.z| <= F7): return 0
n = int(0.3·fps+0.5)=16; Q = P + n·vel (x: +0x574, z: +0x57c)
if sign(Q.z) != sign(B.z) (B.z≥0 → +): return 0
if s·Q.x > F8 and F5 < |Q.z| < F7: return 1
596c30(&tgt): if s·tgt.x > F8 and F5 < |tgt.z| < F7: return 1
return 0
```

## 11. FUN_14068fc70(ctx, id) — "ball deep, target on other side & far" (OBS)
```
if s·B.x < F8: return 0
596c30(&tgt)
if sign(tgt.z) == sign(B.z): return 0
return |tgt.z| >= F5 and |B.z − tgt.z| >= 8
```

## 12. FUN_14068cde0(ctx, id) — "wide target, mate in state 0x27 same side" (OBS)
```
596c30(&tgt); if |tgt.z| < F7 − 2: return 0
if id valid and T+0xc064[(T+0xc112[id]) + side·11]·8 > 20: return 0     # per-player value table (same as §4.2 5ab180)
for j in mates(id) with st(j) == 0x27:
   if sign(Pj.z) == 4f6700(): (INF: sign of tgt.z)
      if |Pj.x − P.x| >= 6 and !a4c9a0(j,−7): return F6 − 10 < s·Pj.x
      return 1
return 0
```
Open: `4f6700` (returns ±1; INF(med) sign of tgt.z via xmm).

## 13. FUN_14068cc40(ctx, id) — (OBS; raw 0x14068cd43)
```
if t(id) < int(0.3·fps+0.5)=16: return 0
if s·P.x < F6: return 0
if a58320(a489f0(id)) >= 3: return 0                  # player sub-class/role
if !a4c720(B.x, P.x, s, 3): return 0                  # ball ≥ 3 m behind runner
if |P−B|² < 144: return 0
return a4c9a0(ctx,id,−1)
```

## 14. FUN_14068baa0(ctx, id) — "fresh pass to a teammate with dribble skill, runner far/behind" (OBS)
```
if a627e0(ctx,0) valid: return 0                      # needs no carrier
ev = a3e190(ctx+0xe0,0); dt = |MI+0x1198 − ev[4]|
if dt > t(id) or dt >= 3 or ev.type ∉ {1,2,3,0x10}: return 0
r = a63020(ctx, side) (receiver); if invalid: return 0
sk = 1b59e20(a3af90(a489d0(side), r)); if 8c75c0(sk,2) >= 1 or 8c75c0(sk,1) >= 1: return 0
tgt = a62ea0(ctx, ev+4); if invalid or side(tgt) != side: return 0
sk2 = ...(tgt); if 8c75c0(sk2,2) < 1 and 8c75c0(sk2,1) < 1: return 0
d² = |P−B|²
if d² >= 484: return 1
if d² >= 144 and !a4c9a0(id,−6.5) and 44e020(T+0xc060,id) <= 60 and a4e720(ctx,id,T+0x250,80,30,0,0,0): return 1
return 0
```
INF(low): `8c75c0(·,k)` = card/skill count. Open: `8c75c0`, `1b59e20`, `44e020`.

## 15. FUN_1406910c0(ctx, id) — cls 0x19 sub 0x16 (OBS; note returns 1 by default)
```
if cls(id) != 0x19: return 0
if sub(id) != 0x16: return 0
if t(id) > int(3·fps+0.5)=162: return 1
d² = |P−B|²; dz = P.z−B.z
if 30.25 <= d² <= 529 and |dz| <= 15 and s·P.x <= s·B.x:
   if |fold(T.dir − bearing(P→B))| <= 70 and (r = a63020 receiver valid):
      return |fold(r.facing − bearing(r→id))| > 90
return 1
```
Constants 3, 30.25, 529, 15, 70, 90.

## 16. FUN_1405a7fe0(ctx, id, flag) — KEEP test "receiver is passing to me" (OBS)
```
q = 5969b0(ctx,side); if invalid or q==id: return 0
A = a489f0(q)
if A+0x48 == 0x18 and dist(q-table MA+0x24710[...q]) < 4 and !a4c9a0(id,2): return 1
for e in MA(G+0x180) list of 24 entries (stride 0x74): if e+0x20 == q:
     if e+0xd == 0 and (A+0x45 ∈ {2,3} → need |fold(A+0x38 − bearing(q→id))| < 50, else go on): ...
         (byte A+0x45 ∉{2,3} or angle ≥ 50 → tail test; angle < 50 → return 1)
     break
if A+0x13ac==0x18 and A+0x13c4==id: return 1
tail: if A+0x1494 ∈ {2,3} or A+0x20 == 0x18:
   if A+0x1494==3: if A+0x1490==id return 1; if A+0x1490 valid return 0
   if flag and A+0x14 ∈ mask 0x66180000300 and A+0x1c==id: return 1
   if action(q) (a48910+1) ∈ mask 0x66180000300:
      return |fold(bearing(q→id) − a48910(q)+0x11c)| < 60 and dist²(q,id) < 2025
return 0
```
Mask 0x66180000300 = actions {8,9,0x1d,0x1e,0x23,0x24,0x27,0x29,0x2a}. INF(med) on G+0x180 branch wiring (goto-heavy).

## 17. FUN_1405a7be0(ctx, id) — KEEP "receiver in action 8 aimed at me" (OBS)
```
q = 5969b0; if invalid: return 0
A = a489f0(q); if A+0x1494 != 2: return 0
if A+0x14 == 8 and A+0x1c == id: return 1
if action(q) == 8:
   return |fold(bearing(q→id) − act+0x11c)| < 45 and 64 < dist²(q,id) < 2025
return 0
```

## 18. FUN_1405a7d10(ctx, id, flag) — STOP "carrier passing/aimed to me" (OBS)
```
c = a627e0 or MA+0x2483c; if invalid: return 0
if flag and |B−P|² < 324 and |fold(c.facing − bearing(B→P))| < 45: return 1
A = a489f0(c); st = A+0x1494
if (st==0 and A+0x1490==id) or (A+0x14 ∈ mask 0x22080000200 and A+0x1c==id and st==0): return 1
if action(c) ∈ mask 0x22080000200 and st==0:      # {9,0x23,0x29}
   hits = mates (list MA+0x2470c, side) j≠c with |fold(bearing(c→j) − act+0x11c)| < 25 and dist²(c,j) < 400
   if any hit ≠ id: return 0; if id hit: return 1
return 0
```
Constants 324 (0x1425c48dc), 45, 25, 400.

## 19. FUN_140596240(ctx, T, id) — "runner is the pinned/best wing option" (OBS; 0 = caller STOP §3 item 6)
```
n = 8c51f0(T+0x8e08, slot, 2)
if n >= 3 or T+0xb8a2: return 1
if 5aaf00(ctx, side): return 1
if n == 2 and a63950(ctx,side,4,0xff): return 1
me = 5a4830(ctx,id,0,1)
for j in mates(id):
   nj = 8c51f0(T+0x8e08, j%11, 2)
   if nj < 3 and nj != 0 and 5a4830(ctx,j,0,1) and (!me or |Pj−B|² <= |P−B|²): return 0
return 1
```
Open: `8c51f0` (instruction/slot role count), `a63950`.

## 20. FUN_1405a4f40(ctx, id, pt) — (OBS; raw 0x1405a4ff6–0x1405a505e)
```
if !a4c9a0(id,−8): return 0
L = lineX(opp)
if !a4c720(L, B.x, s, −12): return 0
if !(a4c720(B.x, P.x, s, −8) or a4c720(L, B.x, s, −8)): return 0    # xmm6 = −8, INF(med) still live
if a4e720(ctx,id,T.dir,180,7.5,0,1,0) or a4e720(…,145,10,0,1,0) or a4e720(…,120,100,0,0,0): return 0
if s·P.x > F6 − 8.5: return 0
if |pt − B|² > 1764: return 0
q = 5969b0; if invalid: return 0
if |fold(q.facing − bearing(B→pt))| > 155: return 0
return id > 21 or a714e0(MA,s)[0xb2+id] < 4
```
Constants −8, −12, 180/7.5, 145/10, 120/100, 8.5, 1764, 155.

## 21. FUN_1405a40a0(ctx, id) — (OBS; tail-call)
```
596c30(&tgt,ctx,id)
if 5951b0(ctx,id,−1.5): tgt = (599d90(ctx,side), ·, P.z)     # x = lineX, z = runner z
return 5a3080(ctx,id,&tgt,1,0)
```
Open: `5a3080` (main body; decompile shows void — result is 5a3080's al).

## 22. FUN_1405a1cc0(ctx, id) — "carrier's dribble line crosses my path, he arrives no later" (OBS; raw loop 0x1405a1f76)
```
c = a627e0; if invalid: return 0
if id valid and MA+0x22e50[c·200 … slot] > 16: return 0         # per-carrier dist² table to id
k = a58320(a489f0(c)); if k == 5 or k == 0: return 0
h = wrap360(bearing(c.pos, aimPt(c)))
E = c.pos + 10 m along h
v = |a6c6d0(c, 0.6944/fps)| (speed); if v < 1404c8dc0(4): return 0
lo = 14043c430(32.7); hi = 14043c430(12.9)·0.8
u = v <= lo ? 0 : v >= hi ? 1 : (v−lo)/(hi−lo); R = 6u + 2.5
n = a6e6b0(pl(id)); cur = a74160(pl(id))
for i in max(1,cur)..n−1:
   X = intersect(path[i−1], path[i], c.pos, E) (a90d80) else continue
   if (|path[i−1]−path[i]|² <= |P−path[i]|² or |P−path[i−1]|² <= |X−path[i−1]|²) and |c.pos−X| <= R:
       if 655660(id,X) >= 655660(c,X) + int(0.2·fps+0.5)(=11): return 1
return 0
```
INF(low) 14043c430 = km/h→m/frame converter. Open: `a6c6d0`, `14043c430`.

## Open sub-functions
a53da0, a4c9a0, a4e250, a4e720, 594a10, 695110, 653480, 655660, 599ff0, a92d40, a72180, a71260, a714e0, a456ea0, a90d80,
a76210/a6e6b0/a74160 (path API), 4f6700, a58320, 8c75c0, 1b59e20, 44e020, a63020, 8c51f0, a63950, 5aaf00, 5a3080, a6c6d0, 14043c430, 1404c8dc0.
