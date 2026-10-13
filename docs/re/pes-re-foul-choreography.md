# PES 2021 foul / card / offside choreography (RE report, FoulChoreographyRE)

Tags: **OBS** = read in raw bytes / disassembly / decompile at the named address, or decoded from the
install's data file; **INF(conf)** = inferred. Addresses are VAs in `pesmem.bin` (base 0x140000000).
Scratch (never in the repo): `~/ghidra-projects/scratch/FoulChoreographyRE/` — `dec/*.c` decompiles,
`dis_*.txt` disassembly, `tbl.py` (table_foul.bin decoder; consumes exactly 100 % of every stream: A 180,
B 360, W 1231, U8 402, S 863), `table_foul.json`, `fdc_decoded.json` (all 78 foul fdcs), `atab.py`
(Animetable row dumper), `fdc/` (foul fdcs + table, extracted at runtime with `pesfmt_dump cat`).
Helper scripts: `pdata.py` (function bounds from .pdata), `callscan.py` (rel32 call/jmp scan),
`dispfind.py` (writers of a struct displacement), `hdec.sh` (headless decompile).

Names: `G` = `*(0x1436f3dc8)`; `MI` = `*(G+0x270)` (match info; the restart/foul block MI+0x1550..0x15c4 is
reset by `FUN_140a3c560` and refreshed every frame from `MatchControl+0x2910..` by `MatchControl` vtable slot 19
`FUN_140422030`, OBS @0x140422175..); `DC` = DemoControl registry object `*(G+0x1a8)` (layout §2.4);
`Flow` = the situation sequencer object (step program `Flow+0x878` u32[30], run by `FUN_14044c880`).

---

## 0. Summary (what GodotFootball must do)

1. **Every whistle stop is a step program** (`Flow+0x878`), not a free-form state machine. A foul/offside
   stop is built by `FUN_14044eb10` and is, in order (OBS):
   `3 STOP` → `5 FOUL` (or `0xb OFFSIDE/INDIRECT`, or `6 LATE-CARD`) → `0x45 RESET` → `0x11 WIPE-OUT 0.1 s` →
   [`0x1d`,`0x1f` substitution window] → `0x23, 0x16, 0x12, 0x13, (0x17), 0x14, 0x15` → `0xe FADE-IN 1.0 s`
   → `0x34, 0x19, 0x1a` (restart armed). §4.
2. **The foul step's hold** (`Flow+0x868`, seconds × gameSpeed frames): **2.0 s when no cutscene** is
   selected; 5.0 s (direct foul) / 3.0 s (offside, `MI+0x1588==1`) as the base value from `FUN_14044e460`
   when a scene plays (the scene's own length then governs). §4.2.
3. **Cutscene choice is deterministic except two random rolls** (variant pick by the 0x43 weighted roll).
   A plain foul **without a card by a player not already booked, outside the "pinch" zone, gets no scene**
   (0x20022). Every card has a scene. §1.
4. **During the 2 s hold every outfield player gets a scripted "out-of-play reaction" role** (fouler 0x0f,
   victim 0x10, team-mates near ref protest 0x11, victim's team-mates 0x12, others 0x13), written into
   `DemoPlayerInfo+0x24` by `FUN_1405ad120`. §3. Offside uses `FUN_1405ad720` (offside player 0x15, passer 0x16,
   attackers 0x19/0x17, defending CB/SB 0x18 appeal). §5.4.
5. **Then a screen wipe** (step 0x11 = fade type 4, 0.1 s) hides the cut to the restart setup; players do not
   walk back on camera. §4.3.
6. **Referee gestures** are Animetable rows 179–193 (`judge_*`) picked by `FUN_1407a1ac0` from the referee
   command (+0x85c) and `MI+0x1554/0x1588`; indirect offences (`MI+0x1588` ∈ {1 offside, 3}) use the
   `judge_fk_indirect_*` raised-arm set; speed ≥ 16 picks the moving variants. §4.4.

---

## 1. Foul / card scene selection (`FUN_140480910` → `FUN_14047f170`)

### 1.1 Callers and wrappers (OBS)

`FUN_14044c880` runs the step program; steps 3..0xb go through `FUN_140446390` (switch, OBS):

| step | handler | role |
|---|---|---|
| 3 | `FUN_140447230` | stop: stamps `Flow+0x14 = *(G+0x2a80)+0x60` and `+0x18 = FUN_140a74d70(match)`; one frame |
| 4 | `FUN_140446b20` | goal stop (pushed when `MI+0x1574 == 3`) |
| **5** | **`FUN_140446960`** | **foul stop + foul/card scene** — `FUN_14047feb0(Flow+0x6d8, …, p5=0, p6=0)` |
| 6 | `FUN_140446520` | late card (card for an earlier advantage) — `FUN_14047feb0(…, p5=1)` → always 0x20002 `FOUL_CMN_CARD_Y_B` |
| 7 | `FUN_140447290` | pushed when `MI+0x1554 == 7` |
| 9 | `FUN_1404466f0` | fatigue injury — scans both XIs for a player whose `player+4+0x1c == 3`, then `p6=1` → `0x2001f + (playerDB[+1]=='R')` (`FOUL_INJURY_FATIGUE01` / `…_OUT`) |
| 0xb | `FUN_140447690` | offside / non-scene stop (pushed instead of 5 when `MI+0x1588 == 1`) |

`FUN_14047feb0` (OBS @0x14047feb0):
- calls `FUN_140480910`; `0x20022` = **no scene** (the foul table has 33 scenes 0x20001..0x20021; `FUN_140480910`
  queues nothing when the first id is 0x20022, OBS @0x1404809e9).
- `req+0x8 = gameSpeed·3 / speedRate` frames (3 s); ×6 (6 s) for injury scenes 0x20019..0x20020 (`FUN_140a3fe70`).
  INF(med): a pre-roll / minimum hold for the scene.
- writes DC: `DC+0x00 = sceneId` (`FUN_14102cad0`), `DC+0x88 = 1` (`FUN_140a60720`, out-of-play kind = FOUL),
  `DC+0x04 = subject` (`FUN_140a607e0`) = fouler `MI+0x1590`, or `match+0x2470c` when `MI+0x1590 > 21` and
  `MI+0x1588 == 1`, and `DC+0x8c = 1` (active). `uVar7` = 1 for 0x20001..0x20003 and 0x20019..0x2001a
  (passed to `FUN_1442313a0`, outside the dump; INF lo: "show card on HUD").
- `FUN_140480910` builds the DemoInfo with event descriptor `{side = MI+0x1558, pos = (0,0,0), player = 0xff}`
  (`FUN_14047c1d0` + @0x140480a8a), so condition keys 0x07/0x08/0x22 see x=z=0 for fouls (OBS).
- EXCITE scenes (0x20015..0x20018, `FUN_140a3fd20(id,0)`) increment `req+0xd`; it is read back as "excite already
  played" (§1.3), i.e. **at most one EXCITE scene per match** (INF hi; the counter is never reset in the code read).

### 1.2 The decision inputs (OBS: field reads in `FUN_14047f170`, raw bytes @0x14047f1f9..0x14047fc05)

| input | source | meaning (evidence) |
|---|---|---|
| victim | `MI+0x1594` (0..21) | must be a valid on-pitch player or no scene; injured if `player+4+0x14 != 0` or `player+4+0x10 != 0` (OBS) |
| fouler | `MI+0x1590` | |
| card given | `MI+0x158c ∈ {3,4}` | otherwise the fouler slot is treated as 0xff = "no card" (`uVar4`) |
| card colour | newest kind-4 event record (`FUN_140a3e150(matchHist,4,0)`, ring of 8 × 800 B per kind) `+0x1c` = a copy of `MI+0x158c` (OBS, `FUN_140a3d0c0`) | 3 = yellow, 4 = red, 2 = warning only (OBS compare chains); INF(med) 0 = no card |
| foul kind | same record `+0x18` = copy of `MI+0x1588` | **3 ⇒ never a scene** (OBS @0x14047f2c0); 1 = offside; `FUN_140a63940` treats 1 and 3 as **indirect** offences (§4.4); 0 = direct (INF hi) |
| injury veto | record `+0x00 != 0x37 && +0x42 != 0` ⇒ victim not treated as injured; `+0x42` is set when the victim's match-stat entry (`MI+0x17ac` block) has kind 0xf (OBS `FUN_140a3d0c0`, meaning open) | |
| booking count | `*(u8*)fouler.player` (`FUN_1414dab90` is identity) | **after** this foul's card: 1 = one yellow, ≥2 = second yellow |
| restart kind | `MI+0x1554` | 6 = penalty kick (PK scenes; also bypasses the pitch-bounds check). INF(hi) from the gesture/linesman choosers: 1 kick-off, 2 throw-in, 3 goal kick, 4 corner, 5 free kick (step 0x17 `FUN_1404a9ce0` = FK setup only for 5), 6 PK, 7 game end |
| advantage-card flag | `MI+0x15b8` | INF(med): the card follows an advantage that was played |
| period | `MI+0x1168` | 3 = 2nd half, 7 = 2nd half of extra time (FUN_140a74d00 map: 1→0,3→1,5→2,7→3,9→4) |
| foul spot | `MI+0x155c` (x), `MI+0x1564` (z) | Fox metres, pitch centre origin |

Event ring: event type 0x1e (30) is the only type mapped to ring 4 by the table at 0x142642e60 (OBS), so the
kind-4 record is the **foul event**; it is pushed by `FUN_140a3dac0` from the generic recorder `FUN_140a3dc40`.
It is written by `ObserverFoul@record@match` slot 1 `FUN_140a13bc0` when the match goes out of play
(`MI+0x116c == 2`) with out-of-play reason `MI+0x1574 == 5` (foul, incl. offside), or when `MI+0x11bb` / `MI+0x135c`
is set; the payload at record+0x14 is `FUN_140a3d0c0(MI+0x1584)`: +0x18 = `MI+0x1588`, +0x1c = `MI+0x158c`,
+0x20/+0x28 = player ids of `MI+0x1590`/`MI+0x1594`, +0x43 = PK flag (OBS).

### 1.3 Decision tree (OBS, verified against disassembly; float compares re-read in raw bytes)

```
if MI==0 || G+0x2aa8==0 || hist==0 || victim>21 || !valid(victim) -> NONE
if foulRec.foulKind == 3 -> NONE          # MI+0x1588 copy; 3 = indirect (non-offside) offence
if !( (spot.x <= fieldInfo[0] && spot.z <= fieldInfo[1])  // signed compares, @0x14047f2cd..2ea
      || restart == PK ) -> NONE
injured = victim injured && !(rec.type!=0x37 && rec[+0x42]) ? victim : none
carded  = (MI+0x158c in {3,4}) ? fouler : none
Y = card is yellow (rec+0x1c==3), R = red (==4), n = fouler's yellow count after the foul

if restart == PK:                                     # FOUL_PK_*
   carded none : booked(n==1) ? PK_Y_CARD_N 0x20014 : PK_CARD_N 0x20011
   R           : n==1 ? PK_Y_CARD_R 0x20013 : PK_CARD_R 0x20010
   Y           : n>=2 ? PK_Y_CARD_W 0x20012 : PK_CARD_Y 0x2000f
elif injured:                                         # INJURY_*
   carded none : booked ? INJURY_Y_CARD_N 0x2001e : INJURY_CARD_N 0x2001b
   R           : n==1 ? INJURY_Y_CARD_R 0x2001d : INJURY_CARD_R 0x2001a
   Y           : n>=2 ? INJURY_Y_CARD_W 0x2001c : INJURY_CARD_Y 0x20019
else:
   pinch = foul in the fouler's own half (team(fouler)+0x254 = attack dir ±1; dir=+1 & x<0, or dir=-1 & x>=0)
           and ( |x| > 31.0  (0x1425a2500)  or  areaQuery(FUN_14042d5e0)->+8 == 3 )
   if pinch && !excitePlayed && goalDiff(foulerSide)==WINNING (FUN_140a81230(...,1,0)==0)
         && ( period==3 && !minuteInPeriod in [0,30] (FUN_140a81040(.,.,0,0x1e))  || period==7 ):
       carded none -> EXCITE_CARD_N 0x20017 ; R -> EXCITE_CARD_R 0x20016
       Y -> n>=2 ? EXCITE_Y_CARD_W 0x20018 : EXCITE_CARD_Y 0x20015
   elif Y && MI+0x15b8 && n==1      -> FOUL_WARN_CARD_Y 0x20007
   elif rec.card == 2 (warning)     -> booked ? FOUL_CMN_Y_CARD_N 0x20006 : FOUL_WARN_CARD_N 0x20008
   elif pinch:
       carded none : booked ? PINCH_Y_CARD_N 0x2000e : PINCH_CARD_N 0x2000b
       R : n==1 ? PINCH_Y_CARD_R 0x2000d : PINCH_CARD_R 0x2000a
       Y : n>=2 ? PINCH_Y_CARD_W 0x2000c : PINCH_CARD_Y 0x20009
   else:
       carded none : booked ? FOUL_CMN_Y_CARD_N 0x20006 : NONE
       R : n==1 ? FOUL_CMN_Y_CARD_R 0x20005 : FOUL_CMN_CARD_R 0x20003
       Y : n>=2 ? FOUL_CMN_Y_CARD_W 0x20004 : FOUL_CMN_CARD_Y 0x20001
```
- OBS: the yellow branch uses `CMP CL,2; CMOVNC` (count ≥ 2 ⇒ W); the red branch `CMP CL,1; CMOVNZ`
  (count == 1 ⇒ "_Y_" variant) — raw @0x14047f47e..0x14047f4a5.
- "booked" in the no-card branches = `*(u8*)player(MI+0x1590) == 1` (the fouler holds exactly one yellow).
- INF(hi): "W" (white) = second yellow → red; "_Y_" prefix = the fouler was already booked (those scenes open
  with BEJUDGED + REFEREE_ONCOMING, see §1.4).
- INF(med): "pinch" = a foul that stops a dangerous attack in the fouling team's defensive third (|x| > 31 m from
  the centre line, or the area query returns 3).
- Rule-faithfulness: card colour / count logic is rule-faithful (second yellow = red); EXCITE / PINCH / WARN are
  game-specific presentation abstractions.

### 1.4 Scene → steps → cut groups → variants (OBS, `table_foul.bin`)

All scene steps have kind 3, fade 0.5, flags 0, no triplets, w2 = −1; second-to-… steps chain cut groups.
Scenes whose name contains `_Y_` start with `20017 FOUL_BEJUDGED` (kind 3) then `20019 FOUL_REFEREE_ONCOMING`
(kind **0**, fade **0.0**: a hard cut, no blend) then the card cut. PK scenes start with `2001a ONCOMING_PK`,
EXCITE with `2001b ONCOMING_EXCITE`, PINCH with `20019 ONCOMING`.

| scene | steps (cut group ids) |
|---|---|
| 20001 CMN_CARD_Y | 20001 |
| 20002 CMN_CARD_Y_B | 20002 |
| 20003 CMN_CARD_R | 20003 |
| 20004 CMN_Y_CARD_W | 20017 → 20019(k0) → 20004 |
| 20005 CMN_Y_CARD_R | 20017 → 20019(k0) → 20003 |
| 20006 CMN_Y_CARD_N | 20017 → 20019(k0) → 20005 |
| 20007 WARN_CARD_Y | 20006 |
| 20008 WARN_CARD_N | 20007 |
| 20009/0a/0b PINCH_CARD_Y/R/N | 20019 → 20008 / 20009 / 20005 |
| 2000c/0d/0e PINCH_Y_CARD_W/R/N | 20017 → 20019(k0) → 2000a / 20009 / 20005 |
| 2000f/10/11 PK_CARD_Y/R/N | 2001a → 2000b / 2000c / 2000e |
| 20012/13/14 PK_Y_CARD_W/R/N | 20017 → 2001a(k0) → 2000d / 2000c / 2000e |
| 20015/16/17 EXCITE_CARD_Y/R/N | 2001b → 2000f / 20010 / 20012 |
| 20018 EXCITE_Y_CARD_W | 2001b → 20011 |
| 20019/1a/1b INJURY_CARD_Y/R/N | 20013 / 20014 / 20016 |
| 2001c/1d/1e INJURY_Y_CARD_W/R/N | 20017 → 20019(k0) → 20015 / 20014 / 20016 |
| 2001f/20 FATIGUE01(_OUT) | 2001c / 2001d |
| 20021 INJURY_DAMAGE01 | 2001e (never returned by the selector; INF: unused or reached elsewhere) |

Variant keys (T1): 0x43xx = weighted random (cumulative %), 0x0d00/0x0d01 = category parity (foul category → 0x0d00
always, so `CMN_CARD_Y_B` = `foul_cmn_card_y02`, PINCH/PK `_y01`/`_r01` etc. are fixed), 0x0100 = stadium fallback.
Examples: CMN_CARD_Y = y01 (16 %) / y03 / y04 / y05 / y06 / y07 (17 % each; keys 0x430f,1f,30,41,52,63);
CMN_CARD_R = r01..r04 (25 % each); BEJUDGED = 8 × 12.5 %; REFEREE_ONCOMING = 10 × 10 %.

**New format finding (OBS):** T1 `b0` is a **slot bitmask** of the humans used by that variant
(e.g. 0x400002 = slots 1 and 22 for `foul_cmn_card_y01`; 0x808 = slots 3, 11 for `fatigue01`); it is OR-ed over the
scene's steps and drives human creation in `FUN_141f5f330` (§2).

### 1.5 Cut timing and props per variant (OBS, `fdc_decoded.json`; ticks are 1/60 s)

`maxTime` = cut length. Prop timings are Timing records on slot 22 (types from the enum registered in
`FUN_141f6b5d0`: 1 REF_CARD_Y, 2 REF_CARD_R, 3 REF_NOTE_Y, 4 REF_NOTE_R, 5 REF_PEN, 6/7 WHISTLE_MOUTH/HAND,
0xc HUMAN_DISP, 0xd REPLAY_RECORD, 0xe CAMERA_TARGET_HEIGHT_ADD, 0x11/0x12 crowd anime player side / other side
with sounds `demo_foul_idole/anger/yale`).

| cut | ticks | actors slot@(x,z)rotY | props on 22 (start–end) |
|---|---|---|---|
| foul_cmn_card_y01 | 240 | 1@(2.57,2.27)r234, 22@(−0.45,4.54)r234 | CARD_Y 30–259, NOTE_Y 260–750 |
| foul_cmn_card_y03 | 410 | 1@0, 2@(2.32,0.31), 22@(−0.22,4.66) | CARD_Y 40–220, NOTE_Y 220–720, PEN 240–720 |
| foul_cmn_card_y04 | 300 | 1@(0.61,−0.19)r300, 22@(−0.21,4.34) | CARD_Y 28–192, NOTE_Y 193– |
| foul_cmn_card_y05 | 460 | 1, 2@(−4.99,7.05), 22@(−0.59,6.13) | CARD_Y 5–216, NOTE_Y 216–, PEN 295– |
| foul_cmn_card_y06 | 310 | 1@(−0.04,1.88), 3@(3.30,0.63), 22@(0.05,5.13) | CARD_Y 40–160, NOTE_Y 160– |
| foul_cmn_card_y07 | 420 | 1@(0,−3.51)r180, 3, 12, 22@(0.06,4.80)r180 | CARD_Y 90–270, NOTE_Y 270–, PEN 300–450 |
| foul_cmn_card_y02 (Y_B) | 340 | 1@(0,−2.49), 22@(0.01,2.68) | CARD_Y 40–340 |
| foul_cmn_card_r01..r04 | 390/420/380/320 | 1, 2 or 3, (12), 22 at 2.0–5.2 m | CARD_R from 5–40 for ~180–270 ticks, then NOTE_R/PEN |
| foul_cmn_card_w01..w03 | 620/520/480 | 1, 2–4 or 11–12, 22 | CARD_Y ~7–30 → NOTE_Y → CARD_R ~130–190 → NOTE_R |
| foul_card_n01..n04 | 460/450/360/540 | 1, team-mate/opponents, 22 | no card |
| foul_warn_card_y01 | 480 | 1@0 r90, 22@(0.82,4.41) | CARD_Y 250–340, NOTE_Y 340–, PEN 370– |
| foul_warn_card_n05..n07 | 300/480/630 | 1, (12), 22 | none |
| foul_beJudged01..06 (+_cam) | 120 each | 1@origin only | — |
| foul_referee_oncoming01..07 | 120 each | 22 (+5 or 12) | — |
| foul_referee_oncoming_pk / _excite | 90 | 22 | — |
| foul_pinch_card_y01/y02 | 480/360 | 1, 2, (12), 22 | CARD_Y 30–190 / 0–315 |
| foul_pk_card_y01 | 660 | 1..5, 22@(−0.92,7.39) | CARD_Y 411–800 |
| foul_excite_card_y01/r01 | 330 | 1, 3, 11–13, 22 | card 174–332, note 333–440 |
| foul_injury_card_y01 (+player01..05 layer) | 550 | 1, 2, 22 + 11, 12 from the layer fdc | CARD_Y 0–170, NOTE_Y 170–300, PEN 200–272 |

Referee distance at card show: 2.0–7.4 m from slot 1, typically 4–5 m (OBS, the table). Cameras target slots 22 and 1
(`targetKind 6`, `targetNo [22,1]` or `[1,22]`).

---

## 2. Slot binding (FixDemoMatch human creation)

### 2.1 What is created (OBS)

`FUN_14047c500` → `FUN_141f5e730(sceneList, side=MI+0x1558)` allocates a 0x290-B `FixDemoMatch` (vtable 0x142ba6bc0,
side at +0x38) and runs `FUN_141f5a8b0`: vtable +0xc8, +0x18 `FUN_141f616f0` (expand steps), +0x70 `FUN_141f5f330`
(build human requests), +0x60 `FUN_141f60890`. Human requests are 8-byte records
`{u32 id, u8 kind, u8 side, u8 idx, u8 flag}` in two vectors, `+0x78` (actors) and `+0xa8` (role / camera-target map).

- `FUN_141f5f330` ORs the T1 `b0` slot masks of all steps and dispatches on the category byte of the scene id:
  2 → `FUN_141f5fa90` (foul), 3 → `FUN_141f5fc00` (goal), 4 → `FUN_141f60140` (pk), 5 → `FUN_141f5f480` (change),
  6 → `FUN_141f60590`, 7 → `FUN_141f5f610`.
- **Foul (`FUN_141f5fa90`, OBS):**
  - `+0x78` actors = default list (`FUN_141f561a0`): slot i (0..10) = `{kind 0, side 0, idx i}`, slot 11+i =
    `{kind 1, side 1, idx i}`, plus `FUN_141f56700(+0x78, 0x16, 0, 1)` = slot **22 referee** (kind 2 = official).
  - `+0xa8` role list: referee `{0x1116, kind 2, side 0, idx 0}` (via `FUN_141f56800` = `jmp 0x15b59a660`, a protected
    stub read from the live process: it is the 5-argument push), and for every slot bit set in the mask:
    slot 1 → (side 0, idx 1), 2 → (0,0), 3 → (0,2), 4 → (0,3), 5 → (0,4), 11 → (1,1), 12 → (1,2), 13 → (1,3), 14 → (1,4).
- `FUN_141f577b0` creates each actor: kind 2 → `FUN_141f58440` (official), 9 → `FUN_141f57e60`, 10..16 →
  `FUN_141f57a00`, else `FUN_141f58160(slot, kitKind, side, idx)`. The FixDemoHuman (0x578 B, vtable 0x142bb1048) is a
  **separate model** built from the condition context's player record `ctx + 0xf8 + side·0x2830 + idx·0x100`
  (appearance copy, `FUN_141f58600` + `FUN_141f7de00`). `ctx` player records are filled by `FUN_141f52ad0` in
  **on-pitch order** (`team+4+i·4` = roster index of pitch slot i; `FUN_140a3ae30`).
- Match side, during the scene (`FUN_14047e140`, run while scene ids 0x20001..0x20021 play, OBS):
  - subject = `DC+0x04` (the fouler from §1.1), overridable by `req+0x10` / `MI+0x178c`;
  - DC per-human state `DC+0x24[i]` (25 ints, default 3) is set 0 for the subject and for every `+0xa8` entry with
    kind < 2 (human index `side·11 + idx`), and `DC+0x08[i]` flag = 1 for them and for officials 22, 23, 24;
  - every **other** match player within **10 m** (0x142588210) of the scene point `MI+0x1318` or of the subject is
    moved by `FUN_14047d7f0` to a point **7 m** (0x14259bdf0) away along his bearing; if that point is off the pitch
    the bearing is turned 180° and the distance doubled; players whose path/target passes within 3 m (0x14258c264)
    are also displaced. When `MI+0x1554 == 6` (PK) displaced players get state 1.

### 2.2 Effective binding (what the data and code together say)

| slot | role (data convention, OBS from 78 fdcs) | code mapping (OBS) |
|---|---|---|
| 1 | carded / subject player, at the scene origin in most cuts | role (0,1) |
| 2–5 | team-mates of slot 1 (protest, hold back) | (0,0),(0,2),(0,3),(0,4) |
| 11–14 | opponents (victim side; injured player = 11 in `foul_injury_*_player0x` layers) | (1,1)…(1,4) |
| 22 | referee (card, notebook, pen props; always present when a card is shown) | official idx 0 |
| 23–25 | linesmen / 4th — not used by foul cuts | |

- **OPEN (the exact match-player ↔ slot resolution):** the role pairs above are literal constants in `FUN_141f5fa90`
  and the ctx player table is in pitch order, so with the code as read slot 1 would be *home pitch-slot 1*. That
  contradicts the data (slot 1 = carded player) and the subject logic in `FUN_14047e140`; a remap must happen in code
  not yet found. Tried: every writer of request bytes 5/6 in 0x141f40000–0x141f80000 and 0x140470000–0x1404c0000
  (`FUN_141f56fd0` = T2 `trip[]` rewriter, `FUN_141f5bb20` = save/replay serializer, `FUN_140482db0` = substitution);
  ctx side-array writers (`FUN_141f52ad0`, `FUN_141f51630`, `FUN_141f5e930`, `FUN_141f608f0`); team order writers
  (`FUN_140a3b3a0` only, at team init). Next: trace `FUN_141f7e400` / `FixDemoHuman` update (vtable 0x142bb1048)
  for a read of `DC+0x04` or `DemoPlayerInfo`, or capture a live foul scene (needs the game in a match).
- **INF(med) recommendation for GodotFootball:** bind slot 1 = fouler (the carded / subject player `DC+0x04`),
  slots 2–5 = the fouler's nearest team-mates, slots 11–14 = nearest opponents (11 = the victim), 22 = referee.
  This reproduces every authored cut; the nearest-first ordering is INF(lo).

---

## 3. Placement

- **Anchor (OBS + INF):** foul scenes all use step kind 3. The foul DemoInfo carries `pos = (0,0,0)` (§1.1), so the
  scene origin is not the event-descriptor position. The match code clears a 10 m disc around `MI+0x1318` and around
  the subject's position (§2.1). INF(med): the scene origin is the subject (fouler) at his current position and
  facing, with the referee placed by the cut (2–7 m from slot 1). Not traced: the FixDemoMatch origin function
  for kind 3 in the foul category (goal kind 3 = `FUN_1404b77d0`, a goal-specific anchor).
- **Actors are teleported, not walked in (OBS):** demo actors are new FixDemoHuman models placed at their Human record
  `position/rotY` (offsetType 1 on all foul records) and the match players they stand for are flagged in DC; the
  match players in the way are relocated instantly by `FUN_14047d7f0` (position written, no move command).
  Transitions between steps use the T2 `fade` (0.5 s) except the 0-fade hard cuts into `REFEREE_ONCOMING` (§1.4).
- `correctRateForHeight`/`humanHeightRange` behave as in `pes-re-cutscenes.md` §3.

---

## 4. Without a scripted scene (the common foul)

### 4.1 Step program built for a foul (`FUN_14044eb10`, OBS)

`FUN_140451fd0(Flow, 7)` resets the program (all 30 steps = 0x48), then:
- `MI+0x1574 == 10` or `MI+0x1775` set: `[0x10, 0x45]` + restart (abandon / special).
- **offside (`MI+0x1588 == 1`):** `FUN_140452260(Flow, kind 2)` → `[3, (0xc,0x45 if p4), 0xb, 0x45]`, then
  `0x11`, then (unless `MI+0x177f`) `[0x1d, 9?, 0x1f]` (9 only if a fatigue-injured player exists), then if a late card is
  pending (`FUN_140444060`, §4.5) `FUN_140452260(Flow,1,1)` = `[3, 6, 0x45]` and `0x20` if `Flow+0x57f8`; then
  `[3, 8]` if a substitution scene is due (`FUN_140482210 == 2`, step 8 = `FUN_140446f80` → `FUN_140484a10` substitution
  demo).
- **foul:** if `FUN_140444d20` (INF: advantage still running) → push `0x18` (`FUN_14044dda0`) and return.
  Else `FUN_140452260(Flow, kind 1)` → `[3, 5 (or 0xb if MI+0x1588==1, 6 if late card), 0x45]`, `0x11`,
  `[0x1d, 0x1f]` unless `MI+0x177f`, `[3, 0x11, 8]` if substitution scene due.
- Then `FUN_1404524e0(Flow, …, 0xe)`: `[0x23, 0x16, 0x12, 0x13, (0x17 if MI+0x1554==5), 0x14, 0x15, 0xe, 0x34, 0x19, 0x1a]`.

### 4.2 Step timings (OBS)

| step | what | timing |
|---|---|---|
| 3 | stop stamp | 1 frame |
| 5 | foul hold / scene | **no scene: 2.0 s** (`Flow+0x868 = 0x40000000` when id == 0x20022, @0x140446a9b); with scene: hold base `FUN_14044e460(kind)` = 3.0 s (`MI+0x1588==1`) or 5.0 s, and the step ends when the FixDemo pointer `Flow+0x1e0` is cleared (scene finished) — `FUN_14047c2d0` case 0. Frames = seconds × `FUN_141e532d0()` (gameSpeed). |
| 0xb | offside / non-scene stop | base `FUN_14044e460(5)` = 2.5 s, **3.0 s when `MI+0x1588==1`** (`Flow+0x868 = 0x40400000`); +2.5 s (0x14259bde4) and a random extra `rand(gameSpeed·100)/100 + 10` frames when `FUN_1404441d0` (INF: injured player down); ends when time elapsed and no demo |
| 6 | late card scene | `FUN_14044e460(5)` = 2.5 s base + scene |
| 9 | fatigue scene | 2.5 s base + scene |
| 0x45 | `FUN_140446350`: releases the MI write lock, `FUN_140a3cb20(MI,0)` | 1 frame |
| 0xc / 0xd | fade-in, 0.5 s (0x142565600) | `FUN_1404489d0` |
| 0xe | fade-in, 1.0 s (0x14256ae1c) | |
| 0xf | fade-out type 6, 0.5 s | `FUN_140448c90` |
| 0x10 | fade-out type 2, 0.1 s (0x142588200) | |
| **0x11** | **fade-out type 4, 0.1 s** — the post-foul wipe | |
| 0x1d / 0x1f / 0x20 | substitution / team-management windows (`DAT_1436f5118` UI callbacks, events 0x1050004/0x105000b/0x105000c) | wait for UI |
| 0x1a | `FUN_14044d2e0`: restart armed (reads `MI+0x1554/0x1558/0x173c`) | — |

`FUN_14044e460` table (OBS, stack array from 0x14259ce60/0x14259ce50): kind 0 → 8.0 s, 1 → 5.0, 2 → 3.0, 3 → 0,
4 → 7.0, 5 → 2.5, 9 → 0; overridden by a player in injury state 0x14 (`player+4+8 ≤ 2`) with that player's u16 at
`playerDB+0x24` (if `playerDB[+1]=='O'`) or 5.0 s.

### 4.3 What players do in the hold (OBS: `FUN_1405069a0` → `FUN_140504d20` → `FUN_1405ad120`)

`DC+0x88` = out-of-play kind (1 foul, 2 offside, 0 generic, 4/5/7/8 others, 9 none) selects a per-player role writer;
the role goes to `DemoPlayerInfo+0x24` (`FUN_140600dd0`) and drives the `ANIME_DEMO_OUTOFPLAY` / `dm_oop_*` clips.
Foul (`FUN_1405ad120`, per on-pitch player, OBS):
- fouler (`MI+0x1590`) → **0x0f**; victim (`MI+0x1594`) → **0x10**;
- within **25 m** (0x14259be20) of the foul spot: fouler's team → **0x11**, victim's team → **0x12**, else 0x13;
- beyond 25 m: within **17 m** (0x14259d614) of the referee (human 22) and on the fouler's team → **0x11**, else **0x13**.
- if `MI+0x15a5` (offside-goal/special) the generic writer `FUN_1405b1740` is used instead.
- INF(med): 0x0f = fouler reaction (appeal "no foul" — clips `dm_oop_*appeal_nofoul*`), 0x10 = victim (down/rise),
  0x11 = protest to referee, 0x12 = victim team-mates gather, 0x13 = idle/return. Per-frame role updates run in
  `FUN_140505ca0` (case 1 → `FUN_1405acd90`, not decoded).

### 4.4 Referee gesture (OBS: `Judge@action@anime` Update `FUN_1407a14e0`, chooser `FUN_1407a1ac0`)

Animetable rows (OBS, `atab.py` on the install's `Animetable.bin`, labels at 0x1434997f0):

| row | name | clip (motionNo) |
|---|---|---|
| 179/180 | judge_point_to_1 / _3 | referee_1_0_* / referee_3_0_*_hand_middle_point (m2960–2963 / m2966–2969), 6 angle columns |
| 181/182 | judge_point_to_1_low / _3_low | referee_*_hand_low_point (m2959/2961, m2965/2967) |
| 183/184 | judge_advantage_1 / _3 | referee_advantage_1_1_jog m2970 / referee_advantage_3_3 m2971 |
| 185 | judge_kickoff | referee_kickoff_start_0 m2977 |
| 186 | judge_game_end | referee_game_end_1_0_hand_up_whistle m2972 |
| 187 | judge_standwait | referee_stand_0 m2978 |
| 188 | judge_whistle_0_0 | referee_stand_0_whistle m2980 |
| 189 | judge_whistle_2_2 | referee_2_2_000_whistle m2964 |
| 190 | judge_fk_indirect_start | referee_stand_0_hand_up_whistle m2979 |
| 191 | judge_fk_indirect_loop | referee_hand_up_0 m2973 |
| 192 | judge_fk_indirect_rotate | referee_hand_up_0_f060 m2975 (c6) / _060 m2974 (c11) |
| 193 | judge_fk_indirect_down | referee_hand_up_0_hand_down m2976 |

Chooser (by referee command `cmd+0x85c`; `v` = referee speed metric from `+0xac0` vtable+0x20):
- 1/2/3 (play-on pointing): `MI+0x1554==3` → point_low (181 if v<16 else 182); `==7` → game_end; else point (179/180).
- 4: advantage, 183 if v<16 else 184.
- 5 (stop): `MI+0x1554==1` → kickoff; **direct offence** (`MI+0x1588` ∉ {1,3}) → whistle 188 (v<11) / 189;
  **indirect** (`MI+0x1588` ∈ {1 offside, 3}, `FUN_140a63940`) → 190 start → 191 loop, 192 rotate when the angle to
  the target point (`FUN_140a6cf70(G+0x40,10)`) exceeds 45°, 193 down once the restart is taken.
- the new row is entered only when the current clip has run to its blend point (`FUN_1407a1d40` vs vtable+0x38);
  the point direction is matched with angle buckets `FUN_1406de990`, transition from `FUN_1406b1f00` (default 20).
- **No card clip exists in the referee's in-play set** (OBS: no `referee_*card*` motion); cards are shown only by
  the fdc scenes (props `card_yellow/red` via TYPE_REF_CARD_Y/R on slot 22).

### 4.5 Late card after advantage (OBS `FUN_140444060`)

When the next stop comes (foul kind `MI+0x1574==4`, or offside `MI+0x1574==5 && MI+0x1588==1`), the advantage
state `MI+0x1690` ∈ {7..0x13} with bit mask 0x883ff, and fewer than 16 (time units of `FUN_140a6bd60`, INF: seconds)
have passed since `FUN_140a3bd80(MI)`, step 6 plays `FOUL_CMN_CARD_Y_B` (= `foul_cmn_card_y02`, 340 ticks).

### 4.6 Restart timing constants

`freekick.o` `suspendTime` 3.0 s / `waitTime` 0.0 (see `pes-re-setpiece-referee.md` §3) govern the set play after
step 0x1a; not re-derived here.

---

## 5. Offside

### 5.1 How an offside is represented (OBS)

- Out-of-play reason `MI+0x1574 == 5` (same as a foul) with foul kind **`MI+0x1588 == 1`**; the offside player is
  `MI+0x1590` (fallback `match+0x2470c`, `FUN_14047feb0`), the offence position is `MI+0x155c` (x) / `MI+0x1564` (z)
  (read by the linesman, §5.3).
- `MI+0x1574 == 5 && MI+0x15a5 != 0` is the **offside goal** (goal scored then disallowed): it switches the goal
  builder to `GOAL_S_OFFSIDEGOAL_START` (0x300f9, see `pes-re-cutscenes.md` §4.3), the stop step to 4, and the
  out-of-play role writer to the generic one; the reason strings `OFFSIDE_GOAL_START/LINESMAN/CELEBRATE/OBJECTION`
  (0x1425d8048..) are its phases (INF hi).
- Offside line used by the linesman (OBS `FUN_140a38f20`, RefereeLinesman vtable 0x142642348 slot 26): per side a
  record at `registry(+0x7e0)->+0x18 + 0x44 + side·0x14` = `{f32 lineX, i32 playerIdx, …}`; side chosen by
  `team0+0x255`. In play the linesman's look-at target is that player (the second-last defender) whenever the ball is
  past him; when stopped with `MI+0x1588 == 1` he looks at `(own x, MI+0x1564)`.

### 5.2 Detection — OPEN

Not located: the moment the line is evaluated, the level tolerance and the passive/active rule. Tried: writers of
`MI+0x1588` / `MatchControl+0x2944` (only struct copies `FUN_140430c30`, resets `FUN_140a571f0`/`FUN_140a57750`);
`lea` sub-struct pointers into 0x2908..0x2980 (BallInfo and situation-unit objects alias the same offsets);
`ObserverOffsideTrap@record` (`FUN_140a15e80`/`FUN_140a172e0`: records event 0x2a for successful *traps* from
team flags `+0xe4/+0x140`, not the judgement); float writers `[r+r*4+0x44]` in 0x140400000–0x140b00000;
`CameraOffside` slots (not decompilable in the GUI database). AI-side constants that are **not** the judge:
`checkOffsideLineDistX*`, `distXOffsideLineMin/Max`, `backOffsideLine` (`pes-re-setpiece-referee.md` §1).
Next: decompile the `MatchControl` vtable 0x1425984c0 slots 6/8/16/17 (`FUN_140423050`, `FUN_140423af0`,
`FUN_1404232e0`, `FUN_140423b60`) and the function that writes `MatchControl+0x2910` (restart kind) with value 6/FK,
or watch `MI+0x1588` live across an offside.
GF recommendation (rule-faithful, INF): judge at the pass/touch frame, attacker offside iff strictly ahead of both
the ball and the second-last opponent in his opponents' half; level = onside.

### 5.3 Call sequence (OBS)

1. **Linesman (`LinesmanAction` Update `FUN_1407a2920`, chooser `FUN_1407a2ea0`).** Animetable rows 213–238:
   | row | name | clip |
   |---|---|---|
   | 216 | linesman_flagup | assistant_3_0_f090_flag_up m5 (c4) / assistant_idle_0_flag_up m14 (c9) / assistant_3_0_090_flag_up m2 (c13) |
   | 217 | linesman_flagup_loop | assistant_flag_up_0 m6 |
   | 218 | linesman_flagup_appeal | assistant_flag_up_0_wave m12 |
   | 219 | linesman_corner_fromflagup | assistant_flag_up_0_flag_ck m7 |
   | 220/221/222 | linesman_offside_far/mid/near_fromflagup | assistant_flag_up_0_offside_far/middle/near m9/m10/m11 |
   | 223 | linesman_offside_cancel_fromflagup | assistant_flag_up_0_flag_down m8 |
   | 213/214/215 | throughin_right/left_hand, goalkick | assistant_*_flag_middle_point / _flag_gk |
   | 224/225 | stand_wait / linesman_0_0 | assistant_stand_0 m16 |
   | 226–232 | side_1_1/2_2/3_3, walk_1_1, run_2_2/3_3/4_4 | locomotion; 235–238 turns |
   | 233/234 | linesman_layer_flag / _switch | layer_assistant_flag_hand_R m2785 / _switch_R_L m2786 |
   - From idle (state < 2) a linesman command 7 or 2 (`cmd+0x85c`) raises the flag (216, angle column).
   - Flag up (state 2–3) with `MI+0x1588 == 1`: `w = halfWidth/3` (FieldInfo[1]/3.0 = 11.35 m); `s = −1` if the
     linesman's z < 0 else +1; `t = s · MI+0x1564`. `t > w` → **222 near**; `−w ≤ t ≤ w` → **221 middle**; `t < −w` →
     **220 far** (the flag points across the pitch to the third where the offence happened).
   - Flag up for a foul seen by the linesman (`MI+0x1588 == 0`): cmd 7 → 217 loop; cmd 2 → 218 wave, or 223 flag down
     once `MI+0x1198 − MI+0x1550` exceeds `gameSpeed·60/60 + 0.5` frames (≈ 1 s) with `MI+0x154c` set; cmd 6 → 218
     while more than `gameSpeed·3.0/60 + 0.5` frames of the current clip remain (OBS 0x14259a764 = 60.0,
     0x14258c264 = 3.0), else falls through to 223.
   - Other restarts while the flag is up: `MI+0x1554` 2–3 → 225 stand, 4 → 219 corner flag; from idle `MI+0x1554 == 2`
     → 213/214 throw-in direction by attacking team vs. linesman side, 3/4 → 215 goal kick / 216 flag up when the
     linesman is within 4 m (0x1425656c0) of the goal line, else 232 run.
   - Rows 223 and 218 play at **1.6×** speed while still in play (`MI+0x116c == 1`) (0x1425ab12c); default
     transition 0.2 s (0x142598578).
2. **Referee (`Judge` action, §4.4):** command 5 with `MI+0x1588 == 1` ⇒ `judge_fk_indirect_start`
   (`referee_stand_0_hand_up_whistle`, whistle + arm up) → `judge_fk_indirect_loop` (arm held) → `_rotate` to face the
   kick point when the angle exceeds 45° → `_down` when the kick is taken.
3. **Sequencer (§4.1):** `[3, 0xb, 0x45]` — hold **3.0 s** (`Flow+0x868 = 0x40400000` for `MI+0x1588==1`, OBS
   `FUN_140447690`) — then `0x11` wipe 0.1 s, substitution window, restart steps, fade-in 1.0 s. **No fixdemo scene is
   requested for an offside** (step 0xb never calls `FUN_14047feb0`); a pending advantage card can add step 6.
4. **Player reactions during the hold (`FUN_1405ad720`, DC kind 2):** offside player → **0x15**; on his team the last
   passer (newest pass event of types 1–3 or 0x10 whose player ≠ offside player, from `FUN_140a3e190`) → **0x16**,
   others → **0x19**, or **0x17** when within **13 m** (0x1425c5948) of the offside player and he is the ball owner's
   team target; defending team: players whose formation role (`team+0x4c00[slot]`) is 1 or 2 → **0x18** (INF med: the
   back line appealing / raising arms), others → **0x13**.
5. **Indirect FK placement:** not traced; INF(med): `MI+0x155c/0x1564` (the offence position) is the restart point.
6. **Camera:** `CameraOffside@match` (vtable 0x142bd3df8) and the `OffsideReplay` string exist; selection not traced.

---

## 6. Live observation

Not possible in this session. `pes_state.py` at the start of the pass: `STATE=MATCH_FROZEN` but `*(G+0x270) == 0`
(no match-info object; read 2026-10-13 between 18:43 and 19:00 CEST); at 19:11 CEST: `STATE=MENU`. No foul/offside could be observed
(input to the game is forbidden). Script for a future capture: `~/ghidra-projects/scratch/FoulChoreographyRE/live.py`
(dumps `MI+0x1550..0x15d0`, period and out-of-play reason).

---

## 7. Open items (with what was tried)

1. **Slot → match-player resolution for foul scenes** (§2.2): role pairs are constants; remap not found (tries listed
   there). Recommendation given as INF.
2. **Scene origin for kind-3 foul steps** (§3): `FUN_140476c50` is the goal anchor; the foul DemoInfo has pos (0,0,0).
   Next: the FixDemoMatch kind-3 origin (FixDemoManager vtable 0x142ba6800 slots) or a live capture.
3. **Offside detection** (§5.2).
4. Meanings of `MI+0x15b8` (WARN = advantage card, INF med), `rec+0x42`, card value 2, `MI+0x1690` advantage states,
   `FUN_14042d5e0` area value 3 (pinch), `FUN_140444d20` (advantage-running branch, step 0x18).
5. Steps 0x12–0x17, 0x23, 0x34, 0x19 of the restart program were only skimmed (UI/net/substitution plumbing and
   restart setup); the frame where players are teleported to set-piece positions is inside these (INF: 0x16/0x17
   `FUN_140449d20` / `FUN_1404a9ce0`, the latter only when `MI+0x1554 == 5`).
6. `FUN_1442313a0` (card HUD flag) lies outside the dumped image (0x144…); `FUN_141f56800` jumps into a protected stub
   at 0x15b59a660 (read live: a plain vector push with 5 byte args).

## 8. GodotFootball / GF divergences to fix

- GF has no cutscene gate: PES plays **no scene** for an ordinary foul (no card, fouler not booked, not in the
  pinch zone) — only a 2.0 s hold with reactions, then a 0.1 s wipe and teleport to the restart.
- Cards are only ever shown inside a scene; the referee has no in-play card clip.
- Offside has no scene: whistle + raised arm (indirect gesture), linesman flag near/mid/far, 3.0 s hold, wipe.
- One EXCITE scene per match at most; late-match leading-team pinch fouls only.
- Second yellow = red via the `W` scenes; booked-player no-card fouls use the BEJUDGED → ONCOMING (hard cut) intro.

---

## 9. Stoppage timings and reaction roles (FoulChoreographyRE-2, 2026-10-13)

Extends §4/§5/§7. Where this section contradicts §0.2, §4.2, §5.3 step 3 or §8, **this section wins** (see 9.9).
Evidence: decompiles `~/ghidra-projects/scratch/FoulChoreographyRE2/out/{a,b,c}.c` (+ `FoulChoreographyRE/dec/`),
raw floats `rf.py` and disassembly `pd.py` on `pes_full.bin`, live step durations from `steps.py` over the sibling
captures `SetPieceChoreoRE/live.jsonl`, `live_match1.jsonl`, `ThrowInRE/seq.jsonl`, `seq2.jsonl` (`fr` = 54 Hz frame).

### 9.1 Tick model of the step program (OBS)

- `FUN_140445e90` calls the runner `FUN_14044c880` once; the runner re-enters the current step once per call and,
  on advance, zeroes `Flow+0x900` (frame counter), `+0x8f2` (sub-state), `+0x8f4`, `+0x8f8`. Live: fade-in `0xe`
  lasts exactly 5 + 54 = 59 frames (n = 8), so **one step call = one 1/54 s logic frame** — the same tick as
  `pes-re-tactics-clock.md` §2.5 (gs = `GetGameSpeed()` 54.0 @0x141E532D0, fps = `GetFps()` 54 @0x141E532E0).
- Three timer shapes, all in seconds × 54, none read the match clock or `match_time`:
  1. **holds** `FUN_1404534f0`: `++Flow+0x900; done = gs·hold_s < counter` (float) → `hold_s·54 + 1` frames;
  2. **fades** `FUN_1404489d0` / `FUN_140448c90`: byte counter `(char)(fps·dur_s)` (truncated): 0.1 s → 5, 0.5 s → 27,
     1.0 s → 54; a fade-**in** first waits a literal 5 frames (`Flow+0x8f3 = 5` in sub-state 0);
  3. **scene start gate** `req+8 = (int)(fps·3 / env+0x5c)` (×6 for injury scenes), counted down by `FUN_14047eb00` —
     the only stoppage timer divided by the speed float `*(gCtx+0x2C0)+0x5c` (tactics-clock §2.5). The ×3 / ×6 are
     `lea` address arithmetic (OBS @0x14047ff23, @0x14047ff57), not data: **not readable at runtime**, code-structural.
- GF conversion: PES frames → ms = `f·1000/54`; every value below is seconds, so GF (10 ms tick) takes seconds.

### 9.2 Step 5 — the foul hold (OBS @0x140446960, @0x14047c2d0 case 0, @0x14047eb00, @0x140480910)

```
sub0: FUN_14047d5d0(Flow)            Flow+0 = 0 (OBS 0x14047d5e7 mov [rcx],edi), demo Flow+0x1e0 freed
      FUN_14047feb0 → FUN_140480910(req=Flow+0x6d8, …, r9=Flow)   scene id → req+0; ONLY if id != 0x20022:
            Flow+0 = id and FUN_14047c500 builds the FixDemoMatch (Flow+0x1e0)
      Flow+0x868 = FUN_14044e460(MI+0x1588==1 ? 2 : 1)              table[1] 5.0 s / table[2] 3.0 s
      if Flow+0 == 0x20022: Flow+0x868 = 2.0                         DEAD STORE: Flow+0 is 0 when no scene
sub1 (per frame): FUN_14047c2d0(Flow, skip, 0), case 0:
      no demo → FUN_1404534f0(Flow, Flow+0x868)  → sub2 when elapsed
      demo    → returns 0 (unless skip); step then runs FUN_14047eb00 (start gate) and sets Flow+0x868 = 0
sub2: demo == null → step done
```
- **No scene: 5.0 s** (`0x14259ce60` table index 1, OBS raw `0x40a00000`) = 271 frames + 2 sub-state frames.
  Live: FK at fr 14286, step 5 = **272 frames** (OBS). Static, except the `FUN_14044e460` overrides in 9.3.
- **Scene: start gate + scene**, the 5.0 s is unused (zeroed while the demo exists; after the demo clears, the hold
  is 0 → 1 frame). Live: 619 and 746 frames (two scene fouls).
- The kind-2 (3.0 s) branch is unreachable from the builders read: `FUN_140452260` sends `MI+0x1588 == 1` to step
  0xb, never 5 (OBS @0x140452260). INF(high).
- **Scene start gate** (`FUN_14047eb00`, OBS): counter 162 (324 injury) − 1/frame; waits while counter > fps
  (≥ 2.0 s; injury ≥ 5.0 s), then starts (`req+0xc = 1`) when
  - ordinary foul/card scenes: referee (human 22) within **5.0 m** (`0x14259222c`) of the fouler `MI+0x1590`;
  - EXCITE 0x20015..0x20018: fouler within **2.0 m** (`0x142565604`) of the victim `MI+0x1594`;
  - injury (`FUN_140a3fe70`: 0x20019..0x20020): victim player bit 10 of `+8` set;
  - unconditionally when the counter reaches 0 (3.0 s / 6.0 s).
  ⇒ a card scene starts **2.0–3.0 s** after step 5 entry (**5.0–6.0 s** injury). Dynamic (referee approach).
- Scene length = Σ cut `maxTime` (§1.5, 1/60 s ticks) + 0.5 s step fades; variant by the 0x43 roll (§1.4). Dynamic.

### 9.3 Step 0xb — out-of-play hold: ball out, offside, generic (OBS @0x140447690, disasm 0x1404477e3..0x140447a2f)

```
sub0: Flow+0x868 = FUN_14044e460(5) = 2.5 s ; if MI+0x1588 == 1: Flow+0x868 = 3.0 (imm 0x40400000 @0x140447a25)
sub1: → sub2
sub2: if FUN_1404441d0()   (any of players 0..21 has byte player+0x34 set, FUN_140a73f10 = "down")
          Flow+0x5730 = 1 ; Flow+0x868 += 2.5 (0x14259bde4) ; Flow+0x5732 = rand(fps·100)/100 + 10
          (rand = FUN_140a92530, LCG x = x·0xd2dd4d1d + 0x4513 on Flow+0x5708+0xc)
sub3 (per frame): hold = Flow+0x5730 ? Flow+0x868 : FUN_14044e460(5)        ← re-read every frame
          done = counter > gs·hold ; if MI+0x137c: also done at once when no downed player
          downed-player branch: recovery test on his record (action byte 'O', u16 +0x1a vs Flow+0x5732) can clear
          Flow+0x5730 and reset Flow+0x868 = 2.5 ; after 0.5 s (0x142565600) Flow+0x29a4 = 1 and
          FUN_141aea140(FUN_141ae8950(0), 1, 0)  (INF med: medical/injury presentation request)
sub4: Flow+0x868 = 0, DC out-of-play kind 9, done
```
- **2.5 s for every 0xb stop, offside included** (OBS code: sub3 ignores `Flow+0x868` unless a player is down).
  Live: 30+ ball-out holds at 135–140 frames; three stop-kind-5 holds through 0xb (seq2 fr 31505/37691/40233 =
  139/140/138 frames) — the builder routes a stop-5 to 0xb only when `MI+0x1588 == 1`, so INF(high) these are
  offsides. One offside-class stop measured end to end: stop fr 31502 → armed 31777 (275 frames, 5.09 s) → in play 32035.
- **Someone down: base + 2.5 s** = 5.0 s (5.5 s offside). Live: 274-frame holds ×2 (live_match1 corner 46272,
  seq2 goal kick 15866) = 5.0 s + 4 sub-state frames (OBS).
- `FUN_14044e460(kind)` (OBS disasm 0x14044e460) also returns **0 s** when the DC subject (`DC+4` < 22) has
  player-object byte `+0x1400` set (meaning open), and, if any XI player is in state 0x14 (`FUN_140a73e40(p,1)`)
  with sub-state `+8 ≤ 2`, **that player's u16 `playerDB+0x24` seconds** when `DB[+1] == 'O'`, else 5.0 s
  (`0x14259222c`). Shared by steps 5, 6, 9, 0xb.

### 9.4 Wipe, fade-in and the restart tail (OBS)

| step | timing | static? | evidence |
|---|---|---|---|
| 0x11 wipe (fade type 4) | 0.1 s (`0x142588200`) → 5 frames | static | code + live 5 frames (every stop) |
| 0x10 wipe (type 2) | 0.1 s → 5 frames | static | throw-ins/goal kicks; abandon path of the foul builder |
| 0xe fade-in | 5-frame literal + 1.0 s (`0x14256ae1c`) → 59 frames | static | foul/offside builder always passes 0xe (`FUN_1404524e0(…,0xe)` @0x14044eb10) |
| 0xd / 0xc fade-in | 5 + 0.5 s (`0x142565600`) → 32 frames | static | abandon path (`MI+0x1574 == 10` or `MI+0x1775`) → 0xd |
| 0x1d, 0x16, 0x13 (+0x17) | 1 + 4 + 6 = 11 frames offline | UI-dependent | live FK ×2; substitution UI can extend |
| 0x14 + 0x15 teleport | 20 + 20 frames | static | live (setpiece-choreography §1.2) |
| 0x34, 0x19 | 2 + 16 frames (0.25 s `0x142592228`, +2.0 s with a caption) | caption-dependent | live |

- `MI+0x177f` (`FUN_140a3c020`) set ⇒ every fade completes on its first frame and the builder omits 0x1d/0x1f
  (OBS @0x1404489d0, @0x140448c90, @0x14044eb10). INF(med): presentation-skip mode.
- Wipe start → teleport start ≈ 16 frames; teleport start → armed (0x1a) = **117 frames (2.17 s)**.
- After armed the CPU taker decides. FK action at set-play counter **164 ×2** = `freekick.o.suspendTime` 3.0 s ×54
  = 162 + the same +2 latency as the throw-in's 81 + 2 → INF(high): `suspendTime` is the CPU FK taker's wait after
  armed (the code reading it was not decompiled).

### 9.5 Late card, step 6 (OBS @0x140446520, @0x140444060, @0x14044eb10)

- Built only in the offside branch of `FUN_14044eb10`: `FUN_140452260(Flow, 1, 1)` with r9 = 0 (OBS @0x14044ed50)
  → `[3, 6, 0x45]` after `0x1d [9] 0x1f`; the ball-out (stop kind 4) builder was not read.
- Window `FUN_140444060`: `MI+0x1690 ∈ 7..0x13` with mask 0x883ff, stop kind 4 or offside, and
  `(int)(FUN_140a3c050(MI+0x1168) − MI+0x17a4) / 60 / 60 < 16` (`FUN_140a6bd60` = two `/60`, OBS). Outside the
  window it clears the pending card (`Flow+0x57f8 = 0`, card fields reset, UI event 0x1050039).
- Step 6: scene `FOUL_CMN_CARD_Y_B` (= `foul_cmn_card_y02`, 340 ticks = 5.67 s) starts on its 2nd frame (`Flow+0x1d4`
  set directly, **no referee-approach gate**); step = **max(2.5 s, scene)** (`FUN_14047c2d0` case 1 with the 2.5 s
  timeout as `param_3`).

### 9.6 Whole stop, one ordinary foul without a scene (live FK fr 14286, every phase OBS)

| phase | frames | s |
|---|---|---|
| whistle hold before the stop (duels §3) | ⌊0.8·gs + 0.5⌋ = 43 (offside gs/2 = 27) + DelayWhistle | 0.80 |
| step 3 stop stamp | 2 | 0.04 |
| step 5 hold | 272 | 5.04 |
| 0x11 wipe + UI windows | 5 + 11 | 0.30 |
| 0x14/0x15 teleport | 40 | 0.74 |
| 0xe fade-in | 59 | 1.09 |
| 0x34 + 0x19 | 18 | 0.33 |
| **stop → armed** | **407** (14286 → 14693) | **7.54** |
| armed → FK action | 164 | 3.04 |

### 9.7 Reaction roles of the officials (OBS)

Officials are indices 22 (referee), 23, 24 (assistants); `FUN_141e53520(i) = i − 22` for 22..24 else 4 (OBS).
The official's record byte `+1` (`FUN_140a48910`) is his current action id: 0x5a LINESMAN, 0x5c JUDGE, 'O' = 0x4f.

**Who is involved** — `FUN_140a3a690(official)`:

| state | referee | assistant |
|---|---|---|
| already in LINESMAN (0x5a) | — | false |
| in JUDGE (0x5c) | true iff `MI+0x116c == 3` (set-play ready) and `MI+0x11b2` (set-play active) | — |
| in play (`116c == 1`) | `MI+0x154c != 0` | (`MI+0x1548` or `MI+0x154c`) and `FUN_140a3a500(a, MI+0x1544 == 1)` |
| stopped (`116c == 2`) | always | stop kind 4: **throw-in → `sign(a.z) == sign(MI+0x1564)`** (same touchline; 0 counts +); goal kick / corner → `sign(a.x) == sign(MI+0x155c)` (same half); other modes false. Stop kind 5 → `FUN_140a3a500(a, MI+0x1588 == 1)`. Other kinds false |

`FUN_140a3a500` (OBS): reference point = ball predicted ⌊gs·4.0/60⌋ = 3 frames ahead in play (`0x1425656c0`,
`0x14259a764`), the spot `MI+0x155c/0x1564` when stopped. Offside: the assistant in the **half of the offence**
(`sign(spot.x) == sign(a.x)`). Foul: same half **and** same touchline side **and** `|spot.z − a.z| < 14.0 m`
(`0x1425c48b8`) — an assistant flags a foul only near his touchline.

**At the stop** — `RefereeActionOutofPlay` (vtable 0x142642738), slot 6 `FUN_140a3a930` picks the action; latch
`+0x30` cleared on enter (slot 3 `0x1409e46b0`, OBS `mov byte [rcx+0x30],0`), set by slot 2 `FUN_140a3a220` the first
time the official's action byte is 0x5a/0x5c:
- referee, foul (stop kind 5, not offside): action 2 (approach) until within **4 m** (d² < 16.0 `0x14259be10`) of
  the target, then **JUDGE only if `MI+0x158c == 1`** (INF med: foul without card; cards are shown by the scene);
  referee, offside or any other stop: JUDGE at once; assistant involved: LINESMAN; uninvolved: 3, or 2/3 by
  distance at stop kind 3. Speed class while stopped: card `MI+0x158c ∈ {2,3,4}` → 3 beyond 4 m, else 2; 1 within.
- slot 18 `FUN_140a3a870`, first JUDGE/LINESMAN request only: gesture param **1 restart / 2 foul / 3 offside**
  (`MI+0x1574 == 5` → 2 + (`MI+0x1588 == 1`)), direction = attack direction `team(MI+0x1558; 2→1)+0x254` of the
  awarded side.
- ⇒ **each official signals at most once per stop** (latch), INF(high).

**At the restart** — `RefereeActionBasePosition` (vtable 0x142642558) Update `FUN_140a39a80` writes the command
`+0x30` (INF med: the `cmd+0x85c` that `FUN_1407a1ac0` reads; same 1–9 values). Enter (slot 3 `0x140a39cd0`, OBS):
cmd 9, `+0x34` 0, latch `+0x38` 0, indirect `+0x39` 0.
- `116c == 3`: at MODE 5 `+0x39 = FUN_140a63940(MI+0x1588)` (kind 1 or 3 = indirect, OBS); involved referee:
  **cmd 5 if the latch is clear or the restart is indirect**, latch = 1; else cmd 9.
- Chooser, cmd 5: MODE 1 → 185 kickoff; arm-raised sub-states 3/4 → 191, or 192 rotate when the angle to the
  restart point > 45° (`0x14259be30`); **direct → 188 `judge_whistle_0_0` if v < 11.0 (`0x14259bdfc`) else 189**;
  **indirect → 190 start (sub-state 1) → 191 loop**. A new row is entered only after the current clip's blend point
  `FUN_1407a1d40 = ⌊(2N/rate)·(gs/60) + 0.5⌋` frames (N = 10-bit motion-header field, INF frames at 30 fps).
- `116c == 1`: indirect arm (state 5) → cmd 9 when the newest record of event ring 0 has `+0x1c == 0` (INF: the
  first touch after the kick) → chooser 193 `fk_indirect_down`; idle referee + `MI+0x154c` rising edge (`+0x34` =
  last frame's value) → **cmd 4 advantage** (183/184 by v ≥ 16); involved assistant → cmd 2, or 7 when
  `MI+0x1544 == 1` (flag); 2 → 6 and 7 → 8 once `MI+0x1548 == MI+0x154c == 0`.
- ⇒ **direct restart: one whistle gesture per BasePosition entry; indirect: cmd 5 re-asserted every frame, arm held
  until the first touch.** Live: `referee_stand_0_whistle` (row 188) at set-play counter 2, once (setpiece §5.1).
- Assistants never get cmd 5; the restart whistle is the referee's alone.

### 9.8 PES timing → GF field (for implementation; no code changed)

| PES timing | value (source, read at runtime) | static / dynamic | GF field that will hold it |
|---|---|---|---|
| whistle hold | ⌊0.8·gs + 0.5⌋ frames, offside gs/2 (duels §3) | static + DelayWhistle | `pes::JudgeConstants::whistleHold_s` (already ported, referee.cpp:798) |
| stop stamp (step 3) | 2 frames | static | `buffer.stopTime` |
| foul hold, no scene | table[1] @0x14259ce60 = 5.0 s | static (9.3 overrides) | `FoulSequence::kWarningCutscene_ms` (6000 → 5.0 s; rename: it is a hold, not a cutscene) |
| scene start gate | fps·3/env+0x5c, open at fps remaining; referee ≤ 5.0 m @0x14259222c; EXCITE 2.0 m @0x142565604 | dynamic 2.0–3.0 s (5–6 s injury) | new offset inside `FoulSequence::CutsceneLength_ms` (card) |
| card scene | Σ fdc `maxTime` + 0.5 s fades | dynamic (variant roll) | `FoulSequence::kCardCutscene_ms` → per-scene from the fdc |
| card on screen | gate + intro (240 ticks for `_Y_`) + `REF_CARD_Y/R` start tick (5–411 /60 s) | dynamic | `GiveYellowCard/GiveRedCard(now + 6000)` (referee.cpp:853/857) |
| out-of-play hold | table[5] @0x14259ce54 = 2.5 s | static | corner / goal kick / throw-in `buffer.prepareTime = stop + 2000` (referee.cpp:186, 200, 227) and `SetPieceLaws::kOffsideCutscene_ms` (7000 → 2.5 s) |
| player down | + 2.5 s @0x14259bde4; offside base 3.0 s (imm @0x140447a25) → 5.0 / 5.5 s | dynamic (`FUN_1404441d0`) | new condition on the same prepare sites |
| wipe + UI | 0.1 s @0x142588200 → 5 frames, + 11 frames | static offline | `kRestartPrepareAfterReplay_ms`, `kOffsidePrepareAfterCutscene_ms` (1500 → ≈ 0.30 s) |
| teleport → armed | 40 + (5 + fps·1.0 @0x14256ae1c) + 2 + fps·0.25 @0x142592228 = 117 frames (+2.0 s caption) | static | `kTakeAfterPrepare_ms`, `kOffsideTakeAfterPrepare_ms`, `startTime = prepareTime + 2000` (2000 → 2.17 s) |
| CPU FK wait after armed | `freekick.o.suspendTime` 3.0 s (+2 frames) | static (INF) | taker delay after `buffer.started` (no GF field yet) |
| late card (step 6) | max(2.5 s, 340 ticks), at the next ball-out/offside stop inside the `MI+0x1690` window | dynamic | `IssueDeferredCards` (stop + 300, prepare + 6000) |
| restart deferral cap | **none** in the step program; set-play clock 54·30 frames after armed (54·15 with S17) for human pads only (throw-in §6.2) | — | `kMaxRestartDeferral_ms` stays a GF safety net: mark `RE-SHIM`, not PES |
| referee gesture holding the restart | **none** (no step waits on JUDGE; cards live in the scene) | — | `kRefereeSpecialHold_ms` (match.cpp:41) has no PES counterpart |
| replay between whistle and restart | **none** in any foul/offside program read or captured | — | `FoulSequence::kReplayLeadIn_ms` has no PES counterpart (`Flow+0x5e14`, set for some scenes, open) |

### 9.9 Killed hypotheses

1. **"No-scene foul hold = 2.0 s"** (§0.2, §4.2, §8; setpiece-choreography §5.1/§8.1): killed. The 0x20022 test reads
   `Flow+0`, which `FUN_14047d5d0` zeroes and only the scene branch of `FUN_140480910` writes (OBS); live 272 frames = 5.0 s.
2. **"Offside hold = 3.0 s"** (§0.2, §4.2, §5.3 step 3, §8): killed for the normal case. Sub-state 3 uses
   `FUN_14044e460(5)` = 2.5 s unless a player is down (OBS disasm 0x1404478d9..0x1404478fc); live 138–140 frames.
3. **"With a scene, the 5.0/3.0 s base is a minimum hold"** (§4.2): killed. The hold is zeroed while the demo
   exists; the step is gate + scene.
4. **"`MI+0x1588 == 0` is the direct foul"** (§1.2, INF hi): weakened — both live direct-FK fouls read `MI+0x1588 = 2`
   (`SetPieceChoreoRE/live.jsonl` `fk`). Kind 0 never observed.
5. **"The referee's gesture delays the restart"** (GF `kRefereeSpecialHold_ms`): no PES step waits on it.
6. **"PES caps restart deferral like GF's 30 s"**: no cap in the step program; the only 30 s clock runs after armed.

### 9.10 Still open

- `FUN_14044e460` zero-hold flag (subject player-object `+0x1400`) and `MI+0x137c` (end 0xb at once): meanings.
- Downed-player recovery test in 0xb sub3 (`'O'` action, u16 `+0x1a` vs `rand(fps·100)/100 + 10`) and what
  `FUN_141aea140(…,1,0)` shows.
- Late-card window unit: `FUN_140a6bd60` divides by 3600; whether the clock float is game seconds (→ hours, window
  always open) or 1/60 s (→ 16 game-minutes) needs one live read of `MI+0x17a4` against the clock. INF(low): minutes.
- `MI+0x158c == 1` as "foul without card" (gates the referee's on-the-spot JUDGE): one live foul read resolves it.
- How the JUDGE action's `cmd+0x85c` is fed from `RefereeActionBasePosition+0x30` (copy site not located).
- Late card on ball-out stops: the stop-kind-4 program builder was not read.
