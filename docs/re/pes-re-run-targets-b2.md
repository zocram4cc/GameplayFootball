# Chance-run commit leaves — batch b2 (static, PES 2021)

Conventions as `pes-re-run-targets.md` §0. Extra (all OBS, decompiled in LeafB2 scratch `suba.c`/`s2a.c`):
- `fold(a,b)` = `FUN_14043a0a0(a,b)` = (b − a) wrapped to (−180,180]; `dev(a,b)` = |fold(a,b)|. `brg(p→q)` = `FUN_140439ef0(p,q)` deg [0,360).
- `T` = team of cx[0] (or of id's side where noted), `T.dir` = T+0x250, `s` = T+0x254, `slot` = id%11, `opp` = other team.
- `lineOpp` = MA+0x44+0x14·opp (doc §1). `a4c9a0(id,k)` = `a4c720(lineOpp, P.x, s, k)` (OBS 0x140a4c9a0).
- `A(p)` = `FUN_140a4f1e0(p,s)`: 2 if s·p.x ≤ −28; 1 if s·p.x < 22; else 0 (attacking zone). `goalDir` = `FUN_140a4f210` = 90°, 270° if s = −1.
- `instr(k)` = `FUN_140a62180(ctx,team,k)` = T+0x8e08 bit k (via 8c5030). `card(id,b)` = `FUN_140a64030(id,b,a489d0(team),T)` / `a63fe0(slot,b,…)`:
  player data (a3b110)+0x48 bit b AND role-mask table 0x142613d20[T+0x4c00[slot]] bit b. `role` = T+0x4c00[slot].
- `grp(k,slot)` = `FUN_140a41b30(T,k,slot)`: any of 5 words T+0xb8f8+(5k+j)·8 has bit slot; `a41b80(T,k,id)` same table +0xb8fc == id.
- `nOpp(id,r,n)` = `FUN_140a4e250`: # opponents with |pred_n(o) − pred_n(id)| < r (pred via a6c6d0, 0 → current).
- `spd(x)` = `FUN_14043c430(x)` = 100/(fps·x) (km/h-ish → m/frame); `vel(p)` = |a6c6d0(p, 0.6944/fps)|.
- `q` = `FUN_1405969b0(ctx,team)` (receiver, doc §3); `c` = carrier `FUN_140a627e0(ctx,0)`; `tgt(p)` = `FUN_140a76240(p)` (player's target point).
- `set2` = `FUN_140a62d90(ctx,id,0) == 0x3cc && T+0x8828[slot·8] == 2` (INF: set-piece role marker).
- Cone helpers (open): `a4e720(ctx,p,dir,ang,dist,b,b,f)` → a4e3f0; `a4fea0(ctx,team,pt,dir,ang,dist,f,b)` → a4fbc0.
Return convention: every leaf is a require-leaf: **true = lane passes** (commit requires true).

## 1. FUN_140660ea0(ctx, cx, id, C) → bool  (OBS 0x140660ea0)
```
if FUN_140a580a0(a489f0(id)) != 1: return 0                       # AI class must be 1
b1 = brg(P→tgt(id)) ; Pp = P + int(fps·0.5+0.5)=27·vel ; b2 = brg(Pp→C)
if sign(fold(T.dir,b1)) == sign(fold(T.dir,b2)) and dev(b1,b2) < 40: return 1
if sign(P.z)==sign(ball.z) and |P.z| < |ball.z| and a4c9a0(id,−15):          # OBS −15 @0x14066114c
    if |C.z| ≥ F2+2 and sign(ball.z) ≠ sign(C.z): return 0               # sign: ball.z<0 → −1; C.z≥0 → +1
    w = 1; if s·ball.x < s·P.x and |P.x−ball.x| > 7: w = d<20 ? (20−d)/13 : 0
    if |P.z − C.z| < 20w + 5: return 1
return 0
```

## 2. FUN_140660220(ctx, cx, id, C, f5, f6) → bool  (OBS, constants raw-checked r6602)
```
Az = A(P)==0 ; thr = Az ? 676 : 400
if cx[5] ≥ thr and (instr(1) or Az) and !FUN_1405a4830(ctx,id,0,1) and (!Az or dev(goalDir,cx[2]) ≤ 70): goto S2
if |P.z − C.z| < 7: return 1
S2: if sign(P.z−C.z) == sign(P.z−ball.z): return 1
  a = norm360(FUN_140a71260(MA,s)[id] + 180)
  if dev(goalDir, brg(P→ball)) < 115 and dev(goalDir,cx[1]) > 60 and |P.z−C.z| > 6.5: return 0
  if Az and !f6:
     if (!f5 or cx[6] < 100) and |P.z−C.z| < 4: return 1
     if |P.z| < 15 and dev(cx[1],goalDir) > 45 and dev(cx[1],a) > 45:
        if dev(cx[1],goalDir) > 65 or a4c720(ball.x,P.x,s,−3.5) or |C.z| > F5 or |C.z−ball.z| > 18: return 0
  for active opp o (MI+0x1748): if |o−ball|² ≤ 4 and dev(brg(ball→o), cx[2]) < 60: return 0
  if q valid:
     if dev(q.facing,goalDir) < 60 and vel(q) > 0.6·spd(22.5):
        if |P.x−ball.x| < 7 or !a4c720(ball.x,P.x,s,0):
           d = |ball.z−C.z|; if d < 14 → 1; if d < 20 and |C.x| < 10 → 1
     if dev(q.facing, cx[2]) > 90: return 0
     if !a4e720(ctx,q,goalDir,110,7,0,1,0):
        if |P.z| > F7 or cx[5] < 784 or |ball.z−C.z| < 22: return 1
  if dev(cx[1],goalDir) ≥ 24 and |P.z−ball.z| ≥ 10:
     d = |P.z−C.z|; if d < 6.5 and dev(cx[1],goalDir) < 65: return 1
     if d ≥ 4: return 0
  return 1
```

## 3. FUN_14065ff20(ctx, cx, id, C) → bool  (OBS)
```
G = same gate as §2 head but with dev(T.dir,cx[2]) ≤ 70
if !G and |P.z−C.z| < 7: return 1
if |ball.z| ≥ F7−2 and |ball.z−P.z| ≤ 18 and sign(C.z−P.z) ≠ sign(ball.z−P.z) and |C.z| ≤ |ball.z|
   and dev(T.dir, brg(P→C)) ≥ 22 and dev(cx[2],T.dir) ≤ 70 and a4c720(ball.x,P.x,s,8.5):
     if |T[+0x4b54+12·slot] − C.z| ≥ 8.5: return cx[5] < 324               # T+(0x647+slot)·0xc, INF slot home z
return 1
```

## 4. FUN_140660cc0(ctx, cx, id, C) → bool  (OBS; angle arg raw 0x140660e2c)
```
if s·P.x < F6−3: return 1
if |P.z| ≥ F5−2 and |P.z−ball.z| ≥ 3 and (sign(P.z)≠sign(ball.z) or |ball.z| ≤ |P.z|):
   if sign(C.z−P.z) ≠ sign(ball.z−P.z): return 0
   if dev(T.dir, cx[1]) ≤ 5: return |C.z| < F5−2
return 1
```

## 5. FUN_140596010(ctx, cx, id, C) → bool  (OBS)
```
if s·C.x < F6−5: return 1
if |C.z| < F5 + 0.5·(F7−F5): return 1
if q invalid: return 0
if dev(T.dir,cx[2]) < 65: return 1
if dev(q.facing,cx[2]) ≤ 90
   and (FUN_140a63950(ctx,team,1,0xff) or !instr(2) or |ball.z−C.z| ≤ 35)
   and (s·AI(id)[+0x12b0] ≤ F6−7 or |AI(id)[+0x12b8]| ≥ F7 or nOpp(id,5.5,0)): return 1   # AI = a489f0(id); +0x12b0/8 INF AI target x/z
return 0
```

## 6. FUN_14066a5a0(ctx, cx, id) → bool  (OBS; tail raw 0x14066a7d5)
```
if c invalid: return 1
if D2[id][c] > 64: return 1          # MA+0x22e4c+200·id, entry (byte[+0xb2+c] + 11·side(c))·8 +4; INF dist² table
v = vel(c); lo = spd(32.7); hi = spd(22.5)
if v < lo: return 1
w = v == lo ? 1 : (v < hi ? (v−hi)/(lo−hi) : 0); r = 5w + 4
ct = brg(c→tgt(c))
if dev(ct, MA[+0x23f7c + 4·(22c+id)]) ≤ 90 and dev(FUN_140439fe0(MA,id,c), cx[1]) ≤ 90 and dev(ct, cx[1]) ≥ 50:
    return a4e720(ctx,c,ct,60,r,0,1,0)
return 1
```

## 7. FUN_14066a200(ctx, cx, id, C) → bool  (OBS; sqrt/a4fea0 args raw 0x14066a4f2)
```
if c invalid: c = MA+0x2483c; if c invalid or side(c) ≠ cx[0]: return 1
if |P.z−C.z| < 4: return 1
if sign(P.z−ball.z) == sign(C.z−ball.z) or |ball.z| > F7: return 1
if a4c720(ball.x,P.x,s,8): return 1
if dev(cx[2], brg(ball→P)) < 45 or dev(T.dir,cx[1]) < 45: return 1
if set2 and |C.z−ball.z| < 5.5 and |C.z| < F2+2: return 1
if a4c720(ball.x,P.x,s,5) and !a4c720(lineOpp,ball.x,s,−6) and (cx[5] > cx[4] or cx[5] > 56.25)
   and a4fea0(ctx,opp,ball,cx[2],40,min(√cx[5],8),0,0): return 1
return 0
```

## 8. FUN_14066dad0(ctx, id, C, f4, f5) → bool  (OBS; ball/P a4c720 + loop raw 0x14066dd6c, 0x14066e01c)
```
if set2 or f4 or T+0xb6e0 or FUN_1405a0d20(ctx,id): return 1
if q invalid: return 1
if |P.z| > F5+1:
   if |P.z| > 18 or |ball.z−P.z| < 6 or |ball.z| < |P.z| or dev(T.dir, brg(ball→P)) > 55: return 1
if |P−ball|² < 42.25: return 1
if s·ball.x > F6−11 or |P.z−ball.z| > 22 or !a4c720(ball.x,P.x,s,8.5): return 1
if dev(T.dir, brg(ball→P)) > 65 or !a4c9a0(id,−12): return 1
if dev(T.dir, q.facing) > 55:
   if |q − tgt(q)|² < 1 or dev(T.dir, brg(q→tgt(q))) > 55: return 1
t = FUN_140a3e840(ctx+0xe0,0)                                     # INF frames counter
if t > int(2fps+0.5)=108 and !a4e720(ctx,q,T.dir,120,7.5,0,1,0): return 1
if f5: if t > int(1.5fps+0.5)=81 or |ball.z| < F7 or |ball.z| < |P.z| or |P.z−ball.z| < 3 or !a4c9a0(id,−8): return 1
if !nOpp(id,6,0): return 0
R = |P−C| + 2
for k in 0..10: o = FUN_140507740(MA,id)[opp][k]; if o ≤ 21 and a4c720(lineOpp,o.x,s,−8.5) and |C−o|² < R²: return 0
return 1
```

## 9. FUN_140675de0(ctx, id, C, cx, _, n) → bool  (OBS)
```
if n ≥ 2: return 0
if T+0xb8e8 and T+0xb8d8 == id and T+0xb8d4 ≤ 21 and |C.z − T+0xb8e4| < 7: return 1      # 'held' lane
if cx[4] > 196 or cx[5] > 484: return 0
if dev(T.dir,cx[1]) ≥ 75 and dev(T.dir,cx[2]) ≤ 20
   and !a4fea0(ctx,opp,ball,cx[2],30,√cx[5]+10,0,1) and q valid: return dev(q.facing,cx[2]) ≤ 75
return 0
```

## 10. FUN_140676010(ctx, id, _, cx) → bool  (OBS raw 0x1406760b5–0x1406761e6)
```
if role ≤ 6: return 0
if s·ball.x > 20: return 0
if a4c720(MA[+0x40+0x14·opp], ball.x, s, −17) and a4c9a0(id,−17) and |P−ball|² ≤ 100
   and c valid and !(a48910(c)[+8] bit 27) and dev(T.dir, c.facing) ≤ 85:
     return !a4e720(ctx,c,T.dir,90,4.5,0,1,0)
return 0
```
(+0x40 not +0x44 — OBS movss [rbp+rax*4+0x40]; field of the same per-team 0x14 record, meaning open.)

## 11. FUN_140a53da0(ctx, j, pt, dir, ang, maxD, minD) → bool  (OBS)
`minD < |P_j − pt| < maxD and dev(dir, brg(pt→P_j)) < ang/2` (tail-forms a53df0 with a = P_j).

## 12. FUN_140a53df0(ctx, a, b, dir, ang, maxD, minD) → bool  (OBS)
`minD < |a − b| < maxD and dev(dir, brg(b→a)) < ang/2`.

## 13. FUN_14066d250(ctx, cx, id, C, flags) → bool  (OBS; −2 call raw 0x14066d49c)
```
if a4c720(ball.x,P.x,s,2.2): return 1
if a4c720(ball.x,P.x,s,0) and |P.z−C.z| < |ball.z−C.z|: return 1
if set2 and a4c720(ball.x,P.x,s,1.2) and |C.z| < F2+2 and cx[5] > cx[4]: return 1
if s·ball.x ≤ F6−3 and a4c720(ball.x,P.x,s,−2) and !a4c720(lineOpp,ball.x,s,−4) and cx[4] ≤ cx[5]
   and (!flags[1] or dev(T.dir,cx[3]) ≥ 22)
   and (a4fea0(ctx,opp,ball,T.dir,70,7.5,0,1) or a4fea0(ctx,opp,ball,cx[2],70,7.5,0,1)): return 1
x = s·ball.x; w = x ≤ 0 ? 1 : clamp(|x−F6|/F6, 0, 1); r = 5w + 5
L = FUN_140599d90(ctx,team); e = |ball.x − L|
u = (s·ball.x < s·L and e > 3) ? (e < 12 ? (12−e)/9 : 0) : 1
return |C.z − ball.z| ≥ u·r
```

## 14. FUN_1406711d0(ctx, cx, id, C) → bool  (OBS)
```
Pp = P + 16·vel ; L = FUN_140599d90(ctx,cx[0])
if !a4c720(L,P.x,s,0) and a4c720(L,Pp.x,s,0): Pp.x = L
if dev(brg(Pp→C), T.dir) ≥ 75 and sign(C.z−ball.z) ≠ sign(P.z−ball.z):
   d = |P.z−C.z|; if d > 8: return 0; if d ≥ 5: return |C.z−ball.z| < 5.5
return 1
```

## 15. FUN_140670e20(ctx, cx, id, C) → bool  (OBS raw r670e + 0x140671100–0x140671197)
```
t = side(id); Pp = P + 16·vel ; L = FUN_140599d90(ctx, t^1)          # NB: other side's line (OBS edx = r12d^1)
if !a4c720(L,P.x,s_t,0) and a4c720(L,Pp.x,s_t,0): Pp.x = L
if dev(T.dir, brg(Pp→C)) ≥ 80 and sign(C.z−P.z) ≠ sign(ball.z−P.z):
   (p0,p1) = P.z ≤ C.z ? (ball, P) : (P, C)
   b0 = FUN_140a71560(F1,p0); b1 = FUN_140a71560(F1,p1)           # z-strip index: 2 m strips from F1−2 downward, 0..32
   if b1 − b0 > 0:
      m = 0; for b in b0..b1: v = MA[+0x25378 + 33·(t^1 or 0 if t≥2) + b]   # byte grid (a7b2c0)
         if v > m: m = v elif v < m and v < 50: return 0
return 1
```

## 16. FUN_140663750(ctx, id, C) → bool  (OBS raw 0x140663792–0x1406637c6)
```
if !(s·ball.x > F6): return 1                              # a4c720(s·F6, ball.x, s, 0)
if |ball.z| > F5+3 or |C.z| < F5: return 1
if role ∈ {2,5,8} (mask 0x124) and |ball.z−C.z| < 17: return 1
Lz = F5 + 0.8·(F7−F5); d = |ball.z − C.z|
if d ≥ 14 or (|P.z| ≤ Lz and |C.z| ≥ Lz): return 0
return 1
```

## 17. FUN_140663440(ctx, cx, id, C, f) → bool  (OBS)
```
if f or instr(2): return 1
if !FUN_1406405c0(ctx, T+0xc48, id, 0): return 1
if role == 9 or card(id,6): return 1
if |T[+0x46ec+12·slot]| < 15: return 1                      # INF slot home z (same field as §24)
if |P.z| < F7:
   if |T[+0x4b54+12·slot]| < F7−2 or |P.z−ball.z| < 10: return 1
if |C.z| ≤ |P.z| and |P.z−C.z| ≥ 3 and dev(cx[1],T.dir) ≥ 22:
   if |ball.z| < |P.z| and |ball.z−P.z| < 12: return 0
   return A(C) == 0                                          # C in attacking zone s·x ≥ 22
return 1
```

## 18. FUN_140664200(ctx, id, C) → bool  (OBS; returns low byte)
```
x = s·ball.x; d = |x − F4|
if x < F4 and d ≥ 5: return 1
w = x ≥ F4 ? 1 : (5−d)/5
dz = |C.z − ball.z|
return !(dz ≤ 4w+5 and (|C.z| ≥ F2 or dz ≤ 4))
```

## 19. FUN_140664320(ctx, id, C, cx, flags) → bool  (OBS from raw r6643 — decompile drops several a4c720 k args)
```
if MI+0x116c != 1: return 1
if |ball.z| < F5: return 1
if dev(T.dir, brg(ball→P)) < 20: return 1
if a53da0(ctx,id,ball,T.dir,75,100,0): return 1
if a4c720(ball.x,P.x,s,0) and |ball.z−C.z| > |P.z−C.z|: return 1
if a4c720(ball.x,P.x,s,8): return 1
e = |P.x−ball.x|; w = e ≤ 3 ? 1 : e ≥ 8 ? 0 : (8−e)/5
if |ball.z−C.z| > 8w: return 1
if A(ball) ≠ 0 and |ball.x−C.x| > 10 and a4c720(ball.x,P.x,s,2)
   and a4fea0(ctx,opp,ball,T.dir,60,|ball.x−C.x|+5,0,1): return 1
if (flags[0] and cx[5] > 196) or q invalid or cx[5] < 20.25: return 0
if dev(T.dir, FUN_140458740(q,0.6944/fps)) < 35 and vel(q) > 0.9·spd(22.5): return 0
cone3 = nOpp(q,1,0) or a4e720(ctx,q,T.dir,120,5,0,1,0) or a4e720(ctx,q,T.dir,70,8,0,1,0)
if cx[5] > cx[4] and a4c720(ball.x,P.x,s,0):
   d = |√cx[5] − √cx[4]|; if d > 2.5: if d > 7 or cone3: return 1
if !flags[0] and a4c720(ball.x,P.x,s,5) and !a4c720(lineOpp,ball.x,s,−7) and a4c9a0(id,−4)
   and |√cx[5] − √cx[4]| < 2.8 and cone3: return 1
if a4c720(ball.x,P.x,s,−2.5) and cx[5] > cx[4] and |P.z| > |ball.z| and |C.z| > |ball.z|
   and |C.z−ball.z| > 3.5 and |P.z| > F7 and dev(T.dir,cx[2]) > 32: return 1
if s·ball.x > F6−3: return 0
if !a4c720(ball.x,P.x,s,−2) or a4c720(lineOpp,ball.x,s,−4) or cx[4] > cx[5]: return 0
if flags[1] and dev(T.dir,cx[3]) < 22: return 0
return a4fea0(ctx,opp,ball,T.dir,70,7.5,0,1) or a4fea0(ctx,opp,ball,cx[2],70,7.5,0,1)
```

## 20. FUN_140664ca0(ctx, id, C, cx, flags) → bool  (OBS)
```
if |C.z − ball.z| > 6: return 1
if a4c720(lineOpp, ball.x, s, −14) and |P.z−C.z| ≥ 3 and q valid
   and !a4e720(ctx,q,T.dir,35,100,0,0,0) and !a4e720(ctx,q,T.dir,120,5,0,0,0)
   and (flags[1] or cx[5] ≤ cx[4] or cx[5] ≤ 25 or !a4c720(ball.x,P.x,s,6)): return 0
return 1
```

## 21. FUN_140663970(ctx, cx, id, C) → bool  (OBS)
```
if T+0x4b40 ≠ id and !set2 and !card(id,2) and !card(id,3): return 1
if |C.z| < F5+2.5: return 1
if s·P.x > F4: return 1
end = FUN_140a8f7c0(P, norm360(cx[1]), 300)                    # ray P along bearing cx[1], 300 m
hit = FUN_140a90d30(P, end, (s·F4, 0, 100), (s·F4, 0, −100))   # segment on line x = s·F4
return hit.t ≥ 0 and |hit.z| < F5+4
```

## 22. FUN_14066b980(ctx, cx, id, C) → bool  (OBS)
```
if !card(id,5) and !card(id,4) and !card(id,6): return 1
if FUN_140a50250(ctx,side(id),1) > 0: return 1                 # carrier record +0xe0 (INF)
if grp(2,slot): return 1
if |C.z| < F7−4: return 1
x = s·P.x
if x ≥ 15 and (cx[5] ≥ 225 or cx[6] ≥ 144 or |C.z| ≥ F7):
   if x > F6−10 or (dev(T.dir,cx[1]) > 32 and |P.z−C.z| > 3): return 0
   if |P.z − ball.z| ≤ 24:
      for k in 0..10: o = FUN_140a714e0(MA,s)[opp][k]
         if o > 21: return 1
         if !FUN_140a638c0(ctx,o):                              # o ≠ side's special slot (a62610; INF keeper)
            if !a4c720(P.x,o.x,s,−1.5): return 1
            if |o.z−C.z| < 7 or |o−C|² < cx[4] or a53da0(ctx,o,P,cx[1],70,100,0): return 0
      return 1
return 1
```

## 23. FUN_140667480(ctx, cx, id, C) → bool  (OBS)
```
if |C.z| < F7: return 1
if (grp(6,slot) and card(slot,0)) or (grp(7,slot) and card(slot,9)):
   for i in 0..10: j = 11·cx[0]+i; if j ≠ id and active(j) and T[+0xa2a0+4i] == 0x1e and a580a0(a489f0(j)) == 0x14:
        if sign(tgt(j).z) == sign(C.z): return 0               # sign(z) = −1 iff z < 0
return 1
```

## 24. FUN_140667310(ctx, cx, id, C) → bool  (OBS)
```
if grp(2,slot): if FUN_140a41b80(T,2,id): return 1; goto X
if !(grp(6,slot) and card(slot,1)): return 1
X: if role ≠ 9 and (role ≠ 7 or |T[+0x46ec+12·slot]| > F7): return 1
   return |C.z| < F5+5
```

## Open sub-functions
- Cone/lane tests: `FUN_140a4e3f0` (via a4e720), `FUN_140a4fbc0` (via a4fea0) — semantics (hit = opponent in cone?) not decoded.
- `FUN_1405a4830(ctx,id,0,1)`, `FUN_1405a0d20(ctx,id)`, `FUN_1406405c0(ctx,T+0xc48,id,0)`, `FUN_140a63950(ctx,team,1,0xff)`.
- `FUN_140a71260(MA,s)` (per-player angle table), `FUN_140507740(MA,id)` / `FUN_140a714e0(MA,s)` (opponent orderings),
  `FUN_140439fe0(MA,id,c)`, `FUN_140458740(p,t)` (heading at t), `FUN_140a6c6d0` (velocity), `FUN_140a3e840(ctx+0xe0,0)`.
- `FUN_140a8f7c0` (ray end), `FUN_140a90d30` (segment intersection), `FUN_140a62610` (via a638c0), `FUN_140a3b110`/`141b59e20` (player record),
  `FUN_140599d90` (doc §1, known), `FUN_1405969b0` (doc, receiver).
- Field meanings INF: T+0x46ec/0x4b54 +12·slot (z of two per-slot points), T+0x4b40, T+0x8828, T+0xa2a0, T+0xb6e0, MA+0x40+0x14·opp,
  MA+0x23f7c, MA+0x22e4c, MA+0x25378 grid, AI+0x12b0/0x12b8, MI+0x116c; `a62d90 == 0x3cc`.
