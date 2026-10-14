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

## 6. The inputs to q1..q6 (second pass)
Every q is `soft-min(I, X, F, R)` over 4 inputs (`FUN_1408413c0`). Which call feeds which record is OBS from raw: the record writes at `0x14083ef2c..0x14083f018` use r10 = Y, rdx = P, rcx = S. The call outputs go to xmm9/10/7/13/15/8 respectively.

| q | call | record field | I | X (the per-q input) | F | R |
|---|---|---|---|---|---|---|
| q1 | `0x14083ee62` | Y.scale | I | `Q` (xmm13) | F1 | R |
| q2 | `0x14083ee7c` | P.scale | I | `Qk = k·Q+(1−k)`, stored `[rbp-0x5c]` @`0x14083e712` | **F2** (xmm10) | R |
| q3 | `0x14083ee97` | S.scale | I | `0.5·Q+0.5`, `[rsp+0x6c]` @`0x14083e64e`, ×0.3 under AI-mask `0x28005` (`0x14083e72d`) | F1 | R |
| q4 | `0x14083eeb0` | Y.amp | I | `A·Q`, `[rbp-0x58]` @`0x14083e654` | F1 | R |
| q5 | `0x14083eecc` | P.amp | I | `A·Qk`, `[rbp-0x7c]` @`0x14083e717` | F1 | R |
| q6 | `0x14083eeea` | S.amp | I | `A·(0.5Q+0.5)`, `[rsp+0x60]` @`0x14083e659` (×0.3 under the same mask) | F1 (via `[rbp-0x80]`) | R |

Definitions:
- **`a`** = `[rbp+0xa0]` = `FUN_14071d760(ap,1)`, a jump to `FUN_14071d770`. It is the kick-type quality `q(S)` from accuracy §2: Finishing `0x1a`, Low Pass `0x1b`, Lofted Pass `0x1c` or Heading `0x1d`, plus skill +5 bonuses. OBS store `0x14083cf39`.
  - `A = 0.75·a + 0.25` (OBS `0x14083e610..0x14083e633`).
  - Only the amps (q4..q6) get the direct stat factor.
- **`Q`** = the situation product (decompile lines 846–1000; rebuilt in xmm13 up to `0x14083e607`). It is the product of:
  - **base by contact class** `uVar10 = FUN_1407205a0(kind)`: 0 → 0.9, 1 → 0.88, 2 or 3 → 0.86, other → 1.0. Constants `0x14258c260`, `0x1425ddf70`, `0x1425eb850`.
  - **subclass factor** `FUN_14071e520(kind)`: 1 → 0.95, 5 → 0.8, 6 → 0.85, 7/0xb → 0.9, 8 → 0.7.
  - **first-time / volley factor:** 0.925 or class multipliers on a kind gate (`0x1407240f0`/`0x140724590`…).
  - **body-turn factor:** `1 − (0.7·(1−a)+0.1)·ramp(|facing ap+0xbe4 − heading ap+0x247c|)`, with a ramp start that rises with launch speed.
  - **kind-specific extras:** for subclass 0/1/9/10, `×(1 − 0.3·ramp²)`; for some 0x2755 stances, a ±a term; a FUN_14042d470 term when `0x1406b0250(..)==1`.
  - **final factor:** `×(1 − (0.5+0.25·ramp)·X·{0.95 for skill 0xb/0xc}·{0.9 or 0.7 by AI bits})`, at `0x14083e57c..0x14083e607`.
  - Status: INF(med), read from the decompile; the branch selectors are OBS in raw.
- **`k`** (Qk): 0.5 by default. 0.7 when the subclass is 5/6, the action byte is 8 and skill `0x18` is set. 0.9 when skill 9 is set on a qualifying kind. OBS `0x14083e668..0x14083e6eb`. Constants `0x142565600`, `0x14257a194`, `0x14258c260`.
- **`I`** = `[rsp+0x58]`: the launch-power and elevation penalty, `1 − ramp·w`.
  - Non-SHOOT path (`0x14083dd32..0x14083df70`), INF(med) labels on OBS arithmetic:
    - `ramp` runs over launch speed in km/h: `|r15 vel|·f·3.6`, where 3.6 = 60·60/1000. The window is `[60+10a, 85+10a]` for action byte 1–3. Action 7 uses 55+10a, and other actions use 60+10a.
    - `w = clamp01(1 − 0.95·a)·{0.3, 0.4, 0.375}` for actions 1–3, 7 and the rest.
    - When `[ap+0x2754]==0`, ramp is `max`'d with an elevation ramp `(pitch°−20)/10`.
  - SHOOT path (`vtbl+0x28 == 10`, `0x14083dc98..0x14083dd27`): `1 − 0.25·ramp(ap+0x2bd0, 0.2..1.0) − 0.02·g` (action byte 10), or `0.35/0.04` for the other actions. `g` = a pitch-angle ramp. Action 0xb uses `1 − sin(…)`. The meaning of `ap+0x2bd0` is still open; the fox-registry uses it as a height/approach gate.
- **`R`** = `1 − 0.4·X` (`0x14083ee3e`, 0.4@`0x14259b89c`). X = `(1−0.5a)·1.3·run·(1−D) + 1.5·D` (`0x14083ee00..0x14083ee33`, 1.3@`0x14259bdd8`).
  - `run` = `(FUN_140723320(ap)·[1.5])²`, capped at 1, ×0.3 for kind 0x6e. INF(high) that this is the kicker's running speed.
  - **`D` = the pressure term:** `(0.8 − 0.4·clamp01((PhysContact − oppByte+0x28 + 20)/40))·distRamp·angRamp`, for the nearest opponent (`0x1406ea340`). `0x28` = Physical Contact (anim-select-2 §; ML). OBS `0x14083eda4..0x14083edf4`.
  - So **pressure does enter the execution error**, via R and the kicker's Physical Contact versus the opponent. This bears on accuracy §9 item 6, which said no Balance (0x29) consumer: correct, it is 0x28.
- **`F1`, `F2`** = `1.0` (stored `0x14083e749`) unless `0x1406b0250(team, …) == 1` (`0x14083e76f`). In that case they come from the geometry of a target entity `[rbp-0x78]`, relative to the kicker's position `ap+0x2480`:
  - its bearing vs heading → `F1 = (1 − 0.3·cos-term·…)·(1 − 2·…)` (`0x14083e9de..0x14083ea95`);
  - `F2 = F1·(1 − (k·[rbp-0x60])·(1−…)·ramp)` (`0x14083eac1..0x14083eaf1`).
  - INF(low): the entity is the pass receiver or set-piece target. I did not resolve what `0x1406b0250` returns.

## 7. Third pass (all OBS raw unless tagged)
### 7.1 `FUN_1408414a0(ap, Y=rdx→r15, P=r8→rbx, S=r9→rbp, …)`
Correction to §1: only the **Y** record gets pSign written, and only at `0x1408417b6`. Everything else writes **P.pSign**.
- `kind` = `ap+0x2752`. If it is −1, kind = 0x27, overridden by action 0x33 → 0x2a, 0x3f → 0x1c, 0x41 → 0x41. `sub = FUN_14071e520(kind)`. `st` = `ap+0x2755`.
- **A. Non-kick action:** `!0x140a6e340(action)`, i.e. action ≥ 0x5e. Effect: `S.scale ×= 0.1`, `S.pSign = 0.5` (`0x140841539`). Go to Z.
- **B. Header:** `0x1407246e0(ap)` is true when the action is 8..10, the Fox motion check `0x140724740` passes, and kick-kind table `0x14344e380` byte+0xc ≤ 1 (the header kinds).
  - **B1:** `0x1407241a0(kind)` is true (kind ∈ {5, 6, 9, 0x48}). Effect: `Y.scale ×= 0.9`, `P.scale ×= 0.1`, `S.scale ×= 0.1` (`0x140841572`). Go to Z.
  - **B2:** otherwise, switch on the motion id `m` = vtbl+0x18 word:
    - `m == 0x1093`, or `m − 0x822` ∈ {0, 0x28, 0x29, 0x2f, 0x31, 0x32, 0x3f, 0x40, 0x58, 0x5e, 0x62, 0x6d}: `P.pSign = 1.0` (`0x140841747`).
    - `m − 0x822` ∈ {0xa, 0xe, 0x1a, 0x1c, 0x1f, 0x21, 0x22, 0x25, 0x27, 0x2a, 0x34, 0x36, 0x41, 0x42, 0x49, 0x53, 0x54, 0x57, 0x5a, 0x5b, 0x61}: `P.pSign = (P.pSign + 1)·0.5` (`0x1408415ee`).
    - otherwise, if `0x140726610(kind)` (kind ∈ 0xe..0x10) or kind == 0x16: `P.pSign = 1.0`.
    - otherwise: `v = arg6²`, `t = 1` if v ≤ 0.2, `0` if v ≥ 1, else `(v−1)/−0.8`. Then `t' = 0.5t + 0.5` and `P.pSign = t'·P.pSign + (1 − t')` (`0x140841625..0x14084167f`). arg6 is `[rsp+0x78]`, the last float arg from cc30. Its meaning is open (INF(low): a normalised ball height).
  - Then go to Z.
- **C. Foot kick:**
  - First, if sub ∈ {5, 6}: `P.pSign` = 1.0 if action byte 8 and skill 0x18; 0.7 if `0x140a6e340` and skill 9; else 0.9 (`0x140841689..0x1408416e8`).
  - Then switch on `m − 0x97c` (`0x1408416fb`):
    - {0, 8, 0x97}: `P.pSign = 1`.
    - {0xa7}: `P.pSign = 0.25·P.pSign + 0.75`.
    - {0x87, 0x88}: `P.pSign = 0.2·P.pSign + 0.8`.
    - {0xa4, 0xa6}: `P.pSign = 0`.
    - {0x7e}: `Y.pSign = vtbl+0x30() ? 1.0 : −1.0`.
    - {0x2e, 0x31, 0xbd} with `st == 0xf`: `Y.pSign = vtbl+0x30() ? 1.0 : 0`.
    - other values: no change.
  - INF(med): `vtbl+0x30` is the kicking-foot side (left/right), so this forces the yaw error to the outside of the foot on these motions.
- **Z:** `if (ap+0x2d14 & 1) Y.scale = 0` (`0x1408417bc`). The jostle/fall flag cancels the yaw scatter.

### 7.2 Q kind predicates (argument = kick kind)
| fn | true when |
|---|---|
| `0x1407240f0` | kind ∈ {0x1d, 0x1e, 0x23, 0x25, 0x28, 0x29, 0x36, 0x3e, 0x47, 0x8a, 0x8c, 0x8f, 0x92}, decoded from the jump table at `0x140724124` |
| `0x140724590` | kind ∈ 0xa..0xd |
| `0x140726610` | kind ∈ 0xe..0x10 |
| `0x140726640` | kind ∈ {0x11, 0x13, 0x14, 0x1a} (mask `0x41a0000`) |
| `0x1407241a0` | kind ∈ {5, 6, 9, 0x48} |
| `0x140a6e340` | action < 0x5e |

- The decompile gate (cc30 l.868) applies the class factor when: not `0x1407240f0`, and not bit 29 of `ap+0x2460`, and one of `0x140724590` / `0x140726610` / `0x140726640` is true, and motion ≠ 0x9f1.
- **Open:** what those kind numbers mean (volley, first-time, etc.). Next step: name the entries of the kick-kind table `0x14344e380` (accuracy §2), using the kind ids above.

### 7.3 `0x1406b0250(tbl = 0x1406b0ed0(), m, flag)`
- It returns the byte at `tbl[m·0x390 + 1 + (flag != 0)]`, a per-motion descriptor byte. Ids 0xe42 and 0xf7c and ids ≥ 0x1125 have special paths (`0x1406b0253..0x1406b0296`).
- The call passes flag 0, so `== 1` means **the current kick motion's descriptor byte[1] == 1**.
- When it holds, F1/F2 use the entity returned by `0x140a57450(…, 1)`.
- INF(low): this marks a motion that is aimed at a teammate or receiver. The byte's meaning is still open. Next step: dump byte 1 for known pass versus shot motion ids out of `0x1406b0ed0`'s table.

### 7.4 `FUN_140470d60(&in, &out, deg)` (confirmed)
- `out = wrap360(*in)`, then `out = wrap360(out + deg)`, with 360.0 at `0x14259858c` (`0x140470d60..0x140470da3`).
- **Confirmed:** it adds `deg` (xmm2) to a heading in degrees and wraps the result to [0, 360). There is no pitch involvement.
- In `FUN_1408398d5`, `in` = `[rbp-0x2c]` (heading from `FUN_14046f230`) and `out` = `[rbp-0x80]`. The result is copied to `[rbp-0x30]`.

## 8. Still open
1. The meaning of the kick kinds and motion ids in §7.1–7.2. They are numeric and port-usable as tables.
2. arg6 of `FUN_1408414a0`, and the motion descriptor byte[1] (§7.3).
