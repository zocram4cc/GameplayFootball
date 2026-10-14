# PES press layer port: partial report (PressLayerPort)

Status: NO code committed. Worktree `the worktree` at detached HEAD 143c0fd, unchanged. Gate and smoke not run.

## Why no port landed
The assignment covers three RE-and-port parts. Part 1 alone (DELAY/PRESS writer tree) is about 400 decompiled lines in `FUN_140536fc0` plus helpers `FUN_140639830`, `FUN_140639a20`, `FUN_140639600`, `FUN_14062af10`, `FUN_140535260`, `FUN_1404567e0`, `FUN_140a42940`, `FUN_140a6ce10`, `FUN_140632ae0`, `FUN_140633940`, `FUN_140634470`. A partial writer would invent gates and fake fidelity, so nothing was ported.

## Decoded and cross-checked (raw via existing decompiles)
- `FUN_140536fc0` head: clears the 4 `T+0xa548` slots to 0xff. Skip if the keeper holds the ball (`FUN_140a53f90 > 0`), out of play (`MI+0x116c != 1`), or the opponent's `FUN_140a62690` man is in state 0x41/0x42. After restart types 1–3 or 0x10, needs `FUN_140a51320 >= 3`.
- Body gate: runs only if `T+0xf1 || T+0xb712 || T+0xb715`. Then `FUN_140a604d0(level, 6)` must be false (or the match-object test), and `T+0xb70c == 0`. With no carrier, the loose ball must be within 13 m (169 = 13², constant at 0x1425c6788).
- Core: `FUN_140639830` fills the candidate table. Each entry with score 0 gets PRESS (0x34), and `T+0xa548[k]`/`T+0xa55c[k]` store the arrival record from `FUN_140535260`. Men are then picked by `FUN_140639a20`.
- PRESS vs DELAY choice: the `T+0xb709` concept-flag plus `|carrier − ball| ≤ 13 m` branch. PRESS is reason 0x34; DELAY is reason 0x33.
- Constants from the raw immediates: 25 (0x41c80000), 64 (0x42800000), 90 (0x42b40000), 81 (0x42a20000), 155 (0x431b0000), 3 (0x40400000), 4 (0x40800000).
- `FUN_140639100` (press target): picks the carrier spot, or the slot target blended by `FUN_140a51320`. Live data and fit are in `docs/re/pes-re-defensive-shape.md` §14.3.
- `FUN_140639600` angle codes: `docs/re/pes-re-defensive-shape.md` §15.14.4.

## GF gap
- `TeamAIController::IsPressing` (`teamAIcontroller.cpp:1932`) is team-level: ForeCheckPress running and `player == possession player`. There is no per-player PRESS/DELAY state.
- `ChooseReaction` (`pesdefenderdecisions.cpp:144`) has no PRESS/DELAY input. The tackle ball gate and safety veto (PES exempts PRESS/DELAY/SAND men) are applied to every defender (`elizacontroller.cpp:880-890`).
- The flag `T+0xb715` (`SealOffSideSurroundingPress`) is set by the concept start at `0x1405794e0` and read at `0x140537278`, `0x140697703`, `0x140964745`. GF does not model it.

## Out, with blockers
- **ReverseSideCounter** (`FUN_1405a58b0`, `FUN_140599d90`, `FUN_140a540a0`, `FUN_1404c8e60`, `FUN_1404f6110`, vetoes `FUN_140576f60`/`FUN_1405773f0`): `FUN_1404c8e60` has no decompile in the scratch dirs. The others are decompiled in `the TeamConcepts scratch`, `d4.c`, and `the DefensiveShape scratch`, but not decoded or ported.
- **Live attacking patterns** (0x1405465b0…0x140572000): not started.
- **Second presser** `FUN_140634470` / `FUN_140632ae0` / `FUN_140633940`: decoded structure only (`pes-re-defensive-shape.md` §14.4, §15.15). Not ported.

## Suggested next step
Split part 1 into: (a) per-player PRESS/DELAY state plus writer `FUN_140536fc0` head and body gate; (b) candidate table `FUN_140639830` and press pick `FUN_140639a20`; (c) second presser. Decode `FUN_140535260`, `FUN_1404567e0`, `FUN_140a42940`, `FUN_140a6ce10` first.

## Helpers decoded this pass (dec.sh, cross-checked against the decompile body)
- `FUN_1404567e0(out)`: writes the 0xff table: 22 bytes of 0xff, then two 11-byte rows (`+0xb0` = 0 flag each), then 0x0a0a... pad. Init for the 4 `T+0xa548` slots in the writer.
- `FUN_140a42940(team, slot, role)`: stores role at `+0xa2a0+slot*4`, calls `FUN_140a40200` (ring push of `(slot, role-time)`, 4-deep), clears the 0x40-byte record `+0x28c+slot*0x40`, then sets per-role defaults for roles 1, 0x1d..0x1f, 0x22 (copies 0x100/1), 0x27 (zeroes). Roles other than these leave the record zeroed.
- `FUN_140a6ce10(role, flag, carrier)`: role 5 with a non-null carrier returns true when `FUN_140a4d660(carrier) != 5`; role 6 or 7 returns true when `flag == 0`. Otherwise false.
- `FUN_140535260(out, team, &slot, t)`: wrapped accumulator. `out = wrap(out + t*k)` where k is `_DAT_14259be5c`, wrap modulus `_DAT_14259858c`. The first term comes from `FUN_140439e50(out, ...)`, an angle difference of two vectors. It is an angle/phase record, not an arrival time.
- `FUN_140a53f90(team, &slot)`: true only when the ball holder is the team's keeper-side man. Returns byte `lVar2+0x2917`. Returns 0 otherwise.
- `FUN_140a4d660(player)`: starts from role `*(FUN_140a3e190(team,0))`. For role 5 it returns 13 (0xd) when either the speed/time test fires (`fVar13 < _DAT_14259be20` with `iVar11` vs `fVar14*fVar16`), or the carrier-proximity branch fires. Thresholds are the `_DAT_...` constants, not yet read from the install, so no numbers are claimed here.
- `FUN_140a603c0(level, role)`: table lookup on `UNK_142645a40..a5c` (0x20-byte rows per role, 7 buckets of `*param/100`), linear interpolation on the `%100` remainder. `FUN_140a604d0` is `FUN_140a603c0(level,6) > _DAT_14259bdc4`.
- `FUN_140a62ea0(player, idx)`: returns the 0..21 slot of the idx-th team member when idx < 2, else 0xff (`FUN_140a3b320`).
- `FUN_140a62610(team, mask, t)`: first 0..10 man (per row) whose `mask` byte is set and whose `t` entry is zero; 0xff otherwise.
- `FUN_140a57720(player) = player + 0x2948`, `FUN_140a6e150/e5e0/e340/e580`: role-bit readers on table `UNK_1426484f0` (bits 3/5/6, and a byte at `+...f2`). `FUN_1404c8e60(team, idx)`: `*(u32*)(team + 0x24710 + (byte[team+0x247be+idx] + bucket)*8)`, bucket 0 or 11.

Status after this pass: the four requested helpers are decoded. Still open for (a): the exact role of `FUN_140a627e0` (head), and the body that reads `T+0xa548` (part b). No code committed.

## Writer body structure (ds1.c 1131+, lines 1250–1420 read)
- Gate (line 123): `T+0xf1 || T+0xb712 || T+0xb715`. Then `FUN_140a604d0(FUN_140a40bd0(...),6)` must be false, and the `0x3b0` object test (`func_0x1423e7fd0` true, `func_0x14234e8e0 == 0x10` sets `cStack_258 = 1`).
- `T+0xb70c == 0` (line 134). Then `FUN_140a627e0(lVar27,0)`; if the slot is > 0x15 the 13 m loose-ball test (169) applies, and role `3..` check uses `FUN_140a6ce10`.
- Candidate fill: `aiStack_200[0..14]` init (`0` then `-1`s), `FUN_140639830` fills them, then the 11-man loop writes `FUN_140a42940(team,i,0x34)`, `T+0xa548[slot]=team*11+i`, and `FUN_140535260` into `T+0xa55c[slot]`.
- Second loop (`iStack_204 < 3`): `FUN_140639a20` picks, then the constants 25/64/90/81/155/3/4 gate PRESS vs DELAY. Role `0x3a` is the "already assigned" role; `func_0x140a42b80(team,slot,role)` reassigns it. `FUN_1404a53e0` and `func_0x140a92b90(…, 60.0)` gate the cross-pitch distance.
- `FUN_140a57800`, `FUN_140a92b90`, `FUN_1404a53e0`, `func_0x140a42b80`, `func_0x140a40bd0` are not yet decoded.
- Conclusion: the port needs the callees above. Nothing committed.

## Callees decoded (pass 3)
- `FUN_140a92b90(vec, eps)`: wrapped-angle test. Returns 0 when `|wrap(x)|` lies in `[eps, 360-eps]`, else 1 (i.e. "within eps of a boundary").
- `FUN_1404a53e0`: wrapped angle difference (same shape as `FUN_140439e50`).
- `FUN_140a42b80(team, slot, role)`: writes role at `+0x278+slot*0x40` (role-only setter).
- `FUN_140a40bd0(team, out)`: returns `team+0xa294` unless `team+0xa29c` is set, then 0.
- `FUN_140a57800`: FieldInfo getter (pes-re-fieldinfo), per the other reports.
- `FUN_140639830(team, side, cand, out)`: candidate filter. For each of 11 men (`side*11+k`, k=0..10): skip if the man is the carrier (`FUN_140a627e0`), or not on the field, or farther than `sqrt(_DAT_1425cee20)` from the ball, or not in the carrier's group. It bumps `out[1]`, calls `FUN_140639600(team,side,&man,0)` for the angle code, sets `out[0]=1` when `FUN_14044e0d0(team,man) < _DAT_1425656c0` (= 4.0), stores the code at `out[4+...]`, and sets `out[8+4*code]`. Helpers still undecoded: `FUN_140a41190`, `FUN_140479470`, `FUN_14044e0d0`, `FUN_140639600` (angle codes, see defshape §15.14.4).
- `FUN_140639a20(team, side, cand, out)`: 250+ line press picker (carrier distance, `_DAT_14259be18` ramp, 11-entry arrival table at `afStack_1d4`). Calls `FUN_1404a53e0`, `FUN_140a92b90`, and the undecoded `FUN_140639600`, `FUN_140a41190`, `FUN_140479470`, `FUN_14044e0d0`, `FUN_1404a53e0`.

## Blocker (honest)

## Press helpers decoded (pass 4)
- `FUN_140639600(team, side, &man, t)`: returns the angle code 0..3 of the pick. It forms the man-to-carrier vector (`FUN_140439e50`) and the heading error `t` against the carrier's `0x254` facing (`FUN_140a4db20` when `0xb712` is clear, else the direction vector). Code 0 when `FUN_140a41190 == 0`. Codes 1/2/3 use the bands `_DAT_14259be38`, `_DAT_1425cb488`, `_DAT_14259be30`, `_DAT_14259be60`, `_DAT_1425c48d4`, `_DAT_1425c6608`, `_DAT_1425cee28`. Returns 0, 1, 2, or 3 by those bands. Matches the angle codes in defshape §15.14.4.
- `FUN_140a41190(team, &man)`: index of `man` in `team+0xa548[0..3]`, else -1. This is the "already pressing" slot test.
- `FUN_140479470(team, slot)`: finds `slot` in the 24-entry table at `+0x20` (stride 0x74), returns 1 when that row's byte `+0xd` is 0, else 0.
- `FUN_14044e0d0(team, idx)`: reads `team+0x24710 + (byte[team+0x247be+idx] + bucket)*8` and passes it to a stored function pointer (`_DAT_1425301f0`). This is a log/sink, not game state, so the port drops it.

## Status (pass 4)
All of the named callees are decoded. The port of (a)+(b) still needs GF mappings: PES's per-team arrays (`T+0xa548`, `T+0xa55c`, `T+0xb88c`), `FUN_140a48a10` / `FUN_140a48a30` (team/player access), the `0x2482c` / `0x55c` position offsets, and the 0x74-stride 24-row table. These are decided in the port, not yet written. No code committed.
Earlier blocker text superseded by pass 4 above: every named callee is now decoded. The remaining blocker is the GF-side data plumbing listed under "Status (pass 4)", not the decode.

## Picker tail (pass 5)
- `FUN_140639a20` pass 1 (lines 1–~100 of decompile): for each of 11 men of the side, skip unless the man is in the `0x1748` on-field table and not the carrier. Score = squared distance to the carrier (or to the ball when none), plus `fVar6` when the man's role is not 0x3a, plus `fVar5` when `FUN_140a41190 < 0` (not yet a presser), plus a heading term (`FUN_140439e50` difference against the carrier heading, `_DAT_14259be18` ramp, `_DAT_14259bdf0` span). Writes `(man, score)` into two 11-entry buckets (`abStack_128[0..1]`) keyed by `man > 10`.
- `FUN_140639a20` pass 2: calls `FUN_140a79640(buckets, 0)` to sort. Then, for each candidate in the bucket for this side, within `fStack_20c` (=`_DAT_14259be18 + ramp*_DAT_14259bdf0`) of the carrier, computes `FUN_140639600` at three offsets (`fVar28`, `+fVar28`, `-fVar28`) to pick the first presser (`aiStack_108[code]`), and writes the result into `out` as `(man, score)` with a count in byte `out+0xb0+bucket`.
- `FUN_140a79640(list, flag)`: compacts the two 11-entry buckets (entries with `man < 0x16`), then sorts each with `FUN_140a79440` (comparator not yet decoded), then writes back. `flag` = 0 selects the full path.
- Undecoded for the port: `FUN_140a79440` (sort comparator), `FUN_140a71540` (float; carrier-side scalar), `FUN_140a62690` (opponent press state), `FUN_140a48a30` / `FUN_140a48a10` (team/player index), `func_0x1415a1780` (sqrt), and the `_DAT_*` constants at the picker's top (read from the install in the port).

## Status (pass 5)
Commits (worktree the worktree): 6c9b547 pure picker (score, bearing, ramp, angle code, sort, constants by VA); c24c256 pass-2 driver (`RunPresserPicker`, 9 tests); 618215a pass-1 bucket (`RunPresserPass1`, 2 tests); 3bd5b6d excluded-man helper (`PressExcludedMan` = `FUN_140a62610` via `FUN_140a62690`, flag array at team+0x4c00+4i, 2 tests). `pes_press_test`: 27 pass, 1 install relation skip-free with PES21_ROOT. Mutations caught and reverted: angle fall-through, presser-role exemption, on-field guard.

## Pass-2 fields (pass 6, decoded from `FUN_140639a20` lines 218–368)
- `lStack_218` = `func_0x140a48a30()` → the defending side's team object. `+0xf1` is a byte flag gating the first-presser skip (`*(char*)(lStack_218+0xf1)==0 && iVar9==0 && fVar27 > 100`), `+0xa2a0 + man%11 * 4` is that side's role table (int[11]), compared to `0x3a`.
- `lVar24` = `func_0x140a48a10(DAT_1436f3dc8)` → the carrier player. `+0x55c`/`+0x564` = carrier position (x, z). `+0x5d0` = carrier action state; `(state - 0x33) < 2` (states 0x33 DELAY and 0x34) enables the slot re-assignment step.
- `lStack_220` = output bucket array (the param_4 `PressCandidate` pair). Bucket count bytes at `+0xb0 + side` (side = `man > 10`); each entry is 8 bytes (man int, score float) at `+ (count + side*11) * 8`.
- `lVar15 = *(param_1+0x18)` (the picker's press-slot table): `+0x18 + (man%11)*4` is the slot index for that man (-1 when none); `+1` is set to 1 when a man is left without a slot.
- Second bucket: `FUN_140a79640(&uStack_1d8, 0)` compacts and sorts the 22-entry `uStack_1d8` pair table into two 11-entry buckets (`0xff` sentinels, `man < 0x16` kept), then writes the sorted men back to `param_1` (`+0x0`, `+0x8`, ...) at 2-word stride, then fills `+0xb2`/`+0xba`/`+0xc2`/`+0xc6` with sentinel bytes and writes each man's bucket index at `+0xb2 + man`. Insertion sort on ≤11 entries (`FUN_140a79240` branch).
- Constants read for pass 2 (verified in image): `0x1425a9464`=36 (pass-2 delay threshold), `0x1425c6780`=81 (slot threshold), `0x14259bdf4`=8, `0x1425656c0`=4, `0x1425656d0`=0x80000000 (sign-bit XOR for the mirrored angle).
- Still open: `FUN_140a62690` (opponent press state, cross-check pending), `FUN_140a4d660` constants (`0x14259ce38`, `0x14259bddc`, `0x142565600`).

Status: pass 2 and its pass-1 builder are ported as pure code. Tick wiring (commit 2b) is NOT done. Blockers: writers of team+0xf1 (presserFlag), team+0xa548 (press-slot man table), team+0x4c00 (excluded flags), and the press-target consumer (`FUN_140536fc0`, part a) are undecoded. Wiring now would feed guessed PES state into `IsPressing`, so it is held.

## Writers of the wiring inputs (pass 7; per-function scan `fscan.py`/`fscan2.py`/`fscan3.py`)
Scan: per-function linear decode over `.pdata` (158,633 functions), exact displacement match; byte-resync scans gave false hits and are not used.

| Field | Writers (raw store addr, containing fn) | Meaning | Per tick? |
|---|---|---|---|
| `T+0xf1` (presserFlag / concept-armed byte) | `FUN_140501283` 0x140501315 (clear, then set to 1 on event type 7 in the event walk), `FUN_140501468` 0x140501ab0 (same walk, second side), `FUN_140a415a0` 0x140a4166a (team ctor reset to 0) and ~35 other stores in unrelated functions (none documented) | Set when the newest event list has type 7 | Yes, from the event walk |
| `T+0xa548` (press-slot man table, int[4]) | `FUN_140536fc0` 0x140537410, 0x140537453 (clears 4 entries at head, writes slot men mid-body); `FUN_1404161e0` 0x1404170a1 (team copy) | Output of the PRESS writer itself | Yes; this is the port's output, not an input |
| `T+0x4c00` (position category, int[11]) | No store in the entire module. Reads only, 19 `lea`/`cmp` sites | Static formation position category; 0 = GK (docs `pes-re-defensive-shape.md` §474) | No; set at team setup |
| `T+0xa508` (team mode, int) | `FUN_140581980` 0x1405819c0 / 0x140581a2c (values 1/2/3 and countdown 0x30), `FUN_140a415a0` 0x140a41604 (ctor = 3) | Counter-trigger state (`counter-trigger.md` §1.1) | Yes |

Consequences for wiring:
- `presserFlag` is NOT a press-layer input to decode further: it is the event-walk flag. GF needs the event-type-7 mapping before it can feed pass 2. Not wired.
- `T+0xa548` is produced by the PRESS writer (part a). The writer `FUN_140536fc0` is still undecoded at body level here; its gates (§1.7) are documented, its body is not ported.
- `T+0x4c00` maps to GF's player role (GK = 0 check in `FUN_140a62610`), so no writer port is needed for it.

Status (pass 7): all wiring-input writers are located. Wiring (2b) is still held: the press-target writer (`FUN_140536fc0`) and the event-type-7 mapping are the blockers.

Smoke BEFORE wiring (commit 3bd5b6d, `gfsim_run --db-sql assets/fixtures/teams.sql --home 1 --away 2 --seed 7 --minutes 20 --pes21-root the PES21 install`): HOM 2 - 0 AWA; possession 34%-66%; shots 8-4; xg 0.44-0.21; determinism 82741f3fb1e8fe9d; wall 2m47s. Full output in the worktree scratch dir `smoke_before.txt`.

## Press writer body structure (pass 8; `FUN_140536fc0`, raw 0x140536fc0–0x140537452)
- Head: clears `T+0xa548[0..3]` to 0xff where the man index < 0x16; `T+0xa558 = 0xff`. Early exit when `FUN_140a53f90`, `FUN_140a62690` opponent press state, or the level check `MI+0x116c != 1` fail.
- Gate chain (matches §1.7): level flag `FUN_140a604d0(…,6)`, match object `+0x3b0`, `T+0xb70c == 0`, 13 m loose-ball gate (169.0 @0x1425c6788), restart gate `FUN_140a51320 > 2`.
- Output path (raw 0x140537187–0x140537380): `FUN_140639830` fills the 11-slot press choice (`aiStack_200[4..14]`); for each chosen slot: `FUN_140a42940(team, i, 0x34)` sets PRESS action, `T+0xa548[slot] = man`, `FUN_140535260` produces the arrival record into `T+0xa55c[slot]`. Then `FUN_140639a20` (ported as pass-2 picker) and the per-man loop.
- Undecoded helpers on this path: `FUN_140a42940`, `FUN_140535260`, `FUN_140639830`, `FUN_140a53f90`, `FUN_140a6ce10`, `FUN_140a6e150/5e5/340/580`, `FUN_140a40bd0`, `FUN_1404a53e0`, `FUN_140a92b90`, `FUN_140a57800/57720`, `FUN_14159f3e0`, `FUN_140a604d0`, `FUN_1404567e0`.
- Status: the writer is NOT ported. Its output is the PRESS action plus `T+0xa548`/`T+0xa55c`; wiring it requires those helpers.

## Event type 7 (pass 8)
- Event walk `FUN_140501283` / `FUN_140501468`: when the event byte `(b & 0x7f) == 7` and `b < 0xf8`, the team byte `T+0xf1` is set to 1 (cleared at walk start).
- Meaning: INF(med) dribble-touch family (defence-leftovers §2.1; duels-fouls §2.4 "events 6/7: missed the ball"). Not mapped to a GF event yet.

## Helper decodes (pass 9; raw-checked against decompile)
- `FUN_140a6e150(role)` / `e5e0` / `e580`: role-class predicates. For role > 0x5d return 0. Else bit 3 / bit 5 / bit 6 of `u32 table[role]` at 0x1426484f0 (returned as bool). `FUN_140a6e340(role)`: byte at 0x1426484f2 + 4*role, bit 0. Table contents not dumped yet; the predicate meanings (which role classes) are still INF.
- `FUN_140a6ce10(&role, bool, ctx)`: `role==5` → true if ctx and `FUN_140a4d660(ctx) != 5`; `role` in {6,7} and bool==0 → true; else `role in [9,14]`. Depends on `FUN_140a4d660` (undecoded).
- `FUN_140a40bd0(team, out)`: if `team+0xa29c` set, zero `out` through `FUN_140a602b0`; else `*out = team+0xa294`.
- `FUN_1404a53e0(out, vec)`: zero-vector → 0; else `out = wrap(FUN_140a92d40(vec) * 1/k)` into [0,mod); `_DAT_14259be40`, `_DAT_14259858c` are the scale and wrap modulus (values not dumped yet).
- `FUN_140a92b90(vec, eps)`: wraps `vec.x` into [0,_DAT_142598580) and returns 0 iff `eps <= x <= mod - eps`, else 1 (angle-near-boundary test).
- `FUN_140a57800(team) = team+0x88`; `FUN_140a57720(team) = team+0x2948`.
- `FUN_140a604d0()`: `FUN_140a603c0() > _DAT_14259bdc4` (threshold not dumped).
- `FUN_1404567e0(out)`: fills the 0xc6-byte candidate record: `out[0..21] = 0xff`; two 11-entry rows at +0xb0 with flag byte 0 per row; then constant 0xa0 fills at +0xb2, +0xba, +0xc2, +0xc6.
- `FUN_140a40200(ring, slot, t)`: 4-deep ring of `(slot, t, t)`; push only if the newest slot differs; advances head when the stored time < t, wraps at 3.
- `FUN_140479470(team, man)`: finds the first of 24 records at `team+0x20 + 0x74*i` with `man` ID; returns 1 if `+0xd` byte == 0 on that record, else 0.
- `FUN_14044e0d0(team, man)`: looks up a 32-bit value at `team+0x24710 + 8*(byte at team+0x247be+man + half)`, half = 0 for side 0 (`man<11`), 11 otherwise; passes it to `_DAT_1425301f0` (an output/print hook, not decoded).
- `FUN_140a53f90(ctx, &cand)`: fetches ball-carrier info via `FUN_140a3e7d0` and `FUN_140a60530`; looks up the man `FUN_140a489d0`/`FUN_140a3b320`; returns `team+0x2917` byte of the ctx-team when the man's flag at `+0x4bd4` is 0 and the candidate is 0xff or equal. Partially decoded; `FUN_140a3e7d0`, `FUN_140a60530`, `FUN_140a3b320` still undecoded.

## Still undecoded on the writer path
`FUN_140a4d660`, `FUN_140a3e7d0`, `FUN_140a60530`, `FUN_140a3b320`, `FUN_140a489d0`, `FUN_140a602b0`, `FUN_140a603c0`, `FUN_140a92d40`, `FUN_140a6e5e0`–table dump at 0x1426484f0, `FUN_140a6cf70` (tail of `FUN_140a57800`), `FUN_14159f3e0` (a logging/stub that returns early on the null-stack check; likely a hook, not game logic).

## Helper decodes (pass 10)
- `FUN_140a489d0(ctx, side)`: `side < 2 → *(ctx + 0xa3+side)*0x28` (player-set pointer for side 0/1); else `*(ctx+0x1978)`. This is the PMS getter for a side (Main's note).
- `FUN_140a92d40(a, b)`: angle helper = `atan(a/b)` with quadrant fix (`b==0` → ±π/2 constants). Doc `pes-re-keeper-port-2.md` agrees: `FUN_140a92d40(a,b) = atan(a/b)`. Used by defender windows (`pes-re-defender-decisions.md` §405).
- `FUN_140a602b0(out, v)`: `out = clamp(v*100, 0, 700)`.
- `FUN_140a603c0(team, col)`: per-level table lookup, `level = team_value/100` clamped 0..6, `frac = team_value%100`, interpolated over the table at 0x142645a40 (32-byte rows indexed by `col <= 0x21`). Same function as the carrier-AI §7 "difficulty table" (`pes-re-carrier-ai.md` §7).
- `FUN_140a60530(&pair)`: `pair[0] < 2 && pair[1] < 0x28` → 1, else 0.
- `FUN_140a3b320(team, man)`: finds the man's slot 0..21 in the team's 0x28-entry records; `0xff` if not found or slot ≥ 11 when found, else `team*11 + slot`.
- `FUN_140a3e7d0(ctx, out, n)`: ring lookup of the n-th most recent ball event (8 deep, 800-byte records at `ctx+0x1908`), returns `0x1436f3a40` default when n ≥ `ctx+0x3208`; a type-0x37 record is replaced by `FUN_140a60500()`.
- `FUN_140a4d660(ctx)`: defender role decision (returns role 13 = 0xd or the input role). Uses `FUN_140a57720` (team+0x2948), `FUN_1415a1780` (sqrt), `FUN_141e532d0` (distance/time helper), `FUN_140a62ea0`, `FUN_140a6cf50/cf30/cf10`, `FUN_140a57450`, `FUN_14042d470`. Constants `_DAT_14259ce38`, `_DAT_142565600`, `_DAT_14259be20`, `_DAT_14259be30`, `_DAT_14259bddc`, which are the same family as defender-decisions. Not yet fully decoded; the sub-helpers are the next batch.

## Helper decodes (pass 11)
- `FUN_140a62ea0(ctx, side)`: `side < 2` → `FUN_140a3b320(FUN_140a489d0(ctx,side), …)` slot lookup; else 0xff.
- `FUN_140a6cf50(team, k)` / `cf30` / `cf10`: per-slot team pointers. `k < 3` → `team + (3k+0x14)*4` (cf50), `team + (3k+7)*4` (cf30), `team + 0x74 + 4k` (cf10 value); `k >= 3` → `team+0x68`, `team+0x34`, `team+0x7c`.
- `FUN_140a57450(team, out, k)`: `k == 0` → `out = (team+0x2948, team+0x294c, team+0x2950) / |·|` (unit vector, dividing by `FUN_141e532d0()`); `k > 0` → difference `P(k) − P(k−1)` using `FUN_140a6cf70`.
- `FUN_141e532d0()` = `_DAT_14351ae54` (global scalar, a fixed constant: the "per-tick speed/time scale" used as a divisor).
- `FUN_14042d470(x)` = `(c·c / _DAT_14256ae34) · x` where `c = FUN_141e532d0()` and `c` multiplied by `_DAT_14259a764`.
- `FUN_140a60500()` returns the sentinel `0xff00000002` (type 0x37 replacement record).
- `FUN_140a3e190(ctx, n)`: `n == 0` → newest event (`ctx+0x180698`); else the n-th most recent in the 800-byte ring; type 0x37 → `0x1436f3d60`; returns `record+20`.
- `FUN_140a627e0(ctx, n)`: `n == 0` → `*(ctx->+0x18)+0x2601c` (ball player id); else resolves the n-th event's player through `FUN_140a3e7d0`, `FUN_140a489d0`, `FUN_140a3b320`.
- `FUN_140a4d660(ctx)`: decode continues in pass 10 notes. Still open: `FUN_140a6cf70` (the team position record, 0x88 base) body details beyond `+0x88`.

## Pass 12: role-property table at 0x1426484f0 (u32 × 94, raw from pes_full.bin)
Nonzero entries (index: value): 0:0x1, 2:0x1, 3:0x5, 4:0x4008, 5:0x400c, 6:0x400c, 7:0x4020, 8/9/10:0x10000, 11/12:0x4080, 13:0x4040, 14:0x4100, 15:0x2000, 16:0x3002, 17:0x3000, 18:0x10, 20/21:0x2, 22/23/24:0x2000000, 25:0x20000, 26:0x2040000, 27/28:0x60000, 29:0x40000, 30/34:0x2080000, 31/32/33/36-39:0xa0000, 35:0x84000, 40:0x200002, 41-43:0x220000, 44:0x2100000, 45-48:0x100000, 49:0x2400000, 50/51:0x420000, 52:0x2800000, 53:0x3000000, 54:0x1020000, 55:0x2, 56-61:0x20200, 62:0x2000000, 63/64:0x20000, 65:0x20400, 66:0x20800, 67/70/71/91:0x2, 68:0x24000, 69:0x4200, 72:0x8000, 73:0xc000000, 74:0x4000000, 75/76/79-86:0x8000000, 87-89:0xc000000, 90/92:0x10000000.
Predicates (`pass 9` helpers): bit 3 (`>>3`) = `FUN_140a6e150`, bit 5 (`>>5`) = `FUN_140a6e5e0`, bit 6 (`>>6`) = `FUN_140a6e580`, byte 0x1426484f2 bit 0 (= bit 16 of the same u32) = `FUN_140a6e340`. Bit 15 = JOSTLE per pes-re-duel-sim.md.

## Pass 12 helpers
- `FUN_140a6cf70(team, k)`: `k < 0xfa` → `team + (5k+0x11)*8`; else `team+0x2770` (per-man position record, 40 bytes each).
- `FUN_140a51320(ctx)` (restart pressure value): man in `FUN_140a62ea0`, `>0x15` → `ball-event(+0x10)` countdown `MI+0x1198 − event.frame`; else the newest event (`FUN_140a3e7d0(…,0)`) record: 0 if its type == 0x37, else `record[+0x14]`.
- `FUN_140a62690(team, side)` (= pass 1's `side*11 + FUN_140a62610`): `FUN_140a62610(side, MI, team)` is the slot, `0xff` if none. Cross-check done: matches the notes.
- `FUN_140a41190(team, man)`: index 0..3 of `man` in `team+0xa548`, else `0xffffffff`.

## Pass 13: writer FUN_140536fc0 body, lines 1–240 read (decode in ~/ghidra-projects/scratch/Unpacker/out/w_main.c)
Order of the body:
1. `T+0xa548[0..3]` cleared to 0xff for man < 0x16; `T+0xa558` saved to `uStack_244`, set 0xff.
2. Entry gates: `MI+0x78 >= 0x16` required; `FUN_141b92460(2) >= 5` required; `FUN_140a53f90(team,&out)` must be ≤ 0; `FUN_140a62690(team, side)` = slot in 0..21 with `FUN_140a48910` role class not in {0x41,0x42}... (`(role-0x41) < 2` ⇒ exit); `MI+0x116c == 1` required. `FUN_140a51320(team)` must be ≥3 unless the newest event type is 1..3 or 0x10 or `FUN_140a6ce10` is true.
3. Loop over 11 men of the side: man with `MI+0x1748` on-field, role predicates (`e150`, `e5e0`+bit22 clear, `e340`+bit9 clear, `e580`) true, and `act.t+0x1a` countdown within the `MI+0x1198` window ⇒ exit (no writer this tick).
4. If `T+0xf1 || T+0xb712 || T+0xb715`: `FUN_140a40bd0(team, &out)`, `FUN_140a604d0(…, 6)` gate, match `+0x3b0` referee check with `FUN_14234e8e0 == 0x10` (cStack_258 flag), `T+0xb70c == 0`, loose-ball 13 m gate (`FUN_140a57800` ball distance² ≤ 169), event-type checks (`FUN_140a6ce10`).
5. Candidate fill: `aiStack_200[15]` initialised (slot 0..10 at 4..14 = -1; [0..3] = 0), `FUN_140639830(team, side, team_ctx, &uStack_208)`.
6. For each of 11 men with candidate `aiStack_200[4+k] >= 0`: `FUN_140a42940(team, k, 0x34)` (PRESS), `T+0xa548[slot] = man`, `FUN_140535260(…)` writes `T+0xa55c[slot]`.
7. If `iStack_204 < 3`: `FUN_1404567e0` init, `FUN_140639a20` (pass 2 — ported), then the press-target loop over `auStack_1b8` candidates. Per candidate: `FUN_140a48a10` player pos, distance² vs `_DAT_1425c6780` (=81), `uStack_208._1_1_` flag, `T+0xa2a0+slot*4 == 0x3a`, then `T+0x74` slot record match on `+0x20` and `+0xd`, break.
Still open for the port: `FUN_140639830` (candidate fill body; partly in report line 58), `FUN_140a40bd0`, `FUN_140a604d0`, `FUN_140a6ce10`, `FUN_140a53f90` (partial), `FUN_140a53f90`'s `FUN_141b92460`, and the remainder of the press-target loop after line 240 of `w_main.c` (`LAB_140537887` onward).

## Pass 14: writer body fully read; helpers decoded
Writer `FUN_140536fc0` (w_main.c 1–358) structure, final:
A. Clear `T+0xa548[0..3]` (man<0x16) to 0xff; save `T+0xa558`→`old_a558`; set `T+0xa558=0xff`.
B. Entry gate (any fail → exit): `MI+0x78 >= 0x16`; `FUN_141b92460()`=5 must be ≥5 (constant 5); `!(FUN_140a53f90(team,&ev)>0)`; `FUN_140a62690(team,side)` slot exists and its role class is not in `{0x41,0x42}` (`FUN_140a48910`+1 minus 0x41 < 2 → exit); `MI+0x116c == 1`. `FUN_140a51320(team)` ≥3, or `newest.type ∈ {1,2,3,0x10}` (`(t-1)<3 || t==0x10`) → skip that test, else `FUN_140a6ce10` true → skip.
C. For each of 11 men k of the side: exit if (on field && (`e150(role)` || (`e5e0(role)` && bit22(role)==0) || (`e340(role)` && bit9(role)==0) || `e580(role)`) && `act.t+0x1a` within the window `MI+0x1198 − 15-bit compare`). That is `[+0x1a] < [+0x1c]` countdown check.
D. If `T+0xf1 || T+0xb712 || T+0xb715` (armed): `FUN_140a40bd0(team,&ev)`; `FUN_140a604d0(ev,6)`; match `+0x3b0` check (`FUN_1423e7fd0` true → `FUN_14234e8e0 == 0x10` else exit, sets flag `cStack_258`); `T+0xb70c == 0`; ball-distance² > 169 → exit (only if `FUN_140a627e0(team,0) > 0x15`, i.e. no ball-owner); `FUN_140a3e190(0)` type check as in B.
E. Candidate fill: `aiStack_200` = int[18]: `[0..3]`=0 (flags for code 0..3), `[4..14]`=−1 (per-man code), `[15..17]` unused; `uStack_208` short (`[0]` flagA, `[1]` flagB). `FUN_140639830(team, side, team_ctx, &cand)`.
   Per man k (`uVar9=side*11+k`, k<11): skip if `k==held` (`FUN_140a62690`); on field; dist²(man,ball) ≤ `_DAT_1425cee20`=462.25; and ((`act==0x34` && (k>10 || `psVar11==0`)) || (`FUN_140a41190(team,k)`≥0 && `T+0xa558 != k`)) and `FUN_140479470(man)`≠0. Then `cand.count++`; `code = FUN_140639600(team,side,&man,0)`; `fVar = FUN_14044e0d0(team,man)`; if fVar < 4.0 → `flagA=1`; `aiStack_200[4+k]=code`; if `code==0` → `flagB=1`; `aiStack_200[code]=1`.
F. For each of 11 men: if `aiStack_200[4+k] >= 0`: `FUN_140a42940(team,k,0x34)` (PRESS), `T+0xa548[slot]=man`, `T+0xa55c[slot] = FUN_140535260(...)` (aim/phase angle write).
G. If `count<3`: `FUN_1404567e0(auStack_1b8)`, `FUN_140639a20(team,side,&cand,auStack_1b8)` (pass 2 — ported), then press-target loop over `auStack_1b8` (up to 11 entries). Per entry: `man ≥0x16 → break`; `code=aiStack_200[4+man%11]`; `d²=dist²(man, ball)` vs 81.0 (`_DAT_1425c6780`); if `flagA==0 && code != 2`: if `d² ≥ 81` or `role==0x3a`(action) → next; set `flagA=1`, `code=0`, go to slot test. Slot test (`T+0xb709`): ... (`LAB_140537749`): `pick role 0x3a` → `FUN_140a42b80(…,0)`; else `FUN_140a42940(…,0x34)` (PRESS) or `…,0x33` (DELAY); `T+0xa548[k]=man`; `FUN_140535260`→`T+0xa55c`; `aiStack_200[k]=1`; `count++`; `break if count>2`.
H. After loop: if `T+0xf1 && count<2 && uStack_248<0x16` → `FUN_140a42b80(…,0x34)`. If `uStack_24c<0x16 && uStack_254<0x16 && uStack_24c != uStack_254` → `FUN_140a42940(…,0x33)` on `uStack_24c`, `FUN_140a42940(…,0x34)` on `uStack_254`.

Newly decoded helpers (pass 14):
- `FUN_140a48a30(ctx,side)`: side<2 → ctx+(side+0xa1)*0x28; else ctx+0x1928 (side's PMS).
- `FUN_140a48910(ctx,k)`: k<0x1a → ctx+(k+0xa5)*0x28; else ctx+0x19c8.
- `FUN_140a489f0(ctx,k)`: k<0x16 → ctx+(k+0xf3)*0x28; else ctx+0x25f8.
- `FUN_140a48a10(ctx,k)`: k<0x1a → ctx+(k+0xbf)*0x28 (player record for man k); else ctx+0x1dd8.
- `FUN_140a6e150/e5e0/e580(role)`: role>0x5d → 0; else bit 3/5/6 of u32 `0x1426484f0[role]` (`role` = PES action-property index).
- `FUN_140a6e340(role)`: role>0x5d → 0; else byte `0x1426484f2[role*4]` & 1 (= bit 16 of the same u32).
- `FUN_140535260(out, side_ctx, &man, k)`: angle = `atan`-bearing of `(player − ball)` (`FUN_140439e50`), then `out += k*_DAT_14259be5c (=90°)`, wrapped to [0,360). `k` is the slot index for `T+0xa55c[]`. So `T+0xa55c` = the "press phase/angle" per slot (90° per slot step).
- `FUN_140a42940(team,k,action)`: stores `action` in `team+0xa2a0+4k`, calls `FUN_140a40200` (ring push), clears 0x40-byte record at `+0x28c+k*0x40`; action 1 / 0x1c<action<0x20 / 0x22 / 0x27 set record fields (see code).
- `FUN_140a42b80(team,k,action)`: stores `action` at `+0x278+k*0x40` only (the role-less store).
- `FUN_140a40bd0(team,&out)`: `team+0xa29c` set → 0; else `team+0xa294`.
- `FUN_140a604d0()`: `FUN_140a603c0(...) > _DAT_14259bdc4 (= 1.19e-7)`.
- `FUN_140a6ce10(ev, flagBool, ctx)`: `ev.type==5` → `ctx!=0 && FUN_140a4d660(ctx)!=5`; `ev.type-6 < 2 && !flag` → 1; else `ev.type-9 < 6`.
- `FUN_140a53f90(team, &out)`: newest event (`FUN_140a3e7d0(…,0)`) passes `FUN_140a60530` (type<2, player slot<0x28) → returns `*(byte)(state+0x2917)` if the man is not the candidate, else 0.
- `FUN_141b92460()`=5. `FUN_14234e8e0(match)`=`*(match+0x8c)`. `FUN_140a92b90`: angle-in-range test (`|x| mod 360 in [r, 180−r]` after reduction).
- `FUN_140439ef0(p,q)`: bearing (degrees) of `q−p` with the same atan convention as `FUN_140a92d40`, times `_DAT_14259be40` (=57.2958 rad→deg).
- `FUN_14043a0a0(a,b)`: signed angle difference in degrees, wrapped to ±180.
- `FUN_1404a53e0(out, vec)`: `out = bearing(vec)` in degrees [0,360).
- Install constants used (read from pes_full.bin, f32): 1425cee20=462.25, 14259bdc4=1.19e-7, 1425c6784=155, 14259be44=64, 14259be5c=90, 14258c264=3, 14259bdf4=8, 1425c6780=81, 14259be20=25, 14259a764=60, 142565600=0.5, 14259ce38=3.6, 14259be30=45, 14259bddc=1.5, 14259be40=57.2958, 1425795e4=2π, 14259858c=360, 142598580=180, 142588218=−1, 14256ae1c=1.0, 14253ee30=100, 1425656c0=4.0, 14256ae34=1000, 1425c6788=169.

## Pass 15 (writer tail and helpers; verified against decompile)
- Writer press loop (w_main.c ~240-358), per entry `man = cand[k]`, `u = man % 11` (man < 0x16 else u=0):
  - Pick branch when `T+0x87d0[u*8]==0` and (`FUN_140439ef0(man.pos, ball) > fVar7` gate) fails the distance-band test via `FUN_14043a0a0(·, bearing)` in `[fVar6, fVar10)`; otherwise go to LAB_140537749 (slot test).
  - Slot test (LAB_140537749): if `iVar14==0 && u<11 && T+0xa2a0[u]==0x3a && cStack_258==0` → `FUN_140a42b80(T,u,0)`, `uStack_248=man`; else if `bVar4` (concept ok) → PRESS: `FUN_140a42b80(T,u,0x34)` or `FUN_140a42940(T,u,0x34)`; else DELAY: `…(T,u,0x33)`, `uStack_254=man`.
  - Then `uStack_244==man && FUN_14044e0d0(…,man) < 9.0` → `uStack_24c = man`. Write `T+0xa548[slot]=man`, `FUN_140535260` → `T+0xa55c[slot]`, `aiStack_200[slot]=1`, count++; break when count>2.
  - Tail: if `T+0xf1 && count<2 && uStack_248<0x16` → `FUN_140a42b80(T,uStack_248%11,0x34)`. If `uStack_24c<0x16 && uStack_254<0x16 && uStack_24c!=uStack_254` → `FUN_140a42940(T,uStack_24c%11,0x33)` and `FUN_140a42940(T,uStack_254%11,0x34)`.
- Install constants: `FUN_140a4db20(out, ctl)`: out = 270.0f (0x43870000); if ctl == -1 out = 90.0f (0x42b40000).
- `FUN_140439e50(out, a, b)`: bearing of (b−a): zero if same; else `wrap(FUN_140a92d40(dx,dz) * radToDeg)` into [0,360) (`_DAT_1425795e4` = 2π added if negative first).
- `FUN_140a627e0(team, 0)` = `team->match+0x2601c`; `(team,side!=0)` = `FUN_140a3b320(FUN_140a489d0(...))` else 0xff.
- `FUN_1423e7fd0(p)` = `p+0x9c` byte.
- `FUN_140a40200(ring, id, t)`: ring of 4 `{id,t,t}` records, head at +0x30; replace head when id differs; advance head when old `t < new t`, wrap at 3.
- `FUN_140a4d660(ctx)` (role-5 ctx check): ~100 lines, geometry using `FUN_140a57720` (team+0x2948), `FUN_0001415a1780` (sqrt), `_DAT_14259ce38`(3.6). Undecoded tail (lines 144–199 of hh7.c). Blocks `FUN_140a6ce10` role-5 branch.
- Still open before a port: `FUN_140639830` candidate fill body, `FUN_140535260` arrival-record body, `FUN_140a42940` action-store body, `FUN_140a4d660` tail, `FUN_14043a0a0` sign, and `T+0x87d0` (per-slot line) semantics.
- Status: writer NOT ported; no code change after 3bd5b6d; no wiring; no gate/smoke run after 3bd5b6d.

## Pass 16 (PressLayerPort2): status, no code
- Re-read `w_main.c` (writer body, lines 54–352) and `hh7.c` (`FUN_140a4d660` body, raw 0x140a4d660–0x140a4d7a2). Pass 14 structure A–H holds; no contradiction found.
- `FUN_140a4d660` (role-5 defender decision, feeds `FUN_140a6ce10`) is fully read but depends on undecoded callees: `func_0x000141e532d0` (distance/time), `FUN_140a6cf50/cf30/cf10` (team position records), `FUN_140a57450` (vector record), `FUN_14042d470` (angle/speed). Their bodies are not yet in the scratch decompiles; decode them before the role-5 branch can be ported.
- Event type 7 → GF touch mapping and the 2b wiring are not started. The PRESS/DELAY state the writer produces (`T+0xa2a0`, `T+0xa548`, `T+0xa55c`) has no GF home yet; `TeamPressState` in `teamAIcontroller.hpp` holds only `role` and `pressSlot`.

## Pass 17 (PressLayerPort2): constants, pure helpers committed
- Install constants read by VA (`PeImage::ReadVa`): `0x14351ae54` = 54.0 (time scale `T`, .data file-backed); `0x14259a764` = 60.0; `0x14256ae34` = 1000.0. `FUN_14042d470(x) = T*60^2/1000 * x` (`PressSpeedKmh`). `FUN_140a92b90` boundary test (`PressAngleOutsideBand`, eps is an argument; boundary 180 at `0x142598580`, full turn 360 at `0x14259858c`).
- Role table `0x1426484f0` (94 u32) loaded from the image (`LoadPressRoleTable`); bit predicates `PressRoleHas` for bits 3/5/6/16 (`kRoleBitA/B/C/Jostle`).
- `FUN_140a40200` 4-deep stamp ring (`PressStampPush`), tested (same id no-op, advance on older head, wrap).
- Commits (worktree): 7a72f65 speed and angle-band helpers; 6bac53b role table; 2a2bf30 stamp ring. Tests: `pes_press_test` 34 pass; install relation tests skip without PES21_ROOT and pass with it.
- Not ported, with reason: the team-record offset getters `FUN_140a6cf50/cf30/cf10` and `FUN_140a6cf70` are address arithmetic over PES's team memory layout. GF has no matching storage, so a port would only be an unused offset table. They map onto GF data once the writer's inputs are wired.
- Not started: writer `FUN_140536fc0` (needs `FUN_140639830`, `FUN_140535260`, `FUN_140a42940` action body, `FUN_1404567e0`, `FUN_14044e0d0` output hook), 2b wiring, event type 7 → GF mapping, parts (c), 2, 3. No gate or smoke run for this pass because no behavior changed yet.
- Parts (c), 2 and 3 not started.
- Next: decode the five callees above in one batch (`dec.sh`), then port pure helpers first (`FUN_14043a0a0`, `FUN_140a92b90`, `FUN_140a40200`, `FUN_1404567e0`, role-class predicates), then the writer with a GF-side action ring.
- Callees of `FUN_140a4d660` decoded (dec.sh, raw-checked): `FUN_141e532d0()` returns the global `_DAT_14351ae54` (read at runtime from the install, not hardcoded). `FUN_14042d470(x) = (T*_DAT_14259a764^2 / _DAT_14256ae34) * x`, with `T = FUN_141e532d0()`. `FUN_140a6cf50(team,k)`: k<3 → `team+(3k+0x14)*4`, else `team+0x68`. `FUN_140a6cf30(team,k)`: k<3 → `team+(3k+7)*4`, else `team+0x34`. `FUN_140a6cf10(team,k)`: k<3 → `*(team+0x74+4k)`, else `*(team+0x7c)`. `FUN_140a57450(team,out,t)`: t==0 → `(team+0x2948..0x2950)/T`; else the difference of position records `FUN_140a6cf70(team,t)` and `FUN_140a6cf70(team,t-1)`.
- `FUN_140a4d660` is now fully decodable. Still to read from the install by VA: `_DAT_14351ae54`, `_DAT_14259a764` (60), `_DAT_14256ae34`.
- Status: writer still NOT ported; no code, no wiring, no gate or smoke run in this pass.

## Close-out (owner order)
- Commits present on the current history (hashes rewritten from the earlier notes; same messages): `8f7f0b6` pure presser picker + synthetic/install tests; `b128a92` pass-2 driver `RunPresserPicker`; `4c5a006` pass-1 bucket builder `RunPresserPass1`; `a8d03f7` `PressExcludedMan` (FUN_140a62610). Also on history: `7a72f65`, `6bac53b`, `2a2bf30`, `790bdd5` (press helpers / ring), by other workers, not verified here.
- Uncommitted work: none found in tracked files (checked with submodule diffs ignored).
- Not done: writer `FUN_140536fc0` port; wiring 2b (`teamAIcontroller.cpp` ~1932); event type 7 mapping; parts (c), 2, 3; gate and after-wiring smoke. Baseline smoke at the old `3bd5b6d` (seed 7, 20 min): HOM 2–0, possession 34–66%, shots 8–4, xG 0.44–0.21, determinism 82741f3fb1e8fe9d.
- Next step: decode `FUN_140639830`, `FUN_140535260`, `FUN_140a42940`, the `FUN_140a4d660` tail (hh7.c 144–199), `FUN_14043a0a0` sign, then port the writer with TDD, then 2b wiring, gate, smoke.
- Close-out commit `9d7a0f9`: presser constants loader (`PressConstantsLoader`) from the install, with tests. `pes_press_test` 39/39 pass (PES21_ROOT set); `check_pii.sh` rc=0. Tree clean after this commit.

## Pass 18 (PressLayerPort2): fill tally, close-out
- `PressCandidateFill` (FUN_140639830 gates and tally) is implemented with 3 synthetic tests (`CountsPassersAndSetsFlags`, `LowIndexNeedsCarrierTagOrStampedSet`, `NothingPassesGivesZero`). Its code is inside HEAD `9d7a0f9`, whose message says constants loader only. The fill was swept into that commit from the shared worktree; not rewritten (no history edits). `pes_press_test` 39/39 pass with PES21_ROOT.
- Not done: the k=10 team-flag test I drafted (not in the file); the fill's distance gate (`462.25`) and `FUN_140a41190` / `FUN_140479470` / `FUN_140639600` inputs are caller-precomputed, not yet read from match state; `FUN_14044e0d0` hook value is unresolved (`_DAT_1425301f0` holds a runtime-set pointer; the install slot is `0x3398c92`, not a valid VA). So `flagA` cannot be fed from the install yet.
- Also done earlier this pass: `TeamPressState` gained `actionRing` and `SetAction` (790bdd5, mirrors `FUN_140a42940` role store; the 0x40-byte record at `T+0x28c` is not ported).
- Not started: writer `FUN_140536fc0` (steps A–H), `FUN_1404567e0` init table, `FUN_140535260` arrival bearing, 2b wiring (`ChooseReaction`, `BlockRadius_m`, `IsPressing`), event type 7 → GF mapping, parts (c), 2, 3.
- Gate: not run for this pass (only `pes_press_test`). Smoke: not run; no behavior changed. Next: decode `FUN_140a4d660` tail and the `T+0x87d0` slot line, port `FUN_1404567e0` and `FUN_140535260`, then the writer, then wiring, gate, smoke.
