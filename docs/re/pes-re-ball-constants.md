# PES ball physics constants — where they come from

Status: **closed** for `C[0x50]`, `C[0x51]`, `C[0x80]` and the wobble table. Source is
`common/match/constant/constant_match.bin`, object `ball.o` — **disk-readable**, no exe
constant block needed. The names come from the game's own JSON dev-loader, the offsets from
its binary loader, and both were cross-checked instruction-by-instruction against the
acceleration function `FUN_140874470`.

Everything below was derived from `<scratch>/pesmem.bin` (base `0x140000000`) with `objdump`
(RVA = VA − `0x140000000`) and from the shipped archive dumped with
`pesfmt_dump cat 'common/match/constant/constant_match.bin'`. Ghidra/GhidraMCP was **not**
used (down). The game was **not** running (`pgrep -f PES2021.exe` empty).

> **Correction to `pes-re-officials-ball.md` §6.2.** That section records "constant_match.bin
> section `ball.o` (352 B) holds different, unnamed values" and lists the source file as OPEN.
> Both are wrong: `ball.o` is **1,040 B** (352 B is `cameraInplay.o`'s size — the two got
> crossed), and it holds *exactly* the live values, with recoverable field names. §7's killed
> hypothesis "`constant::Ball` values come from `constant_match.bin` `ball.o`" must be revived
> and its reason inverted.

---

## 1. Container and record (OBS)

| item | value |
|---|---|
| archive | `common/match/constant/constant_match.bin` (4,817 B on disk) |
| WESYS header | `ff 10 51 'W' 'E' 'S' 'Y' 'S'` + u32 zlib size `0x10` + zlib from +`0x10` |
| inflated flat blob | **22,544 B** (`st2`) |
| record table | u32 count = **27** at flat 0, u32 tableOffset = **0x8**, 12-byte records `{u32 off, u32 size, u32 nameOffset}` |
| `ball.o` | record index **4**, name at its own `nameOffset`, **flat offset `0x3c0`, size `1,040 B`** |
| engine class | `constant::Ball` (RTTI TypeDescriptor RVA `0x36b73f8`, name `.\?AVBall@constant@@`; object-name rule = class name with first letter lower-cased + `.o`, established by the prior `injury.o` resolution) |

Bytes as read back (OBS: the flat blob's first 32 bytes of the record)

```
flat 0x3c0:  3f800000 3f800000 40e00000 41400000  ->  1.0, 1.0, 7.0, 12.0
             40000000 00000110 00000130 00000000  ->  2.0, <blk 0x110>, <blk 0x130>, 0
             41c80000 41200000 00000150 40f00000  ->  25.0, 10.0, <blk 0x150>, 7.5
             41a00000 40a00000 00000170 00000190  ->  20.0, 5.0, <blk 0x170>, <blk 0x190>
             ...
flat 0x424:  42c1fd71                                ->  96.995003
flat 0x428:  4263fb7f                                ->  56.995602
flat 0x458:  3d0f5c29                                ->  0.035
flat 0x46c:  42d20000                                ->  105.0
flat 0x470:  42340000                                ->  45.0
flat 0x474:  3f800000                                ->  1.0
```

### 1.1 Record layout (OBS, decoded)

`ball.o` is **not** a flat float array. It is a 0x110-byte scalar header followed by **24
blocks of 32 bytes** (`0x110 … 0x3f0` inclusive, `0x110 + 24·0x20 = 0x410` = the record size):

* each block = **6 floats + 2 zero u32s** (the last two are padding and are *not* copied);
* the header's u32 fields with values in `{0x110, 0x130, …, 0x3f0}` are **self-relative
  pointers, offset from the start of the record**, to those blocks.

Proof that they are record-relative and not field-relative: the 24 values form an exact
arithmetic progression of step 0x20 covering `[0x110, 0x410)`, while the field addresses that
hold them do not. Field-relative interpretation would give targets `0x124, 0x148, …` (step
0x24, off the 0x20 block grid) and would push the last field's target (`rec+0xec`, value
`0x3f0`) to `rec+0x4dc`, past the record's 1,040-byte end.

---

## 2. Live object and the two loaders (OBS)

`class Ball : constant::ConstantData, constant::BallBase` — class-hierarchy descriptor
`0x2df6708`, base-class array `0x2df6720`, `BallBase` pmd **offset 8**. `Ball`'s vtable is at
RVA `0x2b57440` (COL at `0x2df66e0`), with exactly **two** virtual functions:

| slot | VA | function |
|---|---|---|
| 0 | `0x141e73ee0` | `Ball::LoadJson` — reads `DevelopData/common/match/constant/match/ball.json` (dev only) |
| 1 | `0x141e73720` | `Ball::LoadRecord` — **the shipping binary loader** for the WESYS record |

`LoadRecord` (`0x141e73720 … 0x141e73ed2`, 175 `mov` assignments, OBS) copies
`this = Ball*`; `BallBase* = Ball* + 8`. The live field array starts at `Ball+0x08`, so the
acceleration code (which takes the `BallBase*`) indexes `[r + 4k]` while the report's
`C[k]` is `[Ball + 8 + 4k]` — i.e. **code offset = report live offset − 8**.

Because the shipped record's block pointers are 4 bytes on disk but expand to 24 bytes of
live floats, the disk→live mapping is **field-by-field**, not a constant shift. The exact map
is in §3.

`LoadJson` is what gives the names: it passes NUL-terminated key strings to
`0x1403842a0(node, name)` then `0x140384750` (scalar) or `0x1403841e0(value, index)`
(array element). The keys live in `.rdata` around RVA `0x2b56e78 … 0x2b57408`. 23 six-float
arrays are read with a `cmp $0x6` loop — exactly matching the 23 pointer fields that point at
blocks.

---

## 3. Field-name → offset map for `constant::Ball` (OBS)

Disk offsets are **within `ball.o`** (add `0x3c0` to get the flat-blob offset). "live" is the
`Ball`-object offset the report's `C[k]` uses (`live = C[k] ⇒ live = 8 + 4k`); "code" is the
offset from the `BallBase*` the integrator is handed (`code = live − 8`).

### 3.1 The fields this task asked for

| JSON name | disk off (ball.o) | live (Ball obj) | code (BallBase) | `C[k]` | value |
|---|---|---|---|---|---|
| **dragSpeedMax** | `+0x064` | `+0x148` | `+0x140` | `C[0x50]` | **96.995003** km/h |
| **dragSpeedMin** | `+0x068` | `+0x14C` | `+0x144` | `C[0x51]` | **56.995602** km/h |
| **magnusRate** | `+0x098` | `+0x208` | `+0x200` | `C[0x80]` | **0.035** |
| nonSpinMax | `+0x0AC` | `+0x244` | `+0x23C` | `C[0x8F]` | 105.0 km/h |
| nonSpinMin | `+0x0B0` | `+0x248` | `+0x240` | `C[0x90]` | 45.0 km/h |
| nonSpinRate | `+0x0B4` | `+0x24C` | `+0x244` | `C[0x91]` | 1.0 |
| nonSpinTopDownDecRate | `+0x0B8` | `+0x250` | `+0x248` | `C[0x92]` | 1.0 |
| nonSpinTopRiseDecRate | `+0x0BC` | `+0x254` | `+0x24C` | `C[0x93]` | 1.0 |
| airRegistNormal | `+0x000` | `+0x008` | `+0x000` | `C[0]` | 1.0 |

§6.3's "wobble clamps `C[0x8F]=105, C[0x90]=45, C[0x91]=1.0`" are therefore
**`nonSpinMax` / `nonSpinMin` / `nonSpinRate`**, not wobble parameters. The real wobble table
is §5.

Confirmed against the shipped integrator `FUN_140874470` (OBS, raw `objdump`):

```
1408745b4:  movss  0x140(%rax),%xmm2     ; dragSpeedMax   96.995   (rax = BallBase*)
1408745bc:  movss  0x144(%rax),%xmm1     ; dragSpeedMin   56.9956
1408745d8:  comiss %xmm2,%xmm6           ; s vs dragSpeedMax
1408745ee:  comiss %xmm1,%xmm6           ; s vs dragSpeedMin
1408745fe:  movss  (%rax),%xmm7          ; airRegistNormal = 1.0   -> k = C0 * -0.5 * rho * A
140874605:  mulss  0x14258d694,%xmm7     ; -0.5
14087460d:  mulss  0x142608bfc,%xmm7     ; 1.293
140874615:  mulss  0x142608bf8,%xmm7     ; 0.037110474   (= pi*r^2)
14087463f:  subss  0x1425a9678,%xmm12    ; -9.80665
140874702:  movss  0x240(%rax),%xmm0     ; nonSpinMin  45
140874719:  movss  0x23c(%rax),%xmm5     ; nonSpinMax  105
140874739:  mulss  0x244(%rax),%xmm5     ; nonSpinRate 1.0
140874741:  mulss  0x200(%rax),%xmm5     ; magnusRate  0.035     <-  C[0x80]
```

### 3.2 All other named fields (OBS)

| JSON name | disk off | live | value read back |
|---|---|---|---|
| backSpinBuoyancyRate | `+0x004` | `+0x00C` | 1.0 |
| backSpinLogRate | `+0x008` | `+0x010` | 7.0 |
| boundBoundSpeedMax | `+0x00C` | `+0x014` | 12.0 |
| boundBoundSpeedMin | `+0x010` | `+0x018` | 2.0 |
| boundFrictionSpeedMax | `+0x020` | `+0x050` | 25.0 |
| boundFrictionSpeedMin | `+0x024` | `+0x054` | 10.0 |
| boundRollMax | `+0x02C` | `+0x070` | 7.5 |
| boundRotationSpeedMax | `+0x030` | `+0x074` | 20.0 |
| boundRotationSpeedMin | `+0x034` | `+0x078` | 5.0 |
| curve | `+0x058` | `+0x13C` | 0 |
| debugKickAngle | `+0x05C` | `+0x140` | 3.0 |
| debugKickSpeed | `+0x060` | `+0x144` | 35.0 |
| grounderBoundYAdd | `+0x088` | `+0x1F8` | 0.09 |
| grounderCTan | `+0x08C` | `+0x1FC` | 8.0 |
| grounderFrictionRate | `+0x090` | `+0x200` | 0.985 |
| grounderSpeed | `+0x094` | `+0x204` | 20.0 |
| natualBoundB | `+0x0A0` | `+0x224` | 0.4 |
| naturalRoll | `+0x0A8` | `+0x240` | 2.0 |
| roll | `+0x0CC` | `+0x28C` | −2.0 |
| rollToBoundAdd | `+0x0D0` | `+0x290` | 0.07 |
| rollToBoundRollMax | `+0x0D4` | `+0x294` | 7.0 |
| rollToBoundRollMin | `+0x0D8` | `+0x298` | 2.0 |
| stopSpeed | `+0x0F4` | `+0x02DC` | 0.175 |
| rollToSpeedMaxRoll / topSpin\* | `+0x0F8 … +0x100` | `+0x2E0 … +0x2E8` | see §4 script |

(The remaining scalar slots are filled from block pointers; names for those are the 23 array
names below.)

### 3.3 The 23 six-float arrays (OBS)

Each is a header pointer field → a 32-byte block. Disk "blk" is the block's offset in `ball.o`.

| JSON name (array, 6 × f32) | disk ptr field | disk block | live |
|---|---|---|---|
| boundBoundSpeed (2) | `+0x14` | `+0x110` | `+0x01C` |
| backSpin\* (2) | `+0x18` | `+0x130` | `+0x034` |
| boundBoundRate | `+0x28` | `+0x150` | `+0x058` |
| boundCheckFrictionAddRate | `+0x38` | `+0x170` | `+0x07C` |
| boundCheckRotationDecRate | `+0x3C` | `+0x190` | `+0x094` |
| boundFrictionSpeedMax/Min | `+0x40` | `+0x1B0` | `+0x0AC` |
| boundFrictionRollMax | `+0x44` | `+0x1D0` | `+0x0C4` |
| boundFrictionRollSpeedMax | `+0x48` | `+0x1F0` | `+0x0DC` |
| boundFrictionRollSpeedMin | `+0x4C` | `+0x210` | `+0x0F4` |
| boundFrictionRollSpeedStop | `+0x50` | `+0x230` | `+0x10C` |
| boundRate | `+0x54` | `+0x250` | `+0x124` |
| boundRollSpeedMax | `+0x6C` | `+0x270` | `+0x150` |
| boundRollSpeedMin | `+0x70` | `+0x290` | `+0x168` |
| boundRollSpeedStop | `+0x74` | `+0x2B0` | `+0x180` |
| boundToRotationAddRateXZ | `+0x78` | `+0x2D0` | `+0x198` |
| boundToRotationAddRateY | `+0x7C` | `+0x2F0` | `+0x1B0` |
| boundToRotationBaseXZ | `+0x80` | `+0x310` | `+0x1C8` |
| boundToRotationBaseY | `+0x84` | `+0x330` | `+0x1E0` |
| frictionRollRate\* | `+0x9C` | `+0x350` | `+0x20C` |
| frictionRollSpeed\* | `+0xA4` | `+0x370` | `+0x228` |
| rollToBound\*/RollMax | `+0xC0` | `+0x390` | `+0x258` |
| rollToSpeedSpeedY\* | `+0xC4` | `+0x3B0` | `+0x270` |
| rollToSpeedSpeedYRateBase | `+0xDC` | `+0x3D0` | `+0x29C` |
| grounder\* (last array) | `+0xEC` | `+0x3F0` | `+0x2C0` |

The array names are the *suffix-shared* string pool at RVA `0x2b56e78…0x2b57408`; each entry
above is the exact NUL-terminated string the JSON loader passes, and the 23 JSON array-loop
bases (`+0x1C, +0x34, +0x58, +0x7C, +0x94, +0xAC, +0xC4, +0xDC, +0xF4, +0x10C, +0x124,
+0x150, +0x168, +0x180, +0x198, +0x1B0, +0x1C8, +0x1E0, +0x20C, +0x228, +0x258, +0x270,
+0x29C`) match the binary loader's 23 block copies 1:1 (OBS: both functions enumerate the
same 23 destinations in the same order).

---

## 4. Coder recipe (implementable)

`ball.o` is found **by record name**, never by a magic offset, and always inside the user's
install. `ParseUniformTable` (`src/pes/uniform.cpp`) already does everything except the
record's internal block pointers, which must be added:

```cpp
// 1. read the container from the VFS (dt18_all.cpk -> common/match/constant/constant_match.bin)
// 2. inflate: blob[0..15] is the WESYS header; zlib from +0x10 -> flat (22,544 B)
// 3. u32 count = flat[0], u32 table = flat[4];
//    records i in [0,count): off=flat[table+12i], size=flat[table+12i+4],
//                            nameOff=flat[table+12i+8] -> NUL-terminated name
// 4. take the record whose name == "ball.o"  (class constant::Ball)
const uint8_t* r = flat.data() + off;          // off == 0x3c0, size == 1040
```

Field read:

```cpp
float BallScalar(uint32_t k) {                  // k is the C[] index
  // direct scalars: the loader's per-field table is authoritative (see §3.1/§3.2).
  // The three this task needs:
  //   dragSpeedMax : r[0x64]   (C[0x50]) == 96.995003   km/h
  //   dragSpeedMin : r[0x68]   (C[0x51]) == 56.995602   km/h
  //   magnusRate   : r[0x98]   (C[0x80]) == 0.035
  float v; std::memcpy(&v, r + 0x64, 4); return v;
}
float BallArrayElem(uint32_t slot, int i) {      // block pointers, record-relative
  uint32_t blk; std::memcpy(&blk, r + slot, 4);  // slot e.g. 0x14
  float v; std::memcpy(&v, r + blk + 4*i, 4);    // 6 valid floats; u32[6],[7] are padding
  return v;
}
```

For the airborne model of §6.3 the complete dependency list is
`{C[0] (airRegistNormal), C[0x50] (dragSpeedMax), C[0x51] (dragSpeedMin),
C[0x80] (magnusRate), C[0x8F] (nonSpinMax), C[0x90] (nonSpinMin), C[0x91] (nonSpinRate)}`
→ disk offsets `{0x00, 0x64, 0x68, 0x98, 0xAC, 0xB0, 0xB4}`, all plain little-endian f32 in
`ball.o`, no block pointers.

Two *non*-disk constants are still needed, and they are **exe-image** values (they are not in
`ball.o`): the gravity/air/radius group (see §5) and the spin scale. Read them from the
image, not from the archive:

| value | exe VA | RVA | section |
|---|---|---|---|
| 7.7160487 (crisis threshold ²) | `0x142608c00` | `0x2608c00` | `.rdata` |
| 1.293 (ρ) | `0x142608bfc` | `0x2608bfc` | `.rdata` |
| 0.037110474 (A = π r²) | `0x142608bf8` | `0x2608bf8` | `.rdata` |
| 9.80665 (g) | `0x1425a9678` | `0x25a9678` | `.rdata` |
| −0.5 | `0x14258d694` | `0x258d694` | `.rdata` |
| π | `0x142578480` | `0x2578480` | `.rdata` |
| 0.0118126301 (r²) | `0x1425ea0c0` | `0x25ea0c0` | `.rdata` |
| 0.1086859330 (r) | `0x142598958` | `0x2598958` | `.rdata` |
| 23.6111107, 48.22531, −2.44131 | `0x142608c04…0x0c` | | `.rdata` |
| 1000.0 / −1000.0 | `0x14256ae34` | | `.rdata` |
| 3600.0 (km/h → m/s) | `0x142598590` | | `.rdata` |
| **492.181 (spin scale)** | **runtime-written; see §4.1** | | `.data` slot `0x349f780` holds **1.0f on disk** |
| 0.00203177286 (= 1/492.181) | **runtime-written; see §4.1** | | `.data` slot `0x349f740` holds **1.0f on disk** |
| **0.4 (wobble period)** | **`0x14259b89c`** | `0x259b89c` | `.rdata` |

Nothing in this list is in `ball.o` (OBS: byte searches of the inflated blob for `9.80665`
and `492.181` find nothing, while `96.995`, `56.9956`, `0.035`, `105`, `45` are each found at
exactly one flat offset, inside `ball.o`).

### 4.1 The spin scale is written into `.data` at runtime (OBS) — correction

The first version of this report listed `492.181` at `.data` RVA `0x349f780` and
`1/492.181` at `0x349f740` as if they were file constants. **They are not.** Reading the
on-disk exe (`$PES21_ROOT/PES2021.exe`, `.data` file offset = RVA − `0xE00`):

| RVA | exe on disk | loaded image |
|---|---|---|
| `0x349f774` | 0.0 | 0.0 |
| `0x349f778` | 0.0 | 0.0 |
| `0x349f77c` | **1.0** | 1.0 |
| `0x349f780` | **1.0** | 492.1809997558594 |
| `0x349f754` | **1.0** | 0.00203177286 |
| `0x349f740` | **1.0** | 0.00203177286 |
| `0x349f794` | **1.0** | 492.1809997558594 |
| `0x349f7a8` | **1.0** | 492.1809997558594 |

The float `492.181f` (`0x43f6172b`) is **not present anywhere in the on-disk exe** and
`0x3b05277e` (`1/492.181f`) is absent too — they only exist as *immediates inside `.text`
instructions*.

**The writer (OBS, raw bytes at RVA `0x875f98 … 0x875fce`, VA `0x140875f98 … 0x140875fce`,
ending in `ret` + `cc` padding — a one-time initializer):**

```
VA 0x140875f98  48 c7 05 <rel32=0x02c29805> 2b 17 f6 43   movq $0x43f6172b, 0x14349f7a8
VA 0x140875fa3  c7 05 <rel32=0x02c29793> 7e 27 05 3b      movl $0x3b05277e, 0x14349f740
VA 0x140875fad  c7 05 <rel32=0x02c2979d> 7e 27 05 3b      movl $0x3b05277e, 0x14349f754
VA 0x140875fb7  c7 05 <rel32=0x02c297bf> 2b 17 f6 43      movl $0x43f6172b, 0x14349f780   <-- spin scale
VA 0x140875fc1  c7 05 <rel32=0x02c297c9> 2b 17 f6 43      movl $0x43f6172b, 0x14349f794
VA 0x140875fcb  c3                                            ret
```

So the spin scale is **code-embedded as an immediate**, and the `.data` slot is only a
runtime mirror of it. For the engine: treat `492.181` as a source constant, or read the
immediate at VA `0x140875fb7` (`.text`). The load at `FUN_140874470+0xBD`
(`movss 0x14349f780,%xmm2` @ VA `0x14087452d`) is unchanged and correct.

INF (high confidence): the initializer must run before any ball physics, since the loaded
image holds the written value at the exact slot the integrator reads, and it is the only
writer. Not verified: the call site of that initializer (it was not walked).

**Corrected note on how the wobble scale was originally derived.** §6.3's `492.181` came
from a *live observation* (`w / raw ≈ 492.181`), not from a file constant; it now checks out
against this immediate, so the derived number is confirmed even though its file source was
mis-stated.

---

## 5. Wobble table (OBS) — location and layout

It is **not** in `ball.o` and **not** a static exe table. It is a live per-ball-state array
inside the sim ball state object (`r9`/`rbx` in `FUN_140874470`), at
**`ballstate + 0x2c`, 6 entries × 3 floats (12-byte stride, 72 bytes total)**.

```
1408744ed:  movzbl 0x74(%rbx),%r8d      ; wobble flag byte, ballstate+0x74
1408744f5:  test   %r8b,%r8b
1408744f8:  je     0x140874568          ; flag == 0 -> use raw spin instead
1408744fa:  addss  0x28(%rbx),%xmm7     ; t    += dt   (accumulated time, ballstate+0x28)
1408744ff:  movss  %xmm7,0x28(%rbx)
140874504:  divss  0x14259b89c,%xmm7    ; t / 0.4          <- wobble period 0.4 s
14087450c:  cvttss2si %xmm7,%ecx        ; idx = int(t/0.4)
140874510:  cmp    $0x6,%ecx
140874515:  b8 55 55 55 d5 ; imul %ecx ; ... lea (%rdx,%rdx,2) ; lea (%rcx,%rax,2)  -> idx %= 6
14087452d:  movss  0x14349f780,%xmm2    ; 492.181
140874538:  movslq %ecx,%rax
14087453e:  lea    (%rax,%rax,2),%rcx
140874542:  mulss  0x2c(%rbx,%rcx,4),%xmm0   ; w.x = 492.181 * tbl[idx].x
140874548:  mulss  0x30(%rbx,%rcx,4),%xmm1   ; w.y = 492.181 * tbl[idx].y
14087454e:  mulss  0x34(%rbx,%rcx,4),%xmm2   ; w.z = 492.181 * tbl[idx].z
```

So `w = 492.181 × wobbleTable[int(t/0.4) mod 6]`, a 3-vector, and the fallback branch
(`0x140874568`) builds `w` from the raw spin. The `js`/`jl` guards clamp a negative index to
the fallback.

The enum "6 entries" matches §6.3's `table[int(t/0.4) mod 6]` exactly. **The table's own
values are still unknown** — they are produced by whichever writer fills `ballstate+0x2c`
(the ball-state writer among the ~40 callers of `FUN_140876360`). Note that the block at
`ball.o +0x330` (→ live `+0x1E0`, array name `boundToRotationBaseY`) holds u32
`5, 5, 5, 9, 9, 5`, which as f32 is ~7e-45 — it is an **integer** table, not the wobble table,
and must not be confused with it. There is no 6-float float table anywhere in `ball.o` whose
values look like a spin pattern.

---

## 6. Still open

| # | item | what was tried |
|---|---|---|
| 1 | **wobble table contents** at `ballstate+0x2c` | located the code (above); the writer was not followed. Candidates: the ball-state reset in `FUN_140876360`'s callers. |
| 2 | Field names for the ~40 remaining scalar slots filled from block pointers | `LoadJson` names the 23 arrays and ~40 scalars; the rest of `BallBase` is padding/gap or filled by the arrays. Not needed for the airborne model. |
| 3 | §6.4 ground contact / bounce / rolling (still OPEN, deliberately untouched) | — |
| 4 | `LoadRecord`'s pointer resolver `0x141e82ba0` | the address is misaligned relative to code and its disassembly starts mid-instruction; the record-relative semantics were proven from the data instead (§1.1). |
| 5 | ~~spin scale `492.181` file source~~ **RESOLVED §4.1** | it is an immediate `movl $0x43f6172b,0x14349f780` @ VA `0x140875fb7`, written into `.data` at runtime; the `.data` slot holds `1.0f` on disk. Remaining: the initializer's call site was not walked, so "runs before first use" is INF. |

Exe-image addresses above were mapped with the section rule `.rdata`/`.trace` = VA −
`0x140000000` (VA − `0x140000A00` file offset) and `.data` = VA − `0x140000000` (VA −
`0x140000E00` file offset), verified against the values read back matching the §6.3
formula to the last digit.

**Verification performed.** Every mapped value in §3.1 was read back out of the shipped
archive bytes *and* re-confirmed as an operand of `FUN_140874470`. No build, no ctest, no
game process, no repo edits.
