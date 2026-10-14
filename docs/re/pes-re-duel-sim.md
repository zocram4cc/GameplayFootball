# PES 2021: the duel / dive simulation `FUN_1405b83b0` — what its local RNG decides (DuelSimRE)

Tags: **OBS** = seen in the raw bytes of the unpacked image (`pes_full.bin`, base `0x140000000`), in a
disassembly of those bytes, or in an emulation of that code. **INF(level)** = inference, basis stated.
No player or team names. Read-only: no live process was touched (none was running this session).

Starting points (cited, not re-derived): `docs/re/pes-re-rng-slots.md` (the local default-seeded
`math::Random` in `0x1405b83b0`, draw at `0x1405b8759`, slot 0, M = 100), `docs/re/pes-re-rng.md` §1
(ctor, `Range`, LCG), `docs/re/pes-re-duels-fouls.md` §2 (Judge, SetPending record, commit, advantage).

Scratch: `~/ghidra-projects/scratch/DuelSimRE/` — `dec/main.c` (decompile of `0x1405b83b0`),
`dec/judgeupd.c` (Judge update `0x14050b490` + offside `0x14050df20`), `dis_b83b0.txt` (raw
disassembly `0x1405b83b0..0x1405b89ee`, 369 insns), `callscan_jcc.py` (call/jmp/**jcc** rel32 caller scan,
whole image including `.impdata`).

---

## 0. Answer

**The RNG decides nothing.** It decides, in principle, whether a dive with no real contact still gets
the nearest opponent penalised (direct FK/PK, the "referee was fooled" outcome). In practice it cannot:
a fresh `math::Random` is built on the stack on every call, with the ctor's fixed seeds. Exactly one
`Range(slot 0, M = 100)` is drawn, and it is always **98**. The threshold is at most **10**, so the roll
always fails. The diver is then **booked, yellow, with an indirect free kick at his own position**.

The function is the Judge's **per-frame dive check**. It is not part of ordinary challenges. Its outcome
does not feed the Judge points model (§3).

---

## 1. Draw site, arguments, and the result (OBS)

| step | address | bytes / insn | meaning |
|---|---|---|---|
| ctor | `0x1405b8405`/`0x1405b8409` | `lea rcx,[rbp-0x20]` ; `call 0x140a923f0` | `math::Random` on the stack, seeds `0x2f3d0..0x2f3d3` (rng.md §1.1) |
| threshold 5 | `0x1405b8749` | `b3 05` `mov bl,5` | roll path A (opponent moving, < 1.0 m) |
| threshold 10 | `0x1405b85ed` | `b3 0a` `mov bl,0xa` | roll path B (diver's `human+3` = JOSTLE) |
| halve in box | `0x1405b874b..51` | `bt edi,0xb` ; `jae` ; `shr bl,1` | `edi` = zone flags of the diver's spot; bit 0x800 = the penalty area he attacks → 5→2, 10→5 |
| draw | `0x1405b8753..59` | `xor edx,edx` ; `lea rcx,[rbp-0x20]` ; `call 0x140a92470` | `Range(rng, slot 0)` → `⌊s'·2⁻³¹·100⌋`, M = 100 from `.rdata` table `0x1426548b0` |
| compare | `0x1405b875e..63` | `movzx ecx,bl` ; `cmp eax,ecx` ; `jae 0x1405b88cd` | `r < thr` → real foul by the opponent (`0x1405b8769`); else → book the diver (`0x1405b88cd`) |
| dtor | `0x1405b89a2..a6` | `lea rcx,[rbp-0x20]` ; `call 0x140a92430` | destroyed on every exit |

- **The value is constant.** The only references to `[rbp-0x20]` in the function are the ctor, the draw
  and the dtor (`dis_b83b0.txt` lines 21, 221, 350). The one stack write near it, the 12-byte velocity
  vector from `FUN_140a6c6d0` at `[rbp-0x38]` (`movsd [rdx]` + `mov [rdx+8]`, `0x140a6c733/37`), ends 12
  bytes before the Random. The function has no `setseed` (rng-slots.md †). OBS.
- **Draw = 98 (OBS, emulated).** `Unpacker/emu.py 0x140a92470` with slot 0 = `0x2f3d0` and `edx = 0`
  stores the state `0x7e3b73a3`, computes `mulss → 98.6189`, and returns `rax = 0x62` = 98. The table read is
  `[0x1426548b0] = 0x64`. The bit-exact Python formula from rng.md §1.2 gives the same result (draws 2..8
  would be 43, 19, 41, 20, 60, 43, 66, but the function never makes them).
- **Constants and their sources (OBS): all are literals, none comes from a VFS or `.o` file.** The thresholds
  5 and 10 are instruction immediates. M = 100 is the `.rdata` modulus table. Distances and frame windows
  are `.rdata` floats read RIP-relative: 1.0 `0x14256ae1c`, 1.25 `0x1425ab128`, 3.0 `0x14258c264`, 12.0
  `0x14259be04`, 54.0 `0x14259d624`, and 0.69444 `0x14259bdd0` (velocity scale; only `≠ 0` is tested). Each
  target was recomputed from the RIP displacement.

---

## 2. Callers: it runs only as the per-frame dive check (OBS)

Previous scans (e8/e9 rel32, lea, qword) found **no** caller. That was a scan gap, not the truth:

```
0x1405b89f0: 80 79 08 00          cmp  byte [rcx+8], 0        ; judge+8 = "foul pending"
0x1405b89f4: 0f 84 b6 f9 ff ff    je   0x1405b83b0            ; conditional TAIL JUMP (jcc rel32)
0x1405b89fa: c3                   ret
```

The leaf wrapper `0x1405b89f0` has no `.pdata` entry. `callscan_jcc.py` scans call, jmp and jcc rel32
across the whole image, including `.impdata`. It finds exactly two references:
- `0x1405b83b0` ← `0x1405b89f4` (`0f84`, the wrapper), and nothing else;
- `0x1405b89f0` ← `0x14050b4c7` (`e8`) in the **Judge update `FUN_14050b490`**, and nothing else.

Chain (OBS, raw):
```
Flow functions 0x140449515 / 0x14045478a / 0x1404a56e5   (call sites 0x1404495f8, 0x1404549c2, 0x1404a584d)
 └ Judge tick 0x14050c3e0: skipped when MI+0x1168 ∈ {2,6,9,10} (bt 0x644) or judge+0x9c == 0
    └ Judge update FUN_14050b490 @0x14050c41d
       ├ judge+0xcc ∉ {1,2,3} (ball in play; duels-fouls §3) →
       ├ 0x14050b4c7: call 0x1405b89f0  → if !judge+8: FUN_1405b83b0(judge)   ← dive check, FIRST
       └ 0x14050b4cf: call FUN_14050df20                                       ← offside check, second
```

- **It does not run for real challenges.** Ordinary contact goes through the contact event 0xf →
  `FUN_14050e4a0` → `FUN_1405b7470`/`FUN_1405b8000` (duels-fouls §2.2), and that path has no Random.
  `FUN_1405b83b0` does nothing unless the ball player's anime action is **DIVE 0xf**
  (`cmp byte [rdi+1],0xf` / `jne exit`, `0x1405b85aa`). OBS.
- **It judges each dive at most once.** Every path that gets past the gates ends in `Judge::SetPending`
  (`0x14050ee40`), which sets `judge+8 = 1` (`0x14050eed1`). The wrapper then stops calling it until the
  pending foul is resolved. OBS. INF(hi): **one draw per dive**, always on the first frame the DIVE is
  seen, and always the value 98.
- INF(med): a contact foul raised later in the same stoppage window replaces the dive record. The
  "do not replace a stronger pending foul" test in `FUN_1405b7470` (duels-fouls §2.5 step 8) compares
  points, and the dive record carries points 0. The event-vs-update order inside one frame was not checked.

**Who produces a DIVE (OBS, partial):** the ball carrier's think-answer translator `0x14210e790` maps
answer type 9 to AI action **0x5a = 90 DIVE** (`mov dword [rcx+4],0x5a`, `0x14210e81c`; jump table
`0x14210e844`). INF(med): AI action 90 drives anime action 0xf. Which think unit emits type 9, and whether
a human input can trigger it, is open (§6).

---

## 3. Branch structure of `FUN_1405b83b0(judge)` (raw-disassembly cross-checked)

Notation: `G = *(0x1436f3dc8)`; `MA = *(*(judge+0x3b8)) + 0x18`; `hu(i) = FUN_140a48910(G,i)` (anime
state: `+1` action, `+3` second action byte, `+0x1a` clip frame u16, `+0x1c` contact frame u16, `+0x64/+0x6c`
contact point x/z); `pl(i) = FUN_140a48a10(G,i)` (position `+0x55c/+0x564`, metres);
`gs = FUN_141e532e0()` (int, 54). Every outcome is tagged.

```
E0 exit (no-op):  G+0x400 online-practice object present ∧ 0x141f9c3b0() ∧ 0x141052e30()==0     [0x1405b8416..38]
E1 exit (no-op):  *(G+0x2c0)+0x76 != 0  (card suppression; 0x140a3f570 = cmp byte [rcx+0x76],0) [0x1405b844c..53]
D  = FUN_140a627e0(ctx,0) (ball player, MA+0x2601c); if > 21: player of newest ball event
     (0x140a3e190(ctx+0xe0,0)+4 → 0x140a62ea0)                                              [0x1405b8491..c2]
E2 exit:          D > 21                                                                       [0x1405b84c9]
O  = first id of MA+0x22e4c + D·200 + (1−team(D))·0x58  = nearest opponent (distance-sorted
     per-player lists; defence-leftovers / defender-decisions docs)                            [0x1405b8532..57]
E3 exit:          O > 21                                                                       [0x1405b855d]
vO = velocity of pl(O) (FUN_140a6c6d0, scale 0.69444/gs)
E4 exit:          hu(D)+1 != 0xf (not DIVING)                                                  [0x1405b85aa]
z  = FUN_140a72e70(field, pl(D).x, pl(D).z, dir(team D))
E5 exit:          z & 0x400  (diver inside his OWN penalty area)                               [0x1405b85ce]
if hu(D)+3 == 0x48 JOSTLE (0x140a6e300: bit 15 of 0x1426484f0[id], set only for 0x48):
      thr = 10 → ROLL                                                                          [0x1405b85ed]
dC = |pl(O) − pl(D)|,  dP = |hu(O).contact_pt − pl(D)|   (metres, x/z)
W  = hu(O)+0x1c + 3·gs/54                                (3 frames at 54 fps)
a  = (i8) hu(O)+1
switch a:
  0xb/0xc SLIDING:  frame < contact → BOOK;  W += 12·gs/54 (→ contact+15 frames);  → T
  0xd TACKLE:       → T
  T:                frame < contact → BOOK;  dP ≥ 1.25 → BOOK;  → K
  0x38 / 0x3d (keeper actions):  → K      (no contact-frame lower bound, no dP test)
  K:                frame > W → BOOK;  dC < 1.25 ? REAL : BOOK                                  [0x1405b86ed..05]
  anything else (incl. a < 0xb, 0xe, 0x10, 0x48, negative):
                    |vO| == 0 → BOOK;  dC ≥ 1.0 → BOOK;  thr = 5 → ROLL                         [0x1405b8707..49]
ROLL: if z & 0x800: thr >>= 1;  r = Range(local rng, 0) = 98;  r < thr ? REAL : BOOK            [0x1405b874b..63]
```

Outcomes (all OBS, SetPending argument mapping checked against `0x14050ee40`: callee `[rsp+X]` = caller `[rsp+X−8]`):

| outcome | reached when | fouler / victim | restart `+0x24` | kind `+0x28` | level `+0x2c` | points | spot | `+0x48` | `+0x50` | `+0x54` |
|---|---|---|---|---|---|---|---|---|---|---|
| **REAL** `0x1405b8769..a8` | the opponent's slide, tackle or keeper action is in its contact window and within 1.25 m (deterministic), **or** ROLL success (**dead**: 98 ≥ 10) | O / D | 5 FK; **6 PK** if `z & 0x800` (spot = penalty mark via `FUN_14050d250` → `FUN_14050d280(6,…)`) | **2** direct | **1** (foul, no card) | 0 | D's position or the penalty mark | **1** | 4 | 0 |
| **BOOK** `0x1405b88cd..82` | every other judged path, including every roll | D / 0xff (victim ctx idx 0x18) | 5 FK | **3** (simulation, indirect) | **3** (yellow) | 0 | D's position `pl(D)+0x55c` | 0 | 4 | 0 |
| no-op | E0–E5 | — | — | — | — | — | — | — | — | — |

Both SetPending calls also store the whistle hold `⌊0.8·gs+0.5⌋` (`FUN_14050cd70`), frame `MI+0x1198`, a
replay snapshot (`FUN_140a81860` / `FUN_1405b8a00` / `FUN_140a81ac0(JR+0x148,…)`, destroyed by
`0x14159fa0c(…,0x2c,0x16,…)`), and `+0x4c = 0.0`. OBS.

**Rule faithfulness.** Booking simulation with an IFK is rule-faithful (Law 12: an attempt to deceive
the referee is a caution plus an indirect free kick). The rest are game abstractions: the 1.25 m and
contact-frame windows, the dead roll, PK-for-dive with no card, and the exemption inside one's own box.

---

## 4. Consumer side (OBS unless tagged)

1. **The draw's direct consumer** is `cmp eax,ecx` / `jae` at `0x1405b8761/63`. It picks one of the two
   SetPending calls. Nothing else reads the value.
2. **Judge update `FUN_14050b490`, while a foul is pending** (duels-fouls §3): it holds the whistle
   for 0.8 s, then `ShouldPlayAdvantage` `FUN_14050ff50` returns no for kind 3 and no for `+0x48 = 1`, so
   **a dive never gets advantage on either branch**. It then calls the commit `0x143f546f0`, which calls
   `FUN_14050e770(judge, side, restart, reason 5, spot, kind, fouler, victim)`. **side = the team that is not
   the fouler's** (raw `0x143f54704..2c`: `rec+0 = fouler`, `<0 or <11 → 1`, `11..21 → 0`, `≥22 → 1`).
   For BOOK the fouler is the diver, so the IFK goes to the diver's opponents. For REAL it goes to the
   diver's team.
3. The commit writes the card: **`MI+0x158c` = level for kind 2** (REAL → 1, a foul with no card) and
   **3 for kind 3** (BOOK → yellow); `MI+0x1588` = kind; `MI+0x1590` = fouler (duels-fouls §2.7).
4. **Choreography** (foul-choreography §1, §4.4): `MI+0x1588 == 3` is an **indirect** offence. The
   referee does the `judge_fk_indirect_*` raised arm (190–193), and **kind 3 never plays a foul scene**
   (`0x14047f2c0`), so the yellow has no card cutscene.
5. Sending-off reader (INF med): `0x140523ab2` reads `MI+0x158c`; `== 4`, or `== 3` with the fouler's
   booking byte (`FUN_1414dab90(player)`) `≥ 2`, takes the dismissal branch. The dive yellow therefore
   counts toward a second yellow like any other. The tally increment itself was not traced (§6).

---

## 5. Relation to the foul-point model

**A separate layer, not an input.** The dive check never calls `P_angle`, `P_speed` or `P_action`, the
level thresholds, DOGSO or the repeat-offender memory. It writes points 0 with a hard-coded level (1 or
3) and type byte `+0x55 = 0`. It runs from the Judge update, not from the contact event. No Judge-points
code reads anything it produces, apart from the pending-record replacement test (§2, INF med). The
outcome is not cosmetic, though: it creates a real stoppage, a restart (IFK, or FK/PK against the
opponent), and a card.

---

## 6. Corrections to existing docs (to be applied by Main; `docs/re` not edited)

`pes-re-duels-fouls.md` §2.8:
- **Keeper actions 0x38/0x3d** are also a deterministic REAL path. It uses only the upper window
  (`frame ≤ contact + 3·gs/54`) and the centre distance < 1.25 m, with no contact-frame lower bound and
  no contact-point test (raw `0x1405b8696..9e` → `0x1405b86ed`). It is not the roll path.
- The REAL test for slides and tackles also needs **contact point to diver < 1.25 m** (`dP`,
  `0x1405b86df`) in addition to the centre distance.
- "Diver's action-property bit 15" means **`hu(D)+3 == 0x48 JOSTLE`**. Bit 15 of `0x1426484f0` is set
  for id 0x48 only, and the byte read is `+3`, not `+1`.
- In the roll path for other opponent actions, a **stationary opponent (`|v| = 0`) or one ≥ 1.0 m away
  is booked without a draw**.
- "Own box" is zone bit **0x400** of `FUN_140a72e70`, and "opponent's box" is **0x800**
  (`dir·x > 0`, raw `0x140a72f49..71`).

`pes-re-rng-slots.md` †, and the duels-fouls §4 handball argument: the dive judge **has** a caller,
the Judge update through `0x1405b89f0` and a jcc tail jump. The "+1 hidden setseed in 0x1405b83b0's
neighbourhood" note is unrelated to this function (no setseed in `0x1405b83b0..0x1405b89fa`).

---

## 7. GF impact

- **Counterpart: none.** OBS by searching `src/` in the GodotFootball repo: there is no outfield dive or
  simulation action. All "dive" code is keeper saves (`gameplaytuning.hpp`, `player.cpp`,
  `penaltyshootoutcontroller.cpp`). `Referee::TripNotice` → `JudgeContact` is the only foul producer
  besides offside, and `struct Foul` has no indirect, non-offside kind. rng-slots.md §12 already lists
  "duel / dive simulation: no GF draw site".
- **What GF would need, only if a dive action is ever added** (ponytail: skip until then, YAGNI):
  1. Per tick, while in play, with no pending foul and cards not suppressed: if the ball player is in a
     dive state outside his own box, judge it once (§3 table).
  2. REAL: the nearest opponent is sliding (contact reached, `frame ≤ contact + 15` ticks at 54/s,
     contact point and centre both < 1.25 m), tackling (the same with `+3`), or in keeper action
     0x38/0x3d (`frame ≤ contact + 3`, centre < 1.25 m). The result is a direct FK against the opponent,
     or a PK in the attacked box, foul level 1, no card, no advantage.
  3. Otherwise BOOK: yellow, indirect FK at the diver's position, no advantage, no card cutscene.
  4. **Draw no random number.** The PES roll is a constant failure. A GF draw would consume a stream
     value that PES never consumes, and could produce a "fooled referee" outcome that PES can never produce.
- **Nothing to change today.** No GF behaviour diverges, because GF never produces the input state.

---

## 8. Killed hypotheses

| hypothesis | killed by |
|---|---|
| The RNG picks the body zone or winner of a duel | The function is gated on the diver's anime action == 0xf (`0x1405b85aa`). The single draw only selects between two SetPending calls, and no body-part or capsule data is read. |
| The RNG decides whether an ordinary contact is a foul | Contact fouls go event 0xf → `FUN_1405b7470` with no Random. `FUN_1405b83b0`'s only caller is the Judge update via `0x1405b89f0` (`callscan_jcc.py`). |
| `FUN_1405b83b0` has no caller, so it is unreachable (previous session's interim finding) | `0x1405b89f4: 0f 84` conditional tail jump, which e8/e9 scans cannot see. Wrapper called at `0x14050b4c7`. |
| The roll can sometimes succeed because the Random persists between calls | Ctor `0x1405b8409` and dtor `0x1405b89a6` run on every call, there is no setseed, and nothing else touches `[rbp-0x20]`. Emulated draw = 98 ≥ max threshold 10. |
| Keeper actions 0x38/0x3d take the roll path (previous session's reading of the decompile) | Raw `cmp ebx,0x38 / je 0x1405b86ed` and `cmp ebx,0x3d / je 0x1405b86ed`. They go to the deterministic window + 1.25 m test. |
| Bit 15 is a property of the diver's current action | It reads `hu(D)+3` (`movsx ecx,byte [rax+3]`, `0x1405b85dc`), and bit 15 is set only for id 0x48. |
| The thresholds come from data (`.o`/VFS) | They are `mov bl,5` / `mov bl,0xa` immediates. The floats are RIP-relative `.rdata` literals. |

---

## 9. Open items, each with an exact next step

1. **What `hu(i)+3` holds** (INF low: previous or secondary anime action). Byte-pattern search for
   `+1 → +3` copies found nothing. The record is published by bulk copies (`0x1406e1ce0`,
   `0x140a47290`). Next: decompile the publisher of `*(G+(i+0xa5)·0x28)` (start from the copy loop at
   `0x140a47290..0x140a472ce`) and name the source field copied to `+3`. Alternatively, run a read-only
   live sampler of `hu(i)+1/+3` for all 22 players across a jostle.
2. **Who emits carrier answer type 9 (→ AI 90 DIVE)**, and whether a pad input can trigger a dive. Next:
   scan the 25 think-unit Evaluate functions (carrier-ai §1) for stores of `1`…`9` into the answer
   record's `+0` type with `9`. Run `callscan_jcc.py` on `0x14210e790` and follow its callers to the unit
   table. Also search the controller input map for the AI-action-90 request.
3. **Same-frame ordering between the contact event and the Judge update** (does a contact foul replace a
   dive record?). Next: order the calls of `FUN_1404514f0` (@`0x140451a8e`, the event handler) and the
   Judge tick callers `0x1404495f8 / 0x1404549c2 / 0x1404a584d` inside one Flow frame.
4. **Booking tally for kind-3 yellows.** Next: decompile `0x140523a80` (the `MI+0x158c` reader) and
   `FUN_1414dab90`, and find the writer of the byte it returns. The other readers of `+0x158c` are listed
   in the scratch log of this session: `0x140444e02`, `0x140464ea2`, `0x14047f38f`, `0x1405acfa7`,
   `0x140a3a2a7`, `0x140a3a3e2`, `0x140a3a9dd`, `0x140a3aa2c`, `0x14146364d`, `0x141463744`,
   `0x1420b99d3`, `0x1420c6a70`, `0x1420e20d6`.
5. **Live confirmation** (no game was running this session). Next: with a match running, a read-only
   watcher on `MI+0x1588 == 3` / `MI+0x1590` (pattern: DuelsFoulsRE `watch.py`) to catch a CPU dive and its
   yellow plus IFK.
6. **Period ids skipped by the Judge tick** (`MI+0x1168 ∈ {2,6,9,10}`, `0x14050c402..0f`): the meanings
   are not mapped here. Next: cross-reference with the period table in `pes-re-matchlogic.md`.
