# PES 2021 live clock — ClockLiveRE sweep report (final, 3 matches)

Tags: **OBS** = observed live or in the installed-exe disassembly (method stated).
**INF(conf)** = inference with confidence. Read-only `/proc/<pid>/mem` pread and
OBS screenshots only; **no write was performed**. Scratch: the ClockLiveRE scratch dir.
Prior report: the ClockRecordRE clock-record report (display record `E+0xa0`/`E+0x11c` claim).

## 0. Headline

1. **ClockRecordRE's location is falsified for live play (OBS, 3 matches).** `E+0xa0`
   and `E+0x11c` (`E=*(G+0x2c0)`) read **0.0 continuously** while the HUD clock ran
   5:41 → 87:33 across goals, halves, full time, and a third match (10:33 → 82:06+).
   280-sample and 180-sample E dumps over open play show `E[0:0x3000]` nearly static.
2. **The HUD formatter path is confirmed statically (OBS, §1)**, including the branch
   selecting `+0xa0` vs `+0x11c` — and live `E+0xa4 ∈ {0x28,0x2b} ≠ 0`, so even on
   ClockRecordRE's own terms the HUD would read `+0x11c` = 0.0. **The HUD's env object
   (`param_1[7]`) is not `*(G+0x2c0)`** (INF high, §2).
3. **Clock rate corrected: ≈9.5 displayed-s per wall-s in open play** (OBS, 3 agreeing
   timestamped brackets: +121/12 s, +184/19.3 s, +254/gap; §4). The earlier 2.9 figure
   is retracted (its 64 s window must have contained a frozen period). All fixed-delta
   scans before the correction used wrong bands and are void as negatives.
4. **The true live clock record is still open** (§6). Killed with correct rate:
   f32 absolute seconds anywhere in non-GPU writable memory (4 valid windows, ~450 k
   candidates, 0 kept); combined HUD-env signature (10.0@+0x58, 1.0@+0x5c + HUD value —
   0 hits); per-player `+0x1a480` (0.0 mid-play at 24:08 — definitively not seconds,
   §5); f64 (2 rate-matching hits, both teleporting scratch on re-probe); f32-minutes
   and i32-minute scans (wrong bands / slow AI counters only). Still open: tick-counter
   encoding, `E+0x8b8` lead, computed-on-the-fly model (no f32 store — INF med-high, §5).
5. **Watchpoint infrastructure proven live** (OBS, §5): 45 s `perf mem:WRITE` run under
   the gf-perf lock on a live address returned cleanly with 0 samples (target frozen —
   itself a finding: `MI+0x155c` is event-driven, not per-tick).
6. **No writer, no freeze recipe — owner decision needed (§7).** One write to `E+0xa0`
   is certainly NOT enough (dead memory). The 00:01 freeze cannot be built until the
   live record closes. Three freeze architectures are on the table with different
   risk profiles; none is confirmed.

## 1. HUD clock path, statically confirmed (OBS, installed-exe disassembly)

- `FUN_1420efec0`: `rsi = rcx+0x38` is the env object; reads `rsi+0xb0` (red limit),
  `rsi+0x9e` (yellow limit), then branches on `FUN_1420be780` (live-half test):
  - live half + `byte[rdi+0xa4]==0` → `HMS(*(f32*)(rdi+0xa0))` + `FUN_1420be140(period)`;
  - live half + `byte[rdi+0xa4]!=0` → `minute = FUN_1420be050()`,
    `(s,stop) = HMS(*(f32*)(rdi+0x11c))`;
  - not live → `FUN_1420be140(byte[rdi+0x2da8])`, zeros.
  `showClock = (phase<7||phase==8) && byte[rsi+0x114]==0`; `E+0x118` feeds stoppage
  via `FUN_140a6bd60`. Matches ClockRecordRE §2.2 exactly (re-derived, not re-used).
- `FUN_140a6bd80` (0x140a6bd80): `cvttss2si` + `0x88888889` magic divides — minute =
  (total/60)%60, hour-ish = total/3600 on an f32 seconds count (OBS).
- Callers: exactly one caller of `FUN_1420efec0` (via 0x1420f1148), one of
  `FUN_1420f10d0` (via 0x1420f1094) — OBS E8 census over executable sections.
- `FUN_1420be780`: returns true iff `MI+0x1168 ∈ {1,3,5,7,9}` (OBS disassembly).
- `FUN_14092fad0` (per-tick, gated on `MI+0x1775 ∉ {8,10}` etc.): stamina drain +
  half-time bonus only — full decompile read, no clock store (OBS).
- `FUN_14092ad40` reads `player+0x1a480` (f32) through `FUN_140a6bd80`, scales by
  tick-rate constants, clamps, writes a byte at `player+0x1a468` — a per-player
  fatigue/pressing meter, not the match clock (INF med-high, OBS disassembly).
- The clock call site sits in a HUD dispatch (`FUN_1420f0c70`) that fills widget structs
  via `141f8b550(r14+0x18, key)` lookups; the chain from HUD `rcx` to the Time-object
  ctor was traced two levels but not closed (open, §6).

## 2. Why `*(G+0x2c0)` cannot be the HUD's object (OBS + INF high)

- Live, all three matches: `E+0xa4` low byte 0x28/0x2b (u32 `0x7000028`), i.e. the
  selector byte is nonzero → HUD branch reads `E+0x11c` = 0.0 → HUD would show 00:00
  + period base. HUD showed 5:41…87:33. Contradiction is direct (OBS).
- `E+0x58 = 10.0`, `E+0x5c = 1.0` (match-length scale + speed, sane) but `+0xa0/+0x11c`
  pinned at 0.0 through goals, halves (1168 = 1 and 3), and full time (OBS).
- INF(high): `G+0x2c0` is a menu/setup-side env (or stale snapshot); the tick-side env
  the HUD reads is a different object.
- Supporting: `MI = *(G+0x270)` and `E` are both null outside the tick (must poll);
  after each match, `G+0x2c0` reads null (OBS). Pitch path `G+0x2a90 → +0x10 → +0x10`
  resolves to code bytes in all states, never data (OBS, re-polled in play).

## 3. E/MI dumps over open play (OBS)

- Match 1: 280 samples × `E[0:0x3000]` + MA ball at 20 Hz over 14 s, bracketed by
  screenshots (HUD 38:54 → 42:06, open play both ends). First-vs-last diff: only 25
  changed dwords (counters at `+0x5d0/+0x5ec/+0x5f0`, small floats, a position-like
  block at `+0x880…+0x8d0`). **No offset holds seconds 2334→2526 in any of
  f32/i32/u32/f64, nor half-relative 0→192 s** (OBS, exhaustive per-offset decode).
- Match 2: 180 samples × `MI[0:0x2000]` + `E[0:0x3000]` at 15 Hz over 12 s, bracketed
  open play (HUD 85:32 → 87:33). Results (OBS):
  - `MI+0x1198` frame counter 42290 → 42971 (+681 ≈ 57/s, never pauses for replays).
  - **`MI+0x119c` shadows the frame counter's delta exactly** (1168 → 1849, +681;
    different base) — new OBS finding, free tick proxy. (In match 3, `119c` read 0
    mid-play — it resets per segment; NOT an accumulator.)
  - `MI+0x11a0` advances 1:1 with the frame counter (+424 = frame delta) — total-tick
    counter (OBS).
  - `MI+0x155c`/`0x1568` f32: 0.0 → 51.8 → frozen (play-gated segment timer, §5).
  - `E+0x8b8` f32: −64.91 → +99.99 (signed zero-crossing; open lead, INF low).
  - Only other advancer: `E+0x5ec` i32 (counter, wrong rate).
- Match 3: 180-sample MI/E dump over a goal → replay transition (bracket: open play
  27:44 → goal replay). `MI+0x119c` 2078 → 2354 then FROZEN at 2354 through the replay
  (OBS) — play-gated counter confirmed. No f32 in MI/E ever reads near HUD 1664 s.

## 4. Whole-memory ramp scans (OBS results + corrected rate)

Vectorised numpy scanner (1–4 s per full pass over ~3 GB writable, GPU mappings
excluded): collect in-band values, sleep, keep only values advancing by a
clock-plausible delta. Every run screenshot-bracketed:

- **Rate (OBS, timestamped): w12→w13 +184 displayed-s over 19.3 s wall = 9.5/s.**
  Agrees with +121/12 s (10/s) and +51.8/6 s (8.6/s). The earlier 2.9 figure came from
  an unbracketed 64 s window that must have included a freeze — retracted.
- f32 absolute seconds, correct ~9.5x band (60–140 per ~10 s), GPU-excluded:
  27–192 k candidates per run, **0 kept in 3 valid open-play windows** (OBS). A 75 s
  long run (222 k cands, +600..+800 band) also kept 0 — window may have contained a
  freeze; weakens but does not kill.
- f64: 2 rate-matching hits (d≈34.5, d≈95.8 at ~10/s), both rejected on re-probe
  (teleporting scratch slots in one anon pool; neighbours are (float,int) effect-timer
  pairs) (OBS).
- i32 seconds: every hit is GPU render-buffer pixel data (OBS, twice).
- f32-minutes band: dozens of slow timers, none at HUD-minute value (OBS).
- i32-minute band (+1..+3 over 75 s): slow AI counters only — band was wrong for the
  true rate (+10..+14 expected); inconclusive, not repeated.
- Combined HUD-env signature (10.0@+0x58 AND 1.0@+0x5c AND HUD value@+0xa0/+0x11c):
  **0 hits in all of memory in 2.7 s** (OBS) — no E-shaped live twin object exists.
- Twin-object `(10.0,1.0)`-only scan: 462 hits, all UI float tables (±1.0/0.0 runs)
  except the dead `E` (OBS).
- Triple-snapshot monotonic fit (3 × 3 GB, ~6 s apart, no fixed band — catches ANY
  smooth advancer in [4000,6000]): 2 hits, both teleporting scratch on re-probe (OBS).
- Void runs (~10): recheck window fell in celebrations, replays, tactics menus, stats
  overlays, or highlights — the clock freezes in all of them (OBS behaviour finding).
- Stable infrastructure: sampler under sudo, screenshots as user (screenshots fail
  under sudo — no OBS config for root); `waitmatch.py` 1 Hz match waiter worked twice.

## 5. Watchpoint + rejected leads (OBS)

- **Perf write-watchpoint on `MI+0x155c` (0x1ccd5b64), 45 s, under the gf-perf lock:
  0 samples, value frozen at 0.0 through open play (71:46 → 82:06, 116c=1).**
  The v1 0→51.8 advance was event-driven (set-piece window), not per-tick. Finding:
  `MI+0x155c/0x1568` are event timers; lock/run discipline clean (no errors, lock
  released, no perf process left running).
- `+0x1a480` per-player: 0.0 for all 22 players mid-play at 24:08 (one noisy slot) —
  definitively not match seconds; killed as clock family (was INF med, now OBS-dead).
- Rate-matching f32 families (0x1fef5xx ×12 addrs at ~1.3/s wall; 0x7ffffbda13xx stack
  ×12 at ~2.6/s): froze for 12 s then jumped BACKWARD (1042.75 → ~826) — restart/
  segment timers, rejected by direct re-probe (OBS).
- Computed-on-the-fly model now INF(med-high): display = play-gated ticks × gain +
  period base with catch-up jumps after frozen periods (explains jumpy brackets and
  the absence of any smooth f32 store). Under this model the freezable points are the
  gain/base constants, the tick counter, or the HUD snapshot — all unlocated.

## 6. Open items with exact next steps (samplers named)

1. **Live clock record location.** Ready in the ClockLiveRE scratch dir: `tight2.py`
   (f32, GPU-excluded, region names), `tight3.py` (f64/u32-milliseconds modes),
   `tight4.py` (any-dtype, vectorised recheck), `snap3.py` + `an8.py` (band-free
   triple-snapshot monotonic fit — the recommended default now that the rate is
   unreliable), `midump.py` + `an5/an6/an7.py` (MI/E slope mining), `esig.py`
   (signature scan), `waitmatch.py` (1 Hz match waiter). Next match, in verified open
   play: timestamp two screenshots ~60 s apart to fix the rate, then run `snap3.py`
   and fit any monotonic slope, matching value to HUD post-hoc.
2. **HUD env object identity.** From `FUN_1420f1148`'s caller chain, trace the HUD `rcx`
   (`r15+0x18` → `141f8b550` results) to the Time-object ctor; compare that pointer
   live against `G+0x2c0`. Static half done (§1); needs a live match.
3. **Writer + freeze recipe (ticket items 2–3).** Blocked on (1): once the live record
   is bracketed, wrap `perf record -e mem:<addr>:w` in the gf-perf lock, ≤60 s, during
   open play. **Do not hold `E+0xa0`** — dead store, verified live across 3 matches.

## 7. Owner decision: the 00:01 freeze

No confirmed field exists to freeze, so no freeze was applied (correctly — a blind
write risks the sim). Three architectures, by risk:

- (a) **HUD snapshot hold** (display only, sim untouched) — needs item 1's HUD-env
  object. Safest for "reads 00:01 and stays". Recommended if item 1 closes there.
- (b) **Play-tick hold** (e.g. `MI+0x119c`-class counter) — freezes every consumer of
  play time (stamina drain via `FUN_14092fad0`, AI timers, and the half-time trigger
  path `MI+0x1168++/+0x116c` in the transition code). Would read 00:01 only if the
  display derives from it — unproven. Medium risk.
- (c) **`E+0xa0`/`E+0x11c` write** — OBS-dead: the HUD does not read these during play.
  Would do nothing visible. Do not approve.
- Half-time trigger dependency: the `+0x1168++` transition and `FUN_140923060`
  half-time stamina bonus key off `MI+0x116c == 2 && MI+0x1574 == 1 && MI+0x11a8 == 2`
  (OBS decompile), not off any clock float seen live — but the true clock's consumers
  are unknown until item 1 closes, so any freeze must be reviewed against the writer's
  neighbours first.

## 8. Window usability log

- Match 1 (10-min halves): ~2 h wall, HUD 21:08 → 84:17 + full time; ~10 valid
  open-play scan windows, rest voided by menus/replays/menus. Usable.
- Match 2: HUD 5:41 → 87:33, 1-1; ~8 valid windows; second half fully sampled. Usable.
- Match 3 (fresh G, verified 116c=1): HUD 10:33 → 82:06+, 1-1; ~6 valid windows
  including the timestamped rate calibration and the 45 s watchpoint run. Usable.
- Final state: cup/end screen, match over. No sampler left running; no perf process
  outstanding; no writes performed at any point.
