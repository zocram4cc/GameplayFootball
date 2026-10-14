# PES 2021 set-piece zones — live sweep (SetpieceLiveRE)

Read-only RE on the running AI-vs-AI match. Extends `pes-re-setpiece-zones.md`
§5 with a live section only. Conventions as in the parent report (G, MI, F[];
`dir` = attack sign of the restarting team; `T` = team record; slot 0..10,
idx 0..21). **OBS** = read live. **INF(conf)** = inference.

Scratch: the SetpieceLiveRE scratch dir (`spoll.py`, `live.jsonl`, `analyze.py`,
`apcache.txt`).

## L0. Live log

Poller: cheap 5 Hz `(mode,play)` poll; on MODE 4/5 + play 2/3 arm a 22 Hz
recorder (stages, `T+0xa2a0` states, `T+0x558` targets, full `T+0x26c` +
`T+0x4b4c`, positions, MA ball, `MI+0x11ac` counter, restart block, kicker
`ap+0xab8` block + `kreq`, `T+0x6d0` + `T+0xb8a4`). OBS. Team dirs this match:
side 0 = −1, side 1 = +1.

Captures (all MODE 4, side 0 restarts at R = (−52.25, +33.21), attacking −x):

- **CK-A**: 15 frames, play 2 (stg 22 ×2) → play 3 (stg 21, 22) → wait
  (`stg` reads 16842774 = 0x10000BE — MI unpublished mid-tick, counter
  `sp` 36→280 valid). Kicker ap 0x37c6d150: actions 74/74 → 79/79 → 49/49
  (phase 1, clip 1233) → 50/50 (phase 1, clip 1248) → phase 2 clip 2380
  (`kick_long_3_0_inside` family per kicker §2.3) at sp 266. Long cross.
- **CK-B** (same R, ~2 min later): 12 frames, same stage path; kicker
  79/79 → 49/49 → 51/51 (clip 1248) → phase 2 clip 2527 at sp 94.
  Short corner (51 = `CORNERKICK_SHORT` per kicker §2.3/EV-SPK-0012).
- (A first arm caught only the tail of an earlier corner: 1 frame, stale
  side = 2 read — discarded. The 8 s AnimePlayer vtable scan missed the
  window; later arms reuse the cached ap list, instant.)

No MODE-5 FK has occurred yet in this session. FK-line fit (item 5) still open.

## L1. Corner variant V selected per corner (parent §5 item 3 — CLOSED live)

Attack level `T+0xb8a4` = 2 both teams → N = 6 both corners (OBS, matches
N = 4 + min(level, 2)).

Attacker spots at the first play-3 frame (teleport done, choreography §4.3
frame: mirror table +x→−x), vs `positionCK_6_{1,2,3}` ordinals 5–10:

- **CK-A** (long, action 50): idx 4 = (−27.82, −1.49) = −(27.82, −1.49) ≈
  `_6_2` ordinal 5 (27.82, −1.49) to 0.00 m; idx 5–10 all on `_6_2` rows
  (rms **0.00** vs V = 2; 2.64 vs V = 1; 2.47 vs V = 3). OBS **V = 2**.
- **CK-B** (short, action 51): idx 4 = same (−27.82, −1.49) (ordinal 5 is
  shared: `_6_1` (27.77, −1.99) is 0.5 m off, `_6_2` exact); idx 7–10 at
  x ≈ −37.0/−36.9/−37.9/−37.8 — matches no table (see L3). Best rms 2.77
  vs V = 1, 6.14 vs V = 2. OBS **V = 1** (nearest; ordinal-5 + short-corner
  action agree).

So **V varies corner to corner at fixed N = 6, and V = 1 pairs with the
short-corner action (51) while V = 2 pairs with the long cross (50)**
(OBS ×2). INF(high): V is the short/long choice — the `CORNERKICK_SHORT`
branch of choreography §12 (partner test inside 108 frames) selects V = 1;
else V = 2/3. The registry-reader function itself is still not located
(it computes the id, parent §3.2), but the selection semantic is closed.

## L2. The −46.06 collapse cause (parent §5 item 5 — CLOSED live)

CK-B shows the same collapse live: at play 2 (stg 22, teleport running)
attackers idx 7–10 walk (−40.8, −51.65, −41.2, −46.25) → (−45.14, −52.08,
−42.99, −48.69); **at the first play-3 frame (stg 21) all four snap to
(−37.04, −37.04, −37.82, −37.83)** — a teleported cluster, then they hold
(−37.0/−36.9/−37.9/−37.8) through the wait. OBS.

That cluster is NOT a table spot (table x = 36–45) and NOT the budget clamp
(the budget move only runs at stages 10/13 per choreography §11.2; stg 21
is past it). At that same frame the four have state 0 (clear) while idx 6
shows MARK (54) — i.e. they are **not yet paired, stacked on the stage-10
MARK-branch outlet/fallback mark** (`FUN_140926ef0` MODE-4 branch moves
state-MARK men to `T+0x26c` marks; unpaired attackers pile on one fallback
spot). CK-A by contrast paired immediately (all six on `_6_2` rows at
play 3). So the CK1/CK-B x-collapse = **attackers not yet assigned ordinals
sitting on the shared fallback mark at the ready frame**, not a clamp.
INF(med-high): exact fallback spot = one of the `positionCK` ordinals 0–4
rows; CK-B's (−37, −7..−8) vs base-table ordinals {(−18.23, 0.66),
(1.87, −5.41), (7.49, 5.30), (23.86, 23.73), (18.39, −15.84)} mirrored —
no match, so it is a `T+0x26c` mark, not a table row. CK1's −46.06 is the
same phenomenon one budget window later (its four kept walking to x = −46.06
during the wait: budget moves toward marks at stages 10/13 fire per side,
and the fallback mark itself moved).

## L3. Template-writer timing (parent §5 item 1 — narrowed live)

Defending side 1's `T+0x4b4c` across the play 2 → play 3 edge (CK-A):

- play 2: slot 1 (−41.56, +4.5), slot 2 (−19.53, −16.59), slot 4 (−41.56, −1.4),
  slot 5 (−36.14, 0) — the (−F[8], z) template shape with drifting z.
- first play-3 frame: slot 1 → (−45.56, +0.66), slot 4 → (−45.56, −3.6),
  slot 7 → (−41.56, −6.0) exactly; slot 5 stays (−36.14, 0).

OBS: the template-to-mark transition lands **between the last play-2 frame
and the first play-3 frame (stg 22 → stg 21)** — i.e. inside the 20-tick
placement writer's stage-21 pass (`FUN_140926ef0` at `MI+0x11b0` = 21,
choreography §11.2 rows: stage 21 = the mark-occupancy pass). The writer
function itself is still open, but the window is now one stage: **watch
`T+0x4b4c` for `r64`-base stores with the MI stage field == 21 during a
corner stoppage** (perf mem watch, or poll at 54 Hz across the 22→21 edge).
INF(high): the writer is `FUN_140926ef0`'s MODE-4 branch itself (it already
owns the stage-21 mark move) copying zone-relative spots, not a separate
template function — parent §1's "no displacement store" scan is consistent
with a base+register copy inside that pass.

Also OBS: defending marks at the last CK-A frame: `sta` =
[0,54,54,0,54,0,54,54,109,54,0], `tgt` = [255,8,5,255,1,255,10,7,255,9,255] —
six MARK + one POST_MAN (slot 8, 109), targets = attacker slots; same shape
as CK1 (parent §2.3). Attacking side 0 `T+0x26c` last frame: six slots at
x = −47.06 (z +3.38/+2.02/−4.59/−2.31/+3.25/+5.82/−1.14) = the six box men
on the spot-line marks; `T+0x6d0` reads garbage here (stale T pointer — the
acq-time T went stale after the reset; N = 6 still confirmed via
`T+0xb8a4` = 2 read at arm time... see caveat below).

Caveat (OBS): the `N`/`lvl` fields in these captures read 518/260 and
lvl = 2 — the low word matches (level 2) but the full-word values are
stale-pointer garbage (T pointers were acquired before a `reset` event and
went stale). N = 6 is INF(high) from level 2 + the V = 2 spot match, not
from `T+0x6d0` directly. Next capture re-acquires T after `reset`.

## L4. 0x31 attribution (parent §5 item 4 — narrowed, still open)

The kicker ap's `+0xab8` 0x40-B block was captured every frame, but
`[q+0x28]` never reads 0x31 in either corner: the word walks
0x000c0e24 → 0x00370cc1 (action 79) → …04d1 (action 49) → …04e0
(action 50/51) → …094c (phase 2). OBS: **the low byte cycles action-ish
values, never 0x31 during the MODE-4 wait** — consistent with the kicker
report: the AI emits cmd 2 (not the 0x31 command word) and the *action*
requests 0x31 through `kickRequest` only at strike transition. Both
captures armed at play 2 and sampled at 22 Hz; the 49→50/51 transition
(action enter) fell between samples (sp jumps 153→207 / 43→76).

INF(med): the 0x31 store lands in the single frame where the action flips
49/51 → 50 (the `CanEnter` frame). Exact next step: on the next corner,
poll the kicker ap's action byte (`ap+0xac8`) at 54 Hz and, on the
49→50/51 edge, immediately `perf mem:u` watch `ap+0xab8+0x28` for ≤ 60 s
under the lock — one of the three sites (§4: 0x14208142d / 0x1420815cd /
0x14208397c) must fire within that window.

Note: the capture's `kq` block starts at `ap+0xab8` and `+0x28` is its 11th
word; the word's top bytes (04d1/04e0/094c) look like a clip/phase tag, low
byte a state — the 0x31 command word, when written, should appear as the
full word 0x31 at exactly this offset. The offset is right; the timing
missed it.

## L5. FK line formula (parent §5 item 6 — still open, no data)

No MODE-5 restart occurred in this session (only throw-ins, goal kicks,
kick-offs, three corners). Nothing to fit. Ready sampler: the SetpieceLiveRE
scratch dir poller (`spoll.py`: 5 Hz mode/play poll, 22 Hz recorder on
MODE 4/5 + play 2/3, ap cache for instant arming); run it from match start
and one gate-true FK (d ≈ 20 or ≈ 40) plus FK2 (d = 30.6, line 11.75 m)
gives the three-point fit.

## L6. Ledger deltas vs parent EV-SPZ-0001..0008

- EV-SPZ-0007 (V varies per corner): **promoted to Confirmed (live ×2)** —
  V = 2 with long cross, V = 1 with short corner, N = 6 fixed.
- EV-SPZ-0001 (zone values): confirmed again (defence template shape at
  play 2 matches (−F[8], z) + keeper/box-line posts).
- New INF(high): V = short/long choice; collapse = unpaired fallback-mark
  stack; template lands in the stage-21 pass.

## L7. Session close (match ended, OBS orchestrator + MI-null check)

Poller stopped; 28 per-frame records + full mode log banked in the
SetpieceLiveRE scratch dir `live.jsonl`. Closing state per item:

| parent §5 item | state |
|---|---|
| 1. template writer | narrowed: stage-21 pass window + `FUN_140926ef0` suspect; watchpoint step ready |
| 2. passes 2b/6/7 | untouched (no FK data; corner marks confirm pass-5 shape only) |
| 3. corner V | **CLOSED**: V = short/long choice (V = 1 short, V = 2 long cross, N = 6) |
| 4. 0x31 attribution | narrowed: offset confirmed, store lands on the 49→50/51 edge; perf-watch step ready |
| 5. −46.06 collapse | **CLOSED**: unpaired fallback-mark stack at first play-3 frame |

## L8. Second match — three corners, V = 1 exact hit (OBS)

New match (side 0 dir +1, side 1 dir −1, level 2 both → N = 6). Poller
re-armed with fresh pointers (`live2.jsonl`): three MODE-4 captures.

- **CK-C** (side 1 restarts, R = (−51.78, +33.61)): full stage path
  play 2 stg 22 → play 2 stg 21 → play 3. At the first stg-21 frame the
  six box attackers snap to **(−27.77, −1.99), (−36.33, −10.74),
  (−36.23, −4.84), (−40.83, −10.23), (−43.35, −9.79), (−38.66, −7.76)**
  = mirrored `_6_1` ordinals 5–10 **to 0.00 m**. OBS **V = 1, exact**.
  Kicker 79/79 → 49/49 (clip 1233) → 50/50; long cross.
- **CK-D** (side 0 restarts, R = (+52.25, −33.21), mirrored corner):
  25 frames, kicker → 51/51 short corner. Attackers never settle on a
  table (best rms 4.30 vs V = 3) — same unpaired-stack shape as CK-B.
- **CK-E** (side 1 restarts, R = (+51.78, −33.61)): 28 frames, kicker →
  51/51 short corner, same unsettled shape.

Score: V = 1 exact once (with long-cross action 50 this time — so V = 1
is NOT exclusively the short-corner variant; parent L1's V↔action pairing
was coincidence of two samples). Revised INF(med): V rolls per corner
({1,2,3}) independently of the short/long branch; the short-corner action
(51) is chosen later by the partner test, not by V. CK-C also shows the
teleport snap lands exactly on the stg-21 frame (positions pre-snap walk,
post-snap exact) — the ordinal-fill pass runs at stage 21, same pass as
the template-to-mark transition (L3).

Defence template on CK-C (defending side 0, −x goal): play-2 t4b slots
1–8 = (−36.14/−47.06/−41.56 patterns, keeper (−50.73, 0), area corner
(−47.06, +7.16), outlet (−9.15, −2.66)); at stg 21 slots 3/5/6 jump to
x = −47.06 (goal-area lineProjection) while slots 4/7/8 hold −41.56
(spot line). Same two-line shape as L3. Ready marks: sta =
[0,0,0,0,109,54,54,54,54,54,0], tgt marks on attacker slots 16/17/19/20/21
(OBS). Kicker `+0xab8+0x28` word again never 0x31 (walks
0x100e33 → …01a8 → …04d1 → …04e0, OBS all frames ×3 corners).

## L9. First MODE-5 FK in live data — gate-false sample (OBS)

FK-D: side 0 restarts at R = (−10.94, +20.69), kicker idx 9, 38 frames
(stg 22 → 1 → 21 → wait). Gate: restarting dir +1, dir·R.x = −10.94 < 0
→ **gate FALSE** (defensive FK in own half, same class as parent FK1).
Defence (side 1) holds a deep line: t4b slots 1–4 at x = −47.06
(own box), slots 5–6 (−45.26), midfield (−14.87/−9.12); six MARK
(sta slots 2/4/6/7/8/9) with tgts on attacker slots 8/7/10/4/6/5 —
passes 6–7 pairing (no aerial rank), consistent with parent §2.4 FK1.
Kicker went 17/17 → 79/79 → 74/74 → 30/30 (clip 1261, phase 1);
`+0xab8+0x28` again never 0x31. FK-line fit still needs a gate-true FK;
FK-D + FK1 are both gate-false (defensive-half) samples.

## L10. Third match — usable window, 160 rows (OBS)

Window: ~13 min of state-1 play, 160 per-frame records banked in the
SetpieceLiveRE scratch dir `live3.jsonl` (side 0 dir +1, side 1 dir −1).
Captures: three MODE-4 corners (19/37/29 frames, all with full
play-2 → play-3 stage paths) + two MODE-5 FKs (26 + 16 frames).

- Corners: all three long-cross action 50 (kicker 79/79 → 49/49 →
  50/50; 49→50 edge at sp ≈ 195–272, OBS). V-classify at first play-3
  frame: best rms 4.35–5.16 (no exact hit — all three show the
  unpaired-stack shape, same as CK-B/CK-D/CK-E). No V = 1/2/3 exact this
  match.
- Zone-spot edge (piece 0, defending side 0): six consecutive play-2
  stg-1 frames hold the template ((-41.56, z) line, keeper (−50.73, 0),
  area corner (−49.81, +7.16), outlet (−9.15, +5.65)); the single
  stg-21 frame flips slots 1/3 to x = −45.56 and the outlet z to −2.66.
  OBS: the template→mark write lands exactly on the stg 1→21 transition
  (confirms L3's window to one stage edge).
- Per-frame corner request (Main priority): all 19 frames of piece 0
  dumped — `+0xab8+0x28` walks 0x680cc1 (79) → …04d1 (49) → …04e0 (50),
  never 0x31 (OBS). The 49→50 edge falls between sp 181 and sp 195
  (14-frame sample gap at 22 Hz).
- Hardware watchpoint attempt: `perf mem:addr:w` on the kicker word
  failed — PMU breakpoint events unavailable in this environment
  (`sys_perf_event_open: No space left on device`; all debug registers
  busy). Tried twice under the gf-perf lock, < 60 s each, no trace.
  The 0x31 store therefore remains unattributed; the sample-gap analysis
  above is the closest static timing.
- FK-E (side 0, R = (−20.09, +5.07)): dir·R.x = −20.09 → gate false.
  Defence line at x = 23.47/15.10/13.54 (mid-third zonal); nine MARK.
- FK-F (side 1 dir −1, R = (−46.42, +7.85)): dir·R.x = +46.42 but
  > F[6] (36.14) → gate false (too deep — own-half restart). Defence
  line x = 10.0/−6.75; ten MARK.
- **FK-line fit: still open.** All three live FKs (FK-D/E/F) plus parent
  FK1 are gate-false; only parent FK2 is gate-true. The fit needs one
  gate-true FK at d ≈ 20 or ≈ 40.

## L11. Final state per parent §5 item

| parent §5 item | state |
|---|---|
| 1. template writer | narrowed to the stg 1/21→21 single edge (L10); writer fn still open; needs a debug-register window when free |
| 2. passes 2b/6/7 | partial: three gate-false FKs confirm passes-6–7 shape; pass internals untouched |
| 3. corner V | **CLOSED**: per-corner roll (V = 1 exact CK-C, V = 2 exact CK-A); V↔action pairing killed (CK-C V = 1 with long cross) |
| 4. 0x31 attribution | narrowed: 49→50 edge at sp ≈ 195–272, 14-frame gap; perf-PMU unavailable — needs a 54 Hz poll across the edge next match |
| 5. −46.06 collapse | **CLOSED**: unpaired fallback-mark stack (L2 + three more instances) |
| 6. FK line fit | open: 1 gate-true sample (FK2, 11.75 m); needs one more |

Samplers ready (SetpieceLiveRE scratch dir): `spoll.py` (5 Hz arm,
22 Hz recorder, ap cache), `analyze.py` (V-classify + line fit + stage
track). Habit: re-acquire T/P pointers after any `reset` event (stale-T
caveat L3); poll kicker `ap+0xac8` at 54 Hz across the wait to catch the
49→50 edge for the 0x31 attribution.

## L12. Gate-true FK found on reaudit — fit narrowed, still open (OBS)

The stale-job tail revealed a third MODE-5 piece (33 frames) missed in
L10's inventory. **FK-G**: side 0 (dir +1) restarts at R = (+23.50, −14.06),
dir·R.x = +23.50 ∈ [0, 36.14], |z| = 14.06 ≤ 20.12 → **gate TRUE** (OBS).
Ball 29.06 m from the +52.56 goal line. Defence (side 1, seven MARK at
ready: sta slots 1/3/4/5/6/7/8, tgts on attacker slots 10/9/8/4/6/1/7):
line slots 1/2/4 cluster at **x = −20.55** (slots 1,2,4; z −0.43/+9.48/−11.1)
with slot pairs at −12.18/−10.61 — the guard line sits
**23.50 − (−20.55) = 44.05 m** behind the ball?? No: defending side 1's
own goal is at −52.56; the line at x = −20.55 is 32 m upfield of its own
goal and the ball at +23.50 is 44 m *beyond* the line toward the far goal.
Read correctly goal-side: the restarters (side 0, dir +1) attack +x; the
defending line should sit goal-side (−x) of the ball — and it does, but
44 m deep is no guard. Hmm — side 1's t4b slot 0 = (−45.55, −1.29) is its
keeper (own-frame −(F[0]−1.83) mirrored = −50.73? no, −45.55).

Recheck (OBS): side 1 defends the −x goal here (side 1 dir −1, own goal
−52.56). Keeper slot 0 (−45.55, −1.29) is 7 m off its line — mid-transition
(last frame sp 218, kick action 32 already entered: LONGPASS struck, line
breaking). At the first play-3 frame (stg 1) the line slots 1/2/4 sat at
x = −20.55 with the ball at +23.50 — 44 m of space because this FK is a
mid-third restart (R.x = +23.50 is on the *restarters'* attacking half but
29 m from goal, outside the box): the box-zone gate held (pass 5 ran —
seven MARK with ranked tgts, OBS) but there is no 12 m wall/line this far
out; the "line" is the ordinary shape line, same as the gate-false FKs.

Fit vs parent §1.2 (OBS): FK2 (d = 30.6, gate true): line 11.75 m
goal-side of ball. FK-G (d = 29.1, gate true): **no guard line** — line
slots at −20.55, i.e. the shape line, not 12 m off the ball. So the ~12 m
guard is NOT a pure function of d: INF(high) it needs d ≲ 25 m (FK2 at
30.6 had it, FK-G at 29.1 does not — boundary between 29 and 31 m?) or a
|z|/angle condition (|z| = 0.31 vs 14.06). Two gate-true samples now
banked (FK2 + FK-G); the fit needs one more gate-true FK with d < 25 and
|z| small to separate the distance vs angle gate. Item 6 stays open but is
now one sample from closing with a concrete ask.
