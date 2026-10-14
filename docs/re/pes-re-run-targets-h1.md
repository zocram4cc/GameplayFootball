# Batch h1 — shared helpers (static, decomp + raw spot-checks)

Conventions: G=*(ctx+0x18); team(pid)= pid in [11,21] ? 1 : 0 (pid<0 →0); pl(id)=a48a10(game,id); tm(t)=a48a30(game,t); P=pl+0x55c (x,+0x564 z); V=pl+0x574/+0x57c; s=(float)(char)tm+0x254; Pred(pl,n)=P+n·V'. Angles: bearing = atan2(dx,dz) [a92d40] in rad → wrap to [0,2π) → ×57.2958 deg. `angdiff(c,a)`=0x140481b00: wrap a to [0,360), d=a-c, if |d|>180 d -= sign(d)·360, return |d| (OBS raw). `a4c4e0(&w,out,k)`: out=wrap360(wrap360(w)/k) (OBS raw) → used with k=2 = half-width.

## 1. 0x140a4e720 — opponentsInCone(ctx, pid, centerDeg, widthDeg, radius, frames:u8, includeInactive:u8, minDist) → u8 count
OBS raw: wraps centerDeg (xmm2) and widthDeg (xmm3) into [0,360), then `return a4e3f0(ctx,pid,&center,&width,radius,frames,includeInactive,minDist)`. True = ≥1 opponent in cone/annulus (see 2).

## 2. 0x140a4e3f0 — core of #1
```
if pid>0x15 return 0; t=team(pid); me=pl(pid); if !me return 0
m = me.P + frames*me.V   (raw V +0x574/+0x57c, frames cast u8→float)
opp = a456ea0(&t)= (t==0)?1:0 (OBS raw); half = a4c4e0(&width,_,2.0)
for i in 0..10: id=opp*11+i
  if !includeInactive: if tm(t)[0x4bd4+4i]==0 continue   // NOTE index uses pid's OWN team rec (OBS decomp, INF(med) possibly a decomp quirk)
  o=pl(id); q=o.P+frames*o.V; d2=|q-m|²
  if minDist² < d2 < radius²: b = (q==m)?0:deg(atan2(q.x-m.x, q.z-m.z))
      if angdiff(center,b) <= half: count++
return count
```
Constants: 360 @0x14259858c, 2π @0x1425795e4, 57.2958 @0x14259be40, 2.0 @0x142565604.

## 3. 0x140a4fea0 — playersInConeAroundPoint(ctx, teamSel, pos*, centerDeg, widthDeg, radius, minDist, includeInactive) → u8
OBS raw: wraps centerDeg to [0,360); calls `a4fbc0(ctx, teamSel, pos, &center, &width(stack copy), radius, minDist, includeInactive, frames=0)`.

## 4. 0x140a4fbc0 — core of #3
```
for t in 0,1: if teamSel==2 || t==teamSel:
  for i in 0..10: id=t*11+i
    if !game[+0x270]->[0x1748+id] continue        // slot on pitch
    if !includeInactive && tm(t)[0x4bd4+4i]==0 continue
    v = a6c6d0(pl(id), 0.694444/a1e532d0())     // velocity, dt arg (open)
    q = P + frames*v ; d2=|q-pos|²
    if minDist² < d2 < radius²: b=bearing(pos→q) deg
       if angdiff(center,b) < width*0.5: count++   // strict <, uses raw width/2 (no wrap) — differs from #2's <=
return count
```
Consts 0.694444 @0x14259bdd0, 0.5 @0x142565600.

## 5. 0x140a4c9a0 — isBeyondOwnLineX(ctx, pid, off) → bool
`t=team(pid); gx=G[0x44 + (t^1)*0x14]; return a4c720(gx, P(pid).x, s(t), off)` i.e. (s·off+gx)·s < s·Px. True = player is further (in team attack direction s) than line gx+s·off. INF: G+0x44+k*0x14 = goal-line x of team k's half → (t^1) = opponent goal (attacking end).

## 6. 0x140a4e250 — opponentsNear(ctx, pid, radius, frames:u8) → u8 count
```
if pid>=22 return 0; me=pl(pid); vm=a6c6d0(me,0.694444/a1e532d0())
for id in opp(t)*11 .. +10: v=a6c6d0(pl(id),...)
   if |(me.P+f*vm) - (o.P+f*v)|² < radius² count++
```
(no on-pitch/active check). True = any opponent within radius at predicted time.

## 7. 0x1405951b0 — isBeyondLineX2(ctx, pid, off) → bool
`t=team(pid); gx=a599d90(ctx,t) [open]; return (s·off+gx)·s < s·P(pid).x`. Same as #5 with line from 599d90 (INF: defensive/offside line for team t).

## 8. 0x140a50250 — holderAttr(ctx, team, k) → u32
```
id=G[0x2601c] (a627e0(ctx,0)); if id>=22: id=tm(team)+0x78; if id>=22 return 0
if team(id)!=team return 0
rec=a1b59e20(a3af90(a489d0(game,team), id)); return k<3 ? rec[0xdc+4k] : 0
```
Holder = ball owner (G+0x2601c) else team's fallback player tm+0x78. Field = per-player data dword (open: 3-entry array @+0xdc).

## 9. 0x140a63950 — hasPairEntry(ctx, team, a, b) → bool
`if a<17 && team<2: for i<4: if G[0x25cf8+(team*4+i)*4]==a && G[0x25d38+(team*4+i)*4]==b return 1; return 0`. 4-slot (a,b) table per team.

## 10. 0x1405a4830 — roleCheck(ctx, pid, c, d) → bool
```
role = tm(team(pid))[0xa2a0 + 4*(pid%11)]   (0 if pid>=22)
st = pl(pid)+0x5d0; if st-4<2 (4,5) || st==0x59 return 0
return a5a4790(ctx, role, c, d, a580a0(a489f0(game,pid)))   // open
```

## 11. 0x1405969b0 — focusPlayer(ctx, team) → id or 0xff
```
g=game+0x270
if g[0x1554]==5 && g[0x1558]==team: for j in 0,1: id=g[0x173c+4j]; if id<22 && a6e320((char)a48910(game,id)[1]) return id
id=G[0x2601c]; if id<22 && team(id)==team return id    // ball owner
id=G[0x2483c]; if id<22 && team(id)==team return id    // INF: last toucher/controller
x=tm(team)+0x78; return x<22 ? x : 0xff
```

## 12. 0x140a8f7c0 — addPolar(vec*, angDeg*, len)
`v.x += a92c80(ang,len); v.z += a92c80(ang+90,len)`. INF(high): a92c80 = len·sin(wrap360(ang)) → x+=len·sin, z+=len·cos (matches atan2(dx,dz) bearings).

## 13. 0x140a90d30 — segIntersectXZ(out, a0,a1,b0,b1) → out
OBS raw: out={0,0,0}; hit=a90d80(out,a0,a1,b0,b1); out.y = hit ? 0 : -1.0. So y=-1 flags "no intersection"; x/z = intersection point.

## 14. 0x140a90d80 — segIntersect(out,a0,a1,b0,b1) → bool
```
if !a900e0(a0,a1,b0,b1) return 0           // open: segment-overlap test
dA=a1-a0, dB=b1-b0 (xz); den = dA.x*dB.z - dB.x*dA.z
if |den| < 1.1920929e-7 return 0
t = (dB.z*(b0.x-a0.x) - dB.x*(b0.z-a0.z))/den
out = a0 + t*(a1-a0)  (x,y,z)  ; return 1
```

## 15. 0x140a64030 — roleHasBit(id, bit, x, teamRec) → bool
`r=teamRec[0x4c00+4*(id%11)] (id>=22→idx0); m=1<<bit; return (a3b110(x,idx)[0x48] & m) && (tbl_142613d20[r] & m)`.

## 16. 0x140a62180 — teamTacticFlag(ctx, team, k) → u8
`if k<7 && team<2: rec=tm(team)+0x8e08; mode=*(int*)rec (arg 3 = current); return mode<=2 ? rec[0x13+mode*7+k] : 0` (OBS raw 0x1408c5030). 7 byte flags per tactic set (3 sets).

## 17. 0x140a4c750 — inPitch(ctx, p*) → bool
`return |p.x| <= F0 && |p.z| <= F1` (F0=52.56, F1=34.06, table *(game+0x2aa8)).

## 18. 0x14069cd40 — laneInterceptDist(A,B,C,D,speed*,flag) → float (−1 = cannot cut / not threat)
```
u=(B-A)/|B-A| (xz; unnormalized if 0)
along(P)=(P-A)·u ; lat(P)=cross: -u.z*(P.x-A.x)+u.x*(P.z-A.z)
aD=along(D), lD=lat(D)
if aD<0 && |aD|>0.5 return -1                  // D behind A
if flag:
  if |A-B|² < |D-A|² && |B-C|² < |B-D|² return -1   // D beyond B, C closer to B
  lC=lat(C); da=wrap180(ang(A→B)-ang(A→C)) (a439e50 = bearing deg)
  if (|da|<20 || lC<0.7) && along(C) < aD:
     if |lD-lC|<1.5 return -1
     if |lC|<|lD| return -1                    // C shields lane before D
if |lD|<=0.5 return |lD|
thr = fps*(100/(fps*17.7))*(aD/ *speed) + 0.6  = 5.6497*(aD/speed)+0.6   (fps=a1e532d0, cancels)
return |lD|>thr ? -1 : |lD|
```
Meaning INF(med): A=ball/passer, B=target, D=defender, C=other player; returns D's perpendicular distance to lane if D can reach it before ball arrives (reach 5.65 m/s·t + 0.6). Consts: 0.5 @0x142565600, 20 @0x14259be1c, 0.7 @0x14257a194, 1.5 @0x14259bddc, 100 @0x14253ee30, 17.7 @0x1425cf4f0, 0.6 @0x14257a190, -1 @0x142588218, 180/360.

## 19. 0x140a627e0 — playerIdFromSlot(ctx, n) → id/0xff
n==0 → G[0x2601c] (ball owner). Else a3e7d0(ctx+0xe0,&out,n) → (team,idx); team<2 → a3b320(a489d0(game,team), idx) else 0xff.

## 20. 0x140a63020 — teamFocusId(ctx, team) → id/0xff
Same as #11 without the g[0x1554]==5 set-piece branch: owner G+0x2601c if on team, else G+0x2483c if on team, else tm(team)+0x78 (<22) else 0xff.

## 21. 0x140a62850 — idForRole(ctx, roleId) → id/0xff
```
k = team(roleId) ^ 1 ; rt = tm(k)   (note: roleId compared as value from rt+0x558[i])
for i 0..10: id=k*11+i; if on-pitch(game+0x270)[0x1748+id] && rt[0x558+4i]==roleId return id
return 0xff
```
INF(low): arg is a pid-like value whose team half selects the opposite team; rt+0x558[i] = per-slot marking target/assigned id → returns the opponent slot assigned to arg (who marks pid).

## 22. 0x140a62960 — slotWithRole58(ctx, team) → id/0xff
first i<11 with tm(team)[0xa2a0+4i]==0x3a → team*11+i else 0xff. (role code 0x3a; INF: GK).

## 23. 0x1405a5240 — receiverFreeAndFacing(ctx, team) → bool
```
id=a5969b0(ctx,team); if id>21 return 0
if g[0x1554]!=1 && g[0x11d4]==2 return 1
if a4e250(ctx,id,1.0,0) return 0                         // opponent within 1 m
dir = a4f210(s) = s==-1 ? 270 : 90 (OBS raw)            // attack direction deg
if a4e720(ctx,id,dir,120,4.5,frames=0,includeInactive=1,min=0) return 0  // opponent in 120° cone r 4.5
f = wrap360(a458740(pl(id), 0.694444/fps))               // INF facing deg
return |wrap180(f-dir)| < 60
```
True = focus player has no close opponent and faces within 60° of attack direction.

## 24. 0x1405a5eb0 — nearOppGoalBox(ctx, pid) → bool
```
t=team(pid); s; gx=G[0x44+(t^1)*0x14]
zone=a4f1e0(&P,s): v=s*P.x; v<=-28 →2, v<22 →1, else 0 (OBS raw)
if zone==0 && a4c720(gx,P.x,s,-10) return 1
if a4c720(gx,ball.x(G+0x2482c),s,-12) && a4c720(gx,P.x,s,-7) && |ball.z(G+0x24834)-P.z|<14 return 1
return 0
```
Consts -10 @0x14259be84, -12 @0x14259d664, -7 @0x14259be80, 14 @0x1425c48b8.

## Open sub-functions
a599d90 (line x for team), a6c6d0 (velocity at dt), a1e532d0 (fps/scale getter), a5a4790, a580a0, a6e320, a3b110, a3af90/b59e20 (+0xdc array), a3e7d0, a3b320, a900e0 (segment overlap pretest), a439e50 (bearing deg), a458740 (facing), a92c80 (polar component).
