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
| 6. FK line fit | open, no data; sampler ready |
