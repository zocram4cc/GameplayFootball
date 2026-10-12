# PES21 → GF sim motion mapping

Scope: how to drive visible PES match clips from the GameplayFootball (GF) simulation
(`<repo>/src/sim`, `onthepitch/player/humanoid/*`), i.e. what `ChoosePesClip(gfAction, params)
-> (motionNo, mirror, startTime)` must do. Read-only RE: the exe image (`<image>/pesmem.bin`, the 64 MB memory image at
base 0x140000000, plus the on-disk `PES2021.exe`), the install's own `common/anime/` bins, the GF sources, and the prior report
[`pes-re-animselect.md`](pes-re-animselect.md) (cited as "AR"; §N = its sections). No PES table is
reproduced; every table is described by its layout and the generator script. Scratch (never in the
repo): **`<scratch>/SimMotionRE/`** = `~/ghidra-projects/scratch/SimMotionRE/`, holding the dumps
`Animetable.bin Freemove.bin Parametricblendtable.bin Idletable.bin Movetable.bin Kickinfo.bin`,
the headless decompiles `o_*.txt` (plus `scripts/` with the Ghidra post-scripts and
`report.md`, this report's own working copy) and the derived `tables.json` (name + row-label
strings, scratch only — a PES-mined name list that must never be committed).

Tags: **OBS** = observed, with how. **INF** = inferred, with confidence.

### Status per priority item (end of this session)

| # | Item | Status | Where |
|---|---|---|---|
| 1 | Action layer, end to end (row + column + mirror + startTime + playbackSpeed + crossfade) | **done** — the column picker (re-decompiled from raw bytes, `v1.txt`), the three accessors, SetMotion/SetParametric, `DAT_14344e0e0`, the trap solver and the populated-neighbour walk all decompiled | §6, §12, §13 |
| 2 | Kick taxonomy + both vocabularies + superior-kick selection | **done** — the Idletable/Movetable slot order is now derived from the parser (`FUN_1406aa590`), not fitted; the enum unit is solved (§7.1). GF's feint vocabulary is mapped 1:1 to PES trick rows | §7, §14 |
| 3 | Keepers: clips, states, catch→hold→wait→distribute cycle | **done** — the five-phase keeper state machine is decompiled (`FUN_140791ec0`, states 0–5 at `P+0x245c`), the distribution rows are identified by their *rows* (`throw_*` → `gk*throw*` clips), catch-vs-parry is still a tuned threshold | §8, §14.2 |
| 4 | Non-kick states: touches/turns, back/side, injury, taunts, officials | **done** — turn angles in `rot_data` + clip-name tokens; `back/side/parallel` families; staggers/falls/riseups; `demo_*` selection through the 9×18 priority tables (decoded, they are demo rows not row-neighbours) | §9, §6.5 |
| 5 | What the mapping cannot close | listed with what was tried | §14 |

Live-process sampling was **not available this session**: `PES2021.exe` is not running
(`pgrep -f PES2021.exe` returns nothing; the wine prefix exists but no match process). Every
"live" datum below is carried from AR §5/§7, which did have the process. Everything new here is
from the exe image (`.pdata`-bounded decompiles + data reads) or from the install's bins, and is
tagged accordingly. **Worked examples are table-derived**, not live-observed.

Corrections made this session (each verified against the raw bytes, details in place):
- `Parametricblendtable.bin`'s bit layout was wrong (§3) — the group field is `(w0>>12)&0xff`,
  changes only on a group's first record, and the member id continues that group; every group
  list in this report is re-derived from the corrected split.
- Several worked-example cell ids were wrong (§13) — E3's clip, E6's group numbers and
  E7's slot index are corrected against the files.
- `throw_*` rows (150–177) are the **keeper** distribution rows, not the outfield throw-in;
  the outfield throw-in rows are `throwin_*` (2488–2498), §7.3 row 17.
- The 9 × 18 tables at `DAT_1425e4700` are **celebration / trap row-priority lists indexed by
  the 18 angle classes** from `FUN_1406eb100`, §6.5 and §9.4.

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
- Population (OBS, re-decoded this session byte-for-byte): 3,035 of 3,045 rows non-empty;
  14,662 populated cells, of which 4,801 (33 %) carry the mirror bit; populated columns per
  non-empty row: min 1, median 3, max 18. Fully populated rows: 187 (mostly `dribble_*` and
  `goto_*`). Family coverage re-verified: `dribble` 170 rows / 2,090 cells, `trap` 150/790,
  `trapside` 148/742, `trapchase` 126/589, `head` 115/366, `run` 105/682, `side` 77/583,
  `fk` 74/516, `fall` 62/446, `stagger` 49/233, `blockfront` 47/238, `js` 40/122,
  `tackle` 32/94, `sliding` 19/59. **The 158 family prefixes are listed in scratch
  `<scratch>/SimMotionRE/tables.json` (labels array), never in the repo.**
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

### 2.4 Freemoveoption.bin (OBS layout, verified values)

840 B = 105 × 8 B (OBS from the VFS listing). Record `{u32 w0, u32 w1}`:
key `= (w0 & 0x3ffffff) + 0x900b1` (590,001…37,020,005; **96 distinct** keys), motionNo
`= (w1 & 0xfff) + 0x1f − 1`, mirror `= bit 26`. Seven keys carry more than one motion
(2–4 records), e.g. key 22250005 → `back_2_2_run_f135` *M, `idle_0_1_walk_180`,
`idle_0_2_run_180_near`, `walk_1_1`; key 30770007 → `run_2_3_run_135_feint_000` +
`run_2_3_run_090_feint_f090`. Verified this session: all 105 motions are also reachable through
the main table, so this is a **secondary disambiguation** for the FMOVE step logic
(`[FMOVE_STEPCHANGE] isSpeedChange isAngleChange` — same situation key, motion picked by the
speed delta / side), not a new source of clips. Not on the critical path.

**Freemove file statistics re-verified this session (OBS):** 46,488 B = 3,874 × 12 B;
3,874 distinct keys (sorted ascending, so a binary search is the intended lookup); 408 distinct
states (range 20…531,531); 252 distinct motions; 62 terminal records (next = 0);
50 states carry fewer than 10 digits (a missing digit is a legal answer).

### 3. Parametricblendtable.bin: 1-D blend spaces (OBS, fully decoded)

449 × 8 B records. **Corrected layout this session** (the earlier draft's bit split was wrong —
OBS, re-derived from the run-length structure of the file, 98 group-change markers):
record `i` = `{u32 w0, f32 param}` where

```
motionNo = (w0 & 0xfff) + 0x520 − 1          # 13-bit raw, offset 0x520, stored +1
group    = (w0 >> 12) & 0xff                 # changes only on the first record of each group
mirror   = (w0 >> 19) & 1
```

so the record *sequence* is authoritative: a record whose `group` field differs from the previous
one starts a new group, and the low 12 bits then continue that group's motion numbering
(OBS: records 3,4,5 carry groups 1,1,1 with motionNos 3,4,5; records 6 and 10 both carry
motionNo 7 in groups 2 and 3). 97 distinct groups from 98 markers — one marker is a repeat, and
the max field value 222 is a data error, so **the group id used by `DAT_14344e0e0` (§6.8) is the
group *ordinal*, not the field value** (OBS: ordinal 80 and 85 decode exactly as the report
describes). Every group member's motionNo is contiguous and ascending, which is the check the
loader must apply.

- 95 groups in the writer's numbering (§6 in scratch `parametric.json`; member lists verified
  against the name table):

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

---

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

---

## 6. The writers: decompiled, with exact selection rules

This session the *writers* were decompiled, so the row/column/mirror/duration arguments are now
observed rather than inferred. Headless decompiles are in scratch `o_*.txt`; function bounds come
from `<image>/pesmem.bin`'s `.pdata` via `<scratch>/pdata.py`. All addresses are image VAs (base
0x140000000). Every "row" below is an Animetable row id, resolvable at runtime through the label
table at **0x1434997f0** (§1.2) — the label text is the rule.

### 6.1 The three accessors every writer uses (OBS, small functions, fully decompiled)

```
AnimePlayer*  P   = <the player>          # offsets below are relative to P
tbl           T  = FUN_1406abba0()        # = DAT_1436f2378 → the runtime Animetable
                                    # P->+0x1ac34 + 2*(row*18 + col)   = motionNo+1 (+mirror at bit 15)
row            r  = int(row index)        # Animetable row
col            c  = FUN_1406de990(r, ang) # angle in degrees → column 0..17
populated?       = FUN_1406abdd0(T, &r, &c) != 0
mirror?          = FUN_1406abd60(T, &r, &c) != 0
motionNo         = FUN_1406abb30(T, &r, &c, 0)
```
- **Corrected runtime layout (OBS).** `FUN_1406abba0` is a 7-byte stub `return DAT_1436f2378`
  (scratch `o_supuse.txt`). The runtime table object holds **two matrices**: a u32 table
  `motionNo+1` and a byte table. `FUN_1406abdd0`/`FUN_1406abd60` read
  `*(byte *)(T + col + (row + 0x17ca) * 0x12)` for population/mirror, i.e. a byte matrix at
  `T + 0x2cdc4 + row*18 + col`; the loader `FUN_1406aa440` (decompiled, `o_sup.txt`) writes
  `motionNo = (u32 & 0x1fff) − 1` into the u32 table and the mirror bit into
  `T + 0x1ac34 + row*0x12 + col`. **An empty cell therefore loads as motionNo 0xffff (−1)**, which
  is what `FUN_1406abb30` returns and the writers treat as "no clip".
- `FUN_1406de990(row, angleDeg)` (re-decompiled this session from raw bytes, scratch `v1.txt`): iterates `k = 0..17`; for each
  *populated* column `k` it computes centre `= (k<9 ? k : k−1)·22.5 − 180`, substituting
  −0.001 at `k == 8` and +0.001 at `k == 9`, and keeps the column minimising
  `|ang − centre|`; **default 9** (initial `uVar10 = 9`, OBS: scratch `v1.txt`). The constants are
  read from the image and confirm the centres: `DAT_142598580 = 180.0f`, `DAT_14259d618 = 22.5f`,
  `DAT_1425a9668 = 0.001f` (for k==8), `DAT_1425de6fc = −0.001f` (for k==9),
  `DAT_14259858c = 360.0f` (the wrap). The comparison is done through a double→float rounding
  dance (`(float)(double)(param_2 − fVar11)`), so it is a *float* comparison and ties keep the
  first column in scan order (smallest k). `row == 0xbe5` (3045 = one-past-end
  sentinel) skips the population test and always competes. Columns: −180, −157.5, …, −22.5, −0,
  +0, +22.5, …, +157.5, +180. **No row fallback exists** — an unpopulated (row, col) yields −1
  and the caller must choose something else.
- `FUN_1406abdd0` hard-patches row `0x7c5` col 2 → populated=True, and `FUN_1406abd60` hard-patches
  row `0x7c5` col 2 → mirror=0. Row `0x7c5` = **`kick_mid_0_0_inside_y0_f`** (OBS: this session
  re-derived the id → label by the label table; the earlier report's "row 1989 col 2 → 2484,
  mirror 0" is the same patch: `2484 = kick_mid_0_0_inside_y0_f`'s cell content is *backfilled*
  because that row's own populated cells are the `_keep` set).
- `FUN_1406f0f70(P, motionNo, startTime, mirror, playbackSpeed, duration, transitionType)` is the
  only SetMotion path (AR §5.1). Writes go to P+0x3b50/0x3b54/0x3b58 and P+0x3b80/0x3b88; the
  mirror byte is also stored at P+0x2472 and mirrored into the P+0xad0 bitfield (bit 36).
- `FUN_1406f14e0(P, group, startRate, mirror, ?, duration, blendRate, transitionType)` is the only
  SetParametric path; it pushes one `{motionNo, startTime = clipLength · startRate, mirror}` per
  group member. The startTime multiplier is **300.0** (0x1425a266c) → Fox time units of 1/300 s.
- Wrapper: `FUN_1407649a0` dispatches a 6-word struct — word 0 = 1 → `SetMotion`, 2 → the
  parametric/layer variant `FUN_1406f1280`. Every writer fills that struct.

**Executable rule.** `ChoosePesClip` must reproduce `FUN_1406de990` exactly, including the
population scan and the ±0.001 split, and must reproduce the hard patch on row 0x7c5 col 2. The
mirror bit is per-cell and *not* derived from the row's `_l`/`_r` suffix; read it.

### 6.2 Angle conventions the writers use (OBS, from the decompiled writers)

Each writer computes its column angle from the *target* relative to the actor:
- discrete actions: `angle = atan2(dx, dz)` of (target − actor) with the actor's yaw subtracted,
  wrapped to ±180 (helper `FUN_1406e4230` does exactly `wrap180(src − actorYaw)`).
- rise-up (`FUN_1406deed0`): target = the next waypoint, then `AdjustTransXZ` toward it over
  `round(Hz·6/60)` ticks at factor 0.9 (OBS, AR §4.3.1 — the same function was decompiled in full
  this session; it selects rows `0x913/0x914/0x915…0x91a` by `DemoConnectData` code ×
  next-target distance, and the distance gate is the constant at `DAT_14259bddc`).
- keeper/tackle/block: the angle comes from the *ball or opponent line*, not from a waypoint.

### 6.3 The kick writer (`FUN_1407302b0`, OBS, decompiled in full)

`Kick::Exec`-equivalent. It builds the row from state, not from a string:
```
row = fVar35 (float carrier of a 12-bit row id, packed in the float's bit pattern)
```
Observed id → label mappings read from the decompile (OBS, this session; ids decoded as
`struct` bit patterns of the `double`/`float` constants and resolved through the label table):

| float literal in the decompile | row id | row label | meaning |
|---|---|---|---|
| `6.02558e-43` | `0x1ae` = 430 | `fall_upbody_3_0_180_hold` | foul contact, waist hold |
| `6.12367e-43` | `0x1b5` = 437 | `stagger_upbody_3_0_180_hold_shoulder_appeal` | shoulder contact + appeal |
| `6.06762e-43` | `0x1b1` = 433 | `fall_upbody_3_0_180_hold_waist` | waist hold |
| `6.08164e-43` | `0x1b2` = 434 | `fall_upbody_3_0_180_hold_waist_dive` | waist hold, diving |
| `6.09565e-43` | `0x1b3` = 435 | `fall_upbody_3_0_180_hold_waist_fallforward` | waist hold, forward fall |
| `5.80138e-43` | `0x19e` = 414 | `stagger_ballhit_head_0_0` | ball into head while acting |
| `5.81539e-43` | `0x19f` = 415 | `stagger_ballhit_belly_0_0` | ball into body while acting |
| `5.85743e-43` | `0x1a2` = 418 | `stagger_ballhit_belly_3_3` | ball into body at pace |

So the *same* writer that emits `kick_*` also emits the foul-contact and ball-hit reactions; the
row choice is picked from the animation-state machine, not from a tag on the clip. For an engine
reimplementation this means: **the kick decision and the foul decision share one code path** and the
foul rows are an extra, unconditional output of "a kick against a player happened".

The path to a `kick_*` row (OBS, same function):
1. Compute the **outgoing speed class** `b` from the *ball speed requirements*, not from the
   player's own velocity: `b = (ballSpeed ≥ 22 km/h ? 2 : 1)` when the action is a pass
   (`local_res8[0] == 0`), and the `_keep` variants are chosen when the possession is retained
   (`local_res10[9] == 5`).
2. Compute the **column**: the angle of the desired ball direction minus body facing
   (`FUN_1406e4230`), then `FUN_1406de990(row, angle)`.
3. Special cases OBS in the function body: `row == 0x1ae` (waist hold) and `0x1b5` (shoulder)
   force the column to 0 — i.e. contact rows ignore the angle.
4. If the cell is empty (`FUN_1406abdd0` false) the writer retries with `angle = 0` — this is
   the *only* in-writer fallback in PES.
5. Mirror from the cell bit; `FUN_14072d0b0` supplies the crossfade duration (0.2 s for kind
   0x10/0x11, otherwise computed from the cancel frame).
6. Then `FUN_1406b10d0` converts the chosen clip's hit frame to ticks for the physics touch.

**Executable rule.** The engine's kick decision must produce the row *family + params* itself
(`kick_{short,mid,long}_a_b_{part}_yN[_mods]`), then call the shared column/mirror lookup with the
outgoing-ball-direction angle, then the empty-cell retry at angle 0. No other fallback.

### 6.4 The dribble-touch writer (`FUN_14073f750` head, plus `FUN_14073efb0`, OBS)

- The ball-control/touch row for a *touching* kick comes from a two-row pick:
  `row = 0x51f` (`fk_quick_restart_pick_up_ball`) or `0x520` (`…_near`) when the action kind is
  `0x29/0x2a/0x2b` (dead-ball restarts), else `0x4f7` (`fk_mid_0_0_inside`) / `0x4f5`
  (`fk_long_0_0_infront`) / `0x4f6` (`fk_long_0_0_instep`) by action kind. Column from the
  target angle, mirror from the cell.
- **Free-kick restarts** (OBS, `FUN_14073efb0`): the writer first checks the column by an angle
  cascade (`|Δ| ≤ 22.5 → 2`; `≤ 67.5 → 4`; `≤ 112.5 → 6`; `≤ 157.5 → 9`; `≤ 180 → 11`; else
  `13`; then a mirrored `… ≤ … → 15/17` chain), reads the cell, and only then calls
  `FUN_1406f0f70`. Dead-ball rows in play: `0x4f4` and the `fk_*` families.
- **Throw-in / goalkeeper restarts** use rows `0x51f`/`0x520` for the pick-up, then the family rows
  `throw_*`, `puntkick_*`, `goalkick_*`, `gksidethrow_*`, `gkoverthrow*`, `gkunderthrow_*`
  (§7.3).

**Executable rule.** GF's `BallControl`/`ShortPass`/`LongPass`/`HighPass` all map to the `kick_*`
row families; the *dead-ball* cases map to `fk_*` plus the restart pick-up rows. The distinction
is a **match-phase flag**, not a clip property — GF must supply "dead ball" / "restart kind"
(action kinds 0x29–0x2b) alongside its existing command tuple.

### 6.5 Trap solver (`FUN_140799db0` → `FUN_140798d50` + `FUN_14084b310`, OBS, decompiled)

The trap writer is a small state machine over a solver struct at `param_2`:
- `+0x1c` = candidate row, `+0x24` = candidate column, `+0x20` = "mirror toggled" flag,
  `+0x28`/`+0x2c` = two 1-bit mirror overrides written as `(short)(char)(bit − 1)`.
- `FUN_14084b310(struct +0x1c, col)`: if the row is `0xbe5` (3045 = unset) it returns **0x1125 =
  4389** (the "none" motion) without touching the table. Otherwise it applies the side flip
  (`FUN_140856980`: 0↔0x11, 1↔0x10, 2↔0xf, … 9↔8, 10↔7, 11↔6 — a full 18-column mirror map),
  tests population, and returns the cell's motionNo.
- `FUN_1408527c0(row, col)`: returns the *effective* mirror — reads the cell's mirror bit and
  XORs with the struct's own flag; two special rows `0xa0d` (`trap_3_3_y6_rear`) and `0xa0f`
  (`trap_3_3_y8_rear`) have their mirror **inverted** (OBS).
- The solved struct is filled by `FUN_14084df20` (base row `0xbc6` = `trapstagger_3_3_y0`,
  whose cells are the `dribblerun_stagger*` clips), and `FUN_14084b9c0` re-asks the table with a
  fallback column: `FUN_1406ea840(player, row, class)` walks a **9-table × 18-entry row-priority
  list** at `DAT_1425e4700 + class*0x50` (each entry is an *row id*, indexed by the walk step;
  for class ≥ 9 the index is mirrored through the byte table `DAT_1425e3728` = `{17,16,…,0}`),
  and returns the first row that is populated at the requested column; **the walk terminates
  with the column 0x12 = 18** when nothing is populated. The `class` argument is the 18-bin angle
  class from `FUN_1406eb100`, so the walk is angle-ordered, not arbitrary.
  (OBS, `FUN_14084df20`'s body: `FUN_1406ea840(param_2, 0xbc6, uVar14)` inside the
  `(uVar14 - 3) < 0xc` branch, i.e. only when the requested class is 3..14.)
- **Correction:** the earlier draft called these "9 column-ordered preferred-row tables indexed
  by the demo class". They are indexed by the *angle class* and shared by the demo selector
  (§9.4) and the trap walker (§6.5); their contents are demo rows, which is why the trap walker
  uses them only as a last-resort row list.

**Executable rule.** For traps GF's existing `trap/trapside/trapchase/trapfast/traplift/trapback`
family choice already exists (§4.2); what was missing is the *column mirror map* and the
"preferred row" walk. Implement: side flip by `17 − col` when the solver's side flag is set, then
the 18-entry priority walk at the angle class before giving up. Row 4389/0x1125 is the legal "no
clip" answer — the caller must then not call SetMotion at all.

### 6.6 Tackle / block / slide writers (OBS)

- Tackle (`FUN_140761890`): picks `row` from the per-state tables
  `DAT_1425e77f8` / `DAT_1425e7818` / `DAT_1425e7828`, each 6 × i32, indexed by
  `2·bool + side` (`0..5`). `DAT_1425e77f8` = `{gkrise_faceup_rotateback_0_0_{l,r}` twice,
  `gkrise_sideways_rotateback_3_0_{l,r}}`; `DAT_1425e7818` = the `faceupslow` pair (twice) +
  `faceupslow2`; `DAT_1425e7828` = the `faceupslow2` pair (twice) + `sideways_0_0_{l,r}_catch`.
  The table is chosen by the "special tackle" flag (`FUN_1406e6bf0` parity) and by the
  keeper's hold timer; a second table at `DAT_1425e7848` supplies the `_guard` reaction
  override. `DAT_1425e7838` (6 entries) = the block/tackle-contact rows
  `gkrise_sideways_{0_0,0_3}_{l,r}_catch` (+`_guard`). Column from `FUN_1406de990(row, angle)`,
  with a second attempt at angle 0 when the row is one of `0x6bc/0x6bd` or `0x6b6/0x6b7`
  (`gkrise_sidewaysnear_*` / `gkrise_sideways_*`) and
  the keeper's hold timer has not expired. Crossfade from `FUN_14072d0b0`.
- Block (`FUN_140763560`): `row` from `DAT_1425e7838` (index `2·state + side`), overridden to
  `0x6d8` (`gkrise_facedown_0_3_catch`) when `local_res20[0] == 2`, plus `DAT_1425e7848`
  (`…_guard`) when the keeper's reaction timer allows. `startTime` (passed to `SetMotion` as its
  second argument, via `FUN_1407649a0`) is **0.4 s** (`DAT_14259b89c`) when the chosen clip is
  `0x7cc` (`kick_mid_4_0_inside_y0_v2_f`) and the reaction counter at `P+0x37c8` is < 2, and
  **0.2 s** (`DAT_142598578`) when it is < 3, else 0. duration
  `= (DAT_1425656c0 / tickHz) · _DAT_14258c264` = `4/tickHz · 3.0` s.
- Slide (`FUN_140764fa0`): `row = 0x6bc + (mirrorFlag ? 1 : 0)` → `gkrise_sidewaysnear_0_0_{l,r}`,
  column from the slide direction; three post-hoc motionNo overrides (0x7c0 → 0x7be/0x7bf by
  angle ≥ 135°, 0x7c2 for the "stepped" variant). **Note:** the row ids here are the
  `gkrise_*` *rows*, whose cells contain the actual `sliding_*` clips — the row label and the clip
  family differ, which is why the row-name grammar (§1.3) must not be read as a clip taxonomy.

### 6.7 The jostle / dodge / feint writers (OBS)

- Feint (`FUN_140728420`): a pure decision tree returning row ids:
  - `iVar3 == 1` (trick family 1): `param[10]` (speed-out class) in 3..5 and `param[9]` (speed-in)
    in 3..5 → `0x11d` (285) `blockfront_4_0_y06` if the power ≥ 22 km/h else, for the named
    tricks, one of the 4-entry tables `DAT_1425e5d30` (`blockfront_2_0_y03_shoot_reaction` +
    v2/v3) / `DAT_1425e5d40` (y04 set); else `0x114`/`0x11b`/`0x119` by speed-out;
  - else `0x11a`, `0x109`, `0x10a`, `0x10b/0x10c`, and the `0xef/0xf0/0xf2/0xf3/0xf4/0xf9` set
    with the same 4-entry tables at `DAT_1425e5d18` (`_y03_`) and `DAT_1425e5d08` (`_y02_`),
    indexed by `FUN_1406e6bf0` (a per-player random/personality pick).
  The `blockfront_*_shoot_reaction` rows' *cells* hold the `dribble_*`/`jostle` clips, so again the
  row label is not the clip family.
- Throw-in (`FUN_14073efb0`): the column cascade of §6.4, rows `0x4f4` / `0x4f5` / `0x4f6` /
  `0x4f7`.

### 6.8 Parametric group selection (OBS, this session — the missing piece)

`FUN_1406b8340` (the MOVE/STEP_MOVE exec) ends with:

```c
if (local_67c == 0x11)  SetMotion(iVar9, …)          // single step clip
else  SetParametric(group = DAT_14344e0e0[local_67c], startRate, mirror, …)
```

and `DAT_14344e0e0` (**image VA, 17 i32 entries, read from the image this session**) is the
state → group map:

| state | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| group | **80** | 85 | 88 | 88 | 89 | 89 | 82 | 84 | 81 | 93 | 86 | 90 | 90 | 91 | 92 | 87 | 83 |

(80 = canonical run blend; 85/86 = `near_run_*`; 88–90 = sidestep/`parallel`/`heisou`;
82/84/83/87/91/92 = jostle-guard / relax / tired variants; 81 = `run_adjust_*`; 93 = `run_4_4_v2`.)
The state index itself is `DAT_14344e380[state · 16]`'s first i16 — a 150-entry table of
`{i16 demoRow, i16 demoRow2}` pairs (read this session), defaulting to
`(_DAT_14344e620, …)` outside 1..0x96. This is the table that makes "guard / relax / tired /
sidestep / near-ball" variants selectable at runtime.

**Executable rule.** GF must compute the same 17-valued movement state (its existing
`CrudeSelection` movement families + pressure/stamina flags map onto it) and read group from an
equivalent runtime table. The group membership then comes from `Parametricblendtable.bin` (§3).
This closes the last "INF (medium)" in the draft's locomotion section.

---

## 7. Priority 2 — the kick taxonomy, both sides

### 7.1 PES side: what each kick row family actually encodes (OBS, from all 278 `kick` rows +
1,048 cells — re-counted this session)

Grammar `kick_{len}_a_b_{part}_yN[_mods]`, plus the off-angle families
`kick{side,rear,far,fardown,sliding,rabona}_*`:

| token | values (count) | meaning (OBS, from cell contents) |
|---|---|---|
| `len` | `short` / `mid` / `long` | pass length/power class; `fk_*` rows are the dead-ball restatement of the same three |
| `a` | **0 (111 rows), 3 (135), 4 (23), 2 (1)** | incoming speed class (§4.1). **OBS corrected: there is exactly one `_2_` kick row** (`kick_long_2_0_y0_charge`) — the jogging class is essentially absent, so a jogging entrant takes a `_3_` row |
| `b` | **0 only, in the row grammar** (111 `_0_`, 8 `_4_` per the pair histogram below) | outgoing speed class; `b = 0` rows carry the `_keep` cells (§13 E3 shows the `_keep` cells live on the standing `a = 0` rows, not on the moving ones) |

Speed-pair histogram over the 278 `kick` rows (OBS, label census):
`a_b` = `0_0` ×111, `3_0` ×127, `4_0` ×15, `0_4` ×8, `3_4` ×8, `2_0` ×1. The `b = 4` rows are
the power/charge variants; `b` is never 1/2/3/5.
| `part` | `inside` `instep` `outside` `toe` `sole` `heel` `infront` `overhead` | strike surface; OBS from HitData `hit_bone` + the `sf_*` fields |
| `yN` | 0–9 | contact height (§4.1) |
| mods | `keep` `adjust` `control` `doubletouch` `bodyfake` `jostle` `late` `stagger` `charge` `punch` `curve` `lob` `grounder` `highpower` `topspin` `cancel` `nostep` `stepback` `direct` `scoop` `max` `weak_f` `blast_down` `2tep` `placekick` `push` `kr` `kfl` `kl` `lr` `draw` `fake_pass_000` `fake_shoot_f045` `tacklefoot` `heelflick` `v2/v3/v4` `f/l/r`+angle | per-situation refinement (OBS: each modifier appears in ≥1 populated cell) |

The kick *families* and the modifier set are the PES vocabulary. The full clip-name list is PES
data and is regenerated at runtime (mtar name hashes ↔ exe name table, §6.2 of the loader).

**What decides the row (OBS, from the writer §6.3):** (1) dead-ball vs open play, (2) the ball
speed the shot/pass needs, (3) the touch height, (4) whether possession is retained, (5) whether
the actor is off-balance (`adjust`/`control`/`stagger`). What decides the **column**: the outgoing
ball direction vs body facing. What decides **mirror**: the cell bit, which the authoring set so
that one side's clip serves the other.

**What decides "superior kick" selection.** The mechanism is now fully decoded:

- `Idletable.bin` / `Movetable.bin`, **151 × 20 B** each, decoded this session from the parser
  `FUN_1406aa590` (`param_4 == 1` → Idletable, `== 2` → Movetable; scratch `o_sup.txt`). Each
  record is 5 u32 words `w0..w4` (record `r` byte offset `r*20`, no header — 3020 B / 151 = 20 B
  exactly, OBS), and each word packs two 12-bit Animetable row ids **+1** (0 = none).
  The **slot order differs between the two tables**, which is why a single fitted order never
  worked:

  ```
  Idle (param_4==1): slot 0 = w0>>12, slot 1 = w0&0xfff,
                     slot 2 = w1>>12, slot 3 = w1&0xfff,
                     slot 4 = w2>>12, slot 5 = w2&0xfff,
                     slot 6 = w4>>12, slot 7 = w4&0xfff
                     (two 1-bit flags at slot 0x28/0x2c = w2>>25 and w0 top bit)
  Move (param_4==2): slot 0 = w1&0xfff,      slot 1 = w0>>12,
                     slot 2 = w2&0xfff,      slot 3 = w1>>12,
                     slot 4 = w0&0xfff,      slot 5 = w3&0x7ff,
                     slot 6 = w2>>12,        slot 7 = w4&0x7ff
                     (flags at slot 0x28/0x2c = w-2>>25 and w0 top bit)
  ```
  All reads are masked `& 0xfff` except the two `& 0x7ff` Move slots (an 11-bit field), and every
  value is stored `−1` (OBS from the parser source; `FUN_1406aa590`'s subtype-1/2 loops write
  `((ushort)… & 0xfff) − 1` into consecutive u16 at `+0, +4, +8, …`).
- **The slot enum unit is solved (ID 1..8 = the ball-height class) for the header records and is a
  fixed per-sub-type enum for the kick records** (OBS, histogram over all 151 records, decoded
  with the orders above):

  | subtype | slot 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
  |---|---|---|---|---|---|---|---|---|
  | Idle | y0 | y5 | y8 | **y9** | y4 | y2 | y6 | **y7** |
  | Move | y0 | y1 | y2 | y3 | y4 | y5 | y6 | **y7** |

  OBS: in Idle, slot 3 always holds a `_y9` row and slot 7 a `_y7` row; in Move the slots are
  *ascending* y0→y7 (42 of 121 non-empty Move records are exactly this pattern, e.g. Move rec 42 =
  `kick_mid_3_0_inside_{y0,y1,y2,y4}` in slots 0/1/2/4). Idle's map is deliberately *not*
  ascending — it is an authoring order (y0, y5, y8, y9, y4, y2, y6, y7) that must be reproduced
  verbatim. So the engine's lookup is a **table, not a formula**: for Idle, the incoming height
  class picks the slot by the map above; for Move, by the ascending order. Falling back when a
  slot is empty is legal only by trying the adjacent populated slot (INF, high — the records are
  sparse: 30 of 151 Idle records are empty, and the y9/y7 pair repeats in nearly every header
  record).
- Record meaning: the *record index* is the kick/header **situation**
  (Idle rec 1–26 = the standing header set, rec 28–150 = standing passes/shots/clearances,
  rec 148 = `kick_tacklefoot_y*`, rec 149 = `head_tacklehead_*`, rec 150 = the five
  `_cancel` slots; Move mirrors it with `_3_0` rows and adds `dribblerun_*`/`dribbleslide*`
  members in the same slots). OBS: Idle rec 5 = `head_0_0_y{4,6}_shoot_down_late_f` +
  `head_3_0_y7…`, Idle rec 23 = the `_chest_f` set, rec 22 the `_shoulder_f` pair, rec 25
  `_jostle_f` — the record is the **body-part/situation** and the slot is the **height**.
- The two 1-bit flags per record are unused by any path decompiled this session (INF, medium):
  treat them as "primary option" markers.
- `Kickinfo.bin` (2,416 B = 1 header + 75 × 32 B) is parsed by the same class with `param_4 == 0`
  into 13 × 0x30 B records of 7-bit fields: two raw u32 copied verbatim (`+0x714`, `+0x970`), and
  from each u32 four derived fields — `byte` (bits 0–6), `byte<<8` (bits 8–14), **signed
  `(bits 15–21) − 0x3c`** (the `−60` the earlier report saw: it is a signed angle in 7-bit
  excess-60), `byte<<22` (bits 22–28) — each stored as a float into a per-record slot
  (OBS: `+0x714/0x970` verbatim; `−0x25c, +0, +0x25c, +0x4b8` = the four derived). The 75 records
  carry `f32 5.0` and `f32 0.5` tails. **INF (medium-high): these are per-kick tuning records
  (angle limit, power class, cancel allowance) consumed by the `[Kick Cancel Rate]` logic, not
  clip selectors.** The writer path `FUN_1406b10d0`/`FUN_1406b11e0` converts a chosen clip's hit
  frame to ticks using them; a standalone kick selection does not need them.
- `Slidedribble.bin` (**697,172 B**, OBS from the VFS listing; the draft said 697 KB, which was
  rounded) — the `dribbleslide*` cells are addressable through the Animetable (OBS: 21 + 17 + 15 +
  … rows exist in the row grammar with populated cells), so it stays off the critical path (§9).

### 7.2 GF side: the vocabulary the renderer must answer for (OBS, from `src/sim`)

`e_FunctionType` (Ghidra-readable enum, `src/sim/gamedefines.hpp:52`):
`None, Movement, BallControl, Trap, ShortPass, LongPass, HighPass, Header, Shot, Deflect, Catch,
Interfere, Trip, Sliding, Special`.

Where each is requested (OBS, grep over `src/sim`):

| GF function type | requested by (call-site counts, OBS `grep -rn "desiredFunctionType = e_FunctionType_"`) | extra parameters the request carries |
|---|---|---|
| `Movement` | `elizacontroller.cpp` (9), `playercontroller.cpp` (3), `humanoidbase.cpp` (2), `refereecontroller.cpp` — 15 sites total | `useDesiredMovement`, `desiredDirection`, `desiredVelocityFloat`, `useDesiredLookAt` |
| `ShortPass` | `elizacontroller.cpp` — 7 sites | `desiredPower`, `desiredDirection`, pass type from `playerskills.cpp GetPassDifficultyMultiplier(mask, passType, isCross)`; aliased to `LongPass` in `SelectAnim` (2 `LongPass` sites → the same pool) |
| `HighPass` | `elizacontroller.cpp` — 6 sites (cross / lob path) | same as ShortPass |
| `Shot` | `elizacontroller.cpp` — 3 sites | `desiredPower`, `desiredDirection` |
| `Special` | `elizacontroller.cpp` (celebration, `specialVar1 = celebrationType`), `refereecontroller.cpp` (`specialVar1 = 3`, the whistle) | `specialVar1/2` |
| `Trap` | `playercontroller.cpp:443` | 180/120 ms ball prediction → `incomingBallDirection`; `allowTrapReQueue` |
| `BallControl` | `playercontroller.cpp:345` (+ the trick copy pushed right behind it, :405) | `touchInfo.desiredPower/Direction`, `useSpecialVar1/2` (trick), `onlyDeflectAnimsThatPickupBall` |
| `Deflect` | `playercontroller.cpp:299` | `onlyDeflectAnimsThatPickupBall`, `pickupBall` query |
| `Interfere` | `playercontroller.cpp:487` | `incomingBallDirection` |
| `Sliding` | `playercontroller.cpp:519` | — |
| `Trip` | `humanoidbase.cpp:1342` | `tripType` 1–3, bump direction |
| `Catch` | **never requested** (OBS, grep) — keeper saves run as Deflect + physics | |
| `Header` | **never requested** (OBS, grep) — headers happen as ShortPass/Shot touches with the head | |

GF velocity buckets (`src/sim/onthepitch/velocitystate.hpp:15-18`): `idleVelocity = 0`,
`dribbleVelocity = 3.5`, `walkVelocity = 5.0`, `sprintVelocity = 8.0` m/s, quantised by
`FloatToEnumVelocity`. PES thresholds {2.5, 8.0, 13.5, 22.0} km/h (OBS, live table at
0x14344e250, `FUN_1406deb30`) = {0.69, 2.22, 3.75, 6.11} m/s. GF `e_Velocity_{Idle,Dribble,Walk,
Sprint}` → PES digits {0, 2, 3, 4} (OBS, §4.1 of this report; note GF "walk" is PES "run").

GF per-clip variables the sim writes today (`animcollection.cpp`, `humanoid.cpp`):
`type, incomingballdirection[_maxdeviation], balldirection[_…], touch_bodypart, touchframe, steps,
triptype/bumpdirection, outgoing_special_state, outgoing_retain_state, lastditch, specialvar1/2,
imported`. Every one has a PES counterpart (§5). The two that have **no** PES counterpart are
`outgoing_special_state`-as-a-clip-variable chain (PES keeps it in `demo_connect_data` per clip,
not per transition) and `steps` (PES uses `foot_type` events instead of a strike count).

### 7.3 The compact mapping table

Left = the GF event/decision the renderer needs a clip for. Right = PES clip family + the exact
selection rule. "Table" = the runtime file the engine reads.

| # | GF state / event (left) | PES family (right) | selection rule (right) |
|---|---|---|---|
| 1 | `Movement`, steady, speed v | `Parametricblendtable.bin` group `DAT_14344e0e0[state]` (80 canonical) | rate = v / 8.96; startRate ∈ {0,.25,.75} from foot phase; duration 0.3 |
| 2 | `Movement`, step/stop/turn | `Freemove.bin` key `state·10 + digit` | state = `[0][in][1][style][out][1]` (§2.2); digit 0..4 / 5..9 mirrored; duration 0.133–0.167 |
| 3 | `Movement`, sidestep/back/parallel | same FSM, families 2/3/5 | style digit 2/3/5; transitionType 2 or 6 (OBS live) |
| 4 | `BallControl`, plain touch | `dribble_a_b_{in,out,sole,nutmeg,rear,mid,axisback}_{l,r}` | a/b §4.1; `in`/`out` from touch geometry; row suffix = dribbling foot; column = next-touch direction |
| 5 | `BallControl` at pace | `dribblerun_3_3_[angle]_in/out`, `dribbleslide*` | in ≥ 2; Slidedribble cells via Animetable |
| 6 | `BallControl` + `specialVar1` (trick) | the ~200 trick rows of §9.2 (`kickfeint_*`, `feint_*`, `feintrun_*`, `doubletouch_*`, `scissors*`, `roulette*`, `elastico*`, `nutmeg*`, `bodyfake*`, `lfeint/inout/outin/outout/onetime/stepkick/slidebodyfake/sliderabona/drawopencloase/parry*`) | GF token → family (§9.2); rows `FUN_140728420` is the *feint* decision tree (§6.7) for the `kickfeint`/`bodyfake` subset; no fallback to plain ballcontrol |
| 7 | `Trap` frontal | `trap_a_b_yN_[part]_[in,out,near,far]` | a/b §4.1; yN from predicted contact height; part = nearest surface; column = incoming ball direction |
| 8 | `Trap` side-on | `trapside_*_l/_r/guardside` | row side must match ball side; else mirror; see `trap_3_3_y6_rear`/`y8_rear` mirror inversion (§6.5) |
| 9 | `Trap` running on | `trapchase_*` (out ≥ 2) / `trapfast_*` (one-touch) / `traplift_*` (flick) / `trapback_*` (behind) / `trapthrough*` (let-through, AI-called) | as 7 + "behind" determined by the incoming ball's z sign |
| 10 | `Trap` under pressure | `dribble_*_guard*`, `trapside_*_guardside`, `js_*` | pressure flag → guard rows |
| 11 | `ShortPass` / `LongPass` | `kick_{short,mid,long}_a_b_{part}_yN[_keep]` + `fk_*` for dead balls | len from desired power; `b=0`+`_keep` for a set pass; part: inside=short/set, instep=driven, outside=bend, infront=reach, toe/heel/sole=desperation |
| 12 | `HighPass` / cross | `kick_*_y2..y4` + `_lob`/`_curve` mods, `fklong` variants | yN ≥ 2 for a lofted ball; `isCross` from `GetPassDifficultyMultiplier` |
| 13 | `Shot` | same `kick_*` rows + `_shoot/highpower/curve/lob/grounder/v2/v3/max/blast/2tep`, `kickfar*`, `kickside*`, `kickrear*` | power class → len + mods; angle ≥ 90° → side/rear families; distance → `kickfar*` |
| 14 | Set piece: free kick | `fk_{short,mid,long,quick}_3_0_{part}` + named run-ups | restart kind; only when dead-ball flag set |
| 15 | Set piece: penalty | `pk_enclose_*` rituals, `pk_*` | penalty phase |
| 16 | Set piece: kick-off | `kickoff_loop_*` (OBS live) | kick-off phase |
| 17 | Throw-in (outfield taker) | `throwin_*` rows 2488–2498: `throwin_idle` → `throwin_loop`, `throwin_side_step[_f]` → `throwin_slide_1_1_*_onehand_ver21`, `throwin_run_3_3` → `throwin_run_3_3_000_onehand_ver21`, `throwin_release_{idle,run}_{short,mid,long}` → `throwin_release_*_000/067_*`, `throwin_pickupball` → `throwin_pickupball_{000,090,180}`, `throwin_seamless_liftupball` | restart kind + throw length (short/mid/long); the pick-up row is chosen by the ball side; the run-up by speed (idle ≤ 1 < 3_0 ≤ 3_3) |
| 18 | Goal kick (keeper) | `goalkick_*` rows 127–136 (`coaching_lower/up_{l,r}`, `idle_{l,r}`, `inside_{l,r}`, `instep_{l,r}`) → `gkcoaching_high_0_0_{lower,up}_goalkick`, `gkgoalkick_idle_0_0`, `gkgoalkick_inside_0_0_{000,045,f045}`, `gkgoalkick_instep_0_0[_f045]` | goal-kick phase + running start; `fk_quick_restart_*` rows 1311–1314 handle the quick restart (`quick_restart_step*`, `quick_restart_set_loop`, `quick_restart_to_kamae`) |
| 19 | Corner | `corner_defence_idle` → `postman_loop`; the taker's corner uses the `fk_*` rows (dead-ball vs moving is a match-phase flag, §6.4) | corner phase; GF must add the corner phase itself |
| 20 | Keeper distribution | rows 150–177 (`throw_over_0_0[_hard][_nearline][_cancel]` → `gkoverthrow*`, `throw_under_0_0[_fast][_nearline][_cancel]` → `gkunderthrow*`, `throw_side_0_0[_cut][_cancel]` → `gksidethrow*`, the `_3_0`/`_3_3_bothhard` variants), rows 137–149 (`puntkick_*` → `gkpuntkick_*`), rows 127–136 (`goalkick_*` → `gkgoalkick*`) | §8.2: the keeper phase byte `P+0x245c` = 4/5 + the target position (aim X clamped to the keeper's own X ± 1.5 m, `DAT_14259bddc`) |
| 21 | `Deflect`, outfield block | `blockfront_a_0_yNN[_shoot[_near,_reaction]]`, `blockside_a_0_yNN[_sliding][_reaction][_180heel][_fishdive]`, `block090_3_0_yNN[_000][_far][_near][_sliding][_heel][_hip][_foot_high][_fishdive][_180]`, `blockhead`, `block` | writer `FUN_140763560` (§6.6); yNN from shot height (00–09); `blockside_*_fishdive`/`block090_*_fishdive` = dive blocks |
| 22 | `Deflect`, loose-ball redirect | `trapfast`/`trapchase` at the right height with `pickupBall` | GF nearest-surface rule unchanged |
| 23 | Keeper save (any) | `Parametricblendtable.bin` groups 0–79 at parameter N/11 | §8.2(b); group 18 = `gkcatch_s01*`, 15 = the hold branch, 71–75 = punches, 34–70 = deflects |
| 24 | `Interfere` (duel, no ball) | `js_*` (40 rows: `pushfront/protect/blockback/blockside/trapthrough/variation/idle/run` × `loop/main/sub/win/lose/passcut/tacklefoot`) + `js_run_*_loop_guard_*` (param groups 82–84) | pressure → guard variants; `tacklefoot/tackleshoulder` for the contact |
| 25 | `Sliding` | `sliding_a_0_{l,r,block,legscissors,parallel}[_angle]` (19 rows) + `dribbleslide045/090/f45/f90/00/45/90/180/000/135` and the L9–L12 `slide_*_guardback_y0_sole/notouch_ver20` multi-touch rows | a max 3 (no `_4` rows exist); `_l/_r` = side; column = slide direction; end lie from `demo_connect_data` |
| 26 | `Sliding` with contact | `tacklefoot_*` (14), `tacklehold_*` (16), `tackleshoulder_*` (10), `tacklepulled_*` (2) | contact flag; then the same lie chain |
| 27 | `Trip` type 1 (bump) | `stagger_{air,lowbody,upbody}_a_b[_hard,_tackle,_hold,_pulled,_guard]` (49 rows) | a = §4.1 of the trip speed |
| 28 | `Trip` types 2/3 (fall) | `fall_{air,lowbody,upbody}_a_0[_front,_sliding,_move,_hold,_lose,_carry,_facecover]` (62 rows) | direction bucket = trip direction |
| 29 | Getting up after a fall | `riseup_{allfours,facedown,faceup}_0_{0,3}` (6 rows) | writer `FUN_1406deed0`: lie code from `demo_connect_data`, `_0` (stay) if the next target is within 1.5 m (`DAT_14259bddc`) else `_3` (run off); `AdjustTransXZ` toward the waypoint over `round(Hz·6/60)` ticks at factor 0.9 |
| 30 | Keeper getting up | `gkrise_*` (67 rows) | lie code + catch flag + side; writer tables `DAT_1425e77f8`/`0x1425e7818`/`0x1425e7828`/`0x1425e7848` and `DAT_1425e7838` (§6.6) |
| 31 | `Special` (celebration) | `demo_*` (367 rows: `demo_goal_*`, `demo_guts*`, `demo_appeal_*`, `demo_move_*`, `demo_cheer_*`, `demo_hugset_*`, `demo_pfm_*`, `demo_fail_*`, `demo_coop_*`), `dm_*` (7), `TimeUp_*` (32), `HalfEnd_*` (13) | `HasSpecial(var1, var2)` contract unchanged; intro then loop; the class from `FUN_1406eb100(angle)` and the row from the `DAT_1425e4700` priority lists (§9.4) |
| 32 | Referee / officials | `judge_*` (15), `linesman_*` (26), `assistant_*`, `layer_*` upper-body layers (mask `LRArm`/`LArm`/`RArm`/`Full`), `wall_*` (12) | `e_FunctionType_Movement` + `Special` from `refereecontroller.cpp`; layer masks only for officials |
| 33 | Injury / knockdown | `stagger_*` + `fall_*` + `riseup_*`, plus the keeper `gkfall_*`/`gkstagger_*`/`gkrise_*` rows | same as 27–30; GF's `foulsequence.cpp` supplies the foul type |
| 34 | Foul contact committed (the other side of a foul) | rows 0x1ae/0x1b1–0x1b3/0x1b5/0x198/0x159 (`fall_upbody_3_0_180_hold*`, `stagger_upbody_3_0_180_hold_shoulder_appeal`, `stagger_upbody_3_3`, `fall_lowbody_3_0_v2_l`) | emitted by the *same* kick writer, §6.3; **column forced to 0** for the contact rows |
| 35 | Feint/dummy (not a trick) | `kickfeint_*` (34), `bodyfake*` (6) | §7.3 row 6 and §9.2 |
| 36 | Trick move (`specialVar1` feint) | the ~200 rows of §9.2's table | GF token → PES family; `_0_3_l/_0_3_r` at jog, `_3_3` at pace; no fallback to plain ballcontrol (GF drops the command, `HasSpecial` semantics) |
| 37 | Dodge an incoming ball/player | `dodge_{human,ball,net,post}_*` (38 rows), `avoidjump_*`/`avoidslide_*` clips | GF Interfere/Deflect with a **dodge flag** — **open, §14.5** |
| 38 | `Header` (never requested) | `head_*` (115 rows) + `headclear/headside/headfar/headfardive/headrear/headfarside/headrearfardown/haed` at y1–y9 via `Idletable.bin` (standing) / `Movetable.bin` (moving) | §1.5; the two-table slot map (§7.1) is the selection key; `_f` forward / `_r` rear |
| 39 | `Catch` (never requested) | `gkcatch_*` groups 9–25 + the hold branch (group 15) | §8.2; catch-vs-parry is the action-state decision |
| 40 | Ball hit a bystander / kick against a player | rows 0x19e/0x19f/0x1a2 (`stagger_ballhit_{head,belly}_0_0`, `stagger_ballhit_belly_3_3`) | same kick-writer path, §6.3 |

Rows 1–3 answer every *locomotion* query, rows 4–6 every *possession* query with the ball at the
feet, rows 7–10 every *reception*, rows 11–13 every *deliberate strike*, rows 14–20 every *set
piece*, rows 21–23 every *interception*, rows 24–26 every *duel*, rows 27–30 plus 33 every
*knockdown and recovery*, rows 31–32 every *celebration and officiating*, rows 34/40 every
*foul contact*, rows 36–37 every *trick and evasion*, rows 38–39 the two headers the GF sim never
requests (kept for completeness). **Every GF event that needs a clip has a PES row family; the
only entries with no PES answer at all are the dodge flag (§14.5) and the ~60 two-touch turn rows
that GF's 11-move feint enum cannot express (§9.2).**

**Missing GF-side inputs the table above needs from the sim** the table above needs from the sim** (OBS, gaps in `src/sim` today):
a dead-ball/restart phase flag with the restart kind (free-kick / throw-in / goal-kick / corner /
penalty / kick-off); a keeper possession state (idle/ready/set/hold/thrown/kicked); a pressure flag
on the touch request (GF has `matchpressure.cpp`, but it is not on the animation command);
a foul-type discriminator; and a "catch vs parry vs punch" decision. None of these exist as
animation-command fields today — they are derived from match phase and ball state, exactly where
PES gets them.

---

## 8. Priority 3 — keepers

### 8.1 Which keeper clips exist (OBS; the counts are from the name table at 0x14351e590)

870 of the 4,389 clips are keeper clips (`gk*` prefix; 51 `gk*` families). The families and
their counts (OBS, clip-name census, scratch `tables.json`):

| family | clips | role |
|---|---|---|
| `gkdeflect` (+`Foot/Expect/Expect3/Clear/pk/lie/otherside/snapunder/stagger`) | 110 + 21 + 11 + 6 + 6 + 5 | the deflect save arms of the height blend |
| `gkcatch` (+`s01/s03/b01/line/hold/stand/dive/diveguard/long/fast/light/hard/back/slide`) | 97 | the catch save arms, incl. the hold branch |
| `gkmovenear/SeriesA/SeriesB/mid/low/high/heisou/lie` | 69 + 44 + 27 + 32 + 22 + 17 + 1 + 1 | the with-ball carry loops |
| `gkcollapsing` (+`Foot/stand/crossbar/upperhand`) | 58 | catch-then-collapse |
| `gkrise_*` | 51 clips / 67 rows | get-ups (rows carry the lie+side+catch/modifiers) |
| `gkprejump_*` | 48 | the pre-save crouch/commit |
| `gkdeflectscoop`, `gkdeflectlate` | 34, 30 | scoop and late deflect |
| `gkblockcover` | 31 | body-block saves |
| `gkpunch` | 18 | punch clears |
| `gkpuntkick_gkunderthrow_gkoverthrow(_Step/Bothhands/hign)_gkoverthrowStep` | 13 + 13 + 12 + 5 | distribution |
| `gkscoopout_lie`, `gksavingCancel`, `gkblock`, `gkfall/gkstagger/gkclear` | 13, 12, 11, 8, 6, 6, 4 | lie variants, aborted saves, blocks, knockdowns |
| `gksidethrow(_cut)`, `gkpickupball`, `gkdropball`, `gkgoalkick(_idle)`, `gkcoaching`, `gkseeoff`, `gkmovehigh_idle` | 6+1, 5, 6, 5+1, 2, 8, 5 | the rest of the cycle |

The discrete rows for these (67 `gkrise_*` rows, 10 `gkblock`/`gkgk*` save rows, the `throw_*`
rows, `goalkick_*`, `puntkick_*`) all live in the Animetable like every other clip (§1.1).

### 8.2 The "pre-save → save → hold → wait → distribute" state machine (OBS, decompiled this session)

`FUN_140791ec0` is the keeper **action state machine**. Its state is a byte at `P+0x245c` (the
previous state is kept at `P+0x245d`), the requested action class arrives through a vtable on
`P+0xab8` (`(*(*plVar2+0x20))(plVar2)` → the current action kind), and the loop is a 6-case
switch on it (OBS, scratch `o_gk.txt`):

| case | kind byte | what it does | clips / rows |
|---|---|---|---|
| 0 | 0 | enter the pre-save: clear the motion delay and the "stand" timers, move to state 1 | — |
| 1 | 1 | **PreSaving**: read the shot's predicted aim point (`FUN_1406b03d0` at action `0xcf5`), project it against the keeper's `P+0x20` yaw and the pitch half-width (`DAT_14257a194 = 0.7f`), pick row **0x9bd = `throwin_pickupball`** for the "ball is right at me" branch, else keep the crouch; writes the arm target at `param_1+0x8/0xc/0x10` and sets `param_1+0x1c` (an arm flag, 0 or 1) from the action kind (13/4) | `gkprejump_*` (the crouch rows are the ones the shot writer selects, §6.6/§7.1) |
| 2 | 2 | **Catch/Hold**: if the current motion has passed its catch frame (`FUN_1406ec260` true) set `P+0x2460 |= 0x300` and move to state 3 | `gkcatch_*_hold_*`, `gkcollapsing_*_stand` |
| 3 | 3 | **Hold & look**: if the drop/abort test (`FUN_1406ebf80`) fires, hand the ball back to the AI (the match object at `P+0x3ac8`), otherwise compute the turn to the next teammate / the distribution target, rotate toward it with a ±30°/±360° yaw step, and queue a ball-carry path at `P+0x4d0`. When the "release" test passes, **row 0x9bf = `throwin_run_3_3`** is played with mirror from the cell and `P+0x2479 = 1`, and the state moves to 4 | `gkmovehigh_*_ball`, `gkmoveSeries*` |
| 4 | 4 | **Distribute/Aim**: read the target at `param_1+8`, wrap the aim yaw, clamp the turn to ±360°/rand, call `FUN_141eb56d0` to rotate the keeper toward the target, clamp the aim X to the keeper's own X ± 1.5 m (`DAT_14259bddc = 1.5f`) and Y to `target − 0.65` (`DAT_14259d5d0`) etc.; **if the target is behind the goal line** (X < own X − 0.5) play row **0x9be = `throwin_seamless_liftupball`** and go to state 5; otherwise keep aiming | `gkunderthrow_*`, `gksidethrow_*`, `gkgoalkick_*` |
| 5 | — | **Release** (entered from 4) — the clip's own release frame plays and the ball leaves | `gkoverthrow*`, `gkpuntkick_*` |

Distribution rows (OBS: the rows whose cells hold the `gk*throw*`/`gkgoalkick*` clips):

| row id | row label | clip |
|---|---|---|
| 150–177 | `throw_over_0_0[_hard][_nearline][_cancel]`, `throw_under_0_0[_fast][_nearline][_cancel]`, `throw_side_0_0[_cut][_cancel]`, `throw_over_3_0_high/hard/hard2/longhard/cancel`, `throw_under_3_0_light/fast[_cancel]`, `throw_side_3_0[_cancel]`, `throw_over_3_3_bothhard[_cancel]` | `gkoverthrow_*` (21), `gkunderthrow_*` (13), `gksidethrow_*` (7) |
| 137–149 | `puntkick_{front,side}_0_0[_nearline][_fast][_fast2][_hard][_highpunt][_cancel]` | `gkpuntkick_*` (13) |
| 127–136 | `goalkick_coaching_{lower,up}_{l,r}`, `goalkick_idle_{l,r}`, `goalkick_inside_{l,r}`, `goalkick_instep_{l,r}` | `gkcoaching_*` (2), `gkgoalkick_idle_0_0`, `gkgoalkick_inside_0_0_*` (3), `gkgoalkick_instep_0_0` |
| 2488–2498 | `throwin_release_{idle,run}_{short,mid,long}`, `throwin_pickupball`, `throwin_seamless_liftupball`, `throwin_run_3_3`, `throwin_idle`, `throwin_side_step[_f]` | `throwin_*` (16) — the **outfield** throw-in, not keeper |

**Correction:** the earlier draft's row 119 listed `throw_*` (29 rows) as the *outfield* throw-in.
OBS: rows 150–177 (`throw_*`) hold the **keeper** distribution clips; the outfield throw-in rows
are 2488–2498 (`throwin_*`). Both are used, but by different actors.

Two disjoint mechanisms remain for the *save itself*:

**(a) Discrete keeper actions → Animetable rows + kinematic clips** (OBS, decompiled writers):

| keeper state | families (row count) | writer / evidence |
|---|---|---|
| ready / set (before the shot) | `gkprejump_0_0_{near,mid,pk_front,…}[_min][_move[_front/push]]`, `gkprejump_3_0/3_3_y05_rotate{Step,Run}_{000,045,090,180}[_Otherside]` | name inventory; the pre-jump crouch the keeper plays while the shot is in flight |
| save, standing | `gkdeflect_f00_0_0_y00..y10`, `gkdeflectscoop_f00b01_*_yNN_000`, `gkdeflectlate_*`, `gkdeflectExpect[3]_*`, `gkdeflect_s01_otherside…after…`, `gkdeflect_pk_s01_*` | rows + Parametric groups 34–46, 55–70 |
| save, diving | `gkcatch_f02_dive/f03_dive/f03_diveguard_3_0_y00..y06[_fast]`, `gkblockcover_f0X_dive_3_0_y00..y06`, `gkpunch_f03_3_0_y02..y11` | groups 2–3, 10–11, 71–75 |
| save, side/low | `gk{deflect,collapsing,punch,blockcover,catch}_s01..s05_0_0_y00..y10`, `gkcatch_s01_slide_*`, `gkscoopout_lie_s02/s03/s04_3_0_y00..y05_090` | groups 4–8, 18–25, 27–33, 54–64, 76–79 |
| catch & hold | `gkcatch_f00_hold_0_0_y00..y04` (group 15), `gkcatch_*_stand`, `gkcollapsing_*_stand` (group 29) | OBS: the `_hold`/`_stand` modifiers are the *hold* branch of the same height blend |
| block with the body | `gkblockcover_f00/s01_0_0_y02..y06`, `gkblock_s01_otherside_3_0_after_y01/y05`, `gkblock_lie_*_dive` | groups 0–8, 67 |
| punch clear | `gkpunch_*` (groups 71–75), `gkfall_punch_0_0_y10_*`, `gkstagger_punch_0_0_y10_*`, `gkpunch_stagger_crash_*` | groups 71–75 |
| collapse / crossbar / post | `gkcollapsing_*_y10_crossbar`, `*_y10_upperhand`, `gkblock_s03_3_0_000_postcrash`, `gkrise_sideways_post_*` | OBS: named by the object hit |
| on the ground | `gkcatch_lie_*_dive`, `gkblock_lie_*_dive`, `gkclear_lie_*`, `gkdeflect_lie_*`, `gkscoopout_lie_*_otherside` | groups 20, 52–53, 76–79; rows `gkcatch_lie_near_0_0_{l,r}`, `gkclear_lie_s02_{l,r}` |
| see-off / give up | `gkseeoff_{high,side}_{react,light,reactjump}_0_0/3_0_yNN`, `gksavingCancel_*` | OBS: the keeper's "no chance" reaction |
| rise after a save | `gkrise_*` (67 rows, §7.3 row 30) | writer tables `DAT_1425e77f8/0e7818/0e7828/0e7848` and `DAT_1425e7838` (§6.6) |
| move with the ball | `gkmovehigh_*_ball[_onehand]`, `gkmovehigh_idle_0_0[_ball][_067_ball][_122_ball][_ver02]`, `gkmovelow_*`, `gkmovemid_guard*`, `gkmovenear_Heisou*`, `gkmoveSeriesA_*`, `gkmoveSeriesB_*`, `gkmoveheisou_run_3_3` (param group 89) | name inventory; group 89 is the keeper's parallel/sidestep blend |

**(b) Height blend groups** — `Parametricblendtable.bin` groups **0–79** (OBS, decoded §3): every
member is `gk…_yNN` with parameter `= NN/11` exactly (y02 → 0.18, y04 → 0.36, y06 → 0.55,
y08 → 0.73, y10 → 0.91, y11 → 1.0). Group boundaries (verified against the decoded file, ordinals
§3):

| groups | family | members |
|---|---|---|
| 0–8 | `gkblockcover` | f00/f01/f02/f03/f04/s01..s04, `dive`/`punch`/`wide`/`low`/`slidingCancel` variants |
| 9–17 | `gkcatch` | f00/f02/f03/s01..s05, `dive`/`diveguard`/`diveline`/`hold`/`slide`/`stand`/`long`/`fast`/`light`/`hard`/`back`/`b01` line variants |
| 18 | `gkcatch_s01` | 6 members (parameters 0.0, 0.18, 0.36, 0.55, 0.73, 0.91) — the canonical diving-catch blend; **the earlier draft's "single-member (0.0)" was wrong** (that was group 18 under the wrong bit split) |
| 19–20 | `gkcatch_s01b01`, `gkcatch_s03b01…_line` | near-line variants |
| 21–22 | `gkcatch_s02/s03` | |
| 26–33 | `gkcollapsing` | f00/s01..s04 + `long`/`stand`/`crossbar`/`upperhand`/`b01`/`Foot` |
| 34–40 | `gkdeflectlate` | f00/s01/s03 + `_nutmeg`/`_unjust` |
| 41–46 | `gkdeflectscoop` | f00b01/s01b01..s04b01 + `_long`, angle tokens |
| 47–70 | `gkdeflect` (+`_snapunder`, `_Expect`, `_pk`, `_lie`, `_otherside`, `_stagger`) | f00/f02/f04/f05, s01..s05, b03/b04 |
| 71–75 | `gkpunch` | f00/f02/f03/s02/s04 + `_palm`/`_collapse` |
| 76–79 | `gkscoopout_lie` | f00/s02/s03/s04 + `_otherside` |

GF-side notes: the keeper's save decision (catch / punch / deflect) is *made before* the blend is
entered (OBS: `FUN_14078c820`/`FUN_14078ce90` take the already-made action kind as a parameter and
only choose the row + crossfade — `FUN_1406ec260`/`FUN_1406ec5f0` are the decision callbacks).

### 8.3 What GF must supply for the keeper chain (OBS, gaps in `src/sim` today)

GF has no keeper-specific animation path: `elizacontroller.cpp`/`playercontroller.cpp` request
the same `e_FunctionType_*` set for every humanoid (§7.2), and `e_FunctionType_Catch` is never
requested (OBS, grep). The clip chain above therefore needs, at minimum:
1. a keeper role check on the animation command (GF already knows `e_PlayerRole_GK`),
2. a **keeper phase** value on the command (pre-save / saving / hold / aim / release), exactly the
   `P+0x245c` byte above,
3. a **distribution** decision (underarm roll / side-arm / over-arm / punt / short goal kick) with
   the target position, which then selects rows 150–177 / 127–149,
4. a "ball is loose in the box" state → `gkpickupball_*` (row 0x9bd in case 1).

## 9. Priority 4 — non-kick action states

### 9.1 Dribble touches and turn angles (OBS)

- Touch rows `dribble_a_b_{in,out,sole,nutmeg,rear,mid,axisback,avoid,decelerate,guard,guardside,
  notouch,4cycle1touch,tripletouch}_{l,r}[_verNN]` (170 rows, 2,090 populated cells — the
  best-populated family after `demo`) and `dribblerun_*` (53 rows). The turn angle is carried
  **in the clip name** (`_NNN`/`_fNNN`) *and* in `rot_data` (`{rot_start, rot_end, rot_angle}` in
  frames/degrees), which is the runtime source (AR §6, OBS: median `rot_angle` = +NNN for `_NNN`,
  −NNN for `_fNNN`).
- Turn clips: `run_2_2_back`, `side_2_2_run_f090`, `gkmovenear_idlemidturn_{022,045,090,135}`,
  `gkmoveSeriesB_Front_*_90/135`, plus the `_000/_045/_067/_090/_122/_135/_150/_180/_225/_270`
  token set across `sliding_*`, `slidetackle*`, `js_*`, `dribble*`.
- **Running backwards / sideways** (OBS, dedicated families): `back_a_b_*` (32 rows),
  `walkback/walkbackslant/backslant/backstep` (step family, 44 rows), `side_{l,r}_a_b_*` (77),
  `parallel_a_b_*` (19), `heisou_*`, `walkparallel_1_1` (param group 88), `mid_delayback_*`,
  `side_1_1/2_2/3_3` Freemove states, and the keeper equivalents `gkmovelow_back*`,
  `gkmovenear_HeisouBack*`, `gkmoveSeriesA_Back*` / `_BackSlant*`. GF maps these from
  `Movement` with `useDesiredLookAt` + a negative dot product — GF already computes
  `relBodyDirectionVec`; the PES side needs only the family pick.
- **Evasion** (OBS, 38 rows, not previously listed): `dodge_{human,ball,net,post}_{jump,jumplow,
  jumpslide,slide,decelerate,crouch}[_rapid]_N[_N][_down/up/low/high]` plus `avoidjump_*/avoidslide_*`
  clips and `dribble_*_avoid*` rows. GF maps these from BallControl/Interfere with a
  "dodge requested" flag — **open, see §14.5**.

### 9.2 The trick/feint vocabulary, both sides (OBS — this is the item the draft left vague)

GF's `PlayerSkills::Feint` enum (`<repo>/src/sim/data/playerskills.hpp:101-115`) has **11 moves**;
PES has ~200 trick rows. The mapping is *by clip-name token*, and GF already carries the rule list
(`playerskills.cpp:136-144`, first match wins):

| GF `Feint` | GF clip-name token | PES row family (count) | notes |
|---|---|---|---|
| `KickFeint` | `kick` (then `kickfeint_`) | `kickfeint_*` (34) + `kickrabona_*` (2) | `kickfeint_{a}_{b}_y0_{l,r}[_notouch][_samba]`, `_crujiff`, `_nutmeg`, `_rabona`; speeds `0_0/0_3/3_0/3_3/4_0/4_3` |
| `CutBehindTurn` | `cruijff` | `kickfeint_crujiff_{0_3,3_3}_{l,r}` (4) | **OBS: PES spells it `crujiff`, not `cruijff`** — GF's token never matches a PES name and that move is silently never selected. GF must fix the token (a one-line change on the GF side) |
| `Sombrero` | `chapeu`/`chapeau`/`lift` | `traplift_*_chapeu_*` (18), `dribble_3_3_chapeau_{l,r}` (2), `heellift_*` (6) | the flick-up family |
| `Scissors` | `scissors`/`sciin`/`sciout`/`stepover` | `scissorsin/scissorsin000/scissorsout/scissorsout000/scissorsouthighspeed[_loop]` (24), `doubletouch_ballroll_stepover_*` (4), `feint_stepover_*`/`feintrun_stepover_*` (3) | `stepover` is the named variant |
| `Nutmeg` | `nutmeg`/`through` | `nutmeg_*` (4), `nutmegriquelme_*` (2), `kickfeint_nutmeg_*` (2), `dribble_*_nutmeg_avoid*` (8) | `through` is a *different* PES family (`trapthrough_*`, the let-through) — GF's token is too loose |
| `DoubleTouch` | `doubletouch` | `doubletouch_*` (28) | incl. the `_ballroll_{stepover,rabona,scissors}` sub-family |
| `BodyFake` | `bodyfake`/`feintrun_` | `bodyfake*` (6), `dribble_*_bodyfake*` (6), `feintrun_*` (9) | `feintrun_*` carries the full-curve versions (`roulette`, `stepover`, `shakefoot`, `ballroll_onetime`, `trap_roulette`) |
| `Rabona` | `rabona` | `kickfeint_rabona_*` (4), `doubletouch_ballroll_rabona_*` (4), `sliderabona_*` (2) | |
| `MarseilleTurn` | `roulette`/`meialua` | `roulette*` (4), `rouletteribery*` (4), `roulettesingle*` (2), `rouletteaxisback*` (2), `rouletteonetouch*` (2), `roulettetriple*` (2), `elasticoreturn/reverse*` (8) | `meialua` does not occur in PES names |
| `CrossOverTurn` | `outout` | `outout_{0_3,3_3}_{l,r}` (2) | also `inout_*` (4), `outin_*` (4), `onetime_*` (4), `lfeint_*` (4), `stepkick_*` (4), `neymarstepkick_*` (2), `drawopencloase_*` (2), `elastico_*` (4), `parryside/back/backturn/sideturn` (16) — PES has ~60 two-touch turn rows with **no GF enum entry**; GF currently cannot ask for them |
| `StepOn` | `accordion` | `trap_*_accordion*` (9) | PES's `accordion` rows are **trap** rows, not feint rows — GF maps this move through Trap, which is a different `e_FunctionType` |

Row-level rule (OBS): every trick row is `_0_3_l/_0_3_r` (+`_3_3` at pace, `_0_0` for the
no-moving versions), and the column is the turn/touch direction — exactly the `dribble` rule with
a different family. All are addressable through the Animetable; no extra table is needed.

### 9.3 Injury / knockdown (OBS)

No separate "injury" clip family. The chain is (OBS, from row grammar + the
`DemoConnectData`-driven writers):
`stagger_*` (bump, upright) or `fall_*` (grounded, 62 rows incl. `_facecover`, `_sliding`,
`_move`, `_hold`, `_lose`, `_carry`) → lie state from `demo_connect_data`
(2 allfours / 3 facedown / 4 faceup) → `riseup_<state>_0_{0,3}`; keeper variants
`gkfall_*`, `gkstagger_*`, `gkrise_*`; plus the referee's foul call
(`judge_*` 15 rows) and `tackleshoulder_*`/`tacklehold_*`/`tacklepulled_*` for the contact
itself. GF's `foulsequence.cpp` / `foulseverity.cpp` already produce the foul type; the clip is
the `stagger`/`fall`/`riseup` triple plus the judge's reaction.

### 9.4 Taunts, celebrations, emotes (OBS, fully decoded this session)

- Celebrations: `demo_goal_*` (367 `demo` rows total, of which the goal set is the large part) with
  `_loop` variants; `dm_goal_*`, `dm_tu_*` (time-up), `dm_miss_*` (dejection), `dm_oop_*`
  (out of play, incl. `dm_oop_gk_riseup_*`), `demo_appeal_*`, `demo_guts*`, `demo_miss_*`,
  `demo_move_*`, `demo_cheer_*`, `demo_hugset_*`, `demo_pfm_*`, `demo_fail_*`, `demo_coop_*`,
  `TimeUp_*` (32), `HalfEnd_*` (13). Each celebration has an intro + loop pair (66 loops, AR §7).
- Taunts/emotes toward opponents: `demo_appeal_*` (referee appeals), `demo_guts*` (fist pumps),
  `goalturn_*` (19 rows — the keeper showing off / time-wasting), `js_*_win/lose` outcomes.
- **The demo selector (OBS, decompiled this session, `FUN_1406eb100` + `FUN_1406ea840`).**
  The angle → "demo class" map is the 19-entry float table at **`DAT_1425e36c0`** (image VA):
  `{−180, −168, −150.5, −132, −110.5, −80, −60.5, −34, −15.5, 0, +15.5, +34, +60.5, +80,
  +110.5, +132, +150.5, +168, +180}`, and the class is the byte table at **`DAT_1425e3710`** =
  `{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17}` (identity, 18 entries) — i.e.
  `FUN_1406eb100(angle)` buckets the wrapped angle into one of 18 bins by a linear scan. The
  row priority list is then `FUN_1406ea840(player, row, class)`: 9 tables × 18 i32 at
  **`DAT_1425e4700` + class*0x50** (OBS, decoded; e.g. class 0 =
  `{demo_move_0_1_1, demo_gutsBanzai_0_0, demo_move_0_1_2, demo_gutsR_3_0, …}`), and for a class
  ≥ 9 the list index is mirrored through `DAT_1425e3728` (the byte table = `17−i`), which is the
  same walk `FUN_14084df20` uses for the trap fallback (§6.5). **Correction:** the earlier draft
  called these "9 column-ordered preferred-row tables"; they are **18-entry demo-row priority
  lists indexed by the angle class** (OBS: every entry is a `demo_*` row).

### 9.5 Referee / linesman / assistants (OBS)

- **Referee**: `judge_*` 15 rows, requested via `refereecontroller.cpp`'s `Movement` + `Special`;
  the end-of-game whistle clip was seen live (AR §7 state A). Humanoid/pose: he is a normal
  `AnimePlayer` with kind `ANIME_JUDGE`.
- **Linesmen**: `linesman_*` 26 rows + `assistant_*` clips (`assistant_stand_0`,
  `assistant_flag_up_0[_offside_near/middle/far]`, `assistant_3_0_090_flag_*`,
  `assistant_flag_up_0_flag_up/ck/down`, `assistant_idle_0_{flag_gk,point_f090}`) played as an
  **upper-body layer** with mask `LRArm`/`LArm`/`RArm`/`Full` (OBS live: kind 91 side-stepping + a
  layer clip `layer_assistant_flag_hand_R`, AR §11.2/§11.5). Hand fallback category 2 = the flag
  grip `hatanigiri` (OBS, AR §11.3).
- **Wall players**: `wall_*` 12 rows (`wall_jump_high…`) for the defensive free-kick wall.
- For GF these are three extra `AnimePlayer`-like roles (GF already has `playerofficial.cpp`);
  the layer mechanism is the one piece GF does not have — it must either fake it by playing the
  full-body `linesman_*` rows or implement `SetLayer`.

## 11. Loader summary (what the engine reads at runtime)

Files, all under `common/anime/`, read from `PES21_ROOT` (no committed copies). Byte sizes are
OBS from the VFS listing (`build/apps/pesfmt_dump ls common/anime/AnimeTable/bin`).

| file | size | what is built |
|---|---|---|
| `AnimeTable/bin/Animetable.bin` | 219,240 = 3,045 × 18 × 4 | the `(motionNo+1, mirror)` matrix; layout §1.1 |
| `AnimeTable/bin/Freemove.bin` | 46,488 = 3,874 × 12 | `(state, digit) → (motionNo, mirror, nextState)`; layout §2.1 |
| `AnimeTable/bin/Parametricblendtable.bin` | 3,592 = 449 × 8 | 95 groups (ordinal = the running group count); layout §3 |
| `AnimeTable/bin/Idletable.bin`, `Movetable.bin` | 3,020 = 151 × 20 each | 5 u32 words = 8 × 12-bit row ids + 2 flags, **per-table slot order §7.1** |
| `AnimeTable/bin/Freemoveoption.bin` | 840 = 105 × 8 | secondary motion list for the FSM step (§2.4) |
| `AnimeTable/bin/Kickinfo.bin` | 2,416 = 1 header + 75 × 32 | 7-bit tuning fields; per-kick, not selection (§7.1) |
| `AnimeTable/bin/Slidedribble.bin` | 697,172 | not on the critical path (§14.5) |
| `Mbinfo/json/anim_infos.json` | 15.7 MB | per-clip metadata, §5 (authoritative, plain JSON in dt13) |
| `Mbinfo/bin/Animation.bin` | — | per-clip timing if the json is not used; §5 |
| `FoxAnim/Body/body_anime_file0..9.mtar` | — | the clips (4,389) |
| `PES2021.exe` (install root) | — | the 4,389-name table at **0x14351e590** and the 3,045-row label table at **0x1434997f0** (§1.2); `DAT_14344e0e0` (the 17-entry parametric group map, §6.8), `DAT_14344e380` (the 150-entry movement-state table, §6.8), the demo-class float table **`DAT_1425e36c0`** and its byte index **`DAT_1425e3710`** (§9.4), the 9 × 18 row-priority lists **`DAT_1425e4700…0x1425e4980`** (§6.5/§9.4), the tackle/block/slide row tables `DAT_1425e77f8/0x1425e7818/0x1425e7828/0x1425e7838/0x1425e7848` (§6.6) and the feint decision tables `DAT_1425e5d08/0x1425e5d18/0x1425e5d30/0x1425e5d40` (§6.7) |

**Mapping a VA to a file offset (OBS, from the PE header this session):** `.rdata` VA
`0x14252f000` → file offset `0x252e600`; `.data` VA `0x14339b000` → `0x339a200`; `.pdata` VA
`0x143ac5000` → `0x36cea00`; image base `0x140000000`; 19 sections; optional-header size 240;
PE32+ magic `0x20b`. The rule "`.rdata`/`.trace` = VA − 0x140000A00, `.data` = VA − 0x140000E00"
is **only** correct for VAs that sit in `.rdata`/`.trace`/`.data` — check the section table.
Note also that the raw-file offsets in the image (`<image>/pesmem.bin`, base 0x140000000) equal the
VA minus the base, i.e. the image is the file mapped at its virtual addresses; the header's
`PointerToRawData` values above are for the *on-disk exe*.

Nothing else. No PES bytes or name lists are committed; the loader derives the maps at startup.

---


## 12. ChoosePesClip — the algorithm

```
ChoosePesClip(action) -> (motionNo, mirror, startTime, duration, rate) or NONE:

  # action = {family, velIn, velOut, relAngle(deg), ballHeightCm, ballDirIn, ballDirOut,
  #           footSide, power, variant, locoState, pressure, deadBall, restartKind,
  #           keeperPhase, trick, tripType, lieCode, specialVar1/2, foulType, dodge}
  # All row/column/mirror reads go through the three accessors of §6.1, which must
  # reproduce FUN_1406de990 exactly (population scan, ±0.001 zero split, default col 9,
  # hard patch row 0x7c5 col 2). An empty cell loads as motionNo -1 (§6.1).

  if family == LOCOMOTION_STEADY:
      g      = DAT_14344e0e0[locoState]           # §6.8; 80 canonical
      rate   = clamp(speed / 8.96, 0, 1)          # §3
      start  = footPhase(clip) in {0,.25,.75}     # §5.7
      return PARAMETRIC(groupOrdinal(g), rate, startRate=start, duration=0.3)

  if family == LOCOMOTION_STEP:
      st    = stateCode(locoState)                # [fam][in][sep][style][out][sep] §2.2
      digit = angleBucket(relAngle)               # 0..4, 5..9 = mirrored side
      rec   = freemoveRow(st)[digit]              # key = st*10 + digit; missing digit is legal
      if rec is missing:
          opt = freemoveOptionRow(st)[speedDelta]  # §2.4 secondary disambiguation
          if opt is missing: return NONE           # do NOT fall back to another family
      locoState = rec.nextState
      return SINGLE(rec.motionNo, rec.mirror, startTime=0, duration=0.133..0.2)

  if family in (TRAP, TRAP_SIDE, TRAP_CHASE, TRAP_FAST, TRAP_LIFT, TRAP_BACK):
      row   = rowIndex(familyVariant(ballSide, pressure), pes(velIn), pes(velOut),
                       yN(ballHeightCm), bodyPart(predictedContact), mods)
      sideFlag = (rowSide != ballSide)
      col   = Column(row, angleBucket(relAngleVsFacing(ballDirIn)))
      col   = sideFlag ? 17 - col : col            # FUN_140856980's 18-column mirror map
      if not populated(row, col): col = PreferredNeighbour(row, col, class)
                # FUN_1406ea840 walk over DAT_1425e4700+class*0x50, 18 entries,
                # terminating column 0x12 when nothing is populated
      if not populated(row, col): return NONE      # PES issues no SetMotion
      # FUN_1408527c0: cell mirror XOR the solver's own flag, INVERTED for
      # rows 0xa0d/0xa0f (trap_3_3_y{6,8}_rear)
      return SINGLE(cell(row,col).motionNo, cell.mirror ^ sideFlag ^ invert(row), 0, 0.2)
      # the "ball through the legs" case (row 0x472..0x475 = nutmeg_*) additionally
      # emits a fall: FUN_1406e2c30(param_2, 0x159 = fall_lowbody_3_0_v2_l)

  if family in (SHORTPASS, LONGPASS, HIGHPASS, SHOT):
      row   = rowIndex(kick, lengthClass(power), pes(velIn), outClass(velOutSpeed),
                       footPart(power, curve), yN(ballHeightCm), mods)
      col   = Column(row, angleBucket(relAngleVsFacing(ballDirOut)))
      if not populated(row, col):                  # the writer's only fallback (§6.3)
          col = Column(row, 0)
      if not populated(row, col):
          row = OffAngleFamily(row, relAngle)      # kick{side,rear,far,fardown}_*
          col = Column(row, angleBucket(relAngleVsFacing(ballDirOut)))
      if not populated(row, col): return NONE
      for each hitEvent(cell.motionNo):            # §5: all events, middle-out order
          emit(touchFrame, touchPos, bodyPart)
      return SINGLE(cell.motionNo, cell.mirror, 0, 0.2, touch=hitEvents)

  if family == FOUL_CONTACT:                       # §6.3: the kick writer also emits these
      row = FoulRow(foulType)                      # 0x1ae/0x1b5/0x1b1/0x1b2/0x1b3 by contact kind
      return SINGLE(cell(row, Column(row, 0)).motionNo, cell.mirror, 0, 0.2)   # col forced to 0

  if family == BLOCK:
      row   = blockVariant(frontal/side) + 2*state + side      # §6.6 tables
      col   = Column(row, angleBucket(shotLineVsFacing))
      return SINGLE(cell.motionNo, cell.mirror,
                    startTime = (clip == 0x7cc && react < 3) ? 0.07 : 0.1,
                    duration  = (60/tickHz) * kScale)

  if family == KEEPER_SAVE:
      g     = gkGroup(ballLine, dive?)             # groups 0..79, §8.2(b)
      param = clamp(contactHeightCm / 22.5, 0, 1)  # parameter = NN/11
      hold  = catchable(ballSpeed)                 # → group 15, the hold branch
      return PARAMETRIC(g, blendPoint=param, startRate=0, duration=0.2)

  if family == KEEPER_HOLD / KEEPER_AIM / KEEPER_RELEASE:   # §8.2 state machine
      switch keeperPhase:                          # the byte at P+0x245c
        case 1 (pre-save): row = 0x9bd (throwin_pickupball) or the crouch rows
        case 3 (hold/look): row = 0x9bf (throwin_run_3_3) when the release test passes
        case 4 (aim):      row = 0x9be (throwin_seamless_liftupball) if the target is
                           behind the goal line, else keep aiming
        case 5 (release):  rows 150..177 (throw_*) / 137..149 (puntkick_*) /
                           127..136 (goalkick_*) by the distribution kind
      col   = Column(row, angleBucket(wrappedTargetAngle))   # ±30°/±360° yaw step
      return SINGLE(cell.motionNo, cell.mirror, 0, 0.2)

  if family == SLIDING:
      row   = rowIndex(sliding, pes(velIn) capped at 3, 0, slideSide,
                       ballAttached ? dribbleslide : plain)
      col   = Column(row, angleBucket(slideDirVsFacing))
      return SINGLE(cell.motionNo, cell.mirror, 0, 0.2, lieState=demoConnect(motionNo))

  if family == TRIP:
      row = rowIndex(tripType == 1 ? stagger : fall, pes(velIn), 0, mods)
      col = Column(row, angleBucket(tripDirVsFacing))
      return SINGLE(cell.motionNo, cell.mirror, 0, 0.2, lieState=demoConnect(motionNo))

  if family == RISEUP:
      state = lieCode or demoConnect(previousClip)  # 2/3/4
      row = rowIndex('riseup_<state>_0_', nextTargetDist < 1.5 ? 0 : 3)
      col = Column(row, angleBucket(nextTargetDirVsFacing))
      return SINGLE(cell.motionNo, cell.mirror, 0, 0.2, adjust=AdjustTransXZ(target, 6/60 s, 0.9))

  if family == TRICK:                              # §9.2, GF token -> PES family
      row   = rowIndex(familyFromToken(specialVar1), 0, 3 or 3_3, side)
      col   = Column(row, angleBucket(turnAngle))
      if not populated(row, col): DROP COMMAND      # GF's existing HasSpecial contract
      return SINGLE(cell.motionNo, cell.mirror, 0, 0.2)

  if family == BALL_HIT_BYSTANDER:                 # §6.3, the kick writer's extra output
      row = 0x19e/0x19f/0x1a2 by the part hit (head / belly / body at pace)
      return SINGLE(cell(row, Column(row, 0)).motionNo, cell.mirror, 0, 0.2)

  if family == SPECIAL:
      if not HasSpecial(var1, var2): DROP COMMAND          # existing GF contract
      cls  = FUN_1406eb100(angle)                    # 18 bins, DAT_1425e36c0/DAT_1425e3710
      row  = priorityList(cls)[0]  # first populated row of DAT_1425e4700 + cls*0x50
      rowL = loopRow(var1, var2)
      return PAIR(SINGLE(m1, x1, 0, 0.4), LOOP(m2, x2, startRate=0, duration=0.5))

  if family == LAYER (officials):
      return LAYER(clip=layerClip(role, pose), mask=LRArm|LArm|RArm|Full, duration=0.5)

  # ---- what to do when NONE ----
  # PES never substitutes a row: it either issues no motion at all (the trap solver's
  # 4389/0x1125 answer) or runs the one writer-local retry above. So NONE is a legal,
  # visible result: the actor keeps its current clip. Only if the *current* clip is also
  # gone (e.g. GF's movement query found nothing) does GF fall back to its stock pool or
  # idle — which is exactly GF's existing `GetIdleMovementAnimID()` path.
```

Start times: loops phase-matched (`startRate · blend_start/30`, §5); one-shots 0. The tick-quantised
offset of §14 item 3 is a sub-frame refinement, not a clip-choice issue.

---



## 13. Worked examples (one per rule family)

Each was executed against the tables in scratch (`<scratch>/SimMotionRE/`); the ids/labels are
quoted as evidence. **All are table-derived, not live-observed** (the game is not running).
Where the earlier draft's ids were wrong, the corrected value is marked.

**E1 — Freemove step (OBS).** State `1011` (family 0, speedIn 0, style 1 walk, speedOut 2 jog,
per §2.2), requested turn +45° → digit 1 → key `10111`. `Freemove.bin` row: motionNo **2220**
`idle_0_2_run_000`, mirror 0, next state **10211** (verified by re-reading the file:
key 10111 → `(motionNo 2220, mirror 0, next 10211)`). Note the digit is the *requested relative
turn bucket*, not the clip's own turn token: digit 1 (+45°) from state 1011 yields the
`_000` clip, and digit 2 (+90°) yields `idle_0_2_run_045` (2221, next 10212) — the FSM pairs a
requested turn with a *subset* of turn clips. A mirrored request (digit 6, key 10116) gives the
same motionNo 2220 with the mirror bit set and next state 10216 (verified). Crossfade 0.2 s
(observed live, AR §7).

**E2 — Parametric locomotion (OBS).** Player at 4.2 m/s, no pressure, facing forward →
state 1 → `DAT_14344e0e0[1] = 85` → group 85 (ordinal) = **`{walk_1_0_090_avoidstop 0.0,
runslant_3_3 0.15, run_2_0_135 0.30, near_run_2_0_090 0.50, near_run_4_0_135 0.90,
run_4_4_v2 1.00}`** (verified by decoding `Parametricblendtable.bin`; the earlier draft's
member list was the wrong bit split). Rate `= 4.2 / 8.96 = 0.469` → interpolated between the
`near_run_2_0_090` member (0.50) and `run_2_0_135` (0.30). Foot phase from the current clip's
`foot_type` events → startRate 0.25. Duration 0.3 s.
For comparison, group 80 (the canonical plain run) = `{walk_1_0_090_avoidstop 0.0, runslant_3_3
0.15, run_2_0_135 0.30, run_3_2_side_270 0.50, run_4_3_back 0.90, run_4_4_v2 1.00}` — the same
shape with the non-`near_` variants, which is what makes groups 80/85/86 interchangeable by the
"am I running at the ball?" flag.

**E3 — Pass (OBS, rule from the writer §6.3 + the actual table cells).** Open play, GF `ShortPass`,
`desiredPower` small, actor at run (PES digit 3), ball on the ground (y0), target 30° right of
facing, possession retained. Row: **`kick_short_3_0_inside_y0`** (OBS: row **2020**; note PES has
no `_2_` kick rows — a jogging entrant uses a `_3_` row, GF's `walk` bucket maps to PES digit 3).
Column: `+30°` → `FUN_1406de990` computes centres … `+22.5` (col 11) vs `+45` (col 12); nearest =
col 11. Cell = motionNo **2710** `kick_short_3_0_inside_y0_045`, mirror **0** (verified; the
earlier draft said 2709 `…_045_keep`, which is wrong — that row's `_keep` cells are not at col
11; `_keep` cells appear in the `b = 0` standing rows).
Crossfade 0.2 s, startTime 0. The touch frame comes from that clip's `hit_data`
(`FUN_1406b10d0`), and the physics touch is queued at that tick.
The full population of row 2020 (verified): col 7 `…_f045`, col 9 `…_000`, col 11 `…_045`,
col 13 `…_090`, cols 15/17 `…_135` — no `_keep` variants, because `b = 0` here means the ball
leaves, and the retained-possession variant is the *0_0 row's* job:
`kick_short_0_0_inside_y0` (row 2017) has 4 populated columns (6/9/11/13) with `_keep` cells:
col 6 `kick_short_0_0_inside_y0_f045`, col 9 **2608** `…_000_keep`, col 11 `…_045_keep`, col 13
`…_090_keep`. So PES encodes "possession retained" as the *incoming speed* (0 = standing, set
pass), not as a modifier on the moving row. GF's `outgoingBallDirection`-only contract misses
this: **the engine must pass the outgoing speed class, not just a boolean.**
If the requested cell had been empty, the writer retries at angle 0 — the only in-engine
fallback (column 9).

**E4 — Shot, off-balance (OBS).** GF `Shot`, `desiredPower` high, actor at dash (4), ball at knee
height (y4), target 110° right (behind-left), actor staggering. Row family `kick_long_4_0_*`
(OBS: the kick rows exist for a = 0/3/4, never 2 — the jogging speed class is absent from the
kick family, so a jogging shooter takes a `_3_0` row, INF high). Angle 110° → nearest populated
column of `-112.5`/`+112.5`; if that is empty, the writer's angle-0 retry; if still empty, the
caller picks `kickrear_*` (OBS: the off-angle families exist precisely for the > 90° cases; rows
`kickrear_long_0_0_inside_y0_down`, `kickrear_mid_0_0_{infront_y0_late,inside_y0}`,
`kickrear_short_*`) and re-runs the column lookup. The off-angle families and their row ids are
listed in §7.1's *Idle* records 124–147 (`kickfar_*`, `kickfardown_*`, `kickrear_*`,
`kickside_*`, `kicksliding_*`, `kick_tacklefoot_*`, `head_tacklehead_*`).

**E5 — Trap from the side (OBS).** Ball arriving at 1.6 m height (y7), actor jogging at 3.9 m/s
(digit 3), ball arriving from the actor's right at −60°. Row: `trapside_3_3_y7_*` with the side
token matching the ball side. `FUN_14084b310` applies the side flip only if the solver's flag is
set; the column is −60° (col 6); mirror from `FUN_1408527c0`. If the row's cell is empty,
`FUN_14084b9c0`→`FUN_1406ea840` walks the *same* 9×18 row-priority list at `DAT_1425e4700`
(indexed by the demo class from `FUN_1406eb100`) and takes the first row that is populated at
the flipped column; the walk terminates with column **0x12 = 18** when nothing matches. If the
solver's row is still the unset sentinel `0xbe5`, `FUN_14084b310` returns **0x1125 = 4389** and
**no SetMotion is issued** — GF's existing "keep the previous clip" behaviour is *not* what PES
does; the trap simply does not animate and the ball physics resolves.

**E6 — Keeper diving catch (OBS).** Shot on the keeper's left, 1.1 m high, keeper already moving.
Predicted contact height 1.1 m → parameter `N/11` with `N ≈ 5.5` → interpolate the members of
group **18** = `{gkcatch_s01b01_2_0_y00 0.0, gkcatch_s01_0_0_y00 0.18, gkcatch_s01_0_0_y02 0.36,
gkcatch_s01_0_0_y04 0.55, gkcatch_s01_0_0_y06 0.73, gkcatch_s01_0_0_y08_back 0.91}` (verified by
decoding the file; the earlier draft's group number and members were off). Hold branch: if GF's
catch test says the ball is catchable, use group **15** = `{gkcatch_f00_0_0_y10 0.0,
gkcatch_f00_hold_0_0_y00 0.18, gkcatch_f00_hold_0_0_y02 0.36}` (verified) — the hold variant of
the same family, not a separate clip. Then `gkrise_sideways_catch_0_3_000` (OBS: row 1750's
cells are `gkrise_sideways_catch_0_3_f090`/`_000`/`_090` at cols 4/8/9/13) for the get-up,
`gkmovehigh_idle_0_0_ball` (row 1458) for the hold-carry, then rows 150–177
(`throw_under_0_0_045` → `gkunderthrow_0_0_045`, `throw_side_0_0_045` → `gksidethrow_0_0_045`,
`throw_side_3_0` → `gksidethrow_3_0`, `throw_over_*` → `gkoverthrow_*`) or rows 137–149
(`puntkick_side_0_0` → `gkpuntkick_side_0_0`) for the distribution, and rows 2485–2493
(`throwin_release_*`) if the ball goes out for the throw-in.

**E7 — Header via the superior-kick tables (OBS, layout + slot key now solved, §7.1).** Standing
header, ball at 1.46 m (y5) → `Idletable.bin` **record 1** → slot 1 = row `head_0_0_y5_f`
(verified with the parser-derived slot order; the earlier draft said slot 5). The full record is
`{slot0: −, slot1: head_0_0_y5_f, slot2: −, slot3: head_0_0_y9_f, slot4: −, slot5: −,
slot6: head_0_0_y6_f, slot7: head_0_0_y7_f}`, matching the Idle slot map
`{0:y0, 1:y5, 2:y8, 3:y9, 4:y4, 5:y2, 6:y6, 7:y7}`. Column = the ball line vs facing: the
`head_0_0_y5_f` row's cells (verified) are col 0/4 `head_y05_0_0_090` **mirrored**, col 9
`head_y05_0_0_000` unmirrored, col 13/17 `head_y05_0_0_090` unmirrored. Running in (digit 3) →
`Movetable.bin` record 1, same slot map but with the `_3_0` rows and the *ascending* order
(`{slot5: head_3_0_y5_f, slot6: head_3_0_y6_f, slot7: head_3_0_y7_f}` for the y5 case).

**E8 — Sliding tackle (OBS).** Actor at 5.5 m/s (digit 3), ball to the actor's right, intended
slide direction +50°. Row family `sliding_3_0_r`, column +45° (col 12). The clip's
`demo_connect_data` gives the lie state; then `riseup_<state>_0_3` (the actor will run on).
Mirror from the cell. If the slide is *with* the ball, the rows switch to `dribbleslide_*` /
`dribbleslide045_*` / `dribbleslide090_*` / `dribbleslide180_*` (OBS: the numbered suffix is the
touch direction, not the angle).

**E9 — Trick (OBS, the GF↔PES token table §9.2).** GF `BallControl` with
`specialVar1 = PlayerSkills::Scissors`, actor at jog (3→3). PES row family: `scissorsin_3_3_r` /
`scissorsin000_3_3_r` / `doubletouch_scissors_3_3_r` / `feint_ballroll_scissors_0_3_f045_…` /
`feintrun_scissors_turn_3_3_f045_…` (OBS: all exist; the first is the plain standing variant).
Column = the touch direction vs facing; no football extension touch in the trick rows that carry
`_notouch`. GF's existing `/feint/` import convention maps the clip name → `specialvar1`, so the
PES side needs no change; the GF side needs the `crujiff` spelling fix (§9.2) and a home for the
~60 turn rows with no GF enum entry (§14.5).

**E10 — Throw-in (OBS, both actors).** Outfield taker, standing, ball 2 m to his right →
row 2496 `throwin_idle` → `throwin_loop` (mirror from the cell) while waiting; on the whistle
row 2495 `throwin_run_3_3` → `throwin_run_3_3_000_onehand_ver21`; the release rows
2488–2490 (`throwin_release_idle_{short,mid,long}` → `throwin_release_idle_000_*`) or
2491–2492 (running variants `throwin_release_run_067_*` / `_000_*`) by the throw length;
the pick-up row 2493 `throwin_pickupball` → `throwin_pickupball_{000,090,180}` by the ball's
side. **Keeper** distribution is a *different* row set (150–177 / 127–149 / 137–149), §8.2.

## 14. Open items (status after this session)

**Closed this session** (all were listed open in the draft/AR):
- The Idletable/Movetable record layout **and slot order and enum unit** — decoded from the
  parser `FUN_1406aa590`, §7.1. The slot maps (Idle {0:y0, 1:y5, 2:y8, 3:y9, 4:y4, 5:y2, 6:y6,
  7:y7}, Move {0:y0 … 7:y7} ascending) are reproduced as tables, not formulae.
- The `Kickinfo` 7-bit field pack, including the "−60" mystery — it is a signed excess-60 angle,
  §7.1.
- The parametric group *selection* table (`DAT_14344e0e0`) and the movement-state table
  (`DAT_14344e380`) — read, §6.8. This removes the draft's "INF (medium)" on locomotion.
- The column-picker algorithm and its ±0.001 zero split, with every float constant read from the
  image — re-decompiled this session, §6.1.
- The kick writer's row construction (including the foul-contact rows it also emits) — §6.3.
- The trap solver's side flip, mirror inversion on `trap_3_3_y{6,8}_rear`, and the populated-
  neighbour walk (`FUN_14084b9c0` → `FUN_1406ea840`, terminating column 0x12) — §6.5.
- The tackle/block/slide row tables and the slide's motionNo overrides — §6.6.
- The demo/celebration selection: `FUN_1406eb100`'s angle→class table at `DAT_1425e36c0` and the
  18-entry class index table at `DAT_1425e3710`; the 9 × 18 row-priority lists at
  `DAT_1425e4700` — §9.4.
- The "row 1989 col 2 → 2484, mirror 0" hard patch — identified as row `0x7c5`
  = `kick_mid_0_0_inside_y0_f`, §6.1.
- The keeper's action-state machine (`FUN_140791ec0`, states 0–5 at `P+0x245c`) and the
  distribution row set — §8.2.
- `Parametricblendtable.bin`'s bit layout — corrected, §3; every group member list in this
  report is re-derived from the corrected split.
- The GF trick vocabulary ↔ PES trick rows — mapped token by token, §9.2 (found the `crujiff`
  misspelling and the `through`/`accordion` collisions).

**Still open, with what was tried:**
1. **The Idletable/Movetable record index → situation enum.** The *slot* order is solved (§7.1)
   but the *record index* is not yet an enum: 121 of 151 Idle records are populated and their
   contents identify them (header sets, the `_f`/`_l`/`_r` body-part variants, the trajectory
   modifiers), yet no code decompiled this session consumes the parsed table to name the record
   — the only reader found (`FUN_14079d310`, called from the trap/feint paths) reads it into a
   small struct without selecting rows from it. Tried: RTTI walk from
   `.?AVParsedJsonSuperiorKickTable` (COL at 0x142c9bad8) — the vtable slot leads back into the
   same data-pointer table at 0x1425dbd98 (`FUN_1406aa260`, the generic loader) rather than to a
   per-record consumer; the 9×18 `DAT_1425e4700` lists turned out to be the demo/trap walks, not
   superior-kick readers. *Impact:* headers/volleys only; GF never requests them (§0.7), so this
   does not block. **The engine can drive it from the row-label grammar**: the record's own row
   labels carry the situation, so a `(body part × trajectory × side)` index built from the labels
   is equivalent to the enum.
2. **The catch/punch/deflect decision threshold.** Tried: reading the keeper writers
   (`FUN_14078c820`, `FUN_14078ce90`) — they take an already-made decision as a parameter
   (`FUN_1406ec260`, `FUN_1406ec5f0`) and the caller is in the not-yet-decompiled AI block.
   *Workaround for the engine:* GF's existing `Catch` request plus a holdability test on ball
   speed; a tuned threshold, flagged as such.
3. **The single-clip `startTime` formula** (tick-quantised, AR §9). Tried again this session by
   reading `FUN_140727230` (the STEP_MOVE writer): the start time comes from
   `FUN_1406b11e0`/`FUN_1406b10d0` (clip length in ticks) minus a random `uVar11/3` offset and a
   remaining-duration gate, then divided by the playback rate — i.e. `startTime = (clipTicks −
   rand(0..ticks/3) − elapsed) / rate`, not the foot-event inversion AR tried. **INF (medium):
   this is the formula; not yet verified against a live capture.** Default 0 remains acceptable.
4. **The `playbackSpeed` driver** (0.9–1.15 per player). Untouched this session.
5. **`SlideDribble.bin` internals** (697,172 B). Not needed (§7.1); re-open only if GF wants the
   with-ball slide *touch* semantics rather than the slide *pose*. Tried: `pesfmt_dump cat`
   extracted it, but no parser was decompiled and the `dribbleslide*` cells are already
   addressable through the Animetable.
6. **`FreemoveOption.bin` secondary disambiguation** (§2.4; 105 × 8 B, 96 distinct keys, 7 of
   them carrying 2–4 motions — e.g. key 22250005 → `back_2_2_run_f135 *M`,
   `idle_0_1_walk_180`, `idle_0_2_run_180_near`, `walk_1_1`). Not on the critical path: every
   value motion is also reachable through the main table (verified: the 105 motions are a
   subset of `Freemove.bin`'s 252 × mirrors).
7. **GF-side gaps that are engineering, not RE** (each listed where it is needed):
   a dead-ball phase flag with the restart kind (§7.3), the keeper state chain (§8.3),
   a pressure flag on the animation command, a foul-type discriminator, a "dodge requested"
   flag (§9.1), a home for the ~60 PES turn rows with no GF enum entry (§9.2), the `crujiff`
   spelling fix (§9.2), and the `SetLayer` upper-body mechanism (§9.5). Each is a small addition
   to GF's animation command struct; none requires more PES RE.

### Killed this session
- *The Animetable rows carry a height index* — no: the height lives in the `_yN` token of the row
  label, and the *cells* hold the height choice for the superior-kick tables (§7.1).
- *`Kickinfo` selects clips* — no: its fields are per-kick tuning values (signed excess-60 angle
  + three bytes) consumed by the cancel/hit-frame conversion (§7.1).
- *The locomotion blends are chosen from the clip the player is in* — no: `DAT_14344e0e0` maps a
  17-valued movement state to the group (§6.8).
- *Row labels tell you the clip family* — no: several writers emit clips whose family differs
  from the row's label (e.g. slide poses come from `gkrise_*` rows, feints from
  `blockfront_*_shoot_reaction` rows, keeper distribution from `throw_*` rows; §6.6/§6.7/§8.2).
  Match on *row id + column + mirror*, not on the label text.
- *The `Parametricblendtable` u32 packs `{motionNo:12, group:7, mirror:1}`* — no: the field layout
  is `{motionNo:12 | 0x520 offset, group:8 at bit 12, mirror at bit 19}` and the group changes
  only on a record's *first* member (§3). The old split produced a single giant group 0.
- *`throw_*` rows are the outfield throw-in* — no: they are the keeper distribution rows; the
  outfield throw-in is `throwin_*` (§8.2).
- *The 9 × 18 tables at `DAT_1425e4700` are a trap column-priority list* — no: they are demo-row
  priority lists indexed by the 18 angle classes (`FUN_1406eb100`); the trap path reuses the same
  walk (§6.5/§9.4).

