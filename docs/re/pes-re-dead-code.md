# PES 2021 dead-code census (DeadCodeRE)

Static, read-only census of the PES2021 live image. Nothing in the game process was read or written for this report.
- **Image:** `~/ghidra-projects/scratch/Unpacker/pes_full.bin`, base 0x140000000, file offset = VA − base.
- **Scratch dir:** `~/ghidra-projects/scratch/DeadCodeRE/`. All scripts and outputs named below are there.
- **Tags:** **OBS** = observed in raw bytes, a capstone decode or a script count. **INF(h/m/l)** = inference with confidence high/medium/low.
- **Tools:** Python 3.14.8, numpy 2.5.4, capstone 5.0.7. Date: 2026-10-14.

> **Denuvo caveat, applies to every verdict below.**
> - The real program entry is reached only through the Denuvo VM (§6.1).
> - About 2.3k function starts are `e9` thunks into `.impdata`.
> - 10,978 graph nodes are mutated or VM-entry bodies. Their edges are kept apart as *untrusted*.
> - The VM bytecode can encode a call or `lea` in a form no byte scan can see.
>
> So **"dead" in this report means: no static reference of any kind was found, including in protected code, after the checks in §6.3**. It does not prove the code never runs. A function the VM alone reaches would still be listed as dead. §6.3 measures how often that happens (the VM-RVA test). It matters for vtables and not for plain functions.

---

## 1. Headline

1. **OBS: almost nothing in the shipped `.trace` code is statically dead.**
   - Of 165,185 function starts, **17 are dead** (9 have no reference at all, 8 form dead islands).
   - Another **757 are reachable only through Denuvo** (protected code, protector-held pointers, or VM RVAs).
   - Another **560 are not functions**: they are Denuvo filler left inside bodies that were moved to `.impdata` (§6.4).
   - Everything else is reachable. It is reached from the CRT entry (55,442), or only through code pointers held in data (108,408).
2. **OBS: player and team AI has zero dead functions.** This covers every vtable and every slot function in `match::ai`, `match::ai::bp`, `match::ai_selector`, `match::player`, `match::anime::action` (including `goal_keeper`), `match::pad`, `match::referee` and `matchPlan`. All of it is reachable from the entry or from data roots (§4). In the whole match-engine band 0x140400000–0x140b00000 there are 16,446 functions and **0 dead** ones.
3. **OBS, contradicts `pes-re-leftovers.md` §2 and `pes-re-officials-ball.md`: the match-ball membership query is not dead.**
   - Its wrapper `0x140ea93c0` (which tail-jumps at `0x140ea942c`) is called twice from `menu::MatchGeneralSettings` vtable slot 27.
   - The body is also jumped to from protected code (§5.1).
4. **INF(h): a static census cannot detect data-gated deadness.**
   - `SubConceptPossessionMfJoinDefenceLine` is statically live: its constructor runs and its vtable is installed (§4.2).
   - It is dead only because the selector never starts it.
   - Every AI "never happens" question therefore needs live coverage, not this census.

---

## 2. Census counts

### 2.1 Population (OBS, `census.py`)

| kind | count | how found |
|---|---|---|
| `.pdata` function starts | 117,032 | non-chained RUNTIME_FUNCTION begins. 40,135 chained fragments were folded into their parents. |
| `.trace` → `.impdata` `e9` thunks | 2,273 | `Unpacker/out/thunks.tsv` |
| leaf functions with no `.pdata` | 45,880 | Linear decode of every byte range no `.pdata` covers. A 16-aligned instruction boundary after `ret`/`jmp`/`int3`/`nop` padding counts as a start. 57 candidates inside switch tables were dropped. |
| **total** | **165,185** | |

### 2.2 Categories (OBS)

| category | meaning | pdata | leaf | thunk | total |
|---|---|---|---|---|---|
| live-entry | reached from the entry set with trusted edges only | 17,965 | 5,000 | 155 | 23,120 |
| live-entry-via-denuvo | reached from the entry set, needing ≥1 edge out of a mutated/VM body | 23,549 | 8,002 | 771 | 32,322 |
| live-data-root | reached only from code pointers or static objects held in `.data`/`.rdata` | 74,933 | 32,159 | 1,316 | 108,408 |
| **denuvo-only** (unresolved) | reached only through `.impdata`-held pointers, undecoded protected code (`lea`/`call`/`jmp` byte patterns) or VM RVAs | 573 | 155 | 29 | **757** |
| **denuvo-residue** (not code) | dead "leaf" whose nearest preceding start is a Denuvo thunk: filler bytes | — | 560 | — | **560** |
| not-code | dead "leaf" that decodes as data (a table after a function) | — | 1 | — | 1 |
| **dead-island** | referenced only by other unreached code | 6 | 2 | 0 | **8** |
| **dead-noref** | no reference of any kind | 6 | 1 | 2 | **9** |

Sum: 165,185.

The two brief categories map onto this split as follows:
- "Reachable only through a Denuvo-mutated or VM-entered routine" = **denuvo-only**, plus the edge-level split live-entry vs live-entry-via-denuvo.
- "Reachable only from a vtable slot never filled": the final run has **0** functions in `dead-vtable-uninstalled` or `dead-vtable-deadctor`. Before the Denuvo roots were added there were 40 unreached vtables (§2.4). Their slot functions now sit in denuvo-only.

### 2.3 Root-set sensitivity (OBS, `sensitivity.py`; Denuvo edges allowed)

| roots | population reached |
|---|---|
| entry only (OEP 0x14159f2bc + 45 exports + TLS callback + PE entry stub) | 55,442 |
| + code pointers / static objects in live `.data` | 144,695 |
| + code pointers in `.rdata` (static tables) | 163,855 |
| + `.impdata`-held pointers, undecoded protected code refs, VM RVAs | 164,607 |

- 19,160 functions depend on `.rdata` pointer tables alone.
- INF(m): those tables are probably used (callback and handler registries), but the census cannot prove it (open item 2).

### 2.4 Vtables (OBS)

- **Total:** 13,816 RTTI vtables. **Reached:**

  | edge set | vtables reached |
  |---|---|
  | entry, trusted edges | 895 |
  | entry + Denuvo edges | 3,129 |
  | + data roots | 13,730 |
  | + Denuvo roots | **13,816** |

- **Before the VM-RVA roots:** 40 vtables were unreached, and **27 of them (67.5%) have their u32 RVA somewhere in `.impdata`**.
  - Same test at addr+3: 0%.
  - Across all vtables: 3.2% exact vs 2.4% at +3.
- INF(h): the classes behind those vtables are built inside VM-protected code. That is why they are classed denuvo-only, not dead (§6.3).

---

## 3. Notable unreachable functions

### 3.1 All 17 dead functions

Columns:
- *nearest named*: the nearest RTTI-slot function below the address. MSVC links functions in object order. INF(l) when the distance is more than 0x2000.
- *evidence*: from `verify_dead.py`, a whole-image byte scan with a decode check.

| addr | kind/size | cat | nearest named (+dist) | nearest caller / why none |
|---|---|---|---|---|
| 0x1401532f0 | pdata 80 B | noref | none below | no code ref. 2 `.impdata` u32 hits (noise level, §6.3). |
| 0x140153340 | pdata 80 B | island | — | falls through from 0x1401532f0 only |
| 0x1402c5880 | pdata 73 B | noref | fox::DataBodySet[0] +0x450 | 0 byte-pattern hits anywhere |
| 0x140323460 | pdata 491 B | noref | fox::EntitySerializerFilter[2] +0xa20 | only its own FH3 IP-to-state RVA (0x142e63598) |
| 0x140323160 | leaf 16 B | island | same object | called only at 0x1403234ab / 0x140323621 (inside 0x140323460) |
| 0x1424072b0 | leaf 32 B | island | CRT tail (INF l) | EH funclet of 0x140323460 |
| 0x1417d3ec0 | pdata 422 B | noref | fox::gr::dg::TextureResource[3] +0x8840 | 0 hits |
| 0x1417d3b90 | pdata 33 B | island | same | called only at 0x1417d3efe (inside 0x1417d3ec0) |
| 0x141b76d10 | pdata 119 B | noref | KS::CThread[0] +0x530 (audio middleware) | only its own chained-unwind records (0x1432991d8…) |
| 0x1422e81c0 | pdata 113 B | island | library tail (INF l) | called at 0x141b76de6 (chain fragment of 0x141b76d10) |
| 0x1422fc650 | pdata 159 B | island | library tail | ← 0x1422e8226 (in 0x1422e81c0) |
| 0x142330f04 | pdata 138 B | island | library tail | ← 0x1422fc6d1 (in 0x1422fc650) |
| 0x14233104c | pdata 47 B | island | library tail | ← `jmp` at 0x1422fc924, an unreached tail node |
| 0x1422e1fdc | pdata 39 B | noref | library tail | 1 byte-pattern hit, not an instruction boundary |
| 0x142295d30 | leaf 16 B | noref | std::collate tail | a `jmp` case label in the middle of a function (INF h: gap-detector artifact, not a function) |
| 0x141f7ae00 | thunk → 0x15b59c0d0 (clean) | noref | FixDemoObjectDataBody[15] +0x15b0 (INF l) | 3 hits, none an instruction boundary |
| **0x1421354e0** | thunk → 0x143d7b035 (**mutated**) | noref | match::ai::bp::ThinkUnitOnePattern[2] +0x1860 (INF l) | 0 hits. Body is mutated, so INF(m) VM-only caller is possible (open item 4). |

INF(h): what is truly dead is
- unused library and serializer helpers (`fox::` serializers, audio middleware threads, CRT/library tails), and
- one Denuvo-mutated thunk.

Nothing in the match simulation is on this list.

### 3.2 Denuvo-only items that touch the match or AI (all in `out/dead.tsv`)

| item | addresses | why only Denuvo reaches it (OBS) | verdict |
|---|---|---|---|
| AI tuning-constant deserializers `constant::Shoot/Support/Throughpass/Flypass/PullAway/Avoid/DiagonalRun/Motivation` (+ `MlScreenShot`, `CameraInplay`) | slots [1]/[2], e.g. Shoot 0x141e61360/0x141e61150, Throughpass 0x141e6ebf0, Support 0x141e77110 | Vtable only in `.impdata`:<br>• u64 copies, e.g. Shoot vtable 0x142b4eb38 @0x15acec363, Support @0x15acf9239, Throughpass @0x15acf370c;<br>• or a `lea rcx,[rip+vt]` in undecoded protected code: Avoid @0x159f399bf, DiagonalRun @0x1471a3bc5, Motivation @0x1504ec2ee. | **unresolved, not dead.** Loaders for live constants (e.g. `constant::Gk`, vtable 0x142b57cd8) are relocated `.impdata` bodies too (lea at 0x15b4487b0). |
| `constant::Ball / Combination / PesSmart` | Ball 0x141e73ee0/0x141e73720, Combination 0x141e642f0, PesSmart 0x141e7f240 (5,906 B) | No `lea`/pointer anywhere. Vtable RVA in `.impdata` only: @0x1499360f2 / 0x15011e56a / 0x14e7281b2. | unresolved (VM-RVA only) |
| `match::record::Observer*`, 13 classes (Demo, EventPlayer, Feint, Foul, FreeKickKind, GoalKickKind, KickerSelect, MatchPhase, MatchTime, OffsideTrap, RestartReady, SetPlayActionDecide, Transition) | slot 1 of each, 0x140a13750…0x140a15e80; two ObserverShoot helpers 0x140a16b00/0x140a172e0 | No vtable `lea` in decoded code. `.impdata` u64/RVA hits (e.g. MatchPhase u64 @0x15ac6878c). | unresolved. These are recording/stats observers, not decisions. |
| `match::Main` (vtable 0x142597020) and helpers | 0x14041e240 [0], 0x14041e2b0 [2], dtor 0x14041e1b0, 0x140435b30, 0x140435ef0 | Only vtable `lea` is in its own dtor 0x14041e1b0. RVA in `.impdata` @0x1577836f8. | unresolved. Never constructed by decoded code. |

---

## 4. AI verdict (the repo priority)

### 4.1 Per namespace (OBS, `aiverdict.py`; shared `ret`/`return false`/`return true` stubs excluded)

| namespace | classes | vtables reached | slot functions by category | **dead?** |
|---|---|---|---|---|
| match::ai | 74 | 74/74 | live-entry 4, via-denuvo 233 | **no** |
| match::ai::bp | 38 | 38/38 | live-entry 6, via-denuvo 76 | **no** (the one nearby dead thunk 0x1421354e0 is not a slot; namespace INF l) |
| match::ai_selector | 2 | 2/2 | data-root 4 | **no** |
| match::player | 119 | 119/119 | live-entry 7, via-denuvo 487 | **no** |
| match::anime::action | 79 | 79/79 | live-entry 7, via-denuvo 386 | **no** |
| match::anime::action::goal_keeper | 15 | 15/15 | live-entry 3, via-denuvo 107 | **no** |
| match::pad (incl. ThinkUnit*) | 79 | 79/79 | live-entry 6, via-denuvo 184 | **no** |
| match::referee | 5 | 5/5 | live-entry 4, via-denuvo 49 | **no** |
| matchPlan (team plans) | 57 | 112/112 | live-entry 44, via-denuvo 9, data-root 322 | **no** |
| match::record | 46 | 46/46 | 13 denuvo-only, rest live | 13 observers unresolved (§3.2) |
| match (top level) | 132 | 143/143 | 2 denuvo-only (`match::Main`) | unresolved (§3.2) |
| constant (AI tuning data) | 87 | 87/87 | 25 denuvo-only | 13 loaders unresolved (§3.2) |

- **Match-engine band 0x140400000–0x140b00000 (OBS):**

  | category | functions |
  |---|---|
  | live-entry | 1,036 |
  | live-entry-via-denuvo | 10,086 |
  | live-data-root | 5,173 |
  | denuvo-only | 21 (the §3.2 `match::Main` and record-observer rows, plus 0x140a3d0c0) |
  | denuvo-residue | 129 |
  | not-code | 1 (0x1405cd0c0: a dword table after a `match::ai` helper, decodes as `lodsd; rcr …`) |
  | **dead** | **0** |
  | **total** | **16,446** |

- **Live AI entry points (OBS).** The documented AI entry points are all `live-entry-via-denuvo`:
  - the tick `0x140925750`;
  - `GetAction` `0x1406d9010`;
  - the sequencer `0x14044c880`;
  - the restart driver `0x14092e7e0`;
  - the referee dispatcher `0x140a38c40`;
  - the strategy decider `0x14051e970`;
  - the reaction table `0x140a06dd0`;
  - keeper deflect `0x1409d75d0`;
  - AfterCatchMove `0x1409e8880`.

  The receive entry `0x1409f8220` is `live-data-root`.

### 4.2 Named AI items

- **`SubConceptPossessionMfJoinDefenceLine` (vtable 0x1425c3d00): statically live, dynamically dead.**
  - OBS: its vtable is installed by `0x1404efab0`, which is reached from the entry through a Denuvo hop. Its slots 0x1404f0d50 / 0x14053d730 / 0x14053d600 / 0x14053d5f0 are live.
  - The "dead, do not port" verdict in `pes-re-team-ai-leftovers.md` §2.2 rests on the selector never starting it. This census neither confirms nor refutes it.
- **Player actions are live.** OBS: `ActionPassAndGo`, `ActionShortPass`, `ActionStopGoal`, `ActionShoot`, `ActionThroughPass`, `ActionLongPass`, `ActionCentering`, `ActionGoalGet`, `ActionKeeperBlockLate` and the others are reached through the no-`.pdata` switch dispatcher 0x1409f8030:
  - `jmp r9` through the RVA table 0x1409f8170, with byte map 0x1409f81c0;
  - case blocks 0x1409f8065… tail-jump to each handler.

  An earlier pass reported them dead (killed, §8.3).
- **Ports may assume** that every AI vtable method and every reachable non-virtual AI function is executable code. Whether a branch is *taken* still needs live evidence.

---

## 5. Calibration against existing docs

### 5.1 Match-ball query: "no static root" is wrong (OBS)

Static reference chain:
1. Factory pointer at `.data` 0x1434a7070 → 0x140ad5890.
2. → 0x140ad5800 → 0x140ad55e0.
3. `lea` of vtable `menu::MatchGeneralSettings` 0x14267a778.
4. Slot 27 = 0x140ad5960 (`0x14267a778 + 27*8` holds 0x140ad5960).
5. `call 0x140ea93c0` at 0x140ad59f9 and at 0x140ad5a22.
6. 0x140ea93c0 ends `pop rdi; jmp 0x1414d6010` at 0x140ea942c.
7. 0x1414d6010: `jmp 0x144a58df0`.

Also:
- 0x140ea9475 does `jmp 0x140ea93c0`.
- Mutated protected code jumps straight to 0x144a58df0 from 0x14eb614e3 / 0x14eb61616 / 0x14eb61762 (untrusted edges).

Why the earlier docs missed it:
- `pes-re-leftovers.md` §2 searched for references to `0x140ea942c`, the tail-jump *site*, and not to the start of the containing function, 0x140ea93c0.
- INF(h): the query backs the Match General Settings ball list.
- The GF conclusion still stands: the match needs only the BallCondition lookup. The word "dead" is wrong.

### 5.2 Tick vtable slot address (OBS)

- `pes-re-tactics-clock.md` §2.5 places the tick at vtable slot 0x1426333B8. In this image the qword there is `0x646e`.
- `0x140925750` is slot 2 of `match::Player` vtable 0x1426329a8 (slot VA 0x1426329b8).
- INF(m): a typo in the doc (…333 vs …329).

### 5.3 Docs to correct (do not edit `docs/re` from here)

- `pes-re-leftovers.md` §2 and `pes-re-officials-ball.md` (line ~58): "no static caller / dead".
- `pes-re-tactics-clock.md` §2.5: slot VA.

---

## 6. Method

### 6.1 Roots (OBS unless tagged)

- **Entry chain:** PE entry 0x15b95f020 → 0x15b95e000 `jmp 0x1498f644c`. That target is a Denuvo body (`lea rsp/push r11/…/pushfq`). No static edge leaves it to game code.
- **OEP:** `0x14159f2bc` = `sub rsp,28h; call 0x1415a01e4; add rsp,28h; jmp 0x14159f150`.
  - The callee holds the MSVC default `/GS` cookie constant 0x2B992DDFA232 at 0x1415a01f8.
  - Zero rel32/u64 references to 0x14159f2bc exist.
  - INF(h): this is `mainCRTStartup`, and the VM enters it.
- **Exports and TLS:** the 45 exports and the TLS callback 0x15b9560e0. The CFG function table is empty (GuardCFFunctionTable = 0), so there is no address-taken list to use.
- **Data roots:** every aligned u64 code pointer in `.rdata` (10,079 targets) and in live `.data`/`.data1`/`.bss`/`.tls$`/`.xtls` (4,039 targets), outside vtable slot ranges.
  - Every vtable pointer stored in live `.data` (4,579) also counts. These are static objects whose dynamic initializer already ran.
  - Each pointed-to address becomes a node even when it has no `.pdata` entry.
- **Vtables:** all 13,816 from `FoxRegistryRE/rtti_all.json`. A vtable is reached only when reached code takes its address (ctor/dtor `lea`), or by a data root. It is never a root by itself. That is what makes the "class never instantiated" category possible.
- **Validation path (OBS, BFS parents).** From the OEP to the match tick:
  - `0x14159f150` → … → `flow::FlowListener` → 0x14112b310 → thunk 0x141129870 → 0x1446a6160;
  - → thunk 0x141129650 → 0x1446a5b20 → **mutated** 0x15b8e6f37 → 0x15b1ebdd2 → 0x15add82ef → 0x15adc8623 → 0x15b8d21cc → 0x144680d58;
  - → 0x141fe20a0 → 0x141fe34c0 → `MatchOnline` vtable 0x142bc0100 → `match::Match` slot 1 0x14041aae0 → entity creation 0x1404187b0 → 0x1409211e0 → 0x14091e9f0;
  - → `match::Player` vtable → **0x140925750**.

  The only untrusted hops are inside the mutated chain.

### 6.2 Edges (OBS: what the scripts do)

- **Decoding:** each node is decoded with capstone.
  - `.pdata` nodes: a linear sweep over [begin, end).
  - Other nodes: bounded recursive descent (≤20k instructions).
- **Edges collected:**
  - direct `call`/`jmp`/`jcc` that leave the body;
  - `lea` of a code address;
  - a rip operand pointing into a vtable (→ vtable node);
  - a rip operand whose qword is a code VA (function-pointer slot, or a Denuvo opaque-branch slot);
  - MSVC switch tables (`lea rB,[rip-X]` = image base, then `[rB+ri*4+T]`): 2,460 tables decoded;
  - fall-through when a `.pdata` body does not end in a terminator;
  - chained `.pdata` fragment → parent;
  - vtable → slots;
  - C++ EH: `__CxxFrameHandler3`-style handlers 0x1415a16fc / 0x14159f34c (FuncInfo magic 0x19930522; unwind actions, catch handlers) and the `__C_specific_handler`-style 0x1415a1738 (scope filters). That is 196,355 EH edges.
  - `_ThrowInfo` behind a `lea` (thrown type's dtor, CatchableType copy constructors).
- **Discovery** of new nodes ran 24 rounds and converged: rounds 18–23 add 8, 5, 5, 4, 1 and 1 nodes, none in `.trace`, and round 24 adds 0.

### 6.3 Denuvo handling (OBS counts, INF where tagged)

- **Trust:** edges out of a body in `.impdata` that is classified `vm` or `mutated` are *untrusted*. The classifier uses `vmscan_all.txt`, and otherwise the `thunks.py` heuristic plus `pushfq`. 10,978 source nodes and 39,952 edges are untrusted.
  - S0 = entry, trusted edges (29,002 nodes).
  - S0u = entry, all edges (89,324).
  - S1 = entry + data roots, trusted edges (226,312).
  - S2 = S1 + Denuvo roots, all edges (244,722).
- **Undecoded protected code:** whole-`.impdata` byte patterns were scanned:
  - `lea r64,[rip+d]` and `call/jmp rel32` landing exactly on a function start or a vtable (3,434 targets);
  - u64 pointers at every byte offset.

  Noise was measured against a control set (target +3):

  | pattern | exact hits | control hits |
  |---|---|---|
  | `call/jmp` | 3,058 | 58 |
  | `lea` → vtable | 645 | 5 |

  These hits feed S2 only.
- **VM RVAs:** u32 equal to an RVA, at every offset of `.impdata` (`vmrva.py`).
  - Unreached vtables: 67.5% exact vs 0% control. Kept as S2 roots for vtables (439 vtables have such a hit).
  - Functions: dead/residue sets sit at 10–12% exact vs 9–10% control, the same as the live sets. **Not used as a function root** (it is noise for functions).

### 6.4 Denuvo filler (`denuvo-residue`)

- **OBS:** the bytes after a thunk's 5-byte `jmp` are not the moved body.
  - Thunk 0x1409d6c20 → 0x1440ecf40: original `9e 0f 88 b2 24 26 00 e9…` vs body `40 18 48 89 50 10 55 53…`.
  - Thunk 0x140a384f0 → 0x144161790: the same mismatch.
  - The filler decodes to branches outside the image, `in`/`out`, `fld` and similar.
- **Classification rule:** an unreached leaf is classed *residue* when either
  - its nearest preceding function start is any 16-aligned `e9` into `.impdata` (this includes thunks that `thunks.py` misses because filler breaks its cc/c3/90 test), or
  - its first instructions are port I/O, privileged, x87, or branch outside the image.
- **Enrichment:** 51% of the dead leaves sat right after a thunk, vs 3–7% of live leaves.
- **Caveat, INF(m):** a genuinely dead leaf placed right after a protected function would also land here.

---

## 7. Reproduce

```
cd ~/ghidra-projects/scratch/DeadCodeRE
python3 census.py                    # ~45 s, 14 workers -> out/nodes.tsv, out/vtables.tsv, out/summary.json, out/graph.pkl
python3 deadlist.py ['REGEX']        # out/dead.tsv: every non-live function + nearest RTTI-named neighbour
python3 verify_dead.py > out/verify_dead.txt   # whole-image byte scan + instruction-boundary check of each dead fn
python3 vmrva.py                     # out/vmrva.txt: VM-RVA enrichment per category, +3 control
python3 sensitivity.py > out/sensitivity.txt
python3 aiverdict.py > out/aiverdict.txt
python3 analyze.py why 0xADDR        # category, root path, inbound edges;  analyze.py refs 0xADDR
python3 refscan.py 0xADDR [...]      # brute-force rel32/rip-disp/u64/RVA scan of the whole image
```

- **Inputs:**
  - `Unpacker/pes_full.bin`;
  - `Unpacker/out/thunks.tsv` and `Unpacker/out/vmscan_all.txt` (from `thunks.py` / `vmscan.py`);
  - `FoxRegistryRE/rtti_all.json` (from `rtti.py dump`).
- **Output columns:**
  - `out/nodes.tsv`: addr, kind, size, body/class, category, vtable slots, first 4 inbound (src/kind/untrusted), rva flag.
  - Edge kinds: 0 call, 1 jmp, 2 lea, 3 vtref, 4 ptr, 5 chain, 6 vslot, 7 eh, 8 fallthru, 11 throwinfo.

---

## 8. Killed hypotheses (each one changed the numbers)

1. **"Unaligned u32 RVAs in `.rdata` that hit a function start are roots (EH funclets)."**
   - Killed: it gave 74,128 hits, mostly FH3 IP-to-state entries and chained-unwind parent records (OBS for 0x140fe69e0 @0x143036020 and 0x141b76d10 @0x1432991d8).
   - Replaced by parsing FuncInfo, scope tables and ThrowInfo. Kept only as a diagnostic flag.
2. **"Linear sweep of `.pdata` ranges is enough."**
   - Killed: the first full run marked 16,772 functions dead.
   - Causes: missing fall-through edges (e.g. 0x14002ba89) and leaf functions with no `.pdata` (e.g. the `std::string::c_str` wrapper 0x140ff6b50 → `jmp 0x1410f7eb0`).
3. **"Recursive descent can stop at an indirect `jmp`."**
   - Killed: the `jmp r9` switch in 0x1409f8030 hides every player-action handler (§4.2).
   - Fixed by decoding MSVC switch tables.
4. **"An aligned byte after `cc` starts a leaf."**
   - Killed: `cc` inside immediates (`0x3d4ccccd`) created 16 false leaves in the `CThroughAnimeInfo` area (0x140849f50…).
   - Replaced by linear decoding of uncovered runs.
5. **"Data roots need an existing node."**
   - Killed: pointers to leaf code that had not been detected were dropped, which made `menu::PkOrder` and its online commands look dead.
   - Every data-pointed and vtable-slot address is now a node.
   - Related bug: capstone prints rip displacements below 10 in decimal (`[rip + 9]`), and the regex missed them (OBS 0x14107a9e0 `lea rax,[rip+9]`).
6. **"`constant::Avoid/DiagonalRun/Motivation` are never constructed."**
   - Killed: a `lea rcx,[rip+vtable]` decodes inside `.impdata` at 0x159f399bf / 0x1471a3bc5 / 0x1504ec2ee.
7. **"`.impdata` RVA hits are generic VM references."**
   - Killed for functions (same rate as the +3 control).
   - Kept for vtables (67.5% vs 0%).
8. **"Leaves after a thunk are the original body's tail."**
   - Killed: the bytes differ from the moved body (§6.4). They are filler.
9. **Prior-doc claims killed:** match-ball "no static root" (§5.1) and the tick slot VA (§5.2).

---

## 9. Open items (exact next steps)

1. **Classes reachable only through the VM** (`constant::Ball/Combination/PesSmart`, the 13 `match::record` observers, `match::Main`).
   - Run `sudo python3 findvt.py <vtables>` from the Unpacker scratch dir during a match. It is read-only and lists heap objects per vtable.
   - Vtables:
     - `0x142b57448 0x142b4ff98 0x142b5c678 0x142597020`
     - `0x1426408b8 0x142640818 0x142640b90 0x142640840 0x1426407c8 0x1426407a0 0x1426406b0 0x142640660 0x142640688 0x142640bb8 0x1426406d8 0x142640728 0x142640700`
   - Any object found means the class is live. None found over a whole match plus menus means dead in practice.
   - Add the 10 other `constant::` vtables from §3.2 to settle whether those loaders run.
2. **The 19,160 functions reachable only through `.rdata` tables.**
   - Next: give each `.rdata` code-pointer run its own node, reached only when code `lea`s inside it. Change `census.py` data-root block: group consecutive pointers into tables, add a `lea`→table edge (like the vtable path).
   - None of the AI depends on this: AI is reached from the entry.
3. **Data-gated deadness inside live AI** (the `PossessionMfJoinDefenceLine` kind).
   - Not reachable by static analysis. It needs a read-only live coverage sampler: per frame, log which concept/sub-concept objects have their start state set. The fields are in `pes-re-team-ai-leftovers.md` §2.2: +0x18/+0x1c/+0x20 on the sub-concept, found by vtable with `findvt.py`. Run it over a full match.
4. **0x1421354e0** (thunk → mutated 0x143d7b035, no references).
   - Run `venv/bin/python emu.py 0x143d7b035` from the Unpacker scratch dir to read its effect.
   - If it is an AI helper, find its VM caller live.
5. **Population blind spot.**
   - Leaves that are not 16-aligned and never referenced are not counted. The vtordisp-style thunk 0x140266b68 (8-aligned) was found only because a vtable points at it.
   - Next: also accept 8-aligned boundaries after `int3` padding in `gap_leaves`, then rerun.

## 10. Files (scratch dir)

- **Scripts:** `census.py`, `analyze.py`, `deadlist.py`, `verify_dead.py`, `refscan.py`, `vmrva.py`, `sensitivity.py`, `aiverdict.py`.
- **Outputs (`out/`):** `nodes.tsv`, `vtables.tsv`, `summary.json`, `graph.pkl`, `dead.tsv`, `verify_dead.txt`, `vmrva.txt`, `sensitivity.txt`, `aiverdict.txt`.
