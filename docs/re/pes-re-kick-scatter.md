# PES21 kick scatter — the 6 `Normal` draws in `FUN_1408398d5` (accuracy §9 item 5)

Raw disassembly is in the KickScatterRE scratch dir (`dis.txt`, `cc30.dis`). Decompiles are in `out/`. I checked every branch below against the raw disassembly.

## 1. Where the stack slots come from (OBS)
- `0x140839988 call FUN_14083cc30(rcx=rdi, rdx=&[rbp-0x40], r8=&[rbp+0xa8], r9=&[rbp+0x98], [rsp+20]=&[rbp-0x60] mode (init 3), [rsp+28]=r15 vec, [rsp+30]=&[rsp+0x78] w (init 0), [rsp+38]=&[rbp-0x28] (init 1.0))`.
- Each of the three slot groups is the same 3-float record `{scale, pSign, amp}`:

| record | scale `[0]` | pSign `[1]` | amp `[2]` (the `Normal` xmm1) |
|---|---|---|---|
| **Y** `[rbp-0x40]` | `(1−q1)·22.5` | `[-0x3c]` | `(1−q4)²·4.25+0.75` = `[rbp-0x38]` |
| **P** `[rbp+0xa8]` | `(1−q2)·22.5` | `[+0xac]` | `(1−q5)²·4.25+0.75` = `[rbp+0xb0]` |
| **S** `[rbp+0x98]` | `(1−q3)·0.3` | `[+0x9c]` | `(1−q6)²·4.25+0.75` = `[rbp+0xa0]` |

- OBS: written at `0x14083ef2c..` (raw: `[r10]=(1−xmm9)·22.5`, `[r10+8]=(1−q)²·4.25+0.75`). Constants: 22.5@`0x14259d618`, 0.3@`0x142590d98`, 4.25@`0x1425ef9f8`, 0.75@`0x14259bdd4`.
- Before that write, kind 10 with mode∈{1,2} and a specific bitmask (`0x28005`) applies `q3 = q3·0.25+0.75` at `0x14083ef1c`.
- `FUN_1408414a0` (`0x14083f038`) then post-scales some records:
  - one branch: S.scale ×0.1 and S.pSign=0.5;
  - another branch: Y.scale ×0.9, P.scale ×0.1, S.scale ×0.1;
  - it also rewrites P.pSign (1.0 / 0.9 / 0.7 / lerp) and Y.pSign.
  - The branch conditions are not traced: INF(med) that this is kick-type/situation gating.
- `q1..q6` ∈ [0,1] are qualities. They come from 6 calls to `FUN_1408413c0` (`0x14083ee62/7c/97/b0/cc/ea`), each with 4 args. That function qsorts the 4 args (`FUN_141e53540`, comparator `0x14083cc10`), then folds them pairwise as `x = min·(max+1)/2`. Effect: a soft-min that leans towards the worst factor.
  - Arg 0 is always `[rsp+0x58]`.
  - Arg 3 is always `1 − 0.?·(…)`, built at `0x14083ee3e`.
  - Arg 1 is per-call: `xmm13`, `[rbp-0x5c]`, `[rsp+0x6c]`, `[rbp-0x58]`, `[rbp-0x7c]`, `[rsp+0x60]`.
  - **OPEN:** which stat or situational factor each arg holds. Next step: slice those 7 slots inside `FUN_14083cc30` (`0x14083cc30..0x14083ee62`).
  - The decompile shows the inputs include a stat-minus-opponent term `clamp((stat(0x28) − oppByte − (−20))/40)` (0x28 = attribute index) and the `+0x2bd0`/`+0x281c` lerps (`(1.25? − x)·[+0x281c]·k + x`).

## 2. What `Normal` returns (OBS `FUN_140a92590`)
- It is Box–Muller on slot-2 LCG steps: `r = sqrt(−2·ln u1)·amp / 3.53223` (divisor@`0x1426550e4`), angle `360·u2` → `cos`.
- It returns `N = int(50·(1 + r·cos))` and rejects/redraws until `0 ≤ N < 100`.
- Every caller then computes `|N − 50| / 50`. With xmm2=0, that is a **half-normal `|z|·amp/3.532`, truncated below 1**.
- So `amp` is the spread: 0.75 is narrow, 5.0 is so wide it mostly saturates near 1. A lower quality `q` gives a wider spread.

## 3. Per-draw formula and consumer
Shared notation: `h = |N−50|/50`. `w = [rsp+0x78]`, the cc30 out-param `*param_7` (0, 1.0 or a computed lerp). `sgn` = ±1 from success roll `FUN_1406ebf30(int(pSign·100))`, with 1.0@`0x14256ae1c` and −1.0@`0x142588218`.

**Path A — mode `[rbp-0x60]` ∈ {1,2}** (`0x140839a7e`). In this path the yaw comes from a *uniform* `Range(1000)`, not from `Normal`.

| draw | amp | magnitude | consumer | units |
|---|---|---|---|---|
| `0x140839b51` | P.amp `[rbp+0xb0]` | `h·P.scale` | `xmm12 = pitch + sgn·mag`. pitch = `[rbp+0x3c]` = `atan2(y, |xz|)·57.2958` (`0x1408399c6`, ×`0x14259be40`) | degrees of elevation |
| `0x140839bd5` | S.amp `[rbp+0xa0]` | `S.scale·(w + (1−w)·h)` | `xmm13 = (1 + sgn·mag)·|v|`, where `|v|` = `[rbp+0x40]` (3-D length at `0x140839a04`) | fraction of launch speed |

**Path B — mode == 3** (`0x140839c53 je 0x14083a25d`). This is the default, since cc30 initialises mode to 3.

| draw | amp | magnitude | consumer | units |
|---|---|---|---|---|
| `0x14083a280` | Y.amp `[rbp-0x38]` | `Y.scale·(w + (1−w)·h)`. Note `[rsp+0x78]` is overwritten with `1−w` at `0x14083a269` | `sgn·mag` → xmm2 of `FUN_140470d60(&[rbp-0x2c], &[rbp-0x80])` at `0x14083a5c4`. `[rbp-0x2c]` = heading from `FUN_14046f230(r15)` | degrees of heading (yaw) |
| `0x14083a550` | Y.amp (xmm12 = `[rbp-0x38]`) | `Y.scale·h` (no `w` term) | redraw loop for the same yaw (≤6 iterations, `cmp ebx,5`). It retries while the signed result matches a sign test on `[r14]·[r14+8]` and `mag > ` the first draw's capped bound. The final value feeds the same `FUN_140470d60` | degrees (yaw) |
| `0x14083a5f2` | P.amp `[rbp+0xb0]` | `P.scale·(w + (1−w)·h)` | `xmm12 = sgn·mag`. It is ×0.5 (`0x142565600`) when sgn<0 and the action byte is 9, 10 or 12..14 (`0x14083a6a6`). Then `+ [rbp+0x3c]` → new pitch. For action 0xb with `[rbp-0xc] ≤ 1`, it is then scaled by `(1 − k·Range(…)/…)` (`0x14083a7e2`) | degrees of elevation |
| `0x14083a7fe` | S.amp `[rbp+0xa0]` | `S.scale·(w + (1−w)·h)` (`xmm7` = 1−w) | `xmm13 = (1 + sgn·mag)·|v|` (`0x14083a86c`) | fraction of launch speed |

- **Spin:** none of the 6 draws reaches spin. OBS: no consumer touches a spin or curl register before the joins at `0x140839c5d`/`0x140839c66`.
- Spin noise exists only in the predictor `FUN_1407324b0` (kick-launch §5).

## 4. `errScale` (§0.3)
- **It does not enter `FUN_1408398d5`.**
  - OBS negative: I byte-scanned `0x140839000..0x140842000` for RIP-relative references to 5.5@`0x1425c8378`, −59@`0x1425cd974`, expf@`0x1415a17b6` and `FUN_1407324b0`. There are zero hits. The only "2.5"-address matches are suffix-aliasing false positives at `0x14083e205`/`0x14083fbed`, which are unrelated `divss` instructions.
  - In this path, "ability → error" goes through `q1..q6` (the soft-min of 4 factors, §1). The functional form is quadratic, `(1−q)²` in amp and linear `(1−q)` in scale, not logistic.
- `errScale` belongs only to the predictor `FUN_1407324b0` (kick-launch §0.3, §5).
- INF(med): GF should therefore model the physics scatter with the §1/§3 formulas. It should not reuse `errScale` here.

## 5. Killed hypotheses
- "The xmm1 args are angles in degrees": killed. xmm1 is only the Box–Muller spread (§2). The degrees come from `scale` (22.5).
- "All 6 draws are direction": killed. Two are speed (S record), two are elevation, two are yaw.
- "errScale multiplies the scatter amps" (accuracy §9.5 addendum): killed by the scan in §4.

## 6. Still open
1. Semantics of the `q1..q6` inputs (§1). Next step: slice them as described there.
2. The branch conditions in `FUN_1408414a0` that pick the ×0.1/×0.9 scale cuts and the pSign values.
3. `FUN_140470d60`'s exact maths. INF(high): it rotates the heading by `xmm2` degrees, with the pitch passed by pointer. Confirm by decompiling `0x140470d60`.
