# PES 2021 throw-in — end-to-end choreography (RE report, ThrowInRE)

Read-only RE. Sources:
- Static: the mid-match image `pesmem.bin` (base 0x140000000), GhidraMCP decompiles, headless `DecompAt` for
  addresses Ghidra has no function for, capstone over raw bytes, and the on-disk exe for `.impdata` thunks.
- Live: read-only `pread` of `/proc/<pid>/mem` of the running game (no writes, no ptrace, no input). AI-vs-AI
  match, modded install. No player/team names appear here; players are referred to by slot index 0..21.

Tags: **OBS** = read in raw bytes / disassembly / decompile at the named address, or observed live.
**INF(conf)** = inference. All frame counts are 54 Hz logic frames unless marked "30 fps" (clip-data units).

Scratch (not in the repo): `~/ghidra-projects/scratch/ThrowInRE/` — `dec_*.c` / `dec/*.c` decompiles, `m.py`
(image helpers), `dsearch.py`/`lscan.py` (operand scanners), `deser.py` (constant-object field map),
`tiwatch.py` (live restart-state watcher), `ballwatch.py` (live ball logger), `motinfo.py` (live motion-table
dump), `live1.jsonl` (raw live log), `motinfo_throwin.txt`, `motinfo2.txt`.

Conventions used throughout (OBS unless stated):
- `G = *(u64*)0x1436F3DC8` (match context). `MI = *(G+0x270)` (published match-info snapshot).
  `F = *(float**)(G+0x2aa8)` field record: `F[0]` = half-length, `F[1]` = half-width (both read by every
  restart function below).
- Fox space: Y up; the pitch length runs along **X**, the touchlines are at **Z = ±F[1]**.
  Facing yaw (degrees, `ap+0x20`): **0° faces +Z, 180° faces −Z** (INF high: the throw-in code turns a taker on
  the +Z line to 180° and on the −Z line to 0°, i.e. into the pitch).
- `ap` = an `AnimePlayer` (`match::AnimePlayer`, vtable 0x1425E34D8), action id `u8 ap+0xac8`, sub-id `+0xac9`,
  previous id/sub `+0xaca/+0xacb`, phase byte `ap+0x245c`, clip id `u16 ap+0xae0`, position `ap+0x8..0x10`.
  ANIME ids (name table 0x1434A5470): 2 MOVE, **26 THROWIN_LOOP, 27 THROWIN, 28 THROWIN_LONG,
  29 SEAMLESS_THROWIN**.
- Current clip frame (interface slot 7 → `0x1406e68c0`): `frame = (int)(54 · t_clip + 0.5)` — every action
  timing compares 54 Hz frames. Clip-data frames are 30 fps; the engine converts with
  `f54 = (int)(2·f30 / rate · 54/60 + 0.5)` (`FUN_1406af620`, `FUN_1406af8e0`, `FUN_1406b08a0`).

---

## 0. One-screen summary (live-validated)

```
ball wholly over touchline  (judge event 10 → judge+0xcc = 3)            frame F0
  MI+0x116c 1→2 (stopped), MODE(MI+0x1554)=2, side = team that did NOT touch last,
  restart point R = (ball.x clamped to |x|≤F[0]-1, 0, ±(F[1]+0.25))
F0+1      taker chosen (MI+0x173c): seamless path = fastest ball-arrival of the 4 top-ranked team-mates;
          ceremony path = same-flank full-back with a 20 m bonus (role-biased distance)
          taker runs (ANIME MOVE) to the ball wherever it stopped
+36..+120 SEAMLESS_THROWIN: pick the ball up (throwin_pickupball_000/090/180)
+30       ball in hands → MI+0x116c 2→3 (set-play ready)
          carry one-handed (throwin_run_3_3_000_onehand) to a spot 0.7 m outside the line,
          within ±2 m of R.x; lift (throwin_seamless_liftupball) facing the pitch
          THROWIN_LOOP: aim loop (parametric turn group 94)
ready+18  set-play counter MI+0x11ac starts (0,1,2,…)
counter=83 (ready+101 frames = 1.87 s) the AI throw command lands → THROWIN / THROWIN_LONG
          release clip chosen by target distance (≤13 short, ≤25 mid, else long; run variants
          if thrown straight out of the carry)
release   at the clip's release event (short 18, mid 27, long 99, run-mid 36 frames)
          → MI+0x116c 3→1 (in play), ball launched toward the target with a flight time
```

Measured live (OBS, `live1.jsonl`, 5 AI throw-ins): from ball-out F0 to in-play **185–288 frames
(3.4–5.3 s)**; from set-play-ready to the throw command **101 frames every time**; release 18/27/36 frames
after the throw clip starts, exactly the clip's release event.

---

## 1. Ball out and the restart point

### 1.1 Detection (OBS)
- Judge object (`Flow+0x908`, Flow = the match-flow object) event handler `FUN_14050e4a0` @0x14050e4a0, event
  type 10 (ball event) while `MI+0x116c == 1` (in play) and event flag bit 2:
  `|ball.x| ≤ F[0]` (ball x at `ball+0x27b8`) and `|ball.z| > F[1]` → `judge+0xcc = 3` (touchline out).
  (`=2` end line, `=1` goal.) INF(med): bit 2 is the "ball wholly over a line" event from the ball system.
- `FUN_14050b490` @0x14050b490 consumes `judge+0xcc`: case 3 → `FUN_14050e320` (throw-in) unless an
  advantage/foul is pending (`judge+0xc` advantage counter, set to `fps·4 = 216` frames; a pending foul wins
  via `thunk_EXT_FUN_143f546f0`).

### 1.2 Which team throws (OBS `FUN_14050e320` @0x14050e320)
Walks the 8-entry ball-touch history (`FUN_140a3e190`), skipping entries stamped with the current frame
(`MI+0x1198`), takes the last toucher's player index `p`; `side = (p < 11)` → **the team that did not touch
last** (side 0 = home slots 0–10, 1 = away 11–21). If that team has no eligible players (sum of three counters
at team+0x87a5/b9/cd == 0, not training) the side flips. Calls
`FUN_14050e770(judge, side, kind=2, stopKind=4, pos=NULL, 0, 0xff, 0xff)`.

### 1.3 The restart record and point (OBS `FUN_14050e770`, `FUN_14050d280`)
Restart block = `MI+0x1554` (copied from the match object each frame by `FUN_140422030`):

| off | field | throw-in value |
|---|---|---|
| +0x00 (MI+0x1554) | MODE | 2 (`SituationData.MODE` THROW_IN) |
| +0x04 (+0x1558) | side | 0/1 (team taking) |
| +0x08 (+0x155c) | current ball display point (vec3) | follows the ball / carrier during the stop (see §1.4) |
| +0x14 (+0x1568) | **official restart point R** (vec3) | below |
| +0x20 (+0x1574) | stop kind | 4 |
| +0x28 (+0x157c) | last toucher index (judge+0xa0) | |

`FUN_14050d280(out, kind=2, F, ballPos)`:
`R = (ball.x, 0, sign(ball.z) · (F[1] + 0.25))` (0.25 = `DAT_142592228`), then `FUN_14050e770` clamps
`|R.x| ≤ F[0] − 1.0` (`DAT_14256ae1c`). So **R is the crossing x, 25 cm outside the touchline, never closer
than 1 m to a goal line.** OBS live: R.z = ±34.31 on this pitch (→ F[1] = 34.06).

### 1.4 The moving "display" point (OBS `FUN_140422030` lines ~300–360)
Each frame `MI+0x155c` is rewritten from the match object: while `match+0x74c == 2` it is the live ball
position clamped into the field rectangle; `== 3` → ball x clamped, z = old; otherwise = R. OBS live: during
the taker's approach/carry `MI+0x155c.x` drifts with the ball (e.g. R.x 21.03 → 23.8 → 19.7) while `MI+0x1568`
stays fixed.

---

## 2. Taker selection

### 2.1 The rule (OBS `FUN_1404a93a0` @0x1404a93a0, called from `FUN_1404a7d60` case MODE 2)
Inputs: restart side `t`, restart point `P` (`MI+0x155c` x/z), the team's attack-direction sign
`dir = (s8) team+0x254`, `flankOfR = (dir · P.z > 0)`.
For each of team `t`'s 11 slots that is on the pitch (`MI+0x1748[idx] != 0`):
```
role = team->slotRole[slot]            // i32 at team+0x4bd4 + 4*slot  (team = *(G+(t+0xa1)*0x28))
if role < 0 or role in {2,3,4,5,6}:    // others (0,1,7,8,9…) are not considered in pass 1
    d = |playerPos.xz − P.xz|          // playerPos = *(GetPlayerData(idx)+0x55c / +0x564)
    if role in {2,5}:                   // the flank roles
        if (team->slotBase[slot].z > 0) != flankOfR:  skip      // wrong flank (team+0x46e4+12*slot+8)
    if role in {3,6}:  d += 10.0        // DAT_142588210
    if role == 2:      d += -20.0       // DAT_1425a946c  (bonus)
    keep the minimum d (start cap 500.0, DAT_14259be74)
if none found: the nearest on-pitch player of team t by raw distance (any role)
```
INF(med) role meaning (engine's 10-way role class; matches the masks used here):
0 GK, 1 CB, **2 full-back/side-back (SB)**, 3 DMF, 4 CMF, **5 side-midfielder (SMF)**, 6 AMF, 7+ forwards.
So on the ceremony path the throw-in is taken by **the same-flank full-back unless another candidate is more
than 20 m closer**; then the same-flank wide midfielder or a CMF by raw distance; DMF/AMF only if ≥ 10 m closer
than those; centre-backs, keepers and forwards only as a fallback.
- When it runs (OBS `FUN_1404a7d60`): only when there is no valid taker yet (`local_1d8` set: current kicker
  `> 0x15` or not on the pitch). It is re-evaluated on every flow event `0x13` while `MI+0x116c == 3`
  (`FUN_1404514f0` case 0x13), but with a valid taker already published it returns unchanged. Event `0x12`
  forces a specific taker (user switch).
- **On the seamless path (§3.1), which every observed AI throw-in took, the taker is the seamless candidate of
  §2.2 instead** (fastest ball-arrival among the first four ranked team-mates), published by sequencer step 0x33.
  OBS live: the taker is published 1 frame after the out (`MI+0x173c`); once it switched one frame later
  (slot 5 → 2, throw-in #2), consistent with §2.2 being re-run while the program is built. Takers seen: slots
  3, 2, 15, 5, 5, 5.

### 2.2 Seamless candidate (OBS `FUN_14044e230` @0x14044e230)
When the flow considers the quick "seamless" restart (§3.1) it first proposes `Flow+0x27f4` = the restart
team's rank-0 entry of the AI ball-arrival list (`AIctx+0x2470c`, 11 × 8 B per team) and replaces it with ranks
1..3 if one of them arrives more than `fps/3 = 18` frames earlier (arrival time at `AIctx+0x24844`). If the
rank-0 player is flagged ineligible (`team+0x4c00[slot] == 0`) rank 1 is used.

### 2.3 Long-throw specialists
Not part of taker choice (OBS: no skill read in `FUN_1404a93a0`). The **Long Throw** skill only changes reach
and speed (§5).

---

## 3. Approach, pickup, placement and facing

### 3.1 Program choice: seamless vs. ceremony (OBS `FUN_1404500c0`, `FUN_140444f00`, `FUN_14044b8a0`)
The stop-program builder `FUN_1404500c0` pushes sequencer step **0x33** (seamless throw-in handler
`FUN_14044b8a0`) instead of the normal whistle program when `FUN_140444f00` is true:
- ball speed `·3.6 ≤ 12 km/h` (`DAT_14259be04`), ball height `≤ 1.5 m`, `|R.x − ball.x| ≤ 20 m`
  (`DAT_14259be1c`), ball within 20 m of the touchline, no human-side override;
- the candidate taker (§2.2) can reach it: arrival estimate `< 4·54 = 216` frames, reach `≤ 20 m`, not
  already in certain actions (`player+0x5d0 − 0x58 > 2`) and `|player.z| ≥ F[1] − 12 m`.
Step 0x33 sub-state 0 publishes the candidate as kicker; while `FUN_140450ac0` keeps holding it continues;
if it fails it falls back to the normal program `[0x10, 0x45, 0x46, 0x16, 0x12, 0x13]`; when the taker has the
ball in a throw-in action it appends `[0x15, 0x34, 0x19, 0x1a]` (restart).
OBS live: all 5 observed AI throw-ins used the seamless pickup (no ball boy, no teleport).

### 3.2 `SeamlessThrowin` (id 29) — `Update` 0x140791ec0 (OBS decompile)
| phase | what happens |
|---|---|
| 0 | reset motion delays → 1 |
| 1 | when the root warp is idle: predict the ball at the pickup contact frame (`FUN_1406b03d0(0xcf5)` → ball pos at that future frame). **Throw spot S**: on the line `z = sign·(F[1] + 0.7)` (`DAT_14257a194`), x from the approach line, then `S.x` clamped to `R.x ± 2.0 m` (`DAT_142565604`), and `S.x = R.x` if `|S.x| > F[0] − 0.5`. Pick-up clip: row 0x9bd `throwin_pickupball` column = angle class of (spot bearing − yaw) → `throwin_pickupball_000/090/180` (mirror by column 4/0xd). If the clip's ball-contact root point is ≥ 5 m away (`DAT_14259222c`) → abort to MOVE. Else play (rate 1.0, blend 0.2), root-warp so the hands meet the ball at the contact frame → 2 |
| 2 | at the contact event: `ap+0x2460 |= 0x300` (ball held) → 3 |
| 3 | ball held → notify the flow (`FUN_140445750`) — **this is the frame `MI+0x116c` goes 2→3 (set-play ready)** (OBS live: ready 2 frames after phase 3). Yaw-warp toward S. At clip end (or last kind-5 event if < 1 m from S): play row 0x9bf `throwin_run_3_3_000_onehand_ver21` (col 8) at rate 0.85/1.0/1.15/1.3 by speed class `this+0x14` (0..3), blend 0.3 → 4; if the ball was lost → MOVE |
| 4 | carry: turn toward S at ≤ 360/54 = 6.67°/frame; refresh S if the gate test passes; when `|ap − S| < 0.65 m` (`DAT_14259d5d0`) and `|x| < F[0] − 0.5`: play row 0x9be `throwin_seamless_liftupball` (col 9), yaw-warp to **180° (or 0° if S.z < 0)** over the clip, root-warp onto S → 5 |
| 5 | at clip end → `ChangeAction(THROWIN_LOOP 0x1a)` |

Running throw shortcut (OBS `SeamlessThrowin` slot 14 `0x1407945c0`, used by `ThrowinNormal::CanEnter`):
during the carry (phase ≠ 5) a THROWIN request is accepted directly if `|ap − S| ≤ 5.0`
(`seamlessThrowin.dist`), `|ap.z| ≤ |S.z| + 3.0` (`touchLineDist`) and, when outside the line, the body faces
within 90° of straight into the pitch. Then the run-up release rows are used (§4.2). OBS live: throw-in #3 went
29/phase 4 → 28 with `throwin_release_run_000_mid`, no loop.

### 3.3 Where the taker stands (OBS)
- Standing line: **0.7 m outside the touchline** (S.z). OBS live: taker root z = 34.74–34.76 while
  F[1] = 34.06.
- Standing x: within ±2 m of R.x (OBS live offsets −0.5 … +2.5 m: the carry stops ~0.6 m short of S).
- `ThrowinLoop::SetKick` (slot 12 `0x140794280`, used when the flow places the taker itself, i.e. the
  non-seamless program): stand point = `req.pos − Rot(yaw)·(−ballAtRelease.xz)` with the release offset read
  from clip 0xcfa's ball key (`FUN_1406e8cc0` → `(0.022, 1.847, 0.795) m · bodyScale`, bodyScale = `ap+0x3b18`
  clamped [0.9166, 1.0833] for xz) — the taker stands so the ball at release is over the point.
- Facing: 180° on the +Z line, 0° on the −Z line, i.e. perpendicular into the pitch; the aim loop then turns
  up to ±80° (§4.1).

---

## 4. The throw

### 4.1 `ThrowinLoop` (id 26) — CanEnter 0x140794fa0, Update 0x140793590, SetKick 0x140794280 (OBS)
CanEnter: current action is 1 (IDLE_RESET) or SEAMLESS_THROWIN whose `throwin_seamless_liftupball` (0xd01)
has finished. Enter/Exit are `ret`.

Every tick: `base = 180° (taker on +Z) / 0° (−Z)`; `aim = angleDiff(base, yaw→target)` (negated on −Z);
`d = |taker − target|`; `far = d > 15 m && |aim| > move.beginAngle (80°)`;
`roomToStep = |taker.x|+1.2·s < F[0]−0.5 && |spot.x − (taker.x − s·3.0·0.5)| < 3.0`.

| phase | body |
|---|---|
| 0 (entry) | `ap+0x2476 = 0xcf4` (`throwin_loop`), re-enter id 26, store spot = taker pos, blend = 0, cooldown = 0 → 1 |
| 1 | play the **parametric group-94 turn loop** (`FUN_1406f1280`, members `throwin_turn_067_loop*M` 0.0 / `throwin_loop` 0.5 / `throwin_turn_067_loop` 1.0) at rate 1, blend 0.2/0.5; yaw-warp to `base` over 13 frames → 2 |
| 2 | blend target `= 0.5 + 0.5·aim/80°`, eased `+= (target − cur)·0.12`/frame (`DAT_1425cb294`); if `far`: side-step (→3) when `roomToStep`, else turn-in-place (→5) unless cooldown |
| 3 | side-step: row 0x9c2 `throwin_side_step_f` / 0x9c1 `throwin_side_step` (by line), column 4 or 0xd by aim sign (`throwin_slide_1_1_090/f090_onehand`), start 0.42, rate 1.4, blend 0.15 → 4 |
| 4 | until clip end or the aim sign flips → 1 (or 3 again); snap z back to the stored line if drifted > 0.05 m |
| 5 | turn-in-place: clip 0xcfa at rate 0.85, blend 0.3, root warp to the spot over 17 frames, yaw `base ± 65°` (`DAT_14259be48`) over 13 frames → 6 |
| 6 | after frame 15 → cooldown 35 frames → 1 |

Exits: `FUN_140791e30` (§6.3 forced throw) → THROWIN with flag `0x2000000`; `MI+0x116c == 1` → MOVE.
OBS live: loop phases 0→1→2 on three consecutive frames, then phase 2 until the throw (26–63 frames).

### 4.2 `ThrowinNormal` (id 27) / `ThrowinLong` (id 28) — shared Update 0x140792e10 (OBS)
Both ids use one vtable body; CanEnter 0x140794f20 = "current is THROWIN_LOOP, or SEAMLESS and slot 14 allows".
The long/normal id only changes the ball request path label (both → `FUN_14081c640`) and the AI's receiver
lead time (§5.3). The clip is chosen by **distance to the target** `d = |taker − target(ap+0x2bf4/0x2bfc)|`:

| entry | row | clip (30 fps len / 54 Hz len / **release frame 54 Hz**) | rate |
|---|---|---|---|
| from loop, d ≤ 13 | 0x9b8 `throwin_release_idle_short` | `throwin_release_idle_000_short` (25 / 45 / **18**) | 0.75 (d≤5) … `0.75+0.3·(d−5)/5` … 1.05 (d≥10) |
| from loop, 13 < d ≤ 25 | 0x9b9 `…_idle_mid` | `throwin_release_idle_000_mid` (30 / 54 / **27**) | 1.0 |
| from loop, d > 25 | 0x9ba `…_idle_long` | `throwin_release_idle_000_long` (73 / 131 / **99**) | 1.0 |
| from carry, d ≤ 13 | 0x9bb `…_run_short` | `throwin_release_run_000_short` / `_067_short` (36/65/27, 27/49/27) | 1.0 |
| from carry, d > 13 | 0x9bc `…_run_mid` | `throwin_release_run_000_mid` / `_067_mid` (38/68/36, 40/72/36) | 1.0 |

(`13` = `DAT_1425c5948`, `25` = `DAT_14259be20`. Column = angle class of `yaw→target − yaw` via
`FUN_1406de990`; columns 7/10 = 000 variants (mirror/plain), 5/12 = 067 variants. Release frames are the clips'
kind-3 event `((raw+4)·2)·0.9`, read live from the motion table, OBS; they equal the clips' second ball key.)
Phase 0 also starts a yaw warp onto the target over `max(clipTurnFrames, 12)` capped at `release − 6` frames
(seamless entry: 14 frames).
Phase 1 (each tick until the clip ends):
- **before release** the taker is pushed so the hand further outside stays ≥ `F[1] + 0.1 m` from the centre
  line (`FUN_141eb51a0` bones 3/6, `DAT_142588200`) — **the ball is always released from behind the line**;
- **at the release event** (`FUN_1406ec260(ap, FUN_1406b08a0(clip))`): `ap+0x2460 |= 0x80`, the flow is told
  the ball is in play (`FUN_140445750`) and the ball request is executed (`FUN_1406e47a0` case 0x1b/0x1c →
  `FUN_14081c640`, §5.2);
- clip end → MOVE. Slot 10 (`0x1407951f0`): head/look target = thrower + 3 m forward (+0.5 m up for the
  long clip 0xcf8).
OBS live (in-play − throw start): short 18 ✓, mid 27–28 ✓, run-mid 36–37 ✓ frames.

Hand/ball key positions (OBS, motion table kind-1 entries, cm, taker-local, +Z forward): pickup contact
(0, 11.6, 115) at 30-fps frame 15; release `idle_short` (2.2, 184.7, 79.5), `idle_mid` (5.0, 195.3, 104.7),
`idle_long` (−5.9, 179.6, 147.4), `run_000_short` (0, 185.7, 171.5), `run_000_mid` (−2.2, 188.0, 204.8),
`run_067_short` (76.8, 192.6, 70.6), `run_067_mid` (103.5, 182.3, 127.6). (Live table at
`*(*(0x1436F23C0))` + clip·0x390.)

---

## 5. Ball launch

### 5.1 Target point (OBS `FUN_1409bbf10` @0x1409bbf10; receiver lead `FUN_1409bb450` @0x1409bb450)
The throw command (pad/AI command struct) carries either a receiver index or a gauge + stick angle.
- **Receiver given** (`FUN_1409bb450`): if the receiver is moving (speed over 0.694/54 s ≥ `100/(54·32.7)·0.8`)
  or is ≥ 14 m (`196 = 14²`) from the ball → target = his predicted position after `54` frames (`108` for
  THROWIN_LONG, id 0x1c) via `FUN_140a75740`; else target = his position + 0.5 m along (his facing + 180°).
  Reach clamp: if the target is farther than **30 m** (`DAT_142591140`), or **37 m** with the **Long Throw**
  skill (`DAT_1425cd82c`; skill bit 19 of the player's skill mask `PlayerDataBlock+0x40`), it is pulled back to
  that radius along the bearing; closer than 1.5 m → pushed out to 1.5 m.
- **No receiver (free aim)**: from the thrower's position along the stick angle (`FUN_1409bb250`: limited to
  ±70° of the into-pitch bearing, `DAT_14259be4c`) by the **gauge→distance curve** `FUN_1409bb360`:
  ```
  g ∈ [0,1];  near=search.distNear 5, mid=search.distMiddle 15,
  far = search.distFarNormal 25  (search.distFarLongPara 32 with Long Throw)
  g ≤ gageMiddle(0.6): dist = near + (mid−near)·g/0.6
  g > 0.6:             dist = mid + (far−mid)·(g−0.6)/0.4
  ```
  `longThrowSupport` (adjustRate 0.5) bends the angle halfway toward a found teammate only when
  `throwin.mode == 1` — shipped `mode = 0`, so **off** (OBS live object).
- Then: clipped to the pitch (`FUN_140a54560`), `|x| ≤ F[0]−1`, `|z| ≤ F[1]−1`. Target height = 0.5 m
  (`*cmd == 100` → 1.8 m, `DAT_14259bde0`), scaled by receiverHeightCm/180 for a receiver. If the target is within
  12.5 m of the ball (`156.25`), the aim point is set 0.5 m from the ball toward the target and the real target kept
  at `cmd+0x2c`.

### 5.2 Speed / flight time (OBS `FUN_14081c640` @0x14081c640 — called at release from `FUN_1406e47a0`)
`D` = horizontal thrower→target distance. Constants from `throwin.o` (live object id 0x19, §8):
```
t   = clamp((D − adjustDistMin 10) / (adjustDistMax 20 − 10), 0, 1)
add = t · adjustSpeed (10 km/h)
vMin = baseMinKph 35 + add ;  vMax = baseMaxKph 50 + add          (km/h)
skill = 100 if Long Throw (skill bit 19) else 80     (DAT_14253ee30 / DAT_142596e6c)
vFast = vMin + (vMax − vMin)·skill/100
framesFast = D / (vFast·1000 / (54·3600))   framesSlow = D / (vMin·1000 / (54·3600))
b   = clamp((D − 12) · 0.125, 0, 1)                                   (DAT_14259be04, DAT_14259bdcc)
frames = framesSlow + b·(skill/100)·(framesFast − framesSlow)
```
With a valid receiver ≥ 2 m from the target, frames are re-timed to his arrival:
bearing difference ≤ 60° → his predicted run time + `54·0.3·clamp((Δ−30)/20,0,1)`; > 60° → `54·0.5 +
dist/(16 km/h)`. Final `frames = clamp(frames, framesFast, framesSlow)`.
Request written: `req[0] = 0x10` (throw-in kick kind), `req+0x58 = 1` (time-of-flight request) with
`req+0x74 = (int)frames`, `req+0xd8 = 1`, `req+0xdc = 3.5·(1 − e) − 4.0` where
`e = clamp((D − 5)/30, 0, 1)` (−0.5 at ≤ 5 m … −4.0 at ≥ 35 m; INF(low): a launch-angle/spin bias consumed by
the ball solver — not traced). The ball solver then launches from the hands so the ball reaches the target after
`frames` ticks (INF(med): same time-of-flight solver as passes).
Implied flight time (frames) and mean horizontal speed D/time, computed from this formula without a receiver
re-time (INF arithmetic, not measured): 5 m 28 f / 35 km/h · 10 m 56 f / 35 · 12 m 63 f / 37 · 15 m 68 f / 43
(Long Throw 65 f / 45) · 20 m 72 f / 54 (65 f / 60) · 25 m 90 f / 54 (81 f / 60) · 30 m 108 f / 54 (97 f / 60).

### 5.3 Attributes
OBS only the Long Throw skill bit (reach 30→37 m receiver / 25→32 m free aim; speed weight 80→100 %) and the
receiver's height (target height). No ability stat (kicking power, etc.) is read in `FUN_14081c640`,
`FUN_1409bb360`, `FUN_1409bb450` or `FUN_1409bbf10` (OBS absence in these bodies).

---

## 6. AI decision and timers

### 6.1 When the AI throws, and to whom (OBS `match::ai::bp::ThinkUnitSetPlay` slot 2 `0x142135c50` →
`FUN_142137460` @0x142137460 for MODE 2 and the player == `MI+0x173c`)
Runs while `MI+0x11b2` (set-play active). `this+0x40` = frames since the restart became takeable.
```
r = FUN_142135700()                      // receiver search, below
if r invalid and (flag 0x13 or elapsed ≥ 3·54): r = protected fallback (.impdata thunk 0x15b6efa70)
if r valid:   if elapsed ≥ (int)(54·1.5)=81: command 2 (throw to r)
else: for teammates in the team's order list: first unexcluded, unmarked one →
      elapsed < 54·3.0+0.5 → command 8 (aim/hold on him) else command 2 (throw to him)
      none → after 3.0 s: command 2 with no receiver (free throw)
(3.0 becomes 2.0 s in the G+0x400 mode)
```
OBS live: the throw action starts when the set-play counter `MI+0x11ac` reads **83** in all 5 cases (= 81 + 2
frames of command latency), i.e. **ready + 18 + 83 = ready + 101 frames (1.87 s)**.

Receiver search `FUN_142135700` @0x142135700 (constant `ballplayerSetplay`, id 0x25, live values):
for team-mates in the team's ordering (`FUN_140a714a0`), skip excluded/inactive ones; distance `d` to the thrower
must be in **[Throwin.range_min 2.0, Throwin.range_short 25.0)**; a team-mate more than 5 m behind the thrower
(along the attack direction) is skipped if an opponent is within 25 (units of the AI mark table) of him; then a
pass-course safety test (`FUN_14063dab0`, lead `3 + 5·(d−2)/23`, ×1.3 for backward options, 220, fps/4) — **the
first that passes is the receiver**.

Normal vs long id (OBS `ActionThrowin` slot 4/7 `0x1409bc650`/`0x1409bc4d0`): THROWIN_LONG (0x1c) when the
gauge > 0.9 (`DAT_14258c260`), or the receiver target is > 25 m from the ball (> 35 m in the receiver-flag
variant). Otherwise THROWIN (0x1b). OBS live: 4 of 5 AI throws used id 28 even with short clips — the clip is
chosen by distance (§4.2), not by the id.

### 6.2 Set-play counter and countdown (OBS)
- `MI+0x116c` play state: 1 in play, 2 stopped (ball out, before pickup), 3 set-play ready (ball in the taker's
  hands). OBS live transitions for every throw-in.
- `MI+0x11ac` set-play elapsed counter (published from `match+0x724`): 0 at ready, starts counting **18 frames
  after ready** (OBS live), +1 per frame.
- Three clocks read this counter (OBS; `S17` = the singleton query `PTR_PTR_1434a2d40` slot 17 (+0x88),
  INF: an online/time-limit mode flag):
  - `FUN_140a61ed0` remaining = `54·15 − counter` (`FUN_140a3bdf0`), or `54·30 − counter`
    (`FUN_140a3be10`) when `!S17 && MI+0x1574 != 0` — i.e. **30 s for an ordinary offline throw-in** (stop kind 4).
  - On-screen countdown (`FUN_14044ce80`): with `MI+0x116c == 3` and stop kind ∉ {0, 9}, when
    `54·30 − counter < 54·5` (counter > 1350, 25 s) it shows the remaining time (`FUN_1408bd940` slot 1).
  - Human pads: `ThinkUnitAutoRestart` (`0x1408bbe00`) fires the restart command when `FUN_140a61ed0` < 0.
- Forced throw (OBS `FUN_140791e30`): if `MI+0x116c == 3`, `MI+0x11b3 != 0`, `S17`, and
  `54·15 − counter < −(int)(54·3+0.5) = −162` (counter > 972 frames = 18 s) → `ThrowinLoop` switches to THROWIN
  with `ap+0x2460 |= 0x2000000`; the THROWIN entry then aims 10 m straight ahead (`FUN_140a8f7c0(pos, yaw, 10)`).
  Same rule for kick-offs (`FUN_1407816b0`, which without `S17` applies only to a CPU kicker). None of these fire
  in AI-vs-AI play: the CPU throws at counter ≈ 83.

### 6.3 Whole-stop timeline (OBS live, frames after the out frame F0)

| # | taker slot | pickup start (id 29) | ready (116c=3) | carry→loop | throw start (counter) | clip | in play |
|---|---|---|---|---|---|---|---|
| 1 | 3 | +112 | +142 | loop +217 | +243 (83) | idle_short, id 28 | +261 |
| 2 | 5→2 at F0+2 | +100 | +129 | loop +173 | +230 (83) | idle_mid, id 28 | +258 |
| 3 | 15 | +120 | +150 | no loop (running throw) | +251 (83) | run_000_mid, id 28 | +288 |
| 4 | 5 | +36 | +66 | loop +104 | +167 (83) | idle_short, id 28 | +185 |
| 5 | 5 | +118 | +148 | loop +198 | +249 (83) | idle_short, id 27 | +267 |

The variable part is the taker's run to the ball (36–120 frames); everything after the pickup is fixed by
the clip lengths, the 18-frame counter delay and the 81-frame AI wait.

---

## 7. Everyone else during the stop

### 7.1 Team shape (OBS `FUN_140680980` @0x140680980, team-block width/margin solver)
While `MI+0x116c != 1` and MODE == 2 the block width is replaced by the tactics field
**`throwinWidthZ`** (tactics struct +0x344) and the touchline margin drops from 8 m (or `tactics+0x244` + 5/10 m)
to **3.0 m** (`DAT_14258c264`); MODE 3 uses `gksWidthZ`/`gklWidth` instead. The tactics field offsets were
re-verified this session from the tactics deserializer `FUN_141e79ce0` (name→store pairs):
`adjustDistX_Throwin_CB` +0x2c, `closeZRateThrowin` +0xe8, `rangeX_Throwin_CB` +0x2c8, `throwinLengthDf` +0x340,
`throwinWidthZ` +0x344 (OBS). Their per-team values are not in the VFS (`pes-re-setpiece-referee.md` §1.11) and
were not read live (open item 3).

### 7.2 Team-mates offering (OBS `match::player::ActionPassSupportThrowIn`, vtable 0x1426308B0, AI action
`PASS_SUPPORT_THROWIN` id 35)
- Update `0x140997660` → sub-mode chooser `FUN_140997e70` / `FUN_140997f60`: a supporter that has reached his
  team support point (within 2.5 m of `team+0x4870 + 12·slot`), with a taker set, and once **≥ 4 s
  (`54·4`) have passed in the stop** (`MI+0x11a4`), picks at random (`FUN_140a92530`) one movement among
  modes {1, 2, 3} other than the last one, if there is space (`FUN_140a4e720` at facing+180°, 60°/150° cones,
  5 m / 3 m). Mode 1 lasts `54·3.3`, modes 2/3 `54·3.2` frames, then back to mode 0 (hold the support point,
  re-chosen after `54·2` frames).
- Mode 1 `FUN_140997c30`: check-away — run 15 m along the team's direction (`team+0x250`) and 5 m toward his
  own side of the pitch (z), or 5.5 m in a free direction (`FUN_140a4db20`) if re-triggered within 0.7 s; target
  clipped into the field (`FUN_140a544d0`).
- Modes 0/2/3: targets from `FUN_140996f30`, `FUN_1409973f0`, `FUN_140998270` (constants 5/7/50/55/90°/100 —
  not decoded line by line).
- Movement request (`FUN_140997940`, outputs (a, b), INF(med) a = run level, b = style): mode 3 → (3, 2) when
  > 4 m from its target, (3, 3) at 2–4 m; mode 0 → (4, 2) when > 12 m (`144`), (3, 2) when > 7 m (`49`).
- The receiver's route once the ball is thrown: `PassGetRouteThrowIn` (vtable 0x142633E58; timing body
  `0x1409448d0`, constants 30, 5, 0.3, 1000/60).
- The taker's body aim while in `THROWIN_LOOP` is driven by the pad think unit
  `ThinkUnitThrowinBodyAngleRotation` (`0x1408bbef0`: active when the action byte is 0x1a → pad command 0x3d;
  stick state machine `0x1408b6ef0`, threshold 0.1).

### 7.3 Opponents
INF(med): the defending team uses the same block solver (§7.1, width `throwinWidthZ`, 3 m margin) and the
tactics fields `throwinLengthDf`, `closeZRateThrowin`, `adjustDistX_Throwin_CB` / `rangeX_Throwin_CB`;
their consumers were not found by direct displacement scans (§10 item 3). The engine has **no throw-in
distance rule for opponents** (OBS absence: no 2 m / "THROWIN distance" constant or check in any function above;
compare the 9.15 m free-kick wall).

---

## 8. Officials

Officials are AnimePlayer/player indices 22 (referee), 23, 24 (assistants): `FUN_141e53520(idx) = idx − 22`
(OBS). All three run `match::referee::RefereeActionOutofPlay` (vtable 0x142642738) during a stop:
- **Target** (`0x140a3a290`): the moving ball/carrier point `MI+0x155c` (foul stops: the fouled player).
- **Speed class** (`0x140a3a930`): while stopped (`116c == 2`): 2 when farther than 4 m (`16 = 4²`), else 1.
- **Signal**: the gesture action is `ANIME 0x5c JUDGE` for the referee and `0x5a LINESMAN` for an assistant;
  `0x140a3a870` sets its parameter to 1 (plain restart; 2/3 for fouls) and the **direction = the attack
  direction of the team awarded the throw** (`team(side)+0x254`).
- **Who signals a throw-in** (`FUN_140a3a690` @0x140a3a690, stop kind 4): the referee always; an assistant
  **only if he is on the same touchline as the restart point** (`sign(assistant.z) == sign(R.z)`). In the
  set-play-ready state the referee keeps the gesture while `MI+0x11b2` is set.
- Clip families (from `pes-re-setpiece-referee.md` §1.10): `referee_1_0/3_0_*_hand_*_point_*`,
  `assistant_3_0_090/f090_flag_*`, `assistant_flag_up_0_*`.
No ball boy: OBS live, the ball is never teleported — the taker walks to wherever it stopped (§3). The
`ball_washer` prop family (`pes-re-setpiece-referee.md` §5.4) plays no part in the throw-in code read here.

---

## 9. Foul throws

**PES 2021 has no foul throw** (OBS + INF high):
- no foul-throw token anywhere in the image (searched `foul.?throw`, `illegal`: only `IllegalOperationError`);
  `FOUL_*` tokens are `FOUL_ADV/RECEIVED/RECEIVED_ADV/RED/RESTART_END`;
- the release path cannot produce one: `ThrowinNormal::Update` phase 1 pushes the taker so the outer hand stays
  ≥ `F[1] + 0.1 m` outside until release (§4.2); the taker stands 0.7 m outside (§3.3); both feet are animation-
  driven from clips authored behind the line; the running throw is only allowed outside the line facing within
  90° of the pitch (§3.2);
- the throw-in judge path (`FUN_14050e320` → `FUN_14050e770`) has no re-award branch.

---

## 10. Constants

### 10.1 `constant::Throwin` (`throwin.o`, constant id 0x19) — layout re-derived (OBS deserializer `0x141e6ff10`)
and values read from the live object (identical to the shipped blob, OBS):

| obj off | field | value |
|---|---|---|
| +0x00 | vtable `0x142b55b48` | — |
| +0x08 | `longThrowSupport.adjustRate` | 0.5 |
| +0x0c | `longThrowSupport.enable` (u8) | 1 |
| +0x10 | `mode` | 0 |
| +0x14 | `move` | 1 |
| +0x18 | `move.beginAngle` | 80.0 |
| +0x1c | `seamlessThrowin.possibleDist` | 3.0 |
| +0x20 | `seamlessThrowin.dist` | 5.0 |
| +0x24 | `(seamlessThrowin.)touchLineDist` | 3.0 |
| +0x28 | `search.distFarLongPara` | 32.0 |
| +0x2c | `search.distFarNormal` | 25.0 |
| +0x30 | `search.distMiddle` | 15.0 |
| +0x34 | `search.distNear` | 5.0 |
| +0x38 | `speed.gageMiddle` | 0.6 |
| +0x3c | `speed.adjustDistMax` | 20.0 |
| +0x40 | `speed.adjustDistMin` | 10.0 |
| +0x44 | `speed.adjustSpeed` | 10.0 |
| +0x48 | `speed.baseMaxKph` | 50.0 |
| +0x4c | `speed.baseMinKph` | 35.0 |

**Correction to `pes-re-setpiece-referee.md` §1.3**: that table is shifted (it puts `beginAngle`/`move` at
+0x00/+0x04, which is the vtable) and its values are mis-assigned (`distFarLongPara` is 32, not 80;
`baseMaxKph`/`baseMinKph` are 50/35, not 32/25; `seamlessThrowin.possibleDist` is 3, `dist` 5).

### 10.2 `constant::BallplayerSetplay` (`ballplayerSetplay.o`, id 0x25) — OBS deserializer `0x141e6fbd0` + live
`FreekickShoot.range_frontZ` 12, `.range_max` 40, `.range_middle` 30, `.range_short` 23,
**`Throwin.range_width` 50, `Throwin.range_long` 35, `Throwin.range_min` 2, `Throwin.range_short` 25**,
`waitTimerMin` 0.5.

### 10.3 Image constants used above (OBS `.rdata`)
0.25 `0x142592228` (R outside line) · 0.7 `0x14257a194` (stand line) · 2.0 `0x142565604` (spot ±x) · 0.65
`0x14259d5d0` (carry stop) · 0.5 `0x142565600` · 1.0 `0x14256ae1c` · 5.0 `0x14259222c` · 13 `0x1425c5948` ·
25 `0x14259be20` · 15 `0x14259be0c` · 65° `0x14259be48` · 0.12 `0x1425cb294` · 0.42/1.4/0.15
`0x14259d5cc`/`0x14259d5d8`/`0x1425c48a0` · 0.1 `0x142588200` · 3.0 `0x14258c264` · 1.5 `0x14259bddc` ·
30/37 `0x142591140`/`0x1425cd82c` · 80/100 `0x142596e6c`/`0x14253ee30` · 12/0.125 `0x14259be04`/`0x14259bdcc` ·
3.5/4.0 `0x14259bde8`/`0x1425656c0` · 16 km/h `0x14259d638` (16000 m/h) · 220 `0x1425ca310`.

---

## 11. What GF must reproduce (choreography spec, derived from §1–§10)

1. Out detection: ball wholly over `|z| > halfWidth` with `|x| ≤ halfLength` → stop (play state 2); throwing team
   = not the last toucher; R = (x_cross clamped to ±(halfLength−1), 0, ±(halfWidth+0.25)).
2. Taker: if the ball is slow (≤ 12 km/h, ≤ 1.5 m high) and a team-mate can get to it in < 4 s → the fastest
   arrival among the 4 best-ranked, with an 18-frame bias to rank 0; else same-flank full-back with a −20 m
   bonus (DMF/AMF +10 m, CB/GK/FW excluded except as fallback).
3. No ball boy: the taker runs to the ball, picks it up with `throwin_pickupball_{000,090,180}` (angle class,
   ≤ 5 m reach), play state → 3 ("ready") at the pickup, carries it one-handed (`throwin_run_3_3_000_onehand`) to
   S = (R.x ± 2 m, halfWidth + 0.7), lifts it (`throwin_seamless_liftupball`, 14 frames) turning to face straight
   into the pitch, then idles in the group-94 aim loop (blend 0.5 ± 0.5·aim/80°, ease 0.12/frame).
4. AI wait: set-play counter starts 18 frames after ready; the CPU taker throws when the counter reaches ≈ 81
   (+2 latency) if it has a receiver (2–25 m, safe course), else holds up to 3 s and throws to the first
   unmarked team-mate or blind.
5. Release clip by target distance (≤ 13 short, ≤ 25 mid, > 25 long; run variants straight from the carry);
   release at the clip's event (18/27/99 frames; run 27/36); hands kept ≥ 0.1 m outside the line.
6. Ball: target = receiver lead point (54 or 108 frames ahead) clamped to 30 m (37 m Long Throw); flight time
   from the 35/50 km/h (+10) speed band, 80 % / 100 % (Long Throw) toward the fast end beyond 12 m.
7. Team shape while stopped: block width = `throwinWidthZ`, 3 m touchline margin; supporters start
   check-away / come-short runs after 4 s; officials walk to the ball point and the near-side assistant +
   referee signal the direction.
8. No foul throws; no opponent distance rule; set-play clock 30 s offline (countdown shown in the last 5 s),
   15 s and a forced throw after 18 s only in the `S17`-gated mode.

Labels for GF code: items 1–6 rule-faithful/engine behaviour (OBS); the 18-frame counter delay, the 81-frame
wait, the 13/25 m clip thresholds and the 0.7 m stand line are PES game-specific abstractions (OBS values).

---

## 12. Open items and what was tried

1. **Slot → role table** (`team+0x4bd4`, values 0..9) was not dumped live, so the role-name mapping in §2.1 is
   INF(med). Tried: reading the team struct live at `*(G+(t+0xa1)*0x28)+0x4bd4` — the match had just ended
   (`G` fields zero), retry in the next match.
2. **Ball launch velocity/spin at release** not yet measured: the live ball locator had gone stale for the first
   recording session (`<scratch>/pes_ball_addr` pointed at a non-ball object). `tiwatch2.py` (per-frame recorder with
   re-acquire) was started for the next match; see §13 if it produced data. The ball solver that turns
   `{kind 0x10, target, frames, req+0xdc}` into a velocity was not decompiled; `req+0xdc`'s meaning is INF(low).
3. **Tactics throw-in values and consumers.** Offsets verified (§7.1); only `throwinWidthZ` has a located
   consumer. Tried: displacement scans for `+0x340/+0x2c8/+0xe8/+0x2c` loads in 0x140400000–0x141000000 (noise from
   unrelated structs; no consumer in throw-in context). Values are per-team runtime data (not in the VFS).
4. **Which stop program ran** (seamless step 0x33 vs. the whistle program) was not logged live (the Flow object
   pointer `match+0x38` is not yet located from `G`). All 5 observed throw-ins used `SEAMLESS_THROWIN` for the
   pickup, which both programs can reach (the kicker dispatch `FUN_14092e7e0` → `ThrowinLoop::SetKick` is the
   ceremony placement); INF(med) that slow balls take step 0x33.
5. **Protected code**: the AI's no-receiver fallback `thunk_EXT_FUN_15b6efa70`, the foul-award
   `thunk_EXT_FUN_143f546f0`, the flow notifier `thunk_EXT_FUN_144232210` and `SeamlessThrowin` slot 23
   (`0x140794340` → `.impdata`) were not read (outside the dump; obfuscated thunks).
6. `PassSupportThrowIn` modes 0/2/3 target bodies (`FUN_140996f30`, `FUN_1409973f0`, `FUN_140998270`) and
   `PassGetRouteThrowIn` were only skimmed.
7. The forced-throw singleton (`PTR_PTR_1434a2d40` slot 17, `+0x88`) was not identified (INF: online / human
   involvement flag).

---

## 13. Ledger

| ID | Claim | Conf. | Evidence |
|---|---|---|---|
| EV-TI-0001 | Touchline out = judge event 10 with `|x| ≤ F[0]`, `|z| > F[1]` → judge+0xcc = 3 → `FUN_14050e320` | Confirmed | decompile 0x14050e4a0, 0x14050b490 |
| EV-TI-0002 | Throwing team = not the last toucher (touch history) | Confirmed | 0x14050e320 |
| EV-TI-0003 | R = (x, 0, ±(F[1]+0.25)), `|x| ≤ F[0]−1` | Confirmed | 0x14050d280, 0x14050e770; live R.z = ±34.31 |
| EV-TI-0004 | Restart block MI+0x1554 {MODE, side, display pt, R, stopKind 4, lastToucher}; taker MI+0x173c | Confirmed | 0x140a3c560, 0x140a3cd10, 0x140a3cae0, live |
| EV-TI-0005 | Ceremony taker rule (role-biased distance, SB −20 m, DMF/AMF +10 m, flank check) | Confirmed (code) / Medium (role names) | 0x1404a93a0 |
| EV-TI-0006 | Seamless candidate = fastest arrival of ranks 0–3 with 18-frame bias | Confirmed | 0x14044e230, 0x14044b8a0 |
| EV-TI-0007 | Seamless eligibility: ≤ 12 km/h, ≤ 1.5 m, ≤ 20 m, arrival < 216 frames | Confirmed | 0x140444f00 |
| EV-TI-0008 | SeamlessThrowin phases 0–5, spot = (R.x±2, F[1]+0.7), pickup/carry/lift clips | Confirmed | 0x140791ec0; live clips 0xcf5–0xcf7, 0xd00, 0xd01 |
| EV-TI-0009 | Play state 2→3 at pickup (ball held) | Confirmed | live (2 frames after phase 3), 0x140791ec0 phase 3 |
| EV-TI-0010 | ThrowinLoop aim loop: group 94 blend 0.5+0.5·aim/80°, ease 0.12; side-step/turn branches | Confirmed | 0x140793590 |
| EV-TI-0011 | Release clip by distance 13/25 m; rate 0.75–1.05 for short; release frames 18/27/99/27/36 | Confirmed | 0x140792e10 + live motion table + live in-play timing |
| EV-TI-0012 | Hands forced ≥ F[1]+0.1 outside before release → no foul throws | Confirmed (code) / High (no-foul conclusion) | 0x140792e10; string search |
| EV-TI-0013 | Throw request: kind 0x10, flight frames from 35/50(+10) km/h band, Long Throw 80→100 % | Confirmed | 0x14081c640 |
| EV-TI-0014 | Reach 30 m / 37 m (Long Throw, skill bit 19); free-aim distance 5/15/25(32) by gauge | Confirmed (code) / High (bit 19 = Long Throw) | 0x1409bb450, 0x1409bb360 |
| EV-TI-0015 | AI throws when the set-play counter ≈ 81 (+2) with a receiver; receiver 2–25 m + course test | Confirmed | 0x142137460, 0x142135700; live counter = 83 ×5 |
| EV-TI-0016 | Set-play counter starts 18 frames after ready; 30 s offline clock (15 s with S17), countdown in the last 5 s, forced throw at 18 s (S17 only) | Confirmed (code, live start offset) / Tentative (S17 meaning) | 0x140a61ed0, 0x14044ce80, 0x140791e30; live |
| EV-TI-0017 | Throw-in block width = throwinWidthZ, 3 m margin | Confirmed | 0x140680980 |
| EV-TI-0018 | Officials walk to the ball point; referee + same-side assistant signal the awarded direction | Confirmed | 0x140a3a290/930/870, 0x140a3a690 |
| EV-TI-0019 | throwin.o layout/values (supersedes setpiece-referee §1.3) | Confirmed | deserializer 0x141e6ff10, live object, shipped blob |
