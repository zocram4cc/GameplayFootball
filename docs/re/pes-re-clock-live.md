# PES 2021 live clock — ClockLiveRE sweep report (final: write access, game exited)

Tags: **OBS** = observed live or in the installed-exe disassembly (method stated).
**INF(conf)** = inference with confidence. Read-only pread first; **direct memory
writes were performed after write access was granted** (each logged). Scratch: the
ClockLiveRE scratch dir. Prior report: the ClockRecordRE clock-record report.

## 0. Headline

1. **Main's `MI+0x1190` hypothesis is falsified (OBS).** `MI+0x1188…0x1194` as f32 read
   0.0/denormal garbage through a full half; as u64 they are patterned small-int pairs
   (`0x37dead36…`-style, advancing in lockstep — bitfields/IDs, not floats); as i32
   they DECREASE by ~419 k over 12 s. No float at 9.5/s anywhere in `MI+0x1180..0x11c0`
   over a 1200-sample 20 Hz dump spanning half-time → open play (§3). Step 1 also kills
   the base walk: the HUD reader's object comes from `141f8b550` widget-dict lookups
   keyed by scorebug strings (`scoreTime_time_show`, `show_kickOff`, …), not from any
   MI-derived pointer (OBS, §1).
2. **ClockRecordRE's `E+0xa0`/`E+0x11c` stays falsified (OBS, 4 matches).** 0.0
   throughout; HUD branch would read `+0x11c` given live `E+0xa4≠0`.
3. **Poke protocol results (OBS, all with write access):**
   - `MI+0x1198` (frame) ±5000 during open play, immediate screenshots both ways:
     HUD advanced only naturally. **Frame counter REJECTED as display source.**
   - `MI+0x119c` +3000: game overwrote the poke within 1–2 ticks (766 ≈ orig + 1 tick).
     **119c is re-derived every tick from an upstream source** — poking it can never
     freeze anything. Sustained hold at 500 for 15 s: play continued to a goal;
     HUD unaffected. Downstream copy, irrelevant.
   - `MI+0x118c` +100000: HUD advanced naturally; value later read LOWER than poked
     (re-stamped). Not a monotonic accumulator.
   - `MI+0x118c` hold (300 writes/15 s at 90:16): match played to full time; game
     exited normally afterwards (natural lifecycle, not killed by the write).
   - Writes are safe and precise (240–300 writes per hold, values verified by re-read,
     no crashes across 6 write operations).
4. **New model: wall-µs stamps (INF med-high, §5).** `MI+0x118c/0x1190/0x1194` sit at
   ~1.74–1.99×10^9, drifting ~10^5/s — consistent with microsecond wall timestamps
   (2×10^9 µs ≈ 33 min ≈ match elapsed). The per-tick driver (`FUN_140925470`)
   increments a global tick (`_DAT_1436f391c + 1`); the event statistics switch
   (`FUN_140a115a0`, case 0xd) accumulates `118c += Δt; 1190 += 1` with a divisor
   at `+0x1d0`. The display is plausibly `(wall_now − half_base) × gain` — which
   explains why no smooth f32-seconds store exists anywhere in memory.
5. **Perf mem-breakpoints are unavailable** (OBS: `perf_event_paranoid=2`, all PMUs
   blocked, debug registers denied). The earlier 0-sample watchpoint runs were
   doubly void (wrong attach + blocked PMU) — retracted as findings; the poke
   protocol replaced them.
6. **No confirmed field → no freeze applied.** The 00:01 freeze needs the half_base
   or gain from the stamp model (open). Exact next steps in §6; the poke/hold scripts
   are ready (`hold.py`, `hold3.py`).

## 1. HUD path + base walk (OBS static)

- `FUN_1420efec0` re-confirmed (env at `rcx+0x38`, `+0xa0`/`+0x11c` branch on
  `byte+0xa4`, HMS via `FUN_140a6bd80`). Single callers at each level (E8 census).
- Walking up: `FUN_1420f10d0` ← `FUN_1420f0eb0` (via 0x1420f1094) ← `FUN_1420f0c70`
  (via 0x1420f0bf7). `FUN_1420f0eb0` fills its struct through `141f8b550(r14+0x18,
  key)` dict lookups; extracted key strings name scorebug widgets
  (`scoreTime_time_show`, `scoreTime_time_off`, `scoreTime_losstime_off`,
  `show_kickOff`, `emblemCompe`, `transparent_*`) — the HUD env is a render-side
  widget object, **not MI-derived**. Main's step-1 verdict: the base is not MI;
  the `MI+0x1180` record layout is dead.
- `FUN_14092fad0` (stamina drain + half-time bonus only, no clock store),
  `FUN_14092ad40` (per-player fatigue meter on `player+0x1a480`, not the clock),
  `FUN_140922ff0` (lazy-init accessor for `player+0x1a488`, not an advancer) — all
  OBS (decompile + disassembly).

## 2. Why `*(G+0x2c0)` cannot be the HUD's object (OBS + INF high)

- Live, all matches: `E+0xa4` low byte 0x28/0x2b → HUD branch reads `E+0x11c` = 0.0,
  but HUD showed 5:41…90:16+ET. Direct contradiction (OBS).
- `E+0x58 = 10.0`, `E+0x5c = 1.0` but `+0xa0/+0x11c` pinned at 0.0 through goals,
  halves, full time (OBS). INF(high): setup-side env, not the tick-side object.
- `MI`/`E` null outside the tick (must poll); null after each match. Pitch path
  `G+0x2a90 → +0x10 → +0x10` resolves to code bytes in all states (OBS).

## 3. Dumps over open play (OBS)

- Match 1: 280 × `E[0:0x3000]` + MA ball, 20 Hz/14 s (HUD 38:54 → 42:06). 25 changed
  dwords, none clock-like; no offset holds 2334→2526 s in f32/i32/u32/f64 (OBS).
- Match 2: 180 × `MI[0:0x2000]` + `E[0:0x3000]`, 15 Hz/12 s (HUD 85:32 → 87:33).
  `MI+0x1198` +681 ≈ 57/s; **`MI+0x119c` shadows the frame delta exactly**
  (1168 → 1849, +681); `MI+0x11a0` 1:1 with frames; `MI+0x155c/0x1568` 0→51.8 frozen
  (event timers); `E+0x8b8` −64.91 → +99.99 (open lead, INF low).
- Match 3 (goal → replay): `MI+0x119c` 2078 → 2354 then FROZEN through the replay —
  play-gated counter (OBS). No f32 in MI/E near HUD 1664 s.
- Match 4 (write window): **1200 samples × `MI+0x1180..0x11c0` at 20 Hz over 60 s,
  half-time show → second-half open play (HUD 59:11).** `+0x1188…0x1194` f32 = 0.0
  throughout; u64 = lockstep patterned pairs (`0x6895…/0x68c9…`, freezing mid-dump,
  resuming — bitfields/IDs); i32 drift −377 k…−419 k. `+0x1198` 26352 → 29255
  (frames); `+0x119c` 0 → 831 (segment, reset); `+0x11a0` random-walk 977 → 635
  (NOT a counter — small event values). **Falsifies Main's record layout** (OBS).

## 4. Whole-memory ramp scans (OBS results + corrected rate)

- **Rate: ≈9.5 displayed-s per wall-s in open play** (OBS, 3 timestamped brackets;
  earlier 2.9 retracted — its window contained a freeze).
- f32 absolute seconds at correct rate: 27–222 k candidates, **0 kept in 4 valid
  windows** incl. a 75 s long run (OBS). f64: 2 rate-matching hits, both teleporting
  scratch on re-probe (OBS). i32 seconds: GPU render-buffer pixels only (OBS, twice).
- Combined HUD-env signature (10.0@+0x58 AND 1.0@+0x5c AND HUD value): **0 hits in
  all of memory** (OBS). Twin `(10.0,1.0)` scan: 462 UI-table hits, no live twin.
- Triple-snapshot band-free monotonic fit: 2 hits, both scratch on re-probe (OBS).
- Clock freezes in celebrations, replays, tactics, stats, highlights, subs menus
  (OBS behaviour finding — voided ~10 scan windows).

## 5. Writes, stamps, and the tick driver (OBS)

- Poke protocol (screenshot → poke → screenshot ≤5 s, persistence re-read):
  frame ±5000 (rejected both directions), 119c +3000 (overwritten in 1–2 ticks —
  re-derived), 119c hold@500/15 s (play unaffected, goal scored), 118c +100000
  (HUD natural; value re-stamped lower), 118c hold/15 s (match to full time, clean
  exit). 6 write ops, 0 crashes.
- `MI+0x118c/0x1190/0x1194` ≈ 1.74→1.99×10^9 over the session (~10^5/s drift):
  INF(med-high) wall-µs event stamps. Writers: `FUN_140a115a0` case 0xd
  (`118c += Δt; 1190 += 1; 1d0 = (118c/1190)/rate`) and the half-time transition
  (`116c += 1198 − saved; 1168++; 728 = 116c/1168`). Readers of 118c/1190 not yet
  traced (open).
- Per-tick driver `FUN_140925470` (OBS decompile): lazy-init, `FUN_140922ff0`,
  `FUN_140926b00/FUN_140928fc0/FUN_140929930`, `FUN_14092fad0`, global tick
  `_DAT_1436f391c + 1`, `FUN_14092af10`, `FUN_14092ad40`. The display plausibly
  derives from (wall stamps, tick base, gain) with catch-up jumps — INF(med-high).
- `+0x1a480` per-player: 0.0 mid-play (OBS) — dead as clock family.
- Perf `mem:...:w` breakpoints unavailable in this environment (paranoid=2); earlier
  0-sample runs retracted as evidence (wrong attach AND blocked PMU).

## 6. Open items with exact next steps (samplers named)

1. **Live clock record → half_base/gain of the stamp model.** Ready in the
   ClockLiveRE scratch dir: `hold.py ADDR VAL SEC` / `hold3.py ADDR SEC` (u32 pin),
   `win.py` (targeted 20 Hz window dump), `snap3.py`+`an8.py` (band-free fit),
   `tight2/3/4.py` (typed scans), `live1190.py` (window state), `waitmatch.py`.
   Next match: (a) trace readers of `MI+0x118c/0x1190` (xrefs of the case-0xd
   reader side + `FUN_140a115a0` callers); (b) hold `118c` pinned from open play
   and watch whether the HUD clock stops while play continues — a stopped HUD
   confirms the stamp as the display source; (c) scan u32 ~2×10^9 advancing
   ~10^5/s for sibling stamps incl. the half base.
2. **HUD env object identity.** Needs the `141f8b550` dict owner (render-side);
   pointer comparison against `G+0x2c0` in a live match.
3. **Writer + 00:01 freeze.** No confirmed VA — nothing was frozen. Once the
   stamp/base is confirmed: hold base (display-only freeze, sim untouched) or
   write target seconds into the derived f32 if one exists; re-read + screenshot
   verify. **Do not hold `E+0xa0`** (dead). Half-time trigger keys off
   `116c/1574/11a8`, not off any clock float seen live — but re-verify against the
   confirmed source before any freeze ships.

## 7. Owner decision: the 00:01 freeze

- (a) **HUD snapshot/base hold** (display only) — recommended; needs §6.1.
- (b) **Play-tick hold** — 119c-class counters are re-derived copies; holding them
  provably does nothing (OBS). Dead option unless the upstream tick is found.
- (c) **`E+0xa0`/`E+0x11c` write** — OBS-dead across 4 matches. Do not approve.
- Process state: PES2021.exe + Wine exited after full time (90:16 + ET +4, natural);
  no sampler/perf process outstanding; final match state unrecoverable — next steps
  need a new match.
