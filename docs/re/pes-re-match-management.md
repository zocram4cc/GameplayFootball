# PES 2021 in-match management (MatchManagementRE)

Scope: the CPU's in-match team management — strategy decider, attack-level
mapping, substitutions, tactical changes, time-wasting. Read-only RE.
Tags: **OBS** = seen in decompile (VA named, cross-checked vs raw where
stated) or read live via read-only pread; **INF(conf)** = inference.
No player or team names. Scratch: the MatchManagementRE scratch dir
(`strategy.c`, `goaldiff.c`, `mmc1a.c`–`mmc9a.c`, `watch.jsonl`,
`subwatch.jsonl`).

GF counterparts replaced: `src/sim/onthepitch/matchmentality.cpp`,
`substitutions.cpp`, `aimanager.cpp` (`ChoosePhilosophy`,
`ChooseFormation`, `PlanSubstitution`).

Prior reports assumed, not re-derived: tactics sheet layout, attack level
`T+0xb8a4`, strategy `T+0x134`, target word `T+0x210` (tactics report);
stamina tank/drain/A5, injury states, guarded-u32 codec (stamina-drain
report); contact severity (contact report).

## 0. Headline

- **CPU strategy decider fully decoded** (`FUN_14051e970`, OBS): reads only
  score (period-goal sums, aggregate-aware), clock minute, team-strength
  difference, match-mode word, two RNG-held minute limits. It does NOT read
  possession, stamina, or red cards (OBS by absence in the decompile).
- **Non-pad branch of `FUN_1404f8350` closed** (OBS): strategy→level map
  0→0/1→1/3→3/4→4 else 2, gated by `T+0x210` and "only while in play";
  level 5-valued, init 2.
- **Substitutions decoded** (OBS): the `match::ai::TeamMemberChange`
  object (vtable `0x1425c5fc0`, slots `0x14051f5c0`/`0x140521090`) runs
  four sub-deciders in case 2 per team tick — red-card repair,
  goal/red-card formation reshuffle, stamina refill, behind-or-level
  tactical reshuffle — plus injury/abnormal scans at case 0 and a
  6-slot queue (kinds 2/3/4/5/6). Limits come from a settings byte, not
  a hardcoded 3.
- **No GF-style 80′/85′ rules, no corner-camping, no keep-ball
  philosophy** (OBS): the only "leading late" behaviour is strategy 1
  (defensive) via the decider's minute limits; no formation switch, no
  instruction change, no time-wasting carrier logic found.
- **Live**: first half at 0–0 + half-time + second-half split watched
  (540 + 71 samples). No substitution (pct fell to ~15, nobody left);
  strategy 3/2 → 2/2 then 3/1 split in the second half; level fixed at 2
  (pad branch — watch-mode setup), plan fixed at 0. The decoded rules
  predict exactly this (§7).

## 1. The CPU strategy decider `FUN_14051e970` (OBS)

### 1.1 Object, cadence, inputs

- Object `R = brain+0xbc0`, ≥ 7 u32 words (OBS indices 0,1,3,5,6 + RNG
  state at +8): `R[0]` side, `R[1]` match mode, `R[3]` strategy
  (= `T+0x134`, copied by the team tick `FUN_1404f6c30` right before
  `FUN_1404f8350`), `R[5]`/`R[6]` minute limits, `R+8` LCG state.
- Entry `FUN_14051e910(R, ctx)`: runs only when `MI+0x116c ∈ {1,3}`
  (OBS `b4.c`: `(116c - 1) & ~3 == 0`); `e970` returns at once when it
  is 1, so **decisions happen in phase 3** (INF(med): a stoppage state),
  every team tick (caller `FUN_1404f6c30`, OBS `ds2.c`/`spp_f6c30.c`).
  `R[1]` is latched once (`FUN_14051e7a0`, §1.2).
- RNG: `FUN_140a924e0(R+8, n, 2)` = slot-2 LCG
  `s = (s·-0x2D22B2E3 + 0x4513) & 0x7fffffff`, draw `int(s·2⁻³¹·n)`
  (OBS `mmc8a.c`). All strategy randomness is draws from this slot.
- Inputs read (OBS, complete — every load in `strategy.c`):
  - goals: `FUN_1408c9df0(stats, 5)` = sum of period-goal fields 0..5
    (OBS `mmc8a.c`); goal difference via `FUN_14051e5b0` (aggregate-aware,
    OBS `goaldiff.c`/`mmc1a.c`: adds first-leg goals `FUN_140a7f0f0`
    when set up, with a bit-5/6 flag tweak of `setup+0x6284`).
  - clock: `minute = (int)clock / 3600` (`FUN_140a6bd60`, OBS: `x/0xe10`;
    clock = `FUN_140a3c050(MI+0x1168)` float seconds, INF(high)).
  - strength: `float G+0x2ad0 + 0xc404 + side·0x2804` (INF: team rating).
  - mode `R[1]`, limits `R[5]`/`R[6]`, current strategy `R[3]`.
  - NOT read: possession (`T+0x255`), stamina (`PMS+0xc8`), red cards /
    on-pitch count (`MI+0x1748`), shot counts — none appear in the
    decompile (OBS by absence; the only per-player loop in the file is
    the mode-1/2 goal-sum loop over 5 periods).

### 1.2 Match mode `R[1]` — `FUN_14051e7a0` (OBS `mmc1a.c`)

```
team = FUN_140a48a30(G, side); setup = G+0x2ad0 (+0xc400/+0x2804 per side)
if FUN_140a40bc0(team) == 1: mode = 5      // always attacking
elif == 2: mode = 6                         // always defensive
elif any of 8 records setup+0x2308+0x38·i matches (FUN_140a7d420/450): mode = 1  // INF: two-leg tie
elif setup+0xc31b: mode = 2
elif FUN_140a7f3a0 and FUN_140a7f360: mode = 3
elif FUN_140a7f3a0 and FUN_140a7f580: mode = 4
else mode = 0                               // normal match
```
Mode labels INF(low); numbers and predicates OBS.

### 1.3 Mode 0, first half (`MI+0x1168 < 2`, OBS `strategy.c`)

At minute 0 (`uVar7 == 0`):
```
d = strength(own) − strength(opp); 0.9 = 0x14258c260 (OBS float)
d > 0.9        → (w3, w1, w2) = (7, 0, 3)
d < −0.9       → (4, 4, 2)
else           → (3, 2, 5)
mode 5: w3 = 10 only; mode 6: w1 = 10 only
r = rand(w3+w1+w2):
  r < w3      → strategy 3, hold until minute 10+rand(36)   // R[5]
  r < w3+w1   → strategy 1, until minute 20+rand(26)
  else        → strategy 2                                  // no R[5] set on this path (stays 0)
(mode 5/6: until minute 100); R[6] = 28+rand(10) (mode 5/6: rand(20))
```
Before R[5]: strategy 0/1 and losing → 2 (mode 6: 1); strategy 3/4 and
winning → 2 (mode 5: 3). At/after R[5]: strategy 2. (OBS branch structure;
win/lose = `FUN_1408c9df0` comparisons.)

### 1.4 Mode 0, second half (`MI+0x1168` 2..3, OBS)

`g` = goal difference (`FUN_14051e5b0`). Before minute `R[6]` a state
machine steps the strategy toward 3 when behind (`g < 0`, `g < −1`) and
toward 1 when ahead (mode 5 biased up, mode 6 down); at/after `R[6]`:
ahead → 1 (mode 6: 0, mode 5: 3), else 3 (mode 5: 4, mode 6: 2). Extra
time (≥ 4): `g ≤ 0` → 3 (mode 5: 4, mode 6: 2), else 1. Mode 5 never
rests at 1 (→ 2), mode 6 never at 3 (→ 2). (OBS; full branch table in
`strategy.c` lines 230–330.)

### 1.5 Modes 1–4, 6 (OBS)

- Mode 1 (two-leg): compares goals plus first-leg goals, picks 1/2/3 by
  who leads (OBS lines 40–110; exact aggregate rule in `strategy.c`).
- Mode 2: strategy 3; if any period played (`FUN_1408c71b0` ≥ 1): own
  goals < opp goals + 1 → stay 3 else 2... (OBS lines 110–130; the
  "need one more" rule, INF(med)).
- Modes 3/4: first-leg-aware 1/2/3 picks (OBS lines 330–465).
- `*side ≥ 2` blocks (modes for extra sides): mirror rules with 2-goal
  margins (OBS lines 370–465).

### 1.6 Strategy → attack level, non-pad branch (OBS `spp_t3.c`)

```
want = {0:0, 1:1, 3:3, 4:4}.get(T+0x134, 2)
t = T+0x210   // init 2; no in-match writer found (tactics report O4, still open)
if t != 2 and ((t < 2 and t <= want < 3) or (t > 2 and 2 <= want <= t)): want = t
   (plus extra phase/pad gates: needs MI+0x116c==2 and UI/team-mode checks —
   raw lines 120–160: FUN_140a606c0, FUN_140a3fe50, FUN_140a55cf0 == 0)
if want != L and MI+0x116c != 1: want = L   // level only changes in play
L = min(want, 4); if changed: T+0xb8c0 = frame (MI+0x1198)
T+0xb8a2 = (L >= 4); T+0xb8a3 = (L <= 0)    // set in FUN_1404f6c30 right after
```
Pad branch (OBS, unchanged from tactics report): pad-controlled side
(`FUN_140a55cf0(G+0x180, side)`) moves L ± 1 on pad command 0x6c only.
**CPU vs CPU in a real (non-watch) context takes this non-pad branch**
(INF(high): the branch condition is purely pad-attachment; in watch mode
both sides have pad slots so both take the pad branch and L stays 2 —
OBS live §7).

Strategy vocabulary (INF(med) from usage): 0 IN_PLAY-reset?, 1 defensive,
2 balanced, 3 attacking, 4 all-out. Level = strategy except strategy 2
covers "else" (OBS map).

## 2. Substitutions (OBS)

### 2.1 Object and cadence

- `match::ai::TeamMemberChange` (RTTI COL `0x142c9ac50`, vtable
  `0x1425c5fc0` [2]: slots `0x14051f5c0` (ctor), `0x140521090`
  (per-tick), OBS RTTI lookup).
- Slot `0x140521090(MemberChange, event)` is a big event switch (OBS
  `mmc1a.c`): case 0 = pre-match/window check, 1 = ratings snapshot,
  **2 = the in-match substitution decider**, 4 = commit queued subs,
  6 = half-time-ish re-check, 0x15 = formation/role repair, 0x1f =
  red-card/goal response, 0x27 = clear. Rest are no-ops.
- Gate `FUN_140521000` (OBS `mmc3a.c`): CPU side only
  (`FUN_140a55cf0(pad, side)` must be 0, else needs
  `FUN_1408c4ff0(sheet) != 0`); subs remaining
  (`*(obj+0x370)+0x8d > count(obj+0x20)`); queue not locked
  (`FUN_140a3f330(obj+0x20) == 0`).
- Limit object: `*(obj+0x370)+0x8d` = subs allowed, decremented by the
  commit thunk `FUN_143f659a0` (OBS `mmc2a.c`: `byte[370+0x8d] − 1`,
  floor 0 from XOR-folded 0). **The count is a settings byte, not a
  hardcoded 3** (OBS; GF's `maxSubstitutions = 3` is a special case).
  Windows: case 2 runs only when `MI+0x1168 ∈ {2,4}` (OBS raw
  `0x14052142c–0x140521434`: `(1168−2) & ~2 == 0`, i.e. period 2 or
  extra-time period 4 — corrected from "2/3") and the queue is unlocked
  (`FUN_140a3f360(queue, 1) == 0`); case 6 only when `MI+0x116c == 1`
  (in play, OBS raw `0x1405211f4`) with a minute-change gate
  (`FUN_140a6bd60 != param_2[5]`, OBS raw `0x140521216–0x140521224`).

### 2.2 The four paths in case 2 (OBS `mmc1a.c` + `mmc2a.c`)

In order (OBS raw `0x140521408–0x140521494` — corrected order: 390 and
2a0 run before 3b60), first non-empty queue wins:
1. `FUN_140523110` — **red-card repair** (raw `0x140521408`): scans
   slots 0..10 for an on-pitch player (`MI+0x1748[side·11+i]`) whose
   ancillary record `FUN_140a73e20(...)+8 > 1` (INF: send-off marker),
   picks replacement via `FUN_140521930(..., kind 2)` and queues
   `(in, out, kind 2)` (`FUN_14051f5f0`, OBS §2.5).
2. then `FUN_140523390` — **formation reshuffle following a red card or
   a goal** (raw `0x14052145d`, OBS `mmc2a.c` lines 164–394): reads a
   pending change byte `rec+8` (`FUN_141b59e20` record, must be 1..12);
   gates on period (2: needs opp goals − own goals > 1; 3: needs
   minute > 14 and own < opp, or minute ≥ 25 regardless) and on
   registered-role data; picks the out-player by minimising
   `FUN_140520e50(out, role) − stamina-discount` (0x1e = 30 % discount
   when `rec+0x5d` byte < 30, OBS lines 330–390) and queues kind 6.
3. then `FUN_1405242a0` — **tactical reshuffle when behind-or-level**
   (raw `0x14052148c`, OBS §2.4).
4. then `FUN_140523b60` — **stamina refill** (raw `0x14052148f`, OBS §2.3).

Case 0 (OBS `mmc1a.c`): at `MI+0x116c == 2 && MI+0x1574 ∈ {3,4}` runs
`FUN_140523910` (same scan as (1) but `+8 != 0` — INF: any abnormal
marker) and `FUN_1406006d0` (the plan-switch applier, §3). At
`MI+0x1574 == 5` runs `FUN_140523a10` (injury-time refill, checks
`MI+0x1590/0x158c` and role `T+0x4c00`, OBS `mmc2a.c` lines 48–116).

### 2.3 Stamina subs — `FUN_140523b60` (OBS `mmc2a.c` lines 394–713)

```
// cap: settings byte settings+0x18 (per match length? INF)
//   period 3 (second half): minute>25 → cap; 20..25 → cap·2/3; 15..20 → cap/3 (OBS lines 440–470)
//   period 2 (first half): cap (OBS)
// if used(obj+0xc) >= cap: return
for each on-pitch slot i<11 (role = T+0x3fb8+... class at +0xc48/0x4bd4):
   skip keeper-ish (role 1 with ...) and locked (FUN_140a3f390)
   skip formation-slot mismatches (pad-identity checks, OBS)
   threshold by role class (OBS lines 520–560):
     class {2,5} (INF: wide/mid?): 20/30/40 by sub-mode 1/2/3
     class {3,4}: 20/30/40 with base 2000
     class {6..9}: 25/40/45 with base 0
     else: 0/15 with base 3000   (mode = FUN_140520de0: 1 = CPU full, 3 = ...; OBS mmc3a.c)
   same-position bonus: +500 if bench/own slot pad-ids match, +100 if stamina-close (OBS)
   halved (×0.5) when pad-ids differ (OBS)
   clamp 0..50 (0x33/0x32)
   stamina pct = FUN_140a58550(PMS) (OBS: the guarded pct getter, mmc7a.c)
   if pct ≤ threshold: candidate, score = pct + base
sort candidates by score (qsort-like FUN_141e53540, OBS), take up to
remaining = cap − used, pick replacement via FUN_140521930(kind 3),
queue kind 3 (OBS lines 690–713).
```
So: **stamina subs only from the second-half-ish minute gates, capped
per match, worst-pct-first with role-class thresholds 15–45 % and a
same-position preference** (OBS). No score input in this function (OBS
by absence); the score gate lives in the caller (§2.2(2)/(4)).

### 2.4 Tactical subs — `FUN_1405242a0` (OBS `mmc2a.c` lines 713–1006)

Gates (OBS lines 760–830): not in play (`MI+0x116c == 1` → return);
needs `T+0x134`-side goal comparison (behind-or-level check via
`FUN_1408c9df0` sums); minute > 24 (0x18) with < 3 used, or > 14
(0xe) with < 2 used, in period 3; in period 2 needs
`FUN_1408c8f50(role-data, 6, 5)` race won (OBS). Then: for each bench
slot > 11, for each on-pitch slot, score =
`(role-fit 0x8c7580 + 6 or +10) × (0.7 + 0.3·pct/100)` (0.7/0.3 =
`0x14257a194`/`0x142590d98`, OBS lines 900–960) with ±2/4/8 rating-band
adjustments (`rec+0x3f` byte vs candidate, OBS); keep the best
bench→pitch swap that improves the margin (needs `> 1.0` gap and
`+5` hysteresis between successive picks, OBS lines 960–1000); queue
kind 4 (`FUN_14051f5f0`, OBS).

### 2.5 Who comes on — `FUN_140521930` (OBS `mmc3a.c` lines 32–145)

```
if out > 10 or side·11+out ≥ 22: return 0xff
role = FUN_1408c51b0(sheet, out, 0, plan 3) (kind 5 → role 0)
for each bench slot b in 11..squadCount (0x2eec):
   skip unavailable (FUN_140a3b520/3b500), pad-mismatch (FUN_140a3ac20),
   locked (FUN_140a3f390), pad-identity mismatch
   direct role-fit: FUN_1408cafa0(rec+8, role) → return b   // positional fit first
   else score via FUN_140522970(side, b, role, ctx) → keep best by FUN_140520390 (OBS mmc7a.c)
return best or 0xff
```
Replacement rating `FUN_140522970` (OBS `mmc7a.c`): overall
`FUN_1408c7580(rec, role)` + per-role flags `FUN_1408cb190` ladder
(OBS lines 299–420). Comparator `FUN_140520390` (OBS): prefers closer
role bytes (+6/+7), matching flags (+5), rating within 15 (0x14259be0c),
then higher bytes +8/+4/+5 (OBS lines 226–299). **Positional fit first,
then best rating; GF's "best average stat" is the fallback, not the
rule** (OBS).

### 2.6 Commit — `FUN_14051f5f0` (OBS `mmc3a.c` lines 145–187)

Queues `(in, out, kind)` into `obj+0x20` via `FUN_140a3efa0` iff CPU
side (or sheet override), both players unlocked, queue unlocked. Case 4
(OBS `mmc1a.c`): for each of 6 queue slots with kind 3/4 set, calls the
commit thunk `FUN_1405208e0` (= decrements the settings count, §2.1).
**6 queue slots, kinds observed: 2 (red-card/injury), 3 (stamina),
4 (tactical), 5 (red-card response `0x1521300` path), 6 (formation
reshuffle)** (OBS Română switch + call sites).

### 2.7 Injury-forced subs (OBS)

- Case 1 of the event switch (OBS `mmc1a.c` lines 80–120): averages the
  `rec+0x3f` byte over available players into `obj+0x18`; nothing
  queued here — it arms the rating baseline the pickers use.
- `FUN_140523910` (case-0 injury scan, OBS): any on-pitch player with
  marker `+8 != 0` → immediate kind-2 queue (no minute/score gate).
- `FUN_140521e40` (sent-off/injury replacement picker, OBS `mmc4a.c`):
  groups on-pitch by role class (7 / 8 / 9 = INF: def/mid/fwd bands at
  `T+0x3fb8+...`), prefers class-8 then class-7 then class-9
  replacements minimising `FUN_140520e50` (stamina-discounted rating,
  OBS lines 100–225).
- Stretcher/demo choreography: not in this object (cutscene report
  `FUN_140484a10`; injury-state `st 4` in stamina-drain §6.3). Open how
  the demo completion hands to the queue (O-sub-1).

## 3. In-match tactical changes (OBS)

- **Formation switches**: the plan-switch applier `FUN_1406006d0`
  (OBS `mmc2a.c` lines 1029–1171) runs at case 0 (`MI+0x1574 ∈ {3,4}`):
  needs period 3..7, CPU side, `FUN_140a60340(timer) > 4`, sheet
  `FUN_1408c5360 == 0`, no pending plan edits, all 11 slots'
  registered roles stable across plans (`FUN_1408c51b0(slot,plan 1)`
  equal ×3 reads); then after a `fps·seconds·60` cooldown
  (`FUN_140a6bca0 × fps × 60 ≤ clock − obj+0x70`) builds the candidate
  plan list `FUN_1405fdaf0` + score lists `FUN_1405fe440` and applies
  the first passing chooser (`FUN_140600a80` switch: kinds 1–6 + 9).
- **Plan candidates** `FUN_1405fdaf0` (OBS `mmc5a.c`): buckets the 11
  on-pitch slots by registered role class (1 / 2–3 / 4 / 5 / 6–7 / 8 /
  9–10 / 11 / 12 / default) and classifies the shape into
  `obj+0x2d0 ∈ {0x17..0x1d, 1..9, 0x1e}` (e.g. 3 defenders + 4 mids = 7/9;
  OBS lines 100–160). **This is shape recognition, not a switch
  command.**
- **Instruction/line tweaks on strategy change** `FUN_1405fe440`
  (OBS `mmc5a.c` lines 241–297): when `T+0x134 ∈ {0,1}` queues plan-list
  entries 1/2/4 from negative-flag bytes; when 2 appends shape-based
  entries (`FUN_1405fe590` = 1/2/4, `FUN_1405fe5e0` = 5/6/7/8 gated on
  toggle k2); when 3/4 appends 7/8-gated entries. Sets
  `obj+0x329 = (own goals − opp goals) < 0` (sign bit, OBS). The queued
  kinds map to formation/instruction appliers (`FUN_1405fe130/270`
  write `obj+0x2d0` shape ids, OBS `mmc6a.c`).
- **Role swap on substitution** `FUN_1405fd6d0` (OBS `mmc7a.c`): swaps
  the x/y formation coordinates of the in/out slots across all 3 phases
  (0x28 vs 0x40 record kinds by y-order, OBS lines 537–595).
- Net: **the CPU changes plans via the kind-1..9 queue when the
  strategy is 0/1/2/3/4, gated on toggles and shape; no evidence of a
  "losing → 3-4-3 at minute X" script** — the switch is reactive to the
  strategy word, not the clock (OBS; GF's `ChooseFormation` minute
  rules have no counterpart).

## 4. Time-wasting / game management (OBS + live)

- **No 80′/85′ rules** (OBS): the only minute constants in the
  management code are 10/20 (+rand 36/26), 28 (+rand 10), 14/15
  (0xe), 24/25 (0x18/0x19), 100 (OBS §§1–2). A displacement scan for
  80·60/85·60 equivalents is N/A — minutes are compared directly
  (minute = clock/3600), and no 80/85 immediate exists in
  `FUN_14051e970`/`FUN_140523b60`/`FUN_1405242a0` (OBS).
- **What does exist when leading late** (OBS): strategy 1 via the
  second-half machine (§1.4: ahead at/after R[6] = 28+rand(10) min →
  1), which maps to attack level 1 (defensive) through §1.6; plus the
  `FUN_1405fe440` strategy-1 plan entries and the `0x329` "leading"
  flag. No corner-shielding target, no slow-throw/carry, no keep-ball
  instruction id, no GK-hold change found (OBS by absence in
  `FUN_140600a80` kinds, carrier units, and the §3 appliers).
- **Fatigue cover**: Gegenpress flag `G` in the drain (stamina-drain
  §3.3) and the §5 work-rate demotion at dep ≥ 60 are the only
  "tired legs" management (OBS, prior report).

## 5. Red cards (OBS, incidental)

- On-pitch test `MI+0x1748[side·11+slot]` (OBS everywhere); sent-off
  scan `FUN_140a73e20(...)+8 > 1` → kind-2 queue (§2.2(1)); deeper
  picker `FUN_140521e40` (§2.7). Case `0x1f` (OBS `mmc1a.c`): re-points
  formation slots (`FUN_1408c5c50`) and designated-player records
  (`FUN_1408c5080 → FUN_140a3b000 → FUN_140a621f0 → FUN_1408c5cf0`,
  OBS lines 230–280) when a slot's occupant is gone. No strategy input
  from red cards (OBS §1.1).

## 6. GF replacement map

| PES finding | GF code it replaces | change |
|---|---|---|
| §1 strategy decider (score+minute+strength+mode, R[5]/R[6] limits, phase-3 cadence) | `AIManager::ChoosePhilosophy` + `MatchMentality::Decide` | delete the 80′/85′/60′ minute rules; implement modes, R[5]/R[6] draws, phase-3 gating; strategy vocabulary 0/1/2/3/4 |
| §1.6 strategy→level map + T+0x210 clamp + in-play-only | `matchmentality.cpp` momentum (`chaseMomentum`/`holdMomentum`, `ApplyMomentum`, corner target) | delete momentum/corner logic; level = map(strategy) with the 210-clamp and in-play gate |
| §2 queue (6 slots, kinds 2/3/4/5/6, settings-byte limit, case-2/0/6 cadence) | `Substitutions::{IsSubstitutionWindow,Validate,Commit}` + `State::used` | replace `maxSubstitutions = 3` with the settings count; implement the event switch + queue |
| §2.3 stamina picker (role-class thresholds 15–45 %, cap schedule, worst-first, +500/+100 fit) | `AIManager::PlanSubstitution` fatigue half | replace flat `tiredFatigueFactorInv`/55′ gate with class thresholds + cap schedule |
| §2.4 tactical picker (`(fit+6/10)·(0.7+0.3·pct)`, ±2/4/8 bands, 1.0/5 hysteresis) | `PlanSubstitution` "best average" + `ChooseFormation` | replace best-average with role-fit-first + scored comparator |
| §2.5 `FUN_140521930`/`FUN_140522970`/`FUN_140520390` | `PlanSubstitution` in-pick loop | positional fit first, then scored best |
| §3 plan queue (kinds 1–9, toggle-gated, `FUN_1405fd6d0` coord swap) | `ChooseFormation` 532/3-4-3 scripts + `ChoosePhilosophy` Gegenpress/ParkTheBus | reactive strategy-driven queue; delete clock-scripted formation swaps |
| §4 (no 80′/85′, no corner-camp) | `timeWastingStart_ms`, `ShouldStayInCorner`, `GetCornerTarget`, `comfortableLeadTime` TikiTaka | delete all four; leading-late = strategy 1 only |

## 7. Live (OBS)

- Setup: read-only pread of the live match (watch-mode AI-vs-AI, both
  sides pad-attached). `watch_mm.py` 0.5 s cadence (540 rows,
  `watch.jsonl`); `subwatch2.py` 2 s cadence with guarded-stamina
  decode (71 rows, `subwatch.jsonl`). Raw-vs-decompile check of
  `FUN_1404f8350` head + non-pad branch ran clean (branch directions as
  decompiled; `je 0x1404f85da` = the non-pad path, strategy→level map
  immediates confirmed).
- First half: score level (strategy 3/2 → 2/2, no goal pulse in `sb`
  bytes). Strategy 2 = balanced at 0–0 (OBS, consistent with the mode-0
  first-half machine). Level fixed 2, plan fixed 0 — predicted by the
  pad branch (§1.6) in this watch setup.
- Stamina: pct fell steadily through the half (outfield lows ~14–16 at
  first-half end; keepers 100). **No substitution and no queue entry**
  (`on` bits all 11 set, `plan` 0, no `MI+0x1748` change): consistent
  with §2 — case 2 needs period 2/3 and the stamina cap schedule;
  nothing forces one at 0–0 in period 1.
- Half-time: `per` 1 → 2 with `ph` 2/`st` 1 pulse (OBS `subwatch.jsonl`
  t = 64 s); pct partially recovered after (half-time bonus,
  stamina-drain §3.4 — OBS consistent, not re-proven).
- Second half (t = 86–88 s, `per` 2 → 3): **strategy split 2/2 → 3/1**
  (OBS: one side 3 = attacking, the other 1 = defensive) with level
  still pad-pinned at 2/2 and plan 0/0. A goal pulse was not caught in
  the 2 s samples (`sb` bytes static), but the split matches the §1.4
  second-half machine (one side ahead → 1, the other behind → 3). No
  substitution or plan change followed within the next minute of wall
  clock (`on` unchanged, pct 37–84 after the break).
- Not seen (open live): an actual substitution event with in/out ids,
  score, minute and pct at the time; any plan change. Requires a longer
  second-half capture (O-mm-1).

## 8. Open

- O-mm-1: live substitution capture (in/out, score, minute, pct) —
  needs a longer/older match or a scored state; watchers ready
  (`subwatch2.py`).
- O-mm-2: `T+0x210` writer (tactics O4, still open) — bounds the
  strategy→level clamp.
- O-mm-3: injury-demo → sub-queue handoff timing (stretcheravat);
  choreography in cutscene report, queue here.
- O-mm-4: settings byte backing `+0x8d` (subs allowed) and
  `settings+0x18` (stamina cap) — UI/menu provenance open.
- O-mm-5: `FUN_140520de0` mode-3 provenance (`+0xa290` byte) and
  `FUN_140a40bc0` team-mode 1/2 (always-attack/defend) provenance.
- Killed: GF 80′/85′/60′ rules (no counterpart); "best average comes
  on" (fit-first); hardcoded 3-sub limit (settings byte); possession /
  stamina / red-card inputs to the strategy decider (absent).

## 9. Evidence ledger (new)

| ID | claim | conf | evidence |
|---|---|---|---|
| EV-MM-0001 | strategy decider inputs = score, minute, strength, mode, R[5]/R[6]; no possession/stamina/cards | OBS (code) | `strategy.c` (0x14051e970); `mmc8a.c` (0x140a6bd60, 0x1408c9df0, 0x140a924e0) |
| EV-MM-0002 | R[5]/R[6] minute-limit draws (10+36/20+26, 28+10, 100s) | OBS (code) | `strategy.c` lines 150–200 |
| EV-MM-0003 | non-pad strategy→level map + 210-clamp + in-play gate | OBS (code+raw) | `spp_t3.c` (0x1404f8350); raw 0x1404f8568–0x1404f86c8 branch directions confirmed |
| EV-MM-0004 | TeamMemberChange object + event switch (cases 0/1/2/4/6/0x15/0x1f) | OBS (code+RTTI+raw) | `mmc1a.c` (0x140521090); RTTI COL 0x142c9ac50; raw 0x140521090–0x140521494 dispatch + order |
| EV-MM-0005 | sub limit = settings byte +0x8d, not hardcoded | OBS (code) | `mmc2a.c` (0x1405208e0 thunk); `mmc3a.c` (0x140521000) |
| EV-MM-0006 | four case-2 paths in raw order 3110 → 3390 → 242a0 → 23b60; case-2 period ∈ {2,4} | OBS (code+raw) | `mmc1a.c` case 2; `mmc2a.c`; raw 0x140521408–0x140521494, 0x14052142c–0x140521434 |
| EV-MM-0007 | stamina thresholds 15–45 by role class + cap schedule + worst-first | OBS (code) | `mmc2a.c` 0x140523b60 |
| EV-MM-0008 | tactical score `(fit+6/10)·(0.7+0.3·pct)` ±2/4/8, 1.0/5 hysteresis | OBS (code) | `mmc2a.c` 0x1405242a0; consts 0x14257a194/0x142590d98 |
| EV-MM-0009 | fit-first then rated-best replacement (0x140522970/0x140520390) | OBS (code) | `mmc3a.c` 0x140521930; `mmc7a.c` 0x140522970/0x140520390 |
| EV-MM-0010 | plan queue kinds 1–9 + coord swap on sub | OBS (code) | `mmc5a.c`/`mmc6a.c` (0x1405fdaf0/0x1405fe440/0x1405fd6d0); `mmc3a.c` 0x140600a80 |
| EV-MM-0011 | no 80′/85′ rules; leading-late = strategy 1 only | OBS (code, absence) | `strategy.c`, `mmc2a.c` (no 80/85 immediates) |
| EV-MM-0012 | live: no sub through 0–0 first half (pct → ~15) then 3/1 strategy split in second half; L = 2 pad-pinned, plan 0 | OBS (live) | `watch.jsonl` (540 rows), `subwatch.jsonl` (71 rows) |
