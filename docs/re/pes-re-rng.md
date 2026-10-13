# PES 2021 RNG model — evidence report (READ-ONLY RE + authorised seed-pinning procedure)

Tags: **OBS** = observed (raw bytes / on-disk exe bytes / live `/proc/<pid>/mem` read at the stated
address). **INF** = inference, with a confidence level. No player or team names. No PES-derived bytes
are written anywhere except this report's scratch (`~/ghidra-projects/scratch/RngRE/`).

Sources used this session:
- on-disk exe `$PES21_ROOT/PES2021.exe`, VA→file-offset through the PE section table
  (PE header at file offset `0xd8`; image base `0x140000000`). Build matches the live process
  byte-for-byte at every VA quoted below (spot-checked: `0x140a92470`, `0x140a92590`,
  `0x140a928a0`, `0x140441fc0`, `0x140504100` — identical in `.exe` and memory image).
- `math::Random` / `math::GameRandom` RTTI via `~/ghidra-projects/scratch/FoxRegistryRE/rtti.py`:
  `math::Random` COL `0x142c9bed0`, vtable `0x1425def28` = `{0x1406e28c0}`;
  `math::GameRandom` COL `0x142caf7d8`, vtable `0x1426550d0` = `{0x140a92440}`.
- live process (pid found by `pgrep -x PES2021.exe`), read-only `/proc/<pid>/mem`.
- prior kick-launch report (`docs/re/pes-re-kick-launch.md`) for the per-player LCG and the
  exponential quantile table.

---

## 1. Generators (OBS, every constant confirmed on disk)

### 1.1 `math::Random` / `math::GameRandom` object layout

```
struct math::Random {            // GameRandom derives from Random, same layout
  void*  vtable;      // +0x00  Random: 0x1425def28   GameRandom: 0x1426550d0
  int32  slot[4];     // +0x08 slot0, +0x0c slot1, +0x10 slot2, +0x14 slot3
  uint8  flag_18;     // +0x18
  uint32 field_1c;    // +0x1c
  uint32 slotCount;   // +0x20  (edx at the ctor)
};                              // size 0x28
```

Constructor `0x140a923f0` (Random) / `0x140a923b0` (GameRandom, takes a slot count in `edx`):

```
0x140a923f0: lea rax,[rip+0x1b4cb31]   ; rax = 0x1425def28  (Random vtable)
0x140a923f7: mov [rcx], rax
0x140a923fd: mov dword [rcx+8],  0x2f3d0    ; slot0 = 193488
0x140a92404: mov dword [rcx+0xc],0x2f3d1    ; slot1 = 193489
0x140a9240b: mov dword [rcx+0x10],0x2f3d2   ; slot2 = 193490
0x140a92412: mov dword [rcx+0x14],0x2f3d3   ; slot3 = 193491
0x140a92419: mov qword [rcx+0x1c], 0
0x140a92420: mov byte  [rcx+0x18], 0
```

So the **default seeds are `0x2f3d0, 0x2f3d1, 0x2f3d2, 0x2f3d3`** = 193488…193491 (OBS, immediates
at `0x140a923fd`, `0x140a92404`, `0x140a9240b`, `0x140a92412`). `0x140a923b0` is the same thing but
writes `0x1426550d0` (the GameRandom vtable) and stores `edx` at `+0x20`.

RTTI inheritance: `GameRandom` bases `[GameRandom, Random]` → single non-virtual base, so a
`GameRandom*` is also a `Random*` (OBS, RTTI COL `0x142caf7d8`).

### 1.2 The 31-bit LCG step (slots 0,1,2 — index `edx` ∈ {0,1,2})

`0x140a92470` — **`Range(rng, idx)`**, the "step and map to range" primitive:

```
0x140a92473: cmp edx,3 ; jne 0x140a92488        ; idx==3 takes the 15-bit path (§1.3)
0x140a92488: movsxd rdx,edx
0x140a92491: imul eax,[rcx+rdx*4+8],0x2d22b2e3  ; eax = state * 0x2D22B2E3
0x140a92499: mov ecx,0x4513
0x140a9249e: sub ecx,eax                        ; ecx = 0x4513 - eax   (32-bit wrap)
0x140a924a7: btr ecx,0x1f                       ; clear bit 31
0x140a924ab: mov [r8+rdx*4+8],ecx               ; state = new value
0x140a924a0: lea rax,[rip+0x1bc2409]            ; 0x1426548b0 = modulus table
0x140a924b0: mov r8d,[rax+rdx*4]                ; M = tab[idx] = {100,5000,32768,32768}
0x140a924b6: cvtsi2ss xmm0,r8                   ; (float)M
0x140a924bb: cvtsi2ss xmm1,rax                  ; (float)newstate  (rax = ecx, zero-extended)
0x140a924c0: lea eax,[r8-1]                     ; eax = M-1
0x140a924c4: mulss xmm1,[rip+0x1bc2c0c]         ; 0x1426550d8 = 4.656612873077393e-10 = 2^-31
0x140a924cc: mulss xmm1,xmm0                    ; xmm1 = state/2^31 * M
0x140a924d0: cvttss2si rcx,xmm1                 ; truncate toward zero
0x140a924d5: cmp r8d,ecx ; cmova eax,ecx        ; if (unsigned)M <= result -> result = M-1
0x140a924db: ret                                ; returns eax
```

Literal formula, bit-exact in C++:

```cpp
static const uint32_t kLcgMul = 0x2D22B2E3u;   // immediate at 0x140a92491
static const uint32_t kLcgSub = 0x4513u;       // immediate at 0x140a92499
static const int32_t  kMod[4] = {100, 5000, 32768, 32768};  // 0x1426548b0 (OBS on disk)
static const float    kScale = 4.656612873077393e-10f;      // 0x1426550d8 = 2^-31 (OBS)

inline uint32_t LcgStep(uint32_t& s) {
    s = (kLcgSub - s * kLcgMul) & 0x7FFFFFFFu;   // btr 31 == clear bit 31
    return s;
}
inline int RangeStep(uint32_t& s, int idx) {
    uint32_t ns = LcgStep(s);
    int M = kMod[idx];
    uint32_t r = (uint32_t)(float)((float)ns * kScale * (float)M);  // cvtsi2ss+mulss+cvttss2si
    if ((uint32_t)M <= r) r = M - 1;             // unsigned compare + cmova
    return (int)r;
}
```

Verified numerically in Python against live states (§4).

Note the exact float path: both operands are converted with **`cvtsi2ss` from a 64-bit register**
(`rax` = zero-extended `ecx`), the multiply order is
`(float)state * 2^-31 * (float)M` — that order matters for the last ULP; reproduce it literally.

`0x140a924e0` — the same LCG step returning the **raw 31-bit state** as a float in `xmm0`
(`cvtsi2ss`), used where the caller wants `state/2^31`-style values (49 call sites).

`0x140a92530` — **`Range0(rng, count)`**, slot 1 only:

```
0x140a92533: test edx,edx ; jle -> return 0
0x140a9253a: imul eax,[rcx+0xc],0x2d22b2e3 ; sub ecx,0x4513-eax ; btr ecx,31
0x140a92558: mov [r8+0xc],ecx
0x140a92563: mulss xmm1, 2^-31 ; mulss xmm1,(float)count ; cvttss2si eax
0x140a92573: test eax,eax ; cmovns ecx,eax     ; if result<0 -> 0
0x140a92578: lea eax,[rdx-1] ; cmp edx,ecx ; cmovg eax,ecx   ; min(result, count-1)
```
i.e. `min(count-1, max(0, (int)(state * 2^-31 * count)))` (66 call sites).

### 1.3 Slot 3 — the weak 15-bit LCG (OBS)

`0x140a92470`, the `idx == 3` branch:

```
0x140a92478: imul eax,[rcx+0x14],0x15      ; eax = state * 21   (0x15 = 21 decimal)
0x140a9247c: add eax,7                     ; eax = eax + 7
0x140a9247f: and eax,0x7fff                ; & 0x7FFF
0x140a92484: mov [rcx+0x14],eax
0x140a92487: ret                           ; NO range mapping - returns nothing
```

So slot 3 is **`state = (21*state + 7) & 0x7FFF`** (multiplier `21` = `0x15`, increment `7`, mask
`0x7FFF`) — OBS, immediate `0x15` at `0x140a92478`, `7` at `0x140a9247c`, `and 0x7fff` at
`0x140a9247f`. It is a bare advance: callers use it as the per-player RNG (§3) and through
`0x140a92880` (`Advance(rng, idx)`, 6 call sites).

### 1.4 The normal path `0x140a92590` — `Normal(rng, amp, off)` (OBS, full literal)

```
0x140a925ae: xmm6  = 1.0            (0x14256ae1c)
0x140a925d0: xmm10 = xmm2 (arg 'off')
0x140a925d9: xmm10 = xmm10 + 1.0
0x140a925de: xmm11 = -1.0           (0x142588218)
0x140a925ec: xmm12 = 50.0           (0x14259be34)
0x140a925fb: xmm13 = 0.5            (0x142565600)
0x140a9260a: xmm14 = 90.0           (0x14259be5c)
0x140a92619: xmm15 = xmm1 (arg 'amp')
0x140a9261d: imul eax,[rdi+0x10],0x2d22b2e3     ; slot2 step
0x140a92624: mov r8d,0x4513 ; sub r8d,eax ; btr r8d,31     ; s1'
0x140a92646: imul eax,r8d,0x2d22b2e3             ; slot2 step again
0x140a9264d: sub ecx(=0x4513),eax ; btr ecx,31 ; mov [rdi+0x10],ecx   ; s2' stored
0x140a9264f: mov eax,0x10624dd3
0x140a92658: mul ecx                              ; edx:eax = s2' * 0x10624dd3
0x140a9265d: shr edx,6                            ; edx = s2' / 100
0x140a92660: imul eax,edx,0x3e8 ; sub ecx,eax     ; u2 = s2' - (s2'/100)*100   ∈ [0,99]
0x140a92668: lea eax,[rcx+1]                      ; u2+1
0x140a9266b: cvtsi2ss xmm8,rax
0x140a92678: divss xmm8,xmm1(=1000.0)             ; xmm8 = (u2+1)/1000
0x140a92686: mulss xmm8,[rip-> 0x14259858c]=360.0 ; xmm8 = 360*(u2+1)/1000   (an angle)
0x140a92670: mov eax,0x10624dd3 ; mul r8d ; shr edx,6
0x140a92680: imul eax,edx,0x3e8 ; sub r8d,eax     ; u1 = s1' - (s1'/100)*100    ∈ [0,99]
0x140a92692: lea eax,[r8+1]                       ; u1+1
0x140a92696: cvtsi2ss xmm3,rax ; divss xmm3,1000.0
0x140a9269f: mulss xmm3,[rip-> 0x1426550e8]=512.0 ; xmm3 = 512*(u1+1)/1000
0x140a926a7: cvttss2si edx,xmm3                   ; idx = (int)...
0x140a926ab: cmp edx,0x200 ; jne ... ; else xmm0 = 0   ; if idx==512 -> sample 0
0x140a926c3: movss xmm2,[rsi+rax*4]               ; tab[idx]      (rsi = 0x1426548c0)
0x140a926d2: movss xmm0,[rsi+rcx*4]               ; tab[idx+1]
0x140a926ce: subss xmm3,(float)idx
0x140a926db: mulss xmm0,xmm3 ; addss xmm0,xmm2    ; linear interpolation
0x140a926e3: mulss xmm0,[rip-> 0x1426550dc]=0.000244140625   ; * 2^-12
0x140a926eb: mulss xmm0,[rip-> 0x14259a768]=-2.0              ; * -2
0x140a926f3: call 0x1415a1780                    ; tanhf  (INF: the standard tanh import)
0x140a926f8: xmm7 = tanh(sample)
0x140a926fd: mulss xmm7,xmm15 (amp)
0x140a92702: divss xmm7,[rip-> 0x1426550e4]=3.5322299003601074
; ---- rejection loop (ebx = 0,1) ----
0x140a92710: xmm0 = xmm8 (the 360*(u2+1)/1000 angle)
0x140a9271c: addss xmm0,xmm14 (=90.0)            ; only when ebx != 1
0x140a92721: call 0x140a92c80                    ; deg->rad via the 1024-entry sin table
0x140a92726: mulss xmm0,xmm7
0x140a9272a: xmm1 = xmm10 (off+1.0)
0x140a9272e: addss xmm1,xmm0
0x140a92732: mulss xmm1,xmm12 (=50.0)
0x140a92737: comiss xmm9(=0),xmm1 ; ja -> xmm0 = -1.0 else xmm0 = 1.0
0x140a92746: mulss xmm0,xmm13 (=0.5)
0x140a9274b: addss xmm0,xmm1
0x140a9274f: cvttss2si eax,xmm0
0x140a92753: cmp eax,0x63 ; jbe 0x140a92764      ; if result<=99 -> accept, return eax
0x140a92758: inc ebx ; cmp ebx,2 ; jl 0x140a92710 ; else retry (2 tries max)
0x140a9275f: jmp 0x140a9261d                       ; both rejected -> redraw the pair
; on the third pass it falls through to 0x140a92760:
0x140a92760: mov ecx,0x44fffffe                    ; returns 0x44fffffe as eax (i.e. ~28671)
```

Bit-exact C++ (float32 throughout):

```cpp
static const float kTabScale = 0.000244140625f;   // 0x1426550dc, 2^-12
static const float kNegTwo   = -2.0f;             // 0x14259a768
static const float kDiv      = 3.5322299003601074f; // 0x1426550e4
static const float kQTab[513];                    // 0x1426548c0 (OBS on disk, see below)

int Normal(Random& r, float amp, float off) {
  for (int attempt = 0; attempt < 2; ++attempt) {
    uint32_t s1 = LcgSlot2(r);            // step slot2, keep 31-bit
    uint32_t s2 = LcgSlot2(r);            // step slot2 again (stores s2)
    int u1 = (int)(s1 - (s1/100)*100);    // magic 0x10624dd3, shr 6 == /100
    int u2 = (int)(s2 - (s2/100)*100);
    float f2 = (float)(u2+1) / 1000.0f * 360.0f;
    float t  = (float)(u1+1) / 1000.0f * 512.0f;
    int   idx= (int)t;
    float v  = (idx == 512) ? 0.0f
              : kQTab[idx] + (kQTab[idx+1] - kQTab[idx]) * (t - (float)idx);
    float sample = tanhf(v * kTabScale * kNegTwo);
    float g = sample * amp / kDiv;
    for (int k = 0; k < 2; ++k) {
      float a = DegToSinTable(f2 + (k != 1 ? 90.0f : 0.0f));
      float x = (off + 1.0f + a * g) * 50.0f;
      float s = (0.0f > x) ? -1.0f : 1.0f;
      int   res = (int)(s * 0.5f + x);
      if (res <= 99) return res;
    }
  }
  return (int)0x44fffffe;   // 0x140a92760
}
```

Caveat (INF med): `0x1415a1780` is a protected-region import; the kick report identified
`0x1415a1780` as `sqrtf` in a neighbouring formula and `0x1415a17b6` as `expf`. **`tanhf` here is
inferred from the surrounding math, not confirmed** — see Open list. Everything else in this
function is OBS.

The deg→rad helper `0x140a92c80` uses the 512-entry sin table at `0x142655950`
(`tab[k] = sin(k·pi/1024)`, OBS; `tab[64]=0.19509`, `tab[256]=0.70711`) plus `180.0`
(`0x142598580`) and `90.0` (`0x14259be5c`) for quadrant reduction.

### 1.5 The exponential quantile table (OBS, re-verified on disk)

`0x1426548c0`, **513 f32**, in `.rdata`. Scaled values `raw * 2^-12` reproduce `-ln(512/i)`:

| i | raw (disk) | raw·2^-12 | −ln(512/i) |
|---|---|---|---|
| 1 | −25552.0 | −6.238281 | −6.238324 |
| 2 | −22713.0 | −5.545166 | −5.545177 |
| 8 | −17035.0 | −4.158936 | −4.158883 |
| 32 | −11357.0 | −2.772705 | −2.772589 |
| 128 | −5678.0 | −1.386230 | −1.386294 |
| 256 | −2839.0 | −0.693115 | −0.693147 |
| 448 | −547.0 | −0.133545 | −0.133531 |
| 511 | −8.0 | −0.001953 | −0.001955 |
| 512 | 0.0 | 0.0 | 0.0 |

(The signed samples come out as `2·ln(u/512) ∈ (-inf,0]` after the `×2^-12 × -2` in §1.4.)
Modulus table `0x1426548b0` = `{100, 5000, 32768, 32768, <followed by the 513 floats>}` — OBS on disk.

### 1.6 The ability-scaled normal `0x140a927b0` (OBS)

Called once, from `0x14092ed30` (the player updater; its callers include `match::Player`):

```
0x140a927e7: xmm0 = 40.0                (0x14259be2c)
0x140a927ef: eax  = (u32)dl              ; dl = the ability byte argument
0x140a927f2: cvtdq2ps xmm2,eax
0x140a927f9: comiss xmm0,xmm2 ; jb 0x140a92803   ; if 40 > A -> xmm2 = 0
0x140a92803: comiss xmm2,99.0 (0x1425c48cc) ; jb 0x140a92816
0x140a9280c:   xmm2 = 1.0                        ; A >= 99 -> t = 1
0x140a92816:   xmm2 = (A - 40.0)/59.0           ; 59.0 = 0x1425cb2fc
0x140a92822: xmm2 = t * 100.0                   ; 0x14253ee30
0x140a9282d: xmm1 = 5.0                         ; 0x14259222c
0x140a92835: xmm2 = t100 * 4.5                  ; 4.5 = 0x1425a1ff4
0x140a9283d: xmm2 = that / 100.0
0x140a92845: xmm1 = 5.0 - xmm2                  ; width = 5 - 4.5*(A-40)/59
0x140a9284c: call 0x140a92590                   ; Normal(rng, width, xmm3)
0x140a92851: cvtdq2ps xmm0,eax
0x140a92858: xmm0 = result - 50.0               ; 0x14259be34
0x140a92860: xmm0 = that / 50.0                 ; 0x14259be34
```
Returns a value in `[-1,1]`: `(Normal(rng, 5-4.5*(A-40)/59, off) - 50)/50`.
`0x140a9286d` (the other branch) returns `0`.

### 1.7 Per-player 15-bit LCG at `player+0x32d0` (OBS)

`0x1406e6bf0` — `PlayerRandomMod(player, m)`:

```
0x1406ebf8: rcx = player + 0x32d0
0x1406ebff: edx = 2                            ; slot index 2 of the *inline* Random at +0x32d0
0x1406ebf04: call 0x140a92470                  ; RangeStep on that state
0x1406ebf0e: mov ecx,1 ; if (m==0) ecx = m
0x1406ebf31: idiv ecx ; return edx             ; result % m
```

`0x1406ebf30` — `PlayerRoll100(player, thr)`: same draw, then
`u = state - (state/100)*100` (magic `0x51eb851f`, `shr 5`) and `return u < thr`.

`0x1406eaa10` — three consecutive draws (rolls `mod 100`, compares with `0x32` = 50 to decide a
second and third draw). This is the **success-roll family** the kick report documented.

INF (high): these per-player states are a `math::Random` laid inline at `player+0x32d0` with
`slotCount = 2` (the `edx=2` index argument), i.e. a *separate* generator from the match
generators, seeded per player.

---

## 2. Seeding (OBS for every step that is on-disk; the clock value itself is protected)

### 2.1 The kick-off seed commit site — **exact call site**

```
0x1404422ef: mov  r8d, ebx                      ; r8d = t          (the seed)
0x1404422f2: xor  edx, edx                      ; slot 0
0x1404422f4: lea  rcx, [rsi + 0x5708]           ; &matchCtx->mainRandom
0x1404422fb: call 0x140a928a0                   ; SetSlotState(rng, 0, t)
0x140442300: mov  r8d, ebp                      ; t+1
0x140442303: mov  edx, 1                        ; slot 1
0x14044230f: call 0x140a928a0
0x140442314: mov  r8d, edi                      ; t+2
0x140442317: mov  edx, 2                        ; slot 2
0x14044231c: call 0x140a928a0
```

`SetSlotState(rng, idx, value)` = `0x140a928a0`:

```
0x140a928a0: movsxd rax,edx ; mov [rcx+rax*4+8],r8d ; ret
```
(OBS: `0x1404422fb`, `0x14044230f`, `0x140442323` — the three call sites; state write at
`0x140a928a3`.)

**Enclosing function `0x140441fc0`** = the **match-context constructor**, size `0x39a` bytes, whose
single caller is `0x140441490` (the `new` + ctor helper) ← `0x140422009` in `0x140421fd0`
(the match-context factory: `0x1403fb410(0x17fd8)` then `0x140441490`). Its owner class, resolved
through the vtable of its caller, is **`match::MatchControl`** (`0x142596dc8` slot 1).

The three seed values are computed immediately above the call sites:

```
0x14044229a: call 0x141b34aa0                   ; -> 0x141b34a30 : the clock reader (protected)
0x14044229f: mov  rcx,rax ; call 0x141b34ad0    ; -> normalise (double) / (double)1000.0, int
0x1404422a7: mov  rbx,rax                        ; t64
0x1404422aa: movabs rax,0x346dc5d63886594b ; mul rbx ; shr rdx,0xb ; imul rax,rdx,0x2710
0x1404422c2: sub  rbx,rax                        ; rbx = t64 % 10000      (magic division)
0x1404422c5: lea  ebp,[rbx+1]
0x1404422c8: mov  eax,0xd1b71759 ; mul ebp ; shr edx,0xd ; imul ecx,edx,0x2710 ; sub ebp,ecx
0x1404422c2 ->                                ; slot1 seed = (t64+1) % 10000
0x1404422da: lea  edi,[rbx+2] ... sub edi,eax   ; slot2 seed = (t64+2) % 10000
```

**So: `slot0 = t % 10000`, `slot1 = (t+1) % 10000`, `slot2 = (t+2) % 10000`, slot3 untouched
(default `0x2f3d3`).** All other state in the `math::Random` is left at its ctor value.

The online alternative (disabled in an offline match):

```
0x140442244: mov rcx,[0x1434a2d40] ; mov rax,[rcx] ; call [rax+0x88]    ; registry query
0x140442254: test al,al ; je 0x14044229a                              ; false -> fallback
0x140442258: ... call [rax+0x1b8] three times with edx = 0,1,2         ; online seed getter
```
`0x1434a2d40` is the singleton `match::registry::online::Interface`; its vtable is
`0x142644638` (RTTI COL `0x142caf098`, `.?AVInterface@online@registry@match@@`), slot `+0x88`
resolves to `0x141863350` = `xor al,al; ret` and slot `+0x1b8` to `0x140421f40` = `xor eax,eax; ret`
in this build (OBS, read from the live image). **Conclusion: in an offline match the seed always
comes from the clock fallback** — the online path cannot fire.

`0x141b34a30` (the clock reader) is:

```
0x141b34a43: cmp qword [rip+0x1cbb6a5],0        ; 0x1437f00f0
0x141b34a4b: je 0x141b34a6f
0x141b34a4d: lea rcx,[rsp+0x20] ; call [rip+0x9fa9d8]   ; 0x14252f430 (protected thunk)
0x141b34a58: mov rax,[rsp+0x20] ; ret
0x141b34a6f: mov edx,0xc ; lea rcx,[rsp+0x28] ; call [rip+0x9fb411]  ; 0x14252fe90
0x141b34a7f: mov eax,[rsp+0x2c] ; ret
```
INF (high, from the shape + the `%10000` normalisation + PES's import list): this is a
`QueryPerformanceCounter`/`GetTickCount`-style millisecond clock read (`winmm!timeGetTime` /
`kernel32!QueryPerformanceCounter` are both imported by `PES2021.exe`, resolved at runtime —
the exact API could not be pinned because the thunks live in the 398 MB `.impdata` protection
region, which is outside the 64 MB memory dump). **Not `QueryPerformanceCounter` directly, and not
`time()`:** the value is divided by 1000.0 inside `0x141b34ad0` and then reduced mod 10000.

### 2.2 The sub-generators, seeded from the same value

Inside the same constructor, right after the main RNG is seeded:

```
0x140442328: mov  r9d,edi ; mov r8d,ebp ; mov edx,ebx ; lea rcx,[rsi+0x1e8]  ; call 0x14047ab90
0x14044233c:             ; rcx=[rsi+0x6d8]                                 ; call 0x140481050
0x140442350:             ; rcx=[rsi+0x770]                                 ; call 0x140489440
0x140442364:             ; rcx=[rsi+0x7d0]                                 ; call 0x14048b330
```

Each of those four helpers writes the **same** `(t, t+1, t+2)` into its own embedded
`math::Random` (OBS disassembly, e.g. `0x14047ab90` = `lea rdi,[rcx+0x108]; setseed(0/1/2)`,
`0x140481050` = `lea rdi,[rcx+0x18]`, `0x140489440` = `lea rdi,[rcx+0x10]`,
`0x14048b330` = `lea rdi,[rcx+8]`). So:

| generator | absolute offset in match ctx | seeded values |
|---|---|---|
| main | `ctx+0x5708` | `t%10000, (t+1)%10000, (t+2)%10000`, slot3 default |
| sub 1 | `ctx+0x1e8+0x108` = `ctx+0x2f0` | same triple |
| sub 2 | `ctx+0x6d8+0x18` = `ctx+0x6f0` | same triple |
| sub 3 | `ctx+0x770+0x10` = `ctx+0x780` | same triple |
| sub 4 | `ctx+0x7d0+8` = `ctx+0x7d8` | same triple |

**Confirmed live (OBS):** match ctx `0x2ab9e070` had `math::Random` (vt `0x1426550d0`,
`slotCount = 2`) with slots `(2783, 784, 785, 193491)` at all five offsets — i.e. the live
kick-off seed was **`t = 2783`** (slot1 = 784 untriggered, slot2 = 785 untriggered).

A second live match context `0x2ab85ee0` had `(2779, 1563388670, 2781, 193491)` at `+0x5708`
(`t = 2779`, slot 1 advanced `~155000`-ish steps) — the previous, longer-running match.

### 2.3 Other seeding sites (all verified on disk)

| site | what it seeds | pattern |
|---|---|---|
| `0x140504100` (offset inside `0x140504100`, called from `0x14041e4d0` ← `0x140418940` ← `match::Match` slot 1 of vtable `0x142596dc8`) | a `math::Random` built by the Fox property factory at `obj+0x18` | online path first (`[0x1434a2d40]` slots `+0x1b8` with `edx = 0,1,2`), else `0x141b34aa0` → `t`, `t%10000 / (t+1)%10000 / (t+2)%10000` (identical math at `0x140504304`–`0x140504381`) |
| `0x1404357e0` | `Random` ctor `0x140a923f0` then `setseed(0,t)`, `setseed(1,2t)`, `setseed(2,3t)` — note `lea r8d,[rbx+rbx]` and `lea r8d,[rbx+rbx*2]` at `0x14043582a` / `0x14043583b` |
| `0x14051e400` | `0x140a923b0` (GameRandom ctor, slot count 2) at `obj+0x20`, then online/`%10000` fallback |
| `0x140870020`, `0x1408bd970`, `0x140441fc0`'s sub-helpers | the same `t` split across slots |
| `0x1406eb670` (per-player, near the `Player*` helpers) | one `setseed` on a player-embedded Random |

**Reseeding during a match:** no site re-seeds the *main* match generator after the constructor.
`0x140a928a0`'s 55 call sites are: the match/online/context constructors above, the four sub-RNG
helpers, `match::Player`-adjacent helpers, `sound::util::Random` (`0x1414962f0`),
`onlinemode::ParameterRandom` (`0x14100b640`), the `ImageUnit*` AI plan nodes
(`0x14211a800`/`0x14211ab10`/`0x14211af20`/`0x14211b1d0`/`0x14211b460`), the replays
(`ReplayBall` `0x14217c870`, `ReplayHuman` `0x14217db10`) and `match::Injury` (`0x140443da0`).
INF (high): halves, set pieces and replays do **not** re-seed the match LCG; they either consume
it or use their own objects.

### 2.4 Per-player generator seeding

INF (medium-high, statistical): the live per-player states are all reachable by the 31-bit LCG from
a small seed, and the observed `slot0/1/2` triples of the *sub*-generators all follow `(t,t+1,t+2)`.
The per-player objects were not pinned to a single live address this session (the match context
moved between scans — see Open list), so the per-player **derivation** (from `t` or from a player
index) is not yet proven. What *is* OBS: they live inline at `player+0x32d0`, use slot index 2
(`edx = 2` at `0x1406ebff`), and are drawn by `0x1406e6bf0` / `0x1406ebf30` / `0x1406eaa10`.

---

## 3. Consumer census (call-site counts from the on-disk call graph)

Built by scanning `.trace` for `e8 rel32` direct calls and mapping each site to its function through
the PE `.pdata` `RUNTIME_FUNCTION` table; class names from the MSVC RTTI dump.

| generator entry | VA | call sites | distinct callers | notable consumers |
|---|---|---|---|---|
| `Range(rng, idx)` | `0x140a92470` | 23 | 18 | `0x140e83f10` ×4, `0x1406eaa10` ×3 (per-player), `0x1406e6bf0` (`PlayerRandomMod`), `0x1406ebf30` (`PlayerRoll100`), `match::player::ActionBasePosition` `0x140956c30`, `sound::util::Random` `0x1414962c0/0x1414962f0`, `onlinesystem::HttpTransactionToFile` `0x141170b40` |
| `RangeRaw (state as float)` | `0x140a924e0` | 49 | 32 | `0x14089bae0` ×7, `0x14051e970` ×4, `0x140435ba0` ×3, `0x140435ef0` ×3, `match::bp::ai::ImageUnit{Breakthrough,Cross,CurveShot,Long,MiddleShot}` `0x14211a800/0x14211ab10/0x14211af20/0x14211b1d0/0x14211b460` |
| `Range0(rng,count)` | `0x140a92530` | 66 | 45 | `0x140471361` ×8, `0x1405b0620` ×4, `0x1404886b0` ×3, `0x14048a260` ×3, `0x1405b103f` ×3, `0x140955060` ×3 (animation variants), `0x1405ae43c`, `0x140955650` |
| `Normal(rng,amp,off)` | `0x140a92590` | 12 | 5 | **`0x1408398d5` ×6** (the kick/scatter writer, runs `0x1408398d5`–`0x14083c1eb`), **`0x1407324b0` ×3** (the shared kick launch formula of the kick-launch report), `0x1405ae43c`, `0x140955650`, and `0x140a927b0` (the ability normal) |
| `AbilityNormal(idx)` | `0x140a927b0` | 1 | 1 | `0x14092ed30` — inside a `match::Player`-owned function (its callers resolve to `.?AVPlayer@match@@`) |
| `Advance(rng, idx)` | `0x140a92880` | 6 | 6 | `0x140873787` (an animation/director-side consumer with an embedded Random at `rbx+0x4a8`), `0x1408c0581`, `0x141f80070`, `0x141fb24b4`, `0x14217cf0e` (replay), `0x142180f6a` |
| `SetSlotState(rng,idx,v)` | `0x140a928a0` | 55 | 36 | see §2.3 |
| `GetSlotState(rng,idx)` | `0x140a928b0` | 1 | 1 | `0x142161b90` |
| `WrapAngleDelta` | `0x140a928c0` | 26 | 24 | `0x14072c88e` ×2, `0x14075e365` ×2, `match::anime::action::Dribble` `0x1407ba840`, plus 22 more animation/AI functions |
| `math::Random` ctor | `0x140a923f0` | 38 | 36 | every owner of an embedded Random |
| `math::Random` dtor | `0x140a92430` | 50 | 39 | `match::Injury` `0x140443da0`, `sound::util::Random` `0x141496280`, `onlinemode::ParameterRandom` `0x14100b6a0`, `fixdemo::Random1000` `0x141f4aca0`, `match::bp::ai::ThinkUnitPassRandomTest` `0x14210dcfd`, `match::player::ActionPassGet` `0x140920cf0` |

Class-level attribution that could be resolved this session (OBS RTTI slot ownership): the kick
scatter writer is called from the `match::anime::action::Kick` family (kick-launch report), the
`ImageUnit*` AI plan nodes are `match::bp::ai` (behaviour-plan AI), the replay objects are
`match::ReplayBall` / `match::ReplayHuman`, and the injury roll is `match::Injury`. The remaining
census entries are plain (non-virtual) functions; their callers are listed by VA in
`census2.json` in the scratch dir and can be resolved class-by-class on request.

### Draw order per tick (partial, OBS where marked)

- The match generator's **slot 0** is the one used by the range draws that go through
  `Range(rng,0)`; the kick writer uses the **per-player** generator (`player+0x32d0`, slot 2), not
  the match generator — OBS from `0x1407324b0` (`rng = player + 0x32d0`, two draws: `FUN_140a92590`
  for the angle width and again for spin) — so kicks do **not** perturb the match generator.
- `0x1406aa10`-family per-player rolls happen per player inside the anime-action update
  (`AnimePlayer` per-tick step `0x1406e5fc0`, per the fox-registry report), i.e. in the
  per-player update loop order.
- INF (medium): the fixed per-tick order is (1) per-player anime action update →
  `player+0x32d0` draws; (2) the match-side `Range0`/`RangeRaw` consumers. The exact global
  ordering of the 66 `Range0` sites per tick was **not** reconstructed — it needs a per-tick
  trace, listed as an open item.

---

## 4. Live check (OBS, read-only)

Read-only samples from `/proc/<pid>/mem` (pid from `pgrep -x PES2021.exe`), no writes, no input.

**Match generators (live match ctx `0x2ab9e070`):**

```
ctx+0x5708 (main)  vt 0x1426550d0 cnt 2  slots (2783, 784, 785, 193491)
ctx+0x2f0  (sub1)  vt 0x1426550d0 cnt 2  slots (2783, 925157588, 785, 193491)
ctx+0x6f0  (sub2)  vt 0x1426550d0 cnt 2  slots (2783, 784, 785, 193491)
ctx+0x780  (sub3)  vt 0x1426550d0 cnt 2  slots (2783, 784, 785, 193491)
ctx+0x7d8  (sub4)  vt 0x1426550d0 cnt 2  slots (2783, 784, 785, 193491)
```

Live kick-off seed: **`t = 2783`** (and, for the previous match, `t = 2779` at ctx `0x2ab85ee0`).

**Step-function verification (OBS):**

- `sub2/3/4` hold the untouched `(t, t+1, t+2)` seeds `(2783, 784, 785)` → the constructor's
  `setseed` calls behaved exactly as §2.1 predicts.
- `sub1` slot1 `925157588` = **`step^2(784)`** — verified:
  `step(784) = 592176908`, `step(step(784)) = 925157588`. OBS.
- An earlier sample of a live match generator read `(1833, 561253743, 835)`;
  `561253743` = **`step^3(834)`** — verified: `step(834)=1962245517`,
  `step²(834)=592176908`, `step³(834)=561253743`. OBS. (A second object at that time read
  `(1833, 1962245517, 835)` = `step¹(834)`, i.e. the same generator one draw earlier.)
- Multiple other live `GameRandom` objects advanced by **exactly 1 step** between two samples
  0.7–1.5 s apart (e.g. `0x1fa2d268` slot1 `705320456 → 871656443`, `0x36b89548` slot1
  `1942358968 → 1990390029`), and one by 4 steps (`0x1d779768` slot2
  `1514138902 → 1254111118`). Each of those was confirmed by forward-stepping with
  `s = (0x4513 - s*0x2d22b2e3) & 0x7fffffff`. **OBS: the step function reproduces every observed
  advance.**

**Limit of the live check:** during several 1–15 s windows in open play the *match* generators at
the five ctx offsets did not change at all while the ball was demonstrably moving
(`pos 2.52 → -8.25` in 0.5 s). INF (high): the five ctx generators are consumed at set-up /
event boundaries (restarts, kick requests), not once per logic tick; the per-tick consumers are the
per-player LCGs (`player+0x32d0`), which were not isolated to a stable address in this session
(the match context/heap layout moved between scans).

---

## 5. Seed pinning (procedure; NOT executed)

Authorisation exists (owner, this session) to write the seed/state of the generators PES seeds at
kick-off, **once**, with logging to `~/ghidra-projects/scratch/RngRE/writes.log`. It was **not
executed**: the game was mid-open-play for the whole session and never returned to a kick-off
transition, so the "write at kick-off, before the first draw" gate could not be met reliably.
Writing at an arbitrary moment would have overwritten an already-advanced state and produced a
misleading "seed", which the authorisation explicitly disallows guessing at.

Procedure to run when the game *is* at a kick-off (state machine value `match::Match` slot-1
state `5`, i.e. right after `0x140418940` runs `0x14041e4d0` → `0x140504100`, or equivalently the
first tick after the context constructor `0x140441fc0`):

1. Read `ctx = *(u64*)0x1436F3DC8`; read the live match context object (the `0x17fd8`-byte one
   created by `0x140421fd0`; locate it by scanning writable memory for a `math::Random` with
   `slotCount == 2` and `slot[3] == 193491`, or by walking the ctx's member pointers).
2. Confirm the candidate generator is at `ctx+0x5708` by checking the other four offsets
   (`+0x2f0, +0x6f0, +0x780, +0x7d8`) also hold `math::Random` objects.
3. Wait until the state machine leaves the kick-off phase (ball `pos stops changing for two 0.25 s
   reads = `MATCH_FROZEN`, then `OPEN_PLAY`), which guarantees the constructor has finished but the
   first tick has not drawn.
4. `os.pwrite` the four slot words: `[0x2f3d0, 0x2f3d1, 0x2f3d2, 0x2f3d3]` at `ctx+0x5708+8`
   (that is, write back the *stock default* — no code patch, no constant change; the multipliers
   `0x2D22B2E3` and `21` stay stock). This makes the seed constant and reproducible.
5. Read back immediately; append to `~/ghidra-projects/scratch/RngRE/writes.log`:
   `time, pid, address, old_value, new_value, ctx`.
6. Show it worked: two read-only samples of `ctx+0x5708` ≥ 0.5 s apart once play starts; each slot
   must equal `step^(n)(0x2f3d0+k)` for the observed draw count `n` — exactly the check in §4.

Script path to be written when executed: `~/ghidra-projects/scratch/RngRE/pin_seed.py` (read-only
helpers already exist: `~/ghidra-projects/scratch/pes_state.py`).

---

## 6. Open list

1. **`0x1415a1780` inside `0x140a92590` is unconfirmed.** The kick report identified neighbours
   (`0x1415a17b6` = `expf`, `0x1415a1780` = `sqrtf`) by idiom; `tanhf` here is inferred from the
   math shape. A C++ port must pin this before trusting the normal path bit-exactly.
   The thunk lives in the protected `.impdata` region (outside the 64 MB dump).
2. **The clock API behind `0x141b34aa0`.** Import list resolves `winmm!timeGetTime`
   (`0x15b961980`), `winmm!timeGetSystemTime` (`0x15b961978`), `kernel32!QueryPerformanceCounter`
   (`0x15b960d08`), `kernel32!GetTickCount` (`0x15b960bc8`), `crt!rand` (`0x15b960770`) — but every
   reference to those slots is a runtime-resolved pointer in `.tls$`/`.impdata`, so the exact API
   is inferred from the `/1000.0` normalisation and the `%10000` reduction, not observed.
3. **Per-player seed derivation.** The state lives at `player+0x32d0` with slot index 2, but how
   each player's initial value is derived from `t` (or from a player index) was not traced.
4. **`FUN_140a924e0`'s consumers that want the raw float**: the mapping from raw 31-bit state to the
   consumer's float use (48 of 49 sites) was not decoded; only the arithmetic of the draw itself.
5. **Per-tick global draw order.** The 66 `Range0` + 49 `RangeRaw` sites per logic tick need a live
   trace (or a per-caller static schedule) before a tick-exact replay is possible. The
   `AnimePlayer` per-tick step order is known from the fox-registry report; the match-side order is not.
6. **The `0x44fffffe` return** of the thrice-rejected normal path — decoded literally but its
   downstream meaning is unknown.
7. **The seed-pinning live check (§5)** was not run; needs the game to be caught at a kick-off.
