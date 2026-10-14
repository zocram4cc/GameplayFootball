# Run targets — leaf batch b3 (Chance-run lane commit leaves)

Source: `/mnt/Extra4/gf-RunTargetsRE-scratch/LeafB3/b3a.c` (decomp, annot), raw via `lin.py`. All OBS unless tagged.
Conventions (§0/§4–5 of `pes-re-run-targets.md`): args `(ctx, cx, id, C, …)`; `cx[0]` team, `[1]` brg(P→C), `[2]` brg(ball→C),
`[3]` brg(ball→599ff0 pt), `[4]` |P−C|², `[5]` |ball−C|², `[6]` |P−ball|², `[7]` arg. P = id pos, ball = MA+0x2482c,
T = team block (a48a30), `T.dir` = T+0x250, s = T+0x254. `adiff(a,b)` = FUN_14043a0a0 (signed deg diff; always used as |·|
unless noted), `brg(a,b)` = FUN_140439ef0 bearing a→b (deg), `atan2deg(dx,dz)` = FUN_140a92d40·57.2958 wrapped to [0,360).
`a4c720(a,b,s,k)` = (s·k+a)·s < s·b. `lineX0` = MA+0x44+0x14·opp (raw offside line; FUN_140599d90 = adjusted lineX).
`q` = FUN_1405969b0(ctx,team) (receiver/carrier slot, valid < 0x16). `c` = FUN_140a627e0(ctx,0) (carrier, valid < 0x16).
`D2(q)` = `*(MA+0x24710 + 8·(MA+0x247be[q] + (q≥11 ? 11 : 0)))` (OBS inline; = FUN_1404c8e60(MA,q) INF(high), per-player
dist²-to-ball table INF(med)). `opp(t)` = t<2 ? t^1 : 0. `active(j)` = MI+0x1748[j] ≠ 0 (MI = G+0x270).
`fps` = FUN_141e532d0 = 54. Return value: **true = lane passes** (leaf is a "require"), false = reject.

---

### 1. FUN_14066f450(ctx, cx, id, C) — "pinned slot keeps lane inside the box width" (0x14066f450)
```
T = team(cx[0]); if !T+0xb8a2 or (id ≠ T+0x4b40 and id ≠ T+0x4b44): return true
if |P.z| < F5 and |adiff(T.dir, cx[1])| < 10: return true
sb = sign(ball.z − P.z) (<0 → −1), sc = sign(C.z − P.z) (≥0 → +1)
if sb == sc:
   far = FUN_140a8f7c0(P, fold360(cx[1]), 300)            # point 300 m along bearing (INF)
   hit = FUN_140a90d30(P, far, (s·F4,0,100), (s·F4,0,−100)) # segment vs line x = s·F4 (INF)
   if hit.t (out+4) < 0: return false;  z = hit.z (out+8)
else z = C.z
return |z| < F5
```

### 2. FUN_140662a10(ctx, cx, id, C, p5, p6, p7, p8) — "near-own-goal ball lane radius" (0x140662a10)
```
reg = a4f1e0(ball, s); if !((a62180(ctx,team,1)==1 or reg==0) and a50250(ctx,team,2) < 1 and !p8 and p5): return true
if s·ball.x > 5: return true
if |adiff(T.dir, brg(ball, P))| ≥ 45:
    lst = a714e0(MA, s); if (id > 21 or lst[0xb2+id] < 2) and s·ball.x > −10: return true
    near = false
else near = true
w1 = ramp(s·ball.x, −5 → 5)  (0 at ≤−5, 1 at ≥5, (x+5)/10)
lx = MA+0x44+0x14·opp(cx[0]) (=lineX0)
if !near and a4c720(lx, ball.x, s, −5): return true              # raw 0x140662bf3 (xmm1=ball.x, xmm3=−5 carried, INF(high))
d = |lx − ball.x|; w2 = d ≤ 5 ? 1 : d < 12 ? (d−12)/−7 : 0;  w = max(w1, w2)
R = 8w + 15 + (p7 ? 19 : 0)
if FUN_140674ce0(ctx,id,cx): R = max(R, 26)
if a41b30(T,5,id%11) and !a41b80(T,5,id): R = max(R, 35)
if FUN_140a64030(id,0,a489d0(team),T): R = max(R, 25)
return cx[5] > R²
```
`a4c720` 4th arg: register-carried −5 (loaded 0x140662b49, no intervening call on the fall-through path).

### 3. FUN_140663e70(ctx, id, C) — "no teammate already there" (0x140663e70)
```
t = team(id); if a4f1e0(C, s) ≠ 0: return true
w = ramp(|C.z|, F5 → F7); dz = 4w + 4
R = |P − C| (xz) + 8
skip = T+0xb8e8 ? T+0xb8d4 : 0xff
for j in team t slots (11·t … +10), active, j ≠ id, j ≠ skip:
   if !FUN_1405a4790(ctx, T+0xa2a0[j%11], 0,1,0): continue
   if a4c720(j.x, P.x, s, 2): continue                          # j not ahead of P by > 2
   R0 = FUN_140596c30(ctx) (route end pt, INF)
   if |j − R0|² ≤ R² and |R0.z − C.z| ≤ dz: return false
return true
```

### 4. FUN_140676530(ctx, id, C) — "no opponent in the ball→C cone" (0x140676530)
```
q = 5969b0(); if q > 21: return false
n = FUN_1405997f0(ctx,id,q) (frames); b = brg(ball, C)
for o in opp(team(id)) slots with MI+0x1748[o] == 0:              # raw 0x140676679: cmp byte,0; setne; jne → skip
   d2 = |o − ball|²; if d2 < 2.25: return false
   if d2 ≤ 16 and |fold180(b − brg(ball,o))| ≤ 45: return false
for same o: o' = o.pos + n·o.vel (0x574/0x57c)
   if |ball − o'|² ≤ 16 and |fold180(b − brg(ball,o'))| ≤ 45: return false
return true
```
OBS raw (0x140676670–87): slots with the 0x1748 byte **set** are skipped in both loops — polarity is the reverse of the
other leaves' `active(j)`. Meaning of that byte here INF(low); port literally.

### 5. FUN_14066f6e0(ctx, cx, id, C) — "carrier can see/feed the run" (0x14066f6e0)
```
c = 627e0 or MA+0x2483c; if c > 21 or FUN_1405a0d20(ctx,id): return true
if a4f1e0(P, s) ≠ 0: return true
if a3e840(ctx+0xe0,0) > int(2·fps+0.5)=108: return true
if !(144 < cx[6] ≤ 729): return true
if MA+0x22e4c+200·id+0x58·opp(cx[0])+4 ≤ 2.25: return true     # nearest-opp dist² list, INF
if D2(c) < 16 and a4c720(ball.x, P.x, s, 0):
   if !a4e720(ctx,c,T.dir,90,5,0,1,0) and !a4e720(ctx,c,T.dir,140,1,0,1,0): return true
cdir = a6e6b0(c) ? brg(c, a76240(c)) : *FUN_1404ab8e0(c)
if |adiff(439fe0(MA,c,id), cdir)| > 45: return true
if !a4c9a0(ctx,id,−8) and a4c720(ball.x, P.x, s, −1): goto mate
if !a4e720(ctx,id,cx[1],80,20,0,0,0):
   if no teammate j (active) with |j − C|² < cx[4]: return true
for 11 slots k of opp(cx[0]) with T'+0xc48+0x3fb8+4k ≠ 0 (marker field):
   o: if |ball − o|² ≥ 6.25 and FUN_14069cd40(ball, P, P, o, 18.06 /*0x419071c8*/, 1) ≥ 0: return true
mate: for j in team cx[0], active, j ∉ {id, c}, |j − ball|² < 400:
   if |adiff(439fe0(MA,c,j), cdir)| ≤ 45: return true
return false
```

### 6. FUN_140670230(ctx, cx, id, C, flags) — "receiver-side gate (rec+0xe8)" (0x140670230)
```
if flags[0] or (flags[3] and flags[4]) or (flags[1] and cx[6] < 100) or !FUN_140a534a0(ctx,id) or cx[6] > 400: return true
t = team(id); if |adiff(T.dir, brg(ball, P))| > 35: return true
if s·P.x > F6: return true
if a4c720(lineX0(t), P.x, s, −1): return true
if !a4c720(FUN_140a513a0(ctx,cx[0]), P.x, s, 0): return true
e = MA+0x22e4c+200·id; if e[22·opp] < 22 and e[0x58·opp+4] < 3.24: return true
c = 627e0
if c ≤ 21:
   if !FUN_140a4fea0(ctx, opp, ball, T.dir, 130, 7.5, 0, 1): return true
   if |adiff(c.facing, brg(ball,P))| > 95: return true
return |P − ball|² < 42.25
```

### 7. FUN_14066fd10(ctx, cx, id, C, flags) — "line-break run is open" (0x14066fd10)
```
if flags[3] or a50250(ctx,cx[0],1) > 0 or a50250(ctx,cx[0],2) > 0: return true
if !a4c720(ball.x, P.x, s, 7): return true                        # raw 0x14066fdbb
q = 5969b0(cx[0])
if q ≤ 21 and !flags[1] and cx[5] < 225 and |adiff(q.facing, cx[2])| < 60: return true   # raw 0x14066fe52
d2 = |ball − P|²; if d2 < 72.25: return true
if |adiff(T.dir, brg(ball,P))| > 55: return true
for k in 0..10 of a714e0(MA,s) list (team cx[0]): j = slot; stop if j > 21
   if j ∈ {id,q} or FUN_1405a4830(ctx,j,0,1): continue
   if s·j.x < s·ball.x: break
   if FUN_140a53da0(ctx, j, ball, T.dir, 60, 30, 3): return true
lineX = 599d90(ctx, cx[0]); v = 100/(fps·22.5)
for u in 0..4:
   Q = FUN_140a75740(P_player, …, frames = int(.5fps+.5)·u + int(.5fps+.5), P, v)   # predicted point (INF)
   if |ball − Q|² ≤ d2 and (a4c720(lineX, Q.x, s, −13) or F6 − 8 ≤ s·Q.x)
      and |adiff(T.dir, brg(ball,Q))| ≤ 55 and !FUN_140a4f720(ctx, Q, cx[0]==0, 5, …): return false
return true
```

### 8. FUN_140667cb0(ctx, cx, id, C) — "close-range lane free of markers" (0x140667cb0)
```
if |C.x − ball.x| > 10 or |C.z − ball.z| > 10: return true
if a4c9a0(ctx,id,−5) and cx[4] < 49 and cx[5] > 25 and cx[5] > cx[4]: return true
if |adiff(T.dir, cx[1])| < 22: return true
b = brg(P, ball)
if sign(adiff(T.dir,cx[1])) ≠ sign(adiff(T.dir,b)): return true
if a4c9a0(ctx,id,−5) and |P.z − C.z| < 5 and (|adiff(T.dir,cx[1])| < 55 or |ball.z − C.z| > 6): return true
for k in 0..10 of opp(cx[0]) with marker field (T'+0xc48+0x3fb8+4k) ≠ 0:
   if a53da0(ctx,o,ball,T.dir,90,25,0) or a53da0(…,120,5.5,0) or a53da0(…,160,2.5,0): return true
return false
```

### 9. FUN_140661be0(ctx, cx, id, C, flags) — chance: "outruns nearest marker" (0x140661be0)
```
if flags[6]: return true
if FUN_140a58320(a489f0(id)) < 2: return true;  if a48910(id)+1 == 3: return true
m = FUN_140a62850(ctx,id) (marker); if m > 21: return true
Pp = P + int(.5fps+.5)·vel ; route = FUN_1405976d0(ctx,id,Pp,C,fold360(T.dir),10,&R) else R=C, FUN_140596560(ctx,id,R,−1)
aR = atan2deg(R − Pp); bT = brg(P, a76240(P_player)); bB = brg(P, ball); bM = brg(P, m)
cnt = FUN_140655660(ctx,id,FUN_140599ff0(ctx,id,C),0,0); cm = FUN_140655660(ctx,m,…)
if cm ≤ cnt and |P.z − m.z| ≤ 6 and sgn(adiff(T.dir,aR)) == sgn(adiff(T.dir,bM)) and a4c720(P.x, m.x, s, −2.4): return false
if !flags[0] and sgn(adiff(T.dir,aR)) == sgn(adiff(T.dir,bB)) and sgn(C.z−ball.z) == sgn(P.z−ball.z): return true
L = sgn(adiff(T.dir,bT)) == sgn(adiff(T.dir,aR)) ? 25 : 18
if |adiff(bT, aR)| ≥ L and (|m.z − P.z| ≥ 1.5 or |adiff(bM, T.dir)| ≥ 12):
   X = FUN_140a90fd0(Pp, R, m) (closest pt, INF); dR = |m − X|²; if !a4c720(m.x, X.x, s, 0): dR = |m − C|²
   if dR ≤ 42.25:
      Y = FUN_140a90fd0(P, a76240, m); dT = |m − Y|²; if !a4c720(m.x, Y.x, s, 0): dT = |m − FUN_140596c30(ctx,id)|²
      return dT < dR
return true
```

### 10. FUN_1406622d0(ctx, cx, id, C) — chance: "wide cut back inside" (0x1406622d0)
```
if a58320(a489f0(id)) < 3 or a48910(id)+1 == 3: return true
if |P.z| > F5 + 3 or |P.z − C.z| < 5: return true
if |P.x − ball.x| > 6.5 or !a4c9a0(ctx,id,−8.5): return true
Pp, R as in #9 (same 5976d0/596560 route); aR = atan2deg(R − Pp); bT = brg(P, a76240); bB = brg(P, ball)
if sgn(adiff(T.dir,aR)) == sgn(adiff(T.dir,bB)) and sgn(C.z − ball.z) == sgn(P.z − ball.z): return true
if |adiff(bT, aR)| < 32: return true
if 55 ≤ |adiff(T.dir, brg(ball,P))| ≤ 125 and 55 ≤ |adiff(T.dir, brg(ball,C))| ≤ 125: return FUN_140675320(ctx,cx,id,C)
return true
```
`FUN_140675320` (decoded, 0x140675320): true iff |C.z| ≥ F2+1, |P.z − ball.z| ≤ 12, a4c720(ball.x, P.x, s, −5),
!a4c9a0(id,−1.8), s·P.x ≥ F6−12, then a4e720(id, atan2deg(596c30pt − P), 70, 20, 0,0, 1.0) **true**,
!a4e720(id, cx[1], 44, 20, 0,0, 2.0), !FUN_140a4e250(id, 3, 0).

### 11. FUN_140666b20(ctx, cx, id, C) — chance: "crossing in front of the ball holder" (0x140666b20)
```
q = 5969b0(); if q > 21 or D2(q) > 144: return true
if |ball.z| > F5 + 5: return true
if !a4c720(lineX0, ball.x, s, −18): return true
Pp = P + int(.3fps+.5)·vel ; aT = atan2deg(a76240(P_player) − Pp)
if |adiff(T.dir, aT)| < 5: return true
aC = atan2deg(C − Pp)
if sgn(adiff(T.dir,aC)) == sgn(adiff(T.dir,aT)): return true
if sgn(tgt.z − Pp.z) == sgn(C.z − ball.z): return true
if a53da0(ctx,id,ball,T.dir,90,14,0): return false
if !a4c720(ball.x, P.x, s, 0): return true
return |ball.z − P.z| > 6
```

### 12. FUN_140671440(ctx, cx, id, C) — non-chance: "wide channel stays wide" (0x140671440)
```
if |C.z| < F7: return true
t = team(id); if |adiff(cx[2], T.dir)| > 30 or |ball.z| < F7 − 4: return true
if sgn(ball.z) ≠ sgn(C.z) or cx[6] ≤ 30.25 or (cx[6] < 144 and FUN_1405a0d20(ctx,id)): return true
if |adiff(brg(ball,P), T.dir)| > 30: return true
if !( |P.z − ball.z| ≤ 7 or |P.z − C.z| ≤ 3 or |adiff(T.dir,cx[2])| ≥ 14 or |adiff(T.dir,cx[1])| ≤ 35 ): return true
r = sqrt(cx[4]) + 4;  o-team = FUN_140456ea0(&t) (INF: opp)
for o in that team, active, |C − o|² < r²:
   if |ball.z| < |P.z| and |P.z − ball.z| > 2: return false
   if |P.z − C.z| ≥ 4 and |adiff(T.dir,cx[1])| ≥ 50 and sgn(ball.z − P.z) == sgn(C.z − P.z): return |P.z| ≤ F1 − 6
   return false
return true
```

### 13. FUN_140670cb0(ctx, cx, id, C) — non-chance (0x140670cb0)
```
if |C.z| < F7: return true
if s·ball.x < F6: return true
if |P.z| > |ball.z| + 5: return true
if sgn(P.z) == sgn(ball.z) and |P.z| ≤ F7: return |P − ball|² < 64
return true
```

### 14. FUN_140670a10(ctx, cx, id, C) — non-chance (0x140670a10)
```
if |C.z| < F2: return true
if s·ball.x < F4 − 6 or |ball.z| < F2: return true
if |P.z| > |ball.z| + 7: return true
if sgn(C.z) == sgn(ball.z): return |C.z| < |ball.z|
return true
```

### 15. FUN_140670b80(ctx, cx, id, C) — non-chance (0x140670b80)
```
x = s·ball.x; if x < F6 − 5: return true
w = ramp(x, F6−5 → F4)
zmin = F5 + 5 + w·((F5 − 2) − (F5 + 5))          # = F5+5 − 7w  (·100/100 no-op)
if |C.z| < zmin: return true
return |ball.z − C.z| < 14
```

### 16. FUN_140663c50(ctx, cx, id, C) — non-chance: "no teammate already in that lane" (0x140663c50)
```
if |adiff(T.dir, cx[1])| < 22: return true
q = 5969b0(cx[0])
for j in team cx[0], active, j ∉ {q, id}:
   if |adiff(atan2deg(C − j), cx[1])| ≤ 40 and |j − C|² < cx[4]: return false
return true
```

### 17. FUN_140668d00(ctx, cx, id, C) — non-chance: "wide run matches route" (0x140668d00)
```
if |ball.z| < F7: return true
if sgn(ball.z) ≠ sgn(P.z) or |P.z − ball.z| > 25: return true
G = FUN_140a62780(ctx, cx[0]==0) (INF: goal/aim point)
b = brg(ball, P); if |adiff(T.dir, b)| > 65: return true
aG = atan2deg(G − ball); if |adiff(b, aG)| > 22: return true
route R from P to C via 5976d0(…,fold360(T.dir),10)/596560(−1); aR = atan2deg(R − P)
return |adiff(aG, aR)| > 22
```

### 18. FUN_14066e900(ctx, id, C) — non-chance: "wing overlap not covered" (0x14066e900)
```
if |ball.z| < F7 − 6: return true
t = team(id); aC = atan2deg(C − P); v = |adiff(T.dir, aC)|; if v < 22: return true
if |C.z| > |P.z|: return true
if sgn(P.z) ≠ sgn(ball.z) (ball.z ≥ 0 → +1) or |P.z| < 8: return true
if |adiff(T.dir, brg(ball,P))| > 30: return true
if !a4c720(ball.x, P.x, s, 4): return true
if |T+0x4bd4+0xc·(id%11) − C.z| < 5: return true               # (id%11+0x647)·0xc = formation z, INF
if |P.z − ball.z| > 7 and |P.z − C.z| > 3 and |adiff(T.dir, brg(ball,C))| < 14 and v > 35: return true
list = MA + (ball.z > 0 ? 0x22cbc : 0x22d84); q = 5969b0(t)
for k in 0..10: j = list[2·(11t + k)]; if j > 21 or !active(j) or j == q: continue
   if a4c720(ball.x, j.x, s, 4) and a53da0(ctx,j,ball,T.dir,60,100,0): return j ≠ id ? true : false
return false
```

### 19. FUN_14066a880(ctx, cx, id, C) — non-chance: "receiver can play it" (0x14066a880)
```
if cx[6] < 64 and cx[5] < 64: return true
d = sqrt(cx[5]); r = ramp(d, 15 → 45)
k = −6.5 − 2.5r; if !a4c9a0(ctx,id,k): return true
if !a4c720(599d90(ctx,cx[0]), P.x, s, k): return true          # raw 0x14066a9c0
q = 5969b0; if q > 21: return true
A = 90 − 30r; if |adiff(q.facing, cx[2])| < A: return true      # raw 0x14066aa4c
if !(cx[5] > 576 and cx[6] > 400 and sgn(ball.z) ≠ sgn(C.z)):  # sgn: ≤0&≠0 → −1
   if D2(q) ≤ 20.25 and cx[5] ≤ 400 and (cx[5] ≤ 196 or (!a4c9a0(id,−1) and |adiff(T.dir,cx[3])| ≥ 35)):
      if !a4e720(ctx,q, C.z > ball.z ? 0 : 180, 80, 4, 0,1,0):
         if c > 21 and (MI+0x1198 − a3e190(ctx+0xe0,0)+0x10) < int(.4fps+.5)=22: goto L
         if FUN_1405997f0(ctx,id,q) ≤ FUN_140655660(ctx,id,C,0,0): return true
L: if cx[7] > 70 and cx[5] < 484 and (s·P.x < 20 or (|P.z| < 10 and |P.z − C.z| < 4)) and !a4e720(id, cx[1], 84, 50,0,0,0): return true
v = |adiff(q.facing, brg(q,C))|      # raw 0x14066ad3b–49 (brg(q,P) computed, discarded)
if v < A: return true
if 404c8ea0(MA,id,q) < 100 and |adiff(T.dir, 439fe0(MA,q,id))| > 45: return true
if (|adiff(T.dir, q.facing)| ≥ 65 or v ≥ 140 or |P.z − ball.z| ≥ 14 or !a4c9a0(q,−6) or !a4c9a0(id,−6))
   and (D2(q) ≥ 16 or a4e720(q, T.dir, 120, 5,0,1,0) or !a4c720(ball.x, P.x, s, 0) or a4c9a0(id,−6.5)): return false   # raw 0x14066aeb4
return true
```
(all call args above raw-resolved.)

### 20. FUN_140669940(ctx, cx, id, C, p5, p6) — non-chance: "long ball lane vs receiver" (0x140669940)
```
q = 5969b0; if q > 21 or (cx[5] < 64 and cx[6] < 42.25): return true
if |ball.x − C.x| < 5 and cx[5] < 144: return true
[same 576/400/side block + D2(q)≤20.25 / 400 / 196 / adiff(T.dir,cx[3])≥35 / a4e720(q,0|180,80,4) /
 22-frame / 5997f0 ≤ 655660 → return true  as in #19]                    # raw 0x140669b26–0x140669c4c
if cx[7] > 70 and cx[5] < 484 and (s·P.x < 20 or (|P.z| < 10 and |P.z − C.z| < 4)) and !a4e720(id,cx[1],84,50): return true
if cx[5] > 576 and |adiff(q.facing, cx[3])| > 135: return false
if !a4c9a0(ctx,id,−8.5): return true
qdir = (c > 21 and D2(q) > 6.25) ? FUN_140439f50(MA,q) : q.facing
u = |adiff(cx[3], qdir)|; if u < 90: return true
w = u > 55 ? (u < 135 ? (u−135)/−80 : 0) : 1;  R2 = (12w + 5)²
if !(cx[5] ≥ R2 or cx[6] ≥ 144 or (u ≥ 88 and p5)): return true
if !p6: return false
g = |adiff(T.dir, qdir)|
if g < 92 and u < 95 and s·P.x > s·q.x and cx[5] < 1764: return false
tg = a76240(q)
if |q − tg|² > 1.44 and |adiff(qdir, brg(q,tg))| < 110:
   h = |adiff(brg(q,tg), cx[3])|; if h < 65 or (cx[5] < R2 and h < 95): return true
if g ≤ 140: return false
if D2(q) ≥ 36: return false
for dir in (0, 180): if !a4e720(ctx,q,dir,130,3,0,1,0):
   h = |adiff(dir==0 ? 0 : 180, cx[2])|; if h < 65 or (cx[5] < R2 and h < 95): return true
return false
```

### 21. FUN_14066ca20(ctx, cx, id, C) — non-chance (0x14066ca20)
```
if !FUN_1406768e0(ctx,cx,id): return true
if |C.z| < F7 − 5: return true
if s·ball.x < F6 − 20 or |ball.z| < F7: return true
if sgn(P.z) ≠ sgn(ball.z) or |P.z| > |ball.z|: return true
if |adiff(T.dir, cx[1])| < 15: return true            # raw 0x14066cb6b–7d
return FUN_140a4fea0(ctx, cx[0]==0, ball, T.dir, 32, 100, 0, 1)
```
`FUN_1406768e0` (decoded, 0x1406768e0): r = T+0x4c00[id%11]; true if a64030(id,0,a489d0,T) and a41b30(T,6,id%11);
else if !a64030(id,1,…): true if FUN_1405a5ff0(ctx,id) or (r == 9 and !a63950(ctx,team,5,0xff) and a64030(id,2,…)); else false.

### 22. FUN_14066c920(ctx, cx, id, C) — non-chance (0x14066c920)
```
if !FUN_1406768e0(ctx,cx,id): return true
if |C.z| ≥ F7 − 5 and |P.z| ≤ |C.z|: return |adiff(T.dir, cx[1])| < 15
return true
```

### 23. FUN_14066b6e0(ctx, cx, id, C) — non-chance: "no mate ahead in the lane" (0x14066b6e0)
```
if a4c720(lineX0, ball.x, s, −15): return true
if |P.z − C.z| > 5 or |ball.z − C.z| > 8: return true
if |adiff(brg(ball,P), T.dir)| > 30 or cx[5] < 256: return true
L = a714e0(MA, s); r = sqrt(cx[5]) − 5
for k in 0..10: j = L[2·(11·opp(cx[0]) + k)]; if j > 21: return true
   if !a4c720(P.x, j.x, s, −1): return true
   if |j − C|² ≤ r² and |adiff(cx[2], brg(ball,j))| ≤ 40: return false
return true
```
(note: list indexed with `opp(cx[0])` per decomp line 2893–2898, OBS.)

### 24. FUN_14066b590(ctx, cx, id, C, flags) — non-chance (0x14066b590)
```
if !flags[1] or flags[8]: return true
if a50250(ctx,cx[0],2) ≥ 1: return true
if a4c720(lineX0, ball.x, s, −15): return true
if |adiff(cx[2], T.dir)| > 32: return true
q = 5969b0; if q > 21: return true
return !a4e720(ctx, q, cx[2], 90, 3.5, 0, 1, 0)
```

---

### Raw check: FUN_140662d60 never reads arg 6 (OBS)
Prologue 0x140662d60: 3 home-slot stores, 5 pushes, `sub rsp,0xd0` → stack args at rsp+0x120 (a5), +0x128 (a6), +0x130 (a7),
+0x138 (a8). Whole body to `ret` 0x140663434: reads only `[rsp+0x138]` (0x1406630c6), `[rsp+0x120]` (0x1406630e2),
`[rsp+0x130]` (0x140663321); no access to `+0x128`. **Arg 6 (0xc3 record) unused — confirmed.**

### Open sub-functions
FUN_140a8f7c0, FUN_140a90d30 (#1 ray/segment) · FUN_140a62180, FUN_140a50250 (team counts) · FUN_140a4f1e0 (region) ·
FUN_140a714e0 (sorted lists) · FUN_140a41b30/41b80, FUN_140a64030, FUN_140a489d0 (role/instr) · FUN_1405a4790 ·
FUN_140596c30 · FUN_140a3e840, FUN_140a3e190 · FUN_1404ab8e0, FUN_140a6e6b0 · FUN_14069cd40 (known: intercept) ·
FUN_140a534a0, FUN_140a513a0, FUN_140a4fea0 · FUN_1405a4830 · FUN_140a75740 · FUN_140a4f720 · FUN_140a58320, FUN_140a489f0,
FUN_140a48910 · FUN_140a62850 · FUN_140a90fd0 · FUN_140599ff0, FUN_140655660 · FUN_140456ea0 · FUN_140a62780 ·
FUN_1404c8ea0, FUN_140439fe0, FUN_140439f50 · FUN_1405a5ff0, FUN_140a63950 · FUN_140a4e250.
All call args flagged in decomp were raw-resolved (#2, #4, #7, #19, #21). Remaining INF items: semantic names of the
open sub-functions above, D2 meaning, #4 0x1748 byte meaning.
