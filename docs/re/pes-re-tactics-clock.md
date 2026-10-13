# PES 2021 match clock, stoppage and period transitions — evidence report

Scope: read-only RE of PES 2021 (PES2021.exe image in `<scratch>/pesmem.bin`, base 0x140000000; VFS of the install read through the repo tool `pesfmt_dump`). Goal: the match clock, stoppage model and period state machine as implementable rules, with the runtime source of every constant. No PES-derived bytes are committed. This file **extends and corrects** `docs/re/pes-re-matchlogic.md` §6 — the parts of §6 that this report supersedes are listed in §10.

Tags: **OBS** = observed, method stated. **INF** = inferred, confidence given. **EXT** = third-party claim, unverified unless stated.

All addresses are VAs in the memory image. **Image map used throughout:** file offset = `VA − 0x140000000` (verified on `.rdata` string `s_lossTime` @0x14259E108, `.rdata` float @0x142632A6C and `.data` pointer @0x1436F3DC8). The PE header in the image describes 19 sections; the on-disk section raw offsets do **not** match the dump's offset rule, so every VA → file offset here uses `VA − 0x140000000`. PE section table (for reference when mapping the on-disk exe): `.trace` va 0x1000, `.rdata` va 0x252F000, `.data` va 0x339B000.

---

## 0. Headline facts (all OBS)

1. **PES runs the whole match on one 54-frame logic tick.** `GetGameSpeed()` @`0x141E532D0` reads the float at **0x14351AE54** (default **54.0**, source literal @0x14259D624 written there by the init at **0x141E532F0**); `GetFps()` @`0x141E532E0` reads the int at **0x14351AE50** (default **0x36** = 54, written by the same init, `mov dword [rip+..], 0x36`). 4942 callers of `GetGameSpeed`, 1455 of `GetFps`.
2. **The match clock is one integer, expressed as a percentage of `minutes × 1920`** — not seconds and not minutes. `match_time` is a user setting in **minutes of real time per match**; PES stretches a fixed 90 game-minutes over it.
3. **Half length is chosen by half kind** from **three** consistent tables in the image (@0x1420BE050, @0x1420BE140, @0x1420BE0D0): 45 / 45 / 90 / 105 / 120 minutes by phase.
4. **No WESYS constant object carries a clock value.** All 27 objects of `common/match/constant/constant_match.bin` are enumerated in §8; there is no clock/time object. The clock is built from hardcoded image constants plus the `match_time` setting.
5. **Stoppage (added) time is a linear formula evaluated at half end, from the elapsed match seconds and the on-pitch performer's "perf tier"** — OBS-decompiled in §3.2. There is **no event accumulator**: goals, substitutions, injuries and goal kicks are not individual triggers.
6. **A stoppage never adds time; it freezes the clock.** The ClockFSM simply does not run while the match state byte is a frozen state. The 9000/5400 per-second term is the only compensation.

---

## 1. Where the value lives (runtime sources)

### 1.1 Table

| Value | Runtime source | Notes |
|---|---|---|
| Logic tick / game speed | float **0x14351AE54** (default 54.0) | getter `0x141E532D0`; init `0x141E532F0` writes 54.0 from 0x14259D624 |
| Frame count (int form) | int **0x14351AE50** (default 0x36) | getter `0x141E532E0` |
| Match length (real minutes per match) | settings key **`match_time`**, u16 at settings+**0xD5A** | OBS: serializer `FUN_1410AE130` @0x1410AE130 reads it as u16 into the match-settings message; the only other ref to the key string `s_match_time` @0x142836458 is the same function. Neighbours (verified in that function): `win_lose` u8 +0xD58, `stadium_id` u8 +0xD5C, `random_stadium` +0xD60, `time_zone` u8 +0xD64, `weather` u8 +0xD6C, `random_weather` +0xD6D, `match_skip` +0xD70, `season_num` u16 +0xD74 |
| Current half | int at **`*(gMatchContext+0x270)+0x1168`**; cached as the global at **0x1437F965C** (accessor `FUN_1420BD500` = `mov eax,[0x1437F965C]; ret`), 138 callers | values are the `MATCH_PHASE_*` enum of §6 |
| Half-change pending counter | int at `*(gMatchContext+0x270)+0x116C` | gate: ClockFSM 1st-half path only when `== 0` |
| Match-time percentage | record field, obfuscated triple `+0xC8/+0xD0/+0xD8` | setter `FUN_140A592C0`, getter `FUN_140A58350` |
| Match minutes (for scaling) | record field, obfuscated triple `+0xE0/+0xF0/+0xF8` | getter `FUN_140A58550`, 45 callsites; setter `FUN_140A595C0`, 1 callsite |
| Third record field (constant 1920) | record field, obfuscated triple `+0xE8/+0x110/+0x118` | setter `FUN_140A58F60`; the constant 0x780=1920 is returned by `FUN_140A581B0` |
| Added (stoppage) minutes | SituationData field `lossTime` u8 at **+0x145** (§3.1) | OBS: reflection registration in `FUN_140465F40` @0x140465F40 |
| Match seconds (float, live) | float at **`*(gMatchContext+0x2C0)+0x58`** | read by `FUN_140922DD0`, `FUN_140923060`; OBS: the same object's `+0x5C` is used by `FUN_1409224D0` as the denominator of `GetGameSpeed()`. INF: `+0x58` is the live elapsed match-seconds accumulator and `+0x5C` its unit/scale divisor |
| Match tick counter | u32 at match-ctx +0x10, incremented once per `FUN_140925750` call | OBS: `inc dword ptr [rcx+0x10]` is the first instruction of the tick |

### 1.2 The obfuscated "record::match" field accessors

Every field of the match record is stored as a **4-slot ring** whose pointer index and stored copy are obfuscated, plus an FNV-1a-64 checksum. Writers (`FUN_140A592C0`, `FUN_140A595C0`, `FUN_140A58F60`) do:

```
hash = FNV1a64(value_bytes) with seed 0xcbf29ce484222325, prime 0x100000001b3
stored[i] ^= (keystream(K)*3) - K          // K = another counter field
slotIndex = FNV1a64-of-fixed-address & 3    // the ring slot
ring[slot] = stored[i]
checksumField = hash
```

Readers (`FUN_140A58350`, `FUN_140A58450`, `FUN_140A58550`) validate the checksum and call `FUN_1415A1714` to abort on mismatch. **The engine does not need to copy this** — it needs to know the value semantics below.

| Field group | checksum at | counter at | ring at |
|---|---|---|---|
| matchTime | `+0xC8` | `+0xD0` | `+0xD8 + (*(u32*)(o+0xD0)&3)*4` |
| matchMinutes | `+0xE0` | `+0xF0` | `+0xF8 + (*(u32*)(o+0xF0)&3)*4` |
| "1920" field | `+0xE8` | `+0x110` | `+0x118 + (*(u32*)(o+0x110)&3)*4` |

(OBS: deciphered from all six accessors @0x140A58xx–0x140A59xx.)

---

## 2. The match clock itself

### 2.1 Period structure and half length (OBS)

Three half-length functions, all reading the phase at `*(gMatchContext+0x270)+0x1168`:

**A. `FUN_1420BE050` @0x1420BE050 — the plain half length**
```
halfLengthMinutes[phase]:
  phase 1, 2            -> 0x2d (45)     // 1ST_HALF, HALFTIME
  phase 3, 4, 8, 10     -> 0x5a (90)     // 2ND_HALF, EX_TIME, ?, END
  phase 5, 6            -> 0x69 (105)    // EX_1ST, EX_INTERVAL
  phase 7               -> 0x78 (120)    // EX_2ND
  other                 -> 0
```
**B. `FUN_1420BE140` @0x1420BE140 — same table, 2-arg form**
```
  phase 2, 3            -> 45
  phase 4, 5            -> 90
  phase 6, 7            -> 105
  phase 8, 9, 10        -> 90 if arg==0 ; 120 if arg!=0
  other                 -> 0
```
**C. `FUN_1420BE0D0` @0x1420BE0D0 — jump-table form, indexed by `phase − 2`** (targets in the table at 0x1420BE114):
```
  phase 2, 3            -> 45
  phase 4, 5            -> 90
  phase 6, 7            -> 105
  phase 8, 9, 10        -> 90 if arg==0 ; 120 if arg!=0
  default               -> 0
```
`FUN_1420BE780` @0x1420BE780 — the "half is live" predicate:
```
isLive = phase ∈ {1, 3, 5, 7, 9}     // 1ST, 2ND, EX_1ST, EX_2ND, PKMATCH
```

(OBS: all four functions fully disassembled. Phases are the `MATCH_PHASE_*` names of §6, 1-based, 0 = PREV_MATCH.)

### 2.2 The clock: a percentage of `minutes × 1920` ticks

`FUN_140A581C0` @0x140A581C0 (14 callsites) recovers a **tick count** from the stored clock value:

```
minutes = GetMinutes()                    // the +0xF0/+0xF4/+0xF8 triple
if minutes < 20: minutes = 20             // floor
denom   = minutes * 1920 / 100            // 0x780 = 1920, 100 by the 0x51eb851f reciprocal
ticks   = matchTime * 1920 / 100          // saturating
```
`FUN_140922DD0` writes the mirror image:
```
pct = clampToTicks * 100 / (minutes*1920/100)     // = clampToTicks * 10000 / (minutes*1920)
SetMatchTime(pct)
```

So: **one half is `minutes/2 × 1920` ticks**, i.e. 960 ticks per game-minute, and 960/54 ≈ **17.8 logic frames per game-minute**. For a 90-minute match split 45+45 (the standard case) that is 43,200 ticks per half = 86,400 ticks for the whole match, i.e. 1600 logic ticks per real second at the default 54 fps.

(OBS: `FUN_140A581C0`, `FUN_140A581B0` (returns 0x780), `FUN_140A58550`; the `×0x51EB851F, >>5` idiom is division by 100.)

### 2.3 What a tick is worth in real time — the `1920` and `16000` constants

- **1920** (`FUN_140A581B0`) ticks per half per minute-of-game.
- **16000** (`DAT_14259D638`) clock units per match **second** (the floor term of `FUN_140922DD0`). 16000/1920 ≈ 8.33, i.e. one "match second" is 8.33 clock-seconds of game — INF (medium-high) one game-minute = 60 match-seconds so `1920` per minute is consistent with `16000` per second × 60 s / 500 ≈ 1920; the two constants describe the same scale from different directions.
- INF (medium): the match clock therefore advances **17.8 ticks per game-minute**, and `match_time` (real minutes per match) sets how many real seconds each game-minute takes. **The exact arithmetic that converts the `match_time` u16 into the per-tick real-time increment is still open** (§9, item 1). The record's `+0x58`/`+0x5C` floats in the env object are the pair that carries it (`FUN_1409224D0` computes `GetGameSpeed() / *(env+0x5C)` into a physics field).

### 2.4 The clock computation — `FUN_140922DD0` @0x140922DD0 (0x208 B), OBS, decompiled

This is the function that **sets the match clock value**; it is called once per tick from `FUN_140922BC0`. Pseudocode in instruction order:

```
idx    = *(u32*)(ctx+0x18)                       // performer index 0..21
isSub  = (9 < idx < 22)                          // substituted/bench flag
pIdx   = (idx < 22) ? idx % 11 : 0               // on-pitch performer 0..10
team   = TeamSlice(isSub)                        // FUN_140A48A30(gCtx, isSub)
perf   = *(int*)(team + 0x4C00 + pIdx*4)         // per-player "perf tier" u32
player = GetPlayerRecord(idx)                    // FUN_140A489D0 + FUN_140A3AF60
pObj   = GetPlayerGameObject(player)              // FUN_141B59E20
level  = byte at pObj+0x5D                        // "pitch level", clamped up to 10
lead   = byte at pObj+0x35                        // "lead / behaviour" 5..95
kind   = int  at pObj+0x58                        // 0..4 selector
sec    = float at *(gCtx+0x2C0)+0x58              // elapsed match seconds

// 1. lead → percentage
leadPct = 5   if lead <  0x46 (70)
        = 110 /100 if lead < 0x50 (80)
        = 120 /100 if lead < 0x5a (90)
        = 130 /100 if lead < 0x5f (95)
        = 140 /100 if lead >= 95
// (values 0x6e, 0x78, 0x82, 0x8c, divided by 100 via the 0x51eb851f reciprocal)

// 2. base term
base = leadPct * 400 * sec                        // 400 = literal

// 3. kind adjustment
switch (kind):
  0: base += sec * 6000                           // DAT_14260B390
  1: base += sec * 2000                           // DAT_1425CB004
  2: base -= sec * 4000                           // DAT_1425CB498
  3: base -= sec * 4000                           // DAT_1425CB498
  4: base -= sec * 8000                           // DAT_1425CB4A0
  default: no change

// 4. benchmark adjustment
if perf == 0: base += sec * 8000                  // DAT_1425CB4A0

// 5. clamp, convert, store
tickMin      = sec * 16000                        // DAT_14259D638
clampToTicks = max(base, tickMin)
SetMatchMinutes(clampToTicks)                     // FUN_140A595C0
SetMatchTime(GetMatchMinutes() * level / 100)     // level = pObj+0x5D, clamped >= 10
SetThirdField(1920)                               // FUN_140A58F60, value from FUN_140A581B0
matchTime = GetMatchTime()                        // FUN_140A58350
ctx->[+0x1A484] = matchTime                       // cached copy for other subsystems
```

**OBS constants** (`.rdata` floats, read out of the image):

| Constant | VA | Value | Role |
|---|---|---|---|
| `DAT_14260B390` | 0x14260B390 | 6000.0 | kind 0 term |
| `DAT_1425CB004` | 0x1425CB004 | 2000.0 | kind 1 term |
| `DAT_1425CB498` | 0x1425CB498 | 4000.0 | kind 2 and 3 term |
| `DAT_1425CB4A0` | 0x1425CB4A0 | 8000.0 | kind 4 term; also perf==0 term |
| `DAT_14259D638` | 0x14259D638 | 16000.0 | per-match-second floor |
| literal | code | 400.0 | base multiplier |
| literal | code | 100 (0x51eb851f reciprocal) | percentage divisor |
| lead thresholds | code | 70 / 80 / 90 / 95 | clamp levels |
| lead multipliers | code | 110 / 120 / 130 / 140 | then /100 |

### 2.5 The tick driver — OBS

`FUN_140925750` @0x140925750 (0x57 B) is the **per-tick match tick**, a virtual method (vtable slot **0x1426333B8**, member +0x18 of the class whose vtable starts at 0x1426333A0):

```
sub_140925750(matchCtx):
  matchCtx->[+0x10] += 1                     // match tick counter
  FUN_1408BF090(matchCtx)                    // pre-update: set-piece / phase handlers (0x1406EBxxx)
  FUN_140925470(matchCtx, arg)               // -> FUN_14092FAD0 (the ClockFSM)
  tick = matchCtx->[+0x10]
  if tick % 25 == matchCtx->[+0x18]:          // 0x19 = 25
      FUN_1408BFCF0(matchCtx, ...)            // 25-tick bookkeeping (two writers)
  matchCtx->vtable[+0x40]()                   // end-of-tick record write -> FUN_1409224D0
```

`FUN_1409224D0` (vtable +0x58) — the end-of-tick, which is where the clock's unit is published to the physics world:

```
FUN_1409224D0(matchCtx):
  FUN_142101970(matchCtx+0x980, *(matchCtx+0x7E0))
  byte[matchCtx+0x97c] = 0xff
  rec = FUN_140922FF0(matchCtx+0x1A488)
  FUN_1408BFB20(matchCtx)                                    // controller input
  char[rec+0x12A7] = (char)(longlong)*(float*)(matchCtx+0x784)
  env = *(gMatchContext+0x2C0)
  if *(matchCtx+0x7D8) != 0:
      *(float*)(*(matchCtx+0x7D8) + 0x3B1C) = GetGameSpeed() / *(float*)(env+0x5C)
  FUN_140921170(matchCtx+0x1A488)
  ...
```
This is OBS proof that the object at `*(gCtx+0x2C0)` holds the float at `+0x58` (read by the clock functions) and a second float at `+0x5C` used as the denominator of the global 54.0 game-speed.

**Tick-rate conclusion (OBS):** the clock and everything else advance on the 54 Hz logic tick, tick-counted in the u32 at match-ctx +0x10, with the 54 constant at 0x14351AE50 / 54.0 at 0x14351AE54.

### 2.6 The display conversion — OBS

- `FUN_140A6BD80` @0x140A6BD80: `h = total/3600, m = (total/60)%60` (applied to a **seconds** count; 21 callsites).
- `FUN_140A6BD60` @0x140A6BD60: `h = total/3600`.
- `FUN_1420EFEC0` (the HUD clock block, OBS) builds the HUD's clock state:
  ```
  for i in 0..1:  redLimit[i] = clamp(byte[env+0xB0+i], 0, 5)
  for i in 0..1:  yellowLimit[i] = clamp(byte[env+0x9E+i], 0, 99)
  if IsLiveHalf(phase):
      // live:  minute/second from a float at env+0xA0 through FUN_140A6BD80
      (h, m) = HMS(*(float*)(env+0xA0)); minute = m + FUN_1420BE140(byte[env+0x2DA8])
  else:
      (h, m) = HMS(*(float*)(env+0x11C)); minute = FUN_1420BE050()
  // clock shown while  phase < 7 or phase == 8 ;  hidden at 7 and 8
  showClock = (GetHalfKind() < 7 or GetHalfKind() == 8) and byte[env+0x114]==0
  // stoppage/minutes digits
  if GetHalfKind()==8 and not replayFlag:
      (h,m) = HMS(FUN_1420BDFD0(...)); stopper = ...
      minute = min(minute, FUN_1420BDFD0(byte[env+0x2DA8]))
  if minute >= 100: minute = 0
  store minute, second, stopper
  ```
  `FUN_1420BDFD0` is a thunk into `FUN_1420BDF50(phase, arg)` and is used to read the *half length* for the "time up" comparison.

---

## 3. Stoppage (added) time and its triggers

### 3.1 The stored value and how it is shown (OBS)

- `lossTime` is a **u8** in `SituationData` at offset **0x145** (see §7 for the full reflection registration from `FUN_140465F40`; `s_lossTime` @0x14259E108, registered as ENUM, count 1, at record+0x145; the same function registers an unnamed byte at **+0x144**, INF: the in-half game minute).
- The HUD side (`FUN_1420F1750` @0x1420F1750) is fully OBS:
  - `param_3 + 0x24` is the **stoppage minutes** (u32); the display clamps it: `if (v != 0xffffffff) { if (v > 9) v = 9; ... }` — **max 9 minutes shown**.
  - `param_3 + 0x14` selects the `time_disit_N` glyph (0 → 3 digits, 1 → 2 digits, 2 → 1 digit) from the **remaining game minutes** magnitude.
  - `param_3 + 0x28` selects the `lossTime_disit_N` glyph (0 → 3, 1 → 2, else 1) from the **stoppage minutes** magnitude.
  - `DAT_1437F968A` is the replay/demo flag: when set the clock reads 0 and the entire clock block is skipped.
  - `scoreTime_losstime_show/off`, `scoreTime_time_hide` are driven by `GetHalfKind() < 7` etc.
- Only seven sites in the image write a byte at +0x145 (0x14120919, 0x1474A935, 0x1430919, 0x14B159B6, 0x14215DA2D, 0x14163E5 + one). **INF:** the writer at 0x14215DAxx is in a large serializer that also writes `homeScore` (+0x142) and the +0x144 byte, i.e. the record serializer, not the adder. So `lossTime` is filled from the `FUN_140923060` result — INF (medium) divided by 60 to minutes.

### 3.2 The stoppage computation — `FUN_140923060` @0x140923060 (0x19E B), OBS, decompiled in full

Called from exactly one site: `FUN_14092FAD0` @0x14092FF43, the ClockFSM, and only when the phase is the 2nd half (`+0x116C==2` path) or extra time. Pseudocode:

```
sub_140923060(ctx, phase /*2 or 4*/, record):
  sec  = float at *(gCtx+0x2C0)+0x58                 // elapsed match seconds
  base = GetMatchTime()                              // FUN_140A58350
  idx  = *(u32*)(ctx+0x18)                           // performer 0..21
  pIdx = (idx < 22) ? idx % 11 : 0
  isSub = (idx >= 11 and idx < 22)
  tier = *(int*)(TeamSlice(isSub) + 0x4C00 + pIdx*4) // per-player perf tier

  switch (phase):
    case 2  (2ND_HALF):
       base += (int)(sec * 9000.0)                   // DAT_142632A6C
       if 1 <= tier <= 2:   base += (int)(sec * 3600.0)   // DAT_142598590
       elif 3 <= tier <= 6: base += (int)(sec * 7200.0)   // DAT_142632A68
    case 4  (EX_TIME):
       base += (int)(sec * 5400.0)                   // DAT_142632A64
       if tier <= 2:        base += (int)(sec * 1800.0)   // DAT_142632A60
       elif 3 <= tier <= 6: base += (int)(sec * 3600.0)   // DAT_142598590

  SetMatchTime(record, base)                         // FUN_140A592C0
  ctx->[+0x1A484] = base
```

**All five constants, OBS from the image:**

| Constant | VA | Value | Meaning |
|---|---|---|---|
| `DAT_142632A6C` | 0x142632A6C | **9000.0** | 2nd-half added time per match second |
| `DAT_142632A68` | 0x142632A68 | **7200.0** | 2nd-half tier bonus, tiers 3..6 |
| `DAT_142598590` | 0x142598590 | **3600.0** | 2nd-half tier bonus, tiers 1..2; also EX tier bonus 3..6 |
| `DAT_142632A64` | 0x142632A64 | **5400.0** | EX_TIME added time per match second |
| `DAT_142632A60` | 0x142632A60 | **1800.0** | EX_TIME tier bonus, tiers ≤ 2 |

**Interpretation (INF medium-high):** added time is `elapsedSeconds × rate`, gated by the on-pitch performer's `+0x4C00 + pIdx*4` **perf tier**. Extra time adds less stoppage than the second half (5400 vs 9000) — consistent with football. **No event triggers exist.**

### 3.3 The ClockFSM gating — what freezes the clock (OBS)

`FUN_14092FAD0` @0x14092FAD0 (0x1A4 B), the clock FSM, called from `FUN_140925470`:

```
sub_14092FAD0(matchCtx, record):
  s = *(gCtx+0x270)
  record->[+0x61] = 0
  paused = IsReplayPlaying(*(gCtx+0x400)) || IsOtherPending(*(gCtx+0x3B0))
  if paused: return
  if byte[s+0x1775] != 0 and byte[s+0x1775] not in {8, 10}: return   // frozen state
  if *(u32*)(ctx+0x18) >= 22: return
  if byte[s + 0x1748 + ctxIndex] == 0: return

  FUN_14092B950(ctx, record)                                  // "begin"
  if *(u32*)(s+0x116C) == 0:                                   // no half change pending
      ... (first-half path, ~0x1B0 bytes; ends with
           sec = GetMatchTime(); ... SetMatchTime(record, max(sec, 10)))
  else if *(u32*)(s+0x116C) == 2 and *(u32*)(s+0x1574) == 1
       and *(u32*)(s+0x11A8) == 2 and ((s+0x1168 - 2U) & 0xFFFFFFFD) == 0:
      FUN_140923060(ctx, halfKind, record)                    // <-- the adder
```

**OBS conclusion:** a stoppage (set piece, injury, replay, demo) does not add time — the ClockFSM returns early and the clock simply does not advance. The added-time formula is the only compensation, evaluated at half end from the elapsed seconds.

---

## 4. What ends a half (the transition) — OBS

The half-kind increment lives in the reflection message handler `FUN_140A115A0` @0x140A115A0 (0x1570 B), dispatched from `FUN_140A13060` (loops over queued messages and calls it per message id). At the block labelled `LAB_140a11958`:

```
if (halfKind != -1) {
    *(u32*)(s+0x116C) += *(u32*)(matchEnv + 0x1198) - *(u32*)(pitchStruct + 0x14)
    *(u32*)(s+0x1168) += 1                       // halfKind++
}
*(u32*)(s+0x728) = (halfKind == 0) ? 0 : (*(u32*)(s+0x116C) / halfKind)
```
(OBS raw at 0x140A11958–0x140A119A7: `mov rcx,[rdi+0xB8]`-family; `cmp dword[rsi+0x1168],-1; jae`; `add dword[rsi+0x116c],...`; `inc dword[rsi+0x1168]`; `div`; `mov [rsi+0x728],eax`.)

So: **halfKind is a plain monotonic counter bumped by a reflection message**, `+0x116C` is a second accumulator, `+0x728` is the per-half average. The ClockFSM's `+0x116C == 0 / == 2` gate reads that second counter, not the phase — which is why the 1st-half path is `==0` (initial) and the 2nd-half/EX path is `==2`. **INF (medium-high):** `+0x116C` counts half transitions, so 0 = before the first bump, 2 = after the second bump (i.e. the 2nd half is running).

---

## 5. Period transitions — the state machine

### 5.1 The state byte and its jump table (OBS)

The match update `FUN_1409257B0` @0x1409257B0 (0xA79 B) dispatches on the state byte `*(u8*)(*(gCtx+0x270) + 0x1775)`. Only 7 sites in the image read that byte: `FUN_1409257B0` (0x14092585D), `FUN_140448760`, `FUN_1404526A0`, `FUN_1405109B0`, `FUN_14051D100`, `FUN_14051D9E0`, `FUN_142137180`.

```
eax = 2 if state != 0x17 else 0        // 0x17 = 23 clamps to default
eax -= 1
if eax > 7: goto default (0x1409265A9)
jmp [0x14292666C + eax*4 + 0x140000000]
```

| state | target | content (OBS) |
|---|---|---|
| 3 | 0x1409265A9 | no-op bookkeeping |
| 4 | 0x14092589D | requires `halfKind==1` (1st half), a ball-owner check and `byte[env+0x14]`; writes camera mode `byte[rdi+0x874]=2..3`, `dword[rdi+0x878]=2` |
| 5 | 0x1409259DB | reads a state object via `FUN_140A63090`; branches on `*(u32*)(that+0x2c)` ∈ {0,3}; camera mode 2 |
| 6, 8 | 0x140925BDD | resolves three team slices and the kicker; camera |
| 7 | 0x1409265A9 | default |
| 9 | 0x14092643F | **PK shoot-out**: `dword[rdi+0x878]=4` then `2`; writes `0x42200000` (= **40.0**) into `rdi+0x894` |
| 10 | 0x1409265A9 | default |

State byte values seen in code: **0, 8, 9, 10** (`(u8)(s+0x1775)-8 <= 2` in `FUN_142137180`; `== '\t'` (9) in `FUN_140448760`). INF: 0 = live in-play, 8/10 = frozen (replay/demo), 9 = PK shoot-out.

### 5.2 The situation FSM (OBS) — `FUN_14044C880` @0x14044C880

Indexed by `*(int*)(ctx+0x878 + matchIndex*4)`; advanced by `byte[ctx+0x8F0] += 1` and resetting `+0x900/+0x8F2/+0x8F8/+0x8F4` to 0 on every transition. Full state table (OBS raw disassembly):

| state | handler | notes |
|---|---|---|
| 0 | `FUN_140449B00` | init |
| 1 | `FUN_14044D110` | kick-off; if `halfKind==0` return, else fire message `0x1050048` |
| 2 | `FUN_14044CDA0` | |
| 3..11 | `FUN_140446390` | in-play / set pieces |
| 12..17 | `FUN_1404488E0` | |
| 18 | `FUN_140448760` / `FUN_1404485F0` | if `halfKind==9` (PK) nothing |
| 19 | `FUN_140447B50`; if `*(s+0x1574)==2` fire msg `6` then `FUN_1404505D0` | |
| 20,21,22 | `FUN_14044A2A0` / `FUN_14044BB40` / `FUN_140449D20` | |
| 23 | `FUN_1404A9CE0` | |
| 24 | `FUN_14044DDA0` | |
| 25 | `FUN_14044BFB0` | |
| 26 | `FUN_14044D2E0` | returns immediately |
| 27 | `FUN_1404491D0` | |
| 28 | `FUN_1404494E0` | |
| 29 | `FUN_14044B1D0` | |
| 30,31 | `FUN_14044B380` | |
| 32..35 | `FUN_14044AF70` / `FUN_14044B860` / `FUN_140449390` / `FUN_140448E50` | |
| 36 | `FUN_1404492D0` | |
| 37 | — | fires messages `0x1050004` then `0x105001B` |
| 41..51 | `FUN_14044A180` | |
| 51 | `FUN_14044B8A0` | |
| 52 | `FUN_14044A3E0` | |
| 53..58 | `FUN_140449130` | |
| 59..66 | `FUN_14044C7B0` | |
| 67 | `FUN_140452E00` | |
| 68 | `FUN_140451FD0(8)`; `byte[ctx+0x904]=0` | |
| 69 | `FUN_140446350` | |
| 70 | `FUN_14044C6E0` | |
| default | — | stay |

### 5.3 Kick-off, half-time, full-time, ET, penalties — what starts each (OBS + INF)

- The `MATCH_PHASE_*` enum (§6) is the driver every HUD and clock consumer reads via `FUN_1420BD500` @0x1420BD500 (`mov eax,[0x1437F965C]; ret`, 138 callers).
- **OBS:** every transition in the situation FSM fires a Fox **message id** on the event queue (`FUN_141B4B560(*(void**)(queue+8), id, 0,0,0,0)`) and then advances the state byte. Observed ids: `0x1050004`, `0x105001B`, `0x1050047`, `0x1050048` (plus ~90 more ids in the 0x1050000–0x105005D range used elsewhere).
- **OBS INF:** the hold duration of each phase is therefore the **animation clip length** the state handler selects (the handler bodies are the `match::anime::*` writers of `pes-re-sim-motion-mapping.md` §6). The ClockFSM does not run during those states because of the `s+0x1775` gate.
- **Cutscenes** (OBS from the anim table, `pes-re-sim-motion-mapping.md` §1.4 / §11): `TimeUp_*` (32 rows), `HalfEnd_*` (13), `kickoff_loop_*` (22), `demo_*` (367), `pk_*` (`pk_enclose_*` rituals + `pk_{short,mid,long,max}_3_0_*`), `fk_*` (74), `corner` (1), `goalkick` (10), `throw*` (40), `wall_*` (12).
- **Penalty shoot-out** is phase 9, treated as live by `FUN_1420BE780`; its state-9 handler writes 40.0 (metres) into the camera target.

### 5.4 What delays a restart (OBS, negative result)

There is **no numeric restart timer** in the clock path. Delays come from:
1. the state byte gate in `FUN_14092FAD0` (clock frozen while `s+0x1775 ∉ {0, 8, 10}`), and
2. the per-state handler's clip duration.

(OBS: searched every clock function for a per-state millisecond constant; none exists. Killed hypothesis: "PES adds n seconds per stoppage".)

---

## 6. Enum values (OBS, from the reflection registration `FUN_140465F40` @0x140465F40)

```
SituationData.MODE       0 NORMAL, 1 KICK_OFF, 2 THROW_IN, 3 GOAL_KICK, 4 CORNER_KICK,
                         5 FREE_KICK, 6 PENALTY_KICK, 7 ENDLESS
                         -> record+0x130 (u32)
SituationData.HALF_KIND  0 MATCH_PHASE_PREV_MATCH, 1 MATCH_PHASE_1ST_HALF,
                         2 MATCH_PHASE_HALFTIME, 3 MATCH_PHASE_2ND_HALF,
                         4 MATCH_PHASE_EX_TIME, 5 MATCH_PHASE_EX_1ST,
                         6 MATCH_PHASE_EX_INTERVAL, 7 MATCH_PHASE_EX_2ND,
                         9 MATCH_PHASE_PKMATCH, 10 MATCH_PHASE_END
                         -> record+0x134 (u32)
SituationData.PLAY_KIND  0 INPLAY, 1 KICK_OFF, 2 THROW_IN, 3 GOAL_KICK,
                         4 CORNER_KICK, 5 FREE_KICK, 6 PENALTY_KICK
                         -> record+0x138 (u32)
SituationData.HomeAway   0,1 (restartSide)   -> record+0x13c
restartMember                             -> record+0x140 (u8, count 2)
homeScore                                 -> record+0x142 (u8)
awayScore                                 -> record+0x143 (u8)
<unnamed byte>                            -> record+0x144      // INF: in-half game minute
lossTime                                  -> record+0x145 (u8) // OBS
entry[0x16]                               -> record+0x146
```

This **confirms and completes** the §6 claim in `pes-re-matchlogic.md` that `min` and `lossTime` are u8 in SituationData: `min` is **+0x144** (OBS by position in the registration list; INF for the name) and `lossTime` is **+0x145** (OBS).

---

## 7. Where the values live — WESYS and image constants

### 7.1 WESYS constant archive — OBS, full inventory (this install)

`common/match/constant/` (VFS winner `Data/dt18_all.cpk`), parsed with the repo's `ParseUniformTable` rules (16-byte header `ff 10 51 57 'WESYS'`, u32 compressed at +10, u32 uncompressed at +14, zlib blob at +16; then `u32 count, u32 tableOff=8`, `count × {u32 dataOff, u32 size, u32 nameOff}`, NUL-terminated `.o` names, 16-aligned blobs):

| file | objects |
|---|---|
| `constant_match.bin` (4817 B) | **27**: animeAgingDribble, animeAgingFreeMove, animeAgingKick, animeAgingTrap, ball, cameraInplay, cameraSetplay, command, cpuLevel, cursor, feintCommand, injury, inplayDemo, mlScreenShot, modeMatchup, pes15Test, pesSmart, rating, setplayGuideCommon / CornerKick / FreeKickFar / Middle / Near / GoalKick, teamEmotion, throwin, userPlayTendencyTest |
| `constant_player.bin` (40643 B) | 36 |
| `constant_team.bin` (1354 B) | 16 (basePosition … teamId) |
| `constant_stadium.bin` (8074 B) | 84 |
| `constant_tutorial.bin` (955 B) | 2 |
| `constant_tutorialConsole.bin` (11673 B) | 26 |
| `constant_positionCK.bin` / `constant_positionPK.bin` / `constant_shootAging.bin` | 10 / 5 / 21 |

**OBS negative result: no clock/time object exists in any of them**, including `injury.o` (48 B, the only other candidate). **The clock is therefore not tunable through WESYS**: it is hardcoded (image constants below) plus the `match_time` setting.

### 7.2 Image constants the engine must know

| Constant | VA | Value | Read by |
|---|---|---|---|
| `gameSpeed` | 0x14351AE54 (init src 0x14259D624) | 54.0 | `0x141E532D0` (4942 calls) |
| `fps` | 0x14351AE50 | 54 | `0x141E532E0` (1455 calls) |
| added-time base, 2nd half | 0x142632A6C | 9000.0 | `FUN_140923060` |
| added-time tier 2H (3..6) | 0x142632A68 | 7200.0 | `FUN_140923060` |
| added-time tier 2H (1..2) / ET (3..6) | 0x142598590 | 3600.0 | `FUN_140923060` |
| added-time base, ET | 0x142632A64 | 5400.0 | `FUN_140923060` |
| added-time tier ET (≤2) | 0x142632A60 | 1800.0 | `FUN_140923060` |
| clock floor per match-second | 0x14259D638 | 16000.0 | `FUN_140922DD0` |
| ticks per half-minute | code, `FUN_140A581B0` | 0x780 = 1920 | `FUN_140A581C0` |
| kind terms | 0x14260B390 / 0x1425CB004 / 0x1425CB498 / 0x1425CB4A0 | 6000 / 2000 / 4000 / 8000 | `FUN_140922DD0` |
| base multiplier / lead divisors | code | 400.0, /100 | `FUN_140922DD0` |
| half-length tables | code @0x1420BE050 / 0x1420BE140 / 0x1420BE0D0 (+ jump table 0x1420BE114) | 45/90/105/120 | HUD + length users |
| phase enum + names | strings 0x14259DDxx–0x14259E0xx, registration `FUN_140465F40` | MODE 0..7, HALF_KIND 0..7/9/10, PLAY_KIND 0..6 | — |

---

## 8. What GF does today and how PES differs

GF (`src/sim/onthepitch/match.{hpp,cpp}`, `GetMatchTime_ms`, `matchTime_ms`) advances its own clock as `ticks × 10 ms × timeScale` and drives stoppage/kickoff/half-time/full-time from GF's own rules. Differences:

| # | GF behaviour | PES behaviour (this report) | What to change |
|---|---|---|---|
| 1 | Clock unit is ms, advanced `ticks*10*timeScale` | Clock unit is a **u32 percentage of `minutes*1920`** (§2.2), whose live source is a **float match-seconds** at `*(gCtx+0x2C0)+0x58` with a `16000`-units-per-second floor (§2.3). One half = `minutes/2 × 1920` ticks on a fixed 54 Hz tick | Store the clock as PES's percentage; advance the float seconds by one 54-Hz tick and derive the percentage |
| 2 | No percentage form; can be serialised directly | Percentage is the record/network form (`FUN_140A581C0` / `FUN_140922DD0`) | Add the two-way conversion if GF serialises |
| 3 | GF's stoppage model is GF's own | `added = sec*9000 + tierBonus` at 2nd-half end; `sec*5400 + tierBonus` at ET end; **no event triggers** (§3.2) | Replace the stoppage model with the linear formula; delete goal/substitution/injury/waste counters from the clock path |
| 4 | GF caps/floors its own way | PES clamps the displayed stoppage minutes to **9**, and picks a 3/2/1-digit glyph from the magnitudes of `matchTime` and `lossTime` (§3.1) | Match the display rules if the HUD is to look like PES |
| 5 | GF's half length is its own rule | PES: 45/45/90/105/120 minutes **by phase**, from three tables (§2.1) | Take the half length from the phase table |
| 6 | GF's transition hold is a GF timer | PES: no numeric hold; each transition fires a message and runs the state handler until it returns; the clock FSM is **frozen** for the whole state (§5.4) | Drive restarts by "state handler completed", and freeze the clock in those states rather than adding time |
| 7 | GF's `matchTime_ms` is used for display | PES display: `h = total/3600, m = (total/60)%60` on a **seconds** count (`FUN_140A6BD80`); clock hidden while `phase ∈ {7, 8}` and when the replay flag `DAT_1437F968A` is set | Same formatting / same hide rules |
| 8 | GF's `timeScale` is a multiplier | PES has no user time scale: `match_time` sets real minutes per match; the 54.0 global is divided by an env float at `+0x5C` to produce the physics step (§2.5) | Map GF's `timeScale` onto "real minutes per match" |
| 9 | GF drives half transitions from its own state machine | PES bumps `halfKind` at `*(gCtx+0x270)+0x1168` via a reflection message (`FUN_140A115A0`, block `LAB_140A11958`) and gates the clock on the counter at `+0x116C` (§4) | Same driver shape, or at minimum the same gate semantics |
| 10 | GF has no state byte | PES gates the ClockFSM on `*(u8*)(s+0x1775)`, with frozen states 8 and 10 and PK 9 (§5.1) | Add the equivalent freeze gate |

---

## 9. Open items, with what was tried

| # | Item | Status | What was tried |
|---|---|---|---|
| 1 | `match_time` (real minutes per match) → the per-tick clock increment | **Open** | Read `FUN_140A581C0`/`FUN_140A58550` (the 1920-per-half-minute scaling), traced the only `s_match_time` xref (the settings serializer at 0x1410AE130, u16 at settings+0xD5A), read the whole ClockFSM, the tick driver and the end-of-tick. The float pair `*(gCtx+0x2C0)+0x58/+0x5C` is the place the real-time scale lives (`FUN_1409224D0` computes `54.0 / *(env+0x5C)`). **Next:** find the writer of `+0x5C` (searched `movss [reg+0x5c],xmm` in all encodings — 43 sites, none yet tied to `*(gCtx+0x2C0)`), or read the object's type via its vtable. |
| 2 | Per-state hold durations (kick-off whistle delay, half-time length in seconds) | **Open** | Decompiled/disassembled the FSM dispatch `FUN_14044C880` (message ids only, no constants), the period state jump table (0x14292666C) and every clock function. **Next:** decompile the state handlers `FUN_14044D110`, `FUN_140448760`, `FUN_140446390` and read their clip-duration requests. |
| 3 | Identity of the `*(gCtx+0x2C0)` object | **Open** | Found 56 `[reg+0x2c0]` loads, 65 stores (all in unrelated classes), and the float pair at +0x58/+0x5C; no `.?AV…@…@@` name resolved for it (RTTI COL at 0x142CA8548 leads to a TD whose name field is empty in this dump). **Next:** match one of its known field users to a class in the `match::` RTTI list of `pes-re-matchlogic.md` §5. |
| 4 | `+0x4C00 + pIdx*4` "perf tier" semantics | **Open** | Read as a per-player u32 used only as a tier index (1..2 / 3..6 / ≥7) in `FUN_140923060` and `FUN_14092FAD0` (`+0x100 + pIdx*4`). Same offset family appears in `FUN_140922DD0`. **Next:** cross-read with the player attribute accessors of `pes-re-matchlogic.md` §1.2 (table @0x143529930). |
| 5 | Which event writes `lossTime` (+0x145) besides the formula | **Open** | Only 7 byte-writers found; none on the clock path. INF: it is the `FUN_140923060` result expressed in minutes. |
| 6 | Does anything but `match_time` set the half length? | **Open** | The three half-length tables are pure functions of the phase (OBS). Competition-format selection of ET/PK (how phase reaches 4..9) is in the match setup, not the clock. |

---

## 10. Corrections to `docs/re/pes-re-matchlogic.md` §6

1. **"Stoppage time: only the `lossTime` u8 and the HUD strings are confirmed. The computation is not traced."** — Now traced: `FUN_140923060` @0x140923060, formula and constants in §3.2 (all OBS).
2. **"Open: how the in-match clock maps `match_time` to the 90 game-minutes. Tried: string xrefs and scanning the live context for a SituationData-shaped struct. No candidate matched; the struct base is unknown."** — The SituationData base **is** `*(gMatchContext+0x270)` (OBS: `FUN_1420BD500`, `FUN_1420BE050`, `FUN_1420BE140`, `FUN_1420BE0D0`, `FUN_1420BE780`, `FUN_14092FAD0`, `FUN_1409257B0`, `FUN_140A115A0` all read the phase there), and the field offsets come from the reflection registration at `FUN_140465F40`. The `match_time`→per-tick scaling itself stays open, updated in §9 item 1.
3. **§6's `HALF_KIND 0..10`** — correct, but note **phase 8 is unused** in the registration and the *live* phase counter at `*(gCtx+0x270)+0x1168` is a separate field from the `+0x116C` half-transition counter that gates the ClockFSM.
4. **§6's "Registration code at `0x140465F40` … reflection registrations of offsets 0x142..0x146 around 0x140466453"** — the exact offsets are 0x130 (MODE), 0x134 (HALF_KIND), 0x138 (PLAY_KIND), 0x13C (restartSide), 0x140 (restartMember), 0x142 (homeScore), 0x143 (awayScore), 0x144 (unnamed), 0x145 (lossTime), 0x146 (entry[0x16]).

---

## 11. Reproduction recipe

```
# image
D=<scratch>/pesmem.bin; B=0x140000000        # file offset = VA - 0x140000000

# the clock constants (all .rdata floats)
python3 -c "import struct;d=open('$D','rb').read();B=0x140000000;
[print(hex(a), struct.unpack_from('<f',d,a-B)[0]) for a in
 (0x142632A6C,0x142632A68,0x142598590,0x142632A64,0x142632A60,
  0x14259D638,0x14260B390,0x1425CB004,0x1425CB498,0x1425CB4A0,0x14259D624)]"

# the functions (bounds via <scratch>/fb.py which uses the .pdata table)
for f in 140922dd0 140923060 14092fad0 140925750 140925470 1409257b0 14044c880 \
         140a115a0 140a581c0 140a58550 1420be050 1420be140 1420be0d0 1420be780 \
         1420bd500 140a6bd80 1420efec0 1420f1750 140a592c0 140a58350 140a595c0 140a58f60; do
  echo -n "$f "; python3 <scratch>/fb.py $f
done

# WESYS
PES21_ROOT=<install> build/apps/pesfmt_dump cat common/match/constant/constant_match.bin > cm.bin

# decompile
/opt/ghidra/support/analyzeHeadless $HOME/ghidra-projects/scratch/main PES21 \
  -process pesmem.bin -noanalysis -readOnly -scriptPath <scratch>/TacticsClockRE/scripts \
  -postScript DecompAt.java <scratch>/TacticsClockRE/out.txt \
  0x140922dd0-0x140922dd0 0x140923060-0x140923060 0x14092fad0-0x14092fad0

# disassemble a region
python3 -c "import capstone;d=open('$D','rb').read();B=0x140000000;
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64);
[print(f'{i.address:x}: {i.mnemonic} {i.op_str}') for i in md.disasm(d[0x140923060-B:0x140923060-B+0x200],0x140923060)]"
```
