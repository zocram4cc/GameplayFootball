# PES 2021 set-piece taker: selection, preparation, run-up, delivery, wall/teammate reaction (RE report, SetpieceKickerRE)

Read-only RE. Sources: Ghidra PESLive decompiles via `dec.sh` (scratch `~/ghidra-projects/scratch/Unpacker/out/spk1..6.c`),
capstone over `<scratch>/pesmem.bin` (raw checks), read-only `pread` of the running AI-vs-AI match (`kwatch.py`, an extension of
`SetPieceChoreoRE/spwatch.py` that also logs the taker's request block `ap+0x2bb0..0x2d1f` and the ball trajectory
`*(G+0x40)+0x88`), and the earlier captures `SetPieceChoreoRE/live.jsonl`, `live_match1.jsonl`.
Scratch: the SetpieceKickerRE scratch dir (`kwatch.py`, `ana.py`, `onset.py`, `launch.py`, `live2.jsonl`,
`fk_runup.json`, `strike.json`). No player/team names; slots 0..21. Frames = 54 Hz logic frames.

Tags: **OBS** = read in the decompile/raw bytes at the named address, or observed live. **INF(conf)** = inference.
Conventions as in `pes-re-setpiece-choreography.md` (G, MI, F[], ap, MODE). `dir` = attack sign of the restarting team
(`T+0x254`). Role byte (`T+0xc48+0x3f8c+4·slot`): INF(med) 0 GK, 1 CB, 2 SB, 3 DMF, 4 CMF, 5 SMF, 6 AMF, 7 SS, 8 WF, 9 CF
(grouping `FUN_140a40ca0`: {1,2} {3,4} {5,6} {7,8,9}; kick-off penalises 5 and 8 as "wide").

---

## 1. Taker selection by kind

Entry `FUN_1404a7d60` (step 0x13; table in `pes-re-setpiece-choreography.md` §2). New here: the fallbacks and the
rating they use.

### 1.1 Designated takers and the rating fallback (OBS)

- Designated slot: `FUN_1408c5080(T+0x8e08, kind)` = `u32 [T+0x8e08+0x143c+4·kind]` for kind < 6, else 0xff; mapped to a
  pitch slot by `FUN_140a3b000` (linear search of a 40-entry id list). Must be on the pitch (`MI+0x1748[idx]`).
- **Rating fallback** `FUN_140a621f0(AI, kind, side, exclude)` (OBS): over the 11 slots on the pitch, ≠ `exclude`,
  `FUN_140a3b520(player)` true (INF: valid/registered player), with role filters:
  - kinds 3/4 (corners): role ≠ 0 (GK) and ≠ 9 (CF);
  - kinds 1, 2, 5: role ≠ 0 (GK);
  - other kinds: anyone.
  Pick the **highest `u8 playerdata+0x96+kind`** (`FUN_1408c75a0`), first wins on ties. INF(high): six per-player set-piece
  suitability bytes (0 FK long, 1 FK short, 2 FK 2nd, 3 CK left, 4 CK right, 5 PK). Who computes +0x96..+0x9b is **Unknown**
  (not traced; find the writer of `playerdata+0x96`).
- Online-only path (`G+0x400` and `FUN_141f9c3b0`) uses a formation-ordered list `FUN_140a40ca0`; not used offline.

### 1.2 Per kind (OBS decompile; constants raw-read)

| MODE | order of choice |
|---|---|
| 4 corner (`FUN_1404a8620`) | kind = 4 if `dir·R.z > 0` else 3 (INF high: 4 = right-side corner seen from the attack, 3 = left) → designated → rating fallback (no GK, no CF) |
| 6 penalty (`FUN_1404a90d0`) | designated kind 5 → rating fallback kind 5 (no GK). **Correction** to choreography §2: the `FUN_140a40ca0` list is the online path only |
| 3 goal kick | the slot with role 0 (keeper) → first on-pitch entry of `T+0x8928[]` |
| 5 free kick (`FUN_1404a8ad0`) | `d = |R − (dir·F[0], 0)|`. 1) designated kind 1 if `d < 32` (`0x14259be28`), kind 0 if `d < 60` (`0x14259a764`); 2) if set-play ready (`MI+0x116c == 3`) the current `MI+0x173c`; 3) **keeper** (`FUN_140a62690` → first role-0 slot) when `|R.z| < F[7]` and (distance to the **own** goal `< 28` (`0x1425a2004`) or `dir·R.x < −F[6]`); 4) own half (`dir·R.x < 0`): nearest role 1–2 (CB/SB) to R within 65 m (`0x14259be48`), excluding `T+0x4b40`; 5) `d > 60`: nearest role < 7 within 65 m; 6) rating fallback (kind as chosen, else 0) |
| 5, second kicker | designated kind 2, ≠ kicker, only when the FK type `Flow+0x2ab4 ∈ {1,2,3}` |
| 1/7 kick-off | score sort (choreography §2) |

Live (OBS): every corner and FK of one team in a match went to the same slot; all goal kicks to the keeper. New
capture (`live2.jsonl`): an FK at R = (30.77, −1.47) for the team defending +x (21.8 m from its own goal, `|z|` < F[7])
was taken by **that team's keeper** (slot 11, standing at x = 46.4 before the stoppage) with a 45.6 m long ball — rule 3.

### 1.3 What the kicker "prepares with" (OBS)

Clips are chosen by motion-table **row**, then remapped per player:
- `FUN_1406b3560(styleTbl, playerdata+0xe8, row, k)` (OBS `FUN_1406ea7b0` → `+0xe8`; INF high: signature-motion id) —
  table k = 1 kick (35 entries), 2 free kick (23), 4 penalty (16), 5 (9), 0 (8), 3 (15). Plus three hard-coded overrides
  for two player ids (0x20da3, 0x20d30): FK row 0x4d1→0x4d4, 0x4e0→0x4e3, kick row 0x94c→0x93f.
- if unchanged, `FUN_1406b3780(styleTbl, fkStyle, row, k)` with `fkStyle = FUN_1406e81f0(ap)` = player block byte `+0xe`
  (INF: the "free-kick motion" setting), **zeroed** when `dir·P.x < 8.0` (`0x14259bdf4`) or `|P.z| > F[7] + 4.0`
  (`0x1425656c0`) or ≥ 20 (P = display restart point `MI+0x155c`), and zeroed when a second kicker exists.
- Mirror = left-footed: `FUN_1406e6660(ap, 0x34) == 1` (player block byte +0x34, INF high preferred foot).

Rows (names from `SetPieceChoreoRE/rows.txt`, OBS): FK/CK idle 0x507 `fk_idle`, step 0x508 `fk_side`, run-up 0x509
`fk_runup`, short-corner run-up 0x50a `fk_runup_pass`, 0x50b `fk_idle_sub`, 0x50c `fk_runup_sub` (after clip 0x4dd),
FK-short fallback run-up clip 0x4eb; strike rows 0x4e1 `fk_long_3_0_inside_curve`, 0x4e2 `fk_long_3_0_instep`,
0x4e3 `fk_mid_3_0_inside`, 0x4e4 `fk_mid_3_0_infront`, 0x4e5 `fk_short`, 0x4e6/0x4e7 `fk_short_3_4_inside_left/right`,
0x4e8 `fk_through`; signature strikes 0x4e9–0x4f1 (`fk_long_3_0_inside_<style>`); PK 0x8ea `pk_idle`, 0x8eb `pk_runup`,
0x8ec–0x8f1 styled, strikes 0x8f2–0x8f7.

**Waiting kicker** (`FreeKickLoop` 30 Update `0x14073e150`, OBS): every tick re-resolves the styled `fk_idle`/`fk_runup`.
Phase 0 stores the spot, plays the idle (mirror = foot), → 1 (→ 2 with a second kicker). Phase 1:
- re-spot: with a reposition request (kind 0x66) it plays `fk_side` (column 8/9 by turn side and foot) when the new spot is
  farther than `0.55 · 0.01 · |clip root|` (`0x1425a358c`, `0x14256ae18`); without one (and request ≠ 0x1f) when > 1.5 m
  (`0x14259bddc`);
- facing: hold if `|yaw error| ≤ 5°` (`0x14259222c`), else turn by `error · 0.1` per tick (`0x142588200`), and a radial
  distance correction at the same 0.1 rate when the stand distance error exceeds 0.05 m (`0x1425a8a28`);
- in play → MOVE. `CornerKickLoop` (49) only idles (`fk_idle`, mirror = foot) and leaves on in-play (fox-registry §4.1).

---

## 2. Run-up and delivery

### 2.1 Stand point / run-up (OBS code; live)

- `FreeKickLoop::SetKick` `0x1407411f0`: styled `fk_runup`; `FUN_1406e8cc0` gives the clip's root offset (mirrored by
  foot), negated; yaw = the clip's yaw at contact (`FUN_1406b1c70`) wrapped to [0,360); `FUN_140732d40` places
  `stand = R + rot(aimYaw)·(−offset)`; ball within 0.5 m of a corner pulled in 0.5 m; clamp `|x| ≤ F[0]+1.5`,
  `|z| ≤ F[1]+1.5`. Corner: same with base yaw 315°/45° by side (fox-registry §4.1).
- **Run-up length and angle are the clip's, not constants.** `fk_runup` root (repo `pesfmt_dump gani fk_runup`,
  1/16384 m units per `clip_pose.hpp`): 3.25 m straight-line to its last key, then a −45° root yaw over the last two keys;
  the strike clip `kick_long_3_0_inside_y0_045_curve_placekick` adds ~1.65 m to its contact region and turns +49°.
  Live stand distance 4.15 m (default style ×3, corners and FK) and 3.95 m (one styled kicker). INF(med): 4.15 =
  run-up + strike approach with the turn; the exact contact key of the strike clip was not identified.
- Phase 0 `FUN_140743380` (FK, OBS): row 0x509 (0x50c after clip 0x4dd), styled; if unstyled and request = 0x1f
  (FK short pass) clip 0x4eb; body yaw set to bearing(kicker→ball) ± the clip's root yaw; blend 0.2; → phase 1.

### 2.2 Touch-off timing (OBS)

Strike window = run-up clip frame ≥ `evt − ⌊fps·N/60 + 0.5⌋`: **FK N = 20 (`0x14259be1c`) → 18 frames**, corner N = 14
(`0x1425c48b8`) → 13, penalty N = 5 → 5. Then the strike clip's own kick event is the contact. Live: strike phase entered
19 frames before contact for every corner and FK (×6), 73–77-frame run-ups (OBS).

### 2.3 Strike clip choice = the delivery type

**Free kick** `FUN_140740910` (OBS decompile; row immediates raw at `0x140740ef1..0x140740f18`). Inputs: request kind
(`req+0x28`), power `p = ap+0x2bd0`, curve `c = ap+0x2d04`, `ap+0x2d0c`, target point `ap+0x2bf4/0x2bfc`, yaw offset
`a` = target bearing − body yaw (sign flipped when mirrored):

| request | row |
|---|---|
| 31/37 FK(2nd) SHORTPASS | 0x4e3 `fk_mid_3_0_inside` |
| 33/39 FK(2nd) SHOOT | 0x4e1 `fk_long_3_0_inside_curve`, or 0x4e2 `fk_long_3_0_instep` when `I` |
| 35 2ND_CEDE | 0x4e8 `fk_through` |
| 36 2ND_PASS_TO_KICKER | 0x4e5 `fk_short` (also aims body yaw ±25° / ±135° by foot match, `0x14259be20`/`0x14259be60`) |
| 32/38 LONGPASS (default; also when `ap+0x2459 == 0x18` and `ap+0x2d12 == 2`) | 0x4e4 `fk_mid_3_0_infront` when `L`, else 0x4e1/0x4e2 by `I` |

- `I` (instep) = `c > 0.4` (`0x14259b89c`) and `a > 0` and `p > 0.45` (`0x14256b140`) when the request's mirror flag
  (`req` vtable +0x30) is clear; with it set `c < −0.4` (`0x1425c7670`), `a` sign-flipped. Corners (OBS `0x140732e30`
  phase 1) key the sign on the kicker's foot instead (`FUN_1406e6660(ap,0x34) == 1` → `c > 0.4`) and need `p > 0.85`
  (`0x1425ab120`); fox-registry §4.2. INF(low): `c` is a signed curl request and `I` picks the instep drive when the
  requested curl is strong toward the open side and the kick hard; the exact semantics of `c` were not traced.
- `L` (low, infront) = `p ≤ 0` and target within 10 m (`0x142588210`); or `0 < p < 0.15` (`0x1425c48a0`), `ap+0x2d0c > 0`
  and `c < 0.75` [mirrored `c > −0.75`].
- Column = nearest populated 22.5° bucket of `a` (`FUN_1406de990`, sim-motion-mapping §); for non-short kinds the
  columns 13→11 and 4→6 are swapped.
- Corner (fox-registry §4.2): short corner (51) → 0x4e3 if the target is > 25 m away else 0x4e6/0x4e7 by whether the
  kicker stands behind the goal line; cross (50) → 0x4e4 when `L`, else 0x4e1/0x4e2 by `I`.
- Penalty: choreography §7 (power/height table).

After a successful request the kicker gets a root-motion warp onto the ball and a yaw warp; post-strike yaw correction
`clamp(±30°)` (`0x142591140`/`0x14259be88`) toward the target for kick sub-types 2/6/11/15, using 135° or 45° factors
(`0x14259be60`/`0x14259be30`); not for FK short passes (OBS `0x1407423e0` / `0x14073dfc0`).

### 2.4 Aim rules = the AI command (OBS `ThinkUnitSetPlay@bp@ai` think `FUN_142135c50`)

Command record `param_2`: `[0]` kind (1 shoot, 2 pass, 3, 4, 10, 11), `[2]` receiver or shot type, `[3..5]` target or
pass type. Timers belong to SetplayWaitRE (agreed split). Aim/target rules:

- **Corner (MODE 4)** `FUN_142134c20`: candidates = attacking outfield slots with set-play state 11
  (`FUN_142118d20` table `+0x3c+4·slot`, INF: "ready to attack"), within 46 m of the kicker (`0x1425cb478`) and within 20 m
  of the goal centre `(dir·F[0],0)` inside ±60° of the goal's outward normal (`FUN_140a53da0`, half of 120°
  `0x14256ae30`, 20 `0x14259be1c`). Score = `int(dist to the nearest opponent) − int(dist to the penalty spot
  (dir·F[8], 0)) + 10`, highest > 0 wins. None and waited ≥ 3·fps → the 1st/2nd nearest player to the penalty spot
  (`FUN_140a4ee40`). Command 2 (pass), type 1 (lofted); emitted only after `elapsed ≥ fps·3.5` (`0x14259bde8`, 189
  frames — live CORNERKICK at counters 191/191/380). Short-corner branch first (OBS): a roll `(+8 field) % 100 < 50` when
  `*(param_3)+0x3c == 1` else `< 10` (INF: team short-corner tactic); while `elapsed < fps·2` (`fps·7` once a team-mate's
  `+0x5d0 == 0x2d`) the partner from the team table `+0x2470c/+0x24714+0x58·side` gets a ground pass (type 0) if within
  30 m (`900` m²) and free by `clamp(0.5·k, 2, 6)` m (`0x142565604`/`0x14259bdec`); with no such team-mate, command 10.
- **Free kick (MODE 5)**: shoot vs pass from `ballplayerSetplay.o` (id 0x25: range_frontZ 12, range_max 40,
  range_middle 30, range_short 23; values via SetplayWaitRE), evaluated once `elapsed ≥ fps·3` (`0x14258c264`, 162
  frames; live FREEKICK_LONGPASS at counter 164 ×2) and after ≥ 2 think ticks. With `d` = distance to goal:
  `d < 23` → shoot; `23 ≤ d < 30` → shoot if in the frontal window (`dir·x ∈ (F[0]−40, F[0])`, `|z| < 12`) or `S`;
  `30 ≤ d < 40` → shoot if `S`; `d ≥ 40` → pass. `S` (long-range shooter) = `d > 30` (`900` = 30², `0x1425c65f4`), skill
  8 KNUCKLE_SHOT, the frame-random byte `*(G+0x360)+4 < 40`, and a team gate. Outside the window a player with skill 7
  LONG_RANGE_DRIVE or stat 0x1a ≥ 85 or stat 0x21 ≥ 80 may still shoot (shot type 3). No shot for an indirect FK
  (`FUN_140a63940(MI+0x1588)`). Two-man FK: 20 % gate `(MI+0x1198 − MI+0x11a4) % 100 < 20` → command 4/0xc (INF: tap to
  the second kicker).
  **Shot target**: `x = dir·F[0]`, height `2·F[3]/3` (1.627 m), `z = F[2]·k·s` with `s = sign(keeper z)`,
  `k = −0.7` (`0x1425a967c`), `−0.5` (`0x14258d694`) for `S` → **far side from the keeper**, 2.56 m (1.83 m) off centre.
  Shot type 1 normal, 3 (stat/skill branch), 4 (`S`).
  Pass branch (OBS structure, pass-type meaning INF med: 0 ground, 1 lofted, 2 high/long): if `|ball.z| ≤ F[7] − 3` or
  `dir·x ≤ F[0]/2` → first a ground pass (type 0) to a team-mate both of whose nearest markers are ≥ 15 m away (225 m²,
  `0x14259be70`; only when `*(param_3)+0x3c == 1` and `MI+0x14b8 ≥ 32`), else `FUN_142135230` with type 1 when
  `|ball.z| ≤ F[1]/2` or `dir·x ≤ F[6]/2`, type 2 otherwise; wide in the final third → `FUN_142134fe0` (INF: nearest free
  team-mate within 20 m `0x14259be1c` inside ±60° `0x14259a764` of the attack) type 2. No receiver → hold, then fallbacks.
- **Penalty (MODE 6)** `FUN_142134a40` (raw: `% 7` via magic 0x92492493; 0.13042313 = 1.2·ball radius): after fps
  frames, command 1 to one of 7 points chosen by `*(int*)*(G+0x360) % 7` (frame-random int, 0..32767, OBS live) —
  `x = sign(ball.x)·F[0]`; `zs = (F[2] − r)·0.75` = 2.66 m, `hh = (F[3] − r)·0.75` = 1.75 m, `hm = F[3]/2` = 1.22 m,
  `hl = 1.2r` = 0.13 m (r = 0.10869 `0x142598958`):
  0 (+zs, hh), 1 (+zs, hm), 2 (+zs, hl), 3 (0, hm), 4 (−zs, hh), 5 (−zs, hm), 6 (−zs, hl).
  **Centre-low and centre-high are never chosen.** A training/user override (`G+0x3b0`) replaces z/h by bit flags.
- **Goal kick (MODE 3)**: short (`FUN_142132660`, ground, type 0) in the 0.5 s window, else `FUN_142135230` lofted, else
  no receiver (0xff) lofted. Live (OBS) request at contact: target point `ap+0x2bf4` = a teammate spot 45 m away with
  receive height `ap+0x2bf8 = 1.5`.

### 2.5 What the touch transmits

Launch formula: `pes-re-kick-launch.md` §2 (one shared writer). Per-kind spin-tilt case (byte table `0x140732d20`,
index = ANIME id − 0x20; OBS there): **32 FK long / 38 FK2nd long / 50 corner cross → case 0** (XZ 4.0, Y 4.5),
**33/39 FK shot → case 1** (XZ 2.5, Y 3.5, Y clamp 0.04–0.15), **63 goal kick → case 2** (XZ 2.0, Y 0.02), all others
none. Measured launches: §4.

---

## 3. Wall and teammate reaction

### 3.1 Retreat distances (OBS live; rule INF high)

| restart | nearest opponent to R over the whole wait | rule |
|---|---|---|
| FK ×2 (35 m, 40 m) | **9.150 / 9.150 m** (the wall man) and 9.15 m for a second defender at ready (later 9.78 m) | ring of `F[17] = 9.15 m` at placement; nobody inside it during the wait |
| FK own half (new; `d` > 40 → wall n = 0) | 18.6 m | nobody needs pushing |
| corner ×7 (2 short) | 18.98–27.0 m, except one long cross where two defenders stood at 9.71 / 9.91 m (INF: short-option guards; still ≥ F[17]) | no ring binding seen |
| goal kick ×4 | 11.7–15.4 m, opponents at |x| = 34.1–34.7 (1.5–2.1 m outside the box line F[6]) | opponents outside the penalty area |
| kick-off | 10.15 m = F[17] + 1 (choreography §3.1) | disc + 1 m |

Wall placement and size: choreography §5.2/§5.3 (9.15 m toward the goal centre or 1.3 m outside the near post).

### 3.2 Wall behaviour (OBS)

- Only `WALL_JUMP` (45) was observed (×2), requested ~2 frames before contact, random 1–5-frame start (choreography §5.6).
  `WALL_RUN_OUT` (47, the charge) and `WALL_NOT_JUMP` (46) never ran in AI-vs-AI.
- The user-pad think units `ThinkUnitFreekickWallJump/NotJump/Press` (slot 12 `0x1408aef00/10/20`) all return pad
  command 0x31 with sub-type 0/1/2 (jump / not jump / press) — INF(high): "press" = the charge, issued by a human pad.
  The AI requester of 45–48 was not found (only `WallLoop` re-arming 44 calls `FUN_1406eebb0` with an immediate).
  Lead (INF low): the pad/AI command dispatcher at `0x14207b660` stores command kinds 0x30 / 0x2f / 0x33 into
  `[obj+0x28]` (`0x14207b6c5`, `0x14207b8d7`, `0x14207bbb3`, `0x14207bd28`); whether these are ANIME ids 48/47/51 was not
  proven.
- After the kick a wall man returns to MOVE once the last ball event is ≥ 21 frames (kick) old (choreography §5.6).

### 3.3 Run timing relative to the touch (OBS live, sustained > 4 m/s for 5 frames, `onset.py`)

| restart | attackers' run onsets (frames rel. contact) | defenders |
|---|---|---|
| corner #1 | −26, −22, −5, −5, 0 | +3 … +64 |
| corner #2 | −27, −20, −18, −9, −1 | −5, −2, 0, +20 … +57 |
| corner #3 | −15, −14, −14, −9 | −11, +1 … +32 |
| corner #4 | −23, −18, −4 | −9, +12 … +39 |
| FK 35 m wide | −12, −12, −11, −1 | −6, +9 … +64 |
| FK 40 m | −48, then +14 … +40 | −54, −46, +2 … +84 |
| goal kick ×3 | −11 / −6 / −4, then +18 … +84 | +27 … +69 |
| FK own half, keeper long ball (new) | none before contact; +42, +74 | +43 … +62 |
| short corner ×2 (new) | none sustained before contact (−117/−118 = repositioning) | +22 … +83 |
| corner #5 long cross (new) | −27, −21, −19, −18 | −3, +1, +6, +37 … +52 |

- Attackers start their runs **after the kicker enters the strike phase (−19)**, spread −27 … 0 (mean ≈ −13 frames
  ≈ 0.24 s before contact). INF(med): triggered by the kicker's strike phase / kick prediction, not by a timer.
  Box attackers wait onside (FK: 1.14 m behind the line, choreography §5.1).
- Defenders move after the touch (typically +10 … +40 frames), except 1–2 already adjusting.

---

## 4. Live launches (OBS, `launch.py`, ball from `*(G+0x40)+0x88` 40-byte trajectory entries, v = Δpos·54)

| restart | request block at contact−1 (nonzero) | launch |
|---|---|---|
| goal kick (long, `KP_GOALKICK` 63) | target `0x2bf4` (3.27, 1.5, 4.18) (45 m), `0x2d04` 0.1, `0x2d0c` −0.45, `0x2c04` −1 | 26.19 m/s (94 km/h), 19.8° up, decaying 0.14 m/s per frame |
| FK, own half, taken by the keeper (`FREEKICK_LONGPASS` 32, run-up at counter 163, strike phase −19) | target `0x2bf4` (−13.51, 1.5, −12.51) (45.6 m), second point `0x2c00` (−13.81, 1.5, −10.12), `0x2d04` 0.5 | 26.08 m/s (93.9 km/h), 21.0° up, lateral −8.63 m/s |
| short corner (`CORNERKICK_SHORT` 51, run-up clip 1259 — INF the `fk_runup_pass` row 0x50a, the long-corner run-up is clip 1248 — at counter 57; strike phase −15) | target `0x2bf4` (36.85, 0, −15.80) = 23.2 m (< 25 → `fk_short_3_4_inside_*`) | 18.17 m/s (65.4 km/h) along the ground (vy 0.17) |
| short corner #2 (same, counter 78) | target (38.28, 0, 16.72) = 21.6 m | 16.83 m/s (60.6 km/h) along the ground |
| long corner cross (`CORNERKICK` 50, run-up at counter 379 = 2·189 + 1, strike clip 2380 `kick_long_3_0_inside_y0_045_curve_placekick`) | aim yaw `0x2bc8` 348.37°, height tier `0x2bcc` 0.5, **power `0x2bd0` 0.636**, target `0x2bf4` (41.68, 2.2, 3.28) = on the penalty-spot line (F[8] = 41.56), receive height 2.2 m, curve `0x2d04` 0.5, `0x2d0c` −0.45 | **26.78 m/s (96.4 km/h), 17.2° up**, 38.0 m to target |

Receive height `ap+0x2bf8`: 2.2 m on the corner cross (header), 1.5 m on the long FK / goal kick, 0 on ground passes.
Power `ap+0x2bd0` = 0.636 on the cross (< 0.85 → row 0x4e1 inside-curve, matching the clip, §2.3), 0 at contact−1 on
the other kinds (INF: those kinds' pass planner leaves it 0; the FK row rule then takes `L`/0x4e1 by target distance).

---

## 5. Killed hypotheses

- "Penalty fallback = the formation-ordered list" (choreography §2): that list is online-only; offline it is the
  rating fallback kind 5.
- "AI kicks a penalty to a uniformly random point": 7 fixed points, never centre-low/high.
- "FK shot aims at the keeper's side / goal centre": far side from the keeper, 0.7·F[2].
- "Run-up is a constant 4.15 m": clip-derived (3.95 m for a styled kicker).
- "Attackers run on a timer from the whistle": they start inside the strike window, ~0.24 s before contact.
- "Defenders retreat 10 m at FKs": 9.15 m (F[17]) ring; 10.15 m is kick-off only.
- "The AI wall charges": never observed; the charge is a pad (human) command.
- "The short corner is picked by a frame-random multiple of 3.5 s" (peer suggestion): `0x141e532e0` is
  `mov eax,[0x14351ae50]; ret`, and `0x14351ae50` is int fps (static 0x36, live 54 ×5). The 3.5 gate is fps·3.5 = 189,
  the long-corner gate. The short corner comes from the %100 roll plus `0 < elapsed < (int)(fps·2.0 | 7.0)`
  (raw `0x142135fdc..0x142136004`) plus the partner checks. The 57 vs 78 difference is when the partner test first
  passes (INF med).
- "`ballplayerSetplay.waitTimerMin` (+0x28, 0.5 s) drives the kick timing": the gates are `.rdata` fps·k (3.5 corner,
  3.0 FK, 1 PK), and waitTimerMin is not read on these paths. Do not port it.

## 6. Open

1. Writer of the set-piece ratings `playerdata+0x96..0x9b` (and their attribute formula).
2. AI requester of `WALL_JUMP/NOT_JUMP/RUN_OUT`.
3. The pass planner that fills `ap+0x2bc8..0x2d0c` (aim yaw, height tier, power, target, receive height, curve) from the
   AI command — one cross sample observed (§4); the writers `0x142081190`, `0x142083930` (per SetplayWaitRE) not decoded.
4. Strike-clip contact key → exact 4.15 m geometry.
5. Stat labels 0x1a / 0x21 (INF Finishing / ?).

## 7. Ledger

| ID | Claim | Conf. | Evidence |
|---|---|---|---|
| EV-SPK-0001 | Rating fallback = max `playerdata+0x96+kind`, GK excluded for kinds 1–5 (CF also for corners) | High (code) | `FUN_140a621f0`, `FUN_1408c75a0` |
| EV-SPK-0002 | FK taker chain (designated 1/0 by 32/60 m → ready → keeper near own goal → CB/SB own half → nearest non-forward → rating) | High (code); keeper step Confirmed (live ×1) | `FUN_1404a8ad0` + raw constants; `live2.jsonl` FK at 21.8 m from own goal |
| EV-SPK-0003 | FK strike row table | Confirmed (raw immediates) / High (case mapping) | `FUN_140740910`, raw `0x140740ef1..f18` |
| EV-SPK-0004 | Strike lead FK 18 / CK 13 / PK 5 frames | Confirmed | constants + live 19-frame strike phase |
| EV-SPK-0005 | Corner target scoring (free space − distance to spot + 10, 46 m, 20 m/±60°) | High | `FUN_142134c20` + helpers |
| EV-SPK-0006 | FK shoot gates 23/30/40 m, shot target far side 0.7·F[2] at 2·F[3]/3 | High | `FUN_142135c50` |
| EV-SPK-0007 | PK target = one of 7 points by frame-random % 7 | Confirmed (raw) | `FUN_142134a40` |
| EV-SPK-0008 | FK opponents ≥ 9.15 m; goal kick opponents outside the box | Confirmed (live ×2/×3) | `ana.py` |
| EV-SPK-0009 | Attackers' runs start −27…0 frames before contact on crosses (corners ×5, FK ×1); none before a short corner (×2) or a keeper's long FK | Confirmed (live) | `onset.py` |
| EV-SPK-0010 | Styles: signature (`+0xe8`) then FK-style byte `+0xe` zeroed outside the attacking central zone | High | `FUN_1406b3560/3780`, `FUN_1406e81f0` |
| EV-SPK-0011 | Launches: long FK / goal kick 26.1–26.2 m/s at 20–21°; corner cross 26.8 m/s at 17.2° (power 0.636, receive height 2.2 m, target on the penalty-spot line); short corners 16.8–18.2 m/s on the ground | Confirmed (live ×5) | `launch.py`, `*(G+0x40)+0x88` |
| EV-SPK-0012 | Short corner = AI ground pass from the short-corner branch (live run-ups at counters 57 and 78, inside its `fps·2` window) → `CORNERKICK_SHORT` (51); targets 21.6/23.2 m < 25 → short strike row. Long cross gated at `fps·3.5` (new live: counter 379 = 2·189 + 1) | Confirmed (live ×2 short, ×1 long, action + launch) / INF high (branch attribution) | `FUN_142135c50`, `live2.jsonl` |
