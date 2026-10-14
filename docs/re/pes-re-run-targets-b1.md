# PES21 Chance-run commit 664ee0 — leaf batch b1 (static, decompile b1a.c + raw spot checks)

Notation: `T`=team block a48a30(G,team), `Pl(i)`=a48a10(G,i), `P`=Pl(id)+0x55c, `s`=T+0x254, `ball`=MA+0x2482c (x,_,z at +0,+8),
`slot`=id%11 (id<22 else 0), `state[slot]`=T+0xa2a0+4·slot, `role[slot]`=T+0x4c00+4·slot, `t`=frames in state
(ring a401c0(T+0xa2cc+0x34·slot,0): +8 − +4), `cx` = 591b70 context (`cx[0]` team … `[5]` |ball−C|², `[6]` |P−ball|²),
`C`=candidate (x,_,z), `opp`=(id in 11..21), `dev(a,b)`=|43a0a0(a,b)| angle diff (INF high), `brg(a,b)`=439ef0 bearing a→b (INF high),
`ahead(a,b,k)`=a4c720(a,b,s,k) = s·b > s·a+k. All leaves: 1 = lane OK, 0 = reject. All OBS from decompile unless noted;
constants are the folded rip values (annot), a4c720 k-args checked in raw.

1. `66f080(ctx, cx, id, C)` (OBS 0x14066f080)
   ```
   held = a41b30(T,1,slot) && !a41b80(T,1,id)
   if !(a63950(ctx,team,7,0xff) || held) return 1
   if role[slot] != 9 return 1
   if |C.z| < F2 return 0                         # 3.66
   if s·ball.x >= 7: return |ball.z| < F5           # 9.16
   return 1
   ```
2. `6697b0(ctx, cx, id, C)` (OBS 0x1406697b0)
   ```
   if !a63950(ctx,team,5,0xff) return 1
   if s·ball.x > -5 return 1                        # 0x140669827
   if !ahead(ball.x, P.x, 18) return 1              # k=18 raw 0x140669843
   cond = (sign(ball.z)!=sign(C.z) || |C.z| <= F1-8) && |ball.z| >= F7-5
   if cond: return !6405c0(ctx, T+0xc48, id, 1)
   return 1
   ```
   (sign(ball.z): 0 → +1; sign(C.z): 0 → +1.)
3. `596470(ctx, id, C)` (OBS 0x140596470)
   ```
   if !a63950(ctx, opp, 10, id) return 1
   return |T(opp)[0x5e9+slot]·12 .x? — float at T+(0x5e9+slot)*0xc  − s·C.z| < 10
   ```
   i.e. `|T+0x46ec+12·slot (float) − s·C.z| < 10`; T fetched with team=opp flag (both a48a30 calls use `opp`). INF(med): per-slot home z.
4. `66cc20(ctx, cx, id, C)` (OBS 0x14066cc20)
   ```
   if T+0xb748+4slot==0xe && state[slot]==0x1e && t <= (int)(4·fps+0.5):
       d=|ball.z − C.z|;  return !(d<6) && d<=13      # 6 ≤ d ≤ 13
   return 1
   ```
5. `661550(ctx, cx, id)` (OBS 0x140661550)
   `if state[slot]==0x26 && byte T+0x288+0x40·slot == 7: return cx[7] < 50; return 1`
6. `6705d0(ctx, id, C, cx)` (OBS 0x1406705d0)
   ```
   if 5a4790(ctx,state[slot],0,1,0) && t > 2·fpsInt(1e532e0): return 1
   if |C.z| < F7 || !5951b0(ctx,id,-4): return 1
   dir=T+0x250; bb=brg(ball,P); dz=|P.z−ball.z|
   u = dz<=10 ? 0 : dz<26 ? (dz−10)/16 : 1
   a=dev(dir,bb)
   if (a > 65−25u && dz>10) || (a>65 && cx[6]<196) || cx[6] <= 30.25 ||
      (a>35 && dz>10 && cx[5]<=576 && !5951b0(ctx,id,-2.5) && dev(dir,cx[1])<22 && !a4e720(ctx,id,dir,120,40,0,0,0)):
       return 1
   if (|P.z|<=|ball.z| || |P.z−ball.z|<=2) && |P.z−C.z|>=4 && dev(dir,brg(P,C))>=50
      && sign(ball.z−P.z)==sign(C.z−P.z):  return |P.z| <= F1−6
   return 0
   ```
7. `66af40(ctx, cx, id, C)` (OBS 0x14066af40)
   ```
   if int T+0x28c+0x40slot == 6 && t <= (int)(2.5fps+.5):
      r=a57800(G+0x40)
      if t <= (int)(0.2fps+.5) || r+4 >= 1.5: return |ball.z−C.z| < 8.5
   return 1
   ```
8. `a4fea0` — pure thunk → `FUN_140a4fbc0` (OBS 0x140a4fea0). Called (§5) as (ctx, opp, ball, cx.ballBearing, 100, 5.5); also from 66d100 as (ctx, team==0, ball, cx[2], 30, √cx[5]+10, 0,0). OPEN: a4fbc0.
9. `5a0ec0(ctx, C)` → true(=reject per §5) when carrier `c=a627e0(ctx,0)`: `c<22 && |Pl(c).xz − C.xz|² < 20.25` (OBS 0x1405a0ec0).
10. `66f200(ctx, id, C)` (OBS 0x14066f200)
    ```
    k=5969b0(ctx,opp); m = T.b8e8 ? T.b8d4 : 0xff
    for j in 0..10 (same team, active MA+0x1748+i, i≠id, i≠k, i≠m):
       if 5a4790(ctx,ringhead(j),0,1,0) && t_j <= 2·fpsInt
          && |P.z − Pj.z| <= 4 && |C−Pj|²xz <= 16: return 0
    return 1
    ```
    (note: index compare uses `lVar16 = opp·11+j` vs k/m — global ids; ringhead = *(a401c0(ring_j,1)).)
11. `6683a0(ctx, id, C)` (OBS 0x1406683a0)
    ```
    for j in 0..10, q = list507740(MA,id)[opp·11+j] (8-byte stride), active, q≠id, q≠m(b8d4):
       if 5a4790(state[q%11]) && t_q <= (int)(3fps+.5):
          if 4c8ea0(MA,id,q) > 56.25: return 1
          if |C − 596c30(ctx,q)|²xz <= 49: return 0
    return 1
    ```
12. `6693e0(ctx, id, C)` (OBS 0x1406693e0)
    ```
    if |ball.z| < F7−4 return 1
    if !( (|ball.z|>=F7 || |P−ball|²xz >= 144) && s·P.x >= 15 ) return 1
    if sign(C.z)!=sign(ball.z) || |P.z| < 2 return 1
    for teammates j (≠id, active, 5a4790(state_j)):
       z = 596c30(ctx).z  # predicted pos
       if sign(z)==sign(ball.z) && |z|<=F7 && (role[slot]==9 || |P.x−Pj.x|<8 || !a4c9a0(ctx,j,-8)): return 0
    return 1
    ```
13. `667b90(ctx, id, C)` (OBS 0x140667b90): `0` iff some active teammate j≠id with state_j==0x2f and |Pj.z − C.z| ≤ 7.5; else 1.
14. `6680e0(ctx, id, C, chance)` (OBS 0x1406680e0)
    ```
    if chance && a4c9a0(ctx,id,-7.5): return 1
    R = |P−C|xz + 5.5; k=5969b0(ctx,opp); b = T.b8e8 && id==T.b8d8
    for q in a714e0(MA,s)[opp·11+j] (8B stride), active:
       if q==id return 1
       if q==k || (b && q==T.b8d4) || 5c1620(ctx,q) || (state[q%11]==1 && T.8c1f+slot && T.8ce8==0x11): continue
       if !a4c9a0(ctx,q,-8.5) return 1
       d2=|Pq−C|²; if d2<=30.25 && d2<=R²: return 0
    return 1
    ```
15. `66ce40(ctx, cx, id, outC)` (OBS 0x14066ce40)
    ```
    if byte T+0x2a2+0x40slot != 0 return 1
    h=(int)(fps·0.5+0.5); Q = P + h·vel
    dir=wrap360(T+0x250); out=(0,0,0)
    if !5976d0(ctx,id,&Q,C,&dir,10,&out): out=C; 596560(ctx,id,&out,-1)
    a1 = deg(atan2(tgt−P)) tgt=a76240(Pl) (0 if equal); a2 = deg(atan2(out−Q))
    return dev(a1,a2) < 22
    ```
16. `676210(ctx, id)` (OBS 0x140676210) — true = flag
    ```
    role∈{7,9} && T+0x8b1c+4slot≠0 && T+0x8c2a+slot && T+0x8b48+4slot <= (int)(4fps+.5)
    && s·ball.x <= F6·0.5 && ahead(ball.x,P.x,3) (k raw 0x14067631a) && |P.z| <= |ball.z|
    && dev(brg(P,a76240(Pl)))≥125 && |P−tgt|²≥1   → true; else false
    ```
    (dev args: 43a0a0 called with brg(P,tgt) and implicit xmm0 — INF(med) vs T dir.)
17. `6749c0(ctx, id, C)` (OBS 0x1406749c0)
    ```
    k=5969b0(ctx,opp); if k>21 return 0; f=Pl(k)+0x554 facing
    if !ahead(ball.x,P.x,0) || |P.z−ball.z|>=7.5:
       if dev(f,brg(Pk,P))>120 return 0
       bc=brg(ball,C); D=|P−ball|; w = D<=15?1: D<32?(D−32)/−17 : 0
       a=dev(bc,f); if a > 25w+90 return 0
       if a>25 && dev(T.dir,f)>20 && sign(dev(T.dir,bc))≠sign(dev(T.dir,f)) && |P.z−ball.z|>24: return 0
    if |ball−C|² < 144 return 0
    return !a4c9a0(ctx,id,0)
    ```
18. `6612b0(ctx, id)` (OBS 0x1406612b0)
    ```
    if (state[slot]!=0x1e || int T+0x290+0x40slot==0) && a62180(ctx,opp,3)!=1:
       if |P.z − T[(0x647+slot)·0xc]| >= 10: return a4f1e0(ball,s)==0
    return 1
    ```
19. `66bde0(ctx, id, C, cx, prm, flags)` (OBS 0x14066bde0–0x14066c4xx) — big; skeleton:
    ```
    k=5969b0; if k>21 return 0; if T.b8e8 && T.b8d8==id return 1
    n=5975c0(ctx,opp,0,1)
    if cx[6]<225 && a4f1e0(P,s)==0 && n<3 && (pairTbl a714a0[opp·0x58] first≠k ? it : +8)==id: return 1
    if 5a4f40(ctx,id,C) return 1
    fk=Pk facing; bk=brg(Pk,a76240(Pk)); dz=dir C→? cx[2]
    strict = MA+0x24710+8·(byte MA+0x247be+k + (k≥11?11:0)) < 16 && ahead(ball.x,P.x,0) && !a4e720(ctx,k) ⇒ strong=false else strong=true
    rank = byte a714e0(MA,s)+0xb2+id
    if Pl(k)+0x5d0 && !5aa300(ctx,id) && flags[0]:
       bad = !(gameMode .11d4==2 && .116c≠1);  if a62180(ctx,opp,0)==0 && G270+0x11a0 < 4fps: bad=0
       a=dev(cx[2],fk); if a<75 || !strong: if cx[5] > (a62180(..,1)==1?576:900) bad=0
       if a62180(..,1)==0 && a4f1e0(ball)≠0 && (a<75||!strong) && cx[5]<1764 && (rank<2||a4c9a0(ctx,id)): bad=0
       if n<3: if 675a10(ctx,id,C,cx) {bad=0; near=1}; if 675580(..) bad=0; if 675800(..) goto TAIL
       if bad return 0
    TAIL:
    if n<=prm+0x18 && cx[5] < prm[0x1c]² && (dev(fk,cx[2]) < prm[0x20] || !strong) return 1
    if n<3: R=|P−C|+5;  if !a4e720(ctx,id) && (dev(cx2,fk)<75 || dev(cx2,bk)<75 || !strong)
                          && (cx[6]<900 || cx[5]<900) && !676410(ctx,id,C,-1.5,R,0,1,0): return 1
    a4f230(ctx,&b,&mask); r=role[slot]
    if b>3 || r<4:
       if n<3 && r>3 && mask>>slot&1 && cx[6]<676 && dev(cx2,fk)<90 && rank<3: return 1
       if near: if n>2 return 0; if mask bit: if dev(fk,cx2)<55 || dev(fk,brg(ball,P))<55 return 1
       if n>2 || flags[1] || cx[6]>1444 || dev(cx2,fk)>90 || s·P.x > F6−18 || dev(T.dir,cx[1])>30 || a4e720(ctx,id): return 0
    return 1
    ```
    Partial: dev() argument order and the `near`/mask branch raw-unchecked.
20. `5a2590(ctx, id, C)` → true when (OBS 0x1405a2590)
    ```
    D=|P−C|; near=5951b0(ctx,id,-4.5)
    ∃ teammate j (active, ≠id, ≠5969b0, ≠b8d4) with role_j>6 (role via T+0x4c00 table)
      and |H_j − Pj|² ≤ 64 (H_j = T+0x4b4c+12j home pt)
      and |Pj−C|² ≤ (D+10)² and (!near || |Pj−C|² ≤ D² || D ≥ 6.5)
      and |H_j − C|² < 64
    ```
21. `66d100(ctx, cx, id, C)` (OBS 0x14066d100)
    ```
    if cx[5] > 36 return 1
    if cx[6] < 12.25 return 0
    if |ball.x − C.x| < 3 return 0
    return a4fea0(ctx, cx[0]==0, ball, cx[2], 30, √cx[5]+10, 0,0) && ahead(ball.x, P.x, 0)
    ```
22. `668620(ctx, cx, id, C, f0)` (OBS 0x140668620)
    ```
    if 5a5b20(ctx,id) || a50250(ctx,team,2)>0 || a50250(ctx,team,1)>0 return 1
    if a4f1e0(P,s)!=0 return 1
    if !( |P.x−ball.x|>=8 && !ahead(P.x,ball.x,0) && !5a5670(ctx,team) ) return 1
    A=a4f210(s) attack dir
    if |P.z−ball.z|<8 && |P.x−ball.x|<10 && |fold(cx[1]−A)|>50 return 1
    if |fold(brg(ball,P)−A)|>30 return 1
    if |fold(cx[1]−A)|>50 && sign(C.z−P.z)==sign(ball.z−P.z) && 481b00(A,cx[2])<28 return 1
    if cx[6]>400 || !( (G270.1554==1 || G270.11d4!=2 || 5951b0(id,-5)) && 5951b0(id,-8) ) return 1
    if !f0 && sign(ball.z−P.z)==sign(C.z−P.z) && |C.z|>10 && |P.z−C.z|>2.5 && ahead(ball.x,P.x,8)
       && |fold(cx[1]−A)|>28 && cx[5]<484 && !5951b0(id,-4.5) && cx[4]>16 && 81<cx[6]<484
       && !a4e720(ctx,id,cx[1],44,√?+5,0,1,0)
       && no teammate q with |C−Pq|² < cx[4]:  return 1
    if dev(P.facing,cx[1]) <= 100 && a3e840(ctx+0xe0,0) >= 2·fpsInt return 1
    return 0
    ```
23. `643a30(ctx, team, id)` (OBS 0x140643a30) — returns 1 OK / 0
    ```
    c = carrier a627e0 | MA+0x2483c | T+0x78; none→0
    vb = deg-wrap(atan2(vel-pred a6c6d0(Pl(c), 0.6944/fps))); A=a4f210(s)
    k7=a63950(ctx,team,7,0xff); held = !k7 && a41b30(T,1,slot) && !a41b80(T,1,id); k=k7||held
    if !k && s·ball.x < F6 && |fold(vb−A)|>65: return 0
    if k && |ball.z|>F5 && !ahead(F?,ball.x,−15) && s·ball.x < F6−10: skip line-check else
       if s·ball.x<F6 && |ball.z|<F7: return 0
    if k: w = (s·ball.x<=10)?1 : s·ball.x<0.6F6 ? (x−0.6F6)/(10−0.6F6) : 0
          if |ball.z| < (F7−8)·w + 8: return 0
    e = a4e3f0(ctx,c,&A,80,20,0,1,0) || a4e250(ctx,c,3.5,0)
    v = a72fb0(F,0xc,ball.x,ball.z,6);  if v <= (e?1:0.8):
       x=s·ball.x
       if !k: if x < F6−5 return 0
       elif !held: if x <= 0 return 0
       elif x < F6−15 return 0
       if |ball.z| < F5+3 && (x < F4 || |ball.z| < F2): return 0
    return 1
    ```
    (a4c720 first arg there is the F-table pointer as decompiled — INF(low); raw not checked.)
24. `593e30(ctx, cx, id, C)` (OBS 0x140593e30, body to ~0x140594430)
    ```
    if state[slot]==0x1e && int T+0x290+0x40slot ∈ {2,4}: return 1
    Z = F5 + 0.8(F7−F5)                              # 17.928
    if |C.z| < Z return 1
    if dev(P.z>0 ? 0 : 180, cx[1]) > 65 return 1     # raw 0x140593f7c
    if |ball.z−C.z|<8 && !ahead(ball.x,C.x,10) return 0       # k=10 raw 0x14059403c
    A=a4f210(s); L = MA+0x44+20·(team==0)    # raw 0x140593fec, INF: opp last line x
    v0=14043c430()
    for opp-list q (a714e0(MA,s), active, ≠id, |Pq.z|≥Z):
       if !ahead(L, Pq.x, −14) break                 # raw 0x14059419a
       if sign(Pq.z)==sign(P.z) && |P.z| <= |Pq.z|:
          if 5a4830(ctx,q,0,1) || state_q==0x2f return 0
          if 481b00(A, 458740(Pq,0.6944/fps)) <= 65 && |a6c6d0 vel| >= 0.7·v0 return 0
    if ahead(s·F6, C.x, −5):
       if (sign(ball.z)≠sign(C.z) && |ball.z|>F5 && |P.z−C.z|<7)
          || ((dev(...)<35 || |ball.z|<|P.z|) && |ball.z| < Z): return 0
    return 1
    ```

## Open sub-functions (not decoded)
a4fbc0 (a4fea0 target), a63950, a41b30, a41b80, 6405c0, 5a4790, 5951b0, a4e720, a4e3f0, a4e250, a57800, 5969b0, 507740,
4c8ea0, 596c30, a4c9a0, 5c1620, 5976d0, 596560, a76240, a62180, a4f1e0, a4f210, a4f230, 5975c0, a714a0, a714e0, 5a4f40,
5aa300, 675a10, 675580, 675800, 676410, 5a5b20, a50250, 5a5670, 481b00, a3e840, a6c6d0, a72fb0, 14043c430, 458740, 5a4830.
Assumed helpers (INF high): a48a30 team, a48a10 player, 43a0a0 angle diff, 439ef0 bearing, a401c0 state ring, 1e532d0/2e0 fps f/i.
