# PES RE leftovers — LeftoversRE (2026-10-14)

Four self-contained leftovers. Live state on entry: OPEN_PLAY (match in progress); match later ended (G=0) so live reads
available only early. Static image: `pes_full.bin` base 0x140000000; decompiles via `dec.sh` (PESLive, read-only).
Tags: **OBS** = observed (decompile/disasm/raw/live); **INF(conf)** = inferred.

---

## 1. Officials' ids (§4.3 pick rule) — OPEN (structure OBS, pick-rule writer not found)

- **OBS (FUN_1404187b0 @0x1404187b0, entity creation §4.1):** official `i` (i=0,1,2) is created by
  `FUN_140a35a00(kind=i, id=S.u32[0x30+4*i], S.u8[0x12], S.u8[0x10])`. So the chief+2 assistants take ids at
  `S.u32[0x30],[0x34],[0x38]`; §4.2's 5th/4th (ids 35,19 at +0x3c/+0x40) are used only by demo actors.
- **OBS (FUN_14041b640 @0x14041b640):** `S = *(holder+0x10) + 4`, where `holder = *(match+0x5e8)`. The `+0x5e8`
  object is a lazy proxy (vtable slots 8/0x10 called once); `S+0x30 = holder_block + 0x34`.
- **OBS (FUN_14041b870 @0x14041b870):** materialises the holder by a `FUN_141b55730` bundle-load
  (`SUB_140421f40`), i.e. the setup comes from a serialised pre-match bundle (competition/fixture setup), not a
  runtime RNG at kickoff.
- **INF(medium):** the ids are populated into that pre-match setup block upstream (menu/competition config), then
  read here. Values seen live (8/23/32/35/19) are 5 distinct ids from the 1..35 RefereeAppearance list — consistent
  with a distinct pick but the writer (random vs rotation vs table) is NOT yet located.
- **Killed/next:** no RNG-call in FUN_1404187b0 (ids are pure struct reads). Next exact step: xref/ref the setup
  bundle loader `FUN_141b55730(…, SUB_140421f40)` to find who fills `holder_block+0x34..+0x44` (the 5 ids); if it
  is fed by a per-fixture seed, decompile that seeder for the generator + slot.
- **GF impact (what this replaces):** until the pick rule is found, GF should take the 5 official ids (chief,
  2 assistants, 4th/5th for demos) as a match parameter — ids are ReffereeAppearance ids 1..35, chief = id[0],
  assistants = id[1],id[2]; `FUN_1404187b0` shows they are pure setup reads with no on-the-fly RNG.

---

## 2. Match ball per competition (`0x144a58df0`) — CLOSED

- **OBS — query body (0x144a58df0, decomp):** `bool(table, cond, value)`: for each in-memory BallCondition rec `i`
  with `(u16 condition & 0x3F) == cond`, look the rec's `ballId` up in `Ball.bin` (`FUN_1414bae10` on `*(db+0x48)`),
  read the ball's attribute 0 (`func_1414dfed0(ball,0,&out)`), return 1 if `out == value`.
- **OBS — attribute 0:** `func_1414dfed0(ball,0,out)` → `out = *ball` = the Ball.bin record's first u16 = its own
  ballId (index 2 → order low byte). So `out == value` means the BallCondition rec's ballId == `value` **and** that
  ball exists in `Ball.bin`. ⇒ the query is the **per-competition ball membership test**: *"is `value` a ball used by
  competition `cond`?"* `cond` = the BallCondition record's `condition & 0x3F` (competition/condition id, §1.3);
  `value` = a ball id. INF(high) on the exact competition-id numbering (the game's own cond values per record).
- **OBS — no vtable slot, no caller (this closes the §1.3 sub-item negatively):** full-authoritative xref of all
  158,619 `.pdata` functions plus a whole-image byte/unaligned u64 scan found **zero** call/jmp/rip-ref sites and
  **zero** stored u64 pointers to any of `0x144a58df0 / 0x1414d6010 / 0x14eb61782 / 0x140ea942c`. **Killed: the
  thunk is not in any vtable and has no static caller** — it is either dead code in the shipped exe or dispatched
  via a runtime-built function pointer (no static reference point). The earlier "called through a pointer/vtable"
  is confirmed to have no static root in the image.
- **GF impact:** the ball-match choice GF needs is simply the per-competition `BallCondition.bin` lookup plus the
  §1.3 default rule; the dead membership helper needs no port.

---

## 3. Late-card window unit (§9.10) — CLOSED

- **OBS — the check (FUN_140444060 @0x140444060):** `(int)(FUN_140a3c050(MI+0x1168) − flagTime) / 0xe10 < 0x10`,
  where `0xe10 = 3600` (FUN_140a6bd60 @0x140a6bd60 = `(int)x/3600`), gated on `MI+0x1690 ∈ 7..0x13` (mask 0x883ff)
  and stop-kind 4 / offside. Outside it, clears the pending card (Flow+0x57f8=0, UI 0x1050039).
- **OBS — both timestamps are the SAME clock:** `FUN_140a3c050(clk) = (float)FUN_140a3c070(MI+0x1168) + f32(MI+0x1174)`
  (raw disasm `movss xmm0,[…+0x17a4]`, `addss xmm0,[rbx+0xc]`); the flag time `MI+0x17a4` is written by the same
  expression in `FUN_140a3c9f0` (on card-flag, also sets `MI+0x17a0 = kind` byte) and in the tail of `FUN_140a3c560`
  (reset). Decompiler's `u32` return on `func_140a3bd80` is a mislabel — raw is `movss` (f32). So `now − flag` =
  elapsed clock-units since the card was flagged.
- **OBS — the clock magnitude is tens-of-thousands, so NOT seconds (⇒ NOT hours):** live read of `MI+0x17a4`
  (stored flag clock) = **32007.5**. An in-progress match read as *seconds* would be ~hundreds; 32007.5 is only sane
  as a ~1/60-s tick count (≈ 533 s ≈ 8.9 min of match). **Killed: "16 hours".**
- **Design confirms minutes:** ÷3600 = ÷60 (s) ÷60 (min) is exact only for a 60-per-second clock ⇒ "< 16" =
  **16 game-minutes** (57600 ticks). INF(high): exact tick is 1/60 s (a 54 fp tick would give ≈ 17.8 min — still
  minutes; hours is ruled out either way). The window could not be re-measured live (match ended).
- **`MI+0x158c == 1` (INF(high) = foul penalised WITHOUT a card):** all 17 +0x158c code sites are reads/cmp — there
  is **no** register/immediate store (even scanning REX prefixes), so the value is written by a struct/block copy
  from the foul decision. From the decision tree (carded = `MI+0x158c ∈ {3,4}`; 3=yellow,4=red,2=warning) and the
  referee gate ("JUDGE on the spot only if `MI+0x158c == 1`", cards 3/4 shown by scene with no approach gate), a
  value of 1 = whistle + JUDGE gesture but **no card** = "foul without a card". Open exact-writer: the block-copy
  that sets +0x1584..+0x158c.
- **GF impact (replaces):** the late-card/deferred-card window in `Referee::IssueDeferredCards` must be
  **16 game-minutes** (57600 ticks of a 60 ticks/s clock) from the flag, NOT 16 hours; and GF's judge-gesture
  decision should gate on `card==1` = "foul/no card" (separate from the card-show states 3/4).

---

## 4. `injury.o` reader — deserializer + fields CLOSED; the reading rule still open

- **Class:** `constant::Injury` (RTTI `.?AVInjury@constant@@`, COL **0x142df74e0**, bases `constant::ConstantData`
  + `constant::InjuryBase`). Loaded from `constant_match.bin` `injury.o`.
- **Deserializer:** `FUN_141e78e50 @0x141e78e50–0x141e78f8a` (only code that references the injury field-name
  strings; reads each via `FUN_1403842a0(bin, key)` → int). **Field layout in the object:**

| obj off | field |
|---|---|
| +0x00 | vtable |
| +0x08 | `dummy` |
| +0x0c | `levelDamageMicro` |
| +0x10 | `levelDamageMiddle` |
| +0x14 | `levelDamageMinor` |
| +0x18 | `levelDamageSerious` |
| +0x1c | `symptomDamageBruise` |
| +0x20 | `symptomDamageFracture` |
| +0x24 | `symptomDamageInflammation` |
| +0x28 | `symptomDamageLaceration` |
| +0x2c | `symptomDamageLigament` |
| +0x30 | `symptomDamageTearMuscle` |

  4 `levelDamage*` (damage→severity ladder) + 6 `symptomDamage*` (per-symptom damage) = the "ten values 120..250"
  of the stamina report. (The 4 `levelDamage` are the thresholds; the 6 symptom values gate symptom selection.)
- **Object id + live values (OBS):** `injury.o` = constant id **0x0b**, retrieved by the constant-data getter
  `FUN_141e5ca20(reg=0x2984a40, id)` (`reg[id]<<8`). Live `constant::Injury` @0x1c3ff010 (this boot):
  `dummy=0, levelDamage{Micro=120, Middle=220, Minor=180, Serious=240},
  symptom{Bruise=120, Fracture=250, Inflammation=160, Laceration=220, Ligament=240, TearMuscle=230}`.
- **Consumer ("which injury rules read those fields"):** could NOT be isolated statically — strong negatives (all OBS):
  - The id `0x0b` is **never passed as a literal** to the getter: of all 227 resilient-scanned `FUN_141e5ca20` calls,
    none set `mov edx,0xb`; the id is assembled dynamically (register/`.rdata`), so there is no static reference point.
  - The live scan for pointers to the injury object found only **1 hit — the registry wrapper itself**
    (`{+8 = obj, +0x10 = id 0xb}`); no match-context holder while in menu.
  - The obvious consumer, `match::Injury` roll (stamina §6, reset @0x1404818e0, promotion @0x1404819a0), uses
    **literal** thresholds (85/150/200) and has **no** getter call. The `match::anime::action::Injury` demo action's
    vtable slots (vtable 0x1425e3038) also contain no getter call.
  ⇒ the constant is loaded+registered but its reader is not exercised in the menu/static image (dead in this state,
  or fetched only during a live injury-resolution with a runtime-assembled id). **Next exact step:** while PES is in
  OPEN_PLAY and an injury/symptom resolves, re-run the pointer-to-injury-object memory scan (the match context then
  holds it) and decompile the holder rule — it reads `inj+0xc/+0x14/+0x18` (level thresholds) and `inj+0x1c..+0x30`
  (per-symptom). (Stamina §6's literals for the roll stand; injury.o's reader is separate and not yet observed.)
- **GF impact (what this replaces):** GF's injury model should use a 4-tier damage ladder
  (`levelDamage` 120/220/180/240 → micro/middle/minor/serious severity) and a 6-way symptom table
  (`symptomDamage` 120/250/160/220/240/230 for bruise/fracture/inflammation/laceration/ligament/tear-muscle) as data,
  replacing the hard-coded drain thresholds in `pesstamina.cpp`'s injury roll where they correspond.
