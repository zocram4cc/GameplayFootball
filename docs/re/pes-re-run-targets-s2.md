# Run-targets stop leaves — batch s2 (static, PES 2021)

Conventions as `pes-re-run-targets.md` §0 and `-b1.md`: `id` runner, `side` = id in 11..21, `opp` = side^1, `T` = team(side),
`s` = T+0x254, `T.dir` = T+0x250 (attack bearing), `slot` = id%11, `state[slot]` = T+0xa2a0+4·slot, `sub[slot]` = T+0x28c+0x40·slot,
`t` = frames in state (ring a401c0(T+0xa2cc+0x34·slot,0): +8 − +4), `P` = pl(id)+0x55c, `ball` = MA+0x2482c, `D2` = |P−ball|²(xz),
`dev(a,b)` = 43a0a0 signed angle diff, `brg(a,b)` = 439ef0 bearing a→b, `atan2deg(v)` = 92d40(v.x,v.z) wrapped to [0,360)·57.2958
(0 when v = 0), `ahead(a,b,k)` = a4c720(a,b,s,k) = s·b > s·a + k, `c` = carrier a627e0(ctx,0), `q` = receiver 5969b0(ctx,side),
`marks(id)` = a58320(a489f0(id)) (mark-count byte), `cls(id)` = a580a0(a489f0(id)), `hdg(p)` = 458740(p,0.6944/fps),
`vel(p)` = a6c6d0(p,…,0.6944/fps), `tgt(p)` = a76240(p), `mates` = a714e0(MA,s) row side (stop at id > 21),
`dlist(a,b)` = float at MA+0x22e4c+200·a + 8·(rank_b + 11·side(b)) + 4, rank_b = byte MA+0x22e4c+200·a+0xb2+b
(per-player sorted distance list, INF(med) value is dist²; `list(a)[side][k]` = uint id at same base + 8·(11·side+k)).
Return 1 = STOP, 0 = keep (leaf semantics per §3 cascade). All OBS from decompile unless noted; a4c720/43a0a0 args raw-checked where flagged (R).

### 1. FUN_140691ef0(ctx, id) (OBS 0x140691ef0)
```
if c > 21: return 0
if dlist(id, c) > 121: return 0
G = (s·F0, 0);  if |G − ball|² ≥ 1024: return 0
gB = atan2deg(G − ball)
if |dev(T.dir, gB)| > 47 or a4e720(ctx, c, gB, 50, 4.5, 0,1,0): return 0
if marks(id) > 2 and cls(id) == 1: return 0
if D2 > 169 and list-head dist of id's opp side (dlist entry +4 of list(id)[opp][0]) > 25: return 0
Q = a75740(pl, frames=int(fps+0.5)=54, P, |vel(pl)|xyz, 0)      # predicted point
d = dev(T.dir, atan2deg(Q − P))
if |d| > 35 and |ball − Q|² > 64:
    bc = brg(c.P, tgt(c))
    if |dev(bc, T.dir)| < 15: return 0
    if sign(dev(T.dir, bc)) != sign(d): return 0
bP = brg(ball, P)
a1 = atan2deg((G.x, F2+0.5) − ball); a2 = atan2deg((G.x, −(F2+0.5)) − ball)
return bP within [min,max](a1, gB) or bP within [min,max](a2, gB)     # inclusive, no wrap handling (OBS)
```
Constants: 121 @0x1425c77d8, 1024 @0x1425a9468, 47 @0x1425cfa3c, 50 @0x14259be34, 4.5 @0x1425a1ff4, 169 @0x1425c6788,
25 @0x14259be20, 35 @0x14259a760, 64 @0x14259be44, 15 @0x14259be0c, 0.5 @0x142565600.

### 2. FUN_140692fe0(ctx, id) (OBS 0x140692fe0, R 0x140693143–0x1406931ba, 0x140693237)
```
if marks(id) > 2: return 0
if |ball.z − P.z| < 13 or |P.z| < F7 − 5: return 0
if s·P.x < F6 − 10: return 0
if q > 21: return 1
b = 439fe0(MA, q, id)
if |dev(q.facing, b)| < 55: return 0
if |dev(q.facing, T.dir)| > 135: return 0
Z = T+0x4b54+12·slot (slot point z)
if |Z| > F5 or |Z − ball.z| > 12: return 0
if !ahead(ball.x, (T+0x4b4c+12·slot).x, 3): return 0                       # R 0x140693243
if dlist(id, list(id)[opp][0]) > 196: return 0
return a4e720(ctx, q, b, 80, 43b8b0(MA,q,id) + 5, 0,1,0)
```
Constants: 13 @0x1425c5948, 5 @0x14259222c, 10 @0x142588210, 55 @0x14259be38, 135 @0x14259be60, 12 @0x14259be04,
3 @0x14258c264, 196 @0x1425c4c18, 80 @0x142596e6c.

### 3. FUN_140690b90(ctx, id) (OBS 0x140690b90)
```
if |ball.z| > F5 or s·ball.x < F6 or |P.z| < F7: return 0
r = ramp(|P.x|, F6 → F8)            # 0 below F6, 1 above F8
return |ball.z − P.z| ≥ 17 − 7·r
```
17 @0x14259d614, 7 @0x14259bdf0.

### 4. FUN_140690630(ctx, id) (OBS 0x140690630)
```
if s·ball.x < F8 or s·P.x < F6: return 0
if sign(P.z) == sign(ball.z) and |ball.z| < |P.z| and |P.z| > F5 and |P.z − ball.z| < 14:
    if ahead(s·F4, P.x, −3) or ahead(s·F4, ball.x, −3): return 1
if s·P.x < F8 and !ahead(ball.x, P.x, −8):
    tz = tgt(pl).z
    if |tz| < F5 and |tz − ball.z| < 22: return 0
z = P.z
if (|z| ≥ F7 or |z − ball.z| ≥ 20) and |ball.z − z| ≥ 10 and sign(z) == sign(ball.z): return 1
return 0
```
14 @0x1425c48b8, −3 @0x14259d654, −8 @0x1425c7ba4, 22 @0x1425c48c0, 20 @0x14259be1c, 10 @0x142588210.

### 5. FUN_140690400(ctx, id) (OBS 0x140690400)
```
if s·ball.x < F8: return 0
if |ball.z| < F5 − 1: return 0
if sign(P.z) != sign(ball.z): return 0
if |P.z| > F7: return 0
R = 596c30(ctx, id)                               # route end point
if sign(ball.z) == sign(R.z) and |ball.z| ≤ |R.z| and |P.z| ≤ |R.z|: return |R.z| ≥ F5 + 3
return 0
```

### 6. FUN_140690cf0(ctx, id) (OBS 0x140690cf0, R 0x140690e74–0x140690eca)
```
if cls(id) == 0x13: return 0
if t < int(1.8·fps+0.5)=97 or s·P.x < F6 − 2: return 0
if marks(id) ≥ 3: return 0
if |vel(pl)|xyz > 0.9·100/(fps·22.5): return 0
if D2 > 400: return 0
a4e720(ctx, id, T.dir, 200, 3.5, 0,1,0)           # result discarded (OBS)
for k in 0..10: o = 507740(MA,id)[opp·11+k]
    if o ≤ 21 and |o.z − ball.z| ≤ |P.z − ball.z| and a53da0(ctx, o, P, T.dir, 180, 3.5, 0): return 1
return 0
```
1.8 @0x14259bde0, 100 @0x14253ee30, 22.5 @0x14259d618, 0.9 @0x14258c260, 400 @0x1425a2670, 200 @0x1425a2668, 3.5 @0x14259bde8, 180 @0x142598580.

### 7. FUN_140694650(ctx, id) (OBS 0x140694650)
```
if s·P.x < F6 or |P.z| > F2 + 2: return 0
if |dev(T.dir, hdg(pl))| > 32: return 0
vMin = 0.8·100/(fps·12.9)
if |vel(pl)|xz < vMin or q > 21: return 0
if 4c8e60(MA, q) > 9: return 0                    # open
if |P.z − ball.z| < 5: return 0
if s·ball.x < F8 − 2.5 or D2 > 400: return 0
if ahead(ball.x, P.x, 1): return 0
m = 62850(ctx, id) (marker); if m > 21: return 0
a = |dev(T.dir, 439fe0(MA, id, m))|;  if a > 70: return 0
dm = 4c8ea0(MA, id, m)
if (sign(m.z − P.z) == sign(ball.z − P.z) or a ≥ 80 or dm ≤ 9) and (a ≥ 30 or dm ≤ 7.29):
    if |dev(T.dir, hdg(m))| > 32 or |vel(m)|xz < vMin: return 0
bb = brg(ball, P);  r = sqrt(D2)
if a4fea0(ctx, opp, ball, bb, 40, r, 3, 1) and !a53da0(ctx, m, ball, bb, 40, r, 3): return 0
d = P.z < ball.z ? 0 : 180
return !(a4fea0(ctx, opp, P, d, 180, 3, 0, 1) and !a53da0(ctx, m, P, d, 180, 3, 0))
```
32 @0x14259be28, 12.9 @0x14259be08, 0.8 @0x142572348, 9 @0x14259d60c, 2.5 @0x14259bde4, 70 @0x14259be4c, 80 @0x142596e6c,
30 @0x142591140, 7.29 @0x1425d023c, 40 @0x14259be2c.

### 8. FUN_140693a20(ctx, id) (OBS 0x140693a20)
```
if t < int(1.2·fps+0.5)=65 or !5951b0(ctx, id, −3.5): return 0
if sub[slot] == 0x12: return 0
R = 596c30(ctx, id)
if |P.z| < F7 + 3:
    if |P.z − ball.z| > 4 or |P.z| < F7 − 1: return 0
    if |P.z − R.z| > 4 and sign(R.z − P.z) == sign(−P.z): return 0
if |P.x − ball.x| < 8 or !ahead(ball.x, P.x, 0): return 0
if |dev(T.dir, brg(ball, P))| > 36 or !4e250(ctx, id, 8, 0): return 0
m = 62850(ctx, id); if m > 21 or !a53da0(ctx, m, P, T.dir, 200, 8, 0): return 0
dz = P.z − ball.z; dm = m.z − ball.z
if sign(dz) == sign(dm) and |dm| > |dz|: return 0
n = int(0.5·fps+0.5)=27; Pp = P + n·vel(+0x574)        # xz
if |dev(T.dir, atan2deg(R − Pp))| ≤ 45 or |R.z − m.z| ≤ 5: return !695110(ctx, id)
return 0
```
1.2 @0x14259d5d4, −3.5 @0x14259d658, 3 @0x14258c264, 4 @0x1425656c0, 8 @0x14259bdf4, 36 @0x1425a9464, 45 @0x14259be30, 5 @0x14259222c.

### 9. FUN_140693730(ctx, id, flag) (OBS 0x140693730)
```
if !flag: return 0
if t < int(2.2·fps+0.5)=119 or !5951b0(ctx, id, −2.5): return 0
if marks(id) ≥ 2 and a48910(id)[1] != 3: return 0
if D2 < 289 or |P.z| < F7 − 2: return 0
if |P.x − ball.x| < 10 or !ahead(ball.x, P.x, 0): return 0
m = 62850(ctx, id); if m > 21 or !a53da0(ctx, m, P, T.dir, 200, 8, 0): return 0
dz, dm as #8; if sign(dz) == sign(dm) and |dm| > |dz|: return 0
return !695110(ctx, id)
```
2.2 @0x1425ab130, −2.5 @0x1425c74d0, 289 @0x1425c48d8.

### 10. FUN_14068b6e0(ctx, id) (OBS 0x14068b6e0, R 0x14068b8a9)
```
if c ≤ 21: return 0                    # only with no carrier
if q > 21 or s·ball.x < F8: return 0
n = MA float at 0x24844 + 8·(byte MA[0x248f2+q] + 11·side(q))   # q's entry in ball-sorted list (INF: frames to ball)
Pp = a75740(pl, frames=(int)n, P, |vel(pl)|xyz, 0)
if |Pp.z| < F5 − 2: return 0
if sign(ball.z) != sign(Pp.z) or !ahead(ball.x, Pp.x, −0.5): return 0
return |P.z| ≥ 4
```
−0.5 @0x14258d694.

### 11. FUN_14068c680(ctx, id) (OBS 0x14068c680)
```
if 68cb80(...): return 1                                    # open (args not shown in decomp)
if t < int(2.2·fps+0.5)=119 or !a4c9a0(ctx, id, −17): return 0
R = 596c30(ctx, id)
for j in mates, j ≤ 21, j ∉ {id, q}:
    if !ahead(P.x, j.x, 6): break
    if |j.z − R.z| ≤ 8.5: return 1
Zs = T+0x46ec+12·slot (slot point z)
if |Zs| > F5: return 0
if |P.z| < F7 and |P.z − ball.z| < 20 and a4c9a0(ctx, id, −5): return 0
σ = sign(tgt(pl).z − P.z)
if |P.z| ≤ F7 − 5: return 0
for j in mates, j ≤ 21, j ∉ {id, q}:
    if sign(j.z) == sign(P.z) and sign(j.z − P.z) == σ and |P.z| ≤ |j.z| and |Zs| ≤ |T+0x46ec+12·(j%11)|:
        return a4c9a0(ctx, j, −8)
return 0
```
−17 @0x1425c7224, 6 @0x14259bdec, 8.5 @0x1425c48b4, 20 @0x14259be1c, −5 @0x14259be7c, −8 @0x1425c7ba4.

### 12. FUN_14068e660(ctx, id, flag) (OBS 0x14068e660)
```
if !flag or c > 21: return 0
if |P.z − c.z| < 17: return 0
t = tgt(c);  if |c − t|² < 4: return 0
bP = brg(c, P);  if |dev(c.facing, bP)| < 145: return 0
bt = brg(c, t);  if |dev(bt, bP)| < 125: return 0
if |vel(c)|xyz < 43c430(32.7) (=100/(fps·32.7)): return 0
if |dev(T.dir, bt)| < 100: return 0
return |P.x − ball.x| ≥ 9
```
17 @0x14259d614, 145 @0x1425cb488, 125 @0x1425c5950, 32.7 @0x1425c677c, 100 @0x14253ee30, 9 @0x14259d60c.

### 13. FUN_14068b440(ctx, id, flag) (OBS 0x14068b440)
```
if !flag or t < int(3·fps+0.5)=162: return 0
if |dev(T.dir, brg(ball, P))| > 45: return 0
if |dev(T.dir, hdg(pl))| < 95: return 0
if |dev(T.dir, brg(P, a75720(pl)))| < 95: return 0
if !a4c9a0(ctx, id, 0.2) or D2 < 400: return 0
if D2 ≤ 1369 and q ≤ 21: return |dev(q.facing, 439fe0(MA, q, id))| ≥ 65
return 1
```
95 @0x1425c594c, 0.2 @0x142598578, 1369 @0x1425c48e4, 65 @0x14259be48.

### 14. FUN_14068c050(ctx, id) (OBS 0x14068c050, R 0x14068c13f–0x14068c17c)
```
zone = a4f1e0(ball, s); k = a62180(ctx, side, 1)
if k != 1 and zone != 0: return 0
if t < int(2.5·fps+0.5)=135: return 0
return sqrt(D2) ≥ 1444            # OBS raw: sqrt then comiss vs 1444 @0x1425ce230 → practically never true
```

### 15. FUN_140693340(ctx, id) (OBS 0x140693340)
```
if T+0xb748+4·slot != 0x30: return 0
if |P.z| < F5 + 3.5 or s·P.x < F8 − 1 or !a4c9a0(ctx, id, −5): return 0
if |P.z| > |ball.z| or sign(ball.z) != sign(P.z) or !643a30(ctx, side, id): return 0
x = s·P.x
if x > F4 − 2.5 and s·ball.x > F4 − 2.5: return 1
if x < F8 + 0.4·(F4 − F8): return 0
pt = a8f7c0(origin, wrap360(T.dir), 4)        # 4 m along attack dir
a544d0(F, &pt, −2.5, 0)                        # open (pitch clamp, INF)
for k in 0..10: o = opp·11 + k
    if MI(+0x270)+0x1748[o] != 0 and role_opp[k] (team(opp)+0x4c00+4k) != 0:
        if 69cd40(ball, pt, P, o.P, &20.833334, 1) ≥ 0: return 1     # intercept
return 0
```
3.5 @0x14259bde8, 40/100 @0x14259be2c/0x14253ee30, 360 @0x14259858c, 4 @0x1425656c0, −2.5 @0x1425c74d0, 20.833334 @0x1425c7668.

### 16. FUN_140692ea0(ctx, id) (OBS 0x140692ea0)
```
x = s·ball.x;  if x < F6 − 5: return 0
r = ramp(x, F6 − 5 → F6 + 2)
if |tgt(pl).z| < F7 − (3r + 3): return 0
return |ball.z − P.z| ≥ 18
```
18 @0x14259be18.

### 17. FUN_140694bc0(ctx, id) (OBS 0x140694bc0)
```
if !a63950(ctx, side, 6, 0xff) or q > 21: return 0      # team instruction 6
if D2 > 1024: return 0
if |dev(q.facing, brg(q, P))| < 90: return 0
return D2 ≥ 576
```

### 18. FUN_140691a80(ctx, id) (OBS 0x140691a80)
```
if t < int(fps+0.5)=54: return 0
n = a50250(ctx, side, 2); i9 = a64030(id, 9, a489d0(side), T)
if (n ≥ 1 and i9) or state[slot] != 0x1d: return 0
if ahead(ball.x, P.x, 8) and |dev(brg(P, a75720(pl)), brg(ball, P))| < 45: return 1
if !ahead(ball.x, P.x, 13) or a4c9a0(ctx, id, −5): return 0
return a4e720(ctx, id, T.dir, 70, 20, 0,1,0) or a4e720(ctx, id, T.dir, 170, 2.5, 0,1,0)
```
170 @0x1425c964c.

### 19. FUN_140691d10(ctx, id) (OBS 0x140691d10, R 0x140691dbe–0x140691e04)
```
if state[slot] != 0x1d or sub[slot] != 8: return 0
if ahead((float)MA.i32[0x68 + 4·opp], ball.x, −10) and |dev(T.dir, brg(ball, P))| > 90: return 1
if t < int(0.7·fps+0.5)=38 or !a4c9a0(ctx, id, −8.5): return 0
return a4eaf0(ctx, id, brg(P, a76210(pl, 0xff)), 30, 20, 0, 1)
```
MA+0x68+4·opp: int field converted to float (INF: opponents' line x, integer copy). −10 @0x14259be84, 0.7 @0x14257a194, −8.5 @0x1425cf884.

### 20. FUN_14068fa10(ctx, id) (OBS 0x14068fa10)
```
if !5a5ff0(ctx, id) or T+0xb748+4·slot == 0xe: return 0
if sign(P.z) != sign(ball.z) (ball.z = 0 → +): return 0
if |P.z| > F7 − 5: return 0
tz = tgt(pl).z
if |P.z| > |tz| and |P.z − tz| > 4: return 0
if s·P.x < 15: return 0
if q > 21 or !643a30(ctx, side, id): return 0
if s·ball.x ≤ F6 − 10: return 0
return !a4e720(ctx, q, T.dir, 90, 40, 0,1,0)
```

### 21. FUN_14068d0b0(ctx, id, flag) (OBS 0x14068d0b0, R 0x14068d25f)
```
if !643f50(ctx, side): return 0
if s·P.x < F6 − 1 or |P.z| < F5 + 2 or |tgt(pl).z| < F5: return 0
if flag and (|P.z| < F7 or ahead(ball.x, P.x, 7) or D2 > 144): return 0
if |P.z − ball.z| < 3 or |ball.z| < |P.z| or |ball.z| < F7 + 1.5 or s·ball.x > F8: return 1
if 44e020(T+0xc060, id) < 60: return 1                   # open
r = a63020(ctx, side); if r > 21: return 1
for k in 0..10: o = list(id)[opp][k]
    if o > 21 or dlist(id, o) > 64: return 1
    if |o.z| ≤ |r.z| and ahead(r.x, o.x, −1.5) and |dev(T.dir, brg(r, o))| ≤ 95:
        if s·r.x < s·P.x and |r.x − P.x| > 6: return 1
        if a4e720(ctx, id, T.dir, 50, 50, 0,1,0): return 1
        for j in mates:
            if j > 21: return 1
            if j ∈ {id, r}: continue
            if s·j.x < F6 − 5: return 1
            if sign(P.z − ball.z) == sign(j.z − ball.z) and (state[j%11] == 0x26 or (s·j.x > F6 and |j.z| < F5)): return 0
        return 1
return 1
```
144 @0x14259d62c, 1.5 @0x14259bddc, 60 @0x14259a764, 95 @0x1425c594c, −1.5 @0x14259d650, 64 @0x14259be44, 6 @0x14259bdec, 50 @0x14259be34.

## Open sub-functions (not decoded)
a4e720, a4fea0, a53da0, a4eaf0, a4c9a0, 4e250 (a4e250), 5951b0, 596c30, 5969b0, a627e0, 62850 (marker), 439fe0, 4c8e60, 4c8ea0,
43b8b0 (known: MA distance-list reader), a75740, a75720, a76210, 695110, 68cb80, 643a30, 643f50, a544d0, a8f7c0, 69cd40 (intercept),
a50250, a64030, a489d0, a63950, a63020, a4f1e0, a62180, 5a5ff0, 44e020, 507740, a714e0, a48910.
INF: `sign()` uses −1 for <0, +1 otherwise; ball.z sign same. 691ef0's angle-between test has no 360 wrap (OBS literal).
