# PES 2021 stamina drain / recovery, fatigue effect, injury roll — StaminaDrainRE (2026-10-13)

Read-only RE. Image: full live image `~/ghidra-projects/scratch/Unpacker/pes_live.exe` (headless `dec.sh`, read-only) +
capstone on `pes_full.bin`. OBS = observed at the named VA (decompile cross-checked against raw disassembly where
stated); INF(conf) = inference. No player/team names. Scratch: `~/ghidra-projects/scratch/StaminaDrainRE/`.

## 0. Headline — why the A5 writer was "not found"

- **The A5 writer is ordinary fixed-offset code in `sub_14092AF10` (OBS)**; it stores to `+0xCBB4…+0xCC08`
  (= A5[23..44]), not to `+0xCB58`, so a displacement scan for `0xCB58` could not see it.
- **The stamina value itself is anti-tamper encoded** (OBS): a 4-byte value XOR/add-scrambled with a per-write
  rolling key, kept in a 4-slot ring and guarded by an FNV-1a-64 hash; every getter recomputes the hash and traps
  (`func_0x1415a1714` + `int3`) on mismatch. That is why no plain stamina field ever showed up in live diffs.

- Nothing in the drain reads `pesSmart.o`: every constant below is an immediate or an `.rdata` float (OBS in the
  decompiles + disassembly). The `*Tired` keys of `pesSmart.o` (matchlogic §3.5) are therefore **not** the live
  drain; INF(med) they feed the AI's own tiredness estimate, consumer not located.
- The "tier floats 5/11/16/28 @0x1425E4D64" are **not** action-quality tiers: the table at 0x1425e4d60 is
  `{0, 5, 11, 16, 28}` = movement-class target speeds in km/h (stand/walk/jog/run/dash), read only by
  `FUN_140702680` (callers 0x1406ffd0a, 0x14070364b) as `tab[cls]*1000/(gs*3600)` = metres per tick (OBS).

## 1. Data model (OBS)

| what | where | evidence |
|---|---|---|
| per-player match state `PMS` | `*(match::Player + 0x1a4a8)`; lazily `*( *(Player+0x1a498) + 0x10 ) + 0x15c8` (`FUN_140922ff0`); also `func 0x140a489f0(ctx, idx)` | 0x140922ff0 decompile |
| **live stat bytes** | **`PMS + 0x70 + statId`** (`0x14124dad0` = `lea rax,[rcx+0x70]` is applied to `PMS`, not to `match::Player`) | 0x14092af10 (`lVar21 = 0x14124dad0(lStack_1c0)`, `lStack_1c0 = param_2 = PMS`) — corrects stamina-live §10.5, which read `Player+0x70` (hence the ASCII there) |
| base (unmodified) stat bytes | `rec + 8 + statId`, `rec = FUN_141b59e20(member)` | 0x14092af10 (`lStack_1b8 = lVar17 + 8`) |
| Stamina ability | `rec + 0x35` ⇒ **internal statId 0x2d** | 0x140922e5d `movzx edx, byte [rax+0x35]` |
| condition arrow index (0 Top … 4 Terrible) | `PMS + 0x64` (A2 table index) ; `rec + 0x58` int 0..4 used by the tank | 0x14092af10 (`*(int*)(lStack_1c0+100)`), 0x140922e6a |
| start-of-match freshness % | `rec + 0x5d` byte, floored at 10 | 0x140922e59 |
| injury resistance (0 C, 1 B, 2 A) | live stat `PMS+0x70+0x2f` (internal 0x2f, follows STAMINA/FORM/RESISTANCE in the `.rdata` ability-name pool @0x1425d80f8…0x1425d8108) | 0x140481856 `movzx ecx, byte [rax+0x2f]` |
| **stamina `cur`** | guarded u32 at `PMS + 0xc8` | getter 0x140a58350, setter 0x140a592c0 |
| **stamina `max` (tank)** | guarded u32 at `PMS + 0xe8` | getter 0x140a58450, setter 0x140a595c0 |
| **breath `B`** (short-term dash reserve) | guarded u32 at `PMS + 0x108` | getter 0x140a580b0, setter 0x140a58f60 |
| stamina % | `pct = max==0 ? 0 : cur*100/max` (int64 division) | 0x140a58550 |
| breath % | `bp = B*100 / (max(pct,20)*1920/100)` | 0x140a581c0; `0x780` = 1920 from 0x140a581b0 (`mov eax,0x780; ret`) |
| copy of `cur` | `match::Player + 0x1a484` (written at init and half-time only) | 0x140922fc7, 0x140923060 |

**Guarded u32 layout** (block at `off`): `u64 hash @+0`, `u32 key @+8`, `u32 slot @+0xc`, `u32 ring[4] @+0x10`.
Decode (OBS 0x140a58350): `e = ring[slot&3]`, `k = key*3`, `kb[i] = ((k + 4*i) >> 8*i) & 0xff`,
`v[i] = ((e[i] + kb[i]) & 0xff) ^ ((kb[i]*0x3f) & 0xff)`; valid iff `FNV-1a-64(v[0..3]) == hash`
(basis 0xcbf29ce484222325, prime 0x100000001b3) else trap. Encode (OBS 0x140a592c0): `e[i] = (v[i] ^ kb[i]*0x3f) - kb[i]`,
`slot = FNV-1a-32(KUSER_SHARED_DATA+8 low dword)` (a time value; only selects the ring cell). GF never needs this;
it matters only for reading the live game (decoder: `stam.py`).

Only three code sites write `cur` (OBS, all `e8` xrefs to 0x140a592c0 in `.trace` + `.impdata`): init
0x140922f99, half-time 0x14092313d, per-tick 0x14092feff. `max` only at init (0x140922f6f); `B` only at init
(0x140922fab) and the per-tick breath update (inlined copy of 0x140a58f60 at 0x14092badc).

Call chain per tick (OBS): `match::Player` vtable 0x1426329a8 slot 2 `0x140925750` → `0x140925470` →
`FUN_14092fad0(Player, PMS)` (drain/recovery, §3) → `FUN_14092af10(Player, PMS)` (stat rebuild incl. A5, §4).

## 2. The tank and initial value — `FUN_140922dd0` (OBS decompile = disassembly 0x140922e20–0x140922fd7)

```
s   = rec[0x35]                                   // Stamina ability 40..99
s'  = s < 5 ? 5 : s >= 95 ? s*140/100 : s >= 90 ? s*130/100 : s >= 80 ? s*120/100 : s >= 70 ? s*110/100 : s   // int
F   = *(float*)( *(ctx + 0x2c0) + 0x58 )          // match-length scale: 10.0 live under a 10-minute setting, §7
a   = rec[+0x58] (int)                            // 0:+6000  1:+2000  2:0  3:-4000  4:-8000   (×F)
T   = *(int*)(team(ctx,side) + 0x4c00 + 4*(idx%11))   // team = 0x140a48a30(ctx, side)
max = (int) max( 16000*F , (float)((s' & 0xff)*400)*F + a*F + (T == 0 ? 8000*F : 0) )
cur = max * max(rec[0x5d], 10) / 100             // int; then SetCurrent clamps to max
B   = 1920  → clamped to cap = max(pct,20)*1920/100
```
Constants: 400 = imm 0x190 @0x140922eec; 6000 @0x14260b390, 2000 @0x1425cb004, 4000 @0x1425cb498, 8000 @0x1425cb4a0,
16000 @0x14259d638; multipliers 110/120/130/140 = imm 0x6e/0x78/0x82/0x8c @0x140922eca…0x140922e9d.
INF(med): `a` is the condition arrow (0 Top … 4 Terrible), matching the 5-column A2 table; `T` unidentified (it also
gates the half-time bonus, §3.3).

## 3. Per-tick drain and recovery (OBS)

### 3.1 Gate — `FUN_14092fad0` 0x14092fad0–0x14092fbcc
Runs for player `idx` only if `*(ctx+0x400)` / `*(ctx+0x3b0)` sub-modes are inactive, `MI+0x1775 ∉ {8,10}`,
`idx < 22` and `MI+0x1748+idx != 0` (on the pitch). Then the breath update (§3.2) runs; the drain (§3.3) only
if `MI+0x116c == 1`; else if `MI+0x116c == 2 && MI+0x1574 == 1 && MI+0x11a8 == 2 && MI+0x1168 ∈ {2,4}` the break
recovery (§3.4). `MI = *(ctx+0x270)`. INF(med-high): `+0x116c == 1` is "ball in play", so `cur` is frozen at dead
balls (no drain, no recovery).

### 3.2 Breath `B` — `FUN_14092b950` (asm 0x14092b950–0x14092badc)
```
gs = GetGameSpeed() (float, 54.0 in a match; 0x141e532d0)      r = 48/gs           (48 @0x142588214)
d  = (int)(6*r)                                                   (6 @0x14259bdec)    → 5 at gs 54
v  = |vel| m/tick, 3-D (motion obj 0x140a48a10(ctx,idx); FUN_140a6c6d0 with min 0.694444/gs @0x14259bdd0)
if v * gs*60*60/1000 > 22 km/h:  d = (int)(d - 9*r)               (22 @0x1425c48c0, 9 @0x14259d60c)  → -3
if idx == FUN_140a627e0(team,0) (= *(*(team+0x18)+0x2601c), INF ball holder): d = (int)(d - 2*r)   → 3, or -4 when dashing
B  = min( max(0, B + d), max(pct,20)*1920/100 )
```
So at 54 Hz: refill +5/tick (0→1920 in 384 ticks = 7.1 s), dash −3/tick (1920→0 in 640 ticks = 11.9 s),
with the ball +3 / −4.

### 3.3 Stamina `cur` — `FUN_14092fad0` (asm 0x14092fbe0–0x14092feff; decompile agrees)
```
cls  = speed class (0x140a75be0 on the motion obj): 0 if byte[+0x3f4]==0; v = float[+0x3fc + 16*byte[+0x108]] km/h;
       v<=0.05 → 0 ; v<=8 → 1 ; v<=13.5 → 2 ; v<=22 → 3 ; else 4      (0.05 @0x1425a8a28, 8 @0x14259bdf4, 13.5 @0x1425a1ffc, 22 @0x1425c48c0)
spd  = {+19, +17, +9, -5, -9}[cls]          // = -M + P:  M = {-9,-7,-4,+4,+7} (imm 0xf7/0xf9/0xfc/4/7), P = {+10,+10,+5,-1,-2}
bp   = breath % (§1), after this tick's §3.2 update; term (signed char)bp / 100  (1 only when B is full)
S    = G + 5*F12 + K + (st == 0x34) + F16
R    = 5*(bp/100 - 3 - 2*S) - 2*(F20 + F13) - 3*F14 - F68 + spd
cur' = cur + trunc(R*60 / gsInt)            // gsInt = 0x141e532e0 = 54; C idiv (toward zero)
cur' = cur' < 10 ? 0 : cur'  ; SetCurrent(cur') clamps to max     (0x14092fef7 cmp edi,0xa)
```
Flags (OBS where they are read; meaning INF):
- `F68` = 1 unless the player's side owns the ball (`0x140a3e7d0(...,&o,0)`, `0x140a60530(&o)` and `o.side == side`)
  — the defending side pays 1 more per tick.
- `F20` = 1 if own side has the ball **and** `Player+0x698 == 3`. `st = Player+0x698` (action/command id).
- `F13` = 1 if bit 15 of the per-AI-state table @0x1426484f0[`aiState`] is set (`aiState = (signed) byte[ai+1]`,
  `ai = 0x140a48910(ctx,idx)`, states ≥ 0x5e → 0). If not set: `F16 = (aiState == 0x11 && word[ai+0x1a]==0)`,
  `F14 = (aiState == 0x10 && word[ai2+0x1a]==0)`.
- `F12` = 1 if `st == 0x25` and a team slot counter (`0x140a401c0(team+0xa2cc+slot*0x34)`: `[+8]-[+4] == 1`) — −50/tick.
- `G` = 1 if team instruction id 0xd is on (`0x140a63950(team, side, 0xd, 0xff)`), the player is not tagged with
  instruction 0xf, and his formation-slot category (`0x140a55570`) is not 3. INF(med): 0xd = Gegenpress, 0xf = Counter
  Target, category 3 = forward.
- `K = (int)(gs*50 + 0.5) * (team[+0x100 + 4*slot] == 1)` = 2700 at gs 54 — empties the tank in one tick when that
  per-slot team flag is 1 (INF: a removed/sent-off/"exhausted" marker; never 1 for a normal outfield player).

Plain-running numbers at gs 54, all flags 0, B not full: per-tick Δcur = {+4, +2, −6, −22, −26} for
cls {stand, walk, jog, run(13.5–22), dash(>22)}; defending side −1 more before the ×60/54 (e.g. run −23).

### 3.4 Half-time / extra-time break — `FUN_140923060` (decompile; constants OBS)
```
period = MI+0x1168
2 (half-time):     cur += (int)(9000*F) ; T in 1..2: += (int)(3600*F) ; T in 3..6: += (int)(7200*F)
4 (ET break):      cur += (int)(5400*F) ; T in 1..2: += (int)(1800*F) ; T in 3..6: += (int)(3600*F)
SetCurrent clamps to max; Player+0x1a484 = result
```
9000 @0x142632a6c, 3600 @0x142598590, 7200 @0x142632a68, 5400 @0x142632a64, 1800 @0x142632a60. Whether the gate
holds for one tick or for the whole break is not proven statically (INF med: one tick).

## 4. Fatigue → stats: the A5 writer — `FUN_14092af10` 0x14092af10 (OBS decompile; constants OBS)

```
pct = stamina % (0x140a58550)
t   = pct <= 10 ? 0 : pct >= 70 ? 1 : (pct - 10)/60          (10 @0x142588210, 70 @0x14259be4c, 60 @0x14259a764)
a5  = 0.8 + 0.2*t                                              (0.8 @0x142572348, 0.2 @0x142598578)
if (A5[40] != a5): A5[i] = a5 for i in {0x17,0x18,0x1a,0x1b,0x1c,0x28,0x29,0x2a,0x2b,0x2c}; mark them dirty
```
Stores at `Player+0xcbb4,+0xcbb8,+0xcbc0,+0xcbc4,+0xcbc8,+0xcbf8,+0xcbfc,+0xcc00,+0xcc04,+0xcc08`. The other 50
A5 cells are never written (stay at their initial 1.0). Because `pct` is an integer, A5 moves on a **1/300 grid**
(`0.2/60`), which is exactly the stamina-live §10.4 series (0.996667 = 1−1/300 … 0.956667); its "1/30 grid" and
"all 60 slots drained" readings are corrected by this.

Breath penalty, same function:
```
bp  = breath %   (0x140a581c0)
p   = bp <= 0 ? 10 : bp >= 20 ? 0 : trunc(10*(20 - bp)/20)    (20 @0x14259be1c, -20 @0x1425a946c, 10)
if p != Player+0xcc48: store it; stats {0x1a,0x1b,0x1c,0x2a} are rebuilt this tick with  byte -= p
```
Final live stat (all dirty ids): `PMS+0x70+id = clamp( (u8)(base[id] * A2*A1*A3*A4*A5) - (p if id∈{0x1a,0x1b,0x1c,0x2a}
and p changed this tick), lo, hi )` with lo/hi from `0x1408caf60` (40..99). Note the penalty is applied only in the
tick in which `p` changes; a later rebuild of the same stat (another modifier changing) drops it (OBS, literal
behaviour; reproduce if bit-exactness matters).

StatId names: internal ids, **not** Player.bin field ids. Proven anchors: 0x2d = Stamina (tank input), 0x2f =
injury resistance (0..2). Under the `.rdata` name-pool reading (Tentative) 0x28..0x2c = Physical Contact, Body
Control/Balance, Kicking Power, Explosive Power/Acceleration, Jump. The names of 0x17/0x18/0x1a/0x1b/0x1c are open.

## 5. Fatigue → AI work rate — `FUN_140512cd0` (OBS decompile; was "skills and fatigue", matchlogic §3.3)

```
dep  = (max - cur)*100 / max                         // float, % depleted
thr  = 60 (@0x14259a764); 70 (@0x14259be4c) if any on-pitch teammate (or the captain slot) has skill bit 25 or role 7/10/0x15;
       +15 (@0x14259be0c) if this player has skill bit 27 or a teammate role 0xe;  thr = min(thr, 100)
if dep < thr: return                                  // no effect
t    = dep >= 100 ? 1 : (dep - thr)/(100 - thr)
rad  = 40 - 35*t  (+15 if team+0x255 and not the reference player; +10 if bit27, else +5 if bit25)   (40 @0x14259be2c, 35 @0x14259a760)
tier = *param_4 (≥2 to enter): tier -= 1 if the player is farther than rad from the point at (L+0x2482c, L+0x24834)
       [or he is the special-role 0x2c player]; tier -= 1 more ("severe", cVar7==2) when t >= 0.5 with team+0x87d0[slot]
       set, or t > 0.8 with rad < 20 and he is not in the team's 4/6/10-man core list; tier never below 1
```
Plus several exemptions (ball holder, two reference players, AI states 0x11/0x3e/5, offside-line checks with a 35 m
margin, and a 36 m position test against `L+0x22e4c+200*idx`). INF(med): `tier` is the AI's per-action effort level
(3 full, 2, 1) and the point is the ball/defensive reference. Bits 25/27 are the matchlogic captaincy/fighting-spirit
reading (EXT), not re-proven here.

## 6. Injury roll — `match::Injury` (vtable 0x14259ce20, 1 slot = dtor 0x140443da0; ctor 0x1404811e0)

Object (OBS ctor): per-player contact records `rec[s]` at `+8 + 0x18*s`, injury states `st[s]` at `+0xdcc + 0x28*s`,
`s = side*0x28 + slot` (`0x140a62fe0`), and an embedded `math::Random` at **`+0x1a70`** (default seeds, ctor
0x140a923f0). Draws use `0x140a924e0(rng, M, 2)` = slot-2 LCG step, `(int)(state·2^-31·M)` (OBS decompile; the
rng report's "returns state as float" description of 0x140a924e0 is incomplete: it returns `trunc(u·M)`).

Entry: contact event kind 0xf (`FUN_140481bb0`) → `FUN_140481d70(inj, ev, victim, attacker)` → `FUN_1404812b0` →
`FUN_140481610` (severity). Gates (OBS 0x1404812b0): both ids < 22, victim on the pitch, `MI+0x1775 == 0`, the two
sub-mode objects inactive, settings `*(ctx+0x2c0)+0x72+side != 0` (injuries enabled for that side),
`!0x140a3f570(settings)` (`settings+0x76 == 0`), victim AI state not 0x0f, `byte[ai+3] ∉ {0x10,0x11}`, no teammate in
AI state 'X' (0x58), `MI+0x116c == 1` (in play), and the victim's motion object `+0x610 == attacker` (he is
actually in contact with that attacker). Body part (OBS): `part = 1..6` from the victim motion-object flags
`+0x629..+0x62e` (`+0x624` if already 1..6); none set → no record. INF(high): the 6 values are
`INJURY_PART_{LEFT_THIGH, LEFT_LEG, LEFT_FOOT, RIGHT_THIGH, RIGHT_LEG, RIGHT_FOOT}` (the enum has exactly 6 + NOTHING).

### 6.1 Severity of one contact — `FUN_140481610` (asm 0x140481610–0x1404818c4)
```
base = victim aiState == 0x11 ? 100 : 50                         (100 @0x14253ee30, 50 @0x14259be34)
ua   = clamp(speedA/25, 0, 1);  uv = clamp(speedV/25, 0, 1)      (speed = FUN_140477130(motion), km/h INF; 25 @0x14259be20)
sev  = base * (0.7 + 0.3*ua) * (0.7 + 0.3*uv)                    (0.3 @0x142590d98, 0.7 @0x14257a194)
sev *= attacker aiState in 11..12 ? 1.0 : in 13..14 ? 0.8 : 0.5  (0.8 @0x142572348, 0.5 @0x142565600)
sev *= speedA >= 1 ? 0.75 + 0.5*clamp(angle/180, 0, 1) : 0.2   // angle = 0x140481b00(victim heading +0x554, dir to attacker), degrees
thr  = {30, 50, 70}[resistance C/B/A]  ;  if Range(rng,100) < thr: sev *= 0.5      // 1st RNG draw
sev  = clamp(sev, 0, 255) → byte
```
### 6.2 Accumulation and the jackpot roll — `FUN_1404812b0`
```
if sev > 85 (0x55):  thr = {5, 2, —}[resistance C/B/A];  if resistance < 2 && Range(rng,100) < thr: sev = 200   // 2nd draw, A never
rec.last = sev ; rec.acc = min(255, (int)rec.acc + sev) ; rec.part = part ; rec.cause = attacker aiState 11..12 → 2, 13..14 → 1, else 0
rec.time = 0x140a3c050(MI+0x1168) ; rec.period = MI+0x1168
```
### 6.3 State — `FUN_140481d70` (decompile; constants 200 @0x1425a2668, 150 @0x14259be68, 85 @0x14259be58)
```
level = rec.acc / 200                      → st.level (+0xdd0)
st 0 → 3 if acc >= 200 ; st 0 → 2 if 150 <= acc < 200 ; st 2 → 3 if acc >= 200
if (rec.last >= 85 || st == 3) && victim aiState == 0x11 (down) && !0x140a638c0 && !0x140a3c970(MI,side)
   && settings[+0x17] < 0x140a3bfc0(MI,side) && (char)team[+0x254] · victimMotion[+0x55c] > 0 && 0x1408c5310(...) != 0:
       st.pendingDemo (+0xddc) = 1, copy part/time/period/cause            // the stoppage + demo request
```
`FUN_1404819a0` (per event, via 0x14048202c) promotes st 3 → 4 (copies the record, `level = acc/200`) when subs remain
and the player is down (aiState 0x11 / 'W' 0x57) or `0x140a638c0` holds; st 4 counts `+0x1a68`, a pending demo counts
`+0x1a64`. INF(med): st 2 = "knock, plays on" (`injury worry`), st 3 = injured, st 4 = injury being processed (stretcher /
substitution); `acc` never decays inside a match (no writer other than §6.2 found).

So a single contact can injure only through the jackpot (`sev > 85`, 5 % for C, 2 % for B, never for A — sets 200 ⇒
st 3 directly); otherwise injury is cumulative: total severity ≥ 150 = knock, ≥ 200 = injured. The fatigue cause
(`PLAY_DATA_INJURY_FATIGUE`) was **not** found in this object; open.

## 7. Live validation (read-only `/proc/<pid>/mem`, OPEN_PLAY, one match, 10-minute setting)

Pointer getters used (OBS disassembly): `PMS(i) = *(ctx + (i+0xf3)*0x28)` (0x140a489f0), motion `*(ctx+(i+0xbf)*0x28)`
(0x140a48a10), AI `*(ctx+(i+0xa5)*0x28)` (0x140a48910), team `*(ctx+(side+0xa1)*0x28)` (0x140a48a30); like `MI`, these
context slots read 0 on most reads, so the sampler retries and caches the last non-zero pointer. Tick counter =
`match::Player+0x10` (incremented by slot 2 0x140925750). Scripts: `stam.py` (decoder), `stam2.py` (10 Hz sampler),
`fit2.py`/`fit3.py`, `tank.py` (tank check, not yet run in a match). Data: `live1.jsonl`, `live3.jsonl`.

| check | result |
|---|---|
| guarded-u32 decoder | FNV hash matched on **every** read of all 22 players × 3 values (thousands of reads) — layout OBS |
| `F` | `*(*(ctx+0x2c0)+0x58)` = **10.0** live (INF high: the match-length setting in minutes — the game was set to 10) |
| tank values | `max` ∈ {296000 … 552000}, all multiples of 4000 = 400·F, e.g. 552000 = 138·4000 = Stamina 99 → 99·140/100 = 138 (INF; per-player rec bytes not yet read live, `tank.py` ready) |
| breath cap | `B ≤ max(pct,20)·1920/100` and `B == cap` for rested players (1862 at pct 97, 1900 at pct 99): 150/154 samples exact, 4 misses are non-atomic reads |
| **A5** | `A5[40] == 0.8 + 0.2·clamp((pct−10)/60)` with integer `pct = cur·100/max`: 150/154 samples exact to 1e-6 (same 4 skewed reads) |
| dead ball | `MI+0x116c == 2` (with `MI+0x11a8` counting ≈ 57/s): **cur unchanged in 15,070/15,070 player-intervals**; `== 1`: changes every tick → drain only in play (OBS) |
| half-time | jump between samples clusters at **≈ 89–90 k, ≈ 122–126 k, ≈ 160.7–161.1 k** = 9000F, 9000F+3600F, 9000F+7200F at F = 10 (minus up to a few k of in-play drift inside the sampling gap), applied once (OBS live1) |
| per-tick rate | 7.4 k single-class intervals (1–8 ticks, both ends in play). Most frequent Δcur/tick per class (histogram `fit.py`), each exactly a §3.3 prediction: breath full (`bp/100 = 1`) + `F68 = 1`: stand **+8**, walk **+6** (1,115×), jog **−2** (977×), run **−17** (1,214×), dash **−22** (1,743×); breath not full + `F68`: run **−23**, dash **−27** (355×); `F68 = 0`: run −16, dash −21; `S = 1` + `F68`: dash **−33** (158×). ~88 % of intervals equal a single-flag prediction exactly; the rest change class between samples. ("Breath full" while dashing is common because the breath test uses the 3-D velocity at +0x574 against 22 km/h while the class uses the speed at +0x3fc, so the two disagree near the threshold.) |
| aggregate drain | per player over ≈ 2,622 in-play ticks, Σ actual / Σ predicted(class at interval start, F68 = 1) = **0.97–1.09 for 20 outfield players, 1.03 overall** (keeper ids 0/11 hardly drain: −4,841 and −1,627) |
| tick rate | `Player+0x10` advanced 2,622 in 46.0 s of wall-clock open play = **57.0 calls/s** (same rate as `MI+0x11a8` at dead balls): one drain step per sim tick, the tick being frame-locked on this machine (each tick = 1/54 s of game time, so game time ran ≈ 1.06× wall time here; INF) |
| **per-tick series (60 Hz, second match)** | `live4.jsonl`, 60 s of open play, 22 players: of **39,221 single-tick steps** (`Player+0x10` advanced by exactly 1, cur not at max/0), **38,195 = 97.4 %** equal `trunc(R·60/54)` exactly for the sampled class/breath state and one of {F68, none, F13, F13+F68, S=1(+F68)}; F68 dominates in every class (e.g. run+breath full: 6,565 F68 vs 1,836 none). The 2.6 % rest: INF(med) steps where the speed or breath crossed a threshold between the sampled frame and the game's tick (non-atomic read) |

Reading: `F68 = 1` (the "+1 drain" flag) holds most of the time even for the attacking side, so it is "own side
does not have the ball under control" (loose balls count), not "defending" (INF med-high). The validation covers
the formula structure, the class constants, the breath term, the dead-ball freeze, the half-time bonus and A5; the
tank inputs (`s'`, arrow, `T`) and the rare flags (G, F12, F13/F14/F16, K) were not isolated live.

## 8. What is now implementable (GF), all integer-exact at gs = 54

1. Per player at kick-off: `max` from §2 with `F = match minutes` (or 10 to copy the default); `cur = max·fresh/100`;
   `B = cap`.
2. Each sim tick while the ball is in play: §3.2 then §3.3, in that order (breath first; `bp` uses the new `B`).
   Minimum faithful set: `cls` from speed (0.05/8/13.5/22 km/h), `spd` table, breath term, `F68` (side not in
   control of the ball). Then `cur<10 → 0`, clamp to `max`.
3. At the half-time whistle once: §3.4.
4. Every tick: `pct`; A5 = `0.8+0.2·clamp((pct−10)/60,0,1)` on the 10 stat ids; breath penalty `p` on 4 ids.
5. AI: §5 work-rate demotion at `dep ≥ 60` (70/+15 skill variants).
6. Contacts: §6 severity, `acc` accumulation, 150/200 states, the 5 %/2 % jackpot; RNG = `math::Random` slot 2
   (`s = (0x4513 − s·0x2D22B2E3) & 0x7fffffff`, draw `trunc(s·2^-31·100)`), starting from the ctor default
   slot-2 state 0x2f3d2 = 193490 every match (OBS live default seeds + no seeding site, §9).

Time scale for a 10-minute match (F = 10): a Stamina-80 outfielder (`s'` = 96, arrow normal, T ≠ 0) has max 384,000;
running loses 17–23 per tick (breath full / not full, side not in control) ≈ 0.9–1.25 k per in-play second at 54
ticks/s, so 5–7 min of continuous running empties him; standing refills +3…+10/tick. A5 starts falling below pct 70
and bottoms at 0.8 at pct ≤ 10. "Match minutes" for `F` is INF(high) from the live 10.0 under a 10-minute setting.


## 9. Other stamina consumers found on the way (OBS decompile)

- **Persistence** `FUN_1404239f0` (MatchControl area): for each on-pitch player, when `MI+0x116c == 1 && PMS+0x61`
  (the "cur changed" flag set by SetCurrent) or at the half-time state (`0x116c == 2 && 0x1574 == 1`):
  `rec[0x5d] = pct`. So `rec+0x5d` is the stored stamina % that seeds `cur` at the next kick-off (§2), and the
  condition arrow `PMS+0x64` is copied back via `0x1408c7710` outside dead balls.
- **Pair/duel weight** `FUN_1405027a0` (the matchlogic §3.4 "pairAnime 0.5/0.8/0.2"): when team instruction 0xd is on
  (and not 0xe for that player), a candidate weight is scaled by `1 − 0.2·clamp((cur − 0.2·max)/(0.6·max), 0, 1)`
  (0.2 @0x142598578, 0.8 @0x142572348) — fresher players get up to 20 % less of that weight. Meaning of the weight
  not traced (INF low: pressing assignment).
- Other `pct`/`cur`/`max` readers not traced: AI 0x1404fefe9, 0x140520e50…0x140524541, 0x1405be1b0, 0x1405c0d8e,
  0x1405e9f50, 0x1405fdaf0, 0x1406218fc, 0x1406b93a0…0x1406d6780, 0x140781b30, 0x1407e3050…0x1407eb1a0, 0x140809144,
  0x140953737, 0x140971ffc, 0x140a1ba71, 0x140a23c10, 0x140a543a1, 0x1420c4500…0x1420ed988, 0x142160a60; breath %
  readers 0x1406b9522, 0x1406e5a95, 0x1406ea04f, 0x1406eabe2, 0x1406f8636, 0x1406f9ac0, 0x140798861, 0x1407b4afa,
  0x1407c29b9, 0x1407d7337, 0x1407e8c61, 0x14081e514, 0x14092fbd8 (anime/action side: INF the `_tired` clip
  selection and action success rolls).
- Injury RNG live (OBS, `find_vt` on vtable 0x14259ce20 → `match::Injury` at a heap address): the Random at
  `+0x1a70` has vtable 0x1425def28 (`math::Random`) and slots **(193488, 193489, 193490, 193491)** = the ctor
  defaults; the Injury reset `0x1404818e0 → 0x143f26fc0` (protected, clean) clears the records but never seeds, and no
  `SetSlotState` call targets `+0x1a70` (OBS scan of all 59 call sites). INF(high): the injury roll sequence restarts
  from the default seed in every match (the object is embedded at `+0xcf8` of the 0x17fd8-byte match object built by
  0x140421fd0/0x140441fc0).

## 10. Open, with what was tried

1. **Stat-id names** for A5 (0x17, 0x18, 0x1a, 0x1b, 0x1c, 0x28–0x2c) and the breath penalty (0x1a, 0x1b, 0x1c, 0x2a).
   Anchors 0x2d = Stamina and 0x2f = resistance are proven; the `.rdata` name pool is ambiguous because the linker
   merged strings. Next: live-read `rec+8+id` for a keeper vs an outfielder (GK abilities stand out) — `tank.py`
   prints both arrays.
2. **`T` = team+0x4c00[slot]** (tank +8000F when 0; half-time +3600F/+7200F for 1–2 / 3–6). Live half-time jumps
   show all three classes occur; its meaning (INF low: a per-player "fitness/rest" level) is not identified.
3. **Rare drain flags** G / F12 / F13 / F14 / F16 / K: structure OBS, semantics INF; only `S = 1` (dash −33) is seen live.
4. `injury.o` (constant_match.bin, ten values 120…250) is **not read** by any of the injury functions decoded here
   (all thresholds are literals); its consumer is still open. The fatigue-injury cause (`PLAY_DATA_INJURY_FATIGUE`) and
   the substitution/stretcher timing were not found in `match::Injury`.
5. `pesSmart.o` `*Tired` / `metabolism` consumers: not the drain; consumer open.
6. **Tank inputs live**: per-tick drain is closed (§7, 97.4 %), but `tank.py` (s', arrow, `T`, base/live stat bytes) could
   not run in the second match: `ctx+0x2c0` and the roster slot `ctx+0x1978` read 0 in 5,000/5,000 reads, while
   `ctx+0x270` and the PMS slots were non-null on 6–8 % of reads. Next: locate the roster through `match::Player`
   (`Player+0x7e0`/`+0x7d8` chains) instead of the context slots.

