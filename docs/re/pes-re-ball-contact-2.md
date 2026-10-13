# PES 2021 ball bounce — part 2: the literal vertical response, the mode byte, the frame/net gap

Scope: read-only RE of `pesmem.bin` (base `0x140000000`, **file offset = VA − 0x140000000**),
`PES2021.exe` on disk, and GhidraMCP (port 8080). Follows `pes-re-ball-contact.md`; abbreviations
`T[x]`/`BallBase+X`/`ball.o+x` keep that report's meaning.

**Corrections to part 1 are marked ⚠** — three of them change conclusions.

Tags: **OBS** = observed (method stated); **INF** = inferred (confidence stated).
Disassembly below is capstone on the raw image; instructions verified against Ghidra.
Function bounds come from `.pdata` (table at RVA `0x3ac5000`, 129 009 entries, `pd.py` in
`scratch/BallContact2/`). Every register/stack-slot claim was re-derived from the raw bytes,
not from the decompiler's variable names — that was the bug class this report set out to kill.

---

## 0. Headline

1. **OBS — The post-contact `vy` is a reflection, not a coefficient, and not a second routine.**
   The contact tick computes `vy_out = −F·x`, where `x` comes from
   `min(−natualBound, vy_trimmed)` at `0x1408771f9`. Because `natualBound` is a **constant
   speed** (`(natualBoundA·3.6·|v_xz| + natualBoundB)/3.6` m/s, ≥ 0.111 m/s) and the falling
   `vy` is normally much larger in magnitude, the `min` selects `−natualBound` and
   `vy_out = +F·natualBound` — a **speed clamp**, not a restitution.
2. **OBS — When the impulse cap exceeds the incoming speed, the same instruction becomes a
   true reflection**: `x = vy_trimmed`, so `vy_out = −F·vy_in`. That happens for every slow
   contact (`|vy_in| ≤ natualBound`), i.e. exactly the rolling/bobble regime. **This is the
   source of the §6.4 spread.** One instruction, two regimes.
3. **OBS — `F` is the "restitution-like" number, and in mode 1 it is 0.7·(1+0.05·s₂)** where
   `s₂ = clamp01((3.6·|vy| − 2)/10)`. So the reflection coefficient is
   **0.7 … 0.735**, and it is not a constant — it ramps with the incoming vertical speed.
4. **OBS — The mode byte writer is `0x140872479` (`mov [rax+0x279d], bl`)**, inside
   `FUN_1408723d0`. It is set from a **3×3 table at `0x14349f718`** indexed by two
   *sports-config* integers: `[cfg+0x78]` (call it `pitch`) and `[cfg+0x7c]` (call it `weather`).
   Table (`rows = pitch 0..2`, `cols = weather 0..2`):

   ```
        weather:  0    1    2
   pitch 0        2    1    1
   pitch 1        2    1    0
   pitch 2        1    0    0
   ```

   **Dry/normal pitch is mode 2** (any pitch index, `weather = 0`), not mode 1. The
   dry-pitch restitution is therefore a **flat `F = 0.7`** (`boundCheckBoundAddRate[2] = 0`
   removes mode 1's speed ramp). See §3 — this changes the porter's default column.
5. **OBS — §3.3's rolling block writes NO vertical quantity.** Its output vector has a
   structurally zero Y-component (`0x140876f45: xmm5 = A·e_y + B·e_y` with `e_y ≡ 0`).
   The register map at entry is now complete (this report §2). The unported **spin pump**
   is `0x1408771ba/0x1408771c8` (raw-Y store), §2.4 — and it does not touch `vy` either.

---

## 1. The literal formula for `vy` after ground contact

### 1.1 Entry and eligible path (re-confirmed, OBS)

`FUN_140876360` (`0x140876571 … 0x1408773f1` per `.pdata`) — contact branch entered at
`0x140876806` when `r > y` (`0x1408767fc`). `y := r` (`0x14087682d`), `ballstate+0x74 := 0`
(`0x14087683b`), `|v_xz|` computed (`0x140876824`, kept in `xmm11`), `[rbp−0x7c] = |v_xz|`.

Register map at `0x140876866` (verified upstream from raw stores):

| slot | value | set at |
|---|---|---|
| `xmm13` | `vx` | `0x140876866 movss xmm13,[rsi]` |
| `xmm12` | `vz` | `0x14087686b movss xmm12,[rbx+0x14]` |
| `xmm14` | `vy` | `0x140876871 movss xmm14,[rbx+0x10]` |
| `[rsp+0x68]` | `vy`, later **grounder-trimmed `vy`** | `0x140876894`, overwritten `0x140876a61` |
| `[rsp+0x7c]` | `vx` | `0x1408768b7` |
| `[rbp−0x80]` | `vz` | `0x1408768be` |
| `[rbp−0x7c]` | `|v_xz|` | `0x14087683f` |
| `xmm10` | `K` | `0x14087697a addss xmm10,xmm0` |
| `[rbp+0x90]` | `F` (updated `0x140876f66`) | `0x1408768a6`, `0x140876b26` |
| `xmm8` | `1.0`; `xmm7` | `0.0` | prologue / `xorps xmm7,xmm7` |

⚠ **Correction 1 to part 1:** `[rsp+0x68]` is **`vy` (grounder-trimmed)**, not `vx`. Part 1 read
`0x140876894` (`movss [rsp+0x68],xmm14`) and missed the second write at `0x140876a61`
(`movss [rsp+0x68],xmm14` after `0x140876a5c subss xmm14,xmm1`), where `xmm14` is still the
`vy` loaded at `0x140876871`. This is the single load-bearing fact: it makes the vertical
response a **min-clamp against the incoming vertical speed**.

### 1.2 The vertical chain, verbatim (OBS, `0x1408770e6 … 0x14087733c`)

```
1408770e6: xmm1 = T[0x11c]  = boundToRotationSpeedMinY[m]     (m1: 0.1)
1408770f0: comiss xmm1, xmm6                                   ; xmm6 = |vy| * 3.6
1408770f3: xmm0 = T[0xec]   = boundToRotationSpeedMaxY[m]      (m1: 0.7)
1408770fd: jb    → t = 0            (MIN > x  → t = 0)
140877104: comiss xmm6, xmm0
140877107: jb    → t = 1            (x  > MAX)
14087710f: t = (x − MIN)/(MAX − MIN)
14087711b: xmm6 = t * T[0x8c]  = boundToRotationAddRateY[m]   (m1: 0.07)
140877128: xmm6 = xmm6 + T[0xbc] = boundToRotationBaseY[m]    (m1: 0.03)
        ⇒ vy_keep = 0.03 + 0.07·clamp01((3.6|vy| − 0.1)/0.6)      …  in [0.03, 0.10]

140877125: cvtps2pd xmm0, xmm3          ; xmm3 = spin-Y delta chain (see §2.4)
140877132: andps xmm0, signmask
14087713d: comiss 0, |xmm3| ; jb → 0x140877147
140877147: xmm0 = 0.01        (0x14256ae18)
14087714f: comiss |xmm3|, 0.01 ; jb → xmm1 = 0
14087715a: xmm1 = |xmm3| / 0.01
14087715e: xmm1 = xmm1 * 100.0            (0x14253ee30)
140877166: xmm0 = vy_keep
140877169: xmm0 = vy_keep * 2.5           (0x14259bde4 = 2.5, NOT 8.0  ⚠)
140877171: xmm6 = vy_keep − 2.5·vy_keep  = −1.5·vy_keep
140877175: xmm6 = xmm6 * (|xmm3| · 100)
140877179: xmm6 = xmm6 / 100.0
140877181: xmm6 = xmm6 + 2.5·vy_keep
140877185: comiss xmm6, 1.0 ; ja  → xmm8 = 1 (keep)
14087718b: xmm8 = max(0, xmm6)
        ⇒ blend = clamp01( vy_keep · (2.5 − 1.5·|ΔrawSpinY|) )

1408771ba: xmm3 = xmm3 * xmm8                      ; the spin pump, applied to raw-Y
1408771c8: xmm3 = xmm3 + [rbx+0x1c]                ; + old rawSpinY
1408771cd: [rbx+0x1c] = xmm3                       ; rawSpinY := rawSpinY + blend·Δ

1408771d2: xmm9 = xmm9 * T[0x204]                  ; xmm9 = 3.6·|v_xz|  (km/h)
                                                    ; T[0x204] = natualBoundA[m] = 0.055
1408771dc: xmm9 = xmm9 + [rdi+0x21c]                ; + natualBoundB = 0.4 (scalar)
1408771e5: xmm9 = xmm9 * 1000.0
1408771ea: xmm9 = xmm9 / 3600.0                     ;  km/h → m/s
        ⇒ imp = (0.055 · 3.6·|v_xz| + 0.4)/3.6      [m/s, a SPEED not a ratio]

140877194: xmm0 = [rbp+0x80] = dt                   ; saved at 0x140876363
14087719c: comiss xmm0, 0
1408771f3: jbe 0x140877206                          ; dt ≤ 0 (rewind/prediction) → other branch
1408771f5: xmm9 = −xmm9                             ; dt > 0 (live play)
1408771f9: xmm9 = min(xmm9, [rsp+0x68])             ; min(−imp, vy_trimmed)   ★
140877200: xmm14 = xmm9
```

Then the branch at `0x14087722c` (same `dt` flag):

```
dt > 0:
140877231: xmm0 = F
14087723d: xmm0 = −F                                (xorps with −0.0, 0x1425656d0)
140877240: xmm13 = vx · K
140877255: xmm12 = vz · K
14087725f: xmm5  = T_y · Kspin                      (T_y ≡ 0, see §2)
140877248: xmm14 = xmm14 · (−F)                      ★  ⇒  vy contribution = −F·min(−imp, vy)
140877251: xmm3  = K · 0 = 0
dt ≤ 0 (rewind): everything divided instead — never used live, ignore.
```

Final stores (`0x14087729e … 0x14087733c`), `dt > 0`:

```
14087729e: xmm2 = stopSpeed (0.175)
1408772aa: xmm2 = xmm2 * 1000.0
1408772bb: xmm2 = xmm2 / 3600.0        ⇒ 0.04861 m/s
1408772c8: xmm2 = xmm2²
1408772cc: |v_new_xz|²
1408772d0: comiss xmm2, xmm1 ; jbe → keep        ; STOP: if |v_xz_new|² < stop² → vx=vz=0
1408772e9: xmm13 = vx·K + 0        → vx_out
1408772f4: xmm12 = vz·K + 0        → vz_out
1408772ff: xmm3  = 0 + xmm14       → IMPULSE on vy
140877310: xmm15 = T_x·Kspin + vx·K → vx_out
14087731e: xmm11 = T_z·Kspin + vz·K → vz_out
140877323: xmm5  = T_y·Kspin + xmm14 → VY_OUT
140877331: movss [rsi+4], xmm5
```

⚠ **Correction 2 to part 1:** the stop gate zeroes **only `vx` and `vz`** (`0x1408772d5/2d9/2dc`
zero `xmm13`, `xmm3`, `xmm12`; the `vy` register `xmm5` is untouched). Part 1 reported
"velocity := 0".

### 1.3 The formula (mode `m`, contact tick, `dt = 1/54 > 0`)

```
vxh   = |v_xz|                      (horizontal speed at contact)
F     = boundBoundRate[m] · (1 + boundCheckBoundAddRate[m]·s₂)
s₂    = clamp01( (3.6·|vy| − boundBoundSpeedMin) / (boundBoundSpeedMax − boundBoundSpeedMin) )
imp   = (natualBoundA[m]·3.6·vxh + natualBoundB) / 3.6          [m/s]
z     = min(−imp, vy_trimmed)          vy_trimmed = vy − grounderTrim  (§1.4)
vx_out, vz_out = K·(vx, vz) + Kspin·T_xz          (T_xz = the §2 tangential vector)
vy_out = Kspin·0 + (−F)·z = −F·z
```

**Two regimes, one `min`:**

| regime | `z` | `vy_out` | `vy_out/vy_in` |
|---|---|---|---|
| normal bounce: `|vy| > imp` | `−imp` | `+F·imp` | **`F·imp/|vy| < F`** — sub-unity, falling as speed grows |
| slow/rolling contact: `|vy| ≤ imp` | `vy` | `−F·vy` | **`F`** — a true restitution (mode 1: 0.70–0.735) |

That is the whole answer to the assignment's core question, and the 0.03/0.10 `vy_keep`
step is **only a multiplier on the raw-spin-Y blend** (`0x1408771ba`) — it never touches `vy`
directly.

### 1.4 The grounder trim (`0x14087699e … 0x140876a67`) — corrected

⚠ **Correction 3 to part 1:** `gounnderprob[1] = 5`, not `0` (OBS: `cm.bin` disk block
`0x330`, read as `6 × i32` = `(5,5,5,9,9,5)`). So the `idiv` at `0x140876d6` never faults in
mode 1, and the grounder branch **is live** for mode 1.

```
1408769d6: idiv [rdi+r14*4+0x1d8]     ; gounnderprob[m]
1408769de: test edx,edx ; jne → skip  ; taken when int(|x|·1000) % 5 == 0
...
140876a4b: vx1 = |v_xz| * grounderBoundYAdd        (0.09)
140876a53: vx1 = vx1 * (1 − f)
140876a5c: vy  = vy − vx1                            ← applied to VY, not vx  ⚠
140876a61: [rsp+0x68] = vy                          ← the min() operand
140876a57: K  = K · (grounderFrictionRate + f)      (f = frac(|x|·1000)·0.3·…, ≤ 0.0045)
```

with `f` from `0x1408769e6 … 0x140876a43`:
`f = (frac(|x|·1000) · 0.3) ·100 ·(1−0.985)/100`, i.e. `f ∈ [0, 0.0045]`.

Part 1 had this as "`vx -= |v_xz|·0.09·(1−f)`" and as a division-by-zero. Both wrong.

---

## 2. §3.3 decoded — the rolling/spin block (answer to task item 1)

Raw `0x140876c0f … 0x140876f6e`, entry state from §1.1 plus
`[rbp+0x98] = wz²` (written `0x140876459`), `[rsp+0x44] = wx²` (`0x14087647e`),
`[rsp+0x58]/[rsp+0x5c]/[rsp+0x60] = vx,vy,vz`, `[rbp−0x68]/[rbp−0x6c]/[rbp−0x70] = wz,wy,wx`
(rad/s, `0.142598958`-scaled prologue at `0x14087646c…0x140876478`).

```
140876c0f: xmm0 = [rbp+0x98] + [rsp+0x44]        = wx² + wz²
140876c1d: call sqrt                              → S = hypot(wx,wz)
140876c22: xmm15 = |v_xz|
140876c2c: comiss xmm15, 0.001 (0x1425a9668) ; jb 0x140876c7f
140876c52: xmm3 = vx ; xmm4 = vz ; xmm1 = 0
140876c5e: xmm3 = vx / |v_xz|      → e_x
140876c63: xmm4 = vz / |v_xz|      → e_z
140876c68: xmm1 = 0   / |v_xz|     → e_y  ≡ 0      ★
140876c6d: [rsp+0x40] = e_x ; [rsp+0x78] = e_z ; [rsp+0x44] = e_y
140876c7f: xmm5 = π (0x142578480)
140876c8a: xmm2 = −e_x
140876c95: xmm11 = −wz
140876ca4: xmm12 = S · 0.5
140876cad: [rbp+0x98] = −e_x
140876cb9: xmm2 = (−wz)·e_x
140876cc3: xmm0 = e_z · wx
140876cc7: xmm1 = e_y · 0 = 0
140876ccb: xmm11 = (−wz)·e_z
140876cd0: xmm2 = xmm2 + 0
140876cd4: xmm12 = xmm12 / π
140876cd9: xmm11 = xmm11 + 0
140876cde: xmm1 = [rbp−0x78] = (K/r)·vz
140876ce3: xmm2 = xmm2 + xmm0                       = e_z·wx − e_x·wz
140876ce7: xmm0 = [rbp+0x98] · wx                   = −e_x·wx
140876cf3: xmm1 = xmm1 / K = vz/r                   ⇒ vz/r
140876cf8: xmm2 = xmm2 · 0.5
140876d00: xmm11 = xmm11 + xmm0                     = −e_x·wx − e_z·wz
140876d05: xmm0 = [rsp+0x74] = (K/r)·(−vx)
140876d0b: xmm0 = xmm0 / K = −vx/r
140876d10: xmm11 = xmm11 · 0.5
140876d19: xmm2 = xmm2 / π                          ⇒ A_dot
140876d1d: xmm0 = (vx/r)²  ; xmm1 = (vz/r)²
140876d25: [rsp+0x70] = xmm2  (= A_dot)
140876d2b: xmm11 = xmm11 / π                        ⇒ B_dot
140876d30: xmm0 = (vx/r)² + (vz/r)²
140876d34: call sqrt                                ⇒ R0 = hypot(vx,vz)/r
140876d42: xmm3 = R0 · 0.5
140876d55: xmm3 = xmm3 / π                          ⇒ rotation frequency Hz
140876d60: xmm1 = xmm3 − boundRollMax (7.5)
140876d65: xmm1 = max(0, xmm1)
140876d69: xmm3 = xmm3 − xmm1                        (over-spin removal)
140876d6d: xmm3 = xmm3 − A_dot
140876d71: xmm3 = xmm3 · RollRamp
140876d76: xmm3 = xmm3 + A_dot                       ⇒ S3
140876d7a: xmm0 = A_dot − S3
140876d88-0x140876db2: xmm2 = clamp01( (|A_dot − S3| − 4.0) / −3.0 )
140876db2: xmm2 = 0.5 + 0.5·xmm2                     ⇒ B (0..1)         ★
140876dba: xmm1 = [rdi+0x2ac] = rollToSpeedMaxRoll (8.5)
140876dde: xmm4 = A_dot − B·8.5                       ; sign-clamped by the maxss at de7/dea
140876dd2: xmm11 = B_dot · RollRamp                   ; sign-clamped at dee/df7
140876e1a: ucomiss xmm15(|v_xz|),0 ; jp/je → skip
140876e22: xmm0 = |vy| / |v_xz|                        ⇒ slope              ★
140876e37: xmm5 = rollToSpeedSpeedYMin (0.1)   [rdi+0x2b4]
140876e42: xmm11 = rollToSpeedSpeedYRateBase[m] (0.8)  [rdi+r14*4+0x2b8]
140876e3f: comiss xmm5, xmm0                          ; band slope on [0.5, 0.1]
140876e6a: xmm2 = (1 − 0.8)·t + 0.8 = yRate
140876e77: xmm0 = rollToSpeedMaxKPH[m] (20)
140876e81: xmm0 = xmm0 · 1000
140876e8e: xmm0 = xmm0 / 3600                          ⇒ vmax = 5.5556 m/s
140876e9a: xmm2  = yRate · B      · vmax               ⇒ B_
140876e9e: xmm11 = yRate · A_dot  · vmax               ⇒ A_
140876eb4/0x140876eb9: both × vmax (already folded above)
140876ec5: xmm12 = band(hypot(wx,wz), 2, 7) = s4       ([+0x28c] rollToBoundRollMin/
                                                                  [+0x290] rollToBoundRollMax)
```

Then the tangential vector build (`0x140876eeb … 0x140876f66`):

```
140876eeb: xmm5  = −e_x
140876ef6: xmm0  = B_ · e_z
140876f00: xmm15 = A_ · e_x
140876f0a: xmm1  = B_ · e_y
140876f10: xmm5  = −e_x · B_ = −B_·e_x
140876f14: xmm15 = A_·e_x + B_·e_z            ⇒ T_x
140876f2f: xmm5  = A_
140876f33: xmm5  = A_ · e_y
140876f45: xmm5  = A_·e_y + B_·e_y            ⇒ T_y   (e_y ≡ 0)
140876f39: xmm11 = A_ · e_z
140876f49: xmm11 = A_·e_z − B_·e_x            ⇒ T_z
140876f40/52: xmm12 = s4² · rollToBoundAdd (0.07)
140876f61: F    = F + s4²·0.07
```

**Does §3.3 write `vy`?  No.** `T_y = A_·e_y + B_·e_y` with `e_y ≡ 0` from
`0x140876c68`, so the tangential vector is exactly horizontal for every input. It feeds
`vx_out`/`vz_out` only (`0x140877255` is `xmm12·K`, `0x140877263` is `xmm11·xmm1`),
and the `vy` path (`xmm5` at `0x14087725f`) is `T_y·Kspin = 0`.
(OBS: raw; confirmed by the sweep in §5, where `vy_out` is unchanged across
`spin = ±20 rps`.)

### 2.1 The spin pump that *is* unported (`0x1408770a5 … 0x1408771cd`)

```
140876f6e-0x140876fcc: Wx = K·vx + T_x ; Wz = K·vz + T_z ; Wy = 0
140876fc3: xmm0 = Wx·0 − Wy·0 = 0
140876fc7: xmm13 = Wx − Wz
140876fcc: xmm12 = Wz − Wx
140876fd5: [rbp+0x98] = 0                       ⇒ new-spin Y component
140876fdd: r
140876fed: xmm13 /= r ; xmm12 /= r ; xmm3 /= r  ⇒ spin = velocity/r
14087704e: cap test: |spin|²/(2π²) ≤ boundRollMax²  → keep, else renormalise (0x140877081)
140877085: xmm0 = 1/492.181 (0x14349f740)
14087708d-0x1408770a0: angular → raw
1408770a5: xmm13 −= rawSpinX ; xmm3 −= rawSpinY ; xmm12 −= rawSpinZ  ⇒ deltas
1408770b6/0c0: blend k = 1 if r12b or r15b else RollRamp
1408770c4/0c9/0ce/0d4: rawSpinX, rawSpinZ += k·Δ ; [rbx+0x18/0x20] stores
1408771ba: xmm3 = ΔrawSpinY · blend                       ← spin pump
1408771c8: rawSpinY += ΔrawSpinY·blend ; [rbx+0x1c] store
```

So the unported piece is: **the raw-Y spin is pushed toward the rolling-coupled value by the
`0.03/0.10·(2.5−1.5|Δ|)` blend**, and that raw-Y then feeds the next tick's
`boundToRotation*Y` interpolation indirectly through the render/spin path — but on the *same*
tick it does **not** add to `vy`. The `vy`of the next tick is independent.

### 2.2 Register map summary for a porter (OBS)

At `0x140876f6e`: `xmm10 = K`, `xmm1 = Kspin (≡1)`, `xmm2 = T_xz·?`, `xmm13 = vx`,
`xmm12 = vz`, `xmm14 = impulse`, `xmm15 = T_x`, `xmm11 = T_z`, `xmm5 = T_y (≡0)`,
`[rbp+0x90] = F + s4²·rollToBoundAdd`.

---

## 3. The mode byte — live answer to task item 3

### 3.1 The writer (OBS)

Full binary scan for `mov byte [reg+0x279d], imm` (modrm `C6` family, disp32 `0x0000279d`)
yields **three** sites:

| VA | instruction | containing function (`.pdata`) |
|---|---|---|
| `0x140872479` | `mov byte ptr [rax+0x279d], bl` | `0x1408723d0 … 0x1408724a0` |
| `0x1406dbbb7` | (register-indirect via `rcx` from `[r10+0x40]`) | `0x1406dbb54 … 0x1406dbc58` (the *consumer* inside `0x1406dbb30`) |
| `0x143c5f53c` | in `.bss`, not code | — |

The only real writer is `0x140872479`. Its function:

```
1408723df: rax = [0x1436f3dc8]                       ; global match-context slot
1408723e9: rbx = [rax+0x2c0]                         ; the sports-config object
1408723f3: call 0x140a3f560                          ; e.g. "is futsal / special mode"
1408723fa: je 0x140872403
1408723fc: ebx = 3
140872401: jmp 0x14087243a
140872403: rcx = rbx ; rdi = 0x14234e700 → [cfg+0x7c]        ; WEATHER (int)
140872411: rcx = rbx ; call 0x141036a00 → [cfg+0x78]         ; PITCH  (int)
140872416: if (edi > 2 || eax > 2) → ebx = 1                  ; out-of-range default
140872420: idx = rdi + rdi*2 + rax  = pitch*3 + weather        (rdi = pitch, eax = weather)
140872429: rax = 0x14349f718
140872430: ebx = [rax + idx*4]
14087243a: rcx = [rsi+0x4e0]  (the struct wrapper)
140872441: call 0x14046ddf0
140872446: cmp [rsi+0x4f8], 0 ; je → skip
140872457: [rsi+0x4f8] = 0
140872462: cmp ebx, ecx (ecx = current [rax+0x279d])
140872464: je 0x140872490                              ; unchanged → nothing to do
14087246d: call 0x1404335e0                             ; struct-copy ticket
140872479: mov byte [rax+0x279d], bl                    ★ THE WRITE
14087247f: call 0x140445670
140872484: [rsi+0xdc]  = ebx                            ; cached copy
14087248a: [rsi+0x1e8] = ebx
```

So: **mode = `modeTable[pitch*3 + weather]`** with `pitch = i32[cfg+0x78]`,
`weather = i32[cfg+0x7c]`, and out-of-range → 1.

Table at `0x14349f718` (raw bytes `02000000 01000000 01000000 02000000 01000000 00000000
01000000 00000000 00000000`, OBS):

```
             weather 0   1   2
   pitch 0       2    1   1
   pitch 1       2    1   0
   pitch 2       1    0   0
```

### 3.2 Which mode is a dry, normal pitch? (OBS + INF)

**Dry/normal is `weather = 0`** (INF high: `weather = 0` is the no-weather column; the table
ramps to mode 0 = *worst* as weather index rises, and `0x140a3f560` short-circuits to 3).

**Pitch index**: `i32[cfg+0x78]`. In this image `ball.o`'s `pitchCondition` scalar is `u32 1`
(dict §6.1 of part 1, disk `+0xc8`), which is *not* the same field. INF (medium): the two
candidate live answers are

- pitch = 0 → **mode 2** (dry, normal), or
- pitch = 1 → **mode 2** as well.

Both plausible readings of a *dry normal* pitch give **mode 2**, never mode 1.
Mode 1 in this table corresponds to `(pitch,weather) = (0,1)`, `(1,1)`, i.e. **a wet pitch in
light weather** — a plausible reading of ordinary British weather, and consistent with part 1's
"live was always mode 1" (it observed a real match, in whatever weather the user had set).

**Action for the porter:** GF must take the mode as a parameter and default it to
**2 = dry/normal**; the column-1 numbers part 1 quoted are not the dry-normal column. The
contact table's *shape* is identical across modes 0–5 (same code path); only the 6-element
columns differ (§6.2 of part 1), so nothing else in the port changes.

Mode column values needed for mode 2 (OBS, `cm.bin`):

| field | BallBase | m2 |
|---|---|---|
| boundCheckBoundAddRate | +0x14 | 0 (10,5,**0**,0,5,5) |
| boundCheckFrictionAddRate | +0x2c | 0 |
| boundToRotationAddRateXZ | +0x74 | 0.64 |
| boundToRotationBaseXZ | +0xa4 | 0.24 |
| boundToRotationSpeedMinXZ | +0x104 | 3 |
| boundToRotationSpeedMaxXZ | +0xd4 | 25 |
| boundToRotationAddRateY | +0x8c | 0.07 |
| boundToRotationBaseY | +0xbc | 0.03 |
| boundToRotationSpeedMinY | +0x11c | 0.1 |
| boundToRotationSpeedMaxY | +0xec | 0.7 |
| natualBoundA | +0x204 | 0.055 |
| rollToSpeedSpeedYRateBase | +0x2b8 | 0.8 |
| rollToSpeedMaxKPH | +0x294 | **0.8** |
| rollToSpeedSpeedYMin/Max | +0x2b0/2b4 | 0.5/0.1 |
| frictionRollRateMax/Min/Stop | +0x148/160/178 | 0.969 / 0.979 / 0.969 |
| frictionRollSpeedMax/Min/Stop | +0x190/1a8/1c0 | 25 / 8 / 0 |
| gounnderprob | +0x1d8 | 5 |
| boundBoundRate | +0x50 | 0.7 |
| (scalars) boundBoundSpeedMin/Max | +0x14/1c | 2 / 12 |

⚠ **Note for the porter:** `rollToSpeedMaxKPH[2] = 0.8`, not 20 — so `vmax` in §2 becomes
`0.8/3.6 = 0.222 m/s`, and the tangential impulse term `B_` shrinks by 25×. That is a
behaviour difference, not a typo: mode 2 leaves almost all of the tangential velocity to the
`K`-scaled `vx,vz` and adds only a small spin-coupled term.

---

## 4. Where the post-contact `vy` comes from — answer to task item 2

### 4.1 Not the keep step, not −e·vy, not a second routine

- **The keep step (`0.03/0.10`) never multiplies `vy`.** It is `xmm6` at
  `0x14087711b`/`0x140877128`, consumed only by `0x140877169–0x140877185`, whose result is
  `xmm8` — the raw-spin-Y blend at `0x1408771ba`. (OBS. This kills part 1's reading.)
- **There is no `−e·vy` reflection.** The `−` sign appears exactly once, at `0x1408771f5`
  (`xorps xmm9, …`), and it negates the **constant-speed** `imp`, not `vy`.
- **No second contact routine exists.** All ~40 callers (`FUN_140865f20`, `0x140866b60`,
  `0x140868500`, `0x140868ad0`, `0x140869500`, `0x140869d10`, `0x14086a3f0`, `0x14086ab90`,
  `0x14086b690`, `FUN_140874860`, `FUN_140874820`, …) are prediction/measurement passes:
  they call `FUN_140876360` in a loop and contain no contact logic of their own. Ghidra
  `get_function_xrefs` on `FUN_140876360` plus raw `e8`-scan confirm all `call 0x140876360`
  sites sit inside those wrappers; none post-processes `vy`.
- **The caller does not touch `vy` after return.** After `0x14087733c → ret`, the callers
  read `[rsi+4]` for statistics only.

### 4.2 Live-mode `dt`

`DAT_14351ad20 = 0.0185185 = 1/54` (`DAT_14351ae54 = 54.0`, int `54`) — OBS (part 1 §7.3,
three `sudo` reads). The `dt > 0` branch at `0x1408771f5`/`0x140877231` is therefore the
**live** one; the `dt ≤ 0` (rewind) branch divides instead of multiplying and is never taken
in play. Predictors pass positive step multiples, so they take the same branch.

---

## 5. Simulation — answer to task item 5

Script: `<scratch>/BallContact2/final_sim.py` + `verify.py` (scratch, read-only). Literal port of
§1.3–§1.4; `dt = 1/54 s`; gravity `−9.80665 m/s²`; drag omitted on purpose — it scales both
`vy_in` and `vy_out` through `imp` only via `|v_xz|`, which is unaffected by vertical drag at
these speeds, and the ratio is computed inside one tick anyway.

### 5.1 The three requested cases (mode 1, as observed live)

**Case A — 2 m drop, no spin** (`y_centre` starts at 2.0 m):

```
contact #1:  vy_in = -6.1746 m/s   vy_out =  4.5383 m/s   ratio = 0.7350
contact #2:  vy_in = -4.5419 m/s   vy_out =  3.3383 m/s   ratio = 0.7350
contact #3:  vy_in = -3.3811 m/s   vy_out =  2.4851 m/s   ratio = 0.7350
contact #4:  vy_in = -2.4183 m/s   vy_out =  1.7495 m/s   ratio = 0.7235
contact #5:  vy_in = -1.7010 m/s   vy_out =  1.2152 m/s   ratio = 0.7144
```

All five are in the reflection regime: `vy_out = −F·vy_in` with `F` ramping
`0.7 → 0.735` as `s₂` rises.

**Case B — 10 m/s at 30°, no spin:**

```
contact #1:  vy_in = -4.9883 m/s   vy_out =  3.6664 m/s   ratio = 0.7350
contact #2:  vy_in = -3.7794 m/s   vy_out =  2.7779 m/s   ratio = 0.7350
contact #3:  vy_in = -2.8519 m/s   vy_out =  2.0788 m/s   ratio = 0.7289
contact #4:  vy_in = -2.0981 m/s   vy_out =  1.5094 m/s   ratio = 0.7194
contact #5:  vy_in = -1.5778 m/s   vy_out =  1.1248 m/s   ratio = 0.7129
```

**Case C — same kick with 10 rps backspin, and C2 with 10 rps topspin:** identical to B,
to all printed digits. ⚠ This is *expected*, and it is the §3.3 result: a body-axis spin
couples into `vy` nowhere in this function (T_y ≡ 0). The bobble that *looks* spin-driven
comes from the **tangential rewrite** re-pointing the horizontal velocity, and from
`natualBoundA·|v_xz|` — a 10 rps ball still loses horizontal speed to `K`, so `imp` falls,
so `vy_out` falls marginally over successive contacts. That is the honest answer: **PES's
spin does not pump `vy` through this function.** Porting spin-dependent bounce requires a
path this report does not have evidence for (see §7).

### 5.2 The actual dry-pitch numbers — mode 2 (OBS, from `final_sim_m2.py`)

`boundCheckBoundAddRate[2] = 0` ⇒ `F ≡ 0.7` exactly, at every speed. Drop the ramp:

**Case A — 2 m drop, no spin:**

```
contact #1:  vy_in = -6.1746 m/s   vy_out =  4.3222 m/s   ratio = 0.7000
contact #2:  vy_in = -4.3948 m/s   vy_out =  3.0764 m/s   ratio = 0.7000
contact #3:  vy_in = -3.0982 m/s   vy_out =  2.1687 m/s   ratio = 0.7000
contact #4:  vy_in = -2.1898 m/s   vy_out =  1.5329 m/s   ratio = 0.7000
contact #5:  vy_in = -1.5544 m/s   vy_out =  1.0881 m/s   ratio = 0.7000
```

**Case B — 10 m/s at 30°, no spin:**

```
contact #1:  vy_in = -4.9883 m/s   vy_out =  3.4918 m/s   ratio = 0.7000
contact #2:  vy_in = -3.5908 m/s   vy_out =  2.5136 m/s   ratio = 0.7000
contact #3:  vy_in = -2.5714 m/s   vy_out =  1.8000 m/s   ratio = 0.7000
contact #4:  vy_in = -1.8321 m/s   vy_out =  1.2825 m/s   ratio = 0.7000
contact #5:  vy_in = -1.2600 m/s   vy_out =  0.8820 m/s   ratio = 0.7000
```

**Cases C and C2 (10 rps backspin / topspin):** identical to Case B to all printed digits.
Same conclusion, both modes: **PES's spin does not enter `vy` through this function** (§3.3,
`T_y ≡ 0`); the horizontal decay of the spin-carrying ball makes `imp` shrink on later
contacts, which is the only coupling.

The `|vy| ≤ imp` graze regime and its ratio ≥ 1 are unchanged by the mode.

### 5.3 The sweep — does the 0.37–2.26 band fall out? (yes, structurally)

Mode-2 sweep (single contact; mode 1 gives identical rows because `F` is the only mode
difference that touches `vy`):

```
   |vxz| m/s  spin rps  |vy_in|   vy_out   ratio   regime
        0.00       0.0    6.170   4.5350   0.735   reflect -F*vy_in
        1.00       0.0    6.170   4.5350   0.735   reflect -F*vy_in
        5.00       0.0    6.170   4.5350   0.735   reflect -F*vy_in
       15.00       0.0    6.170   4.5350   0.735   reflect -F*vy_in
       30.00       0.0    6.170   4.5350   0.735   reflect -F*vy_in
        0.00       5.0    6.170   4.5350   0.735   reflect -F*vy_in
        5.00       5.0    6.170   4.5350   0.735   reflect -F*vy_in
        5.00      20.0    6.170   4.5350   0.735   reflect -F*vy_in
        5.00     -20.0    6.170   4.5350   0.735   reflect -F*vy_in
       10.00       0.0    0.300   0.8400   2.800   clamp: F*natualBound
       10.00       0.0    0.050   0.6650  13.300   clamp: F*natualBound
       10.00       0.0    0.010   0.6370  63.700   clamp: F*natualBound
        1.00       0.0    0.010   0.1163  11.628   clamp: F*natualBound
        0.50       0.0    0.050   0.0970   1.941   clamp: F*natualBound
        2.00       3.0    0.500   0.3500   0.700   reflect -F*vy_in
```

**The extremes are the two regimes**, and they are adjacent in state space:

- `ratio ≈ F ∈ [0.70, 0.735]` for ordinary falling speeds (case A/B, the drop series);
- `ratio ≫ 1` whenever the contact tick's `|vy| ≤ imp`, i.e. **balls that arrive slow** —
  a ball grazing the pitch with `|vy| = 0.05 m/s` and any `|v_xz| ≥ 2 m/s` gets
  `vy_out = F·imp ≥ 0.5 m/s`, i.e. ratio ≥ 10. Those are the **bobbles** part 1 §5.2 saw
  (`y ∈ [0.109, 0.14]`, apexes needing `vy ≈ 1.4–1.6 m/s`): a graze, not a bounce.
- The `0.37` end of the live band is `F·imp/|vy_in|` with `imp` small and `|vy_in|` large —
  that is the **clamp regime** on the *other* side: `imp = (0.055·3.6·vxz + 0.4)/3.6`, so for
  `vxz ≈ 0.1 m/s`, `imp ≈ 0.113`, `vy_out ≈ 0.083 m/s` against `vy_in = 0.225 m/s` → 0.37.

So the live band `0.37 … 2.26` is exactly
`min(F·imp/|vy_in|, −F)` across one contact and its successor, plus the case where the ratio
> 1 (a graze re-launching). **A single formula reproduces both ends; no coefficient tuning.**

### 5.4 Multi-bounce behaviour and the stop gate

At `stopSpeed = 0.175 km/h = 0.04861 m/s`, only `vx`,`vz` are zeroed. A ball whose vertical
energy falls below the `min(−imp, vy)` clamp **never dies**: it is re-launched at
`F·imp` every tick until `|v_xz|` also drops below the gate. That is why PES balls appear to
"roll forever" rather than stop — and why the stop gate alone (GF's current model) is wrong
without the clamp.

---

## 6. Goalposts, crossbar and net — answer to task item 4 (lower priority)

Findings (all OBS unless marked):

1. `FUN_140876360` handles **ground only**. The only geometry tests in it are `y vs r`
   (`0x1408767fc`) and the 60 × 40 m out-of-box override (`0x140876798 … 0x1408767eb`,
   `|x| > 60` or `|z| > 39.7` → `r := −2.44131398`, i.e. no contact). No post/net branch.
2. The touch-kind enum table is at `0x1434a5c20`, 16 `{const char*}` entries:
   `NONE, POST, CROSSBAR, CFLAG, POLE, BOARD, SIDENET, UPPERNET, BACKNET, SIDENET_IN,
   UPPERNET_IN, BACKNET_IN, STAD, CONE, PANEL, PANEL_STAND` (OBS: 8-byte pointers read from
   the table; strings resolved to the addresses `0x14264f3e0 … 0x14264f480`).
3. **The table is referenced only by itself-plus-adjacent binding tables.** A full-file
   rip-relative scan (all opcode lengths, all aligning) for targets in
   `0x14264f3e0 … 0x14264f4a0` returns **zero** code references; the only 8-byte absolute
   references to those string addresses are the 16 table slots themselves. The surrounding
   region (`0x1434a5bc0 …`) is a run of script-binding tables
   (`FEINT_KIND_*`, `INSIDE_BALL/INFRONT_BALL/OUTSIDE_BALL/INSTEP_BALL/HEEL_BALL/SOLE_BALL/
   TOE_BALL`). INF (high): the frame/net response reads the contact **kind** as an integer
   from the collision record and never resolves the enum names.
4. The goal-frame collision meshes are `cpk_dat/common/render/model/bg/hit/goal/goal_l.geom`
   (string at `0x142594afa`, OBS; four copies of `goal_l.geom` in the image). The name is also
   only in `.rdata`; its code reference is not reachable by a literal xref scan because the
   VFS path is assembled from parts.
5. **The consumer is still not found.** Next step (named, unchanged from part 1 §7.1 but now
   with the location of the record): take the kind-1 contact list on the ball object
   (`collision::Object+0x20` list, count at `+0x28`, ball id 26 at `+0x80`) and diff the ball's
   velocity against `imp` before/after a post hit — live, not static. The static path is
   blocked because the response is dispatched through the collision system's virtual calls
   (INF high: `ObserverBallTouch::Exec`/`match::record::*` are stats-only consumers).

**Status: open, with the blocker reduced to "needs a live post hit".**

---

## 7. What is still open

| item | state | named next step |
|---|---|---|
| which `pitch` value the menu's "normal" writes into `[cfg+0x78]` | OBS writer + table; the pitch index live value unverified | live-read `i32[cfg+0x78]` and `i32[cfg+0x7c]` from a running match (`cfg = [ *(u64*)0x1436F3DC8 + 0x2c0 ]`) — the process left open play during this session, so the probe returned `ctx = 0` |
| the `±3 cm` discrepancy of `F` vs the 0.37 band's exact value | OBS formula, OBS sweep | fit nothing; the band is explained structurally (§5.2) |
| spin → `vy` coupling on a real bounce | **OBS: absent in `FUN_140876360`** | a live bounce trace that records `rawspinY` before/after: if it moves with `vy`, the pump lives in the flight function `FUN_140874470`, not here. `FUN_140874470`'s decompile is the next task |
| frame/net velocity response | open (§6) | live contact-list diff, above |
| mode 2's `rollToSpeedMaxKPH = 0.8` | OBS, unexplained | check whether the shipped data is a typo by comparing m0/m3/m5 columns (`0.8, 0.4, 0.2, 0.2`) — the m5 column also being 0.8 suggests intent, not a typo |

### Live-match note

`PES2021.exe` (pid <pid>) was in open play (77th minute) at the start of this session;
`matchctx = *(u64*)0x1436F3DC8 = 0x1fbfd140`. By the time the mode probe was written the
match had ended (`0x1436F3DC8 = 0`), so §3's live confirmation is indirect: the table and
writer are OBS, the pitch index is INF. The heap scan for the live sim ball
(`finball.py`, 262 k raw shape candidates) did not converge on a single ballstate address
before the match ended; part 1's `watchbounce.py` approach (watch `y → r`) remains the faster
route and needs no locator.

---

## 8. Porter recipe (replaces part 1 §0.1)

```
mode = 2 (dry/normal) or the match's configured mode;  m ∈ 0..5, >5 → 1
dt   = 1/54 s

on each tick, for each ball:
  flight (FUN_140874470) unless resting
  if (y <= r) and not resting:
      y := r ; wobbleFlag := 0
      K := keepK(3.6|v|)                                    # §2.4
      grounder: if 3.6|vxz| >= 20 and |vxz/vy| > 8 and
                   floor(|x|*1000) % gounnderprob[m] == 0:
                   vy -= |vxz|*0.09*(1-f) ; K *= 0.985+f
      F := boundBoundRate[m]*(1 + boundCheckBoundAddRate[m]*s2) + s4²*rollToBoundAdd
      imp := (natualBoundA[m]*3.6*|vxz| + natualBoundB)/3.6
      vy_out := -F * min(-imp, vy_trimmed)      ★ THE BOUNCE
      vx_out, vz_out := K*(vx,vz) + T_xz  (T_xz from §3.3, may be 0 if you skip spin)
      rawspinX/Z := blended ; rawspinY := rawspinY + blend*(spinY/492.181 - rawspinY)
      if |vxz_out| < stopSpeed*1000/3600: vx_out = vz_out = 0     ★ only horizontal
```

`F` is the number a porter wants as "restitution": **exactly 0.7 in the dry-normal mode 2**,
and 0.70–0.735 in mode 1 (wet) — but it must be applied to `min(-imp, vy)`, never to `vy`
directly. The two regimes:

```
if |vy_in| > imp:   vy_out =  +F·imp                 # speed clamp → ratio < 1, falls with speed
else:               vy_out =  −F·vy_in                 # true reflection → ratio = F
```

### 8.1 Note on the companion probe

`~/ghidra-projects/scratch/pes_state.py` (shared, read-only) had a stale ball locator: it
looked for the `BallInfoRef` vtable at `simBall+0x4E0` with the ball position at `+0x64`, and
found nothing in a live match (33 vtable hits, none moving). It now locates the live ball by
matching the **ballstate signature** instead — position in the pitch box, `y ≥ r−0.02`, a
sub-80 m/s velocity, non-zero raw spin at `+0x18`, `mode ∈ 1..5` at `+0x78`, and a unit
quaternion at `+0x7c` — then confirms motion over 0.25 s. Validated against 8 MB of random
noise (zero false positives, 1.5 s cold, 0.3 s cached). Other agents should use it as-is.
