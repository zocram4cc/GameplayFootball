# PES21 → GF sim motion mapping

Scope: how to drive visible PES match clips from the GameplayFootball (GF) simulation
(`<repo>/src/sim`, `onthepitch/player/humanoid/*`), i.e. what `ChoosePesClip(gfAction, params)
-> (motionNo, mirror, startTime)` must do. Read-only RE: the exe image, the install's own
`common/anime/` bins, the GF sources, and the prior report
[`pes-re-animselect.md`](pes-re-animselect.md) (cited as "AR"; §N = its sections). No PES table is
reproduced; every table is described by its layout and the generator script. Scratch:
`<scratch>/SimToPesMotionRE/` (16 MB: `anim_infos.json`, `motion_names.json`, `animetable_rows.json`,
raw decodes; the Ghidra project copy is on disk at
`~/ghidra-projects/scratch/SimToPesMotionRE/proj`).

Tags: **OBS** = observed, with how. **INF** = inferred, with confidence.

---

## 0. Headline facts

0. **Clip identity is the 13-bit `motionNo` (0..4388).** OBS, AR §0.1. Name table = 4,389 `char*` at
   image VA **0x14351e590** (re-read this session, 4,389 names). Clip `m` lives in
   `body_anime_file<m/439>.mtar`. Every selection table below keys on `motionNo` (usually stored
   `+1`, 0 = none).
1. **Match ganis run at 30 fps; root scale 1/16384 m per raw unit.** OBS, AR §0.2–0.3 and §6 of
   this report. `<repo>/src/rig/clip_pose.hpp` already carries both constants; the remaining
   GF-side import (`tools/pes21_import/gani_to_anim.py`, `PES_FRAME_MS = 1000/59.94`,
   `PES_POS_TO_M_GAMEPLAY` calibrated at 1/20480) is **2× wrong on time and 25 % short on
   distance** and must not be used for match clips.
2. **The Animetable row names are their own documentation.** OBS: 3,045 authored situation strings
   at **0x1434997f0** (re-read this session; count matches `Animetable.bin` exactly). The full
   grammar is §1.3; every row is `family_params` where the params are PES speed classes, the `yN`
   contact-height class, foot/part/side tokens and (via the column) the angle.
3. **The Freemove key is solved.** OBS from the raw `Freemove.bin` (3,874 records, key-sorted, §2):
   the *state code* is a 7-slot decimal compound
   `[family][speedIn][sep][style][speedOut][sep][angleDigit]`, leading zeros dropped, and the table
   key is `state·10 + turnDigit`, `turnDigit` 0..4 = +0/45/90/135/180°, 5..9 = the mirrored side.
   The two open digits from AR §9 are: slot 2 = style (0 run, 1 walk, 2 parallel, 3 side, 4 rot180,
   5 back; §2.2), slot 5 = speedOut (0/2/3/4); the seps are 1 (locomotion families) or 0
   (near/delay families). Same conclusion AR reached for slot 1/speedIn, now certain.
4. **GF's query parameters translate 1:1.** OBS by comparing GF `SelectAnim` (`humanoid.cpp`
   ~:1983-2130) with the PES row grammar: GF's `(functionType, incomingVelocity, desiredVelocity,
   relative angle, ball height, ball direction, foot/side)` is the same tuple PES rows and Freemove
   states are built from. The unit maps are: GF buckets {idle 0, dribble 3.5, walk 5.0, sprint 7.0}
   m/s → PES digits {0, 2, 3, 4} (dribble/JOG, walk/RUN, sprint/DASH; §4.1); ball height cm → `yN`
   (§5.4, re-measured, matches AR §4.2 plus GK y10/y11).
5. **All per-clip data GF needs already exists in PES metadata**, no curve measurement required
   for the tables (measurement stays a validator): duration, blend start, root displacement,
   touch frame(s) + ball position, cancel, turn, foot phase, lie state (§5–6).
6. **Locomotion = Freemove FSM + one parametric blend.** OBS live, AR §4.3.3/§5.2: steady running is
   parametric group 80 (rate ≈ speed / sprint speed); entries, exits, stops and turns are Freemove
   clips. GF's `movement` autogen + template machinery (which builds transitions at load) is the
   part PES replaces — the stock GF movement pool must stay out of a PES-driven build.
7. **GF's `e_FunctionType_Header` and `e_FunctionType_Catch` are never requested by the sim.**
   OBS: no `desiredFunctionType =` site for either in the whole sim (`grep` over `src/sim`). Headers
   happen as ShortPass/Shot touches with the head (`GetTouchTypeForBodyPart`); keeper saves happen as
   Deflect + physics + retain. Their PES rows are documented for completeness, not wired.

Consequences for the build: no PES table is committed; at startup the loader reads the bins named in
§6.2 from `PES21_ROOT`, builds the maps in §6.3, and `ChoosePesClip` (§7) runs the four lookups the
game runs (row+column for discrete actions, state FSM + parametric for locomotion, height blend for
keeper saves and headers).

---

## 1. Animetable.bin: 3,045 situation rows × 18 angle columns

### 1.1 Layout and lookup (OBS, AR §2.3 + re-verified)

- File: `common/anime/AnimeTable/bin/Animetable.bin`, 219,240 B = 3,045 × 18 × u32, little-endian.
- Cell: bits 0–12 = `motionNo + 1` (0 = empty), bit 13 = mirror. The 18 columns are the relative
  angle (target direction − body facing, wrapped ±180°) in 22.5° buckets; OBS, AR §2.3:
  `FUN_1406de990(row, angleDeg)` picks the *nearest populated* column, centre
  `k·22.5 − 180` with `k = c < 9 ? c : c − 1`; column 8 uses −0.001 and column 9 uses +0.001, so
  0° is split into a left and a right column. Column list:
  −180, −157.5, …, −22.5, −0, +0, +22.5, …, +157.5, +180.
- Population (OBS, decoded this session): 3,035 of 3,045 rows non-empty; 14,662 populated cells, of
  which 4,801 (33 %) carry the mirror bit; populated columns per non-empty row: min 1, median 3,
  max 18. Fully populated rows: 187 (mostly `dribble_*` and `goto_*`).
- Semantics of the mirror bit (OBS, examples §1.4): the same authored clip serves both sides.
  Rows ending `_l` hold left-authored clips with the bit set on the positive columns (the clip is
  reused mirrored); `…_r` rows hold the mirror image. In clips, `_l`/`_r`/`_R` = authored side.
- Hard patch (OBS, AR §2.3): row 1989 col 2 → motionNo 2484, mirror forced 0.
- The 10 empty rows (OBS, exact labels): `kick_short_0_0_heel_y0_l`,
  `kick_short_0_0_heel_y2_l`, `kick_short_3_0_heel_y0_l`, `kick_short_3_0_heel_y2_l`,
  `kick_short_0_0_heel_y2_l_b`, `kick_short_0_0_heel_y2_r`, `kick_short_0_0_heel_y4_r`,
  `kick_short_0_0_heel_y2_r_b`, `trapchase_3_0_y6`, `trapside_l_0_3_y4_in`.
  INF (high): dead heel-kick and chase combinations whose clips were cut but whose row labels
  remain; a lookup that lands here must fall back (nearest populated *row* is not implemented by
  `FUN_1406abb30` — it returns −1, and the action picks another row).

### 1.2 The row-name source (OBS)

- The 3,045 labels are **authored strings in the exe**, `char*` table at **0x1434997f0**, used by the
  `[trap_check] ANIMETABLE : %s` family of debug prints. They are not an enum (no 1:1 numeric id
  exists in the image; the row number is the id) and not generated at runtime (no format string
  builds them; searched `_%d`, `_y%`, `%s_%s` — none touches clips, AR §6).
- Because the label *is* the situation name, the name *is* the row's meaning; §1.3 gives the
  generative grammar per family, which reproduces all 3,045 labels (a verbatim dump would be
  PES-derived bulk and is not committed; regenerate with the layout in §1.1 plus the label table).

### 1.3 Row grammar (OBS, from all 3,045 labels + cell contents)

Speed classes `a_b` = entering/leaving, 0 still, 1 walk, 2 jog, 3 run, 4 dash, 5 top sprint
(AR §4.2: bucketed at 2.5 / 8.0 / 13.5 / 22.0 km/h). `_yN` = contact height (§5.4).
`_NNN`/`_fNNN` inside a *clip* name = +/− turn degrees; rows carry no angle themselves — the angle
is the column.

In-play families (row count; cells; params — `|` separates token slots):

| Rows | Cells | Family | Meaning and slots |
|---|---|---|---|
| 278 | 1,048 | `kick` | `kick_{short,mid,long}_a_b_{footpart}_yN[_mods]` — all foot strikes. `footpart` = `inside instep outside toe sole heel infront overhead`. `short/mid/long` = pass length / power class. Mods: `late` (last-moment), `adjust` (reach correction), `keep` (possession kept), `charge/punch` (power), `curve/lob/grounder/highpower` (trajectory), `cancel` (cancellable), `stepback/nostep` (approach), `stagger` (off-balance), `control/doubletouch/bodyfake/jostle` (touch quality), `f`/`l`/`r` + angle token, `v2/v3/v4` variants, `kfl/kl/lr/direct/scoop/max` (kickoff/long-shot variants). Kicks from the side/rear have their own families: `kickside` 9, `kickrear` 10, `kickfar` 18, `kickfardown` 4, `kicksliding` 1, `kickrabona` 2. |
| 170 | 2,090 | `dribble` | `dribble_a_b_{in,out,sole,nutmeg,rear,mid,axisback,…}_{l,r}[_mods]` — touches on the move. `_in` = touch toward the body, `_out` = away; `sole/axisback/rear` = drag-back family; `avoid/decelerate/guard/guardside` = under pressure; `notouch/4cycle1touch/tripletouch` = stride pattern; `verNN` variants. Speed slots run 0/2/3/4 (never walk — possession runs at jog+). |
| 150/148/126 | 790/742/589 | `trap / trapside / trapchase` | `trap[_side,_chase]_a_b_yN_[bodypart]_[in,out,near,far,guardside,…]` — receptions. `trap` = frontal, `trapside` = side-on (`_l/_r/guardside` state), `trapchase` = running onto it (`in/instep/out/sole` + `lobball/drop/fastball/rest`). Bodypart: the strike surface (`sole instep thigh breast head shoulder knee heel belly`); without one = inside-foot. `traplift` 39 (flick-ups: `boomerang/arc/chapeu/toe`), `trapfast` 19 (one-touch redirections), `trapback` 23 (facing away: `reverse/axisback`), `trapthrough` 5 + `trapthroughrun` 5 + `trapthroughrunside` 5 (let-throughs), `trapsidel/r` 6, `trapmove` 1, `trapstagger` 2, `trapsoleaxisback` 2, `trapchaseback` 2. |
| 105/77/32/19/8/4/1… | 682/583/… | `run / side / back / parallel / stop / walk / …` | locomotion transitions, aliased by the Freemove FSM (§2). `run_a_b[_style][_angle][_mods]` (`stop/turn/hurry/relax/tired/gk/lose/pain/…`), `side_{l,r}_a_b…`, `back_a_b…`, `parallel_a_b…`, plus `walk/walkback/walkside/walkslant/backslant/runslant/backstep/step(step/delay/timing, 44 rows)/near(5)/mid(6)/loop(5)/stand(4)`. The Animetable locomotion rows are the *action* side (which transition to play); the FSM (§2) is the *locomotion* side (which transition is legal from the current one). |
| 61 + 16 + 10 + 9 + 7 + 6 + 6 + 5 + 1 | 188+… | `head / headfardive / headclear / headside / headfar / haed(=shed) / headfarside / headrear / headrearfardown` | `head_a_b_yN_{f,r}[_shoot,_clear,_chest,_shoulder,_backhead,_jostle,_ground,_down,_late,_v2]` — headers, standing (`0_0`) or running in (`3_0`). Suffix `_f` = forward header, `_r` = rear/flick. Heights y4..y9 (§5.4). Chosen via `Idletable` (standing) vs `Movetable` (moving) (§1.5), not directly. |
| 53 | 83 | `dribblerun` | `dribblerun_[guard,guardback,stagger,preslowdown]_3_3_[angle]_in/out[_mods]` — touches at full pace with the ball tied to the stride (`notouch` = carry). |
| 49 | 233 | `stagger` | `stagger_[air,lowbody,upbody]_a_b[_hard,_weakly,_tackle,_hold,_pulled,_guard]` — knocked off stride but upright (GF trip type 1). |
| 62 | 446 | `fall` | `fall_[air,lowbody,upbody]_a_0[_front,_sliding,_move,_hold,_lose,_carry,_facecover,_vN]` — going/lying down (GF trip types 2/3). |
| 6 | — | `riseup` | `riseup_[allfours,facedown,faceup]_0_{0,3}` — getting up, staying (`_0_0`) or running off (`_0_3`). From `DemoConnectData` (§5.3). |
| 47/20/11/6/2 | 238/… | `blockfront / blockside / block / block090 / blockhead` | shot blocks. `blockfront_a_0_yNN[_shoot[_near,_reaction]]`, `blockside_a_0_yNN[_sliding,_reaction,_180heel,_fishdive]`. `yNN` = two-digit height (00..09). Picked by height + shot line, like headers. |
| 40 | 122 | `js` | jostle/duel: `js_{pushfront,protect,blockback,blockside,trapthrough,variation,idle,run}_…` (`loop/main/sub/win/lose/passcut/tacklefoot`). GF Interfere. |
| 32/16/14/10/10 | 94/… | `tackle / tacklehold / tacklefoot / tackleshoulder / tacklepulled` | `tackle_a_0_[far,mid,near,rear,shoulder][_block,_intercept,_090,_hard,…]`; `tacklehold` (`waist/shoulder`, `miss` variants); `tacklefoot` (foot-first, with `down/kneedown/blockcut`); `tackleshoulder` (`interrupt/grab`, `quick`). Standing tackles. |
| 19 + 21 + 17 + 15 + 4 + 3×4 + 2 + 1 | — | `sliding / dribbleslide / dribbleslide045 / dribbleslide090 / dribbleslide00 / dribbleslide45… / dribbleslide180 / dribbleslide000` | `sliding_a_0_{l,r,block,legscissors,parallel,…}` + angle `045/090/f030/f180/…`; `dribbleslide_a_b_[in,out,sole,loop]` (with ball); numbered families (`…045/…090/…f45/…f90/…000/…180`) = slide-tackle touch directions; long L9–L12 rows = `slide_045/090_guardback_y0_{sole,notouch}_ver20` multi-touch slides. GF Sliding. |
| 34 | 190 | `kickfeint` | `kickfeint_[namedtrick_]a_b_y0[_notouch][_l,_r]` (`crujiff/doubletouch/nutmeg/rabona/valdivia/vanpersie/samba`) — dummy kicks. GF feint (specialvar1). |
| 28 + 6 + 4×20… | — | `doubletouch / inout / outin / outout / lfeint / onetime / stepkick / bodyfake(000) / scissors* / roulette* / elastico* / nutmeg* / neymarstepkick / drawopencloase / parry* / shakefoot* / slide*` | ~100 rows of named tricks, each `_0_3_l/_0_3_r` + `_3_3` (+`_000` no-turn) variants. GF feints. The long `feint_`(6)+`feintrun_`(9) rows are the full-curve versions with `y0/Ltouch/in/verNN`. |
| 38 | 83 | `dodge` | `dodge_[human,ball,net,post]_…` — evading the ball/man/object (`jump/jumplow/jumpslide/slide/decelerate/crouch`). |
| 8 | — | `gkcatch` | `gkcatch_lie[_near]_a_b_[f00,s04]_l,r` — keeper lying catches (used with the `gkrise` get-ups, 67 rows: `gkrise_[sideways,faceup,facedown…]_a_b_[l,r]_…`). |
| 6/3/3/2 | — | `gkblock / gkfall / gkstagger / gkclear` | keeper blocks/falls/staggers/clears from the ground. |

Set-piece / official / cutscene families: `fk` 74 (`fk_{short,mid,long,quick}_3_0_{inside,instep…}` + named run-ups `bale/beckham/cristiano/…`), `pk` 34 (`pk_enclose_*` rituals + `pk_{short,mid,long,max}_3_0_…`), `kickoff` 22 (`kickoff_loop_*` — observed live at kick-off, AR §7), `throw` 29 + `throwin` 11 + `puntkick` 13 + `goalkick` 10 (`throw_over/side/under_…`, release variants), `corner` 1, `wall` 12 (`wall_jump_high…`), `judge` 15 (referee), `linesman` 26, `goto` 37 (`goto_run/turn_…_near/mid/far` — scripted walks), `goalturn` 19 (keeper showboating), `demo` 367 + `dm` 7 + `TimeUp` 32 + `HalfEnd` 13 (all cutscene: `demo_goal/dm_tu/dm_miss/dm_oop/…` goals, misses, time-up, half-end), `gk*` save families belong to §3 groups. `XXX_…_Stocking` (2) and `test_bodyfake000_…` (2) are authoring leftovers (INF, high: `XXX` and `test_` prefixes, two rows each).

### 1.4 Cell examples (OBS, exact rows; column = index, centre, clip, mirror)

- `trap_0_0_y0`: cols 4–7 hold `trap_0_0_f090_y0_in` / `trap_0_0_f045_y0_in`, cols 8–9 `trap_0_0_000_y0_in`,
  all unmirrored; all other columns empty. A frontal ground reception served by left-authored clips
  on the negative side and straight clips at 0.
- `kick_short_0_0_inside_y0`: cols 6/9/11/13 = `…_f045` / `…_000_keep` / `…_045_keep` / `…_090_keep`.
  Positive columns carry the `_keep` (retain possession) variants — the row, not the clip, decides.
- `dribble_0_2_out_r` (right-side row): all 18 columns populated; negatives hold `…_fNNN…_out`
  and mirrored sets (`_r` clip under `*M` on the −0 side), positives the `_NNN…_in` set; col 0 =
  `dribble_0_3_f180_sole`.
- `head_0_0_y5_f`: cols 0/4 (`head_y05_0_0_090` mirrored), 9 (`…_000`), 13/17 (`…_090`).
- `sliding_0_0_block`: cols 4/6 unmirrored (`…_f067_block`, `…_f045_block_f090`), cols 11/13 the
  same clips mirrored. Col 9 (dead ahead) is empty: straight-on blocks use `blockfront_*`.
- `js_pushfront_loop`: only col 9 = `js_idle_set_sub`.

Rule of thumb (INF, high): rows whose clips all carry `_in`/`_out`/side suffixes are *directional*
and populate ~8–18 columns; pure `_000` rows populate only col 8/9 and are *straight-only*.
A column request outside the populated set snaps to the nearest populated one (existing engine
behaviour, AR §2.3) — the mapping must apply the same snap, not invent rows.

### 1.5 Superior-kick tables: Idletable / Movetable / Kickinfo (OBS)

- `Idletable.bin` / `Movetable.bin`: 151 records × 20 B = `{u16 rowA+1, u16 class, u16 rowB+1,
  u16×4 zero, u32}` (OBS: `head_0_0_y5_f` is row 1836 and Idletable rec 1 starts
  `0x072d,0x0100,0x0733,…`). Sub-type 1/2 of `ParsedJsonSuperiorKickTable`. The class field reads
  `0x100,0x200,…` per record (height/situation class, exact unit open). Record shape: pairs of
  Animetable rows, e.g. Idle rec 1 = (`head_0_0_y5_f`, `head_0_0_y9_f`), Idle rec 2 =
  (`head_0_0_y5_f`, `haed_0_0_y9_shed_f`), Idle rec 3 = (`head_0_0_y5_f`, `head_0_0_y9_shoot_f`).
  Idle = standing headers, Move = running-in headers (`head_3_0_y*_f` pairs in Movetable rec 7).
  INF (high): the record index is the header situation (attack/defence, jostle, shoot/clear…); the
  two rows are the low/high (or front/rear) options for that situation.
- `Kickinfo.bin`: 2,416 B = 1 header + 75 records × 32 B = `{u32 bitfields, u32 5, u32 0,
  f32 0.5}` (OBS). The u32 (e.g. `0x0f003201`) is the 7-bit field pack the report cites (one field
  stored −60 = signed angle); the trailing floats are 5.0-as-int and 0.5. Semantics not decoded —
  it feeds the kick-cancel logic (`[Kick Cancel Rate]`, CancelData); treat as a per-kick tuning
  record, not a selector.
- `SlideDribble.bin` (697 KB, `ParsedJsonSlideDribbleTable`) and `FreemoveOption.bin` (§2.4) are the
  two remaining selection inputs; slide-dribble rows (≈2,000 dribbleslide cells above) are
  addressable through the Animetable alone, so SlideDribble is not on the critical path.

## 2. Freemove.bin: the locomotion state graph (OBS, key construction solved)

### 2.1 Layout and lookup

- File `common/anime/AnimeTable/bin/Freemove.bin`, 3,874 × 12 B, **sorted by key** (OBS). Record
  `{u32 w0, u32 w1, u32 w2}`: key = `(w1 & 0x7fffff) + 200` (200…5,315,319, all 3,874 distinct);
  value = motionNo `(w2 & 0xfff) + 0x22 − 1`; mirror = `w0` bit 23; next state = `w0 & 0x7fffff`.
  Key = `state·10 + turnDigit` (408 distinct states). Lookup = array search on the key (sorted, so
  binary search); the report's loader mask pass (keeping `w0` top 8 bits, `w1` bits 23–31, `w2`
  bits 12–31) packs the runtime struct.
- `next` lands on an existing state key in 3,792/3,874 cases; `next = 0` (82 cases) = terminal —
  the clip is played and the FSM exits (e.g. keys 10010…10017 → `idle*` / `mid_jockey_passget_v2`
  with next 0).
- Each key holds exactly one motion (no ambiguity): the state machine is
  **(state, turnDigit) → (motionNo, mirror, nextState)**. The 252 distinct motions in the table
  are the only clips the FSM can be "in"; any other current clip (e.g. after a trap) enters the
  FSM at the state the AI computes for it (§2.3) — no motionNo→state index is needed or present.

### 2.2 The state code (OBS, fitted over all 252 motions + 408 states)

The state is a decimal compound of 7 slots, leading zeros dropped:

```
[state] = [family][speedIn][sep][style][speedOut][sep][angleDigit]
```

| Slot | Values (OBS) | Meaning |
|---|---|---|
| family | 0 plain run/walk/stop/idle/near, 1 delay/side-loop, 2 parallel, 3 side, 5 back | body posture / locomotion mode |
| speedIn | 0 still, 1 walk-ish, 2 jog, 3 run, 4 dash | entering speed class |
| sep | 1 (locomotion families), 0 (near/delay families) | family separator digit |
| style | 0 run/plain, 1 walk, 2 parallel, 3 side, 4 rotate-180, 5 back | how the body moves |
| speedOut | 0 stop, 1 walk, 2 jog, 3 run, 4 dash | leaving speed class |
| sep | 1 / 0, as above | |
| angleDigit | 0..4 = +0/45/90/135/180°, 5..9 = the mirrored side (5 = −0) | the clip's own turn |

Checks (exact, all agree): `run_3_2_side_135` = 0313213 = fam 0, in 3, style 3, out 2, +135;
`side_2_2_run_f090` = 3210217 = fam 3, in 2, style 0, out 2, −90; `side_2_2_parallel_135` = 3212213
(style 2); `walk_1_0` = 0110010; `run_2_2` = 0210210; `run_3_3` = 0310310; `run_2_2_back` = 0215215
(style 5, angle 5 = slight negative turn); `idle_0_2_run_045` = 0010211; `near_run_2_2` = 0200200
(short form, both seps 0). The loop family: `mid_delay_1_1` = 1111110, `mid_delayslant_2_2` =
1211211, `mid_delayside_2_2` = 1211212, `mid_delaybackslant_2_2` = 1211213, `mid_delayback_2_2` =
1211214, `side_1_1` = 1111122, `side_2_2` = 1211222, `side_3_3` = 1311322, `back_2_2` = 1211224,
`walkside_1_1` = 1121122 (style 2 = walk), `runslant_2_2` = 1221221.
Slot histograms (252 motions): sep slots = 1 in 227/215 cases (rest 0, the near/delay families);
angleDigit 0..4 in all 249 non-terminal codes.
Residual: whether the engine *computes* the state arithmetically or reads it from the clip's
per-motion runtime object is open, but unnecessary — §7 never needs state-from-clip: the state is
carried in the player FSM and every transition (including entry) is a table lookup.

### 2.3 Using the FSM (OBS behaviour, AR §4.3.3 + §7)

- Entry: states 1000–1001 (keys 10010… → `idle*`, next 0), 20/30 (`gkdeflect…_180`, `near_run_0_2/3`),
  and the 101x–153x block (`idle_0_1_walk_*` / `idle_0_2_run_*` / `idle_0_2_parallel_*`) are the
  standing-start rows. Observed live: `idle_0_2_run_045` → parametric run → `run_2_4_000` →
  parametric; `back_2_2 → backslant_2_2 → walkbackslant_1_1`.
- A "current clip code + requested relative turn" step (AR §2.5) is exactly: `key = state·10 +
  turnDigit`, read (motion, mirror, next). Turn buckets: 0..4 for +0/45/90/135/180, 5..9 mirrored.
- The side/back asymmetry is in the data, not the code: rows for the 3/5 families list both sides
  explicitly (e.g. state 321041x has digits 0–4 and 6–9), while 0-family rows often list only one
  side plus mirrors.

### 2.4 FreemoveOption.bin (OBS layout, INF use)

105 × 8 B: key = `(w0 & 0x3ffffff) + 0x900b1` (590001…37,020,005; 96 distinct, so some keys carry
several records = a list), value = motionNo `(w1 & 0xfff) + 0x1f − 1`, mirror = bit 26. Two key
clusters: 590001–740006 → `back_2_4_run_*` / `back_3_3_run` entries; 22200005–… → `idle_0_1_*` /
`idle_0_2_*` / `walk_1_1` / `side_3_3_parallel_f225` entries (same key → up to 4 motions, e.g. key
22250005 → `back_2_2_run_f135*M`, `idle_0_1_walk_180`, `idle_0_2_run_180_near`, `walk_1_1`). INF
(medium): second-stage disambiguation for the FMOVE step logic (`[FMOVE_STEPCHANGE] isSpeedChange
isAngleChange`) — same situation key, motion picked by extra state (speed delta / side). Not on
the critical path: every value motion is also reachable through the main table.

## 3. Parametricblendtable.bin: 1-D blend spaces (OBS, fully decoded)

449 × 8 B: `u32 {motionNo − 0x520 :12, group :7, mirror :1}` + `f32 parameter`. 95 groups (§6 in
scratch `parametric.json`; member lists verified against the name table):

- **Groups 0–79: goalkeeper blends over ball height.** Members `gk…_yNN`, parameter = N/11 exactly
  (y02 → 0.18, y04 → 0.36, y06 → 0.55, y08 → 0.73, y10 → 0.91, y11 → 1.0). Families: `gkblockcover`
  0–8, `gkcatch` 9–25, `gkcollapsing` 26–33, `gkdeflectlate` 34–40, `gkdeflectscoop` 41–46,
  `gkdeflect(_snapunder/_stagger/_Expect/_lie/_pk/_otherside)` 47–70, `gkpunch` 71–75,
  `gkscoopout_lie` 76–79. Multi-member groups carry `long/_light/_hard/_wide/_punch/_line/_back`
  variants at the same parameter (engine picks by extra state, INF medium).
- **Groups 80–93: locomotion over speed.** 80 is canonical: `walk_1_1` 0.0, `run_1_1` 0.15,
  `run_2_2` 0.3, `run_3_3` 0.5, `run_4_4` 0.9, `run_5_5` 1.0. Variants replace one member:
  81 `run_adjust_*` (dodge steps), 82–84 jostle guard loops (`js_run_*_loop_guard_*`), 85–86
  `near_run_*` (ball approach), 87/91/92 relax/tired, 88–90 sidestep (`walkparallel/heisou/parallel_*`,
  89 the `gkmoveheisou` keeper one), 93 `run_4_4_v2`. 83 has one mirrored member.
- **Group 94: throw-in turn loop** (`throwin_turn_067_loop *M` 0.0 / `throwin_loop` 0.5 /
  `throwin_turn_067_loop` 1.0).
- Playback (OBS live, AR §5.2): each member starts at `frames/30 × startRate` with
  startRate ∈ {0, 0.25, 0.75} (the foot-phase markers, §5.7); blend rate ≈ speed normalised so
  that 0.5 holds ≈3.9 m/s. OBS live mapping: rate 0 ↔ 1.4 m/s, ≈0.3 ↔ 3.1, 0.5 ↔ 3.9–4.6,
  ≈0.8 ↔ 6.5–7.7. INF (high): rate = speed / top speed (`run_5_5` natural ≈ 8.96 m/s at 30 fps),
  engine lerps the two bracketing members.

## 4. GF animation categories and what the sim actually asks for

GF vocabulary (OBS, `animcollection.hpp`, `humanoid.cpp` SelectAnim ~:1983–2130, repo `assets/gf_anims`
293 clips): function types Movement BallControl Trap ShortPass LongPass HighPass Shot Deflect Catch
Interfere Trip Sliding Special (+Header/Catch, never requested, §0.7). The sim's stock pool the PES
clips must answer for: `ballcontrol/ celebration/ deflect/ highpass/ interfere/ movement/
movement_special/ pass/ shot/ sliding/ trap/ trip/` (idle/dribble/walk/sprint subdirs) + the
`pes/` + `feint/` import convention (`animcollection.cpp` Load: `/pes/` → `imported=true` first
refusal; `/feint/` → `specialvar1 = Feint`, invisible to plain ballcontrol queries).

Per-request GF parameters (OBS): incoming velocity (current e_Velocity), desired velocity/direction
(movement, ballcontrol with touch), incoming body direction + linearity gate, desired look-at
(side-fence), outgoing ball direction (passes/shots with desiredPower ≠ 0), incoming ball direction
(trap/interfere/deflect, from the 180 ms vs 120 ms ball prediction), pickupBall (deflect with
`onlyDeflectAnimsThatPickupBall`), tripType 1–3 + trip direction, specialVar1/2 (specials, feints),
lastditch flag.

### 4.1 GF bucket → PES digit; ball height → yN (OBS)

| GF e_Velocity | m/s | → PES speedIn/Out digit | note |
|---|---|---|---|
| idle 0 | < 1.8 | 0 | |
| dribble 3.5 | 1.8–4.2 (12.6 km/h) | 2 | GF dribble is PES jog |
| walk 5.0 | 4.2–6.0 (18 km/h) | 3 | GF walk is PES run |
| sprint 7.0 | ≥ 6.0 (25 km/h) | 4 | GF sprint is PES dash |

PES thresholds {2.5, 8.0, 13.5, 22.0} km/h = {0.69, 2.22, 3.75, 6.11} m/s (OBS, AR §4.2).
GF quantisation {1.8, 4.2, 6.0} (`velocitystate.hpp`, `animation.cpp` bucketing, `anim_metrics.py`
`quantize_velocity`). GK-only digit 5 (top sprint) is the top of the speed blends.

Ball height → `yN` (OBS, re-measured over 2,139 `hit_data ball_pos_y` values; agrees with AR §4.2):

| yN | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| cm | 11–103 (med 12) | 20–55 | 46–66 | 64–88 | 88–120 | 113–146 | 56–156 (med 146) | 158–179 | 184–194 | 213 | 236–258 | 258 |

y0 = ball on the ground (radius 11 cm); y6 overlaps (ground + rising balls); y9 = header height;
y10/y11 GK-only (`gkpunch/gkdeflect …_y11`, 236–258 cm).

### 4.2 Category mapping (row construction + column + mirror + foot + height)

- **Movement** (steady). Parametric group 80 (canonical) / 81–93 (variants) at rate from §3;
  variant pick: guard/jostle groups when pressured, tired/relax by stamina state, parallel/heisou
  for sidestep, near_run for ball approach. Crossfade 0.3 s; startRate from the foot phase (§5.7).
- **Movement** (step/stop/turn). Freemove FSM §2: GF (in, desiredOut, turn) → state
  `[0][in][1][style][out][1]` with style from the step family (run 0, walk 1, parallel 2, side 3,
  back 5), digit = turn bucket, crossfade 0.133–0.2 s. GF dribble→idle and walk/sprint→sprint denials
  (`CrudeSelection`) correspond to states that don't exist in the table (OBS: no `*_0_2` state with
  digit 4/9 pairs outside the side/back families) — a missing row is a legal answer only with the
  fallback chain (§7).
- **BallControl** (dribble touch). `dribble_a_b_{in,out,…}_{l,r}` rows (170): a/b from §4.1,
  `in` (toward body) vs `out` (away) from the touch geometry, `_l/_r` row = current dribbling foot
  side; column = direction of the next touch relative to facing; mirror for the other foot.
  At pace: `dribblerun_*` rows. Feint requested (specialvar1): the ~100 trick rows — `feint_*`,
  `feintrun_*`, `doubletouch_*`, `heellift`, `scissors*/roulette*/elastico*/nutmeg*`, `bodyfake*`,
  `lfeint/inout/outin/outout/onetime/stepkick/*slide*` — each `_0_3_l/_0_3_r` + `_3_3`, column by
  angle, no fallback to plain ballcontrol (GF drops the command instead; `HasSpecial` semantics).
- **Trap** (receive). `trap` (frontal) / `trapside` (side-on, `_l/_r/guardside` state — pick the row
  whose side matches the ball side, else mirror) / `trapchase` (running on, out ≥ 2) / `trapfast`
  (one-touch) / `traplift` (flick-up, ball rising fast) / `trapback` (ball behind: `reverse/axisback`)
  / `trapthrough*` (let-through called by the AI, not a failed trap). Row: a = §4.1 of incoming
  velocity, b = §4.1 of desired outgoing, yN = §4.1 of predicted ball height at contact,
  body part = nearest surface to the predicted contact (sole/instep/thigh/breast/head/shoulder…),
  `in/out/near/far` = touch toward/away + ball distance. Column = incoming ball direction vs facing.
  `notouch` clips are for dummy runs (skip unless the AI calls one).
- **ShortPass / LongPass / HighPass.** `kick_{short,mid,long}_a_b_{footpart}_yN` rows (278):
  length class by desired ball speed/distance (short < mid < long; `fk_*` only for dead balls);
  a = incoming §4.1, b = 0 for a set pass (`_keep` variants keep possession) else 3/4 for a driven
  ball; footpart: `inside` = short/set, `instep` = driven/long, `outside` = bend, `infront` = reach,
  `toe/heel/sole` = desperation/high; yN = ball height at contact (HighPass ⇒ y2+, `lob/curve`
  rows). Column = target direction vs facing. Prefer `adjust/control` rows when off-balance.
- **Shot.** Same `kick_*` rows, filtered to the power/trajectory set: `*_shoot/highpower/curve/lob/
  grounder/*_v2/v3/max/blast/2tep` and `kickfar/kickfardown` (distance), `kickside/kickrear` (angle);
  `scorpio/taekwondo/roulette/overhead` stay out (acrobatic, `*_cancel` rows only when the AI
  explicitly calls a placed finish — they end the move). GF `preferPassAndShot` narrows the touch
  radius (§6.4 of GF `humanoid.cpp`), unchanged.
- **Deflect** (outfield block / redirect). Shot block: `blockfront_a_0_yNN[_shoot[_near,_reaction]]`
  frontal, `blockside_a_0_yNN[_sliding]` side-on, `block090/blockhead` for the high line; yNN from
  the shot height (two-digit 00–09 ≈ y0–y9); column = shot line vs facing. Loose-ball redirect:
  `trapfast/trapchase` at the right height with `pickupBall` semantics (= GF `touch_bodypart`
  nearest-surface rule, §6.3). Keeper: parametric GK groups 0–79 at parameter N/11 by shot height;
  catch vs parry = `gkcatch/gkcollapsing-hold` (+`outgoing_retain_state=right_elbow`) vs
  `gkdeflect/gkpunch` families.
- **Interfere** (duel, no ball). `js_*` rows (40): `pushfront/protect` (frontal), `blockback/blockside`
  (goalside), `trapthrough/variation` (disguised), speeds from §4.1, `move/step/tackle` by duel
  intensity; the `js_run_*_loop_guard_*` parametric members (groups 82–84) for sustained jostling;
  `tacklefoot/tackleshoulder` for the contact itself. Column = opponent direction vs facing.
- **Sliding.** `sliding_a_0_{l,r,block,legscissors,parallel}[_angle]` rows (19): a = incoming §4.1
  (max 3 — no `_4` sliding rows exist; sprint slides enter at 3), `_l/_r` = sliding side, `block` =
  with a block, column = slide direction vs facing. With-ball variants: `dribbleslide*/slidebodyfake/
  sliderabona/slidescissors*`; standing-tackle-with-contact: `tacklefoot_*` (`block/intercept/
  kneedown`). Ends grounded → `outgoing_special_state` = lie from `DemoConnectData` (allfours/
  facedown/faceup, AR §4.3.1), then the stand-up plays `riseup_<state>_0_{0,3}`.
- **Trip.** GF type 1 (bump/stagger): `stagger_[air,lowbody,upbody]_a_b` (49); types 2/3 (fall):
  `fall_[air,lowbody,upbody]_a_0` (62) + `tacklehold/tacklepulled` (held/pulled down) +
  `gkfall/gkstagger` (keeper). Column/bucket = trip direction vs facing (GF `desiredTripDirection`
  → `CompareTripDirectionSimilarity`). Getting up: `riseup_*` as above; `gkrise_*` for the keeper.
- **Special** (celebration). `demo_goal/dm_tu` intro + `_loop` pairs (66 loops), `dm_miss` (dejection),
  `TimeUp/HalfEnd` (walk-offs). GF asks intro by (specialvar1, specialvar2), then the loop with the
  loop pair — the loop must exist or the command is dropped (existing `HasSpecial` contract).
  Referee (referee controller asks Movement/Special): `judge_*`; linesmen: `linesman_*` (+`layer_
  assistant_*` upper-body layers, AR §11); wall: `wall_*`; set-piece choreography: `fk/pk/kickoff/
  throw*/puntkick/goalkick/corner` families.
- **Header / Catch rows for completeness** (never requested, §0.7): headers `head_* + headfardive/
  headclear/headside/headfar/haed/headfarside/headrear` at y4–y9 via Idletable (standing) /
  Movetable (moving) §1.5, `_f` forward / `_r` rear; keeper saves the §3 groups. If the sim ever
  requests them, the column is the ball line vs facing and the height is §4.1.

## 5. Root motion and timing compatibility (formulae)

All per-clip inputs exist in `anim_infos.json` (= the decoded Mbinfo bins: Animation/HitData/
CancelData/RotData/FootType/DemoConnectData/BallData/MoveAdjustInfo…); no curve fitting is needed
to *select*, only to *validate*.

- **Frames.** Match gani frames are 30 Hz (OBS, AR §0.2). GF frames are 10 ms (100 Hz; `main.cpp:412`,
  `animTouchFrame * 10` = ms). `f_gf = round(f_pes · 100/30)`.
- **Duration.** `T = end_frame / 30` s (`Animation.bin` bits 13+ = `end_frame − 1`, OBS AR §2.2).
  Median clip 43 frames = 1.43 s; max 598 (a loop/cutscene). GF derives the frame count from the
  keys, so generate GF keys at 10 ms from `t = min(f·10/33.33…, frame_count)` (existing converter
  shape, with 33.33 ms not 16.68 ms).
- **Outgoing blend start.** `blend_start / 30` s (`FUN_1406af720` returns `blendStart/30.0`, OBS).
  Median `blend_start` 32 vs `end_frame` 43: the last ~0.37 s is the blend-out tail. Equivalent to
  GF's `GetEffectiveFrameCount` overshoot (`Slowdown` clamps at `+16` frames).
- **Incoming velocity (m/s).** `|root(1) − root(0)| / 16384 · 30`, 2-D (OBS scale, AR §0.3; median
  check §6.2 of this report: clip-name `a=0..4` first-frame root speeds rise 2.6/4.6/7.4/10.9/18.6
  cm/frame = 0.78/1.38/2.22/3.27/5.58 m/s — inside classes 0/1/2/3/4). Quantise with
  {1.8, 4.2, 6.0} → {0, 3.5, 5, 7} (GF `Animation::GetIncomingVelocity`, `velocitystate.hpp`).
  GF computes it from the first two *generated* keys — same number by construction.
- **Outgoing velocity.** `|root(N) − root(N−1)| / 16384 · 30`, same quantisation; correlates with
  the json `cancel_speed` (median 4.04 cm/frame = 1.21 m/s), which is the speed PES itself carries
  past the cancel point — use `cancel_speed·30/100` m/s as the cross-check.
- **Outgoing angle.** `atan2(dx, dz)` of the last root step (Fox, +Z forward) = GF
  `FixAngle(atan2(dx, −dy))` after the `(x,y,z)→(x,−z,y)` basis map (existing converter `map_vec`).
  `end_move_angle` (median 7.1°, range ±180) is the authored value — use it as the check, the root
  track as the source.
- **Touch frame(s).** `hit_data.hit_frame / 30` s → GF `round(hit_frame·100/30)`. 2,948/4,907 json
  clips carry 3,254 hit events (some clips two); GF iterates *all* football-extension touches
  (`GetBestCheatableAnimID` touch loop) — emit every event, ordered middle-out as GF does. Clips
  without hit data must not answer touch queries (`_PrepareAnim` warns) — movement/trip/special
  only. Median hit frame 10 (0.33 s).
- **Touch position.** `hit_data.ball_pos_{x,y,z}` (cm, player-root space, median height 11.6 cm =
  ball radius) → GF metres `(x, −z, y)/100` at the touch frame, relative to the body root then
  (existing `AddExtraTouches` measures the nearest body part → `touch_bodypart` for
  `GetTouchTypeForBodyPart`). `hit_bone` (e.g. 6/10) + `sf_*` (strike-foot joint/pos/vec) +
  `ik_*` (IK window `ik_start..ik_end`) disambiguate foot vs head vs hand. Z-cheat allowances
  (`ballDistanceZ < 0.22`, height scaling in `humanoid.cpp` ~:3100) are unchanged.
- **Ball direction variables.** `incomingballdirection` = unit vector from the predicted ball line
  to the touch point (existing `incoming_ball_direction(contact, shot_distance=3.0,
  shot_height=0.11)`); deviation gate 0.5π (trap/interfere; keeper 0.4π; GF defaults 0.25π, deflect
  0.4π). `balldirection` = touch-out direction (toe velocity for dribbles, `sf_root_vec` for
  strikes); gate 0.13π–0.25π per family (existing stock values). PES does not store the outgoing
  ball vector — the row's angle column *is* the outgoing direction contract (the clip turns the
  body to face it), which is why GF's `byOutgoingBallDirection` maps to the column, not to a
  per-clip vector.
- **Turn.** `rot_data {rot_start, rot_end, rot_angle}` (frames/degrees, 30 Hz) = the authored yaw
  profile; the name `_NNN/_fNNN` token equals the median `rot_angle` per bucket (OBS, AR §6; the
  1,719/3,535 exact matches are the plain turn clips, the rest carry extra rot records for
  multi-phase turns — check per-bucket, not per-clip). `rotationSmuggle` spreads it over the clip
  as today.
- **Foot phase.** `foot_type` events (14,376; `(frame, type 0..3)`): 0 = left planted, 1 = right
  strike, 2 = right planted, 3 = left strike (AR §5.3, high). Loops start/end on type 0; events sit
  ≈ 0/¼/½/¾ of the cycle = the parametric `startRate` {0, 0.25, 0.75} markers. `steps` = strike
  count (existing `step_count`); keep it — GF sorts and gates on it (`CompareMovementSimilarity`).
- **Lie / rise.** `demo_connect_data` (2 allfours / 3 facedown / 4 faceup) = GF
  `outgoing_special_state` for slides/falls; the matching `riseup_<state>_0_{0,3}` re-enters.
  `jump_data / hold_data / sub_hit_data / ball_data / move_adjust_info` ride along for the vault/
  hold/redirect details; `danger_data / pivot_info / divert_info / joint_pos_info /
  equal_parts_pos_info / camera_pickup_info` are not needed for selection.
- **Playback.** Crossfade durations 0.133/0.167/0.2/0.3 s (OBS live + 188 call sites, AR §5.1):
  discrete actions 0.2, locomotion steps 0.133–0.167, parametric 0.3, official idles 0.5.
  `playbackSpeed` ≈ 0.9–1.15 per player (OBS live; driver open — stamina or body-size factor,
  AR §9) × speed-matching for parametric members. `transitionType` 0 nearly everywhere (2/6 for
  sidesteps). Upper-body layers (`SetLayer`, masks `LRArm/LArm/RArm/Full`) only for officials.

## 6. What the loader reads and builds (runtime, from the install)

### 6.1 Per-clip metadata (one record per motionNo, from `anim_infos.json` + the bins)

`name, end_frame, blend_start, root_pos_first_{x,y,z}, root_pos_blend_{x,y,z}, cancel_speed,
end_move_angle, hit_data[]{hit_frame, hit_bone, ball_pos_cm, sf_*, ik_start/end}, cancel_data[],
rot_data[], foot_type[], demo_connect_data`. Anything missing for 44 motions without an Animation
record falls back to the default at 0x1436f2430 (OBS, AR §2.2) — treat as "no metadata, loops only".

### 6.2 Tables

`Animetable.bin` + row labels (re-derived at runtime from the exe table — or, cleaner, from the
row-name grammar §1.3 checked against the exe table at load); `Freemove.bin` direct (§2.1);
`Parametricblendtable.bin` direct (§3); `Idletable/Movetable.bin` row pairs (§1.5); `Hitframes.bin`
(24,144 B; per-motion hit-frame bytes, the FH hand-timing companion — load for the hand report's
rule, not selection).

### 6.3 Derived maps (built, never committed)

- `rowIndex by (family, a, b, yN, part, side, mods)` — hash the row labels once at load
  (case-folded); collisions (e.g. duplicate row names) resolve to the first non-empty row.
- `freemoveRow by state code` — from the file (§2.1).
- `paramGroup members + points` — from the file (§3).
- GF-side per-clip variables (`type, incomingballdirection[_maxdeviation], balldirection[_…],
  touch_bodypart, touchframe, steps, triptype/bumpdirection, outgoing_special_state,
  outgoing_retain_state, lastditch, specialvar1/2, imported`) — same tail the stock pool carries,
  measured per §5 (prior art: `install_anims.py prepare_match_anim`, `anim_metrics.py`).

## 7. ChoosePesClip — the algorithm

```
ChoosePesClip(action) -> (motionNo, mirror, startTime, duration, rate):
  # action = {family, velIn (GF bucket), velOut, relAngle (deg, target-dir minus facing),
  #           ballHeightCm, ballDirIn, ballDirOut, footSide, power, variant, state (loco FSM)}
  if action.family == LOCOMOTION_STEADY:
      g = paramGroup(variant)                       # 80 canonical
      rate = clamp(speed / 8.96, 0, 1)              # §3
      startRate = footPhase(state) in {0,.25,.75}   # §5.7
      return PARAMETRIC(g, rate, startRate, duration=0.3)
  if action.family == LOCOMOTION_STEP:
      st = [0][pes(velIn)][1][style][pes(velOut)][1]  # §2.2, §4.1; style §4.2
      digit = angleBucket(relAngle)                    # 0..4 / 5..9
      (m, mir, nxt) = freemoveRow(st)[digit]           # nearest populated digit, engine rule
      state = nxt
      return SINGLE(m, mir, startTime=footStart(m), duration=0.133..0.2)
  if action.family in (TRAP, INTERFERE, DEFLECT_LOOSE):
      row = rowIndex(familyVariant(ballSide, pressure), pes(velIn), pes(velOut), yN(ballHeightCm),
                     bodyPart(predictedContact), mods)  # §4.2; empty row -> fallback chain
      (m, mir) = animetable(row, angleBucket(relAngleVsFacing(ballDirIn)))
      return SINGLE(m, mir, startTime=0, duration=0.2)
  if action.family in (SHORTPASS, LONGPASS, HIGHPASS, SHOT):
      row = rowIndex(kick, lengthClass(power), pes(velIn), outClass(keepOrDrive),
                     footPart(power, curve), yN(ballHeightCm), mods)  # §4.2
      (m, mir) = animetable(row, angleBucket(relAngleVsFacing(ballDirOut)))
      touch = hitEvent(m, ballHeightCm)                # §5: frame + pos; all events to GF
      return SINGLE(m, mir, startTime=0, duration=0.2)
  if action.family in (BLOCK):
      row = rowIndex(blockVariant(frontal/side), pes(velIn), 0, yNN(shotHeight), shootMods)
      (m, mir) = animetable(row, angleBucket(shotLineVsFacing))
      return SINGLE(m, mir, 0, 0.2)
  if action.family == KEEPER_SAVE:
      (g, param) = (gkGroup(situation), clamp(ballHeightCm/11-convention → N/11))  # §3
      return PARAMETRIC(g, blendPoint=param, startRate=0, duration=0.2)
  if action.family == SLIDING:
      row = rowIndex(sliding, pes(velIn), 0, slideSide, ballAttached ? dribbleslide : plain)
      (m, mir) = animetable(row, angleBucket(slideDirVsFacing))
      return SINGLE(m, mir, 0, 0.2, lieState=demoConnect(m))   # §5: outgoing_special_state
  if action.family == TRIP:
      row = rowIndex(tripType==1 ? stagger : fall, pes(velIn), 0, mods)
      (m, mir) = animetable(row, angleBucket(tripDirVsFacing))
      return SINGLE(m, mir, 0, 0.2, lieState=demoConnect(m))
  if action.family == SPECIAL:
      if not HasSpecial(var1, var2): drop command      # existing contract
      row = introRow(var1, var2); (m, mir) = animetable(row, 9)   # col 9 = +0
      ...then loopRow(var1, loopVar2) same way
  # fallbacks, in order: nearest populated column (engine rule) -> b±1 row -> a±1 row ->
  #   same family yN±1 -> GF stock pool (only if the PES row set is exhausted) -> idle state.
  #  startTime for loops = phase-matched (startRate·frames/30); one-shots = 0
  #   (the exact tick-quantised single-clip offset, AR §5.3, stays open and defaults to 0).
```

Fallback order is load-bearing: PES rows are sparse (median 3 populated columns) and 10 rows are
entirely empty (§1.1); a miss must walk the chain, never hold the previous clip. The GF-side
radius/cheat narrowing (`GetBestCheatableAnimID`) runs unchanged on the emitted candidates.

## 8. Open items and killed hypotheses

Open (carried from AR §9, narrowed): the tick-quantised single-clip `startTime` formula (defaults
to 0 — visually a sub-frame phase error, not a wrong clip); the engine blend curve (linear
assumed); `transitionType` bits beyond "0 = crossfade"; layer masks 4–6 (officials only); the
`playbackSpeed` driver (assume 1.0 + parametric speed-match until the stamina/size factor is
found); `Kickinfo` field semantics (not a selector); `SlideDribble.bin` internals (addressable via
the Animetable); `FreemoveOption` use (secondary list, §2.4); Idle/Move `class` field unit;
logic-vs-root-motion split (order-of-magnitude agreement, §5 speeds); Sequenceinfo bits
+0x66/+0x69/+0x6a.

Killed this session: *Freemove digits are opaque* — solved as the 7-slot state code (§2.2; every
one of 252 motions and 408 states fits, mirror digit ±5 included). *The `next` field is the clip's
code* — refined: it is the *next FSM state* (82 terminal `next = 0`; mirrored entries add 5 to the
digit; §2.1). *Idletable/Movetable are 12-bit packs* — they are `{u16 row+1, u16 class, u16 row+1,
…}` pairs (header rows resolve exactly, §1.5). *GF's `PES_FRAME_MS = 1/59.94` import path can be
reused* — it is 2×/25 % off for match clips (§0.1; repo constants already corrected). *Row labels
are an enum* — they are authored debug strings, the row number is the id (§1.2).
