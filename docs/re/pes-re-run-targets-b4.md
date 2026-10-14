# PES21 Chance-run lane commit leaves — batch b4

Static, decompile `the RunTargetsRE scratch dir /LeafB4/b4a.c` + raw `lin.py` spot checks. Conventions per
`pes-re-run-targets.md` §0/§5. Leaf return 1 = pass (lane kept) unless the commit negates it (`!` in §5 list).
Common args: `ctx` (ctx+0x18 = MA), `cx` (FUN_140591b70 context), `id` runner, `C` lane point (C.z = +8).
`T` = team(cx[0]) (`a48a30`), `T.dir` = T+0x250, `s` = T+0x254. `ad(a,b)` = FUN_14043a0a0 signed angle diff (deg);
`|ad|` = fabs. `sgn(x)` = x<0 ? −1 : +1. `L` = opp offside-line x `MA+0x44+0x14·opp`. `q` = FUN_1405969b0(ctx,team),
`r` = FUN_140a63020(ctx,team) (both "receiver"-type ids, see §4), `c` = carrier FUN_140a627e0(ctx,0). Ids > 21 = invalid.
`mates(list)` = FUN_140a714e0(MA, s) row `team` (11 ids, order INF(med): by s·x descending; loops stop at first id > 21).
`pv(p)` = |FUN_140a6c6d0(p, 0.694/fps)| (velocity mag; xz or xyz as noted). `hdg(p)` = FUN_140458740(p, 0.694/fps)
(predicted heading bearing, INF(high)). `vMin` = 100/(fps·32.7), `vMax` = 100/(fps·12.9), `vRun` = 100/(fps·22.5).

### 1. FUN_140666f20(ctx, cx, id, C) → true unless a lateral teammate run crosses (OBS 0x140666f20)
```
if |ad(T.dir, cx[1])| < 45 or |P.z − C.z| < 2: return 1                 # 45 @0x14259be30, 2 @0x142565604
σ = sgn(C.z − P.z)
for t in mates (t>21 → return 1; skip t==id, t==q):
    if !a4c720(P.x, t.x, s, −6): return 1                                 # −6 @0x1425a200c
    if FUN_1405a4830(t,0,1) and t' = FUN_140596c30(t) and |C.z − t'.z| ≤ 16:   # 16 @0x14259be10
        if sgn(t'.z − t.z) == sgn(P.z − t.z) and sgn(t'.z − t.z) ≠ σ:
            if |ad(T.dir, bearing(t→t'))| ≥ 45 or |C.z − t'.z| ≤ 12: return 0   # 12 @0x14259be04
return 1
```

### 2. FUN_14066e130(ctx, cx, id, _, flags) → true unless a deep forward near the box/ball should not run (OBS 0x14066e130)
```
k = T+0x4c00[id%11];  if k ∉ {3,4,6}: return 1                            # (k−3)&~3==0 && k≠5
if FUN_140a64030(id,9,info,T) or cx[5] < 196 or cx[4] < 64 or (cx[5] < 324 and !flags[0]): return 1
if a4c720(L, ball.x, s, −13): return 1                                    # −13 @0x1425c767c
if s·ball.x > F6 − 5: return 1
if a4c720(L, P.x, s, −7) or |P − ball|²(xz) > 169 or q > 21: return 1
if id ≤ 21 and MA[q·200 + 0x22efe + id] ≥ 2: return 1                    # byte rank, INF(med)
if s·P.x < F6 − 5.5: return 0                                             # 5.5 @0x1425c8378
if |ad(T.dir, hdg(id))| > 40: return 0
if pv_xz(id) ≥ FUN_1404c8dc0(15) and |ad(T.dir, P.facing)| ≤ 45: return 1
return 0
```

### 3. FUN_1406665d0(ctx, cx, id) → true unless runner already ahead with lane angle ≤ 42 (OBS 0x1406665d0, raw-checked)
```
if FUN_140a50250(ctx, team, 2) > 0 and cx[5] < 576: return 1
if cx[5] < 289 or cx[6] < 64: return 1
if s·P.x < F6 − 3: return 1
if !a4c720(ball.x, P.x, s, 8): return 1                                   # raw 0x1406666ad, 8 @0x14259bdf4
return |ad(T.dir, cx[2])| > 42                                            # 42 @0x1425cd830
```

### 4. FUN_14066cd50(ctx, cx, id, C) → true unless the receiver beats the runner (OBS 0x14066cd50)
```
if c ≤ 21: return 1
if q > 21 or !FUN_140a4e720(ctx, id, cx[1], 90, 30, 0,0,0): return 1
return FUN_1405997f0(ctx, id, q) ≤ int(fps·0.2+0.5)=11 + FUN_140655660(ctx, id, C, 0, 0)
```

### 5. FUN_14066e470(ctx, cx, id, C) → true when lane is lateral-short or forward enough (OBS 0x14066e470)
```
if |P.z − C.z| < 3: return 1
if |ad(cx[1], T.dir)| < 72: return 1                                      # 72 @0x1425cb47c
if |C.z − ball.z| < 3 and |P.z − C.z| < 10 and |P.z − ball.z| > 3 and |ball.z − C.z| < |P.z − C.z|
   and |ad(T.dir, cx[2])| < 35: return 1                                  # 10 @0x142588210, 35 @0x14259a760
return 0
```

### 6. FUN_1406679e0(ctx, cx, id, C) → (non-chance) true unless a wide backward-angled lane away from ball side (OBS 0x1406679e0)
```
if |P.z − C.z| < 3 or |ad(cx[1], T.dir)| < 50: return 1
if sgn(ball.z − P.z) == sgn(C.z − P.z): return 1
if !FUN_140a4e720(ctx, id, cx[1], 50, sqrt(cx[4]) + 4, 0, 0, 2.0): return 1
return |ad(cx[1], P.facing)| < 35
```

### 7. FUN_140a72180(tab, x) → byte lookup (OBS 0x140a72180)
`return tab[x ≥ 1 ? (u16)((x − 1)·0.5) : 0]` — called on MA+0x24c6a (§5); the float arg's source is in the commit (not here).

### 8. FUN_14066e5f0(ctx, cx, id, C) → true unless a stronger/marked mate already wide on C's flank (OBS 0x14066e5f0)
```
if |C.z| < F7: return 1
k = T+0x4c00[id%11]
if k > 4 and FUN_1406405c0(ctx, T+0xc48, id, 0): return 1
for t in mates (t>21 → 1; t==id → 1 [stop at runner]; skip t==r):
    if |t.z| ≥ F7 − 3 and sgn(t.z) == sgn(C.z) [C.z==0 → +] and |P.z − t.z| ≤ 12:
        if FUN_1405a4830(t,0,1): return 0
        if T+0x4c00[t%11] > k: return 0
return 1
```

### 9. FUN_14066ed80(ctx, cx, id, C) → true unless a mate already holds the same wing (OBS 0x14066ed80)
```
if |C.z| < F7 − 2: return 1
for t in mates (t>21 → 1; skip id, r):
    if !a4c720(P.x, t.x, s, −8): return 1                                 # −8 @0x1425c7ba4
    if sgn(t.z) == sgn(C.z) and FUN_1405a4830(t,0,1) and |t.z| ≥ F7:
        tt = FUN_140a76240(t)    # t's run target
        if |tt.z| ≥ F7 and (|P.z − t.z| ≥ 4 or !a4c720(t.x, P.x, s, 6)): return 0
return 1
```

### 10. FUN_14066b210(ctx, cx, id, C) → true unless an opponent reaches the lane before the ball can (OBS 0x14066b210, raw-checked)
```
if cx[5] < 324: return 1
Q = C moved 10 m along fold360(T.dir)                     # FUN_140a8f7c0(&Q, &dir, 10)
v = 85000/(fps·3600)  (92000/… if FUN_140a50250(ctx,team,2) > 0)
tb = 1.3 · |ball − Q|_xz / v                               # 1.3 @0x14259bdd8
for o in opp list (a714e0 row ¬team; o>21 → return 1):
    if FUN_140a638c0(ctx,o): continue
    if !a4c720(P.x, o.x, s, −15): return 1                 # raw 0x14066b47f, −15 @0x14259d668
    if |o − Q|²_xz ≤ 900:
        to = FUN_140655660(ctx, o, Q, 0, 0)
        if to < tb and |int(tb − to)| > int(fps·0.1+0.5)=5: return 0
return 1
```

### 11. FUN_1406676c0(ctx, cx, id, C) → run-state 0x2f lane must not cross marker / curl (OBS 0x1406676c0)
```
if T+0xa2a0[id%11] ≠ 0x2f: return 1
m = FUN_140a62850(ctx, id);  if m > 21: return 1
if sgn(ball.z − P.z) ≠ sgn(C.z − P.z) and |P.z − C.z| > 3: return 0
if sgn(m.z − P.z) == sgn(C.z − P.z) and |C.z − P.z| > |m.z − P.z|: return 0
if pv_xz(id)·fps·3600/1000 > 7:                           # km/h
    a1 = ad(T.dir, cx[1]); if |a1| > 12 and sgn(ad(T.dir, hdg(id))) ≠ sgn(a1): return 0
return 1
```

### 12. FUN_14066c7f0(ctx, cx, id, C) → true unless C outside the active z-band (OBS 0x14066c7f0)
```
if MA+0x24d38 and bit (id%11) of MA+0x24d44 and s·ball.x ≥ 17 and |ball.z| ≤ 15 and |C.z| ≥ F5:
    if C.z > MA+0x24d3c + 10 or C.z < MA+0x24d40 − 10: return 0
return 1
```

### 13. FUN_14066b070(ctx, cx, id, C, flags) → true unless a mate already near C inside the box width (OBS 0x14066b070)
```
if cx[5] < 324 or !flags[1]: return 1
if a4c720(L, ball.x, s, −15): return 1
for k in 0..10: t = team·11+k ≠ id:
    if FUN_1405a4830(t,0,1) and t' = FUN_140596c30(t) and |C.z − t'.z| ≤ 12 and |t'.z| ≤ F5 + 3: return 0
return 1
```

### 14. FUN_14066a130(ctx, cx) → receiver's direction vs ball→pt (OBS 0x14066a130)
```
if r > 21: return 1
d = sqrt(cx[5]); w = d ≤ 20 → 1; d < 42 → (42 − d)/22; else 0
return |ad(FUN_140a48910(r)+0x124, cx[3])| ≤ 40w + 65
```

### 15. FUN_14068ac40(ctx, id, pos, flag) → (commit uses !…(C,1)) true = "wide-box situation hands off to FUN_140694ee0" (OBS 0x14068ac40, raw-checked)
```
tm = team(id); if q(tm) > 21: return 0
if |ball.z| < F7: return 0
if sgn(P.z) ≠ sgn(ball.z) or |P.z| > |ball.z|: return 0
k = flag ? 0.8 : 0
if a4c720(ball.x, P.x, s, 5 + k) and !a4c720(L, ball.x, s, −10 − k): return 0   # raw 0x14068adb7/addc
lim = (s·ball.x ≤ F6 − 12) ? F7 + 0.8 − k : F5 + 0.8 − k
if |pos.z| < lim: return 0
return FUN_140694ee0(ctx, tm, flag)
```

### 16. FUN_140669080(ctx, cx, id, C) → (chance) true unless a runner cutting across the box is too slow vs marker (OBS 0x140669080)
```
if s·P.x < F8 or |P.z| > F5 or s·ball.x < F8: return 1
if sgn(ball.z) == sgn(C.z) or |ball.z − C.z| < 6: return 1
if sgn(C.z − P.z) == sgn(ball.z − P.z) or |C.z| < 3: return 1
tgt = FUN_140a76240(id); if sgn(ball.z − P.z) ≠ sgn(tgt.z − P.z): return 1
if |ad(T.dir, bearing(P→tgt))| < 3: return 1
m = FUN_140a62850(ctx,id); if m > 21: return 1
if sgn(m.z − P.z) == sgn(ball.z − P.z) or s·P.x > s·m.x: return 1
return pv_xz(id)² < FUN_1404f6110(FUN_1404c8dc0(8))        # 4f6110 open (INF: square)
```

### 17. FUN_140666710(ctx, id, C, cx) → (non-chance) true unless an unmarked mate deep at C's z is in a better sector (OBS 0x140666710, raw 0x1406668f3)
```
if cx[7] > 50 or s·ball.x > F6 − 12: return 1
lx = FUN_140599d90(ctx, team)
for t in mates (skip t>21; skip t==id; skip all if r == id [raw cmp rcx,r9]):
    if !FUN_140593430(ctx, T, t, 0x7e): continue
    if !a4c720(lx, t.x, s, −18): return 1                                 # −18 @0x1425c7e70
    if |t.z − C.z| > 7: continue
    covered = any opp slot k (o = opp·11+k): (o>21 or !G270+0x1748[o]) and !FUN_140a638c0(o)
              and (k>10 or opp+0x558[k] > 21 or opp+0x558[k] == t)
              and FUN_140a53da0(ctx, o, t.pos, T.dir, 70, 50, 0)
    if !covered and FUN_14059a1f0(ctx, t, (lx, 0, t.z)) ≥ 50: return 0
return 1
```
INF(med): the `(o>21 or !active)` clause is as decompiled; raw not re-walked.

### 18. FUN_1405a1310(ctx, id, C, extra) → (commit !…) true = runner path collides with carrier's dribble (OBS 0x1405a1310)
```
if c > 21: return 0
if id ≤ 21 and MA[c·200 + 0x22e50 + (rank[c·200+0x22efe+id] + (id≥11?11:0))·8] > 36: return 0
cseg = c.pos → +100 m along bearing(c→FUN_140a76240(c))
vc = pv_xyz(c); w = vc ≤ vMin → 0; vc < 0.8vMax → (vc−vMin)/(0.8vMax−vMin); else 1;  rc = 7w + 3.5
b = bearing(P→C); rseg = P → C + 25 m along b
X = FUN_140a90d80(rseg, cseg); if none: return 0
a = |ad(hdg(id), b)|
R = (a > 60 ? 15 : 5) + rc + extra;  if |c − X|² > R²: return 0
vr = class(FUN_140a58320(info(id))) == 4 ? vMax : vRun
return |c − X|/vMax ≤ int(fps·(a > 60 ? 0.5 : 0.2) + 0.5) + |P − X|/vr
```

### 19. FUN_1405a3080(ctx, id, C, p4, p5) (commit !…(C,1,1)) → true = a teammate is better placed for C (OBS 0x1405a3080; partial)
```
w = (no T-flag gate [a62180==0 && T+0xb6e0] ? 0 : max(ramp(s·C.x, F6→0), ramp(|C.z|, F5+2 → F7+2)))
R2 = (3w + 6)²
marked = FUN_1405a4830(id,1) or (run(id)==0x26 and (class ≥ 9 or class == 6))
hcap = marked ? 0 : 5;  dP = |P − C|
if |ad(bearing(P→C), T.dir)| ≥ 40 and sgn(ball.z − P.z) ≠ sgn(C.z − P.z) and !a4c9a0(id): hcap += 5.5·ramp(a, 65→90)
if FUN_140a6e320(info(id)+1): hcap += 5
route via FUN_1405976d0(…, 10) else FUN_140596560 → route point Rp, bearing b
for each active teammate t ≠ id (≠ T+0xb8d4 slot if T+0xb8e8):
    skip if run(t)==1 and T+0x29c+0x40k and |C − target(t)|² < 64; skip run 0x1e with class 4;
    need FUN_1405a4830(t,1) or run 0x27/0x26
    p5==0: need dwell ≥ int(fps·0.2+.5) (unless both 0x26) and |P.z − t.z| ≤ 8
    need !a4c720(lx, t.x, s, 5)
    marked && p5==0 && FUN_1405a2850(ctx,id,C,t) → return 1
    t' = FUN_140596c30(t); r2 = R2 (144 when T+0xb748 slot 0x30 rule), capped 25 if C/t' on opposite sides of ball.z;
       angle-based widen (24→70°, (5u+6)² ) via 4c8ea0/439fe0/4f6700   [sub-branch not fully decoded]
    if |t' − C|² ≤ r2:
        p4 and receiver q's info+0x1494==3 and +0x1490==t → return 1
        h = 7 (0 if t marked) + 5.5·ramp(|ad(bearing(t→C),T.dir)|,65→90) [opp side, !a4c9a0(t)]
            +3 [marked && fwd-angle && !a4c720(P.x,t.x,s,−3)] −3 [t marked, |ad|<45, a4c720(P.x,t.x,s,3)]
            −7 [!marked && t marked && |t'.z| > F7 && |P.z| < |t.z|] +7 [rank flag, class ≠9, !a4c9a0(t)] +5 [a6e320(t)]
            − (p5 && t marked ? FUN_140598350(ctx,id,t,C) : 0)
        dt = |t − t'|
        if (dP ≥ 2.5 or dt ≤ dP + 5) and h + dt < hcap + dP: return 1
return 0
```

### 20. FUN_140674f00(ctx, id, C) → (commit !…) true = runner can intercept the loose ball on P→C (OBS 0x140674f00)
```
if c ≤ 21: return 0;  if T+0xb748[id%11] == 0x20: return 0
gs = *FUN_140a3e190(ctx+0xe0,0); if gs ∉ {1,2,3,0x10}: return 0
q = FUN_1405969b0(team); if q > 21: return 0
tgt = (q.state(+0x5d0) == 5) ? FUN_140a76240(q) : ball(MA+0x2482c);  B = ballObj pos (FUN_140a57800(G+0x40))
tgt += 1.2 m along bearing(B→tgt)
X = FUN_140a90d30(P, C, B, tgt); if X.y < 0: return 0
t = FUN_140655660(ctx, id, X); if t ≥ 250 or t ≤ int(fps·0.2+.5)=11: return 0
Bt = FUN_140a6cf70(ball, t); if Bt.y > 2.2 or |X − Bt|²_xz > 64: return 0
vb = |ballVel_xz(FUN_140a57720)| / int(fps+0.5); if vb < 0: return 0
return t ≤ int(fps·0.5+0.5)=27 + |B − X|_xz / vb
```

### 21. FUN_1405a2190(ctx, id, ang) → (commit !…) true = runner's path crosses an ahead teammate's run (OBS 0x1405a2190)
```
for t in team·11..+10, active (G270+0x1748), ≠ id, FUN_1405a4830(t,0,1), s·t.x ≥ s·P.x:
    σ = sgn(ad(T.dir, MA[0x23f7c + (t·22 + id)·4]))
    a1 = fold360(ang − 20σ); a2 = fold360(bearing(t→FUN_140a76240(t)) + 20σ)
    if FUN_140a90d80(seg(P, P + 8∠a1), seg(t, t + 8∠a2)): return 1
return 0
```

### 22. FUN_1406615e0(ctx, cx, id, C) → (chance) true unless lane bends against runner's route (OBS 0x1406615e0)
```
v = pv_xyz(id); cls = info(id)+1
if !FUN_140593bd0(id) and (v < 1.2vMin or (cls == 3 and v ≤ vRun)): return 1
k = FUN_140a580a0(info(id)); if k > 21 or k ∉ {1,17,21}: return 1     # mask 0x210002
if q > 21: return 1
if |ad(q.facing, cx[2])| > 120 and id ≤ 21 and MA[0x24710 + (rank[0x247be+id] + off)·8] > 225: return 1
if c > 21 and MA[0x24844 + (rank[0x248f2+q])·8] > int(fps·0.6+.5)=32: return 1
n = route count FUN_140a6e6b0(id); if n == 0: return 1;  i = FUN_140a74160(id)
D = i < n−1 ? bearing(wp[i]→wp[i+1]) : bearing(P→wp[i])
b = bearing(P + 27·vel → C)                                             # int(fps·0.5+.5)
if v ≥ c8dc0(16) and a4c9a0(id, −3) and |ad(D,b)| ≥ 35 and !FUN_140a4e720(id, D, 36, 30, 0,1,0): return 0
if sgn(ad(T.dir,D)) ≠ sgn(ad(T.dir,b)):
    x = |ad(D,T.dir)|
    if x ≥ 3 and (x ≥ 10 or |ad(b,T.dir)| ≥ 35 or |ad(b,D)| ≥ 45):
        if |ad(b,hdg(id))| ≥ 35 or |ad(b,q.facing)| ≥ 45 or |ad(b, info(id)+0x124)| ≥ 45:
            return |ad(D,b)| < 35
return 1
```

### 23. FUN_1406627c0(ctx, cx, id, C) → (chance) true unless lane turns opposite to current heading (OBS 0x1406627c0)
```
if !FUN_1405a7fe0(ctx,id,1): return 1
if pv_xyz(id) < 0.8vMin: return 1
a = ad(T.dir, hdg(id)); if |a| < 12: return 1
b = bearing(P + 16·vel → C)                                             # int(fps·0.3+.5)
return sgn(a) == sgn(ad(T.dir, b))
```

### 24. FUN_14066d720(ctx, id, C, cx, flags) → true unless far lane in open play (OBS 0x14066d720, raw-checked)
```
if FUN_140a50250(ctx,team,1) > 0 or FUN_140a50250(ctx,team,2) > 0: return 1
if |ball.z| < F7 and s·ball.x > F6 − 5: return 1
if cx[5] ≤ 1024 and FUN_140a3e840(ctx+0xe0,0) ≥ int(3fps+.5)=162 and c ≤ 21
   and |ad(c.facing, cx[3])| ≤ 75 and !FUN_140a4e720(ctx, c, cx[3], 120, 7, 0,1,0): return 1
if (cx[5] > 529 and flags[1]) or ((a4c720(ball.x, P.x, s, 10) or |P.z − C.z| > 20) and cx[5] > 729): return 0
return 1
```

### 25. FUN_14066d980(ctx, id, C, cx) → true unless wide-ball situation and C outside box width (OBS 0x14066d980)
```
if FUN_140a50250(team,0) ≤ 0 or FUN_140a50250(team,1) > 0 or FUN_140a50250(team,2) > 0: return 1
if |ball.z| < F7 + 2 or s·ball.x < F6 − 8: return 1
if sgn(C.z) ≠ sgn(ball.z): return 1
return |C.z| < F5
```

## Open sub-functions
FUN_140a50250 (team counter, kind 0/1/2), FUN_140694ee0, FUN_140593430(…,0x7e), FUN_140a53da0, FUN_14059a1f0 (sector value, §4),
FUN_1405a2850, FUN_140598350, FUN_140a4d2c0, FUN_1404c8ea0, FUN_140439fe0, FUN_1404f6700, FUN_1404f6110, FUN_140a6e320,
FUN_1406405c0, FUN_140a64030, FUN_140a62850 (marker), FUN_140a638c0, FUN_140a3e190/3e840 (game-state / frames),
FUN_140655660 (frames-to-reach), FUN_1405997f0, FUN_140593bd0, FUN_1405a7fe0, FUN_140a8f7c0 (move pt along bearing, INF high),
FUN_140a90d80/90d30 (segment intersect / projection, INF high), FUN_140a6cf70 (ball pos at t), FUN_140a57720/57800,
FUN_140a74160/6e6b0/76210 (route idx/count/wp). 5a3080 angle-widen sub-branch (0x1405a3afe–0x1405a3c34) not fully decoded.
