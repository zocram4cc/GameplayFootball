# PES 2021 stamina substitutions — `FUN_140523b60` + replacement picker

Sources: decompiles in the MatchManagementRE scratch dir (`mmc2a.c` 394–713) and the StaminaSubsRE
scratch dir (`out/a.c`, `out/b.c`); every branch below cross-checked against raw `pdis` where noted.
`obj` = `match::ai::TeamMemberChange`, `G` = `*0x1436f3dc8`, `MI = *(G+0x270)`, `S = *(G+0x2c0)` (settings),
`T = FUN_140a48a30(G, side)`, `PMS = FUN_140a489d0(G, side)`, `side = obj+8`.

## 0. Callers (OBS, rel32 scan + raw)

- `0x14052148f` — event case 2, only when `MI+0x1168 ∈ {2,4}` (raw `0x14052142c`), order 390 → 2a0 → **b60**.
- `0x140521277` — event case 6: `MI+0x116c == 1` (in play), minute (`FUN_140a6bd60`) ≠ `ev+0x14`,
  `(ev+0x14)&1 == side`, queue unlocked; order 390 → **b60** → 2a0 (stamina before tactical here). OBS raw `0x1405211f4–0x140521290`.
- INF(med): `MI+0x1168` = period id, 1 = first half, 2 = half-time break, 3 = second half, 4 = pre-ET break
  (match-management/clock docs: first half `<2`, second half 2..3, judge skips 2/6/9/10 = breaks).
  So the case-6 caller is the only one that can see period 3.

## 1. Named helpers

| VA | name | semantics | tag |
|---|---|---|---|
| `FUN_140521000` | `subsAllowed(obj)` | CPU side (or sheet `FUN_1408c4ff0 != 0`), `count(queue obj+0x20) < *(obj+0x370)+0x8d`, queue not locked | OBS |
| `FUN_140520de0` | `subMode(obj)` | pad-attached → `*(obj+0x370)+0xa268`; else byte `+0xa290`: 0→1, 1→3, ≥2→2. 0 = stamina subs off | OBS; meaning INF(med): menu "CPU substitution frequency", 1 low … 3 high |
| `S+0x18` byte | `staminaCap` | max stamina subs per match (menu/competition byte, team-ai-leftovers §4.1) | OBS read |
| `S+0x76` byte (`FUN_140a3f570`) | `lockUserPlayer` | when set, the pad-identity player is never stamina-subbed | OBS read, INF(med) name |
| `FUN_140a3e940(*(obj+0x368)+0xe0, 6)` → byte `+0xc` | `clockMinuteInPeriod` (INF(low)) | record 6 of a 0x37-entry table (stride 0x328, count `+0x1c578`, sentinel id 0x37 → value stays 0); used only as the minute gate | OBS read; meaning INF(low) |
| `T+0x4bd4 + 4·slot` (`T+0x3f8c+0xc48`) | `slotClass[slot]` | **role class 0..9** = `FUN_1408c51f0` map of PES role 0..12: GK0 CB1 LB/RB2 DMF3 CMF4 LMF/RMF5 AMF6 SS7 LWF/RWF8 CF9 (jump table `0x1408c52a4`, dumped OBS) | INF(high): the b60 switch is `cmp class-2, 7` = exactly classes 2..9 (raw `0x140523f92`); offside doc uses `==0` for keeper |
| `FUN_140520350(PMS, slot, pad)` | `onPitchEligible` | valid member id, `blk+0x1c8 == 0`, not the pad-held player (pad entry `+3` set, `+6` clear, `+0x24 < 22`) | OBS; pad clause INF(med) |
| `FUN_140a3f390(queue, roster)` | `alreadyQueued` | roster idx appears as in/out in pending queue | OBS |
| `FUN_140a61e70(ctx, side, slot, id)` | `liveStat(side·11+slot, id)` = `PMS+0x70+id` byte | OBS |
| `FUN_1408caf60(&lo,&hi,id)` | `statRange(id)` table `0x142613c60`; id 0x37 → [0,7] | OBS; id 0x37 = form/condition INF(med) |
| `FUN_1408c8f50(stats, f, 5)` | team total of match stat `f` over 40 roster blocks (`+0x64d8`, stride 0x2134) | OBS |
| `FUN_1408ca920(blk, f, 5)` | player match total of stat `f` (case 5 = sum of 8 period ints) | OBS; f0 = goals, f4 = assists INF(med) |
| `FUN_140a58550(PMS)` | `staminaPct` = cur·100/max (FNV-guarded) | OBS |
| `FUN_140a3b250` / `FUN_140a556a0(pad)` | slot member id `{s16,i32}` / pad user-player id (`pad+0x10dc`) | OBS |
| `FUN_141e53540(arr,n,8,cmp 0x142132000)` | qsort, **ascending** by float `+4` (raw `0x142132000`) | OBS |
| `FUN_14051f5f0(obj,in,out,kind)` | `enqueueSub` → false when limit/lock/queued | OBS |

## 2. `FUN_140523b60` pseudocode (OBS unless tagged)

```
if !subsAllowed(obj) or (mode = subMode(obj)) == 0: return
if MI.period(0x1168) == 1 or MI+0x1554 == 7: return          // no first-half stamina subs; 0x1554==7 restart mode
m = clockMinuteInPeriod()                                      // 0 if record absent
if period != 3: cap = S.cap                                    // breaks / ET
else switch mode:                                              // raw 0x140523c82–0x140523cef
  1: cap = m >= 25 ? cap*2/3 : 0
  2: cap = m >= 25 ? cap*2/3 : m >= 15 ? cap/3 : 0             // m>=35 stores full cap but is overwritten by the >=25 store (raw: dead store, bug-for-bug)
  3: cap = m >= 20 ? cap : m >= 10 ? cap*2/3 : 0
  else: cap = 0
// integer division (raw: *0x55555556 / *0xaaaaaaab>>1)
if cap <= obj.used(+0xc): return; remaining = cap - used

cands = []
for i in 0..10:
  if side*11+i >= 22 or !MI.onPitch(0x1748)[side*11+i]: continue
  cls = T.slotClass[i]
  if !onPitchEligible(i) or alreadyQueued(roster(i)) or cls <= 1: continue   // GK, CB never
  isUser = slotId(i) == padUserId    // both null counts as equal
  if S.lockUserPlayer and isUser: continue
  r = roster(i); form = liveStat(i, 0x37)                     // (lo,hi) = (0,7)
  if r < 40:                                                  // keep match-winners
     tg = teamTotal(goals); ta = teamTotal(assists); p = playerStats(r)
     if tg and p.goals:
        if obj+0x1c == p.goals: continue                      // obj+0x1c unknown
        if p.goals >= 0.5*clamp(tg,1,3): continue
     if ta and p.assists >= 0.5*clamp(ta,1,5): continue
  (thr, base) = by cls:                                       // mode 1 / 2 / 3
     2,5  (full-backs, side mids): thr 20/30/40, base 1000
     3,4  (DMF, CMF):              thr 20/30/40, base 2000
     6..9 (AMF, SS, WF, CF):       thr 25/40/45, base 0
     else (unreachable for 0..9):  thr  0/ 0/15, base 3000
  if isUser:                     base += 500; thr = int(thr*0.5)
  elif 3.5 <= form+0.05 and obj.byte(+0x14) <= form+1:   base += 100; thr = int(thr*0.5)
  thr = clamp(thr, 0, 50)
  pct = staminaPct(PMS(side*11+i))
  if pct <= thr: cands.push({i, base + pct})
sort cands ascending by score                                   // attackers first, then wide, then central; tiredest first in each
for k = 0; k < len(cands) and k < remaining; k++:
  in = pickReplacement(obj, cands[k].slot, kind 3, obj+0x368)
  if in != 0xff and !enqueueSub(obj, in, cands[k].slot, 3): break
```

Corrections to match-management §2.3 (OBS): the minute schedule is per **mode**, not per period (period 3 gated,
others full cap); mode 2 never reaches full cap; ×0.5 applies on the **match** (+500) path and the form path (+100),
not "when pad-ids differ"; class 1 (CB) and 0 (GK) are excluded; goal/assist protection added; sort is ascending.

## 3. Replacement picker `FUN_140521930(obj, outSlot, kind, ctx)` (OBS)

```
if outSlot > 10 or side*11+outSlot > 21: return 0xff
role = kind == 5 ? 0 : registeredRole(T+0x8e08, outSlot, f=0, plan=current)   // FUN_1408c51b0, 0..12, default 12
best = none
for b in 11 .. PMS.squadCount(+0x2eec)-1:
  r = roster(b); if r == 0xff: continue
  m = member(b); if !hasId(m) (FUN_140a3b520): continue
  if !benchEligible(FUN_140a3ac20: valid id, blk+0x1c8==0, FUN_141084130, blk+0xc0==0, blk+0xa9==0): continue
  if alreadyQueued(r): continue
  if memberId(m) == padUserId and rec.posFlag[role]: return b                    // user's player first
  c = rateBench(side, b, role)                                                  // FUN_140522970
  if c.slot == 0xff: continue
  if kind in {3,4} and !c.canPlay and !c.secTier: continue
  if kind == 5 and !c.canPlay: continue
  if best is none or better(c, best): best = c
return best ? best.slot : 0xff
```

### `FUN_140522970` — candidate record (16 B) (OBS)
`rec = FUN_141b59e20(member)+8`: `rec[0]` = main position (0..12), `rec[1+p]` = position-p flag.
Groups: GK {0}, DEF {1,2,3}, MID {4..8}, FWD {9..12}. Returns slot 0xff if side>1, b∉11..39 or null id.
- `+0` slot; `+4` `rec[0]==role`; `+5` `canPlay = rec[1+role]`; `+0xc` float `fit = blk(+0x30 sub)+0x88+role` (`FUN_1408c7580`, 0 if role>12).
- `+6` main-position tier by needed role group (main pos group → value):
  need GK: GK4 DEF3 MID2 FWD1 · need DEF: GK0 DEF3 MID2 FWD1 · need MID: GK0 DEF1 MID2 FWD1 · need FWD: GK0 DEF1 MID2 FWD3 · role>12: 0.
- `+7` secondary-flag tier (any flag set in the listed positions):
  need GK: flag0 →3, else 1..3 →2, else 4..7 →1 · need DEF: 1..3 →2, else 4..7 →1 · need MID: 4..8 →2, else 9..12 →1 ·
  need FWD: 9..12 →2, else 5..8 →1 · else 0.
- `+8` = skill bit 26 of `rec-8+0x48` **and** `MI+0x1168 ∈ 3..7` (INF(high): Super-sub skill, second half/ET only).

### Comparator `FUN_140520390(a, b)` → a better than b (OBS decompile; constants `0x14256ae1c`=1.0, `0x14259be0c`=15.0, `0x14259bdf4`=8.0)
```
if a.t6 > b.t6+1: true;  if |a.t6-b.t6| > 1: false
if a.t7 > b.t7+1: true;  if |a.t7-b.t7| > 1: false
if a.canPlay != b.canPlay: return a.canPlay
d = int(a.fit - b.fit)
if d >= 16: true;  if |d| > 15: false
if a.superSub > b.superSub or a.t7 > b.t7: true
if a.t7 < b.t7: false
if d > 8: true;  if |d| > 8: false
if a.isMain != b.isMain: return a.isMain > b.isMain
if a.canPlay != b.canPlay: return a.canPlay > b.canPlay      // equal here already
return a.fit > b.fit
```

## 4. Open (exact next steps)
- `clockMinuteInPeriod` (`FUN_140a3e940(...,6)+0xc`): INF(low). Next: decompile writers of `ctx+0x1c258+6·0x328` region / a live read of that byte in the second half vs. the on-screen clock.
- `obj+0x1c` (int compared to the player's goal total) and `obj+0x14` (byte vs form+1): readers only; next: find writers in the ctor `0x14051f5c0` / case 1 (ratings snapshot) of `0x140521090`.
- Stat ids 0 / 4 = goals / assists and 0x37 = form: INF(med) from ranges/usage; next: match-stat struct doc or a live read after a goal.
- Mode meaning (`+0xa290` menu option name): INF(med).
- Comparator was read from the decompile only (branches simple; no Denuvo-moved blocks in its body range `0x140520390–0x1405204c0`); picker loop order raw-unchecked.
