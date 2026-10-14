# PES 2021 match-clock record — ClockRecordRE evidence report

Tags: **OBS** = observed at the named VA / live address (method stated).
**INF(conf)** = inference with confidence. No player or team names.
Live reads are read-only `/proc/<pid>/mem` pread; **no write was performed**.
Scratch: the ClockRecordRE scratch dir (`out/` decompiles, `*.txt` logs, `pdata.py`).

Related: the GodotFootball `docs/re/pes-re-tactics-clock.md` clock/stoppage/period
report, `docs/re/pes-re-stamina-drain.md` (StaminaDrainRE), `docs/re/pes-re-hud-ui.md`
§3.1/§4/§6 (scorebug). GF counterpart: `src/sim/onthepitch/pesclock.*`.

## 0. Headline (OBS unless marked)

1. The on-screen clock does **not** come from any FNV-protected `matchTime`
   record. It comes from two **plain float seconds** in the match-env object
   `E = *(G+0x2c0)`: `E+0xa0` (live half) and `E+0x11c` (otherwise) — OBS
   (`FUN_1420efec0`, `FUN_140a6bd80`, live §5).
2. The `FUN_140a58xx` family (`140a58350/140a58450/140a58550/140a580b0` getters,
   `140a592c0/140a595c0/140a58f60` setters, `140a581c0`, `140a581b0`) are the
   **per-player stamina tank** accessors on `PMS` (OBS §1); the old
   tactics-clock §2.4 "clock computation" (`FUN_140922dd0`) is the **stamina
   tank init**, and its §2.2 "percentage of minutes×1920" is the **stamina %
   and breath %** scaling (OBS §1). There is no `matchTime` clock record.
3. The `match_time` setting (u16 at settings+0xd5a) becomes the float
   `F = *(E+0x58)` = match minutes (OBS live: 10.0 under a 10-minute setting,
   stamina report §7). It scales the stamina tank (§2), not a clock increment:
   the displayed clock advances 1/54 s per logic tick from the frame counter;
   the per-tick increment question as posed has no clock-record answer (INF high).
4. Freeze write: **no FNV bytes need changing** — the displayed clock is a plain
   float. Exact bytes for a freeze are open (writer not yet isolated); the
   read-only verification the ticket asked for (FNV round-trip) does not apply
   because the record is not FNV-protected (§6).

## 1. The 140a58xx accessors are stamina, not clock (OBS)

(Ticket premise, re-verified in this session because the whole decode depends
on it.)

- `FUN_140a58350` (0x140a58350–0x140a5844c): decodes block `+0xc8` —
  hash `+0xc8`, key `+0xd0`, idx `+0xd4`, ring `+0xd8[idx&3]` (OBS decompile
  `out/acc.c`). `FUN_140a58450`: same shape at `+0xe8/+0xf0/+0xf4/+0xf8`.
  `FUN_140a58550` (0x140a58550–0x140a58821): validates the `+0xe8` block then
  the `+0xc8` block and returns `cur*100/max` (stamina %). `FUN_140a580b0`:
  same shape at `+0x108/+0x110/+0x114/+0x118` (breath B).
- `FUN_140a581c0` (0x140a581c0–0x140a58300): calls `140a58550` (stamina %),
  floors at 20, computes `cap = pct*1920/100`, returns `B*100/cap`
  (breath %). `FUN_140a581b0` returns 0x780 = 1920 (the breath cap unit).
- Setters: `140a592c0` writes `+0xc8` (3 call sites: 0x140922f99 init,
  0x14092313d half-time, 0x14092feff per-tick drain); `140a595c0` writes
  `+0xe8` (1 site: 0x140922f6f init); `140a58f60` writes `+0x108` (1 site:
  0x140922fab init) — OBS caller census (`out/set.c`, E8 scan).
- `FUN_140922dd0` (0x140922dd0–0x140922fd7) reads `rec+0x35` (Stamina ability),
  `rec+0x5d` (freshness), `rec+0x58`, team `+0x4c00` tier, `F = *(E+0x58)`,
  writes max/cur/B via the three setters and copies cur to `+0x1a484`
  (OBS `out/clk.c`, `out/init.c`). It is the stamina tank init, not a clock
  setter. `FUN_140923060` adds the half-time bonus to **cur** (OBS).
- Live (stamina report §7, re-polled this session): `F = 10.0`, tank values
  multiples of 4000 = 400·F, A5/drain predictions hold.
- Cross-check (OBS, new): `FUN_14092ad40` (0x14092ad40–0x14092aea7, called from
  the per-tick `FUN_140925470` path) reads `match::Player+0x1a480` as a float
  seconds count through `FUN_140a6bd80` — the same HMS helper the HUD uses —
  and `FUN_1420bded0` (0x1420bded0–) adds the half-length constants
  (45/90/105) after HMS, mirroring the HUD's live-half rule. So `+0x1a480`
  is a per-player seconds field in the same clock family (consumer, not the
  display record). Its only writers (0x1417ec5b4/0x1417ec8ec, in menu-side
  `FUN_1417ec580`) were not traced to the tick — open.

Killed: tactics-clock §1.1 rows "Match-time percentage / Match minutes /
Third record field (constant 1920)", §2.2, §2.4 as clock claims. Kept: the
half-length tables (§2.1), the HalfKind/phase globals, the HMS helpers, the
HUD clock-block description (§2.6/§3.1), the FSM freeze gate (§3.3), the
transition message (§4) — each re-cited below where reused.

## 2. The displayed minute/second source: env floats (OBS)

### 2.1 `FUN_140a6bd80` / `FUN_140a6bd60` (OBS decompile `out/hud2.c`)

```
FUN_140a6bd80(f32 totalSeconds, *h_or_m, *s):
  t = (u32)(s64)totalSeconds / 60; *s = t % 60; *h_or_m = t / 60
FUN_140a6bd60(f32 totalSeconds): return (u32)(s64)totalSeconds / 3600
```

i.e. minute = (total/60)%60, hour-ish = total/3600. Operates on a **seconds**
count. (Matches tactics-clock §2.6; Ghidra dropped the float return — raw
disassembly cross-checked.)

### 2.2 `FUN_1420efec0` — the HUD clock-state builder (OBS decompile `out/hud.c`)

Inputs: `param_1[7]` = the match-env object `E` (same object that carries
`+0x58/+0x5c`); output `param_2` = 17-u32 clock-state struct consumed by
`FUN_1420f1750` (clock digits), `FUN_1420f12d0` (score), `FUN_1420f1210`
(cards) via the single caller `FUN_1420f10d0`:

```
redLimit[2]   = min(byte[E+0xb0+i], 5)
yellowLimit[2]= min(byte[E+0x9e+i], 99)
if IsLiveHalf(phase):                        // FUN_1420be780: phase in {1,3,5,7,9}
    if byte[E+0xa4]==0:
        (m,s) = HMS(*(f32*)(E+0xa0)); minute = m + FUN_1420be140(byte[E+0x2da8])
    else:
        minute = FUN_1420be050(); (s2,stop) = HMS(*(f32*)(E+0x11c))
else:
    minute = FUN_1420be140(byte[E+0x2da8]); (0,0)
minute += uStack_80  (0 on this path)
showClock = (phase<7 || phase==8) && byte[E+0x114]==0
if phase==8 && !replay: minute = min(minute, FUN_1420bdfd0(byte[E+0x2da8])); sec=0
if minute>=100: digitVariant=0  elif minute<10: 2  else: 1
param_2[7] = minute-or-(uStack_80+iStack_88) by phase; param_2[9] = byte[E+0x114] ? HMSmin(*(E+0x118)) : -1
param_2[0xc..0xd] = u32[E+0xa8..0xac]; +0x10 flag: scoreboard-attention test
```

So the **record that holds the displayed minute and second is `E+0xa0`
(float seconds, live half) / `E+0x11c` (frozen/other path)**. Layout: plain
IEEE-754 f32, **not** FNV-protected — no hash/key/idx/slot surrounds it
(OBS live dump §5: neighbouring bytes are unrelated small ints/flags).

### 2.3 Digit rendering `FUN_1420f1750` (OBS `out/hud.c`)

- `param_3+0x24` = stoppage minutes u32; `0xffffffff` = hidden, else clamped
  to 9 (`if v>9: v=9`).
- `param_3+0x14` = clock digit-count variant from minute magnitude
  (0 → 3 digits, 1 → 2, else 1); `param_3+0x28` = same for stoppage.
- Replay flag byte `DAT_1437f968a` forces phase 0 → clock block skipped.
- Whole scorebug visible iff `phase<7 || phase==8` (via `FUN_1420f0eb0`;
  phase 7 shows `show` instead of `show_kickOff`). Matches hud-ui §4.1.

## 3. `match_time` → per-tick increment (OBS + INF high)

- Setting: `match_time` u16 at settings+0xd5a (OBS `out/ser.c`:
  `FUN_140383b20(buf, *(u16*)(s+0xd5a))` with key string `match_time`
  @0x142836458; neighbours `win_lose` u8 +0xd58, `stadium_id` u8 +0xd5c —
  confirms tactics-clock §1.1 row).
- Runtime scale: `F = *(f32*)(E+0x58)`; live 10.0 under a 10-minute setting
  (OBS, stamina §7 + this session §5). `E+0x5c` = 1.0 live; the end-of-tick
  `FUN_1409224d0` publishes `GetGameSpeed() / *(E+0x5c)` (54.0) to the physics
  step (OBS `out/tick.c`).
- The stamina tank multiplies every term by `F` (§1); nothing in the clock
  path reads `F` except as `sec*16000`-style floors inside stamina.
- The displayed clock has no per-tick increment constant of its own in the
  HUD path: it advances because the env seconds advance 1/54 s per logic tick
  (tick = `GetGameSpeed()` 54.0 @0x14351ae54, int 54 @0x14351ae50; tick
  counter `matchCtx+0x10` in `FUN_140925750` — OBS, tactics-clock §2.5
  re-verified). **INF(high): the writer of `E+0xa0` adds `1/54` (or the
  frame-counter-derived equivalent) per tick while the FSM gate holds; the
  writer itself was not isolated** (§7.1). There is no `match_time`→increment
  arithmetic left to find for the display clock: `match_time` sets `F`, and
  `F` sets the stamina scale and (INF med) the env seconds' full-match span.

## 4. Freeze write (read-only result — DO NOT WRITE without review)

Because the displayed clock is a plain f32 (§2), freezing it is a 4-byte
float store:

- Live half: write IEEE-754 bits of chosen seconds to `E+0xa0`
  (e.g. 2700.0 = 45:00 → `0x4528C000`).
- Frozen/other path: same to `E+0x11c`.
- No hash/slot update exists or is needed (not FNV-guarded).
- Caveat (open, §7.1): the per-tick writer will keep advancing it unless the
  FSM gate is also held (frozen state byte `MI+0x1775 ∈ {8,10}` stops
  `FUN_14092fad0`; replay/demo flags at `G+0x400/G+0x3b0` stop it earlier).
  A pure display freeze (scoreboard only) can additionally set
  `byte[E+0x114] = 1`, which forces `showClock = false` in `FUN_1420efec0`
  (OBS §2.2) — 1 byte, no checksum.
- **Not verified**: the ticket's "decode the live record and show the encoder
  round-trips byte-exactly" step assumed an FNV record; it does not apply.
  What was verified instead (read-only): the live floats decode as sane
  values (F=10.0, +0x5c=1.0, +0xa0=+0x11c=0.0 pre-kickoff at phase 2/3 —
  OBS §5) and the HMS math reproduces the displayed digits.

## 5. Live observations (OBS, read-only pread)

- `GetGameSpeed()` 54.0 @0x14351ae54, int 54 @0x14351ae50; cached phase 2
  @0x1437f965C, replay flag 0 @0x1437f968A at survey time.
- `MI = *(G+0x270)` = 0x26c54608, `E = *(G+0x2c0)` = 0xa3ef534 (both null
  outside the tick — polled; brief's note confirmed).
- `MI+0x1168` phase 3 (2nd half), `+0x116c` 1, `+0x11a8` cycling 0/62/148/149,
  `+0x1198` advancing ≈57/s (46458 → 49352 over ~67 s wall).
- `E+0x58` = 10.0, `E+0x5c` = 1.0, `E+0xa0` = 0.0, `E+0x11c` = 0.0,
  `E+0x114` = 0, `E+0x118` ≈ 1.4e-45, `E+0xa4` = 0x15, `E+0x2da8` = 0.
  (Zeros at +0xa0/+0x11c with phase 3 = menu/demo state, not open play;
  open-play advance not observed — §7.2.)
- `G` itself read 0x0 for minutes at a time (menu); pitch path `G+0x2a90`
  → 0x142608810 → 0x14044e0c0 (code, not a table) — brief's pitch-table path
  did not resolve to data in this state (open, §7.3).

## 6. GF counterpart: `src/sim/onthepitch/pesclock.*` — corrections needed

The GF model (percentage of `minutes×1920`, 960 ticks/game-minute, 16000
units/s floor, 9000/5400 added-time, freeze gate, 9-minute cap, 100-minute
wrap, half tables 45/90/105/120) ports the **stamina-tank numbers**, not a
clock:

1. `kPercentageUnitTicks = 1920`, `kTicksPerGameMinute = 960`,
   `PercentageFromTicks/TicksFromPercentage`, `PeriodEndTicks`,
   `TicksPerGameSecond` — implement breath-%/stamina-% scaling, not any
   displayed-time record. **Rename/rehome to the stamina port** (or delete
   if `pesstamina.cpp` already covers it — check; ai-map §2 says the fatigue
   port is done) and remove the clock framing.
2. `PesClock::Advance/Ticks/Percentage/Display` as a percentage counter has
   **no PES counterpart** — PES displays `HMS(envSeconds)` (§2). Replace with:
   `float envSeconds` advanced `gameSeconds` per tick while unfrozen;
   `Display = HMS(envSeconds) + FUN_1420be140(prevPeriodEnd)` in live halves.
3. `AddedTimeUnits` (9000/5400 × sec + tier bonus) adds to **stamina cur at
   the break** (`FUN_140923060` → `SetCurrent`), not to a clock. The added-time
   the board shows is `SituationData.lossTime` u8 @+0x145 (registration OBS
   §7); its writer from the formula result is still open (§7.4). GF should
   feed the formula into stamina recovery and `lossTime`, not into clock ticks.
4. Keep (correctly attributed): half-length tables (re-verify against
   `out/half2.c`: `1420be050`: 1,2→45; 3,4,8,10→90; 5,6→105; 7→120;
   `1420be140`: 2,3→45; 4,5→90; 6,7→105; 8,9,10→90/120 by arg; else 0;
   `1420be780` live = phase ∈ {1,3,5,7,9}); the freeze-gate shape
   (`s+0x1775 ∉ {0,8,10}` + `+0x116c/+0x1574/+0x11a8/+0x1168` conditions);
   the 9-minute clamp; the digit-variant rule; the hide rules
   (phase<7||==8, replay flag); the half-transition
   (`+0x116c += MI+0x1198 − pitch+0x14; +0x1168++; +0x728 = +0x116c/+0x1168`).
5. `match_time` mapping for GF: minutes → `F` float; stamina terms × F.
   No clock-increment mapping is needed.

## 7. Open (with what was tried)

1. **Writer of `E+0xa0`** (the per-tick display-clock advance). Tried: full
  `.trace` movss-store scan for disp 0xa0/0x11c (only stack/menu hits);
  addss scan (7 sites, all vetted: 0x14045d304/0x14045d34f = vector code,
  0x1423c9a1c = render code, 0x1423e1e91 = +0x11c/+0x128 menu-timer code in
  `FUN_1423e1e4d`, 0x14047d3b1/0x14047d3d6 etc. = +0x110..+0x11c int/float
  writes in `FUN_14047d280` — a different object: `param_1+0x78`-rooted,
  ints at +0x110/0x114/0x118 + float +0x11c, and it never touches +0xa0).
  New: HUD clock-state builder has exactly one caller (`FUN_1420f10d0` via
  0x1420f1148); digit setter one caller (0x1420f116a); `FUN_140a6bd80` has 21
  call sites, `FUN_140a6bd60` 46 (OBS E8 census — full lists in scratch).
  Next: xref the HUD's `param_1[7]` load (`rcx+0x38` at 0x1420efed3) back to
  the Time-object ctor, then scan stores through that register; or perf-write
  watchpoint (orchestrator; needs sudo).
## 8. Reproduction

```
D=<the ClockRecordRE scratch dir>
# decompiles (each queues on the proj lock — batch):
dec.sh out/hud.c  0x1420EFEC0 0x1420F1750 0x140A6BD80 0x140A6BD60
dec.sh out/acc.c  0x140A58350 0x140A58450 0x140A58550 0x140A580B0 0x140A581B0 \
                  0x140A592C0 0x140A595C0 0x140A58F60
dec.sh out/clk.c  0x14092FAD0 0x140922DD0 0x140923060
dec.sh out/tick.c 0x140925750 0x1409224D0 0x140925470
dec.sh out/ser.c  0x1410AE130        # match_time u16 +0xd5a
dec.sh out/situ.c 0x140465F40        # SituationData min +0x144 / lossTime +0x145
dec.sh out/half2.c 0x1420BE780 0x1420BE140 0x1420BE050
# caller census (slow, ~5 min for all seven):
python3 -c # E8 scan for 140a592c0/140a595c0/140a58f60/140a58350/140a58450/140a58550/140a580b0
# function bounds: python3 pdata.py <VA...>
# live (read-only, needs sudo; MI/E null outside tick — poll):
sudo python3 # live_env.txt / live_watch.txt / live_g.txt scripts (in D)
```

## 9. Report status

Decode 1 (display record + writer + layout): record + layout OBS (§2);
writer open (§7.1). Decode 2 (`match_time` scaling): OBS + INF(high) (§3).
Decode 3 (freeze bytes + read-only verify): bytes given, FNV round-trip
N/A by OBS layout (§4). GF verdict: §6. Confidence on every claim inline.
