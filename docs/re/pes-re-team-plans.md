# PES 2021 team plans, off-ball runs, pressing and team shape — evidence report

Scope: the *team-level* and *off-ball* AI. This report fills `pes-re-player-ai.md`
§3 (off-ball run choosers) and §4 (team plans, defensive positioning and pressing),
and adds the attribute/style inputs to the ball-carrier think units of §2.2.

Image: `<scratch>/pesmem.bin` (mid-match memory image, base 0x140000000, so
`VA = 0x140000000 + file_offset`; the image is 64,045,056 B, i.e. code through
`.trace` 0x140001000–0x14252e800 plus `.rdata`, `.data`, `.pdata`).
Decompile: Ghidra MCP on 127.0.0.1:8080 (`pesmem.bin` project). Raw re-check:
capstone linear disasm of each quoted function, with every rip-relative literal
resolved to its value in the image (script `<scratch>/TeamPlansRE/dz.py`).
RTTI: `~/ghidra-projects/scratch/FoxRegistryRE/rtti.py cls '<regex>'` over
`rtti_all.json` (13,816 COLs).

Tags: **OBS** = raw bytes + decompile at a stated address. **INF** = inference with a
confidence. Live process read-only via `/proc/pid/mem` (sudo, never written);
`pesmem.bin` itself was captured outside a match (its `gMatchContext` is 0).

## 0. Status summary

| item | status |
|---|---|
| class inventory for team AI | **closed** (OBS, RTTI) |
| SubConcept vtable layout + call chain | **closed** (OBS) |
| base-position target-position formula (`FUN_140688300`) | **closed** (OBS) |
| advanced-instruction + style enums | **closed** (OBS, strings) |
| off-ball run selector gates (Chance/Counter/SecondLine/Diagonal/PullAway/PostPlay) | **closed** (OBS) |
| fore-check press trigger + LineUp/TransitionChase/Retreat gates | **closed** (OBS) |
| `ActionPress` target selection + urgency/stamina | **closed** (OBS) |
| `ZoneDefenceManager` mark states/assignment/cap/compaction | **closed** (OBS) |
| press-count test (`FUN_140574200`) | **closed** (OBS) |
| PassForward / Shoot / Dribble scoring skeletons | **partial** (OBS arithmetic, INF meanings) |
| run scorers, scale-triple writer, style consumers, GK, VFS tables | open (see §6) |

---

## 1. The team brain class inventory (OBS)

Resolved with the RTTI resolver. `match::ai` holds 47 `SubConcept*`, 15
`ActionSelector*`, `Team`, `TeamMemberChange`, `ZoneDefenceManager`,
`PlayerAIBase` → `PlayerOffence` / `PlayerDefence` / `PlayerGKAuto`, `Judge`,
`Avoid`/`AvoidArea`/`AvoidPlayer`, and the `bp::` ball-carrier brain.

`match::ai::SubConcept` vtable `0x1425c3cc0` (7 slots, OBS):

| slot | offset | VA | note |
|---|---|---|---|
| 0 | +0x00 | 0x1404f0d80 | deleting destructor |
| 1 | +0x08 | 0x1415a171a | `ret` (pure-ish) |
| 2 | +0x10 | 0x1415a171a | `ret` |
| 3 | +0x18 | 0x1415a171a | `ret` |
| 4 | +0x20 | 0x141794410 | `ret` |
| 5 | +0x28 | 0x141794410 | `ret` |
| 6 | +0x30 | 0x141863350 | `xor al,al; ret` (false) |

Every concrete subclass overrides slots 0,1,2,3,5,6 (some leave one as `ret`).
Comparing two concepts makes the roles clear (OBS):

- `SubConceptFastCounter` 0x140543f40 (slot 2), `SideCounter` 0x140543f40,
  `ReverseSideCounter` 0x140543f40 — three concepts share slot 2.
- `SubConceptPossessionMfJoinDefenceLine`: slot 2 = 0x14053d730, slot 3 = 0x140ca1a50
  (`mov al,1; ret` → always-true gate), slot 5 = 0x14053d600, slot 6 = 0x14053d5f0.
- `SubConceptForeCheckPress` / `…LineDown` / `…LineUp` / `TransitionChase` /
  `…RetreatBlockCreate` / `…RetreatPressBack` / `…RetreatBlockForwardPass` /
  `…RetreatCrossBlockCreate` share **slot 2 = 0x140574910** and
  **slot 1 = 0x140574910's family `0x1405747f0/…4ea0/…5300/…5510/…55ea0-side`**,
  i.e. one shared "fore-check / retreat" evaluator with a per-concept scoring
  function. That family is the block in `0x140574700..0x140579700`.

`match::ai::ActionSelector` vtable `0x1425cd358` (4 slots) with
slot 0 = 0x140600d80 the destructor. The 15 selectors are (COL address, vtable):

`ActionSelectorCenteringGet` 0x142c9b568/0x1425cde88 (4),
`…ChanceSpaceRun` 0x142c9aea0/0x1425c61b0 (7),
`…CounterSpaceRun` 0x142c9af28/0x1425c61f0 (7),
`…DiagonalRun` 0x142c9b4e0/0x1425cdd18 (7),
`…GoalGet` 0x142c9b1c8/0x1425c6318 (4),
`…LineBreak` 0x142c9b038/0x1425c6270 (7),
`…Manager` 0x142c9b2d0/0x1425c6380 (1),
`…Overlap` 0x142c9ad50/0x1425c6148 (4),
`…OverlapCB` 0x142c9b148/0x1425c62f0 (4),
`…PostPlay` 0x142c9b248/0x1425c6340 (7),
`…PullAway` 0x142c9b0c0/0x1425c62b0 (7),
`…Score` 0x142c9ae20/0x1425c6170 (7),
`…SecondLineSpaceRun` 0x142c9afb0/0x1425c6230 (7).

Note the 7-slot selectors (Chance/Counter/SecondLine space runs, DiagonalRun,
LineBreak, PostPlay, PullAway, Score) mirror the `SubConcept` 7-slot shape, i.e.
they carry both an evaluate and a gate; the 4-slot ones are simpler.

**Callers, OBS** (from a direct call graph built over the team-AI code range
0x1404F0000–0x140700000, 7,714 functions; edges verified in raw disasm):
- `FUN_140688300` (below) is called from `FUN_140681ff0` and `FUN_140687e7c`
  (both inside the fore-check block `0x140574700..0x140579700`).
- `FUN_140681ff0` in turn is called from `FUN_1405ec600`, whose caller is
  `FUN_14051dd10` → `FUN_14051dc78` → `FUN_14051de29`. So the chain is
  **`team brain tick (0x14051dc78) → 0x1405ec600 → fore-check block → 0x140687e7c
  → 0x140688300`**.

---

## 2. Team shape: from formation slot to target position (OBS)

`FUN_140688300(ctx /*rcx*/, out /*rdx/r14*/, phase /*r8d/r15d*/)` writes two
floats into the caller's buffer — the **ball-relative X shift** of the base
position. Signature facts (raw):

- `r13 = ctx[0]` — a struct holding the *formation slot base position*.
  `r13+0x04` is read and multiplied by literals (see below) → **the slot's own
  base X (depth), in metres**. `r13+0x0C` = a half-length-like value,
  `r13+0x14` = a small X offset, `r13+0x18` = a lateral/length value.
- `rsi = ctx[0x28]` — the **match-info block** (same base as the report's
  `gMatchContext + 0x270`): `rsi+0x116c` = the tick/phase word (1 = in-play),
  `rsi+0x1554` = `SituationData.MODE`, `rsi+0x1558` = the restart side.
- `rbx = ctx`; the sub-structs are:
  - `ctx[0x10]` = the shared match env (`rbx+0x10` is passed to 0x141e532d0
    via `ctx[0x10]+0xb8a4` = a live **float clock in ms**);
  - `ctx[0x20]` = `fVar22`, a per-slot float array base (`ctx[0x24]+0x30/0x34`
    are read further down — these look like **pitch length / half length
    bounds**);
  - `ctx[0xf0]` = a struct with these OBS-read fields:
    `+0x160`, `+0x164`, `+0x168`, `+0x16c`, `+0x28`, `+0x218`, `+0x284`,
    `+0x288`, `+0x28c`, `+0x34`, `+0x30`. This is **the per-team "base position
    tuning" record** (`basePosition.o`), indexed by `dil` = a role byte.
  - `ctx[0xf8]`, `ctx[0x118]`, `ctx[0x1e]`, `ctx[0x1f]`, `ctx[0x22]`,
    `ctx[0x23]`, `ctx[0x24]`, `ctx[0x120]` = further sub-structs.

### 2.1 The two writes (raw, `0x1406884a7` and `0x14068856a`/`0x140688b1e`)

`FUN_140688300` is entered with `r15d` = a **phase**. The disassembly shows
three exclusive branches:

**(a) phase ≥ 3 (`jbe 0x140688581` is *not* taken, `cmp r15d,2; jne 0x140688b12`
is taken), i.e. `phase > 2`.** Both writes are skipped — no base-position change.
OBS.

**(b) phase == 2 (`0x140688440`).** This is a *defensive-phase* special case:
```
if (*(char*)(ctx[0xf0] + 0x108) != 0) {            // flag: use pitch-width bounds
    halfW = *(float*)(ctx[0x24] + 0x34) - *(float*)(ctx[0x24] + 0x30);   // 0.5*width
    ...
    x = r13->baseX * 0.55;                          // 0x1425a358c = 0.55
    ...
}
```
Raw at `0x14068844a`:
```
mov rax,[rbx+0xf0]; cmp byte[rax+0x108],0 ; je 0x1406884c2
mov rax,[rbx+0x120]
movss xmm3,[r13+4]          ; slot base X
mulss xmm3,[0x1425a358c]    ; * 0.55
movss xmm0,[rax+0x30]; subss xmm0,[rax+0x34]     ; width delta
mulss xmm6,[0x14253ee30]    ; * 100.0   (= smooth factor *100)
...
xmm3 = (baseX*0.55 - |width delta|) * (smooth*100)/100 + |width delta|   [A]
```
and, when the flag is 0, the symmetric width-bound branch starting `0x1406884c2`
uses `xmm7 = (slotBaseX*0.7) - (roleIndex-1) * (basePosition+0x284)` with
`smooth*100`, then clamps `xmm7` to `min(xmm7, halfW*0.5)` where
`halfW*0.5` is `0x142565600 = 0.5` scaling; the two writes become
`out[0x130] = -xmm1` and `out[0x134] = xmm2 + xmm1`.

The repeated `(... * 100.0) / 100.0` idiom is a compiler artefact of a
*time-smoothing* helper `FUN_14063f230()` (called at the top of the function, its
float result held in `xmm9`) whose body divides by a live ms clock — the whole
pattern is a **linear low-pass filter**
```
newValue = target + (current - target) * smooth
```
with `smooth` clamped to `[0, 1]` and computed from a **frame-rate-corrected
time constant**. OBS for the shape of the arithmetic, INF (med) for the exact
meaning of every operand.

**(c) phase ≤ 1 (the default path, `0x140688581` onwards) — the real formula.**
Raw, in order:

1. `fVar13 = (float) FUN_14063f230()` — the smoothing factor, `xmm9`.
2. `xmm8 = (float) *(int*)(ctx[0x10] + 0xb8a4)` — the **match clock in ms**
   (`cvtdq2ps`), then a piecewise map:
   ```
   if (0 >= clock)                 t = 1.0
   else if (clock >= 2.0)          t = 0.0
   else                            t = (clock - 2.0) * (-0.5)
   ```
   (raw `0x14068839f..0x1406883cd`; literals 0x14256ae1c=1.0,
   0x142565604=2.0, 0x14258d694=−0.5) — INF (med-high): a **match-start ramp**
   from full formation shape (1.0) to none (0.0) over the first two seconds.
3. `xmm6 = |*(float*)(rbx+0x104)|`, then clamped/remapped through the same
   `[min, max]` pattern:
   ```
   if (6.0 >= x)  -> 0.0
   else if (x >= lm) -> 1.0     where lm = r13[0x1c] - 5.0
   else x = (x - 6.0) / (lm - 6.0)
   ```
   (raw `0x1406883cd..0x140688425`; literals 0x14259bdec=6.0, 0x14259222c=5.0).
   **[OBS]** this is a **ball-relative shift blend**, i.e. `x` = the ball's
   distance along the pitch and the output is the 0..1 "how far up the pitch the
   team should move" weight. INF (high) on `x` being ball distance, because the
   same idiom reappears at `0x140688750` with `r13[0x18]`.
4. The base depth (raw `0x140688595..0x140688615`; this is the part that picks
   the *role*):
   ```
   if (isRestart(ctx[0xf8], 0))                       // FUN_140a533a0
       xmm7 = basePosition[0x16c];                    // set-piece base depth
   else if (mode == 6 && side != teamId)
       xmm7 = r13[0x14] + xmm13;                      // defensive-third offset
   else switch (role = dil):
       role >= 5 : xmm7 = basePosition[0x168];        // forwards
       role == 3: xmm7 = basePosition[0x160];         // centre-back
       role >= 2: xmm7 = basePosition[0x164];         // full-backs
       else      : xmm7 = 10.0;                       // 0x142588210 = 10.0
       xmm7 += xmm9;                                  // + smoothing
   ```
   `dil` is the byte loaded at the top: `movzx edi, byte [r12 + rdx + 0x58]`
   with `r12 = slotIndex * 0x68`, i.e. **`byte[teamBase + slot*0x68 + 0x58]` is the
   role/linearity index for the slot** (OBS the load; INF med on the exact enum,
   it is clearly 0..5+ with 2,3 picking FB/CB and ≥5 picking forwards).
   The constants `basePosition+0x160/0x164/0x168/0x16c` are the **per-line base
   depths** read from `constant_team.bin` `basePosition.o`. The literal `10.0`
   is the fallback for an unclassified role.
5. Then (raw `0x140688615`), a mode-dependent subtraction:
   ```
   if (inPlay || mode != 3)  xmm7 -= xmm8 * 6.0      // xmm8 = the start ramp t
   else {
       xmm7 -= basePosition[0x28];
       if (slot == 1 && role == 3) xmm7 += (-2.0);   // 0x14259a768
   }
   ```
   **[OBS]** — `basePosition+0x28` is a **set-piece depth offset**, and the
   second path is the **free-kick special case for slot 1 (the CB) with role 3**.
6. Then the **compactness / line-height step** (raw `0x14068866a`):
   ```
   if (inPlay || mode != 3) && !isRestart(...):
       w = lerp(basePosition[0x284], basePosition[0x28c], 100.0-clamped)
   else
       w = basePosition[0x288]
   step = (role - 1) * w * 0.5
   if (step <= target && (target <= slotBaseX * 0.85)) step = target
   ```
   (raw `0x14068866a..0x1406886f6`; literals 0x142565600=0.5,
   0x1425ab120=0.85). So `basePosition+0x284/0x288/0x28c` is a **3-value
   per-role step between adjacent lines** and `0.85 × slotBaseX` is a hard
   upper clamp. **[OBS]**
7. Then an **Offensive-Awareness dependent push** (raw `0x14068878a`):
   ```
   OA = (float)(signed char) byte[ctx[0x20] + teamIdx*0x14 + 0x40]   // see note
   baseAwarenessX = OA * float[ctx[0x20] + teamIdx*0x14 + 0x40]  ... (see §5.1)
   ```
   Raw is:
   ```
   mov rax,[rbx+0x20]
   lea rdx,[rax+rax*4]                 ; teamIdx*5
   movsx eax, byte[ctx[0x10]+0x254]    ; a live star-rating / direction byte
   cvtdq2ps; mulss with float[rcx+rdx*4+0x40]
   ```
   i.e. `awarenessScale = byte[matchEnv+0x254] * float[ctx[0x20] + teamIdx*20 + 0x40]`.
   **[OBS the arithmetic]** — INF (med-high): `byte[matchEnv+0x254]` is the
   **direction of attack ±1** (`teamData+596` in `pes-re-matchlogic.md` §2.4)
   and the float is a **per-team Offensive-Awareness-derived forward push**
   (Aoba's `adjustXFromAttackAwareness3/4`, quoted in that report §2.5).
8. Then a **distance-driven extra push** (raw `0x1406887b8..0x14068886e`),
   gated on `FUN_141b92460(2) >= 2` (a **2-second timer**; `0x141b92460` is the
   shared "seconds since event" helper — INF med) and on
   `inPlay || mode == 2 || mode == 5`:
   ```
   d = |awarenessScale - rampStart|      (raw: cvtps2pd + abs mask)
   if (7.0  < d)   k = clamp((d - 20.0) / (-13.0), 0, 1)
   else              k = 1.0
   if (diff < gameSpeed * 7.0 / 60 + 0.5)   // a ~7-frame window
       if (12.0 < d)  k = clamp((d - 24.0) / (-12.0), 0, 1)
       else              k = 1.0
   k *= basePosition[0x218]
   if (awarenessScale > rampStart) k = basePosition[0x218]   (full)
   if (mode == 5) k = max(0.4, k)                            // 0x14259b89c
   ```
   **[OBS]** — the raw is a three-segment piecewise weight over the distance
   between the team's target and its current base position, with a
   `basePosition+0x218` scale, clamped to 1 and floored at 0.4 in mode 5.
   INF (med): this is the **"line steps up when the ball is far from the
   defensive line"** term, with 0.4 = a minimum in the `mode == 5` situation
   (INF: counter-attack).
9. Then a **GK-penalty carve-out** (raw `0x1406888b8`):
   ```
   if (slot == 0 && byte[ctx[0x118] + 0x58] < 4)  { k = 1.0; ramp = 5.0 }
   ```
   (literal 0x14259222c = 5.0). INF (med-high): slot 0 is the goalkeeper, and
   the byte is the in-match registered-position clamp index (0..12 per
   `pes-re-matchlogic.md` §1.3), so "the keeper may advance while in a
   goalkeeper-ish position".
10. Final smoothing (raw `0x1406889a5..0x1406889e1`):
    ```
    step = (step - target) * pushWeight * (100 - ramp*100)/100 + target
    ```
    and the two outputs at `0x140688b1e`:
    ```
    out[0x60] = value;                 // the smoothed X
    out[0x64] = value + value;         // INF: the *mirror/centre* term
    ```
    followed at `0x140688b12` by a **pitch-length clamp** on both:
    ```
    lim = r13->halfLen - 0.5
    if (lim < value) out[0x60] = lim * (value >= 0 ? 1 : -1)
    if (lim < mirror) out[0x64] = lim * (mirror >= 0 ? 1 : -1)
    ```
    (raw `0x140688b12..0x140688b64`; literals 0x142588218 = −1.0,
    0x142565600 = 0.5, 0x14258820c = 2.5625 unused here).
    **[OBS]** — so a player is never pushed outside the pitch half-length minus
    0.5 m, and the sign is preserved.

11. Finally (raw `0x1406889e5`), one more blend with a
    `basePosition+0x…`-derived value through `FUN_140a63950(ctx[0xf8], teamIdx,
    0xb /* 11 = the slot count */, 0xff)` and the `0.55`-class scale, clamped to
    not exceed the (already smoothed) base — INF (med): the **support / second
    wave** read of the same record.

### 2.2 The advanced-instruction enums (OBS)

Two enum string tables in `.rdata`, both complete and alphabetically adjacent
(raw addresses, 0x10 bytes apart except where noted):

**COM styles / playing styles (0x142614180..0x1426142F0)** — 21 values:
`GOAL_POACHER`, `DUMMY_RUNNER`, `FOX_IN_THE_BOX`, `POST_PLAYER`, `CHANCE_MAKER`,
`WING_STRIKER`, `INSIDE_PLAYER`, `CROSSER`, `CLASSIC_NO_10`, `FREE_ROAMING`,
`BOX_TO_BOX`, `ANCHOR_MAN`, `ENFORCER`, `PLAY_MAKER`, `BUILD_UP`, `OVERLAP`,
`OFFENSIVE_SB`, `DEFENSIVE_SB`, `INNERLAP`, `OFFENSIVE_GK`, `CLASSICAL_GK`.
(Preceded by the 11 skill names `TRICK_STAR`, `DARTING_RUN`, `MAZING_RUN`,
`EARLY_CROSS`, `INCISIVE_RUN`, `LONG_RANGE`, `LONG_PASS`, `THROUGH_PASS`,
`MIDDLE_SHOT`, `EDGE_TURN`, `DOUBLE_TOUCH`, `INTERCEPT`, `DROP_SHOT`,
`RISING_SHOT`, `HIGH_PUNT_KICK`.)

**Advanced instructions, defensive block (0x142614348..0x142614448)**:
`OPEN_WIDE`, `FALSE_9`, `FALSE_SB`, `BOTH_SB_UP`, `FLOW_SIDE`, `TIKI_TAKA`,
`TARGET_CROSS`, `DEFENSIVE`, `FALSE_WG`, `KEEP_LANE`, `CAUTION_CROSS`,
`CAUTION_THROUGH_PASS`, `GEGEN_PRESS`, `MARK_HARD`, `READY_COUNTER`,
`WING_BACK`, `DEFENCE_MAX`.

The engine names in the image (not enum order) confirm the defensive half:
`FORE_CHECK_PRESS` (0x1425db0b0 / 0x14264fac0), `RETREAT_PRESS_BACK`
(0x1425db128), `NET_DEFENCE_CENTRAL_SURROUNDING_PRESS` (0x1425db200),
`SEAL_OFF_SIDE_SURROUNDING_PRESS` (0x1425db288), `1ST_DF_DOUBLE_PRESS`
(0x1425db2c8), `KICK_OFF_PRESS` (0x14264fc58),
`DEFENCE_START=FORE_CHECK_PRESS` (0x14264fca8), and the defensive advanced
instructions above. So **the defensive advanced instructions map 1:1 to
`match::ai::SubConcept*` classes**: `FORE_CHECK_PRESS`→`SubConceptForeCheckPress`,
`RETREAT_PRESS_BACK`→`SubConceptRetreatPressBack`,
`NET_DEFENCE_*`→`SubConceptNetDefenceCentralSurroundingPress`,
`SEAL_OFF_*`→`SubConceptSealOffSideSurroundingPress`,
`1ST_DF_DOUBLE_PRESS`→`SubConceptDoublePress`. **[OBS]**
The UI names (`gmPlanAnimeTactics_defGegenPress`, `…_defMarkHard`,
`…_defReadyCounter`, `…_attTikiTaka`, `…_attFalseNine`, `…_attBothSBUp`,
`…_attFalseSB`, `…_attFlowSide`, `…_attEarlyCross`, `…_attOpenPosition`,
`…_defCautionCross`, `…_defWingBack`, `…_defSaveSlide`) match the same enum,
which pins `GEGEN_PRESS`, `MARK_HARD`, `READY_COUNTER`, `WING_BACK` as the four
defensive instructions and `OPEN_WIDE`, `FALSE_9`, `FALSE_SB`, `BOTH_SB_UP`,
`FLOW_SIDE`, `TIKI_TAKA`, `TARGET_CROSS`, `EARLY_CROSS`, `CAUTION_CROSS`,
`CAUTION_THROUGH_PASS` as the attacking ones.

</content></invoke>
---

## 3. Off-ball run choosers (OBS, partial)

### 3.1 The selector family and its shared gate pattern

Each `ActionSelector*` subclass overrides slots 0, 3, 4, 5, 6 (and sometimes 7) of
the base. OBS, the concrete slot-4 VAs (the "can this player start this run now"
gate) and slot-5/6 (the "scorer / executor"):

| selector | slot 4 (gate) | slot 5 | slot 6 | slot 7 |
|---|---|---|---|---|
| `ChanceSpaceRun` | 0x140607a60 | 0x140606cd0 | 0x140606060 | 0x140602600 |
| `CounterSpaceRun` | 0x14060b410 | 0x14060ab00 | 0x14060a8d0 | — (`0x140525150` = shared) |
| `SecondLineSpaceRun` | 0x1406118e0 | 0x14060ebf0 | 0x14060d210 | 0x14060c390 |
| `LineBreak` | 0x140612830 | `0x140421f40` (stub) | 0x1406127a0 | — |
| `PullAway` | 0x14061e1e0 | `0x140421f40` (stub) | 0x14061dee0 | — |
| `DiagonalRun` | 0x140615090 | 0x140614290 | 0x140613910 | 0x140612cf0 |
| `PostPlay` | 0x140620900 | 0x140620590 | 0x14061f940 | — |
| `Score` | shared 0x140602060 | shared+ | 0x140601ce0 | 0x1408b5d10 |
| `Overlap` | 0x140601580 | — | 0x1406013d0 | — |
| `OverlapCB` / `GoalGet` / `CenteringGet` | 4-slot shape (destructor + 3) | — | — | — |

**Shared gate idiom (OBS, all slot-4 decompiles).** Every gate is:
```
ready = FUN_14050f560(playerBlock)            // "is this player AI-controlled and free?"
if (!ready) return <same-side default>
if (carrierIdx == myIdx) {
    if (!FUN_140a6e340(sbyte[player+1])) return true;   // ...
}
if (actionState == 5 && FUN_140a51320(ctx) > 1) return true;
slot = myIdx % 11 (or 0 if myIdx >= 0x16)     // formation-slot index
entry = FUN_140a401c0(slot*0x34 + ctxBase + 0xa2cc, 0)  // per-slot run timer block
if (entry[8] - entry[4] < gameSpeed*0.2 + 0.5) return false;  // cooldown
return FUN_140688c00(ctx, myIdx, flagId, arg);   // flagId 0x1d/0x1e/0x1f
```
Raw-verified in `FUN_140607a60` (Chance, flag `0x1e`, cooldown
`gameSpeed * 0.2 + 0.5`, i.e. **≈ 11 frames at speed 54**),
`FUN_14060b410` (Counter, flag `0x1f`, inverted: returns false while the
cooldown runs, then needs `ballX < direction * candidateBaseX`),
`FUN_1406118e0` (SecondLine, flag `0x1d`, cooldown `gameSpeed` raw = **54
frames = 1 game-second**).
The per-slot timer block is `0x34` bytes per slot at `ctx+0xa2cc`, with
`+0x04` = last-trigger frame and `+0x08` = current frame. **[OBS]**

`FUN_140688c00(ctx, idx, flag, arg)` itself (raw `0x140688c00`, partially
decompiled): returns 1 immediately when `phase != 1 && phase2 != 2`; requires
`byte[team+0x255]` (the off-ball "has a run assignment" flag); requires
`carrier = FUN_140a627e0(ctx,0)` to differ from `idx` or the carrier's
`actionState (+0x5d0)` to be 5; then walks an 11-slot side table. INF (med-high):
this is the **"is there a free run lane for slot idx"** test, with `flag`
`0x1d/0x1e/0x1f` = the **SecondLine/Chance/Counter run-lane bitmask** in the
team bitmask built by `0x140a40bd0`. **[OBS the code, INF the lane meaning.]**

### 3.2 Per-selector notes (OBS)

- **`DiagonalRun` gate `0x140615090`** adds: `phase == 1`, `byte[team+0x255]`
  set, `param_2[0x1c] != idx` (not the designated player), the same
  `gameSpeed*0.2+0.5` cooldown, and then `FUN_140695800(ctx, idx)`.
- **`PullAway` gate `0x14061e1e0`** inverts the control test (runs when
  `FUN_14050f560` says *not* free), compares
  `direction * candidateBaseX` against `*(gCtx+0x2aa8)+0x20` (a pitch-bound float),
  and uses a cooldown of **4× the frame counter** (`(iVar5<<2) < delta`).
- **`PostPlay` executor `0x14061f940`** sets `byte[out + slot + 0xe5]` to
  **0, 1 or 2** (`0` = no post play, `1` = `FUN_140620ca0` matched,
  `2` = `FUN_1406210e0` matched) and calls `FUN_14061f8b0` (lane 1) or
  `FUN_14061fa40` (lane 2). So post-play has **two explicit lanes**. **[OBS]**
- **`PostPlay` lane-1 test `0x140620ca0`**: needs
  `FUN_140a50250(ctx, teamIdx, 0) >= 1` (a count of eligible runners),
  `carrier = FUN_140a627e0(ctx,0) <= 0x15`, the slot's `actionState
  (+0xa2a0+slot*4)` to be a real state and `FUN_140a40050(state)` nonzero,
  then `direction * runnerBaseX` inside `[goalX - 10.0, goalX]`
  (literals `0x142588210 = 10.0`, goal floats from `*(gCtx+0x2aa8)+0x18/0x1c`).
  **[OBS]**
- **`PostPlay` lane-2 test `0x1406210e0`**: needs the same runner count with
  mode 2, then `direction * runnerBaseX <= 5.0` (`0x14259222c`) and
  `>= -25.0` (`0x1425c48f8`), `|runnerBaseZ| >= 12.0` (`0x14259be04`), a
  passing-lane test `FUN_14043a0a0(...) <= 75.0` (`0x1425aa1bc`), and the
  slot's lane bit `0x2b`. **[OBS]** — so lane 2 is the **far-side / long
  post-play** (wide, behind the line, lane open within 75°/m — INF med on the
  unit of 75).

### 3.3 Attribute/style weighting in runs — open

The scorer bodies (`0x140606060`, `0x140606cd0`, `0x14060ab00`, `0x14060d210`,
`0x140613910`, `0x14061dee0`, `0x14061e810`, `0x14061a640`) are not yet
decompiled. The gates above establish *when* a run may start; the *choice
between* runners (and any Offensive Awareness / style weighting) lives in the
scorers. The `Overlap` family (`0x140601580`, `0x1406013d0`) is small and is
the recommended next target, because `overlap.o`'s fields are the ones
`pes-re-matchlogic.md` §2.5 already quotes (`addCount`, `adverse`,
`checkMiddleLineX`, `checkTopLineX`).

---

## 4. Pressing and marking (OBS)

### 4.1 Fore-check press trigger — `FUN_1405747f0` (OBS, full raw check)

This is **slot 3 of `SubConceptForeCheckPress`** (and shared by LineDown/LineUp,
TransitionChase, RetreatBlockCreate/… — all carry `0x140574910` in slot 2 and
`0x1405747f0` in slot 3). It returns true = "start/continue the team press".
Raw `0x1405747f0..0x140574940`, every literal resolved:
```
if (*(gCtx+0x270)+0x116c != 1) return false;          // in-play only
if (*(gCtx+0x270)+0x11a0 > gameSpeed * 5.0 + 0.5) return false;   // [P1]
carrier = FUN_140a627e0(ctx, 0);
r = FUN_140a48a30(gCtx, teamOf(carrier));             // carrier's team env
push = (float)(signed char)byte[matchEnv+0x254] * *(float*)(r + 0x2482c);
if (0.0 <= push) {                                    // [P2]
    if (push < 12.0 &&                                // 0x14259be04
        FUN_140a4c720(carrierBaseX, *(float*)(ctx + (teamIdx+3)*0x14), dir, runnerX)) {
        return false;                                 // [P3]
    }
    if (!FUN_140a63950(ctx, teamIdx, 0xd, 0xff)) return false;   // [P4] needs the
    return FUN_140574200(ctx, 0) != 0;                 // [P5] the per-player press
}
return false;
```
- **[P1] press window = `gameSpeed × 5.0 + 0.5` frames** (≈ 270 frames ≈ 5 s at
  speed 54). After that the fore-check lapses unless re-armed. **[OBS]**
- **[P2]** the same `direction × baseX` push term as §2. Only a *negative* push
  (i.e. the carrier is being driven backwards — INF med-high) keeps the press
  alive; otherwise the gate falls through to [P3]. **[OBS the arithmetic]**
- **[P3]** `FUN_140a4c720(a, b, dir, c)` is exactly
  `(dir*c + a) * dir < dir*b` (decompiled, 8 lines). With `b` =
  `*(float*)(ctx + (teamIdx+3)*0x14)` — a **per-team 0x14-stride float** — this
  is a "carrier still inside the press corridor" test with a **12.0 m**
  ceiling. If the carrier is inside the corridor the press is *denied*
  (returns false) — INF (med): inside = "already contained, drop back".
- **[P4]** `FUN_140a63950(ctx, team, 0xd, 0xff)` scans 4 sub-slots for a stored
  `(0xd, 0xff)` pair at `ctx+0x25cf8/0x25d38`. This is the **"team plan slot is
  armed for fore-check"** test (same helper as the selector flags). **[OBS]**
- **[P5]** `FUN_140574200` is the press-count test (closed, §4.5).

The sibling gates, for comparison (OBS, decompiled):
- **`SubConceptForeCheckLineUp` slot 3 = `0x1405752a0`**: in-play, and
  `byte[carrierTeam+0xb70a] == 0`, then `FUN_140575210(ctx)` true and
  `FUN_140574f40(ctx)` *false*. So LineUp = "push the line up when the carrier
  is *not* in the 0xb70a state and the 0x574f40 containment test fails".
- **`SubConceptTransitionChase` slot 3 = `0x140575410`**: in-play; takes
  `u = FUN_140a627e0(ctx,0)` (carrier), and if `u > 0x15` replaces it with the
  player at `byte[team+0x78]` (clamped to `< 0x16`); requires
  `u == *(ctx+0x20)` (the designated chaser) or `u > 0x15`, and
  `FUN_140575390(ctx, team) == *(ctx+0x1c)`; then reads the chaser's
  `actionState (+0x5d0)` and only proceeds for **1, 0x32, 0x36, 0x3d**.
  **[OBS]** — so the transition-chase has a **designated-chaser slot**
  (`ctx+0x20`/`+0x1c`) and only fires from four action states.
- **`SubConceptRetreatBlockCreate` slot 3 = `0x140575db0`**: in-play; reads
  `dir = sbyte[team+0x254]`, `a = dir*float[team+0x89a0]`,
  `b = dir*float[team+0x89a4]`; fires (returns 0 = "take this branch") when
  `|a - float[team+0x875c]| >= 5.0` or `|b - float[team+0x8770]| >= 5.0`
  **and** `a >= goalX - 8.0` or `b >= goalX2 - 18.0`
  (literals `0x14259222c = 5.0`, `0x14259bdf4 = 8.0`, `0x14259be18 = 18.0`,
  goal floats from `*(gCtx+0x2aa8)+0x18`). **[OBS]** — the retreat trigger is
  a **5 m deviation** of two team-shape floats from their anchors, armed only
  inside 8/18 m of the goal.

### 4.2 Who presses: `ActionPress` target selection — `FUN_140639100` (OBS)

Called from `ActionPress` slot 4 (`0x140962440`). It writes the presser's
**target point** (3 floats) into the action. Raw-checked flow:
```
dst = playerBase + 0x1414 (3 floats)                      // default: own base
if (carrierWithinTeam) { ... corridor test [P3 above] ... }
if (carrierIdx <= 0x15) {
    d = |ball - playerBaseXZ|                             // metres
    if (d <= 4.0)            w = 0.0                      // 0x1425656c0
    else if (d >= 8.0)       w = 1.0                      // 0x14259bdf4
    else                     w = (d - 4.0) * 0.25          // 0x142592228
    w = w * 0.3 + 0.3                                    // 0x142590d98 ×2
    if (actionState == 4) w += 0.2                        // 0x142598578
    ang = ballAngle...
    if (staminaGate && staminaRatio > 0.4*u16[0x1c]/u16[0x1a])
        ang = FUN_140638fb0(...)                          // tired: wider angle
    dst = ballPos
    frames = gameSpeed * w + 0.5
    FUN_140a8f7c0(dst, &ang, dist(gameSpeed)/gameSpeed * frames)
} else if (flag) {
    // nearest-of-two: |ball-carrierAnchor| vs |ball-ownAnchor|
    dst = nearer anchor (0x2482c vs 0x1414), sampled gameSpeed*0.1+0.5 frames ahead
}
```
- **[OBS]** the press urgency weight `w` is a **4–8 m ramp** (`(d−4)×0.25`,
  floored 0.3, ceiled 0.6, **+0.2 when the presser's actionState is 4**),
  converted to frames as `gameSpeed × w + 0.5` (≈ 16–32 frames at speed 54).
- **[OBS]** the stamina gate reads `u16[player+0x1c]` vs `u16[player+0x1a]`
  with a **0.4 factor** (`0x14259b89c`) — a tired presser (ratio below 40 %)
  takes the wider angle from `0x140638fb0`.
- **[OBS]** the else-branch picks the nearer of the carrier anchor
  (`+0x2482c`) and the presser's own anchor (`+0x1414`), i.e. **press = chase
  the carrier anchor, cover = hold own anchor, whichever is nearer the
  predicted ball**.

### 4.3 Marking: `ZoneDefenceManager` (OBS)

`FUN_1405ce4f0` is the per-tick zone/mark update, called from the team tick
`FUN_140681ff0` right after the context builder `FUN_1405cc070`. Structure
(full decompile, 215 lines, raw-checked entry/exit):
- **Marking cooldown (OBS).** The first block counts slots whose state
  `!= 3`, then: if `phase == 0` → freeze marking (`+0x1b0 = 0`); if
  `phase == 2` → freeze; if `phase == 3 && timer(+0x11a4) < frameCounter*3`
  → freeze; if `byte(+0x11b0) < 0x15` → freeze. So marking only updates when
  `phase == 1` (in-play), the mode-2/3 carve-outs above, and **11 players
  registered** (`0x15 = 21` total, `0xb` per side). **[OBS]**
- **Three mark states (OBS).** Every one of the 11 slots carries a u32 state
  at `mgr+0x08+slot*8` with values **1, 2, 3** (reset to 3 by `0x14051c860`):
  the two rewrite loops set `2` when `teamFlag == slotTeam && count > 1 &&
  (ballCarrierSlot == slot || prevState == 2)`, else restore `prevState` or 3;
  `FUN_1405ce150` (below) writes them. INF (med-high): **1 = zonal, 2 = man,
  3 = unassigned/free**.
- **Assignment core `FUN_1405ce150` (OBS, 185 lines).** For each of the 11
  slots with a live player (`*(ctx+0x1748+teamSlot)` set and
  `countdown(+0x3fb8+slot*4) != 0`): if the slot is the ball-carrier slot or
  the mode-4 carve-out matches → state 2; else if
  `FUN_140a64410(sub, slot)` is false → state = `FUN_1405cd540(sub)` which
  maps the player's **role byte** (`+0x3fb8+slot*4`: 0→3, 3–6→1, 7–9→2,
  else 0) — **[OBS]** so the zone/man default comes from a per-slot role
  class; else state = `FUN_1405ccd50(...)` (below) and a score
  `float[sub+0x3aa4+slot*4]` is pushed into a per-state bucket
  (`local_148`, capped at 10 entries per bucket — `abStack_98` starts at 10;
  `fVar = score > 0 ? 10000.0 : -10000.0`, literals `0x14259d634`,
  `0x1425c80e8`). Then, if more than one man-mark was assigned
  (`local_158 > 1`), `FUN_1405cc390` runs a **de-conflict pass**.
  Finally the buckets are compacted (`0x140a79640`) and each slot's
  **mark order index** is written at `mgr+100+slot*8`
  (`local_154[state]++`). **[OBS]**
- **`FUN_1405ccd50` (OBS, 62 lines)** — the per-slot man-mark scorer: outside
  in-play with no plan (`phase != 1`, no sub-concept) and mode 3–5 it records
  a set-piece mark (`+0x1b4/0x1bc/0x1c4`, max 2 — `byte(+0x1cc) < 2`) and
  returns 0; otherwise if `FUN_1405cd540(...) == 2` fails or the attack
  direction test fails, it scores `FUN_14063f760(sub, countdown, slot)` and
  compares against **0.5 normally, 0.4 when the slot has no previous mark**
  (`*(mgr+0xb8+slot*8) == 0`; literals `0x142565600`, `0x14259b89c`).
  **[OBS]** — so keeping an existing mark is easier (0.4) than taking a new
  one (0.5): a **mark hysteresis of 0.1**.
- **Slot compaction `FUN_1405cc1a0` (OBS, 46 lines).** When a slot's player
  leaves (`teamSlot >= 0x16` or `*(ctx+0x1748+teamSlot) == 0`), every higher
  slot with the same mark state decrements its state byte — i.e. **mark
  indices are dense and shift down on substitution/send-off**. Runs 3 passes
  over the 11 slots. **[OBS]**
- **How many mark (cap) — `FUN_1405cd0f0` (OBS, 53 lines).** Returns the max
  number of man-marks: starts from `FUN_14063fa70(team, ctx, countdown, 0)`;
  `+1` (capped at 5) when `byte[team+0xb710]` set (INF: "mark-hard" style
  flag) and the count is < 5; then clamped by `int[team+0xb8a4]` (the live
  match clock, INF: "early game = fewer marks"): `< 1` → `alt+1`, `> 2` →
  `min(alt, count-1)`. The caller `0x1405cdf30` additionally requires the
  candidate to be a non-GK action state (`!= 0x3a` when `slot%11 > 10`),
  a different slot than the requester, in-play-or-not-mode-4, the
  `(0xf, slot)` plan bit, **count < cap**, and **distance² ≤ 10000**
  (`0x14253ee30`, i.e. 100 m — effectively uncapped; INF: the real gate is
  the cap). If `byte[team+0xb8a2]` is set, at most `bVar10` extra marks are
  granted. **[OBS]**

### 4.4 Zonal vs man — summary

- Default is **zonal** (state 1 from the role map `FUN_1405cd540`).
- **Man** (state 2) is assigned to the ball-carrier slot, the mode-4
  carve-out, or any slot whose `0x14063f760` score beats 0.5 (0.4 to keep).
- **Free** (state 3) = no live player or countdown expired.
- The number of simultaneous man-marks is capped (≈ 5 max, fewer early,
  +1 with the mark-hard flag). The cap, the 0.5/0.4 hysteresis and the dense
  slot compaction are all OBS above.
### 4.5 How many press: `FUN_140574200` (OBS, 206 lines)

Called from the fore-check gate [P5]. Returns 1 = "enough players are pressing".
Raw-checked flow:
```
if (!FUN_140a55d40(ballRef, teamIdx, 2)) return 1;   // ball loose/dead → press on
dir = sbyte[team+0x254];
FUN_140a4db20(&baseAng, dir);                         // base angle from direction
u = FUN_140a62960(ctx, teamIdx);                      // presser slot, 0x16 = none
lim0 = 70.0; thr = 6.5; bonus = 0.0; add = 0.0;       // 0x14259be4c/0x1425a2828
if (param_2) add = 2.0;                               // 0x142565604
if (u < 0x16 && FUN_140a53da0(ctx, u, carrierAnchor, baseAng)) bonus = 5.0;
collect candidate slots via FUN_140639830 + FUN_140639a20 (...);
count = 0; close = false;
for (each of the 11 team slots with a live player and a collected flag):
    ang = atan2(dZ, dX) * 57.29578                    // 0x14259be40 = 180/π
    ang -= baseAng; wrap to [0, 360); if (|ang| > 180) ang -= sign*360;
    // (wrapping uses 6.2831855 = 2π, 0x1425795e4, and 360.0, 0x14259858c)
    if (|ang| > 100.0) w = 0.0                        // 0x14253ee30
    else if (|ang| < 170.0) w = (|ang| - 100.0) / 70.0
    else w = 1.0;                                     // 0x1425c964c = 170.0
    r = w * 4.0 + 8.0 + add;                          // 0x1425656c0 / 0x14259bdf4
    if (w > 0.2) r += bonus;                          // 0x142598578
    if (|dXZ| <= r) { count++; if (|ang| < 70.0) close = true; }
return (count > 2 && close) ? 1 : 0;
```
**[OBS]** — so the team press is "on" when **more than two teammates are
inside an angle-dependent radius** around the carrier anchor: the radius grows
**4 m per unit of angular weight over a 100–170° window** (8 m minimum, +2 m
when `param_2`, +5 m bonus when the designated presser has lane), and at least
one of them must be within **70.0°** of the base angle. The 70.0/6.5/5.0/2.0
literals are at `0x14259be4c/0x1425a2828/0x14259222c/0x142565604`.


---

## 5. Attributes and styles in decisions (OBS, partial)

### 5.1 The live-stat pipeline feeds the units indirectly (OBS)

Raw `0x14092b849..0x14092b894` (in the per-frame stat function that also fills
the `+0xC798/+0xC888/+0xC978/+0xCA68/+0xCB58` arrays):
```
idx = *r15 (u16 stat index); if (idx > 0x3b) idx = default;
w = A2[idx] * A3[idx] * A4[idx] * A5[idx] * A1[idx];   // the five live arrays
live = (float)byte[playerBlock + idx] * w;              // base byte × product
```
**[OBS]** — this is exactly the `clamp(base × A1..A5)` pipeline of
`pes-re-matchlogic.md` §3.1, with A1 present (the report's "always 1.0" array
is multiplied too, or folded into the default). The Super-sub write three
instructions earlier (`0x14092b7e0`: `lea rcx,[rip+…]` →
`0x142632f74`, the Super-sub table) confirms the table identity. The skill-bit
test just before (`0x14092b6fa`: `(u32)(player+0x40) >> 0x1a & 1`) selects an
alternate A5 row. **[OBS]**

The think units do **not** read these arrays directly. They read three
precomputed per-player float triples plus a multiplier:
- `player+0x55c / +0x560 / +0x564` — three **base values** (position anchors);
- `player+0x574 / +0x578 / +0x57c` — three **scale values**;
- `ctx0+0x14` — a per-unit **multiplier float** (`*(rdi[0]+0x14)` in the
  Shoot/Pass bodies).
The recurring idiom (raw, e.g. `0x1421231a0`, `0x1421231d1`, `0x142122997`):
```
score = (frames * scale + base) * mult;
```
**[OBS]**. The writer of the scale triple was *not* found in this session
(see open list item 5); it is presumably refreshed once per frame from the
live stats, which is where abilities enter.

### 5.2 Pass (OBS — `ThinkUnitPassForward` slot 2 = `0x14212a700`, 157 lines)

The evaluator walks the 11 candidate slots (`uVar18` = 0..10) and keeps the
best in `param_2[2]` (slot) / `param_2[3]` (kind). Gates first:
- `FUN_140142125c80(ctx, flag)` must return a slot `!= 0xff` (the candidate
  pre-filter; 326 lines, not yet mined — open list item 6);
- each candidate needs `FUN_140a714c0(...)` lane data with
  `laneByte > 40.0` (`0x14259be2c`), `FUN_140a142127ff0` slot `!= 0xff`, and the
  `0x141052e30` mode in {1, 2}.
Then the scoring (raw):
```
s = candRecord[0x3b];                       // per-candidate float
if (!(15.0 <= s && s <= 25.0)) continue;    // 0x14259be0c / 0x14259be20
if (!(candType == 3 && candFlag != 1)) continue;
if (ctx+0xc4 == 3 && |candGoalZ| >= |pitchGoalZ+0x14| && ...) {   // same-side veto
    ...fVar = 1.0 or -1.0 by sign of carrier/candidate goal-Z...
}
if (candFresh && candMark != 0) {
    t = (s - 15.0) / 10.0  (clamped 0..1);  // normalise the 15–25 window
    dist = t * 5.0 + 10.0;                  // 10–15 "units"
    if (dist² < *(candPos + 0x4c) && count > 2 && best < 0x16)
        newBest = carrierMult * candBaseX    // *(ctx0+0x14) * *(cand+0x55c)
        if (newBest < oldBest * oldCandBaseX) adopt;
}
```
**[OBS]** — so the pass score is **`carrierMult × candidateBaseX`**, minimised
(nearest/lowest wins), gated by a **15–25 window** on the candidate float and
a **10–15 distance window**. The `+0x14` multiplier and the `+0x55c` base are
the ability/style entry points (their writers are open list items 5–6).
Direct attribute reads in the pre-filter `0x142125c80` (raw): **height**
(`movzx esi, byte[rax+0x1b]`, twice), and a **skill-bit test**
`FUN_14014063e960(carrier, 0xc)` — i.e. **skill bit 12 must be set** for the
`+0x14` height bonus (`sil += 0x14`), else the raw height is used.
**[OBS]**

### 5.3 Shoot (OBS — `ThinkUnitShoot` slot 2 = `0x142121920`, 443 lines)

The body evaluates the same `(frames × scale + base) × mult` curves against
the **goal-mouth reference** (`r13+0x55c/0x560/0x564` = the goal mouth point;
`+0x574/0x578/0x57c` = the per-candidate scales). Recurring raw fragments:
- `0x142122664`: `x = frames*scaleZ + baseZ`, `y = frames*scaleY + baseY`
  against two candidate goal points;
- `0x1421231a0 / 0x1421231d1`: `score = (n*scale + base) * mult`
  with `mult = *(rdi[0]+0x14)` — the same multiplier slot as Pass;
- `0x142123373`: the three scales `+0x574/0x578/0x57c` read together for a
  **3-way branch** (near / central / far goal region — INF med);
- distance clamps: `comiss` against `16.0` (`0x14259be10`) after a
  `FUN_140a14119d80` call, and the `6.25` test (`0x1425a9be4`) in the dribble
  path below.
**[OBS the arithmetic and addresses; INF the region meaning.]** The Finishing /
Kicking Power entry is the scale triple's writer (open list item 5).

### 5.4 Dribble (OBS — `ThinkUnitDribble` slot 2 = `0x14212fe70`, 535 lines)

- Team-strength gate (raw `0x14213122b..0x1421312b6`): for two team indices it
  reads `byte[teamBase + idx + 0x247be]` and compares
  `float[teamBase + idx2*8 + 0x24710]`; if the best such value exceeds **6.25**
  (`0x1425a9be4`) the dribble is vetoed. **[OBS]** — INF (med-high): a
  **"don't dribble when a much stronger option exists"** team-rating test on a
  per-team float table at `+0x24710` indexed by a byte table at `+0x247be`.
- Readiness gates `FUN_140140699710 / 0x140699820` (both decompiled, 8 lines):
  `*(ctx+0x170) >= 0.0 && (bit0(ctx+0x16c) || (ctx+0x17c < 3 && ctx+0x178 < 3))
  && ctx+0x178 == 0` (resp. `ctx+0x17c < 1`). **[OBS]**
- The sub-call `FUN_140a142131810` (262 lines) fans out to the matchup helpers
  `0x1405d06c0`, `0x14063d2f0`, `0x14063e9f0`, `0x140474c40`, `0x14043a0a0`
  (not yet mined — open list item 7).

### 5.5 Style/skill bits observed in gates (OBS)

- `FUN_14014063e960(team, slot, bit)` = `((*(u64*)(player+0x48) >> bit) & 1)`
  (raw `0x14063e9b2..0x14063e9b9`: `mov rax,[rax+0x48]; shr rax,cl; and al,1`).
  So `player+0x48` is a **u64 skill bitmask**. Observed bit uses:
  - bit `0xc` (12) in the Pass pre-filter (height bonus path);
  - bit `0x1a` (26) in the live-stat A5-row select (`0x14092b6fa`).
  **[OBS]**
- Playing-style bits were not traced to a consumer in this session (open
  list item 8). The 21 style names at `0x142614180..0x1426142F0` and the
  `style` bitmask at in-match `+0x48` (`pes-re-matchlogic.md` §1.3) are the
  anchors for that search.

---

## 6. Open list / what was tried

1. **constant_team.bin index format.** The decompressed blob (3,676 B, names at
   200..402) has a u32 index at 4..136: 16 pairs `(a, b)`. Large `a` values
   (864, 1276, 1580, 1868, 2284, 2380) are file-absolute offsets whose data
   decodes to plausible floats (`defenceMark.o @1580 = 8.88, 12.0, 6.5, 4.0,
   24.0`; `overlap.o @1868 = 50.0, 0.0, 22.0, 5.1, 4.0`). Small `a` values are
   not DATA-relative (decode as zeros/garbage). Tried: absolute, DATA+,
   IDX+, NM+ bases. The authoritative fix is to find the loader — it reads the
   path `cpk_dat/common/match/constant/constant_team.bin` at 0x1425938e0; the
   RIP-xref scan over the whole image returned no hits (bg jobs 1749/1764),
   so the path is likely referenced via a registry/loader table, not a direct
   LEA. Next: xref the `.json` name strings (`basePosition.json` @0x142b4c9d8).
2. **Live validation.** Game is in OPEN_PLAY (probe: `STATE=OPEN_PLAY
   ctx=0x3b870010`). `gMatchContext @0x1436F3DC8 → 0x3b870010` is a live cobra
   registry (247 entries, stride 0x28, vtable pointers match RTTI:
   `MatchInfoRef @+0x258`, `MatchEnvRef @+0x2a8`, `BallInfoRef @+0x28`, …
   mapped via rtti_all.json). Reads are via `sudo python3` + `/proc/pid/mem`
   (`<scratch>/live2.py`, tolerant `rd`). The dump-era offsets (`MI+0x116c`,
   `+0x270`, pitch dims at `+10920`) do **not** hold in the live heap — the
   per-object data pointers (`entry+0x10`) lead to small registry objects, and
   the pitch signature (52.56, 34.06, 3.66, 2.44) was not found by scan. So no
   live formula check was completed. Next: locate the live team-AI object via
   the `0x1405cc070`-built context (search heap for the 0x68-stride slot array
   with a `+0x58` role byte 0..12).
3. **Press assignment `FUN_140574200` — closed** (§4.5, 206 lines, all literals
   resolved). Its sub-calls `FUN_140639830` / `FUN_140639a20` (candidate-slot
   collection) not yet mined.
4. **Run scorers** (`0x140606060`, `0x140606cd0`, `0x14060ab00`, `0x14060d210`,
   `0x140613910`, `0x14061dee0`, `0x14061e810`, `0x14061a640`) not decompiled.
   Start with the `Overlap` pair (`0x140601580`, `0x1406013d0`) — smallest.
5. **Scale-triple writer** (`player+0x574/0x578/0x57c`, `+0x55c/0x560/0x564`,
   `ctx0+0x14`). A whole-image scan for `movss [reg+0x574/0x578/0x57c], xmm`
   found 123 *read* sites, no writers (the scan pattern only matched reads;
   writers use a different base or `movaps`). Next: scan for `movss` with the
   displacement in the *destination* position (`[r+0x574],xmm`).
6. **Pass pre-filter `FUN_140a142125c80`** (326 lines) not mined beyond the
   height/skill-bit reads. Contains the `0x14063e960` gate and the
   `byte[+0x1b]` reads — mine it for the full candidate admissibility.
7. **Dribble sub-calls** (`0x142131810`, `0x1405d06c0`, `0x14063d2f0`,
   `0x14063e9f0`) not mined. `0x142131810` is 262 lines with matchup-helper
   calls — the likely home of the Dribbling/Tight-Possession weighting.
8. **Playing-style consumers** not found. Search anchor: the 21 style strings
   at `0x142614180..0x1426142F0` had no code xrefs (bg job 1749); they are
   likely consumed via the `ai_selector` data files or the style bitmask at
   in-match `+0x48`. Next: xref the bitmask readers (`shr rax, cl` on
   `[player+0x48]` with cl = style-bit range 0..20).
9. **GK (§5 of player-ai.md) untouched.** `ActionKeeper*` vtable inventory is
   in hand (27 classes, §4.4-adjacent list from the RTTI grep); none
   decompiled. The `PlayerGKAuto` vtable `0x14262fd78` slot 0 = `0x1421018e0`
   (shared with PlayerOffence/PlayerDefence — the per-player AI entry).
10. **VFS AI tables (§7) untouched** beyond the constant_team.bin attempt.
    `ai_selector::File/ArchiveFile` and the `selectorVision.o` /
    `combination.o` blobs are the entry points.

*Scratch kept:* `<scratch>/TeamPlansRE/` = `688300.c`, `1405747f0.c`,
`140574ea0.c`, `1405752a0.c`, `140575db0.c`, `140574200.c`, `140639100.c`,
`14051c860.c`, `1405cc070.c`, `1405ce4f0.c`, `1405cdf30.c`, `1405cda40.c`,
`1405cdbd0.c`, `1405ce930.c`, `1405cffe0.c`, `1405cf310.c`, `1405cfc80.c`,
`1405cc1a0.c`, `1405cc250.c`, `1405ce150.c`, `142121920.c` (Shoot),
`14212fe70.c` (Dribble), `14212a700.c` (PassForward), `142128bf0.c`,
`142124cc0.c`, `142124950.c`, `142125c80.c`, `142131810.c`, `1405ccd50.c`,
`1405cd540.c`, `1405cc390.c`, `1405cd0f0.c`, `constant_team.bin`,
`constant_team.json`, `cg_teamai.json`, plus `<scratch>/dz.py`, `<scratch>/fnscan.py`,
`<scratch>/scan2.py`, `<scratch>/xrefrip.py`, `<scratch>/live.py`, `<scratch>/live2.py`, `<scratch>/srch.py`.


> **Correction (TransitionsRE, `pes-re-transitions.md`):** §4.1 has the fore-check branch inverted: the fore-check (and TransitionChase) needs instruction 0xd **not** armed (raw 0x1405748d0).
