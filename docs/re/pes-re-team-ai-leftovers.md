# Team-AI leftovers: tactics / transitions / defender-aggression / match-management (TeamAILeftoversRE)

Tags: **OBS** = raw/decompile at the VA or a read-only live read; **INF(conf)** = inference.
Prior reports assumed, not re-derived: `docs/re/pes-re-tactics.md` (TacticsRE),
`docs/re/pes-re-transitions.md` (TransitionsRE), `docs/re/pes-re-defender-decisions.md`
(DefenderDecisionsRE), `docs/re/pes-re-match-management.md` (MatchManagementRE).
Notation as TransitionsRE: `G`, `MI`, `T`, `P(i)`, `hu(i)`, `ai(i)`, `MA`, `F`, `fps=54`, `dir`.
Scratch: the TeamAILeftoversRE scratch dir (`r1a.c`…`r18a.c` annotated decompiles,
`live_q.py`, `live_reg*.py`, `live_subc.py`, `samp1.jsonl` + analysis).

Live match state: at task start the match was in phase 1/2 (second half into extra
time, `per` 3 → 10); a 20 Hz sampler (`live_samp.py` in the scratch dir, own read-only
memory fd) captured 4611 rows over ~177 s. Mid-capture the match returned to a menu
(`G` null, `MI` null, `ph` 0 rows zeroed), so per the brief all remaining live
checks are listed as open. Findings below marked live use the 2895 non-menu rows.

## 1. Tactics

### 1.1 Writer of the active-instruction array `MA+0x25cf8` / `MA+0x25d38` — OPEN
OBS: the whole image contains exactly 9 immediates of `0x25cf8` and 9 of `0x25d38`,
all paired reads (`cmp [base + idx*4 + 0x25cf8], id` / `cmp [base + idx*4 + 0x25d38], slot`):
`0x140a623ab/0x140a62494` (id 3/9 eligibility), `0x140a633ab/0x140a6354b`,
`FUN_140a63950` (`0x140a6397a`), `FUN_140a64410` (`0x140a6444b/0x140a6448b`, ids
0x10/0xf), `0x14147bcd0` (id 0xe UI marker), `0x142160bd9` (checksum walker).
No `mov [.., 0x25cf8], ..` store exists anywhere in the image, so the writer does
not use a `0x25cf8` displacement (base+register-offset store, DMA/reflection fill,
or a computed `lea`). Same negative result as TacticsRE O7.
Live (OBS, `samp1.jsonl`): team 0 slots `[0(TARGET? id0), 7, 14, 0]` tgt
`[255,255,21,255]`; team 1 `[0, 0, 16, 14]` tgt `[255,255,255,10]` — constant over
all 2895 non-menu rows (match was in a stoppage phase `ph` 2, consistent with
TacticsRE §7.1 "defensive instructions only out of possession" being frozen).
INF(low): the 0x25d38 slot array sits exactly 0x40 past 0x25cf8; a writer storing
the pair would still show one of the two immediates, so the fill path likely goes
through the MA reflection layer (same INF as TransitionsRE §9.5 for `MI+0x11a0`).
Next: value-scan for the `lea` form is N/A (only 9 hits each, all reads); break on
the MA+0x25cf8 write with a hardware watchpoint in a per-frame capture session.

### 1.2 Writer of `T+0x210` (plan-command shift) — OPEN, narrowed
- OBS (`0x140a41810` inside the team-record init reached from `FUN_140a415a0`):
  `mov dword [rbx+0x210], 2` — init value 2, matching the live constant.
- OBS (`0x1404166d7/0x1404166dd` in the T copy `FUN_1404161e0`): `T+0x210` is
  copied (`mov eax,[rsi+0x210]; mov [rdi+0x210],eax`) — the copy path, not a writer.
- KILLED: "`FUN_140a64e20` writes `T+0x210`" (already killed in tactics §9; confirmed
  again OBS: it is a pad-command-buffer ctor, `param_1[0xe] = 2`, `+0x7c = 2`; its only
  two callers `0x140427db4/0x14042adb3` build 0x103c-strided buffer arrays). The
  `mov [rsi+rbp+0x210], ecx` at `0x140429847` and `mov [rsi+rbx+0x210], 0x3f800000`
  at `0x140429be1` are fields of that pad buffer struct (entry `+0x20e` plan-command
  byte, `+0x214`, `+0x3f8/0x458` floats), reached through pad-command jump-table
  cases 0–3 (pad codes 9–12) — not the team record.
- A full-image dword-store scan for `[reg+0x210]` (OBS, capstone sweep) finds no
  other team-struct writer: all remaining hits are stack spills, other structs'
  `+0x210`, or menu/UI records (`0x1410xxxxx`). So there is **no in-match writer**;
  the value is init-2 plus menu-driven copies. Live (OBS): `T+0x210 = 2` for both
  teams in all 2895 non-menu rows.
- Consequence (OBS, re-verified in `r12a.c` `FUN_1404f8350` non-pad branch): with
  `t == 2` the `T+0x210` clamp is a no-op and `want = map(T+0x134)` always.
Next: confirm the menu provenance (which UI struct feeds the `FUN_1404161e0` copy)
if match-management needs more than "constant 2 in match".

### 1.3 "Not reduced" toggle read sites — reduced (OBS raw unless noted)
Most-frequent functions first. All keep the prior reports' value meanings.

Attacking Style (k0):
- `FUN_1405a3080` (`0x1405a31f0`): OBS — Counter (k0==0, `test eax,eax; jne` over the
  branch): when k0==0 AND `T+0xb6e0 == 0`, zero two support accumulators
  (`xorps xmm10/xmm12`); Possession skips the zeroing and uses the computed weights.
- `FUN_14060b590` (`0x14060b6a9`): OBS — Counter: keeper-hold check
  (`FUN_140a53f90`) then opponent-leader distance test (`FUN_140a57800`) gates the
  long-keeper-distribution branch; Possession (`jne 0x14060bd93`) skips to the
  short-distribution path.
- `FUN_1421197b0` carrier plan ctor (`0x142119855`): OBS — the returned k0 value is
  stored to the carrier plan (`mov [rbp-0x74], eax`); a following `cmp [rsi+8], 4`
  fast-path (`mov edi, 4`) selects the 4th plan entry for one style only (which side
  needs the ctor's caller context — INF(med): Possession keeps the short-plan entry).
- `FUN_1404fedc0` (`0x1404ff66f`): OBS — Possession (`cmp eax,1; je`): `movaps xmm8,
  xmm7`, else `movaps xmm8, xmm10` — support-weight constant swap before the
  `T+0x6d50` concept-flag loop.
- `FUN_140592cf0` (`0x140592f2d`): OBS — Counter (`test eax,eax; jne` over an early
  `ret`): k0==0 returns immediately (`al=1`); Possession continues the function.
- `FUN_1405938a0` (`0x14059390c`, same shape — INF(high) same early-return idiom;
  not disassembled this pass).
- `FUN_14066bde0` (`0x14066c1dc`): OBS — Counter: `MI+0x11a0 ≤ fps·k+0.5` recency test
  (`mulss/addss/cvttss2si` window, `cmp ebx,eax; cmovb r12d,esi`) gates the branch;
  Possession (`jne 0x14066c225`) skips it.

Attacking Area (k2): `FUN_14063a420` fully reduced (OBS): a reader-struct fill —
three consecutive dwords from three toggle reads: `[rsi+4] = k1 (Build Up)`,
`[rsi] = k0 (Attacking Style)`, `[rsi+8] = k2 (Attacking Area)` (raw
`0x14063a443–0x14063a475`: r8d = 1 → `[rsi+4]`; r8d = 0 → `[rsi]`; r8d = 2 →
`[rsi+8]`). No tactic effect at the site. Remaining Area sites (`1405d8b70` lane
tests, `14060a8d0`, `140680980`, `14067d6d0`, `14099d650`, `142132020`) stay as
reported.

Build Up (k1): all three un-reduced sites are the same Short-pass gate idiom
(OBS `cmp eax,1; je SHORT-path`):
- `FUN_14065ff20` (`0x14065ffdc`), `FUN_140660220` (`0x14066033d`): Short
  (`==1`) takes the `r9b=1` + `FUN_1405a4830` check path directly; Long falls into
  the `test esi,esi / test edi,edi` pre-gate first. Run-chooser short-combination
  permission (INF(high)).
- `FUN_140662a10` (`0x140662a6d`): Long + flag set → immediate `al=1` return (run
  allowed); else falls through to the `FUN_140a50250` attack-level check. So Long
  bypasses the level gate here (OBS).
- `FUN_14060bdd0` (`0x14060be42`): `test eax,eax; jne` — Counter/Long build-up
  (`==0`) continues into the keeper-release + `FUN_140a57800` distance branch;
  Short jumps to `0x14060c373` (short-outlet path).
- `FUN_1404fbca0` (`0x1404fbe08`), `FUN_140509f60` (`0x14050a010`): Short selects an
  alternate float constant (`movss xmm10, [rip]` vs zeroed) before the same field-size
  ramp — support-shape constant swap (OBS branch, constants not read).
- `FUN_1405e9f50`, `FUN_140606cd0`, `FUN_14060cb50`, `FUN_14065ff20`-adjacent
  `FUN_140662d60`/`FUN_14068c050`/`FUN_142111db0`: call sites confirmed present but
  the branch effect not traced this pass (still open, low value — all are the same
  short-pass-chooser family).

Positioning (k3): sites confirmed as Flexible (`==1`) free-position branches
(OBS spot-check `FUN_1405dc350` region: `call 0x140a62180` with r8=3 in the support
scorer; full body decode beyond this sweep). No change to the report.

Defensive Style (k4) + Containment (k5): `FUN_14063a420` (above) covers the only
un-reduced k4 reader; `FUN_140528380`, `FUN_1406216a0`, `FUN_1406295a0`,
`FUN_14062acf0` keep their reported effects. `FUN_1406295a0` (OBS): a large
state-gated team-AI pass (checks `T+0x255`, `MI+0x116c == 1`, order `+0x1c == 3`)
— the k4/k6 reads sit past the gate; not a tactic branch per se.

Pressuring (k6):
- `FUN_1409677c0` (`0x140967d2a`): OBS — the k6 value is stored (`mov [rbp-0x50], eax`)
  then used as a selector: Aggressive (`test ebx,ebx; jne`) picks constant
  `1.8` (`0x14259bde0`), Conservative picks `~4.66e-10` (`0x14259bdfb`, i.e.
  effectively 0) in a `mulss` weighting chain (75.0/90.0 angle clamps nearby).
  INF(med): tackle-commit style weighting.
- `FUN_14096a4f0` (`0x14096ab95`): OBS — same idiom: k6 read feeds a
  clamp/normalise chain (`comiss` vs two rip constants, `subss/divss/mulss`,
  `minss`); exact semantics need the caller (still open at the constant level).
- `FUN_140971ba0` (`0x140971f8e`): the slide-aggression L assembly (see §3;
  `test eax,eax; jne` over the `inc ebx`).

### 1.4 Instruction read sites — reduced most-frequent-first (OBS decompiles)
- `FUN_1405d8b70` (12 sites) + `FUN_1405dc350` (13 sites): support-position scorers.
  Pattern (OBS `r16a.c`): `id 4` (BOTH_SB_UP) tested with `0xff` (team-wide) as a
  permission gate around full-back support/overlap branches; `id 3` (FALSE_SB) and
  `id 9` (FALSE_WG) tested with the candidate slot as eligibility (`FUN_140a62360`
  / `FUN_140a62440` results `uStack_168/uStack_164` feed the branches); `id 8`
  (DEFENSIVE, per-slot) vetoes run candidates; `id 6` (TIKI_TAKA) gates the no-near-
  mate cell multiplier (already in tactics §4.1); `id 2` (FALSE_9) adds the −10
  score term. Net effect INF(high): the supporter loop skips ineligible slots and
  re-weights cells; no single constant to quote (score accumulation).
- `FUN_1405e6e80` (10 sites, OBS `r17a.c`): same family — `id 4` team-wide
  permission, `id 9/3` slot eligibility, `id 8/0xa` per-slot vetoes, `id 1`
  (OPEN_WIDE) lane branch, `id 2` score term, k2 (Area) lane weight
  (`FUN_140a62180(...,2)` at line 1193).
- `FUN_140623750` mark spot (7 sites, OBS): `id 0xe` (MARK_HARD, per-slot) and
  `id 0xb` (CAUTION_CROSS, team) select the mark-distance variant; `id 0xd`
  (GEGEN_PRESS) and `id 0xc` (CAUTION_THROUGH_PASS) select the press/cover blend;
  k6 (Pressuring) Conservative adds the +2.0 m (tactics §5, re-confirmed at raw).
- `FUN_1405325e0` mark pass (6 sites, OBS `r17a.c` lines 2930–3135): `id 0xd` read
  once (start-test veto context); `id 0xb` read once (result discarded — same dead-
  read shape as slider k1); `id 0xe` (per-mark-target) gates three pairing-score
  branches: with MARK_HARD the `+[rbx+8]` addend path and the `FUN_14052c440`
  check are skipped (`je LAB_140532eef` = reject pairing); k4 (Defensive Style)
  All-out drops the addend (already reported). The `fVar27 += [rbx+8]` addend is
  INF(med) a mark-tightness bonus.
- `FUN_140528380` (6 sites, OBS): k4 (Style) read at entry
  (`FUN_140a62180(...,4)`), then `id 0xe/0xd` per-target tests select tight-mark
  vs zonal pairing branches (same veto-to-`LAB` shape as `1405325e0`).
- `FUN_14096dfc0` tackle scorer (5 sites, OBS raw): `id 0xc` (CAUTION_THROUGH_PASS)
  at `0x14096eb8b`: when set AND slot `< 0xb`, the `T+0xb8c4` byte must equal `sil`
  or the candidate's score addend (`movss xmm10, [rip]` = 0.5-ish constant) is
  skipped. `id 0xd` + `id 0xe` at `0x14096f19e/0x14096f1ba`: combined gate —
  `0xd` result saved to `r15b`, `0xe` to `[rbp+0xc8]`; both clear → pad-slot scan
  loop (`imul rcx,rax,0x74`, `cmp [rcx+rdx+0x20], r9d`), i.e. the CPU-tackle
  permission path; either set → jump to the conservative branch (`0x14096f239`).
  INF(med): Gegenpress/Tight-marking make this tackler defer.
- Remaining instruction rows (`§6.3` ids 5/7/0xf/0x10, run selectors, zone/line
  functions) keep "not reduced" — same scorer/branch family, lower frequency.

### 1.5 Six-designated-players field at sheet `+0x143c` — CONFIRMED (OBS)
- Accessor `FUN_1408c5080(S, k)`, k ≤ 5 (default 10). Live (OBS): team 0
  `[9, 7, 9, 5, 18, 8]`, team 1 `[8, 4, 8, 8, 2, 1]` — non-default, so the field is
  populated in this match (tactics O8's "live 8,7,10,1,7,10 / …" was one frozen
  frame; values differ per match phase/side).
- k=5 reader (OBS `r7a.c` `FUN_1404a90d0`): `uVar6 = FUN_1408c5080(sheet, 5)`,
  mapped to an on-pitch slot via `FUN_140a3b000`, fallen back via
  `FUN_140a621f0(..., 5, side, 0xff)` (designated-player fallback scorer, raw
  `0x140a621f0`–`0x140a6233e`: walks 11 slots, skips absent/unavailable, prefers
  role-class match from `T+0x3fb8/T+0xc48`, returns best or 0xff). Called from the
  kicker-setup path (restart choreography) — INF(high): k5 = penalty-kick taker.
- k=3/4 reader (OBS `r8a.c` `FUN_1404a8620`): `uVar6 = 3 or 4` by the sign of
  `dir·ball-x` (`0.0 < dir·fStack_88 ? 4 : 3`), i.e. corner side selects k3 vs k4;
  same map+fallback pattern — INF(high): k3/k4 = left/right corner takers.
- `FUN_1404a8ad0` (OBS): general k-indexed reader (`FUN_1408c5080(sheet, k)`,
  `param_3 < 0x16` slot path) used by the free-kick/dispatch logic with the
  keeper branch (`FUN_140a62690`) — INF(med): remaining k's cover free-kick takers.
- Sub swap (OBS `r6a.c` `FUN_140520930` tail): for each committed sub, every
  `FUN_1408c5080(sheet, k)` equal to the outgoing player is re-pointed at the
  incoming player (`FUN_1408c5cf0`), and every `FUN_1408c5060(sheet, j)` (set-piece
  takers `+0x1454`) equal to him is reset to 0xff (`FUN_1408c5cb0`).
  So `+0x143c` = the six designated players, `+0x1454` = three set-piece takers;
  not tactics. GF: no counterpart (GF has no designated-taker memory; kicker
  choice is contextual).
- Which-k-is-which beyond k3/k4/k5 stays INF(med): k5 penalty, k3/k4 corners,
  k0–k2 captain + free-kick takers by elimination. The `FUN_140a62ee0` mapper
  (tactics O8's suggested next step) is OBS a pad-identity→slot mapper
  (`r5a.c`: scans 0x28 pad ids for `param_3`, returns `*base·0xb + idx`), not a
  k-label decoder — that lead is dead.

## 2. Transitions

### 2.1 `ReverseSideCounter` start `FUN_140577a10` + continue `FUN_140577800` (OBS raw + decompile `r1a.c`)
Class `SubConceptReverseSideCounter` (vtable `0x1425c46c0`: gate `0x140543f40`
= Counter-Attack k0==0 with the side≠2 quirk — raw `cmp edx,2; jne` falls through
to a `side==0` test, INF(med) a "not human-controlled" gate; start `0x140577a10`,
continue `0x140577800`, reset `0x140576a20`, execute `0x140577c30`).
- Start (all must hold): counter-allowed latch `T+0xb6e0` set (OBS `0x14057acd3`:
  set to 1 when the counter-window helper `FUN_14057a580` passes, cleared to 0 when
  `FUN_14057ab30` passes — a per-tick counter-availability latch); start-age window
  `(now − concept+0xc) ≤ fps·4` frames (raw: `sub ebx,[rdi+0xc]; fps; shl eax,2;
  cmp eax,ecx; ja fail` — fps·4 ≈ 4 s, OBS); carrier `c = MA+0x2601c`, else
  opponents' `T'+0x78`, must be < 0x16; slot `uVar12 = c % 0xb`;
  `T+0xb748[uVar12] == 0x30` (slot free); carrier lateral
  `|c.z| ≥ F[0x14] − 2.0` (`F+0x14 − 2.0 ≤ |z|`, raw `subss xmm1, 2.0; comiss`);
  `FUN_140a4c720(side5-or-0, c.x, dir, −5.0)`: INF(med) the carrier is ≥ 5 m upfield
  of the offside line (same helper/signature shape as SideCounter's goal-side
  count); then EITHER `FUN_140a4e720(c, attackHeading, 90°, 10)` (facing within
  90° of goal) OR `FUN_140a4e250(c, 7.0, 0)` (within 7 m of something — INF(low):
  support-distance variant); then ALL of `FUN_140576f60` (== 0),
  `FUN_1405773f0` (== 0), and runner `FUN_140576a60(...) < 0x16`. Records
  `+0x20 = runner, +0x1c = carrier, +0x18 = 0, +0x24 = 0`; returns 1.
- `FUN_140576f60` (runner-side veto, OBS): walks our 11 slots in the MA depth list
  `FUN_140a714e0`; counts men ahead of the carrier within 60° heading / 15 m
  lateral (`bStackX_18`) and eligible switchers (`bVar15`); returns 0 (allow) only
  when a switch target exists with gap ≥ 3 m, or ≥ 5 m with numbers-up conditions
  (constants 3.0/5.0/15.0/60.0/100° OBS). INF(high): "no switch without a
  numbers-up lane".
- `FUN_1405773f0` (blocker veto, OBS): walks our 11, counts opponents within
  45°/25–900 m lane cones (`45.0/25.0/900.0/30.0/1.5/0.6944` OBS); returns counted
  blockers; start needs 0. INF(med): reverse-side lane must be clear.
- `FUN_140576a60` (runner pick, OBS): scans all 22 on-pitch slots with
  `T+0xb748 == 0x30`, own-frame-x behind the opponents' offside line
  (`MA+0x44+0x14·opp`), opposite-side-of-carrier test (`fVar11 != fVar10` sign
  split on `±z`), gap `−5 ≤ Δx ≤ 30` (`−5.0/30.0`), lateral `≥ 20 m` from the ball
  (`20.0`), nearest-marker ≥ 25 m (`25.0`, via `MA+0x22e4c` distance list), and two
  `FUN_140576820` feasibility calls (10.0/10.0/1.0/1.0 args); picks the deepest
  such man (keeps overwriting while scanning upfield). Returns 0xff none.
- Continue `FUN_140577800` (OBS): needs in play, our possession (`T+0x255`),
  both records < 0x16, carrier still ours-or-loose; then the opposite-side
  condition (`fVar11 != fVar10` from the two men's `±z` signs) AND lateral gap
  `|z_A − z_B| ≥ 15.0 m`; stage machine on `+0x18`: stage 0 while age
  `≤ fps·3.0` frames; stage 1 same window but re-checks `FUN_140576f60` (lane
  re-validation); stage 2 checks `FUN_140a71460(...) < 625.0` (25² — INF(med) a
  25 m regroup radius); else (stage ≥ 3) stop (returns 1). So the reverse-side
  run lives at most ~3 s per stage and ends when both wide men are on the same
  side or within 15 m laterally.
- Execute `FUN_140577c30` (OBS): stage 0 builds the carrier pass plan
  (`FUN_140a84e30/84d40/84fb0/85120/85160` on `T+0x6f4`); stage 0→1 when the
  carrier holds (`== +0x1c`); stage 1→2 when the other man is upfield of the
  carrier and his speed > 22 (`FUN_140477130`, `22.0`); stage 2 issues the pass
  to the runner via the history-gated branch (`FUN_140a60300` event checks).
  Then every tick: runner (if not already `SPACE_RUN_CHANCE 0x1e`) gets
  `FUN_140a40000(state → 0x1e)` + `FUN_140a429a0(slot, 0x1e, lane-flag by sign of
  `z` ±) and `T+0xb748 = 4` (ReverseSideCounter order), `T+0xb774 = 0x1e`,
  `T+0xb7f8 = 4`; age `+0x24++`. Reset `FUN_140576a20` clears the pass-plan slots.
- Live: no ReverseSideCounter episode in `samp1.jsonl` (team sheets at the time:
  team 0 Possession k0=1, team 1 Counter k0=0 but `a508 = 0`/stoppage phase —
  start needs in-play possession; listed open in §5).

### 2.2 `PossessionMfJoinDefenceLine` (OBS)
- Gate `FUN_14053d730` (slot 1): returns false for side 2, else
  `FUN_140a62180(ctx, side, 0) == 1` — Possession Game only (re-confirmed raw,
  8 instructions).
- Start (slot 2) is the false stub `0x141863350` (`ret 0`, OBS) — the manager can
  never start it; dead on the selector path (as TacticsRE §2 reported).
- Continue/end `FUN_14053d600` (slot 3, OBS `r1a.c`): returns true (stop) unless
  ALL hold: in play (`MI+0x116c == 1`); instruction id 4 NOT armed; `T+0xb8a2 == 0`
  (level < 4); `T+0x87a4 == 4` (INF(low): a shape/phase byte); age
  `(now − concept+8) ≥ fps·0.5` frames; ball own-frame-x behind the opponents'
  offside line (`dir·MA+0x2482c`, per-side `MA+0x40/0x44`-style table at
  `MA+0x44+side·0x14` — same addressing as the runner pick). So IF started by a
  non-selector path it would persist while the ball is behind the offside line
  and stop otherwise; no writer of its running flag besides the selector exists
  (TacticsRE §2), so the body is unreachable in match play.
- Reset/execute `FUN_14053d760/14053d5f0` (OBS raw): write `0xff` to record slots
  (`+0x18/+0x1c/+0x20`); no movement orders — consistent with dead code.

### 2.3 Per-concept enables in `team/subConcept.json` (registry 0xc4) — CLOSED (OBS live)
The selector reads the deserializer object for registry index 0xc4
(`FUN_141e5d4c0(ctx+0x10, 0xc4)`, raw `0x14057986f`), then per id
`[reg+8+4·id] ≤ 0` → skip (raw `add rbx,8; cmp [rbx],0; jle`, `0x1405799ec–0x140579a1f`).
Live (OBS `live_reg4.py`, modded match): object at `0x1f489610`, ids 0–47 ALL 1
(`offset0` = a magic/header dword then forty-eight 1s). So every concept id is
enabled; the only gates are the sheet predicates (slot 1) and the start tests.
This is the stock/mod `constant_team.bin → subConcept.o` (208 B) content live;
the VFS `constant_team.bin` itself is a compressed archive (raw bytes only), not
parsed here. No per-concept tuning distinguishes the two live teams.

## 3. Defender-aggression team terms (DefenderDecisionsRE §11 item 5) — CLOSED (OBS)
Code refs: standing-tackle geometry `FUN_14096fda0` (`r18a.c`), slide chain
`FUN_140971b60 → FUN_140971ba0` (raw `0x140971f8e–0x1409720a3`).

- `T+0xa298` (u32, team defensive mode; getter `FUN_140a40bc0` = single
  `mov eax,[rcx+0xa298]; ret`, OBS).
  - Set by (OBS): init `FUN_140a415a0` → 4 (`mov [rbx+0xa298], 4` at `0x140a41636`);
    menu setter `FUN_140a424d0` (`mov [rcx+0xa298], edx`, OBS) called once from the
    team-setup path `0x141fba4c5` (match/team setup switch, OBS context) — i.e. a
    pre-match team-defensive-mode choice (UI provenance below the setup switch;
    values 1–5 by menu branch). Copied by the T-copy `FUN_1404161e0`
    (`0x140416fca/0x140416fd2`). No in-match writer found (only these + the copy).
  - Effect (OBS): tackle `A = ENFORCER(bit12 of `+0x48` via `0x14124dad0`) + (A==3 ?
    +1)`; slide `L += 2` iff `FUN_140a40bc0(...) == 3` (raw `cmp eax,3; jne; add
    ebx,2`); inside the own box (`FUN_140a4c7a0`) with L ≥ 3: `L = 2` if mode==3
    else 0 (raw `cmove`). Tackle behind-tolerance (OBS `r18a.c` lines 344–382):
    A=0 → 45°, A=1 → 11.25°, A=2 → 0° (code: default 11.25, A≠1→45, A==2→0);
    touch-timing window `0.075·fps / 0.1·fps / 0.125·fps` for A=0/1/2 (OBS
    immediates in the decompile).
- `T+0xa53c` (byte, press flag).
  - Set by (OBS): init `FUN_140a415a0` → 0 (`mov [rbx+0xa53c], sil` at `0x140a41656`);
    copied by the T-copy `FUN_1404161e0`/`FUN_140a406a0`-family (`mov [rdi+0xa53c], al`
    at `0x14041707d`); read in the press-timer `FUN_140535650` tail
    (`if (*[T+0xa53c]) fVar18 = −5.0`, OBS `r9a.c`). The only `mov [r...+0xa53c], 1`
    class stores are the init/copy; the live value flips (see below), so the setter
    is a reflection-layer or per-tick publisher write without a displacement store —
    same open class as `MI+0x11a0`. INF(med, from `FUN_140535650` structure): set when
    the late-game losing-side press conditions hold (period ≥ 4 i.e. second half
    `MI+0x1168`, minute `FUN_140a74d70 > 19`, team level > 2, score behind via
    `FUN_1408c9df0` sums, ball in the attacking half with `MI+0x11a0 > 9`).
  - Effect (OBS raw `0x140972020`): slide `L += 1` iff `== 1`
    (`cmp byte [r12+0xa53c],1; lea edi,[rbx+1]; cmovne edi,ebx`). No effect on
    tackle A. Live (OBS): team 0 `a53c = 0`, team 1 `a53c = 1` across all non-menu
    rows (team 1 was the losing/out-of-possession side, `poss = 0`, `a508 = 0`).
- `T+0xb8a2` (byte, `L >= 4` flag; written in `FUN_1404f6c30` right after
  `FUN_1404f8350`, OBS `r12a.c` lines 216–222: `= (L > 3)`, paired with
  `T+0xb8a3 = (L < 1)`).
  - Set by: the level writer only (no other writer; 82 image hits are all reads).
  - Effect (OBS raw `0x140972052`): slide `L += 1` iff `!= 0`
    (`cmp byte [r12+0xb8a2],0; lea ebx,[rdi+1]; cmove ebx,edi`), then `min(L, 3)`
    (`cmp ebx,3; cmova`). No effect on tackle A. Live (OBS): 0 for both teams
    (L stayed 2, pad-pinned — see match-management §4).
- Team-sheet kind 6 (`FUN_1408c5030`, 7-byte block k6; INF(high) "Pressuring =
  Aggressive/Conservative" per tactics §2).
  - Set by: the sheet (menu/team-edit provenance; live team 0 k6=1, team 1 k6=0).
  - Effect (OBS raw `0x140971f8e`): slide `L += 1` iff `FUN_140a62180(ctx,side,6)
    == 0` (`call; test eax,eax; jne skip; inc ebx`). No effect on tackle A.
    (DefenderDecisionsRE §3's formula already says this; confirmed raw here.)
- Sheet flag (`FUN_140a3b520` + `FUN_1414dab90`, OBS `r18a.c` + raw `0x140972086`):
  if the player's record fails availability/eligibility AND the registry flag is set,
  `L = 0` (`cmovae ebx,esi` with esi=0x18?? — raw: `cmp byte [rax],1; cmovae ebx,esi`;
  esi holds the pre-accumulation base 0, so L=0, OBS). Tackle A: same condition
  zeroes A (`uVar21 = 0`, OBS `r18a.c` line 123–127).
- Full slide accumulation (OBS raw order `0x140971f8e–0x1409720a3`): `ebx = ENFORCER?1:0`;
  `+1` if k6==Aggressive; `+2` if mode==3; per-mark-target `+1` if instruction
  `0xe` armed (`FUN_140a63950(ctx,side,0xe,markTarget)`); `+1` if `a53c==1`;
  `+1` if stamina% (`FUN_140a58550`, OBS `cur·100/max`, rip-constant 30.0 → the
  `< 30` compare) `< 30`; `+1` if `b8a2`; cap 3 (`cmp ebx,3; cmova` — note: cap
  applies BEFORE the sheet-flag zeroing and the box rule); pad-controlled → 2;
  box rule above. Reach `d < 4.0 + 0.8·L` (OBS immediates `4.0/0.8`).
  GF code replaced: `src/sim/onthepitch` slide/tackle aggression assembly
  (`tackle.json`/`Aggression` reach `1.5 + 3·clamp((Agg−60)/35)` consumers) —
  see defender-decisions §10 table; team terms map to `T+0xa298/a53c/b8a2` +
  sheet k6 + instruction 0xe + stamina% + ENFORCER.

## 4. Match management

### 4.1 Settings-byte provenance: substitution limit (`+0x8d`) and stamina cap (`settings+0x18`)
- OBS (`r14a.c`): `FUN_140521000` gate reads `*(obj+0x370)+0x8d` (subs allowed) vs
  used count; `FUN_140520de0` (sub-mode) reads pad-attachment, else
  `*(obj+0x370)+0xa268` (pad/competition override) or `*(obj+0x370)+0xa290` byte:
  0 → mode 1, 1 → mode 3, else mode 2 (`3 − (b<2)`).
- OBS (`0x141fba4c5–0x141fba4da`, menu team-setup switch): `FUN_140a424d0`
  (the `T+0xa298` setter) is called with the menu's mode value, and the next
  instruction writes the settings `+0x8d` byte: `al = 6` default, else
  `al = [r14+0x18]` (the menu competition-rule byte), stored to `[r15+0x8d]`.
  So the sub limit = competition rules (6 default, e.g. extra-time allowance).
- OBS (`r15a.c` `FUN_140523b60` head): the stamina cap reads
  `*(G+0x2c0-relative settings + 0x18)` with the minute schedule (mode 1:
  `< 25 → cap·2/3? full schedule in §4.2; mode 2: 0x22/0x18/0xe gates; mode 3:
  0x13/10 gates). Same settings object family (`lVar21 = *(G+0x2c0)`).
- INF(med): both bytes come from the pre-match competition/menu setup
  (`0x141fba380`-family switch on match type, `FUN_140a602b0`/`FUN_140a74070`
  menu readers). No in-match writer (only the commit decrement
  `FUN_143f659a0`). GF: `substitutions.cpp maxSubstitutions = 3` → settings byte.

### 4.2 Substitution capture — OPEN (no substitution occurred)
OBS (`samp1.jsonl`, 2895 non-menu rows): no queue entry, no `MI+0x1748` change
(not sampled — noted), both strategies static (team 0 `s134 = 1`, team 1 `s134 = 3`),
stamina not sampled this pass. The match went `per` 3 → 10 (extra time) with
`ph` 2 (stoppage) throughout the capture, then to the menu — case 2 needs
`MI+0x1168 ∈ {2,4}` + queue unlocked, so no substitution could fire in this window.
The sampler (`live_samp.py`) is ready; next: re-run it on a live second half
(`sudo python3 live_samp.py OUT.jsonl 600`) and watch for `a508`/strategy splits.

### 4.3 `T+0x210` bound for the strategy→level clamp — CLOSED as constant
See §1.2: no in-match writer; init 2 + T-copy only. With `t == 2` the clamp
(`match-management §1.6`) is a no-op: `want = map(T+0x134)` gated only by the
in-play rule. Live (OBS): `t210 = 2` in all 2895 non-menu rows for both teams.
Mode-3 (`+0xa290`) provenance: OBS §4.1 (`FUN_140520de0` reads the settings byte;
menu/setup origin, same switch as `+0x8d`). `FUN_140a40bc0` mode 1/2: OBS the
`T+0xa298` getter; values 1/2 are menu-set (same `0x141fba4c5` switch family),
never observed live (both teams `a298 = 0`).

## 5. GF code each finding replaces

| finding | GF code (`src/sim/onthepitch`) |
|---|---|
| §1.3 toggle branches (Style counter/keeper paths, Build-Up short gates, k6 tackle weights, `14063a420` struct fill) | `teamAIcontroller.cpp` pressure/counter branches, `aitactics.hpp` Short-pass gates, carrier-plan ctor style switch |
| §1.4 instruction effects (support-scorer permissions/vetoes, mark-pairing gates, tackle-scorer deferral) | Eliza/support positioning scorers, `defenceMark` pairing, tackle-commit scorer |
| §1.5 designated players `+0x143c` (+0x1454 takers, sub re-pointing) | no counterpart — new small designated-taker memory + kicker fallback `FUN_140a621f0`-equivalent + sub swap |
| §2.1 ReverseSideCounter (4 s start window, lane vetoes, runner pick, 3 s stages, orders `b748=4/b774=0x1e`) | counter block (`ShouldLaunchCounter`, `ApplyAttackingRun`, `SelectAttackingRunPlayer`) — add the reverse-side variant |
| §2.2 PossessionMfJoinDefenceLine (dead start; continue rule if revived) | nothing (dead code — do not port; document only) |
| §2.3 all 48 subConcept enables = 1 | `constant_team.bin → subConcept.o` import: all-enabled; GF needs no per-concept table |
| §3 slide L / tackle A team terms | slide/tackle aggression assembly + `tackle.json` consumers |
| §4.1 sub limit +0x8d (default 6), cap +0x18, mode +0xa290 | `substitutions.cpp` limit/cap/mode + `AIManager::PlanSubstitution` thresholds |
| §4.3 `T+0x210` = 2 constant in match | `matchmentality.cpp` clamp input — hard-code 2 / delete the menu feed |

## 6. Open list (exact next step each)
1. **MA+0x25cf8 writer** (§1.1): no displacement store in the image (9+9 reads only).
   Next: hardware watchpoint on the MA slot write during a per-frame capture.
2. **T+0x210 menu provenance** (§1.2): in-match behaviour closed (constant 2); only
   the UI/setup feed remains. Next: trace the `FUN_1404161e0` copy source in the
   menu path if GF's setup UI needs it — otherwise drop.
3. **Low-frequency toggle/instruction sites** (§1.3–1.4 tail): same families as the
   reduced ones; reduce on demand when GF ports that scorer.
4. **Live episodes still missing**: a ReverseSideCounter start (needs Counter team
   in play with `a508 ≠ 0`), an in-play substitution with in/out ids + minute + pct,
   the stationary-interception touch rule (TransitionsRE §9.1 — needs per-frame
   capture). Next: `sudo python3 live_samp.py OUT.jsonl 600` on a running second half.
5. **`T+0xa53c` setter** (§3): no displacement store (init/copy only, yet flips live).
   Next: perf hardware watchpoint or per-tick poll correlating with `FUN_140535650`
   inputs (score/minute/level/ball-x/`MI+0x11a0`).
6. **VFS `constant_team.bin` parse**: archived/compressed bytes (not plain JSON);
   registry-0xc4 live read already gives the enables (all 1). Next: parse only if
   GF needs the other `constant_team` objects (mark/support/defence constants).

## 7. Ledger
| ID | Claim | Conf. | Evidence |
|---|---|---|---|
| EV-TAL-0001 | MA+0x25cf8/0x25d38 have no displacement store (9+9 reads only) | OBS (image scan) | VAs §1.1; `r2a.c` readers |
| EV-TAL-0002 | T+0x210 init 2, T-copy only, no in-match writer; clamp no-op live | OBS (code + live 2895 rows) | `0x140a41810`, `0x1404166dd`, `r12a.c`; `samp1.jsonl` |
| EV-TAL-0003 | `FUN_140a64e20` = pad-buffer ctor, not a T writer | OBS | callers `0x140427db4/0x14042adb3` |
| EV-TAL-0004 | Toggle branches §1.3 (Style/Build-Up/k6/63a420 fill) | OBS (raw) | VAs §1.3 |
| EV-TAL-0005 | Instruction effects §1.4 (support/mark/tackle) | OBS (decompile) | `r16a.c`, `r17a.c`, raw §1.4 |
| EV-TAL-0006 | `+0x143c` = six designated players (k5 pen, k3/k4 corners); sub re-points; `+0x1454` takers | OBS (code + live) | `r6a.c`, `r7a.c`, `r8a.c`; `live_q.py` |
| EV-TAL-0007 | `FUN_140a62ee0` = pad-id→slot mapper, not a k decoder | OBS | `r5a.c` |
| EV-TAL-0008 | ReverseSideCounter start/continue/execute §2.1 | OBS (raw + decompile) | `r1a.c`, `r3a.c`; VAs §2.1 |
| EV-TAL-0009 | PossessionMf gate/start-stub/continue §2.2 | OBS | `0x14053d730/0x141863350/0x14053d600` |
| EV-TAL-0010 | subConcept enables all 1 (registry 0xc4 live) | OBS (live) | `live_reg4.py` |
| EV-TAL-0011 | Tackle A (§3): ENFORCER + mode3, 45/11.25/0°, timing 0.075/0.1/0.125·fps | OBS | `r18a.c` |
| EV-TAL-0012 | Slide L (§3): +1 k6-aggr, +2 mode3, +1 0xe, +1 a53c, +1 stamina<30, +1 b8a2, cap 3, box rule | OBS (raw) | `0x140971f8e–0x1409720d5` |
| EV-TAL-0013 | `T+0xa298` init 4, menu setter `0x140a424d0` ← setup switch; getter `0x140a40bc0` | OBS | VAs §3 |
| EV-TAL-0014 | Sub limit +0x8d (menu default 6 / rule byte), cap +0x18, mode +0xa290 readers | OBS | `r14a.c`, `r15a.c`, `0x141fba4c5` |
| EV-TAL-0015 | No substitution in 2895 non-menu rows (stoppage phase, static strategies) | OBS (live) | `samp1.jsonl` |
