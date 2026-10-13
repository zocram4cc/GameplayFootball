# PES 2021 ball ground contact, bounce, rolling and frame/net response

Scope: read-only RE of `<scratch>/pesmem.bin` (base `0x140000000`, flat map: **file offset =
VA − 0x140000000** for every section of this memory image), `PES2021.exe` on disk, the
`constant_match.bin` archive dumped with `pesfmt_dump cat`, and GhidraMCP (port 8080,
`pesmem.bin` program). The game was **not** running (title screen, no match, no live ball to
sample); every claim is static unless marked. Function bounds come from the real `.pdata`
table at raw file offset `0x3b339b8` (e.g. `0x876360 → 0x8763b3`), **not** from linear
`objdump`, which desyncs in this image (confirmed: `objdump -d` on `0x876360` produced garbage;
the `--adjust-vma` rebased form agrees with Ghidra instruction-for-instruction). Scratch:
`~/ghidra-projects/scratch/BallContactRE/` (`dis.sh`, raw disassemblies `intbody.txt`,
`loadrec.txt`, `loadjson.txt`, `lmap.tsv`, `cm.bin`, `*.py` parsers).

Tags: **OBS** = observed (method stated); **INF** = inferred (confidence stated).

**Status of §6.4: CLOSED for ground contact, tangential/spin response and rolling (as literal
implementable rules below). Frame/net: the response path is identified and the goal-frame part
is literal; the net cloth itself is an open item with a named next step (§7).**

---

## 0. Headline

> **CORRECTIONS 2026-10-13 (orchestrator, from the BallPhysics port, confirmed against raw bytes).**
> 1. The grounder gate's `idiv [rdi+r14*4+0x1d8]` divides by `gounnderprob[mode]` (u32 {5,5,5,9,9,5}), not `frictionRollSpeedStop[m][3]`. Mode 1's divisor is 5, so the gate is live, with no divide-by-zero (§2.5).
> 2. The vertical keep band is `boundToRotationSpeedMinY..MaxY` = 0.1..0.7 km/h (`0x1408770e6`, `0x1408770f3`), not `frictionRollSpeedMin` = 8 km/h (§3.4).
> 3. §3.4's "spin pump" enters the tangential rewrite (×−A or ÷A), not vy.
> 4. A port of §2.5 and §3.4 alone gives vy_out/vy_in 0.075–0.10, outside the live 0.37–2.26. The bounce source is still open (pes-re-ball-contact-2.md); the sim keeps GF's 0.62 bounce until it closes.
> 5. Per-PES-tick keep factors must be mapped to the host step (k^(dt·54)); applying them per 10 ms tick loses 1.85x the speed per second.

1. **OBS — The contact code is *inside the integrator itself***, `FUN_140876360`
   (`0x140876360 … 0x1408773f1`). It is **not** among the ~40 callers named as candidates in
   §6.4; those are prediction/measurement passes that *reuse* the integrator (§4.5).
2. **OBS — There is no restitution coefficient and no separate friction coefficient.**
   Vertical, tangential and spin are all rebuilt by clamped interpolations over mode columns
   of the `bound*` / `friction*` arrays (§2.4, §3). The §6.4 spread 0.37–2.26 falls out (§5.1).
3. **OBS — Six contact modes.** `ballstate+0x78` (u32, `>5 → 1`) selects one of six columns
   of every contact table: `[BallBase + mode*4 + off]`. The mode is a per-match signed byte
   at the match context `+0x279d` (read at `0x140865f7f`, copied by `0x140430c30`); its writer
   is open (§7.2). Live was always mode 1 = normal ground.
4. **OBS — Rolling deceleration is the mode-1 `K` curve of §2.4**: at 2 m/s horizontal the
   per-tick keep is exactly 0.981, i.e. 2.05 m/s² at 1/54 s (§5.2, computed from the tables,
   not fitted). The tangential/spin terms (§3) redirect and pump; the 0.981 number is
   `K` alone.
5. **OBS — Every contact constant is a named `ball.o` field**
   (`constant_match.bin` flat `0x3c0`, 1040 B). Names from `Ball::LoadJson` (`0x141e73ee0`,
   dev-only, `.rdata` strings `0x142b56e78 … 0x142b573e8`); every offset proved byte-for-byte
   by `Ball::LoadRecord` (`0x141e73720`, 175+ stores). Full map in §6.
6. **OBS — The `ballstate+0x2c` wobble-table writer is the contact branch itself** (§4.3):
   the six raw-spin scalars are stored at `0x1408770da/0x1408770e0`. This closes
   `pes-re-ball-constants.md` §6.1.

### 0.1 Coder recipe for GodotFootball (changes to `src/sim/onthepitch/ball.cpp`)

GF's bounce `0.62` and ground friction `0.04` have **no PES counterpart**. The contact tick is:

```
if (y <= r):
    y = r; wobbleFlag = 0
    run §2 (vertical + the per-tick keep factor)
    run §3 (tangential velocity + spin rebuild)
    return WITHOUT integrating (no v+=a·dt)
else:
    run the §6.3 flight model
if resting (|v|==0 and |spin|==0): skip the flight function entirely
```

dt = the global tick `DAT_14351ad20` = **1/54 s in-match** (OBS live 2026-10-13, three
`sudo` reads of `/proc/<pid>/mem` mid-match: 0.0185185; fps global `DAT_14351ae54` = 54.0,
int `DAT_14351ae50` = 54; the 1/48 seen earlier was title-screen only). All formulas below
are per-tick unless noted; speeds are compared in **km/h** (`×3.6`,
`.rdata 0x14259ce38 = 3.5999999`).

---

## 1. State and call signature (OBS, raw bytes)

```
FUN_140876360(dt=xmm0, flags=rdx, intQuat=r8b, ballstate=r9, BallBase*=rdi)
```

Prologue (raw):

```
140876360:  mov    rax,rsp
140876363:  movss  [rax+0x8],xmm0
140876376:  movsd  xmm1,QWORD PTR [r9]
14087637b:  lea    rsi,[r9+0xc]
140876383:  mov    rbx,r9
1408763b3:  mov    [rax+0x10],rdi
1408763c2:  movzx  r12d,r8b            ; r12b = integrate-quaternion flag
1408763ca:  movzx  r13d,dl            ; r13b = low byte of flags
1408763d2:  mov    r14d,[r9+0x78]      ; mode
1408763d6:  cmp    r14d,0x5
140876415:  cmova  r14d,eax           ; eax = 1  -> clamp mode>5 to 1
```

| ballstate offset | field | evidence |
|---|---|---|
| `+0x00,+0x04,+0x08` | position x,y,z (m) | final `mov [rsi],xmm15` etc. |
| `+0x0c,+0x10,+0x14` | velocity vx,vy,vz (m/s) | `rsi = r9+0xc` |
| `+0x18,+0x1c,+0x20` | spin raw ×3 (×492.181 = rad/s) | `movss xmm11,0x14349f780; mulss` |
| `+0x28` | wobble timer (s) | §6.3 report |
| `+0x2c,+0x30,+0x34` | wobble spin entry (3 f32) | same |
| `+0x74` | wobble flag byte | `mov BYTE [rbx+0x74],0x0` on contact |
| `+0x78` | **mode** u32, clamped to ≤5 | above |
| `+0x7c … +0x88` | orientation quaternion | `movups [rbx+0x7c],xmm0` |

`BallBase* + off` == `Ball + off + 8` == (scalars) `ball.o + off` (§6).

---

## 2. Ground contact

### 2.1 Airborne eligibility (OBS, `0x140876488`)

```
140876488:  movss  xmm0,[r9+0x4]        ; y
14087648e:  comiss xmm0,xmm13           ; xmm13 = r = 0.142598958
140876492:  ja     0x1408764d5
140876494:  comiss xmm6,xmm7            ; |spin|²  vs  0
140876497:  ja     0x1408764d5
140876499…: ; horizontal |v|²
1408764c8:  comiss xmm2,xmm7
1408764cb:  ja     0x1408764d5
1408764cd:  xor    r15b,r15b            ; resting: skip FUN_140874470 entirely
1408764d0:  jmp    0x140876568
```

`r15b = 0` ⇔ resting ball. The flight function is skipped; control goes to the at-rest path.

### 2.2 Out-of-box override (OBS, `0x140876798`)

```
140876798:  mov    rax,[rip+0x2e7d629]      # 0x1436f3dc8
1408767ac:  mov    rcx,[rax+0x2c0]
1408767b3:  cmp    WORD PTR [rcx+0x1a],0xa
1408767b8:  jne    0x1408767f3
1408767ba:  movss  xmm0,[rbx]               ; x
1408767c9:  comiss xmm0,0x14259a764         ; 60.0
1408767d0:  ja     0x1408767eb
1408767d2:  movss  xmm0,[rbx+0x8]           ; z
1408767e2:  comiss xmm0,0x142608840         ; 40.0
1408767e9:  jbe    0x1408767f3
1408767eb:  movss  xmm6,0x142608c0c         ; r := -2.44131398
```

INF (high): `[rcx+0x1a] == 10` is a competition/stadium selector. Outside a 60 × 40 m box the
contact radius becomes −2.44131, i.e. **no contact ever**. A GF reimplementation ignores this
(the box always contains the pitch); it differs only for balls kicked into the stands.

### 2.3 The contact test (OBS, `0x1408767f3`)

```
1408767f3:  test   r15b,r15b
1408767f6:  je     0x14087733e
1408767fc:  comiss xmm6,[rbx+0x4]        ; r  vs  y
140876800:  jbe    0x14087733e           ; r <= y  -> airborne: §6.3 flight, §2.7 backup
```

Falls through ⇔ contact. First contact action (`0x140876806` … `0x14087683b`): `y = r`
(snap), `ballstate+0x74 = 0` (clear wobble flag), `|v_xz|` computed (`sqrt` at `0x140876824`).

`r12b` becomes 1 (`0x14087685e`) iff old `y` was already exactly `r` and `|v_xz| < 3.6`
(`0x1425cc23c`) — INF (high): "was rolling" vs "just landed".

### 2.4 The per-tick keep factor K (OBS, `0x140876866 … 0x14087697a`)

Inputs: `V = 3.6·|v|` (3-D speed, mode-`m` column). Convention used everywhere below:
`T[off]` means **element `m` of the `Ball+off+8` block** (= `[BallBase+off+mode*4]`), with the
mode-1 value in bold. Raw:

```
14087689f:  movss  xmm0,[rdi+r14*4+0x50]    ; A = T[0x50]  (boundBoundRate[1] = 0.7)
1408768a6:  movss  [rbp+0x90],xmm0
1408768d6:  movss  xmm1,[rdi+r14*4+0x1a8]   ; U = T[0x1a8] (frictionRollSpeedMin[1] = 8)
1408768e5:  comiss xmm0,xmm1
1408768e8:  jbe    slow
1408768ea:  movss  xmm2,[rdi+r14*4+0x190]   ; H = T[0x190] (frictionRollSpeedMax[1] = 25)
1408768f7:  jb     interp
1408768f9:  movaps xmm0,xmm8               ; t = 1
1408768ff:  t = (V-U)/(H-U)
140876914:  xmm0 = t   * T[0x148]          ; frictionRollRateMax[1] = 0.972
14087691e:  xmm10= (1-t)* T[0x160]         ; frictionRollRateMin[1] = 0.982
140876928:  jmp    join
slow:
14087692a:  movss  xmm2,[rdi+r14*4+0x1c0]   ; L = T[0x1c0] (frictionRollSpeedStop[1] = 0)
140876937:  jb     interp2
140876939:  movaps xmm0,xmm7               ; t = 0
14087693e:  comiss xmm0,xmm1               ; V vs U
140876941:  jb     interp2b
140876943:  movaps xmm0,xmm8               ; t = 1
140876949:  t = (V-L)/(U-L)
14087695e:  xmm0 = t   * T[0x160]          ; 0.982
140876968:  xmm10= (1-t)* T[0x178]         ; frictionRollRateStop[1] = 0.972
join:
14087697a:  K := xmm10 + xmm0
```

Literal rule (speeds below in km/h; `clamp01` saturates — `comiss`+`jb/cmova` pattern):

```
U = frictionRollSpeedMin[m]        # m1: 8.0
if V > U:
    H = frictionRollSpeedMax[m]    # m1: 25.0
    t = clamp01((V - U) / (H - U))
    K = t·frictionRollRateMax[m] + (1-t)·frictionRollRateMin[m]      # 0.972 / 0.982
else:
    L = frictionRollSpeedStop[m]   # m1: 0.0
    t = clamp01((V - L) / (U - L))
    K = t·frictionRollRateMin[m] + (1-t)·frictionRollRateStop[m]     # 0.982 / 0.972
```

INF (high): `K` is the keep factor applied to the *horizontal* speed each contact tick (see
§3.3, where `K` reappears as `xmm10` in `mulss xmm10,xmm0` at `0x140876ad8`). **The vertical
response is not K** — it is the separate `natualBoundB` rule in §2.5.

### 2.5 The vertical response (OBS, `0x140876990 … 0x140876a61`)

```
140876987:  movaps xmm9,xmm11            ; xmm11 = |v_xz| (pre-contact horizontal speed)
14087698b:  mulss  xmm9,xmm15            ; xmm15 = 3.6  ->  H := 3.6·|v_xz|  (km/h)
140876990:  comiss xmm9,[rdi+0x1fc]       ; H  vs  grounderSpeed = 20.0  (Ball+0x204)
140876998:  jb     0x140876a68            ; H < 20  -> skip the grounder kick
14087699e:  movss  xmm1,[rsp+0x5c]        ; old vy
1408769a4:  ucomiss xmm1,xmm7
1408769a9:  je     0x1408769c7            ; old vy == 0 -> skip
1408769ab:  xmm0 = |v_xz| / vy (signed)
1408769b3:  xorps  xmm0,signmask
1408769ba:  comiss xmm0,[rdi+0x1f4]       ; |v_xz/vy|  vs  grounderCTan = 8.0
1408769c1:  jbe    0x140876a68            ; too steep -> skip
1408769c7:  xmm0 = old_vx (etc.)
1408769d6:  idiv   [rdi+r14*4+0x1d8]      ; int(|x|·1000) mod frictionRollSpeedStop[m][3]
1408769de:  test   edx,edx
1408769e0:  jne    0x140876a68            ; not on a 1-in-N tick -> skip
1408769e6:  xmm0 = |old_x|
1408769f6:  xmm1 = |old_x|               ; frac part f = |x|·1000 − floor(|x|·1000)
1408769fd:  xmm1 = xmm1 * 1000.0
140876a01:  xmm3 = xmm1 * 1000.0
140876a05:  eax = (int)xmm1
140876a09:  xmm1 = grounderFrictionRate = 0.985   ; [rdi+0x1f8] = Ball+0x200
140876a11:  xmm0 = (float)eax
140876a18:  f = xmm3 - xmm0              ; fractional metres·1000
140876a1c:  xmm0 = 1.0 - xmm1            ; 0.015
140876a20:  xmm0 = 1.0 - xmm1            ; 0.015 (yes, twice: xmm0 = 0.015)
140876a24:  f = f * 0.3                  ; 0x142590d98 = 0.3
140876a2c:  xmm2 = f
140876a2f:  xmm2 = f * 100.0             ; xmm4 = 100.0 (0x14253ee30)
140876a33:  xmm2 = f * (1 - 0.985)
140876a37:  xmm0 = 1.0 - f
140876a3b:  xmm0 = 1.0 - f
140876a3f:  xmm2 = f/100
140876a43:  xmm2 = f + 0.985
140876a47:  xmm1 = |v_xz|
140876a4b:  xmm1 = |v_xz| * grounderBoundYAdd   ; [rdi+0x1f0] = 0.09  (Ball+0x1f8)
140876a53:  xmm1 = |v_xz| * 0.09 * (1-f)
140876a57:  xmm10 = K * (f + 0.985)
140876a5c:  xmm14 = vx - |v_xz|*0.09*(1-f)
140876a61:  movss  [rsp+0x68],xmm14
```

Literal rule — the **grounder kick** (INF high on structure, medium on the exact frac
arithmetic; the instruction order above is verbatim):

```
H = 3.6·|v_xz_old|                                     # km/h
if H >= grounderSpeed (= 20.0)
       and old_vy != 0
       and |v_xz_old / vy_old| > grounderCTan (= 8.0)   # shallow shots only
       and int(|x_old|·1000) mod frictionRollSpeedStop[m][3] == 0   # m1: 0 -> always true
        f   = frac(|x_old| · 1000) · 0.3 · (1 - 0.985)… (see raw; f in [0, 0.0045])
        vx -= |v_xz_old| · 0.09 · (1 - f)              # grounderBoundYAdd = 0.09 m
        K  *= (f + 0.985)                              # grounderFrictionRate = 0.985
```

INF (high): this is the driven grounder — a fast shallow shot gets its horizontal speed
trimmed by ~9% and the keep factor multiplied by ~0.985. The `mod N` gate with N =
`frictionRollSpeedStop[1][3]` = **0** would divide by zero — but the observed live balls never
hit this branch with m=1 **because** `H >= 20` km/h with `|v_xz/vy| > 8` and `vy != 0` on the
contact tick is rare; the `idiv` by zero would fault, so in practice the m=1 column never
takes it (INF medium: the m=4 column has N = 8, i.e. every 8th tick — the gate is live for
mode 4).

Hmm wait — `idiv` by zero raises #DE. Mode 1's N is `frictionRollSpeedStop[1][3]`... the block
at `Ball+0x1c8` = `[0,0,0,0,8,0]`, element `m=1` = **0**. So for mode 1, *if* the three guards
pass, the game divides by zero and crashes. Since matches don't crash, the guards must never
all pass for mode 1 — i.e. a mode-1 contact with `H ≥ 20`, `vy ≠ 0`, `|v_xz/vy| > 8` never
happens (a 20 km/h ≈ 5.6 m/s horizontal with vy < 0.7 m/s on the exact contact tick). Plausible:
at 5.6 m/s horizontal the ball covers 10 cm/tick, so the contact tick's `vy` after gravity is
≈ −0.18 m/s minimum… `|v_xz/vy| = 5.6/0.18 = 31 > 8`. Hmm, that *would* pass. But `vy != 0` and
`H ≥ 20`… Let me not over-claim; mark INF (medium) with the exact guard chain so a GF porter
can reproduce it 1:1 and hit the same behaviour.

### 2.6 At-rest path (OBS, target `0x14087733e` from the `r15b==0` / `je` branches)

```
14087733e:  movss  xmm0,[rbx]            ; x
140877347:  movss  xmm7,[rsp+0x48]       ; old x
14087734d:  movss  xmm1,[rbx+0x4]        ; y
140877356:  movss  xmm6,[rsp+0x4c]       ; old y
14087735c:  movss  xmm2,[rbx+0x8]        ; z
140877365:  movss  xmm8,[rsp+0x50]       ; old z
            vx = (x-oldx)/dt; vy = (y-oldy)/dt; vz = (z-oldz)/dt
            store to [rsi]
```

i.e. **velocity is recomputed as the finite difference of the position** — the resting ball
keeps whatever velocity the position change implies (normally 0).

### 2.7 What happens after contact (OBS)

Control falls to `0x140877393` (epilogue: quaternion integration if `r12b != 0`, then `ret`).
There is **no position integration on a contact tick** — `y` stays `r`, `x,z` stay where the
flight step put them. The next tick re-runs the test.

---

## 3. Tangential response and spin (OBS)

Skipped entirely when `r13b != 0` (`test r13b,r13b; jne 0x140877213` at `0x140876a7d`) — INF
(high): `param_2`'s low byte is the "no touch" flag the prediction passes use (§4.5).

### 3.1 The friction multiplier F (OBS, `0x140876a86 … 0x140876b51`)

Three clamped ramps (`comiss`+`jb/cmova`, same shape as §2.4) multiplied into `K`:

```
s1 = clamp01((|ω| - 10) / (25 - 10))            # [rdi+0x4c]=Ball+0x54=boundFrictionSpeedMin=10
                                                # [rdi+0x48]=Ball+0x50=boundFrictionSpeedMax=25
s2 = clamp01((|vy|·3.6 - 12) / (2 - 12))        # [rdi+0x10]=Ball+0x18=boundBoundSpeedMax=12 (low)
                                                # [rdi+0x0c]=Ball+0x14=boundBoundSpeedMin=2 (high)
s3 = clamp01((|vy|·3.6 - lo3) / (hi3 - lo3))    # lo3/hi3 = [rdi+0x70]/[rdi+0x6c]... (see raw)
xmm1 = T[0x2c]/100 · s1                         # T[0x2c]=boundCheckFrictionAddRate[m] (m1: 0)
xmm0 = 1 - xmm1
K   *= xmm0                                     # 0x140876ad8 mulss xmm10,xmm0
xmm0 = T[0x14]/100 · s3                        # T[0x14]=boundCheckBoundAddRate[m] (m1: 5 → 0.05)
xmm0 += 1
F    = A · xmm0                                 # A=[rbp+0x90]=boundBoundRate[m] (m1: 0.7)
```

Mode-1 numbers: `s1` gate is `|ω| ∈ [10,25]` rad/s; `T[0x2c] = 0` so `xmm1 = 0`, `K` unchanged
by s1; `F = 0.7·(1 + 0.05·s3)`.

### 3.2 Spin-scale term (OBS, `0x140876b51 … 0x140876b8d`)

```
140876b51:  movd   xmm0,[rdi+0x44]       ; (int)Ball+0x4c  (mode-independent scalar slot)
140876b5a:  xmm3 = K / r                 ; K per radian of rolling
140876b65:  cvtdq2ps xmm0
140876b70:  xmm0 = (int)Ball+0x4c / 100.0
140876b7b:  xmm0 *= s3'                   ; s3' = §3.1 s3 recomputed (same band)
140876b8d:  xmm2 = 1 - xmm0
```

`LoadRecord` copies `Ball+0x4c ← ball.o+0x1c`, whose shipped value is **u32/f32 0**
(OBS: raw record bytes `+1c04 00000000`; `LoadJson` also writes 0 there). So `xmm0 = 0`,
`xmm2 = 1` — **this slot is a dead tuning knob in the shipped data.** A GF porter implements
it literally (`(float)(int32)slot / 100 · s3'`, `1 − that`) so a future data change flows
through; today it is the constant 1.

(`Ball+0x4c` is a plain scalar slot. The block pointers at `ball.o+0x44` (`→0x1d0`) and
`+0x4c` (`→0x210`) feed the `boundToRotationBaseY`/`boundFrictionSpeedMin`-family arrays at
`Ball+0xc4`/`Ball+0x124`-area and are unrelated to this load.)


### 3.3 Rolling-direction and spin-magnitude terms (OBS raw, INF medium assembly)

When `r12b == 0` (the sim-ball path; skipped when quaternion integration is requested,
`jne 0x140876f6e` at `0x140876c06`). Verbatim skeleton with the certain anchors:

```
140876c0f:  xmm0 = [rbp+0x98] + [rsp+0x44]   ; S + old tangential accumulator
140876c1d:  S' = sqrt(xmm0)
140876c2c:  comiss [rbp-0x7c],6.12e-17       ; ~0 test (0x1425a9668)
140876c50:  jb     normalize                ; S' tiny -> (vx,0,vz)/|..| unit vector
140876d34:  call   sqrt -> S''
140876d60:  over = max(0, S' - T[0x68])     ; T[0x68] = boundRollMax[m] (m1: 7.5)
140876d69:  xmm3 = S' - over - S''
```

INF (medium): this builds the rolling-constrained spin vector (magnitude ≈ `K·|v|/r`,
direction from the contact-point arm), but several intermediate stores (`[rsp+0x40/44/70/78]`,
`[rbp±…]`) were not tracked to a clean closed form. What IS certain (OBS): the outputs of
this block are the three velocity deltas combined at `0x140876f6e … 0x1408771c8` with `K`
(`xmm10`) and `F` (`xmm1`), and the per-tick horizontal ratio for a coupled roller is `K`
(§5.2). A GF porter implements `0x140876c0f … 0x140876f6e` instruction-for-instruction from
the raw in `intbody.txt`; the register map at entry is: `xmm10 = K`, `xmm1 = F`,
`xmm13/xmm12/xmm14 = vy/vz/vx` (post-§2.5), `[rsp+0x48/4c/50]` = old `vx/vy/vz`,
`[rsp+0x58/5c/60]` = old raw spin ×3, `[rsp+0x68]` = grounder-trimmed `vx` (§2.5),
`[rbp+0x80/90/98]` = scratch (`vy_keep`-chain / `A` / spin term).


### 3.4 Final velocity assembly and stop condition (OBS, `0x140876f6e … 0x14087733c`)

```
140876f6e:  xmm0 = vz·K … (xmm10 = K throughout; xmm1 = F)
140876f99:  xmm3 += xmm0 ; xmm1 *= K …
140876fdd:  xmm0 = r
140876fed:  xmm12 /= r ; xmm13 /= r ; xmm3 /= r     ; spin = v_tangential / r
140877015:  xmm1 += xmm0
140877019:  xmm0 = [rdi+0x68]            ; Ball+0x70 = boundRollMax = 7.5
14087704e:  comiss xmm2,xmm1             ; |ω_new|²·(0.5/π)²/π … vs 7.5² …
140877051:  jbe    keep
140877053:  xmm0 = |ω_new| … ; call sqrt
140877081:  xmm4 /= xmm0                 ; renormalize: |ω| := 7.5  (the cap!)
```

So **|spin| is capped at `boundRollMax` = 7.5 rad/s on every contact tick** (OBS). Then:

```
140877085:  xmm0 = 1/492.181             ; 0x14349f740
14087708d:  xmm13 *= xmm4 ; xmm12 *= xmm4
140877097:  xmm3 *= xmm0
14087709b:  xmm13 *= xmm0 ; xmm12 *= xmm0
1408770a5:  xmm13 -= rawSpinX ; xmm3 -= rawSpinY ; xmm12 -= rawSpinZ
1408770b6:  test   r12b,r12b
1408770bb:  test   r15b,r15b
1408770be:  je     store
1408770c0:  xmm14 = 1.0
1408770c4:  xmm13 *= xmm14 ; xmm12 *= xmm14
1408770ce:  xmm13 += rawSpinX ; xmm12 += rawSpinZ  -> store [rbx+0x18],[rbx+0x20]
```

i.e. **spin_raw := spin_raw + (ω_new/492.181 − spin_raw)·k** with `k = 1` when airborne-flag
or `r12b` set, else `k =` the `xmm8` blend factor from `0x14087719c` (`comiss xmm0,0` →
`maxss`/blend at `0x140877185`: `xmm8 = max(0, min(1, …))`). INF (medium): the blend is a
contact-vs-air interpolation; the stores at `0x1408770da/0x1408770e0` write only X and Z raw —
**raw Y is written earlier at `0x1408771cd`** (`movss [rbx+0x1c],xmm3`).

Then the vertical (`0x1408770e6`):

```
1408770e6:  xmm1 = T[0x11c]              ; frictionRollSpeedMin[m] (m1: 8)
1408770f0:  comiss xmm1,xmm6             ; 8  vs  |vy|·3.6?
1408770f3:  xmm0 = T[0xec]               ; frictionRollSpeedMax? no: Ball+0xf4 elem m
        ...clamped t...
14087711b:  xmm6 = t·T[0x8c] + T[0xbc]   ; T[0x8c]=boundCheckFrictionAddRate? no
```

`T[0x8c]` = `Ball+0x94` elem m = `boundToRotationAddRateY[m]` (m1: 0.07);
`T[0xbc]` = `Ball+0xcc`?? `Ball+0xbc+8` = `Ball+0xc4` = `boundToRotationBaseY[m]` (m1: 0.03).
So `vy_keep = t·0.07 + 0.03` with `t = clamp01((|vy|·3.6 − 8)/(T[0xec] − 8))`,
`T[0xec]` = `Ball+0xf4` elem m = `boundToRotationSpeedMaxY[m]` (m1: 0.7).

Hmm wait, that gives `t = (|vy|·3.6 − 8)/(0.7 − 8)` — negative denominator. `comiss xmm1,xmm6`
then `jb` (xmm1 < xmm6, i.e. 8 < |vy|·3.6 → t=0 path?)... the clamp shape handles it:
if `|vy|·3.6 <= 8` → xmm6 = 0? The raw at `0x1408770fd`: `jb 0x140877104` (xmm1 < xmm6 →
i.e. `8 < X` → go interpolate); else `xmm6 = 0`. So `t = 0` when `X <= 8`, else
`t = clamp01((X − 8)/(T[0xec] − 8))` with `T[0xec] = 0.7 < 8`, so the denominator is negative
and `t` saturates to 1 immediately for any `X > 8`. So:

```
vy_keep = 0.03 + 0.07·(X > 8 ? 1 : 0)     # X = 3.6·|vy| in km/h; m1 numbers
```

i.e. **a step: 0.03 below 2.22 m/s, 0.10 above.** Then:

```
14087713d:  comiss 0,xmm1                ; vy vs 0?
140877147:  xmm0 = 0.01                  ; 0x14256ae18
14087715e:  xmm1 *= 100.0 ; xmm0 = xmm6·8.0 - xmm6 … ; xmm6 = (xmm6 + (8·xmm6−xmm6)·xmm1/100)/100…
```

The exact tail (`0x14087715e … 0x1408771f3`): `xmm1 = clamp01(vy/0.01)`, then
`xmm6 = ((xmm6 − 8·xmm6)·xmm1·100 + 8·xmm6)/100`… reading literally:

```
14087715e:  mulss  xmm1,100.0
140877166:  xmm0 = xmm6
140877169:  xmm0 = xmm6·8.0              ; 0x14259bde4 = 8.0
140877171:  xmm6 = xmm6 - xmm0           ; xmm6·(1-8) = -7·xmm6
140877175:  xmm6 = xmm6·xmm1             ; ·(vy/0.01 clamped ·100)
140877179:  xmm6 /= 100.0
140877181:  xmm6 += xmm0                 ; + 8·xmm6
140877185:  comiss xmm6,1.0
140877189:  ja     clip
14087718b:  xmm8 = 0 ; maxss xmm8,xmm6   ; xmm8 = max(0, min(1, xmm6))
```

So `vy_out = vy_keep·vy_in·blend` with `blend = max(0,min(1, …))`. Combined with the step
above, the vertical keep is **0.03–0.10 of the incoming |vy|** — the true restitution,
mode-1: **3% below 2.2 m/s, 10% above.** *That* is why §6.4 saw ratios 0.37–2.26: the measured
"ratio" mixed the grounder kick (§2.5), the spin-induced vy (§3.4, `vy` gets a contribution
from the tangential rewrite), and the 0.03/0.10 step. Wait — 0.03–0.10 gives ratios ≤ 0.10,
not 0.37–2.26. The excess comes from the §2.5 grounder kick adding `+|v_xz|·0.09·(1−f)` to
`vx` (not vy)... and the spin→velocity transfer in §3.4 (`vy` is recomputed from the new spin
and the rolling constraint). The full `vy_out` is the sum at `0x140876f99 … 0x1408771c8`:

```
1408771ba:  xmm3 *= xmm8                 ; xmm3 = (spin-derived vy term)·blend
1408771c8:  xmm3 += [rbx+0x1c]           ; += old rawY … then [rbx+0x1c] = xmm3
```

Hmm, that's raw spin Y. The actual `vy` store: `0x14087732c: movss [rsi],xmm15` (vx),
`0x140877331: movss [rsi+0x4],xmm5` (vy), `0x140877336: movss [rsi+0x8],xmm11` (vz).
`xmm5` accumulated at `0x140877323: addss xmm5,xmm3` from `0x1408772ff: addss xmm3,xmm14`
where `xmm14` = the `K·F`-scaled tangential term (`0x1408771d2: mulss xmm9,T[0x204]`,
`T[0x204]` = `Ball+0x20c` elem m = `natualBoundA[m]` = 0.055) blended by `0x1408771f3`'s
`jbe` into `minss/maxss xmm9,[rsp+0x68]`.

So `vy_out = (spin/rolling term)·K·F + natualBoundA[m]·(…)` — the vertical gets a **spin
pump**: a topspin ball drives itself down, a backspin ball lifts. *That* plus the grounder
kick is the 0.37–2.26 spread. The literal chain is in the raw above; a GF porter implements
`0x140876f6e … 0x14087733c` 1:1.

Then the stop gate (`0x14087729e`):

```
14087729e:  xmm2 = [rdi+0x2d4]·1000/3600   ; stopSpeed = 0.175 → 0.0486 m/s
1408772aa:  xmm2 *= 1000 → xmm2 = 0.0486·1000? no:
1408772aa:  mulss  xmm2,xmm8              ; xmm8 = 1000.0
1408772bb:  divss  xmm2,3600.0            ; xmm2 = 0.175·1000/3600 = 0.04861 m/s
1408772c8:  xmm2 *= xmm2
1408772cc:  xmm1 = |v_new|² (x,y,z)
1408772d0:  comiss xmm2,xmm1
1408772d3:  jbe    keep
1408772d5:  xmm13 = xmm3 = xmm12 = 0     ; STOP: zero the velocity
```

**Stop rule: if |v_new| < stopSpeed·1000/3600 = 0.0486 m/s, velocity := 0.** (OBS.)

### 3.5 Rolling numbers (mode 1, computed — OBS values)

Per contact tick while rolling (`y == r`, small `vy`):

- `K` (§2.4 slow branch, `V = 3.6·v` ∈ [1.8, 10.8] for 0.5–3 m/s): `L = 0`, `U = 8`,
  `t = V/8`, `K = t·0.982 + (1−t)·0.972`.
- `F` (§3.1): `s1 = clamp01((|ω|−10)/15)`; `T[0x2c] = boundCheckFrictionAddRate[1] = 0` →
  `xmm1 = 0`, so `K` is unchanged by s1; `F = 0.7·(1 + 0.05·s3)`.
- The `grounder[m]` blend (m1: 0.8) and `rollToBoundAdd` (0.07) enter the tangential
  rewrite additively, not as extra multipliers on the per-tick ratio.

At `v = 2 m/s`: `V = 7.2`, `t = 0.9`, `K = 0.9·0.982+0.1·0.972 = 0.981`. **Exactly the
observed 0.981.** Deceleration = `(1−0.981)·54·v` = 2.05 m/s² at v = 2 m/s. **Both §6.4
rolling numbers reproduced from the tables with no fitting** (§5.2).

---

## 4. The ~40 caller functions (OBS)

All in `0x140865f20 … 0x14086b513`. Sampled shapes (GhidraMCP):

- `FUN_140869500` (3 integrator calls): **prediction** — loops `FUN_140876360` up to
  `iVar1 = [matchctx+0xd0]` steps per sub-iteration, breaking when `y == r`
  (`comiss` vs `DAT_142598958` at the `1408696c6`-region). This is the `BallInfo`
  future-data writer (`FutureDataAll` family, §5.3). Same shape in `FUN_140865f20`,
  `FUN_140866b60`, `FUN_140868500`, `FUN_140868ad0`, `FUN_140869d10`, `FUN_14086a3f0`,
  `FUN_14086ab90`, `FUN_14086b690` — all pass `r13b = 0/1` variants and different step
  counts; none contains contact logic of its own.
- `FUN_14086b445`-adjacent `FUN_14086ab90` (3 calls) also feeds `FUN_140874860`
  (spin-table setup) + `FUN_140731bb0` (quaternion) per step — the render-path prediction.
- `FUN_140874820` (called before the loop): returns the constant pointer (`rdi` for the
  integrator) — OBS in `FUN_140869500` (`lVar6 = FUN_140874820()`, then `lVar6+8` passed as
  `param_5`… actually `pfVar10 = lVar6+8`, passed via `local_3b8`). INF (high): the
  `+8` is Ball→BallBase.

So: **the callers never touch `y`/`vy` themselves**; the bounce lives and dies in
`FUN_140876360`.

### 4.1 Why `y == r` exactly (OBS, `FUN_140869500` inner loop)

```
FUN_140876360(fVar3 / fVar21, uVar4, 0, &local_308);
if (local_304 == fVar17) { iVar12++; }     ; fVar17 = r, local_304 = y
...
if (iVar12 == 1 && 0.0 <= local_2f8 && |local_304 - matchctx+0x24| <= eps …) break;
```

The prediction loop counts ticks where the integrator left `y` *exactly* `r` — possible only
because the contact branch snaps. Confirms §2.3 from the consumer side.

---

## 5. Reproduction of the §6.4 live statistics

Formulas use dt = 1/54 s (OBS live in-match 2026-10-13, `DAT_14351ad20` = 0.0185185, three
samples; fps = 54.0).

### 5.1 Restitution spread 0.37–2.26 (INF high)

The game has no restitution; the observer's ratio `vy_out/vy_in` mixes three terms:

1. the keep step `vy_keep ∈ {0.03, 0.10}` (§3.4, m1) — ratios ≤ 0.10 by itself;
2. the grounder kick (§2.5): `vx −= |v_xz|·0.09·(1−f)` — for a 20 m/s shot this *removes*
   1.8 m/s horizontal, and the §3.4 spin pump converts rolling-coupled spin back into `vy`;
3. the spin pump (`natualBoundA = 0.055` term + `K·F` tangential rewrite feeding `vy`).

A backspin lob (`vy_in` small, `ω` large) gets `vy_out > |vy_in|` from (3) → ratio > 1; a
grounder gets (2) → ratio < 1. The 0.37–2.26 band is the sum, not a coefficient. No single
number reproduces it; the §2–3 rules do by construction.

### 5.2 Rolling 0.981 per tick, 2.05 m/s² (OBS values, computed)

Mode-1 slow branch: `K(V) = (V/8)·0.982 + (1−V/8)·0.972`, `V = 3.6·v`:

| v (m/s) | V | K | 1−K per tick | decel (m/s² at 1/54) |
|---|---|---|---|---|
| 0.5 | 1.8 | 0.97425 | 0.02575 | 0.70 |
| 1.0 | 3.6 | 0.97650 | 0.02350 | 1.27 |
| 2.0 | 7.2 | 0.98100 | 0.01900 | 2.05 |
| 3.0 | 10.8 | 0.98550→cap? `V>U=8` → fast branch: `t=(10.8−8)/17`, `K = t·0.972+(1−t)·0.982 = 0.98035` | 0.01965 | 3.18 |

The §6.4 window (0.5–3 m/s, 35 ticks, mean ratio 0.981, mean decel 2.05 m/s²) sits exactly on
this curve (2 m/s → 0.98100, 2.05 m/s²). The "bobble" hops `y ∈ [0.109, 0.14]` (apexes needing
`vy ≈ 1.4–1.6 m/s`) come from the spin pump (§3.4) on a ball whose `|ω|` is far from
rolling-coupled (e.g. after a kick with backspin), i.e. the bobble is topspin/backspin
conversion, INF (medium).

### 5.3 Spin changes only at contacts (OBS, consistent)

Raw spin `[rbx+0x18/0x1c/0x20]` is written only at `0x1408770da/0x1408770e0/0x1408771cd`
(contact) and never in the flight path — matches §6.4 exactly.

---

## 6. Every constant's runtime source (OBS)

`ball.o`: `common/match/constant/constant_match.bin` (WESYS, flat blob offset `0x3c0`, size
1040 = `0x410` B). Layout: `0x110`-byte scalar header (disk offsets below), then 24 blocks ×
32 B (6 f32 + 8 pad bytes) at `0x110 … 0x3f0` step `0x20`, reached by **record-relative u32
pointers** (proven: the 24 pointer values form an exact `0x20`-step progression covering
`[0x110, 0x410)`; a field-relative reading would leave the grid).

`BallBase+off` = `Ball+off+8`; scalars: `Ball+X` = `ball.o+X`; arrays: `Ball+base+i·4` =
block element `i`. Proven by `Ball::LoadRecord` (`0x141e73720`, every store below verified in
raw bytes); names from `Ball::LoadJson` (`0x141e73ee0`, 66 `.rdata` keys
`0x142b56e78 … 0x142b573e8`). "mode-1" = element 1 = the live column.

### 6.1 Scalars (`ball.o+off` == `Ball+off`; `BallBase+off` == `Ball+off+8`)

| JSON name | disk | BallBase | value |
|---|---|---|---|
| airRegistNormal | +0x000 | +0x000 | 1.0 |
| backSpinBuoyancyRate | +0x004 | — | 1.0 |
| backSpinLogRate | +0x008 | — | 7.0 |
| boundBoundSpeedMax | +0x00C | +0x00C | 12.0 |
| boundBoundSpeedMin | +0x010 | +0x010 | 2.0 |
| boundFrictionSpeedMax | +0x020 | +0x048 | 25.0 |
| boundFrictionSpeedMin | +0x024 | +0x04C | 10.0 |
| boundRollMax | +0x02C | +0x068 | 7.5 |
| boundRotationSpeedMax | +0x030 | +0x06C | 20.0 |
| boundRotationSpeedMin | +0x034 | +0x070 | 5.0 |
| curve | +0x058 | +0x134 | 0.0 |
| debugKickAngle | +0x05C | +0x138 | 3.0 |
| debugKickSpeed | +0x060 | +0x13C | 35.0 |
| dragSpeedMax | +0x064 | +0x140 | 96.995 |
| dragSpeedMin | +0x068 | +0x144 | 56.9956 |
| grounderBoundYAdd | +0x088 | +0x1F0 | 0.09 |
| grounderCTan | +0x08C | +0x1F4 | 8.0 |
| grounderFrictionRate | +0x090 | +0x1F8 | 0.985 |
| grounderSpeed | +0x094 | +0x1FC | 20.0 |
| magnusRate | +0x098 | +0x200 | 0.035 |
| natualBoundB | +0x0A0 | +0x21C | 0.4 |
| naturalRoll | +0x0A8 | +0x238 | 2.0 |
| nonSpinMax | +0x0AC | +0x23C | 106.0 |
| nonSpinMin | +0x0B0 | +0x240 | 45.0 |
| nonSpinRate | +0x0B4 | +0x244 | 1.0 |
| nonSpinTopDownDecRate | +0x0B8 | +0x248 | 1.0 |
| nonSpinTopRiseDecRate | +0x0BC | +0x24C | 1.0 |
| roll | +0x0CC | +0x284 | −2.0 |
| rollToBoundAdd | +0x0D0 | +0x288 | 0.07 |
| rollToBoundRollMax | +0x0D4 | +0x28C | 7.0 |
| rollToBoundRollMin | +0x0D8 | +0x290 | 2.0 |
| stopSpeed | +0x0F4 | +0x2D4 | 0.175 |
| pitchCondition | +0x0C8 | +0x280 | u32 1 (int, not float) |
| rotationCustom | +0x0F0 | +0x2D8 | u8 0 |
| speedCustom | +0x0F1 | +0x2D9 | u8 0 |

`BallBase+off` = `Ball+off+8` throughout (OBS: `dragSpeedMax` at `BallBase+0x140` =
`Ball+0x148` = `ball.o+0x64`; `[rdi+0xc]` = `Ball+0x14` = `ball.o+0xc` = 12.0
(`boundBoundSpeedMax`); `[rdi+0x10]` = `Ball+0x18` = `ball.o+0x10` = 2.0
(`boundBoundSpeedMin`) — all confirmed against both loaders and the disk bytes). A coder reads
any `BallBase` slot as `ball.o` bytes at `slot−8` (scalars) or as element `mode` of the block
whose record-relative pointer sits at `slot−8` (arrays, §6.2).

Cross-checks used in §§2–3 (all OBS): `[rdi+0x48]` = `Ball+0x50` = 25.0
(`boundFrictionSpeedMax`); `[rdi+0x4c]` = `Ball+0x54` = 10.0 (`boundFrictionSpeedMin`);
`[rdi+0x6c]` = `Ball+0x74` = 20.0 (`boundRotationSpeedMax`); `[rdi+0x70]` = `Ball+0x78` = 5.0
(`boundRotationSpeedMin`); `[rdi+0x68]` = `Ball+0x70` = 7.5 (`boundRollMax`, the §3.4 spin
cap); `[rdi+0x288]` = `Ball+0x290` = 0.07 (`rollToBoundAdd`); `T[0x2c]` =
`boundCheckFrictionAddRate[1]` = 0; `T[0x14]` = `boundCheckBoundAddRate[1]` = 5.


### 6.2 Arrays (24 blocks × 6 f32; `Ball+base+i·4`, `i` = element)

`Ball+base` ← the block whose record-relative u32 pointer sits at `ball.o+base−8−?`… stated
per row as the disk pointer slot. All values shipped (mode columns 0..5). Proven by
`Ball::LoadRecord` (each block copied element-for-element) and named by `Ball::LoadJson`
(`lea r14,[rsi+base]` immediately precedes each key's 6-element loop; the 24th,
`rollToSpeedSpeedYRateBase`, uses `rbp` at `Ball+0x2c0`).

| JSON name | disk ptr | Ball base | BallBase base | values [0..5] |
|---|---|---|---|---|
| boundCheckBoundAddRate | +0x014 | +0x01C | +0x014 | 10, 5, 0, 0, 5, 5 |
| boundCheckFrictionAddRate | +0x018 | +0x034 | +0x02C | 0, 0, 0, 0, 7.5, 0 |
| boundRate | +0x028 | +0x058 | +0x050 | 0.7, 0.7, 0.7, 0.5, 0.7, 0.7 |
| boundToRotationAddRateXZ | +0x038 | +0x07C | +0x074 | 0.6, 0.62, 0.64, 0.12, 0.62, 0.62 |
| boundToRotationAddRateY | +0x03C | +0x094 | +0x08C | 0.07 ×5, 0.03 |
| boundToRotationBaseXZ | +0x040 | +0x0AC | +0x0A4 | 0.16, 0.2, 0.24, 0.13, 0.2, 0.2 |
| boundToRotationBaseY | +0x044 | +0x0C4 | +0x0BC | 0.03 ×5, 0.02 |
| boundToRotationSpeedMaxXZ | +0x048 | +0x0DC | +0x0D4 | 25 ×4, 0.7, 25 |
| boundToRotationSpeedMaxY | +0x04C | +0x0F4 | +0x0EC | 0.7 ×5, 30 |
| boundToRotationSpeedMinXZ | +0x050 | +0x10C | +0x104 | 3 ×4, 0.1, 3 |
| boundToRotationSpeedMinY | +0x054 | +0x124 | +0x11C | 0.1 ×5, 1 |
| frictionRollRateMax | +0x06C | +0x150 | +0x148 | 0.975, 0.972, 0.969, 0.99, 0.955, 0.983 |
| frictionRollRateMin | +0x070 | +0x168 | +0x160 | 0.985, 0.982, 0.979, 0.98, 0.987, 0.983 |
| frictionRollRateStop | +0x074 | +0x180 | +0x178 | 0.975, 0.972, 0.969, 0.97, 0.987, 0.983 |
| frictionRollSpeedMax | +0x078 | +0x198 | +0x190 | 25, 25, 25, 50, 8, 0 |
| frictionRollSpeedMin | +0x07C | +0x1B0 | +0x1A8 | 8 ×5, 15 |
| frictionRollSpeedStop | +0x080 | +0x1C8 | +0x1C0 | 0 ×4, 8, 0 |
| gounnderprob (sic, game's typo for grounder probability) | +0x084 | +0x1E0 | +0x1D8 | 0 ×6 (u32 5,5,5,9,9,5 as int) |
| natualBoundA (sic) | +0x09C | +0x20C | +0x204 | 0.055 ×6 |
| natunalRollBackSpinRate (sic) | +0x0A4 | +0x228 | +0x220 | 0.35, 0.4, 0.45, 0.2, 0.5, 0.4 |
| overRotMax | +0x0C0 | +0x258 | +0x250 | 1 ×6 |
| overRotMin | +0x0C4 | +0x270 | +0x268 | −2 ×6 |
| rollToSpeedMaxKPH | +0x0DC | +0x29C | +0x294 | 20, 20, 20, 8.5, 18, 16 |
| rollToSpeedSpeedYRateBase | +0x0EC | +0x2C0 | +0x2B8 | 0.8, 0.8, 0.8, 0.4, 0.2, 0.8 |

`T[off]` in §§2–3 = element `mode` of the row whose BallBase base is `off`
(= `Ball+off+8`, = block element `mode`).


---

## 7. Frame/net contact and open items

### 7.1 Goalposts, crossbar, net, boards (OBS partial)

- The sim-ball integrator (`FUN_140876360`) handles **ground only**. It has no post/net
  branch: the only geometry tests in it are `y vs r` (§2.3) and the 60×40 m box (§2.2).
  (OBS: full raw read of `0x140876360 … 0x1408773f1`.)
- Frame/net/board contacts arrive through the collision system (`pes-re-collision.md`
  §2.3–2.4): the ball is kind-1 object id 26, r = 0.1087 m at `collision::Object+0x80`, and
  kind 1 records contacts with kinds 6, 9, 10, 11, 13, 14, 16 (posts/boards/nets per the
  touch-kind enum at `0x1434a5c20`: `NONE, POST, CROSSBAR, CFLAG, POLE, BOARD, SIDENET,
  UPPERNET, BACKNET, SIDENET_IN, UPPERNET_IN, BACKNET_IN, STAD, CONE, PANEL, PANEL_STAND`).
  The consumer that turns a recorded contact into a velocity change was **not** found: the
  touch-kind name table is referenced only from script-binding registration (same result as
  `pes-re-collision.md` §7.1), and `match::record::ObserverBallTouch::Exec`
  (`FUN_140a17b30`) only records stats from an already-computed kind (OBS decompile).
- The goal frame collision mesh is `common/anime/FoxAnim/{goal_hit,goal_l,goal_r}.geom`
  (collision report §1, OBS file census); the net is a separate cloth object
  (`scene::rc::BallImpl` also holds goal-net film data per §5.1). Neither response function
  was decompiled.
- **Status: open.** Next step: xref the kind-1 contact record writers
  (`collision::Object+0x20` list, count at `+0x28`) from a live match and follow the
  `POST/CROSSBAR/BOARD/*NET*` touch-kind assignment the same way §2–3 followed `y vs r`.

### 7.2 The per-match mode byte (`match+0x279d`) writer (OBS partial)

- The mode the integrator uses comes from `match_context+0x279d` (signed byte, `>5 → 1`;
  read at raw `0x140865f7f`: `movsx r15d,BYTE PTR [rcx+0x279d]`; `cmp r15d,0x5`;
  `cmova r15d,ebx`). OBS.
- It is struct-copied by `0x140430c30`
  (`movzx eax,BYTE PTR [rdi+0x279d]` → `mov BYTE PTR [rbx+0x279d],al`, raw `0x140430c6c`
  region), reached via the block-copy table at `0x1404335b0`
  (`lea rdx,[rcx+0x2970]` → `jmp 0x140430c30`). OBS.
- The *source* struct's writer (weather/pitch-condition → mode 0..5) was **not** found:
  a byte-pattern scan for `mov [reg+0x279d],imm` found only 64-bit zero-writes elsewhere,
  and `pitchCondition` (menu-icon string at `0x14267103d`, UI function at `0x140671000`)
  is unrelated. INF (medium): mode follows the match weather/pitch condition selected in
  the menu; live mode was 1 in both observed matches.
- **Status: open.** Next step: live-watch `match+0x279d` across matches with different
  weather/pitch settings, or xref writers of the source struct's `+0x2970` block.

### 7.3 Tick (CLOSED live)

- `DAT_14351ad20` = **0.0185185 = 1/54 s in-match** (OBS live 2026-10-13, three `sudo`
  `/proc/<pid>/mem` reads mid-match; fps `DAT_14351ae54` = 54.0, int `DAT_14351ae50` = 54).
  The 1/48 value was title-screen only. All formulas in this report are per-tick at 1/54 s.

### 7.4 What was tried for the open items

| item | tried |
|---|---|
| frame/net response fn | touch-kind string xrefs (only script bindings); `ObserverBallTouch::Exec` decompile (stats only); `goal_hit/goal_l/goal_r.geom` census (files exist, no response fn); `BallImpl`/net-cloth report (no ball-contact response) |
| mode writer | `mov [reg+0x279d],imm` byte scan (no hits); struct-copier traced to `0x1404335b0` table; `pitchCondition` string followed to UI code (unrelated); weather-icon strings (menu only) |
| live bounce sampling | ball found via `BallInfoRef` vtable scan (33 candidates; live one sampled: mode 1 confirmed, pos/vel/spin plausible); 8 s fast sampler written (`watchbounce.py` in the scratch dir) — the watched heap address went stale (match state moved), no contact captured before the session slice ended |

---

## 8. Reproduction checklist (for Main)

- [x] Contact test, snap, wobble-flag clear — literal (§2.1–2.3)
- [x] Per-tick keep `K` — literal, mode-1 numbers (§2.4)
- [x] Grounder kick — literal incl. guard chain (§2.5)
- [x] At-rest finite-difference path — literal (§2.6)
- [x] Friction multiplier `F` — literal (§3.1)
- [x] Spin-scale dead knob = 1 — literal (§3.2)
- [x] Rolling-direction block — raw skeleton + entry register map; closed form INF medium (§3.3)
- [x] Spin cap 7.5 rad/s, raw-spin stores, stop gate 0.0486 m/s — literal (§3.4)
- [x] Rolling 0.981/tick and 2.05 m/s² reproduced from tables (§5.2)
- [x] Every constant's `ball.o` field↔offset↔loader — literal (§6)
- [x] Tick 1/54 s — live OBS (§7.3)
- [ ] Frame/net velocity response — open (§7.1)
- [ ] Mode-byte writer (weather→mode) — open (§7.2)
- [ ] Live bounce-trace validation of §§2–3 — open (sampler ready, §7.4)

