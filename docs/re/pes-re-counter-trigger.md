# PES 2021 counter trigger: `T+0xa508` and the mark-spot blend rate

Reverse-engineering report (CounterTriggerRE, continuing the predecessor of the same name).
Binary: the unpacked PES 2021 image, base `0x140000000`. Notation as in the other `pes-re-*`
reports: `T` = the per-team record, `T = FUN_140a48a30(G, side)`, `G = *(u64*)0x1436F3DC8`,
`MA = *(G+0x18)`, `MI = *(G+0x270)`, `F` = the pitch table `*(G+0x2aa8)` / `FUN_140a57800(*(G+0x40))`.
Every claim is tagged **OBS** (read out of the image, with the address given) or **INF** (inference).

Scripts and intermediate decompiles live in the CounterTriggerRE scratch dir
(`scan2.py` displacement scanner, `fd.py` full-function disassembly with RIP-constant
resolution, `cst.py` float-constant reader, `callers.py` call-site finder, `fit2.py` / `fit3.py`
live refits, plus the `a508*.c` / `spot.c` / `blend.c` Ghidra output).

---

## 1. What enables a counter-attack: the writer of `T+0xa508`

### 1.1 Where the field is written (OBS)

A displacement scan of the whole `.text` for memory operands carrying `0xa508` gives, in total:

| address | instruction | containing function | role |
|---|---|---|---|
| `0x1404171f9` / `0x1404171ff` | `mov eax,[rsi+0xa508]` / `mov [rdi+0xa508],eax` | `FUN_1404161e0` | team-record **copy/assignment** — bulk-copies `0xa508`, `0xb834`, `0x26c` and neighbours |
| `0x1405819c0` | `mov [rcx+0xa508],eax` | `FUN_140581980` | **the runtime writer** |
| `0x142155590` / `0x14215e89c` (and `0x142154eaf`, `0x14215e005`) | load/store through the reflection record | `FUN_142153ed0` / `FUN_14215cf10` | **save/load (replay) serialiser** only |
| `0x14059be76`, `0x140606d9e`, `0x140665312` | `mov/cmp [rax+0xa508]` | `FUN_14059ba60`, `FUN_140606cd0`, `FUN_140664ee0` | the three runtime **readers** |

There is exactly **one** runtime writer: `FUN_140581980`. It has exactly one call site,
`0x1404f6f42` inside `FUN_1404f6c30`, which is the per-frame team-brain tick (it walks both
teams from `FUN_14041ecf0`, the two brains at `+0x450` / `+0x478`, gated on `MI+0x1168 != 10`;
the call to `FUN_140581980` sits in the `MI+0x1775 != 0x17` branch of that tick).

**OBS.** The call and the argument:

```
cStack_396 = *(char *)(*(undefined4 **)(param_1 + 0x6d50) + 0x255);   /* T+0x255 = in possession */
bVar22    = cStack_396 != 0;
...
FUN_140581980(param_1 /*brain*/, bVar22 /*inPossession*/);
```

`brain+0x6d48` = ctx, `brain+0x6d50` = T, `brain+8` = side.

### 1.2 The writer body (OBS, `FUN_140581980 @ 0x140581980–0x140581a3b`)

```c
uVar3 = *(int *)(brain + 8);              /* side                        */
cVar2 = FUN_1405807d0(ctx, side, inPossession, 0);
if (cVar2 == 0) { uVar3 = 0; *(int *)(brain + 0x6cf0) = 0; goto WRITE; }
cVar2 = FUN_140580a50(ctx, side);
if (cVar2 == 0) {
    cVar2 = FUN_140580980(ctx, side);
    if (cVar2 == 0) goto SET1;             /* both sub-tests fail -> 1    */
    uVar3 = 2; *(int *)(brain + 0x6cf0) = 0x30;   /* 48-frame latch     */
} else {
SET1: uVar3 = 1;
}
if (*(int *)(brain + 0x6cf0) != 0) { *(int *)(brain+0x6cf0) -= 1; *(int *)(T + 0xa508) = 2; return; }
WRITE: *(int *)(T + 0xa508) = uVar3;
```

**Consequence (OBS):** `T+0xa508 == 0` happens **only** when the master gate
`FUN_1405807d0` fails. If the gate passes the value is *always* ≥ 1 — the two sub-tests only
decide between 1 and 2, they can never produce 0.

So the field is a three-state per-frame byte:

* `0` — the master gate is closed;
* `1` — the ordinary case;
* `2` — one of the "special situations" in §1.4 fired within the last `0x30 = 48` brain ticks
  (~0.9 s at 54 fps), or is still latched by `brain+0x6cf0`.

**INF(med-high):** 48 ticks is the latch length; the latch is set only on the rising edge of a
special situation and then simply counts down, so `2` means "a special situation happened at
most 48 ticks ago".

### 1.3 The master gate `FUN_1405807d0` (OBS, `0x1405807d0–0x1405808b1`)

```c
if ((MI[0x116c] == 1 ||                                   /* ball in play            */
     (MI[0x11d4] == 2 && (h = FUN_1405969b0(ctx)) < 0x16 &&
      *(char *)(FUN_140a48910(G, h) + 1) == 0x28 /*'(' = anime 40*/)) &&
    inPossession != 0 && param_4 == 0) {
    carrier = FUN_140a627e0(ctx, 0);                       /* MA+0x2601c, 0xff if none */
    holder  = *(uint *)(MA + 0x2483c);
    if (carrier > 0x15 && holder > 0x15) {                /* loose ball              */
        u = T(side)   +0x78; if (u  > 0x15) return 1;
        v = T(side^1) +0x78; if (v < 0x16 &&
            (dist(MA+0x24840, u) - dist(MA+0x24840, v)) > 30.0 /*0x142591140*/) return 1;
    }
    if (carrier > 0x15 && holder < 0x16) {                /* we hold it              */
        P  = FUN_140a48a10(G, holder);  dir = T(side) +0x254;  Fp = F;
        if (fabsf(P[0x55c] - Fp[0]) > 15.0 /*0x14259be0c*/ &&
            FUN_140a4c6e0(Fp, P + 0x55c, dir, 0) == 0) return 0;
    }
    return 1;
}
return 0;
```

`MI+0x116c == 1` is the in-play phase (`pes-re-stamina-drain.md` §3.4, `pes-re-tactics.md` §3).
`MI+0x11d4 == 2` is the staged-restart state (`pes-re-camera-target.md`, §2.2) and anime `0x28`
on the current holder is a **keeper holding the ball** — this is the one path that lets the
gate open outside open play.

**The two conditions that make `T+0xa508` 0, in order of likelihood:**

1. **the team does not have the ball** — the writer is only called with `T+0x255 != 0`
   (`OBS`, call site `0x1404f6f42`);
2. the ball is dead (`MI+0x116c != 1`) and it is not a keeper-hold at `MI+0x11d4 == 2`;
3. the "stale ball" veto — we are recorded as in possession but the holder's ball reference
   (`P+0x55c`) is more than 15 m from the real ball and `FUN_140a4c6e0` says we are not inside it.

**Not a tactics toggle.** Nothing in `FUN_1405807d0`, `FUN_140581980` or any of the
sub-tests reads an Attacking-Style / team-sheet option except `FUN_140a62180(ctx, side, {0,1})`
inside `FUN_140580a50`/`FUN_1405a58b0`, which selects *distances*, not the gate. So the
**Attacking Style option `k0` (Counter Attack vs Possession Game) is not what sets `a508`** —
it only decides which concepts the SubConcept registry will then consider. That closes the
"tactics toggle" hypothesis from the open list in `pes-re-transitions.md` §9.

### 1.4 Value 1 vs value 2 — the nine special situations (OBS)

`FUN_140580980` (0x140580980–0x140580a47) is a plain OR-chain of nine tests, **gated by the
attack level**:

```c
if (*(char *)(FUN_140a48a30(G) + 0xb8a3) != 0) return false;   /* attack level 0  */
return FUN_140581610(ctx,side) || FUN_1405a4b90(ctx,side) || FUN_140581370(...) ||
       FUN_140581770(...) || FUN_140580ef0(...) || FUN_140581460(...) ||
       FUN_140581190(...) || FUN_140580c10(...) || FUN_1405a58b0(...);
```

`T+0xb8a3` is set in the same tick at `0x1404f6e4f` / `0x1404f6e58`
(`cmp dword ptr [rax+0xb8a4],0` / `mov byte ptr [rax+0xb8a3],1`, with the clearing store
just before): `T+0xb8a3 = (T+0xb8a4 < 1)` — i.e. **the lowest attack level disables value 2
entirely**. (`T+0xb8a4` is the attack-level field whose in-play writer is `FUN_1404f8350`,
`pes-re-tactics.md` EV-TAC-0005.)

Summary of the nine (all `OBS` from `a508d.c`; only the discriminating condition is listed):

| test | discriminating condition |
|---|---|
| `FUN_140581610` | `MI+0x11a0 ≤ 3·fps`, `T+0xa50c ∈ {3,4}` (ball-zone index), and the holder's slot depth beats ours by > 30 (`0x142591140`) |
| `FUN_1405a4b90` | ours or theirs `T+0xa50c ∈ {3,4}` + ball in our half + a run 2 s behind the last event + the holder ≥ 7 m from the point; or the holder's anime is `0x09` with `FUN_140a510f0 == 3` and a ≥ 7 m distance |
| `FUN_140581370` | the line height `T+0x875c` is ≥ 6.0 m behind `dir·T+0x875c·?` and the band depth (`T+0x8898`) ≤ `0x142596e6c`, and `FUN_1405806c0` (common helper, also used by 3 more tests) holds |
| `FUN_140581770` | ball within `7.5 − 0x14259ce3c` of our goal **or** their `T+0xa50c == 3`, carrier exists, `FUN_1405806c0`, and no opponent inside the 140 / 110 cones |
| `FUN_140580ef0` | holder's lane ≥ 60 m ahead of ours, ball within `0x14259bdf4 = 8`, holder within 8 m of the ball (`0x14259be44`), and no opponent inside the 160 / 80 cones |
| `FUN_140581460` | `FUN_1405806c0`, and an opponent ≥ 3 m goal-side inside a 90 / 25 cone |
| `FUN_140581190` | ball in our half and ≥ 12 m upfield, then `T+0xa50c == 3` with `|ball.z| ≤ 12` or a fallback on `FUN_14043a0a0(T+0x250)` ≤ 100 |
| `FUN_140580c10` | `FUN_1405969b0` valid, a recent possession event, ≤ 3 opponents in the pressing zone, `|ball.z| ≤ F[0x1c]`, and **fewer than 3 team-mates goal-side within `0x14259be7c`** |
| `FUN_1405a58b0` | instruction `0xd` armed, `MI+0x11a0 ≤ 4·fps`, `FUN_140a62180(ctx,side,0) == 0`, `T+0xb8a4 > 1`, ball goal-side of the halfway line, and at least one opponent goal-side of it |

`FUN_140580a50` (the value-1 shortcut) returns 1 immediately when
`FUN_140a74ec0(...) < 5·fps` (the kick-off timer — the first 5 s of a half), or when the
carrier is a keeper (`T+0x4c00[slot] == 0`), and otherwise tests ball depth against
18 m (24 m when `FUN_140a62180(ctx,side,1) != 0`).

### 1.5 What the value enables (OBS)

* `FUN_140606cd0` (`0x140606d9e`, the space-run candidate search used by the counter concept
  slot): `if (*(int *)(T + 0xa508) == 0) { uVar20 = 0; } else { … }` — it then requires
  `T+0x255` (in possession), a valid `FUN_1405969b0` holder, and scans the on-pitch list for a
  player whose `T+0xa2a0` action is `0x1e SPACE_RUN_CHANCE` with `T+0x28c == 2`. This is the
  runner pick of `pes-re-transitions.md` §3.1 step 6 and the chance mask of
  `pes-re-off-ball.md` §"Chance mask".
* `FUN_14059ba60` (`0x14059be76`): reads the value into a local and uses it as
  `if ((cVar3 == 0) && (iStack_2c0 != 2))` — an extra branch that only opens at **2**.
* `FUN_140664ee0` (`0x140665312`): `if ((iStack_cc == 2) && FUN_1406612b0(...) && !cVar5 &&
  !FUN_140a63950(...,6,0xff)) { … }` — again a branch that needs **exactly 2**.

So: `1` = "the ordinary counter/transition concepts may be considered", `2` = "a special
situation just fired" and unlocks two extra decision branches in the off-ball and pass/decide
passes. **INF(med):** the nine tests are the situations the game considers worth a fast
break (deep into the opponent half with numbers, ball-side overload, etc.).

### 1.6 Why the live capture showed `a508 = 0` for the Counter-Attack team

`pes-re-transitions.md` §7.2 records `T+0xa508` = 1 / 0 for the two teams of the second-half
capture. With §1.3 decoded, 1 for team 0 is exactly "in possession, ball in play", and 0 for
team 1 is exactly "out of possession" (team 1 was set to All-out Defence + Counter Attack, so
it spent the window without the ball). The capture therefore does **not** show that the field is
hard to set — it shows it is a *possession-gated* field.

**INF(med-high):** to catch a `SideCounter` episode live one needs a window where the
Counter-Attack team wins the ball and keeps it for at least a few seconds while `MI+0x116c == 1`
and `MI+0x11a0 ≤ 3·fps`; the concept's own window (§3.1 step 1) is ≤ 3 s after the change and
needs the ball in the team's own half, so the practical recipe is to force turnovers in the
defending third.

---

## 2. The mark-spot blend arithmetic

### 2.1 The stored rate is telemetry, not a blend input (OBS)

The scan for `0xb834` over the whole `.text` returns **only**:

| address | instruction | function |
|---|---|---|
| `0x1404171f9` / `0x1404171ff` | copy `T+0xb834` in the team copy/assignment | `FUN_1404161e0` |
| `0x140624235` | `movss dword ptr [r14 + rax*4 + 0xb834], xmm0` | `FUN_140623750` |
| `0x1406259e0` | `movss dword ptr [rsi + rax*4 + 0xb834], xmm11` | `FUN_140623750` |
| `0x140a41672` | `lea eax, [rbx + 0xb834]` | `FUN_140a415a0` (address of the array) |
| `0x142154eaf` / `0x14215e005` | reflection read / write | `FUN_142153ed0` / `FUN_14215cf10` (save/load) |

**There is no load of `T+0xb834` anywhere in `.text` outside the copy constructor and the
save/load serialiser.** The blend functions do not read it; they receive the rate as an
argument. This kills the "the stored rate is blended in" reading of `pes-re-defensive-shape.md`
§14.3 step 4 — the stored value is the same float the blend uses, passed in a register.

### 2.2 The call chain and the argument contract (OBS, raw disassembly)

`FUN_140623750` (the mark spot, `0x140623750–0x140625a68`) does:

```
0x140623a2e  call 0x1406216a0          ; the mark rate
0x140623a3a  movaps xmm10, xmm0        ; rate
0x140623a3e  movss  xmm9, [0x14253ee30] ; 100.0
0x140623a47  movss  [rsp+0x78], xmm0   ; rate parked on the stack
...
0x140625638  movss xmm0, [r14 + rbx*4 + 0x3f0c]   ; per-slot float #2
0x140625642  movss xmm1, [r14 + rbx*4 + 0x3f04]   ; per-slot float #1
0x140625658  movss [rsp+0x48], xmm0               ; stack arg 10
0x14062565e  movss [rsp+0x40], xmm1               ; stack arg 9
0x140625664  movss [rsp+0x38], xmm9               ; stack arg 8
0x14062566b  mov    [rsp+0x30], al                ; stack arg 7  (byte)
0x140625673  movss [rsp+0x28], xmm11              ; stack arg 6  <-- the rate
0x14062567f  call 0x140625e00                     ; returns the spot x in xmm0
0x1406256a0  movss [rbp+0x1b0], xmm0              ; x result parked
0x1406256af  movss xmm0, [rbp-0x74]
0x1406256b7  mulss xmm0, xmm10                    ; × rate
0x1406256bc  mulss xmm0, [rip+0x1f4cc84]         ; × 0.8   (0x142572348)
0x1406256c4  addss xmm7, xmm0
0x1406256c8  movss xmm0, [r14 + rbx*4 + 0x3f0c]
0x1406256d2  movss [rbp+0x1a0], xmm0              ; per-slot float #2 again
0x1406256ea  mov    [rsp+0x40], &[rbp+0x1a0]     ; stack arg 9  (in/out)
0x1406256f6  mov    [rsp+0x38], &[rbp+0x1b0]     ; stack arg 8  (in/out, the x)
0x1406256ff  movss [rsp+0x30], xmm7
0x140625705  movss [rsp+0x28], xmm11              ; stack arg 6  <-- the rate again
0x140625711  call 0x1406276b0                     ; spot z
0x140625723  movss xmm7, [rbp+0x1b0]             ; x
0x14062575b  movss xmm6, [rbp+0x1a0]             ; z
   … then |x| and |z| are clamped to the pitch …
```

`FUN_140625e00` is 6311 bytes / 1475 instructions with a single `ret` at `0x1406276a6`;
`FUN_1406276b0` is 3405 bytes / 791 instructions with the real `ret` at `0x1406283fc`;
`FUN_1406216a0` is 3553 bytes with `ret` at `0x140622480`. Ghidra's function bodies
(`body [[… , 0x140625e2c]]` etc.) are unwind-chain artifacts — do not trust `fn.py` for these
three; `fd.py` in the scratch dir re-dereives the true bounds.

### 2.3 The blend itself (OBS, raw SSE)

`FUN_140625e00 @ 0x140627196–0x1406271d6` — the two-candidate path:

```asm
140627196  movss  xmm13, [rbp+0xb0]          ; base  (stack arg 10)
14062719f  movaps xmm6, xmm8                ; cand1 (stack arg 8)
1406271a7  subss  xmm6, xmm13                ; cand1 - base
1406271ac  subss  xmm7, xmm13                ; cand2 - base
1406271b1  mulss  xmm6, [rbp+0x98]          ; × rate      (stack arg 6)
1406271b9  mulss  xmm7, [rbp+0x98]
1406271c1  divss  xmm6, [0x14253ee30]        ; / 100
1406271c9  divss  xmm7, [0x14253ee30]
1406271d1  addss  xmm6, xmm13                ; base + (cand1-base)*rate/100
1406271d6  addss  xmm7, xmm13
```

and the general path, `0x1406270e5–0x14062710d`:

```asm
1406270e5  movss  xmm8, [rbp+0xa8]
1406270ee  subss  xmm7, xmm12                ; r - 1.0
1406270f7  mulss  xmm7, [rbp+0x98]           ; × rate
1406270ff  divss  xmm7, xmm11                ; / 100
14062710d  addss  xmm7, xmm12                ; 1 + (r - 1)*rate/100
```

and the same idiom verbatim in the z function, `FUN_1406276b0 @ 0x14062808b–0x1406280ec`:

```asm
14062808b  movss  xmm0, [rsi]                ; base
14062808f  mulss  xmm6, xmm12                ; × rate
140628094  movss  xmm12, [0x14253ee30]        ; 100.0
14062809d  mulss  xmm8, xmm12
1406280a2  addss  xmm6, xmm0
1406280a6  subss  xmm6, xmm0
1406280aa  mulss  xmm6, xmm8
1406280de  …
1406280e7  divss  xmm6, xmm12
1406280ec  addss  xmm6, xmm0
```

**The blend law (OBS), for both coordinates:**

$$\text{spot}_i \;=\; B_i \;+\; \bigl(C_i - B_i\bigr)\cdot\frac{\text{rate}}{100}$$

with `rate` = the float returned by `FUN_1406216a0`, `B` = the caller-supplied per-slot
reference coordinate and `C` = the computed candidate. It is a **plain linear interpolation
away from the per-slot reference with weight `rate/100`**, not a clamp-and-nudge.

Two further rate-weighted terms in the same functions:

* `1 + (r − 1)·rate/100` (`0x1406270ee`) — the **mark distance `r` itself is pulled toward 1 m
  by the same weight**, so a high rate makes the marker hug the man he marks;
* `flag + (15 ∓ 2·dir − flag)·rate/100` (`0x140626f99–0x140626fa8`), where `flag` is the byte
  from `FUN_140a63950(ctx, side, 0xd, 0xff)` at `0x1406260f0` — a second distance blend with a
  15 ± 2 m limit.

The rate itself is 0…100 (`0x14253ee30` is used as the divisor everywhere) and is shaped in
`FUN_1406216a0` with the per-slot table `FUN_140a603c0(slot, 0xd)`, a `min(rate, 80)` branch
(`0x1406223e8`, taken when `FUN_140a64260` says so) and a final clamp.

The return of `FUN_140625e00` is a clamp: `0x140627661–0x140627688` orders the two bounds and
emits `clamp(x, lo, hi)` — the pitch edges `|x| ≤ |F[0x20]|` and the MODE-3 band
`B ± 5·dir` (`0x140627502`: `xmm6 = dir*5 + base`; `0x140627554`: `xmm7 = [rbp+0x90] − dir*5`).

### 2.4 Live check (`fit2.py` / `fit3.py`, 7674 MARK snapshots, `live2`)

Reproduced first, unchanged: rate p10/p50/p90 = 2 / 79.9 / 100; per-bin signed fractions from
the slot target to the reconstructed goal-side point **0.14 / 0.39 / 0.58 / 0.67** (x) and
**0.01 / 0.09 / 0.20 / 0.39** (z); `|spot − G|` median 11.25 / 8.88 / 6.18 / 5.03 m.

| rate bin | n | med rate | `w = rate/100` | signed αx | signed αz | **\|αx\|** | med \|spot−ref\| | med cos(Δ, G−ref) |
|---|---|---|---|---|---|---|---|---|
| [0,25)   | 1975 | 5.4  | 0.05 | 0.14 | 0.01 | 0.72 | 4.46 m | 0.27 |
| [25,50)  |  874 | 37.8 | 0.38 | 0.39 | 0.09 | 0.99 | 5.95 m | 0.68 |
| [50,75)  |  814 | 63.1 | 0.63 | 0.58 | 0.20 | 1.01 | 5.11 m | 0.77 |
| [75,101) | 4010 | 100  | 1.00 | 0.67 | 0.39 | 1.03 | 4.28 m | 0.87 |

**What this says (OBS).** `|αx|` is ≈ 1.0 in every bin above rate 25: the *magnitude* of the
displacement from the reference is not scaled by the rate at all, and the displacement is
rate-independent (4.3–6.0 m). What the rate changes is the **direction**: the cosine between
`spot − ref` and `G − ref` rises monotonically 0.27 → 0.68 → 0.77 → 0.87, and the fraction of
rows with a positive dot product 0.59 → 0.71 → 0.77 → 0.84. That is exactly what the code
predicts: at `rate = 100` the lerp degenerates to `spot = C` (the candidate, i.e. the
goal-side point, modified by the `1 + (r−1)·w` distance pull); at `rate = 0` it degenerates to
`spot = B` (the per-slot reference).

**Why 0.67 and not 0.875 in the top bin.** Three contributors, all `OBS` in the code and all in
the right direction:
1. at `w = 1` the candidate is **not** the bare goal-side point of `pes-re-defensive-shape.md`
   §14.3 step 2 — the mark distance has already been pulled to `1 + (r−1)·w = 1` m, and the
   line-hold cut of step 3 replaces `G` with the cut point for a documented 4 % of snapshots;
2. the returned value is clamped to `|x| ≤ |F[0x20]|` and, in MODE 3, to `B ± 5·dir`
   (`0x140627502`, `0x140627554`) — both push the spot *back* toward `B` for deep, own-half
   candidates;
3. the refit's `G` is a reconstruction (`r = 6 − 4·k`, goal at `dir·52.56`), so `α` inherits
   its error; the measured implied radius (median 5.8–10.1 m projected on the goal direction,
   vs a fitted `r` of 2.6–3.5 m) shows the reconstruction is the dominant error term.

The first three rate bins are reproduced by `α ≈ w` to within ±0.05 (0.14 vs 0.125,
0.39 vs 0.375, 0.58 vs 0.625); the top bin is the open item.

### 2.5 Open, with the exact next step

1. **Which array are stack args 9 / 10?** They are `[r14 + rbx*4 + 0x3f04]` and
   `[r14 + rbx*4 + 0x3f0c]` on the team record. I could **not** prove that `rbx == 3·slot`
   (which would make them `T+0x4b4c+12·slot` and `+8`, the live-read slot target), and the x
   function uses arg 10 (`[rbp+0xb0]`) as the lerp base while the z function uses arg 2.
   **Next step:** in `f_spot.txt`, find where `rbx` is last written before `0x140625638`
   (it is a scale, not a slot — `rbx*4` indexing a float array) and resolve it against the
   live-read `T+0x4b4c+12·slot`.
2. **Exact reproduction of αx = 0.67 at rate 100.** Needs the true `r` from `FUN_140622a80`
   (the mark distance, called from `0x140626150` and `0x14062732c`; ~1.3 kB) and the exact
   behaviour of the MODE-3 band clamp. **Next step:** read `FUN_140622a80` with `fd.py` and
   re-run `fit3.py` with `r` taken from the code instead of `6 − 4·k`; then re-check
   `fit2.py`'s per-bin medians.
3. **The second candidate `C` in the `ebx == 2` path** (`0x140627196`, reached when
   `T+0x87d0[slot] == 2`) — it lerps *two* coordinates, one of which is stack arg 8.
   **Next step:** trace arg 8 back in `f_spot.txt`; on the fall-through path it is `xmm9`,
   which the caller sets to `xmm11` (the rate) at `0x140625448`, so this branch needs its own
   live population.
4. Whether `T+0xb834` is read by **anything outside `.text`** (it has an address-taken
   `lea` at `0x140a41672`) — **next step:** dump the vtable/data users of `FUN_140a415a0`.

---

## 3. Killed / closed hypotheses

- "`T+0xa508` is a tactics toggle (Attacking Style option `k0`)" — **killed.** The writer chain
  (`FUN_140581980` ← `FUN_1404f6c30` ← `FUN_14041ecf0`) reads no sheet option; the gate is
  possession + in-play.
- "`T+0xa508 == 0` can be produced by the special-situation tests failing" — **killed.** Both
  sub-tests failing sets the value to **1**, not 0 (`FUN_140581980 @ 0x140581a03`).
- "The mark-spot blend reads `T+0xb834`" — **killed.** No load of that displacement exists in
  `.text` outside the copy constructor and the save/load serialiser; the rate arrives as
  stack argument 6.
- "α = rate/100 is a bad fit because the blend is non-linear" — **killed.** The SSE sequence is
  an exact lerp (`base + (cand−base)·rate/100`); the residual is in the candidate, not the weight.
- "`Ghidra` loses the float returns of `FUN_140625a70`" — **worked around**: `fd.py` in the
  CounterTriggerRE scratch dir dumps the full function with RIP-relative constants resolved;
  the true bounds of the three functions are in §2.2.