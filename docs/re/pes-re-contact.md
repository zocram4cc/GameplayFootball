# PES 2021: player-vs-player contact, balance, falls and aerial duels (RE report, ContactRE)

Companion of `pes-re-duels-fouls.md` (the foul Judge that scores a contact), `pes-re-defender-decisions.md`
(who chooses to tackle), `pes-re-collision.md` (capsules, contact records) and `pes-re-anim-select.md` (clips).
Read-only RE. Tags: **OBS** = read in raw instructions / decompile re-checked against disassembly at the named
VA, or read live; **INF(conf)** = inferred. VAs are PES2021.exe (base 0x140000000). Decompiles: headless
`dec.sh` (PESLive project), constants annotated from the live image dump; every quoted formula below was
re-read in raw disassembly where the decompiler dropped a return value or a float.

Scratch: the ContactRE scratch dir — `out/*.c` (decompiles), `out/*.s`, `duel_watch.py` (read-only live
sampler), `tune.py` (live tuning-record reader).

Notation (OBS, raw `0x140a48910/0x140a48a10/0x140a489f0`):
- `G = *(0x1436F3DC8)`; `human(i) = *(G+(i+0xa5)*0x28)` (published anime state, the record the Judge reads);
  `pl(i) = *(G+(i+0xbf)*0x28)` (match player: pos `+0x55c/+0x564`, heading deg `+0x554`, vel/frame
  `+0x574/+0x57c`, AI action `+0x5d0`, AnimePlayer `+0x7d8`); `ai(i) = *(G+(i+0xf3)*0x28)`, ability block
  `ab(i) = ai(i)+0x70` (`FUN_14124dad0` = `lea rax,[rcx+0x70]`).
- `ap` = AnimePlayer (`pl+0x7d8`); `ap+0x2458` id; `stat(ap,k)` = `FUN_1406e6660(ap,k)` = **byte `ab(id)+k`**
  (OBS raw: it is the same block, not a separate "live" copy — `pes-re-duels-fouls.md` §1.2 calls one of the two
  reads "live"; both read `ab+k`).
- Ability bytes used here (index map `pes-re-matchlogic.md` §1.3): `+0x1d` Heading, `+0x1e` Ball Winning
  ("Tackling"), `+0x28` Physical Contact (PC), `+0x29` Balance, `+0x2c` Jump, `+0x31` height cm, `+0x32` weight.
  **New OBS evidence for the map:** the kick-quality picker `FUN_14071d770` selects `+0x1a` for SHOOT, `+0x1b`
  for short passes, `+0x1c` for lofted passes and `+0x1d` for header kick kinds — confirming 0x1a Finishing,
  0x1b Low Pass, 0x1c Lofted Pass, 0x1d Heading (previously INF).
- ANIME action ids (table `0x1434a5470`): 7 TRAP, 8/9/10 long pass / short pass / shoot, 0xb/0xc SLIDING(_KEEP),
  0xd TACKLE, 0xe BLOCK, 0xf DIVE, **0x10 STAGGER, 0x11 FALL_DOWN**, 0x15 DODGE, **0x48 JOSTLE**,
  0x50 DEMO_INPLAY_CONTACT, 0x57 (see §4). AI action ids: **88 STAGGER (0x58), 89 FALL_DOWN (0x59)**.
- Action-class predicates (OBS, bit tests on the property table `0x1426484f0`): `isLoco` (0/2/3:
  NOTHING/MOVE/STEP_MOVE) `0x140a6e1e0`; `isKick` (8–10) `0x140a6e340`; `isSlide` (0xb/0xc) `0x140a6e4b0`;
  `isTackle` (0xd) `0x140a6e580`; `isTackleSlideSave` (0xb–0xd + keeper saves) `0x140a6e0d0`; `isStgFall`
  (0x10/0x11) `0x140a6e090`; `isJostle` (0x48) `0x140a6e300`; `isTrap` (7) `0x140a6e5e0`; `isGkHandle`
  (keeper catch/punch/…) `0x140a6e450`. `human+8` bit 15 = "sliding" flag, bit 16 = "tackling" flag (used
  together with the predicates).
- Body parts: the 19-bone enum of `pes-re-collision.md` §2.1 (HIP 0, THIGH/LEG/FOOT_L 1–3, …_R 4–6, BELLY 7,
  CHEST 8, NECK 9, HEAD 10, SHOULDER/UPPERARM/FOREARM/HAND_L 11–14, …_R 15–18). The contact code maps a part
  to a **class**: 1–3 → 1 (left leg), 4–6 → 0 (right leg), anything else → 2 (body) (`FUN_140843c30`, OBS).

---

## 0. Summary (what GodotFootball must do)

1. **Ball possession in a duel is still geometric** (`pes-re-duels-fouls.md` §1.2 stands). What this report adds
   is the *body* outcome: every tick each player runs a **contact reaction** (AI reaction-table entry 0,
   `FUN_140933ad0` → `FUN_140844cb0`) that can turn the player into **STAGGER** or **FALL_DOWN**.
2. The core is a **deterministic 3×3 table** (`FUN_140842d20`, OBS): *how far the contact has pushed the
   player's body part off its animated pose* (m) × *the closing speed of the pair* (km/h), each cut by two
   thresholds from the data file **`contact.o`** (in `constant_player.bin`), selected by which part was hit and
   what the opponent is doing (plain / standing tackle / slide). Level 0 nothing, 1–2 **STAGGER**, 3 **FALL**.
   A set of **forced-fall** rules (slide into the legs, legs pushed > 0.5–0.8 m) and **nine veto** rules (arms,
   two runners brushing, a dodge, a slow touch, a player already falling, …) wrap it. **No RNG in the decision**
   (the only draws pick a cosmetic clip variant, per-player LCG, §5).
3. **Physical Contact** enters three times: the push share of the body capsules (known), the **jostle hold-up
   time** before a shielding carrier can be knocked off the ball (2–4 s by ΔPC), and a **shoulder-to-shoulder
   balance gauge** (`ap+0x2820`) that drains while two players run side by side and staggers the loser
   (≈1.5 s ± ΔPC/120). **Balance** sets how fast the stagger/rise-up clips play. **Height/weight are not read**
   by the contact decision; Ball Winning/Aggression only gate the tackle itself (known).
4. **Aerial:** the highest ball a player can head is **`reach = 1.935 + 0.435·clamp((Jump−40)/59) +
   0.4·clamp((height−150)/60)` m** (minus 0–1 ball radius by use, `FUN_140a6de20`). The AI uses it to decide
   who can play a high ball; the header clip's authored hit frame is aligned to the ball's arrival and the body
   is lifted by at most `reach − height/100 − 0.335` m (Jump/height) — then (INF hi) the first head capsule at
   the ball wins, exactly like a foot. **Heading** sets only the header's accuracy (kick-quality curve, §3.3).
   Bodies meeting in the air go through the same contact reaction; live, 22 of 28 airborne reactions were mutual.
5. **Falls feed the Judge** through `human(victim)+1 == 0x11` (fell) / `0x10` with flag bit 30 (one of seven
   "hard" stagger clips), flag bit 29 (face-cover fall → the Judge's forced "FACE" yellow), and the
   cause/partner bytes `human+0x3d/+0x46/+0x47` (§4.4).
6. **Live (§6):** stagger playback rate 39/39 exact, gauge refill/drain 864/870 exact; 44 knock-downs in ≈ 13
   match minutes, none reaching the 4cc speed thresholds, so every fall came through the forced-fall or
   direct-trigger rules (§1.5–1.6).

---

## 1. Contact resolution

### 1.1 Pipeline (OBS)

```
per tick, AI reaction table entry 0 (ScriptBind 0x14263ec60 → 0x140933ad0, see defender-decisions §1)
 └ FUN_140844cb0(ap, ?, cmd)                         # contact reaction; writes cmd+8 AI action, +0xc partner, +0x10 cause
    ├ ap in JOSTLE and ap+0xb10 ∈ 1..7 → action = (b10 ∈ {3,4}) ? FALL_DOWN : STAGGER, cause = b10   (§2.2)
    ├ FUN_1408440f0(ap,&opp): body contact this tick?  (partner pl+0x610 touching, or touched < gs/2 frames ago,
    │      or 0xff = hit by the ball)
    │   ├ yes, opp < 22: level = FUN_140842d20(ap, opp)  (§1.2–1.5), clamp 7
    │   ├ yes, ball (0xff): level = 1
    │   └ action = {0:none, 1:STAGGER, 2:STAGGER, 3:FALL_DOWN, 4:FALL_DOWN}[level] (table 0x1425efc18), cause = level
    ├ no contact but balance gauge ap+0x2820 < 0 → STAGGER, cause 5, partner pl+0x610   (§2.3)
    ├ extra geometric triggers (§1.6): slide/tackle foot at the shins → FALL cause 4/3/6/7; studs near the
    │      foot → STAGGER cause 5/1 (Physical Contact widens the radius)
    └ commit: FUN_140a6d420(ctx,id,action) → FUN_1406b4c10(ap, …)
anime layer: AnimePlayer::TryChangeAction(ap, 0x10/0x11) → Contact::Start FUN_14072f3b0 (§4)
```

- The reaction runs only in play (`MI+0x116c == 1`) and never for a player already in FALL_DOWN or DIVE (OBS
  `0x140844cb0` head).
- `FUN_1408440f0` (OBS): the partner is `pl+0x610` when the capsules still overlap by more than
  `2·(0.23 − r)` m (0.23 @0x1425ab4a8) with the allowance `r = FUN_140a6dd30`: **0.025 for an opponent, 0.05
  for an opponent who is tackling/sliding/saving and already separating, −0.1 for a team-mate** (team-mates
  must overlap 0.66 m, opponents 0.41 m); or the last touch is younger than `gs/2` frames; or a tracked part is
  displaced > 0.2 m. Otherwise a ball-hit test (`FUN_140843e60`) may return `0xff` (hit by the ball).

### 1.2 The contact record `pl+0x610` (OBS, writers `FUN_1408c31c0` / `FUN_1408c48c0`, readers above)

| off | meaning |
|---|---|
| +0x610 u32 | partner id (`< 22`, else none) |
| +0x614..+0x61c | contact point (Fox x,y,z, m) |
| +0x624 i32 | **own body part** at the contact (19-bone enum) |
| +0x63c f32 | capsule overlap depth (m) |
| +0x64c..+0x654 | separation vector applied this tick |
| +0x658 u32 | match frame of the last touch (`MI+0x1198`) |
| +0x65c i32 | **partner's body part** |
| +0x661 u8 | touching now |

`FUN_1408c48c0` (per player per tick, `match::Player` `P`, vtable 0x1426329a8) gathers the PvP contacts
(`FUN_1408c31c0`, collision kind 5 capsules and kind 7 roots, `pes-re-collision.md` §2.3–2.4), keeps the deepest
as the record, adds the separation `Σ normal·(depth − allowance)·share` with **share = `FUN_140a6d670`** (the
push share of `pes-re-duels-fouls.md` §1.2) to the position `P+0x624/+0x62c` and the AnimePlayer root offset
`ap+0x248c..0x2494`, and stores the record at `P+0x6d8` (previous tick's at `P+0x72c`). **`P+0xc8` is a
player-state block with the same layout as `pl(i)`** (OBS: `FUN_1408c31c0` passes `P+200` to the velocity
predictor it uses on `pl(j)`; live, 3/3 players: `P+0xc8+0x55c/+0x564` = `pl+0x55c/+0x564` = `P+0x624/+0x62c`
and `P+0x6d8` = `pl+0x610`), but a different allocation (`pl` is the published copy, `pchk.py`).

### 1.3 Inputs of the level function `FUN_140842d20(ap = self, opp)` (OBS)

- **Displacement `d` (m)** = `FUN_140843c50`: the largest distance, over the self's 4 tracked parts
  (`ap+0x508+12k`, bones by table `0x1425efbe0`), between where the animation puts the part and where the
  collision has pushed it (`FUN_140842be0`; part 0 also removes this tick's separation). When the opponent is
  sliding (flag bit 15 + isSlide) and the slide leg class matches the touched leg class, `d = 0.2` fixed; when
  no part is tracked, `d = overlap(+0x63c) − 0.46`.
  INF(hi): "how far the opponent has knocked my body off its animation".
- **Closing speed `v` (km/h)** = `FUN_140843a20`: both players' velocities `0.6944/gs` s ahead
  (`FUN_140a6c6d0`), minus their contact separation, rotated into the self→opponent and opponent→self lines;
  `v = 3.6·gs·(along_self + along_opp)` (60·60/1000 = 3.6 @0x14259a764, 0x14256ae34).
- **Hit class** `cs = class(pl+0x624)` (own part), `co = class(pl+0x65c)` (partner part).
- **Opponent mode `m`**: 2 = slide that reaches the hit leg (isSlide and (`co==2` or `co == class(human(opp)+5)`)),
  1 = standing tackle past half its clip or tackling flag, self not DODGE, matching leg (`m = (tackle sub-kind
  human(opp)+0x44 != 1)`), 0 otherwise (also 0 while the self is in JOSTLE).

### 1.4 Thresholds (`contact.o`) and modifiers (OBS)

The record is the constant registry entry **0x29** (`FUN_141e5ca20(*(0x1437f4808), 0x29)`), which is
**`contact.o`** of `common/match/constant/constant_player.bin` (runtime record = 8-byte header + the file
blob; live bytes equal the file byte-for-byte, OBS `tune.py`). Index `t = (cs==2) + 2·m`:

| use | contact.o off | stock (`pes-re-injury-fatigue.md` §1.4) | **live (4cc gameplay pack)** |
|---|---|---|---|
| forced-fall closing speed, body hit | +0x04 | 56 | **88** |
| `dF` leg plain / slide / tackle | +0x08 / +0x0c / +0x10 | 0.75 / 0.45 / 0.5 | 0.75 / 0.45 / 0.5 |
| `dS` leg plain / slide / tackle | +0x14 / +0x18 / +0x1c | 0.5 / 0.15 / 0.2 | 0.5 / 0.15 / 0.2 |
| forced-fall displacements (§1.5) | +0x20 / +0x24 / +0x28 / +0x2c | 0.6 / 0.5 / 0.8 / 0.8 | same |
| `vF` / `vS` closing speed km/h | +0x30 / +0x34 | **24 / 16** | **42 / 36** |
| `dF` body plain(=slide) / tackle | +0x38 / +0x3c | 0.2 / 0.04 | same |
| `dS` body plain(=slide) / tackle | +0x40 / +0x44 | 0.15 / 0.03 | same |
| from-behind forced fall enable (byte) | runtime +8 | — | 0 (off) |

(t = 0 leg plain, 1 body plain, 2 leg tackle, 3 body tackle, 4 leg slide, 5 body slide → `(+0x40,+0x38)` again:
the 12-float array at `0x140842fe6` overruns into the plain-body pair, OBS.) Code fallbacks when the record is
missing: 56 / `dS 0.05, dF 0.15` / `vF 27, vS 10`.

Modifiers, in order (OBS):
1. Opponent is a keeper in KP_CATCH/KP_DEFLECT/KP_SCOOP_OUT/KP_BLOCK (mask `0x3500000000000000`): `dS 0.1,
   dF 0.2, vF 15, vS 7`.
2. `m == 1` and the opponent's tackle clip is in the frontal set `FUN_140a6e360` and the two headings differ by
   < 78.75°: `dS 0, dF 0.05` (a frontal standing tackle always at least staggers).
3. `m != 2` and the self is *committed* (mid-kick before its contact frame, or dodging an opponent who is not
   tackling, or both stagger/fall-protected cases of `0x1408431a7`): all five thresholds ×1.25 (the forced-fall
   speed ×2.25).
4. Self in a kick clip of set `FUN_140a6e170` between `8/60` and `16/60` s: ×1.5 (forced-fall speed ×2.5).
5. **Jostle hold-up** (opponent in JOSTLE, self owns the ball `FUN_140a63f80`, self not before a kick contact):
   `T = 120 + 120·clamp((stat(self,PC) + 60 − ab(opp)+0x28 − 20)/80)`; once the opponent's jostle counter
   `human(opp)+0x2f` (frames in jostle phases 7–9, OBS `FUN_140781440`) exceeds `gs·T/60`:
   `dS ×0.005, dF ×0.5, vF ×0.5, vS = −3`, forced-fall speed ×1.5 → the carrier staggers on the next touch.
   **Hold-up = 2 + 2·clamp((PC_carrier − PC_jostler + 40)/80) s**: 3 s at equal PC, 2–4 s at ±40.

### 1.5 The level (OBS; raw `0x140843386..0x1408433c0`, return path `0x1408436e3`, `0x1408439a6..0x140843a10`)

```
dl = d < dS ? 0 : d < dF ? 1 : 2
vl = v < vS ? 0 : v < vF ? 1 : 2
if cs == 2 (body hit) and v > forcedFallSpeed: dl = 2
level = M[dl][vl],  M = [[0,2,3],[1,2,3],[2,3,3]]          # table 0x1425efbf0
```

| displacement \ closing | `v < vS` | `vS ≤ v < vF` | `v ≥ vF` |
|---|---|---|---|
| `d < dS` | 0 | 2 STAGGER | 3 FALL |
| `dS ≤ d < dF` | 1 STAGGER | 2 STAGGER | 3 FALL |
| `d ≥ dF` | 2 STAGGER | 3 FALL | 3 FALL |

Then (OBS):
- **Receiving-body clash** (self's clip has hold data and is inside its hold window, both foot bones > 0.25 m
  off the ball, self not DODGE, partner < 0.65 m, `cs == 2`, self not already STAGGER):
  `p = 3·clamp((PC−40)/59)`; `level = v > p+7 ? 3 : v > p−6 ? 2 : 0`; if either is in TRAP or the hold window has
  started: `level = v > p+25 ? 3 : v > p+22 ? 2 : 0` (PC raises the speed a receiver can absorb by up to 3 km/h).
- Self kicking with AI action WAIT_TIMER, body hit, still within `12/60` s after his contact frame → level 2.
- **Forced fall** `FUN_140844540` → **level 3** (raw `0x1408436e3 mov edi,3`; the decompiler shows a veto):
  - a sliding opponent (flag 15) whose slide leg hits the same leg class (or the body), with the contact point
    above the self's knee bone or on a leg part, and the slide coming from within 112.5° of its direction;
  - an opponent tackling/sliding/saving from behind (> 135° off the self's heading) — gated by `contact.o`
    runtime byte +8, **0 in the live data** (off);
  - any tracked part displaced more than `contact.o +0x2c` (0.5 m; +0x28 = 0.6 while a kick root-curve is
    active; +0x34 = 0.8 while the self is tackling; 0.1/0.2 in a jostle sub-state), parts 1–2 by +0x30 (0.8), or
    part 3 by > 0.5 m;
  - second hits: a body-check while already staggering, tackle hits on a stagger in its first half, etc.
    (remaining branches of `0x140844540`, OBS structure, summarised).
- **Vetoes** (`level > 0` only): nine predicates `*(0x14348cab0)[0..8]` (a `.data` table, same pointers on
  disk) are tried in order; the first that returns true sets **level 0** (raw `0x1408439ef..0x1408439f9`):

| # | fn | veto when (OBS, condensed) |
|---|---|---|
| 0 | `0x140842950` | own part is an arm (12,13,16,17); or the partner's arm hits and nothing special; or two legs touch while the opponent is not kicking/tackling/sliding/jostling/saving; or the self runs ≥ 13.5 km/h and the touched foot trails the body by > 0.25·scale m |
| 1 | `0x140842150` | opponent in KP_THROWPASS/KP_PUNTKICK, or kick/tackle-class bookkeeping (see decompile) |
| 2 | `0x1408428a0` | self already FALL_DOWN, or self in locomotion while recovering from a stagger/fall |
| 3 | `0x140841ac0` | self handling as keeper before its contact frame; opponent's tackle not yet at its contact frame |
| 4 | `0x140841dc0` | self kicking/trapping inside the kick's protected window |
| 5 | `0x140841960` | self in DODGE and the opponent is not tackling/sliding (or has flag bit 9) |
| 6 | `0x140842330` | opponent running away (> 112.5° analysis angle) at > 13.5 km/h |
| 7 | `0x1408419e0` | **self and opponent both in locomotion** (plain running into each other never knocks anybody over through this path) |
| 8 | `0x1408424c0` | relative speed < 10 km/h (22 when the self is nearly still < 5 km/h; 5 inside a kick hit window), unless the facing tests allow it |

### 1.6 Direct geometric triggers in `FUN_140844cb0` (OBS)

After the level path, with the nearest opponent `o` (distance-sorted table `MA+0x22e4c`) within 1.5 m:
- **Slide hits the standing leg → FALL_DOWN, cause 4**: `o` sliding/tackling (team-mode switch
  `FUN_140a55d40`), self not STAGGER/OVER_TAKEN, `o`'s foot bones (`human(o)+0xac/+0xb8`) within 0.1 m (0.2 m
  when the self is DODGE-flagged) of the self's bone 10 point.
- **Studs on the foot → STAGGER, cause 5**: a ball-owning self being challenged (flag 28 tackle ≥ 16 km/h,
  geometry 0.8/0.7 m, jostle counter > `gs/6`): opponent foot within **`0.3 + 0.2·(1 − clamp((PC−40)/59))` m**
  (0.5 m at PC 40, 0.3 m at 99) of the self's bone 8.
- **Trip from behind → FALL_DOWN, cause 3 / 6**: either player in a run-cut clip `FUN_140a6e600` before its
  contact frame, heading within 45°, the other directly behind (`z > 0`, |lateral| < 0.5 m).
- **Tackle sub-kind 1 at its contact frame (±4 frames) → FALL_DOWN, cause 7**: foot within
  `0.2 + 0.1·(1 − clamp((PC−40)/59))` m.
- **Stud-trip → STAGGER, cause 1**: opponent in a set-`FUN_140a6e170` clip between 8/60 and 16/60 s, its foot
  within 0.3 m of the self's bone 7, the self in front and < 1 m lateral.

### 1.7 What is NOT an input (OBS by absence in every function above)

Height and weight (`+0x31/+0x32`) are not read by the contact level, the vetoes, the gauge or the jostle; Ball
Winning / Aggression / Defensive Awareness are read only by the tackle/slide *start* gates
(`pes-re-defender-decisions.md`, `pes-re-duels-fouls.md` §1.2). Body size enters only through the physique
scale `ap+0x3b18` (capsules are fixed-size, `pes-re-collision.md` §2.2). Angle enters through the hit part, the
closing-speed projection, rule 2 of §1.4 and the vetoes. INF(hi): PES has no "strength duel roll".

---

## 2. Shielding and jostling

### 2.1 What already exists (do not re-derive)

- **Body push share** `FUN_140a6d670` and the **Jostle action** rotation / contact-point blend /
  **shoulder-through** (`js_run_3_2_090_guard_back` → `tackleshoulder_3_3` when `PC_self > PC_opp + 5`):
  `pes-re-duels-fouls.md` §1.2.
- **Guard (shielding) locomotion**: the carrier's run loop switches to the `js_run_*_loop_guard_*` styles when the
  nearest opponent (4 ticks ahead) is within `0.7 + 0.3·clamp((PC−50)/45)` m: `pes-re-anim-select.md` §2 (RUN_LOOP).
- **Jostle for an incoming pass**: AI reaction entry 11 (`PASS_GET` / anime JOSTLE): `pes-re-defender-decisions.md` §1.
- **AI shield choice**: `pes-re-attack-decisions.md` §2.7. No CPU "shoulder charge" decision exists.

### 2.2 How a shielding carrier is beaten (OBS, this report)

A challenger in JOSTLE never takes the ball by a roll. The carrier keeps it until one of:

1. **Hold-up timeout** (§1.4 modifier 5): after the challenger's jostle counter `human(opp)+0x2f` (frames in
   jostle sub-phases 7–9; saturating u8 maintained by Jostle slot 18 `FUN_140781440`) exceeds
   `gs·(2 + 2·clamp((PC_carrier − PC_challenger + 40)/80))` frames, the carrier's contact thresholds collapse and the
   next body touch makes him **STAGGER** (level ≥ 1). The stagger releases the ball geometrically (the carrier's
   clip no longer carries it).
2. **Shoulder-through** (§2.1): when the challenger's clip is `js_run_3_2_090_tacklesholder` (m0x8ec) or
   `tackleshoulder_3_3` (m0xced) with `human+0x30 == 0xd`, the carrier's `ap+0xb10` is set to **2**
   (`FUN_1406f6ce0` @0x1406f7fda) → the contact reaction issues **STAGGER, cause 2** (`FUN_140844cb0`: jostling
   player with `b10 ∈ 1..7` → STAGGER, or FALL_DOWN for `b10 ∈ {3,4}`). Only the value 2 has a writer in the image
   (OBS: one `mov byte [rbp+0xb10],2`, no other encoding found); 1 and 3–7 are written elsewhere (open, §8).
3. **Balance gauge** (§2.3): two players running side by side.
4. **Studs on the foot** (§1.6, cause 5): a challenger's foot reaching within `0.3–0.5 m` (PC-dependent) of a
   ball-owning carrier's bone 8 while he is being tackled at speed.

### 2.3 The shoulder-to-shoulder balance gauge `ap+0x2820` (OBS raw `0x1406f9de8..0x1406fa13f`)

Updated every time the AI command is applied to the AnimePlayer (`FUN_1406f9b70`, INF(hi): once per tick):

```
gsI   = FUN_141e532e0()                                   # integer fps
delta = 1_000_000 // (3·gsI)                              # refill: empty → full in 3.0 s
if pl.touching(+0x661) and partner p = pl+0x610 < 22 and both in locomotion (NOTHING/MOVE/STEP_MOVE)
   and both speeds > 13.5 km/h and both bearings to the other ∈ (45°, 135°):          # side by side
    a   = θ_self ≤ 45 ? 0 : (θ_self − 45)/90               # θ_self = angle(own heading, direction to partner)
    r   = (PC_self − 40)/59                                # FUN_1408caf60 clamp bounds of stat 0x28 (40..99)
    T   = 1.5 + (PC_self − PC_p)/120 + 0.5·(2a − 1)·(1 − 0.5·r)    # seconds to empty a full gauge
    delta = −1_000_000 // (int)(gs·T + 0.5)
gauge = min(gauge + delta, 1_000_000)
```

PC here is the ability byte `ab+0x28` of each player (`FUN_14124dad0`). When the gauge is **below 0** and no
other contact is resolved that tick, the contact reaction issues **STAGGER, cause 5**, against the partner
(`FUN_140844cb0` @`*(int*)(ap+0x2820) < 0`). So in a straight shoulder-to-shoulder race at equal PC the player
slightly *behind* (θ < 90°) empties first (≈1.5 s minus up to 0.5 s); each 12 points of PC advantage buys 0.1 s.
Constants: 13.5 @0x1425a1ffc, 45/135 @0x14259be30/0x14259be60, 120 @0x14256ae30, 1.5 @0x14259bddc,
90 @0x14259be5c, 0.5 @0x142565600, `1e6 = 0xf4240`, `−1e6 = 0xfff0bdc0`.

### 2.4 Jostling for a loose ball or a pass

The pass-reception jostle (reaction entry 11) puts the defender in JOSTLE; from then on §2.1 (rotation by
ΔPC, contact-point blend) moves the pair, §1 resolves any knock-down, and the ball goes to whoever's trap /
kick capsule meets it first (geometric, `pes-re-duels-fouls.md` §1.2). There is no separate "who wins the
jostle" roll (OBS: no RNG callee in the Jostle functions `0x1407780d0`, `0x14077cdb0`, `0x140780870`, nor in §1).

---

## 3. Aerial duels and headers

### 3.1 Who can reach the ball: the jump-reach formula (OBS `FUN_140a6de20`, raw)

```
reach(H, J, mode) = 1.935 + 0.435·clamp((J − 40)/59, 0, 1) + 0.4·clamp((H − 150)/60, 0, 1)
                    + { mode 0: −0.108686 (one ball radius) ; mode 1: 0 ; mode ≥ 2: −0.054343 }      # metres
```
`H` = height byte `ab+0x31` (cm), `J` = Jump byte `ab+0x2c` (both raw ability bytes, not modified).
Constants 1.935/2.335/0.435 @0x1426489cc/0x1426489d0/0x1426489c4, 150/210/60 @0x14259be68/0x14259d630/0x14259a764.
Wrappers: `FUN_140a6df00(id, ctx, mode)` = reach; `FUN_140a6df70(ai, mode)` / `FUN_140a6dfd0(id, ctx, mode)` =
`reach − H/100` (the **lift above standing height**). Examples (mode 2): H 180, J 40 → 2.08 m; H 180, J 99 →
2.52 m; H 195, J 70 → 2.40 m.

Consumers (OBS call sites): the per-player ball-height band table `FUN_140a4eb80` (bands `{0.30, 0.60, 1.15,
1.46, 1.80, 2.135, —, —, 2.36}·H/180`, bands 6/7 = `H/100 + lift(mode 2)`: the band the AI uses for "a jumping
header can reach this"); `ActionSelectorCenteringGet` (`0x14061ccc0` → `0x140644210`) — which attacker attacks
a cross; set-piece marking (`FUN_1405f63c0`, reach + 0.05 m when the player data byte `+0x25 > 84`, INF: an
aerial playing style); the player-AI receive helper `FUN_14097fa30` (within 16 m of goal and 8 m of the nearest
opponent).

### 3.2 Jump timing and height (OBS `FUN_140727230`, `FUN_14071c910`)

A header is a **Kick action** clip (kick-kind table `0x14344e380`, 150 × 16 B; byte `+0xc ∈ {0,1}` marks the
28 header kinds: indices 1–27 and 149). Kick start `FUN_140727230`:
- the clip is chosen so that its authored **hit frame** meets the ball's predicted position at `cmd` frame
  `param+0x18` (the same root-alignment as every kick, `pes-re-anim-select.md` §5; `pes-re-collision.md` §3);
  the jump itself is the clip's authored jump (JumpData), started `hitFrame/rate` before arrival;
- the root is then lifted (`FUN_141eb3830(ap+0x498, lift, ikStart, ikEnd, …)`, over the hit record's IK window
  `FUN_1406b11e0..FUN_1406b10d0`) by `clamp(ballY − clipHitY, −L, +L)`, where `L = FUN_14071c910(...)`:

| kick category (`ap+0x2754`) | ball above the authored hit | `L` (m) |
|---|---|---|
| 9 (INF: jumping header) | yes | `reach(H,J, mode) − H/100 − 0.335 + (1 − max(1,scale))·0.4437` |
| header kind, category 7 | yes | `0.0543·max(1,scale)` (kinds 10–13, 0x79–0x86) or `0.1087·max(1,scale)` |
| kicks in mask `0x3600440000000000`, category ≥ 2 | yes | `0.0543·max(1,scale)` |
| other | yes | `0.2174·max(1,scale)` (two ball radii) |
| category < 8 | no (ball lower) | 0 |
| clip without an IK window (`FUN_140724620`) | — | 0 |

So for a jumping header **Jump adds up to +0.435 m and height up to +0.4 m of reach, but the lift above a
player's own height is `0.4·hn − H/100 + 1.935 − 0.335 + 0.435·j`**: at J 40 a 1.80 m player gains 0.0 m over
the clip's authored jump, at J 99 0.435 m. INF(med): category 9 = the jumping-header family (the only one that
reads reach); the category byte's writer was not traced.

### 3.3 Header accuracy and power (OBS `FUN_14071d770`, `0x14071cf36`)

For header kick kinds the kick-quality stat is **Heading** (`stat(ap, 0x1d)`), +5 for a header SHOOT by a player
with skill bit 6 (Heading skill; `FUN_1406ead40(ap,6)`). Quality:

```
q(S) = S < 60 ? max(0, 0.1·(S − 40)/20)
     : S < 95 ? 0.1 + 0.85·(S − 60)/35
     :          0.95 + 0.0125·(S − 95)
error_term = kickRecord+8 + 1 − 5·q²            # stored by the caller FUN_14071cde0 (0x14071cf43..0x14071cf70)
```
The error term then goes through the kick launch's per-player LCG scatter (`pes-re-kick-launch.md` §3,
`ap+0x32d0`). Direction and power come from the AI request (target point and speed) exactly as for a foot kick
(INF(med): the same `FUN_14071cde0` path; header-specific spin/tilt table entries not traced).

### 3.4 Who wins an aerial duel (OBS + INF)

There is no aerial-duel roll (OBS: no RNG callee in §3.1–3.3 or in the contact code). The outcome is:
1. **who goes**: the AI's reach bands (§3.1) decide who can attack the ball and the receive/centering-get
   selectors pick the player and the meeting point (INF(med): the selector internals beyond the reach call were
   not traced);
2. **who touches first**: the header clip's head capsule reaching the ball at its hit frame (`pes-re-collision.md`
   §3), lifted by §3.2 — so height and Jump decide how high, timing decides who;
3. **bodies meeting in the air**: the contact reaction of §1 runs for airborne players too. The Contact start
   switches on head height: below `0.5·(1.685+1.91)·scale ≈ 1.80·scale` m → grounded start `FUN_1407302b0`; above →
   **airborne start `FUN_14072f670`** (clip groups `stagger_air`/`fall_*`, §4.2). Upper-body hits use the
   body thresholds of §1.4 (0.15/0.2 m displacement), and the push share decides who is displaced: **Physical
   Contact wins the shove in the air, Balance shortens the stagger; Jump/height only buy the first touch.**
   Physical Contact does not enter the reach.

---

## 4. Falls and stumbles

### 4.1 Actions (OBS RTTI)

`match::anime::action::Contact` (vtable 0x1425df020) is the base of **Stagger** (0x1425df138, id 0x10) and
**FallDown** (0x1425df250, id 0x11). Shared: Update `0x14072c7a0` (phase 0 start, 1 main, 2 rise-up/continue;
afterwards keeps the player inside the pitch rectangle), Start `0x14072f3b0`. Per class: slot 29 clip-group
selector (`0x14072d5a0` Stagger / `0x14072d260` FallDown), slot 18 publish-to-human (`0x140731a70` /
`0x1407318f0`), slot 31 cancel test (`0x14072dd90` / `0x14072dbd0`), slot 10 end handling (`0x14072f260` /
`0x14072f020`), slot 32 rise-up gate (FallDown `0x14072c5c0`).

### 4.2 Which clip (OBS)

The selector builds a descriptor and returns a **clip group** that `FUN_1406abb30(selector, group, angle)` turns
into a motion by the hit angle (`FUN_1406de990`). Descriptor (airborne start `FUN_14072f670`): `[0]` lateral
speed class of the hip over 10/60 s (`< 8` km/h → 0, `< 22` → 1, else 2); `[1]` hit-part class (`FUN_140843c30`:
right leg 0 / left leg 1 / body 2, mapped to 2/3); `[2]` = 1 when hit by the ball (the ball's position picks
`0x19e..0x1a1`); `[4]` partner/ball relation; `[5]` distance to the partner; `[7]` side; `[10]` lateral speed;
`[11]` variant. Stagger groups `0x178..0x1a6` (speed 19/2.0 km/h cut-offs, variants `% 3` / `& 1`); FallDown
groups `0x145..0x177`, `0x1a7..0x1ae` (speed 13.5/16/22 km/h cut-offs, `% 6` / `& 3`, the 0.652 and
`0.6 + 0.1·…` distance tests). Motion families (names from the motion table `0x14351e590`): `stagger_lowbody_*`,
`stagger_upbody_*` (incl. `_pulled`, `_quickly`, `_hard`), `stagger_head_0_0_000`, `stagger_air_*`,
`fall_lowbody_*`, `fall_upbody_*` (incl. `_facecover`), then `riseup_*`.

**Variant source:** airborne start = the match frame `MI+0x1198` (deterministic); grounded start = **per-player
LCG `FUN_1406e6bf0(ap, 10000)`** (`0x140730494`), plus a 70 % roll `FUN_1406ebf30(ap, 70)` between groups 0x1ae /
0x1b3 when hit by a `tacklehold_waist_3_0_020` (m0xf6c) below 16 km/h (`0x140731231`). These are the **only RNG
draws in the whole contact system, and they are cosmetic** (clip variant), §5.

### 4.3 Timing and recovery (OBS)

| phase | rule | VA |
|---|---|---|
| blend-in | `min(clipCancelTime, FUN_1406e7ce0(ap, 0.2))`, FALL clamps it to [0.1, 0.2] s, +0.2 s when `FUN_1406ec200(ap,5)`, final clamp [0.1, 0.4] s | `FUN_14072d0b0` |
| playback rate | **STAGGER: `0.95 + 0.15·b²`, `b = clamp((Balance − 40)/59)`** (0.95 → 1.10); FALL_DOWN 1.0 | `0x14072fe1b` (`FUN_14072f670`), `FUN_1407302b0` |
| stagger exit | may be cancelled `min(5, 6 − ⌊6f²⌋)` frames after the clip's cancel frame, `f = clamp((Balance − 40)/60)` | `FUN_14072dd90` (`pes-re-duels-fouls.md` §1.2) |
| fall: lie | stays down until the clip end; may cancel early when not in a stoppage, both foot bones are above the reference height and the partner no longer overlaps (> 0.46 m) | `FUN_14072dbd0` |
| fall: rise-up gate | rise-up starts at the clip's end frame (window +4/60 s) only if the ball (predicted 1 s) is within 10 m, or the fall kind is 4, or `FUN_140a638c0`; never during a stoppage or a pending Judge decision (`FUN_140a53f90`) | `FUN_14072c5c0` |
| rise-up rate | **`0.95·(1 − b) + 1.1·b`, `b = clamp((Balance − 40)/60)`**, blend 0.3 s | `FUN_14072ea00` @0x14072eefd |
| after | ball owner → DRIBBLE continuation (`GetAction(4)` slot 11); else MOVE; an injury check first may switch to `DEMO_INPLAY_CONTACT` (0x50) | `FUN_14072ea00` |

Clip lengths (`anim_infos.json`, `pes-re-injury-fatigue.md` §5): staggers 46–64 frames, rise-ups 36–50, falls up
to 170. **Correction to `pes-re-anim-select.md` §4 (playback table, "kick (`FUN_1407302b0`, kick kind 0x10)")**:
`FUN_1407302b0` is the grounded **Contact** start and 0x10 is the anime action STAGGER — the `0.95 + 0.15·p²`
Balance rate is the stagger playback rate, not a kick's.

### 4.4 What the Judge sees (OBS slot 18 publishers)

| human field | Stagger (`0x140731a70`) | FallDown (`0x1407318f0`) |
|---|---|---|
| `+1` action | 0x10 | **0x11 (the Judge's "victim fell")** |
| `+4` | 5 | 4 |
| `+8` bit 25 | contact kind (Contact `+0xd`) ∈ {2,3} | same |
| `+8` **bit 30** | **clip ∈ {stagger_lowbody_3_3_000, _hard, _hard_long, _090_hard, stagger_upbody_3_3_090, _hard, _quickly}** (`FUN_14072e7d0`) — the Judge's "staggering with bit 30" (§2.3 of the Judge report: ×1 instead of ×0.7, +5 points) | — |
| `+8` **bit 29** | — | **clip ∈ {fall_upbody_0_0_090_facecover, fall_upbody_3_0_090_facecover}** — the Judge's forced **"FACE" yellow** outside the box (`pes-re-duels-fouls.md` §2.5 step 1) |
| `+8` bit 1 | — | rise-up can cancel early |
| `+0x3d` | partner id when caused by an opponent, else 0xff | same |
| `+0x46` | partner's action at the hit (0xd when tackling-flagged; kicks with sub-kind ≠ 0/7 → 2) | same |
| `+0x47` | own previous action (unless 0x10–0x15 other than 0x14) — the Judge's "cause byte" | same |

So the "victim fell" flag is set by the §1 decision (level 3 or a forced fall) and the Judge's gate
(`FUN_14050d6a0`: A in STAGGER/FALL_DOWN) admits the contact event for scoring.

---

## 5. Randomness

| draw | generator | slot | effect |
|---|---|---|---|
| contact level, forced falls, vetoes, gauge, jostle hold-up, reach, header lift | none | — | **deterministic** (OBS: no RNG callee in any function of §1–3; the only rng-region callees are angle helpers `0x140a928c0/0x140a92950/0x140a92c80/0x140a92d40`) |
| grounded contact clip variant | per-player LCG `ap+0x32d0` via `FUN_1406e6bf0(ap,10000)` | stream 2 (`FUN_140a92470(rng,2)`, `pes-re-kick-launch.md` §3.2) | which stagger/fall clip |
| tackle-hold reaction group | per-player LCG `FUN_1406ebf30(ap,70)` | stream 2 (`LCG mod 100 < 70`) | 0x1ae vs 0x1b3 group |
| airborne contact clip variant | none — `MI+0x1198 % n` | — | which clip |
| header scatter | per-player LCG (kick launch) | as `pes-re-kick-launch.md` §3 | header direction error |
| keeper stand-or-fall | per-player LCG stream 2 `% 100 < Balance − 10` (`FUN_1406d6780`) | `pes-re-duels-fouls.md` §1.2 | keeper collapse |

None of these touch the match `math::Random` slots of `pes-re-rng.md` §3 (OBS by callee). Coordinated with
RngSlotsRE (message sent: "contact/aerial: no match-RNG slot; only per-player LCG ap+0x32d0 stream 2 for clip variants").

---

## 6. Live validation (read-only `/proc/<pid>/mem`, no input, no writes)

Session 2026-10-14. A first 25-min capture saw only a post-match demo (AI action 114 for every player; 0 events,
kept as a negative control). A new match then started (`MI+0x116c` 1 = in play); `duel_watch.py` sampled all 22
players once per sim frame for 1,500 s (frames 6,406 → 49,594, ≈ 13 min of match time incl. stoppages): human
record, published player block, ability block, and the AnimePlayer (`match::Player` objects located by vtable
0x1426329a8, `ap = P+0x7d8`, validated by `ap+0x2458 == id`). Output `w2.jsonl`, analysis `analyze.py`. 4cc
rosters (PC/Balance 69–99).

| check | result |
|---|---|
| **Stagger playback rate = `0.95 + 0.15·clamp((Balance−40)/59)²`** (§4.3) | **39/39 exact** (max error 0.00000; Balance 69→0.9862 … 99→1.1000) |
| FALL_DOWN playback rate = 1.0 | 5/5 |
| **Gauge refill = `1e6 // (3·gs)` = 6,172 per frame** (§2.3) | 2,040 refill rows all 6,172 except the capped last step before 1,000,000 |
| **Gauge drain = `−1e6 / (int)(gs·T + 0.5)` with the §2.3 `T(ΔPC, θ, PC)`** (C truncating division, state of the previous frame) | **864/870 exact** (INF: the 6 misses are frames where the pair enters/leaves the side-by-side window between the two reads); e.g. PC 81 vs 86 at θ 92–94° → −12,500 (T = 1.481 s) |
| gauge reaching < 0 (cause-5 stagger) | not observed (minimum 360,926, i.e. no side-by-side drain longer than ≈ 1 s) |
| closing speed at every STAGGER/FALL entry (estimated from the sampled positions/velocities, not the engine's 0.6944/gs-ahead predictor) | max 28.8 km/h < `vS` 36 → speed level 0 for all 44 entries under the 4cc `contact.o`; so every live **FALL must come from the forced-fall / direct-trigger paths** (§1.5–1.6; seen: `fall_lowbody_3_0_090_plunge` leg-on-leg against a TACKLE sub-kind 3, `fall_upbody_2_0_020_tackle_lose_*`, two mutual `fall_air_*`) and every STAGGER from a non-speed path (displacement axis, receiving clash, ball hit, §1.6) — no contradiction |
| airborne vs grounded start (§3.4) | all 28 `stagger_air_*`/`fall_air_*` entries had a kick/header previous action (SHORT_PASS 21, SHOOT 4, LONG_PASS 3); the 16 grounded ones (`stagger_lowbody/upbody/ballhit`, `fall_lowbody/upbody`) followed DRIBBLE/TRAP/MOVE/STEP/OVER_TAKEN or a ground kick — consistent with the head-height switch, not a proof of it |
| **aerial duel outcome** | **22 of 28 airborne reactions were mutual** (the partner also entered an airborne stagger within 12 frames) — the symmetric per-player evaluation of §1 makes both jumpers stagger; INF(hi): the ball goes to the head that touched it first |
| hit by the ball (level 1 via the `0xff` partner) | 1, INF(hi) from the clip `stagger_ballhit_reaction_y03_0_0_0` with the recorded partner 16 m away |
| jostle | 50 JOSTLE entries (pass-reception and carrier duels); `ap+0xb10` of the jostling player was 0 at every entry (the carrier-side shoulder-through value 2 was not sampled) |
| Judge bits 29/30 at entry | 0 in all 44 — consistent: none of the 7 "hard" stagger clips nor the two face-cover falls was chosen |

Not checked live (§8): the displacement `d` of §1.3 (needs bone world positions), the forced-fall thresholds,
the jostle hold-up timeout, the header lift.

## 7. GF counterpart: what each finding replaces

| PES finding | GF code it replaces | note |
|---|---|---|
| §1 level table (displacement × closing speed, `contact.o` thresholds), §1.5 forced falls, the nine vetoes, §1.6 direct triggers | `Match::CheckHumanoidCollision` trip block (`match.cpp` "u b trippin?": `p1sensitivity`… backfacing + velocity + possession + `(1−balance)·3` + penetration·6 + ball proximity, `/14`, thresholds 0.38/0.48/0.58 → trip types 1/2) and the tackle-AABB block (frames 5–28, foot/lower-leg AABB → `TripMe` type 3 slide / 1 standing) | PES has **no Balance term and no possession term** in the knock-down; GF's per-pair sensitivity sum and its thresholds are not PES. Port: per player per tick, displacement (GF already has `decayingPositionOffset`/`positionOffset` ≈ PES's part displacement) and closing speed → `M[dl][vl]`; slide into the legs → fall; arm/locomotion-vs-locomotion/slow (<10 km/h) contacts → nothing |
| §1.2 push share (known) | `CheckHumanoidCollision` `bounceBias` (backfacing ±0.8, velocity ±0.2, tackle `0.1+0.4·stat`, designated possession ±0.4, ball distance ±0.6, **Balance ±1.0**) and the possession side-snap (`balance·0.3`) | PES uses **Physical Contact**, speed classes and a 45°/135° heading test; Balance is not in it |
| §2.2 jostle hold-up (2–4 s by ΔPC), shoulder-through → STAGGER cause 2 | GF `e_FunctionType_Interfere` "similarBias" take-over and the possession side-snap | GF has no hold-up timer; the carrier is never knocked off by time |
| §2.3 balance gauge (side-by-side running, 1.5 s ± ΔPC/120, refill 3 s) | none (GF "similar" force only steers) | new subsystem, deterministic |
| §4 STAGGER (levels 1–2) / FALL_DOWN (level 3) | `HumanoidBase::TripMe` / `AddTripCommandToQueue` trip types 1 (bump/stagger) and 2/3 (fall) | map level 1–2 → trip type 1 (`stagger_*`), 3 → 2/3 (`fall_*`); stagger rate `0.95+0.15·b²` and rise-up rate `0.95…1.1` by Balance replace GF's fixed trip clips; `TripMe`'s "ball retainer immune" rule is not PES (a shielding carrier *is* staggered after the hold-up) |
| §4.4 human fields bit 29/30, `+0x3d/+0x46/+0x47` | `Referee::TripNotice(victim, fouler, tripType)` | feed the Judge port (`pesjudge.cpp`) with: fell = level 3, "hard stagger" = clip family of bit 30, FACE = facecover fall (forced yellow) |
| §3.1 reach(H, J) and §3.2 header lift | `humanoid.cpp` touch search: `animBallPos.z · playerHeight/defaultPlayerHeight` and the hard caps (`> 1.8 m` ×2, `> 2.6 m` ×20 penalty) | replace the caps with `reach(H,J,mode)` and the lift `L` of §3.2; GF never reads `physical_jump` outside set pieces |
| §3.3 header quality `q(Heading)` | GF's header uses the generic shot/pass error (`humanoid_utils.cpp` shot power/accuracy) and `GetHeaderMultiplier` (+12 % for Heading/Target Man skills, `playerskills.cpp`) | PES: Heading stat through `q(S)`, +5 stat for a header shot with the Heading skill; no Target Man bonus |
| §5 no RNG in contact | GF trip decision is deterministic too, but GF's `random(-2,2)` in team positioning "to stop people bumping" (`teamAIcontroller.cpp:1044`) is unrelated | keep the port RNG-free; clip variants may use a per-player LCG (cosmetic) |

Labels for the port: every non-PES approximation `RE-APPROX(EV-CONTACT-xxxx)` (ledger §9).

---

## 8. Open items (what was tried)

1. **`ap+0xb10` jostle result writers for values 1 and 3–7** (3/4 → FALL): only the `= 2` store at
   0x1406f7fda exists as a direct `[reg+0xb10]` write in the image (scanned disp32 `0xb10` stores of every width
   and SIB forms in 0x1406c0000–0x140a80000). INF(med): the other values are written through the AnimePlayer
   interface pointer (`ap+0xab8`, field `+0x58`) inside the Jostle action. Next: scan Jostle functions for
   `[r+0x58]` byte stores with `r = ap+0xab8`, or live-watch `ap+0xb10` with a perf write watchpoint.
2. **Kick category `ap+0x2754` writer** (which category is "9 = jumping header"): not traced.
3. **Header direction/power** beyond the quality term: the request path from the receive selectors to the
   header kick (target, speed) was not followed; the per-kind spin/tilt rows for header kinds not read.
4. The remaining branches of the forced-fall function `FUN_140844540` after the displacement tests (second hits
   during a stagger) are summarised, not enumerated.
5. **Live check of the full level formula**: the displacement `d` needs the bone world positions
   (`FUN_141eb51a0`); only the observable parts (§6) were checked live.
6. `contact.o` runtime byte +8 (the from-behind forced fall) is 0 in the live 4cc data; its stock value was not
   read (the stock decode in `pes-re-injury-fatigue.md` §1.4 lists only the floats).

---

## 9. Evidence ledger (2026-10-14, ContactRE)

| ID | Claim | Confidence | Evidence |
|---|---|---|---|
| EV-CONTACT-0001 | Contact reaction = AI reaction entry 0 → `FUN_140844cb0`; outputs AI STAGGER 0x58 / FALL_DOWN 0x59 + partner + cause | Confirmed | `0x140933ad0` → `0x140844b33`; decompile + table `0x1425efc18` |
| EV-CONTACT-0002 | Level = `M[dl][vl]`, M = {0,2,3,1,2,3,2,3,3}; level→action {0,S,S,F,F} | Confirmed | `.rdata` 0x1425efbf0 / 0x1425efc18 (disk = image) |
| EV-CONTACT-0003 | Thresholds = `contact.o` (registry 0x29); live = 4cc values 88/0.75/0.45/0.5/0.5/0.15/0.2/0.6/0.5/0.8/0.8/42/36/0.2/0.04/0.15/0.03 | Confirmed | live record read (`tune.py`) = `constant_player.bin` blob byte-for-byte |
| EV-CONTACT-0004 | Forced fall (`FUN_140844540` true) returns level 3; vetoes zero the level | Confirmed | raw 0x1408436e3, 0x1408439ef..f9 |
| EV-CONTACT-0005 | Jostle hold-up `T = 120+120·clamp((PC_s+60−PC_o−20)/80)` frames@60 | Confirmed (arith) / High (meaning) | `FUN_140842d20` 0x140843308.. |
| EV-CONTACT-0006 | Side-by-side balance gauge `ap+0x2820`, refill 1e6/(3·gs), drain 1e6/(gs·T), T formula §2.3; < 0 → STAGGER cause 5 | Confirmed (code) | raw 0x1406f9de8..0x1406fa13f; `0x140844cb0` |
| EV-CONTACT-0007 | No match-RNG in contact/aerial decisions; per-player LCG only for grounded clip variants | Confirmed | callee scan of 39 functions; `0x140730494`, `0x140731231` |
| EV-CONTACT-0008 | Header reach `1.935 + 0.435·j + 0.4·hn` (+mode offset), raw Jump/height bytes | Confirmed | raw `0x140a6de20`, `0x140a6dfd0` |
| EV-CONTACT-0009 | Header lift `L = reach − H/100 − 0.335 + (1−scale)·0.4437` for category 9 | Confirmed (arith) / Tentative (category 9 = jumping header) | raw `0x14071c910` |
| EV-CONTACT-0010 | Header quality uses Heading (0x1d), q(S) curve | Confirmed | raw `0x14071dc1e..0x14071dd59` |
| EV-CONTACT-0011 | Stagger playback `0.95+0.15·b²` (Balance); rise-up `0.95…1.1`; corrects anim-select §4 "kick kind 0x10" | Confirmed (code) | `FUN_14072f670`, `FUN_14072ea00` |
| EV-CONTACT-0012 | Judge bit 30 = hard stagger clip set; bit 29 = facecover fall | Confirmed | `FUN_14072e7d0`, `FUN_1407318f0` |
| EV-CONTACT-0013 | `pl(i)` is the published copy of the state block `Player+0xc8` (same layout); record `pl+0x610` mirrors `Player+0x6d8` | Confirmed (live 3/3, values) | `FUN_1408c31c0` (`P+200`), `FUN_1408c48c0`, `pchk.py` |
| EV-CONTACT-0014 | Ability getter `FUN_1406e6660(ap,k)` = byte `ai(id)+0x70+k` (no separate "live" PC) | Confirmed | raw `0x1406e6660` |
| EV-CONTACT-0015 | In-match stat map 0x1a Fin / 0x1b LP / 0x1c LoP / 0x1d Head | Confirmed (usage) | `FUN_14071d770` 0x14071d89b..0x14071dc26 |


> **Follow-up (ContactLeftoversRE, `pes-re-contact-2.md`):** `ap+0xb10` has exactly one writer (value 2, shoulder-through); values 3/4 fall, 5–7 stagger, and the field is never cleared. `contact.o` stock vs mod differs in exactly three fields (+0x04 56→88, +0x30 24→42, +0x34 16→36); the "from behind" switch is 0 in stock too. §3.2's "category 9 = jumping-header family" is retracted: `ap+0x2754` is the ball-height band 0–11 (HeaderRE).
