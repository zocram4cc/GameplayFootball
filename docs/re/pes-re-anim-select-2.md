# PES 2021 — closing the `pes-re-anim-select.md` open items (AnimLeftoversRE)

Read-only RE, 2026-10-14. Image: flat live image `pes_full.bin` (base 0x140000000), decompiles via
`dec.sh` + `annot.py`, raw disassembly via `pdis.py`/`refs.py`. Scratch: the AnimLeftoversRE scratch
dir (`stidx.py`, `slot3.py`, `mgr3.py`, `xglob.py`, `img.py`, `names.py`, `fnaddr.py`, decompiles
`st1..st6`, `sty`, `uc..uc6`, `kick`, `kp`, `ld2/ld3`, `ctx1.txt`, `ctx15.txt`, `ctx1416.txt`,
`slot_all.txt`, `az_sites.txt`, `rt.txt`).

Tags: **OBS** = observed at the named VA / from the named scan. **INF(c)** = inferred, confidence c.
Addresses are image VAs. `P` = `match::AnimePlayer`, `req` = the per-tick move-request struct
(`uint*`, so `req[n]` is byte offset `4n`).

Carried in (not re-derived): `pes-re-anim-select.md` (AS), `pes-re-anim-blend.md`,
`pes-re-locomotion.md`, `pes-re-matchlogic.md` (ML), `pes-re-contact.md`,
`pes-re-stamina-drain.md` (SD), `pes-re-sim-motion-mapping.md` (SMM).

---

## 0. Headline

1. **AS open item 4 is closed for `+0x27` and `+0x2b`; `+0x15` is closed as Defensive Awareness.**
   OBS by the ContactRE method (identify the ability by *what the code does with the byte*, not by
   the name tables): `+0x27` = **Speed**, `+0x2b` = **Acceleration**, `+0x15` = **Defensive
   Awareness**. This kills ML §1.3's caveat O4 and AS §3.5/§4.2's "INF(medium)". Details in §1.
2. **AS open item 2 is closed negatively.** Styles 1–5, 9, 11–14 have **no writer**: an exhaustive
   scan of every `mov` store to the `req[0x37]` slot (`0xdc`) in the whole image finds exactly
   **five** sites, all of which write `FUN_1406ba2f0`'s return value or the 6/7/16 override. The
   other twelve styles are unreachable dead values of `DAT_14344e0e0`. Details in §2.
3. **AS open item 3 is closed: nothing reads `Freemove.bin`.** OBS: the loader
   `FUN_1406abe40` is the only writer of the manager's words `[2]`/`[3]`, and a whole-function
   dataflow scan of all **319** call sites of the manager accessor `FUN_1406abba0` finds **zero**
   reads of `+0x10`/`+0x18`. They are freed and never consulted. Details in §3.
4. **AS open item 7 is closed: `FUN_140a581c0` is the breath (short-term dash reserve) percentage,
   not a condition byte.** OBS from raw disassembly, and it *is* an FNV-guarded accessor like the
   stamina ones, exactly as the task warned. This **corrects** AS §3.5/§3.6/§4.2, which call the
   same byte "condition". Details in §4.
5. **AS open item 5 (open-play kick horizontal alignment) is not closable in a few steps**: the
   writer `FUN_1407302b0` provably does **not** touch `P+0x428` (`AdjustTransXZ`) — it only spreads
   height (`P+0x498`) and yaw (`P+0x4d0`). The alignment lives in the *approach*, and the exact
   site is not identified. Recorded as open with the next step in §5.

---

## 1. AS open item 4 — the ability bytes `+0x27`, `+0x2b`, `+0x15`

Method (ContactRE's, ML EV-CONTACT-0015): find the *semantics* of the consumer. The stat block is
`PMS + 0x70` and every read goes through `FUN_14124dad0` (`lea rax,[rcx+0x70]`), so a read is
`movzx r, byte [rax + idx]`. An exhaustive scan of all **327** call sites of `FUN_14124dad0`
(`stidx.py`) gives the read histogram below; only 0x27 / 0x2b / 0x15 are discussed.

```
n=1 0x11 | 3 0x14 | 13 0x15 | 8 0x16 | 1 0x17 | 1 0x18 | 2 0x19 | 6 0x1a | 4 0x1b | 1 0x1c
n=24 0x1d | 2 0x1e | 2 0x1f | 6 0x20 | 2 0x21 | 4 0x22 | 5 0x23 | 1 0x24 | 1 0x26
n=12 0x27 | 20 0x28 | 3 0x2a | 2 0x2b | 26 0x2c | 2 0x2f | 2 0x30 | 70 0x31
n=24 0x34 | 1 0x35 | 1 0x36 | 1 0x38 | 2 0x39 | 2 0x3a | 3 0x3b
```

### 1.1 `+0x2c` = **Jump** (OBS; already in ML/contact docs, restated as the anchor)

`FUN_140a6de20`'s jump-reach formula (contact §3.1) reads `ab+0x31` (height, cm) and `ab+0x2c`;
`FUN_140a6dfd0`, `FUN_1405fc761`, `FUN_1405fc761`'s set-piece ranking
(`h + jump + heading`, `FUN_1405fc761` at 0x1405fc78a) all confirm 0x2c is the aerial ability.
**Consequence:** ML §1.3's unresolvable pair **Speed↔Jump = {0x27, 0x2C}** is resolved *by
elimination* — 0x2c is Jump, so 0x27 is Speed. That already settles the first of AS's three bytes
without needing Aoba.

### 1.2 `+0x27` = **Speed** (OBS)

Three independent consumers, all locomotion-speed:

| Site | What the byte does | Verdict |
|---|---|---|
| `0x1406f9a2c` (`FUN_1406f9a2c`, the per-frame rate cap of AS §3.5) | `q = clamp((0x27 − 20 − 20·fatigue)/79, 0, 1)`; `rateMax = 1 − 0.085u(G) − 0.1(1 − q²)`. The cap is on **running rate**, and AS §3.5 measured live `P+0x377c` inverting to integers 60/72/77/…/99 for all 25 players | Speed |
| `0x14096d2d0` (`FUN_14096d2d0`) and `0x14096e284` | read 0x27 of **two different players**, subtract, clamp against a constant, and multiply by `FUN_141e532d0()` (a per-frame time) — a **speed differential** fed into a pursuit/closing term | Speed |
| `0x14211cbef` (`FUN_14211cbef`) | 0x27 normalised `(s−40)/(99−40)` and summed with `0x31` (height) and `0x1d` (heading) normalisations into one linear blend, then scaled | a physical "athleticism" blend whose 0x27 term is the Speed member |

The `(s − 40)/59` normalisation and the 40..99 clamp match ML's ability range exactly.
**`+0x27` = Speed: OBS.** Kills ML O4's first half.

### 1.3 `+0x2b` = **Acceleration** (OBS)

Only **two** readers in the whole image, both in the anime rate ramp:

1. `0x1406eab00` (`FUN_1406eab00`, AS §3.6): reads `0x2b`, subtracts `(10 − breath%)` when
   breath ≤ 10, then `a = clamp((0x2b − 40)/59, 0, 1)` (constants 40 `0x14259be2c`,
   99 `0x1425c48cc`, 59 `0x1425cb2fc` — all three read at 0x1406eac05..0x1406eac29) and returns the
   **ramp time constant τ** for the blend rate. A ramp constant is an acceleration.
2. `0x1406e9fb9` (`FUN_1406e9fb9`, the playback helper `vt+0x50`): same byte, same
   `(s−40)/59` clamp, feeding the turn/accelerate/decelerate **playback** formula
   `0.95 + 0.10p² + 0.05t` (AS §4.2). Turn cadence is an agility quantity.

ML §1.3's other unresolvable pair is **KP↔Acc = {0x2A, 0x2B}**. The other member, `+0x2a`, has
only **three** reads and they are all *power/strength* gains, not ramps:

| Site | What the byte does |
|---|---|
| `0x1406d96fb` (tail at 0x1406d99f6) | if `bit 7 of (block+0x40)` then `g = clamp((0x2a − 40)/59, 0, 1)`, `g² = g·g`, `x += 3·0.6·g²` — a **squared** gain added to a speed-like accumulator, gated on a per-player bit |
| `FUN_140816e0f` (0x140816ea6) | `0x2a` read into `in_stack_00000050` and passed as the caller's power argument, then overridden by `*(int*)(rbp+0x30)` if non-negative — a **launch-power** input |
| `FUN_1406d9996` (0x1406d99f6) | the tail above, reached by fall-through |

The asymmetry is decisive: the pair member used as a *time constant* is Acceleration; the member
used as a *squared power gain* is Kicking Power. **INF(high) `+0x2a` = Kicking Power,
`+0x2b` = Acceleration** (matches carrier-ai §369's INF "0x2A Kicking Power" and ML's group
table, which lumps `0x2A, 0x2B` with "KP + Acc"). Kills ML O4's second half and AS §3.6/§4.2's
INF(medium) → **OBS**.

### 1.4 `+0x15` = **Defensive Awareness** (OBS, by elimination from a 3-way pair)

`+0x15` has **13** readers. The decisive ones are the *pair selectors*, which pick between
neighbouring indices on a flag:

- `0x140502c5b` (`FUN_140502a3d`, AI pass-read / interception decision):
  `if (*(char*)(rbp+0x80) == 0) s = ab[0x15]; else s = ab[0x14];` then
  `s = min(max(s, 50), fVar37)` and the shortfall is added to a time budget. **The choice between
  0x14 and 0x15 is a single boolean** — the classic "am I attacking or defending?" switch.
- `0x1405c0630` (`FUN_1405c0630`, 13 call sites, the AI's *interception/duel* judge): reads 0x15,
  normalises `(s − 65)/(−10)` clamped to `[0,1]` (constants 55 `0x14259be38`, 65 `0x14259be48`,
  −10 `0x14259be84`) and **adds `5.0 · q` metres** (`_DAT_14259222c` = 5.0) to a distance
  threshold when the target's per-player value differs. A defensive stat widening an
  interception radius.
- `0x140a543a1` (`FUN_140a543a1`): reads 0x15, adds a **stamina/fatigue** bonus
  (`20·clamp((pct−80)/(−60))` and then `(x−70)/(−25)`), and compares against a per-player
  **reaction-speed table** `*(float*)(player + 0x224 + slot·4)` scaled by 0.6 — i.e. a reaction
  threshold.

ML §1.3 groups `0x14, 0x15, 0x16` as "awareness + GK" (`0x14, 0x15, 0x16` appear together in all
three `.rdata` tier tables). The pair selector picks 0x14 vs 0x15 on a possession/attacking flag,
so they are **Offensive vs Defensive Awareness**, and 0x16 (8 reads, all in the GK decision
functions `0x1409e30b5`, `0x142115aaf`, normalising `(s−lo)/(hi−lo)` into a shot-stopping
threshold) is GK Awareness. Given `0x16` = GK Awareness is the group's odd one out and 0x14/0x15
are the attacking/defending pair: **`+0x15` = Defensive Awareness: INF(high)** — one notch below
0x27/0x2b because the assignment within the pair rests on which branch the boolean takes, not on
an independent semantic test.

**Also settled as a side effect:** AS §4.1/§4.2's `FUN_1406e6660(P, 0x29)` read is
**Balance = 0x29** (contact doc's correction, and ML's `0x29 Bal`, both agree); `0x28` =
Physical Contact (ML, contact §2.1), `0x34` = the stance/technique byte ML leaves open.

---

## 2. AS open item 2 — writers of blend styles 1–5, 9, 11–14

**There are none. The twelve styles are dead entries of the style table.**

### 2.1 The scan (OBS)

The style is `req[0x37]`, i.e. a `dword` at byte offset **0xdc** of the request struct. `slot3.py`
byte-scans the whole `.trace` for the literal `dc 00 00 00` and re-disassembles backwards to the
enclosing instruction, keeping only `mov [reg+disp], reg` stores. Over `0x140001000..0x142530000`
(1163 raw hits) the anime-player range yields **five** stores:

```
0x1406c0f05  mov dword ptr [rsi + 0xdc], ebp    FUN_1406c0650   RUN_LOOP handler  -> 6 / 7 / 0x10
0x1406c586f  mov dword ptr [rdi + 0xdc], eax    FUN_1406c4f59   STEP_LOOP_CHANGE -> FUN_1406ba2f0()
0x1406d21fe  mov dword ptr [rbx + 0xdc], edx    FUN_1406d2030   loop entry        -> FUN_1406ba2f0()
0x1406e0b28  mov qword ptr [rcx + 0xdc], rdx    (constructor, zeroing — not a style)
0x1406f0056  mov dword ptr [rcx + 0xdc], edx    FUN_1406effb0   unrelated flag block at +0x3754
```
(A further scan of `0x140700000..0x140740000`, the user-control sub-controller range behind
`P+0x3725`, adds only **reads**: `0x1407042a4 mov eax, dword ptr [rbp+0xdc]`.)

### 2.2 What each writer can produce (OBS)

`FUN_1406ba2f0` (AS §3.3, decompile confirmed here) returns exactly five values:
`0xf` (not open-play phase 1 / cmd 0x4a / scripted slot owned), `0xd` (stamina < 20),
`0xa` (accelerate-request nested branch), `8` (dribble, ball within 3 m), `0` (otherwise).
`FUN_1406c0650` overrides to `6`, `7` or `0x10` only (the jostle-guard loop styles).
So the reachable set is **{0, 6, 7, 8, 10, 13, 15, 16}**.

### 2.3 The table has 17 entries and 9 of them are unreachable (OBS)

`DAT_14344e0e0` (style → parametric group ordinal, read at `0x1406b88bb`, the single consumer,
`FUN_1406b8761`, which indexes it with `req[0x37]` and calls `SetParametric`):

| style | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| group | 80 | **85** | **88** | **88** | **89** | **89** | 82 | 84 | 81 | **93** | 86 | **90** | **90** | 91 | **92** | 87 | 83 |
| writer | ✔ | — | — | — | — | — | ✔ | ✔ | ✔ | — | ✔ | — | — | ✔ | — | ✔ | ✔ |

Bold = no writer exists. Groups 85, 88, 89, 90, 92, 93 (SMM §3's "85–86 `near_run_*`",
"88–90 sidestep (`walkparallel`/`heisou`/`parallel_*`)", "87/91/92 relax/tired", "93 `run_4_4_v2`")
are therefore **present in the blend table but never selected by the AI move path**.

**Conclusion (OBS):** AS §3.3's parenthetical "(near-run, parallel, heisou, tired v2) are set by
other callers not traced here" is wrong — there are no other callers. The correct reading is that
PES ships the groups and their style slots but the 54 Hz MOVESTEP FSM only ever asks for eight of
them. A reimplementation must map only the eight reachable styles; mapping the other nine would
be inventing behaviour.

**INF(high):** the slots survive because `DAT_14344e0e0` is authored data (17 style rows) while the
chooser was only ever wired for the eight that the current animation set needs; the unused groups
are the near-run/parallel sidestep set that older PES titles used. Not verifiable without the
authoring pipeline.

---

## 3. AS open item 3 — who reads `Freemove.bin`

**Nothing. The runtime copies are written once and freed.**

### 3.1 The object and its writers (OBS)

The anime-table manager is the global at `_DAT_1436f2378`, returned by the 8-byte accessor
`FUN_1406abba0` (`mov rax, qword ptr [rip+0x30467d1]`, `ret`). It has exactly **ten** RIP-relative
references in the image (`xglob.py 0x1436f2378`): seven in `.trace`
(`0x1406aa29b`, `0x1406aa35b` — the loaders; `0x1406ab2bb`, `0x1406ab32c`, `0x1406ab4dc`,
`0x1406ab6bd` — teardown; `0x1406abba1` — the accessor) and three in `.impdata` inside one
116-byte thunk at `0x143fabea0`.

`FUN_1406abe40` (the only caller of the loader, from `FUN_140419621`) stores:
word `[0]` = Animetable object, `[1]` = 0x2a78-byte table (`FUN_1406a9f20`), **`[2]` = 0xf220 bytes
(3874 × 16 = Freemove.bin)**, **`[3]` = FreemoveOption**, `[4]`. Teardown `FUN_1406ac3c0` frees
`[0]`, `[1]`, `[2]` with exactly those sizes and never touches `[3]`.

### 3.2 The reader scan (OBS)

`FUN_1406abba0` has **319** call sites. `mgr3.py` maps each to its `.pdata` runtime function,
disassembles the **whole** function, tracks which register holds the manager pointer across
`call`/`mov`-copies, and reports any `[mgr + 0x10]` / `[mgr + 0x18]` access. Result: **zero**
hits. The four `[rax+0x18]` reads it does find (`FUN_1407c1f40`, 0x1407c25ab/25c2/2679/2694) are
`call qword ptr [rax + 0x18]` — a **vtable** slot on a different object, not the manager's word 3.

Combined with the AnimSelectRE-2 finding that no MOVESTEP handler reaches the word and that the
`[FMOVE_*]` debug strings have no code xrefs, this closes the item: **`Freemove.bin` and
`FreemoveOption.bin` are loaded, kept alive for the process lifetime, and never read.** A
reimplementation should not implement a Freemove FSM at all — that is the single largest
correction this item delivers (it kills AR §4.3.3 / SMM §0.6 / LOC §5.2, already flagged there).

**INF(low):** the table is probably read by code that is not in this image's `.trace`/`.impdata`
(debug builds, or a cut-down executable), or is pure legacy. Not worth more steps.

---

## 4. AS open item 7 — what `FUN_140a581c0` is

**It is the breath (short-term dash reserve) percentage — an FNV-guarded accessor, not a
condition byte.** AS §3.5/§3.6/§4.2 mislabel it.

### 4.1 Raw disassembly (OBS, `pdis.py 0x140a581c0`)

```
FUN_140a581c0:
  call 0x140a58550            ; stamina %  (SD §1)
  movzx edx, al
  mov  ecx, 0x14              ; 20
  cmp  al, cl
  cmovb edx, ecx             ; edx = max(stamina%, 20)
  imul edx, eax, 0x780       ; × 1920
  mov  eax, 0x51eb851f        ; 2^37/100
  mul  edx
  mov  r11d, edx
  shr  r11d, 5                ; r11 = max(stamina%,20)*1920/100
  test r11d, r11d
  jne  0x140a581ff
  xor  al, al ; ret                     ; denominator 0 -> 0
0x140a581ff:
  mov  eax, [rbx+0x114] & 3              ; guarded-u32 ring slot
  mov  eax, [rbx + rax*4 + 0x118]        ; ring[slot]
  ...
  cmp  qword ptr [rbx + 0x108], rcx      ; FNV-1a-64 hash check
  jne  0x140a582f7                       ; mismatch -> trap
  imul eax, dword ptr [rsp+0x48], 0x64   ; B * 100
  cdq ; idiv r11d
  ret                                    ; eax = B*100 / (max(pct,20)*1920/100)
```

This is **literally** SD §1's row: *"breath % | `bp = B*100 / (max(pct,20)*1920/100)` |
0x140a581c0; `0x780` = 1920 from 0x140a581b0"*. The task's warning was right: it is a guarded
(FNV-1a-64, ring of 4) u32 accessor, byte-for-byte the same scheme as `FUN_140a58350`
(stamina cur) and `FUN_140a58450` (stamina max) — the decoders at `0x140a58550` and here share
the same 40-line unscramble (`e[i] = ring[slot&3]`, `k = key*3`, `kb[i] = ((k+4i)>>8i)&0xff`,
`v[i] = ((e[i]+kb[i])&0xff) ^ ((kb[i]*0x3f)&0xff)`, valid iff FNV-1a-64 == hash, else `int3`).
The only difference from the stamina getters is the *denominator*, which mixes in the stamina %.

### 4.2 Correction to AS

AS uses `c = FUN_140a581c0()` as a "condition/fatigue" byte:

| AS § | AS formula | Correct reading |
|---|---|---|
| §3.5 | `fatigue = clamp((20 − c)/20, 0, 1)`, `s = ability(0x27) − 20·fatigue` | breath < 20 % removes up to 20 Speed points |
| §3.6 | `Acc' = Acc − (10 − c)` when `c ≤ 10` | breath ≤ 10 removes up to 10 Acceleration points |
| §4.2 | same `(10 − c)` on the playback stat | same |

So the "fatigue" that damps locomotion is **the short-term dash reserve running out**, not
long-term condition/stamina. That is a materially different mechanic for a reimplementation:
drained breath (sprint bursts, `FUN_140a58f60`) is a *separate tank* from stamina, and it is the
one that caps top speed and slows the rate ramp. Stamina enters locomotion separately, via
`FUN_140a58550` (stamina %) at AS §2.4's "stamina > 19" and §3.3's "stamina < 20 → style 0x0d".

**INF(high):** this also explains AS §7.1.5's residual pattern (all residuals low-side, Acc read
from playback being "the same reduced byte only when sampled at the same moment") — the reduction
is on the breath-derived byte, which moves within a tick, so a playback-sampled Acc is a
*different* quantity from a ramp-sampled one unless both are read in the same tick.

---

## 5. AS open item 5 — open-play kick horizontal alignment: **still open**

Closed negatively, i.e. the alignment is provably **not** in the kick writer.

### 5.1 What the writer does not do (OBS)

`FUN_1407302b0` (AS §6.2 step 7, decompiled in full here) touches only two adjusters:

- `P+0x498` — `FUN_141eb3830(plVar1, *(u32*)(param_2+0xc), 0, iVar25, 0)` then
  `FUN_141eb3ae0(plVar1)`, guarded by `if (iVar25 = FUN_1406b10d0(motion, rate, 0, 0) < 1) skip`.
  `iVar25` is the hit-frame tick count. This is the **height** spread AS documented.
- `P+0x4d0` — `FUN_141eb35a0(param_2+0x4d0, yaw, targetYaw, round(54·12/60), …)` then
  `FUN_141eb3ae0`. The **yaw** spread, 11 ticks.

`grep` over the decompile finds **no** reference to `P+0x428` (`AdjustTransXZ`) or to
`FUN_141eb39a0` anywhere in the function. `P+0x428` has 58 `FUN_141eb39a0` call sites image-wide
(`az_sites.txt`), and the open-play kick region `0x14072xxxx–0x14074xxxx` contributes
`0x14073417f`, `0x14073d15c`, `0x14073d729`, `0x14073d9e7`, `0x14073dc13`, `0x14073ec0f`,
`0x14073f3eb`, `0x14073f666`, `0x140740009` — **none inside `FUN_1407302b0`**
(`0x1407302b0..`, mapped by `.pdata`). Likewise the root-target solver `FUN_1406e85b0`
(46 call sites, `rt.txt`) is absent from the kick writer.

So AS §6.2's item 7 stands as written: "their horizontal alignment comes from the approach, not
traced". Confirmed negatively.

### 5.2 The next exact step (not done — out of budget)

The candidate is the **approach target**, i.e. the AI move request the carrier receives *before*
the kick, not the kick exec. Concretely: the two `AdjustTransXZ` sites nearest the kick writer on
the *carrier* side are `0x14073f3eb` / `0x14073f666`, both inside the dribble-touch writer
`FUN_14073f750` (SMM §6.4), and `0x140742923` / `0x1407443fb` are in the same
`0x14073xxxx–0x14074xxxx` band. The open question to answer next is whether the carrier's
**move-command target** (`cmd+0x14`, the field AS §1.1 reads as the move target) is offset so
that the *contact foot* lands on the ball, or whether the root is pulled by an `AdjustTransXZ`
registered earlier and merely *completed* by the time the kick's hit frame arrives. Deciding that
needs the hit-frame-to-approach-window timing, i.e. `FUN_1406b10d0`'s tick count compared with the
MOVESTEP dwell of AS §5.2 — one decompile of `FUN_1406b10d0` plus the two `FUN_14073f750`
`AdjustTransXZ` argument blocks.

**Left open on purpose.** No claim is made about the horizontal mechanism.

---

## 6. Ledger

| ID | Claim | Evidence | Confidence | Dependents |
|---|---|---|---|---|
| AL-01 | in-match stat `+0x27` = **Speed** | 0x1406f9a2c rate cap; 0x14096d2d0/0x14096e284 speed differential; 0x14211cbef athleticism blend; 0x2c proven Jump (contact §3.1) | OBS | AS §3.5, ML O4, LOC O2 |
| AL-02 | in-match stat `+0x2b` = **Acceleration** | 0x1406eab00 τ (0x1406eac05..29 `(s−40)/59`); 0x1406e9fb9 playback; pair member 0x2a used only as squared power gain | OBS | AS §3.6/§4.2, ML O4 |
| AL-03 | in-match stat `+0x2a` = **Kicking Power** | 0x1406d96fb `+3·0.6·g²` gated on bit 7 of block+0x40; 0x140816e0f launch-power arg | INF(high) | carrier-ai §369 |
| AL-04 | in-match stat `+0x15` = **Defensive Awareness** | 0x140502c5b boolean pair selector 0x14/0x15; 0x1405c0630 +5 m interception radius; 0x140a543a1 reaction table | INF(high) | AS §4.2 note, ML §1.3 |
| AL-05 | in-match stat `+0x16` = **GK Awareness** | 8 reads, all GK decision code (0x1409e30b5, 0x142115aaf) | INF(high) | ML §1.3 |
| AL-06 | only 8 of 17 blend styles are reachable; 1–5, 9, 11–14 have no writer | `slot3.py` whole-image scan: 5 stores to `req[0x37]`, all traced | OBS | AS §3.3, SMM §6.8 |
| AL-07 | the 9 dead styles map to groups 85, 88, 88, 89, 89, 93, 90, 90, 92 | `DAT_14344e0e0` read at 0x1406b88bb | OBS | SMM §3 |
| AL-08 | nothing reads `Freemove.bin` / `FreemoveOption.bin` | 319 call sites of `FUN_1406abba0`, whole-function dataflow scan: 0 reads of +0x10/+0x18; teardown `FUN_1406ac3c0` frees but never reads | OBS | AS §0.1/§10.6, AR §4.3.3, SMM §0.6, LOC §5.2 |
| AL-09 | the manager global `_DAT_1436f2378` has exactly 10 RIP xrefs | `xglob.py` | OBS | AS §10.6 |
| AL-10 | `FUN_140a581c0` = **breath %** `B*100/(max(stamina%,20)*1920/100)`, FNV-guarded | raw disassembly 0x140a581c0..0x140a582f6; identical scheme to SD §1's guarded u32 | OBS | **corrects** AS §3.5/§3.6/§4.2 |
| AL-11 | the locomotion "fatigue" term is dash-reserve depletion, not condition | AL-10 + the three AS call sites | INF(high) | AS §3.5/§3.6/§7.1.5 |
| AL-12 | `FUN_1407302b0` sets no horizontal correction | decompile: only `P+0x498` (height) and `P+0x4d0` (yaw); no `P+0x428`/`FUN_1406e85b0` reference | OBS | AS §6.2 item 7 |

## 7. Killed / corrected hypotheses

| Hypothesis | Killed by |
|---|---|
| ML O4: "0x27↔0x2C and 0x2A↔0x2B cannot be separated" | 0x2c is Jump (contact §3.1, existing); 0x2b is a ramp time constant while 0x2a is a squared power gain (AL-01/02/03) |
| AS §3.3: "Styles 1–5 / 9 / 11–14 are set by other callers not traced here" | there are no other writers; the style table has 9 dead rows (AL-06) |
| AS §10.6: "No reader [of Freemove.bin] was located" (left as open) | located: there is none, over a whole-image dataflow scan of all 319 accessor sites (AL-08) |
| AS §3.5/§3.6/§4.2: `FUN_140a581c0` is a "condition byte" | it is the breath/dash-reserve percentage; AS's "fatigue" is short-term reserve depletion (AL-10/11) |
| AS §6.2 item 7: "open-play kick horizontal alignment comes from the approach" | confirmed negatively for the writer; the approach mechanism itself remains open (AL-12) |

## 8. Still open, with the exact next step

1. **Open-play kick horizontal alignment** (§5.2): decompile `FUN_1406b10d0` (hit-frame → ticks)
   and the two `AdjustTransXZ` argument blocks in the dribble-touch writer `FUN_14073f750`
   (0x14073f3eb / 0x14073f666); compare the tick count with the MOVESTEP dwell to decide whether
   the root is pulled by an earlier-registered `AdjustTransXZ` or by an offset move target.
2. **`+0x15` vs `+0x14` ordering** (AL-04) is INF(high), not OBS: a runtime test that reads
   `PMS+0x70+0x14` and `+0x15` for a known attacking and a known defending player would settle it
   in one read-only `pread`.
3. **`0x2a` vs `0x2b`** (AL-02/03) rests on "time constant ⇒ acceleration, squared gain ⇒ power".
   A cleaner split exists but was not searched: the `.rdata` familiarity/condition tier maps
   (`0x142632F74`, `0x142632EB0`, `0x142632CC0`) were decoded only as far as confirming that
   `0x2A, 0x2B` share a group; a scan for a table that *orders* them (e.g. a "which of the two is
   the acceleration bar" UI table) would make AL-03 OBS.
4. **The 9 dead blend groups** (AL-07): their member names are readable from
   `Parametricblendtable.bin` but nothing selects them; not worth further steps for a
   reimplementation.

## 9. Scratch

All files in the AnimLeftoversRE scratch dir. Tools written this session (reusable):
`img.py` (flat-image reader/dumper/strings), `names.py` (resolve a pointer array to strings),
`fnaddr.py` (VA → `.pdata` runtime function), `stscan.py` (fast rel32 call-site finder),
`stidx.py` (call-site scan + stat-index histogram), `slot3.py` (byte-scan for a struct-field
displacement and recover the enclosing store/load — this is the one that closed item 2),
`xglob.py` (RIP-relative xrefs to a global — closed item 3), `mgr3.py` (whole-function register
dataflow for a returned pointer's fields).

Note: `xglob.py` is deliberately **not** named `glob.py` — that shadows the stdlib module
`pathlib` imports and breaks `capstone`.