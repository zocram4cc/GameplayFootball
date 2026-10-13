# PES 2021 marking rasterisation at texel level — MarkingPixelsRE-2

Scope: the exact integer/2048-px arithmetic PES performs when it rasterises digits, the
print name and the leg number into the per-player marking textures, and the status of the
remaining marking kinds. Companion to `docs/re/pes-re-markings.md` (whose §2–3 rules this
re-derives from raw instructions), `docs/re/pes-re-uniform-params.md` and
`docs/re/pes-re-shaders.md` §3.1.

Tags: **OBS** = derived from disassembly of `<scratch>/pesmem.bin` (image VA; file offset VA −
0x140000000) or from VFS bytes; **INF** = inferred. The prior pass's Ghidra names
(`FUN_*`, `DAT_*`) are kept so the addresses stay greppable. Every VA below is readable at
runtime from the image. All arithmetic is given in float32 exactly as the exe computes it
(the constants are the image's float bit patterns, quoted as `%08x`).

Method: raw `objdump -D -b binary -m i386:x86-64 --adjust-vma=<VA>` on bytes sliced from the
image (the decompiler was used only to locate call sites and constants; every rule below was
re-read from instructions), plus synthetic renders of the resulting rules against the decoded
VFS strips (PNG evidence kept in `<scratch>/MarkingPixelsRE/`).

Headless invocation that works (prior attempts failed on arg parsing):
`/opt/ghidra/support/analyzeHeadless $HOME/ghidra-projects/scratch/main PES21 -process pesmem.bin -noanalysis -readOnly -scriptPath <dir> -postScript <Script>.java <ABSOUT> <addr>-<addr> …`
with a freshly renamed copy of `DecompAt.java` (the original fails to load under this install).

---

## 1. Digit cell — OBS `FUN_1419e4a90` (0x1419e4a90, 216 B)

Signature: `(uint* uOut, uint* vOut, float* wOut, float* adjOut, int d /* stack+0x28 */, bool vertical /* stack+0x30 */)`.

```
if (vertical) {                       // stack byte at +0x30 != 0
  *uOut    = 0;
  *vOut    = (float)d * 0.142a96c08;  // 3dcc0000 = 0.099609375
  *wOut    = (d == 1) ? 0x3f400000    // 0.75
                        : 1.0f;
  *adjOut  = (d == 1) ? 0x3e200000    // 0.15625
                        : 0.0f;
} else {
  *vOut    = 0;
  *uOut    = (float)d * 0x142a9734c;  // 3dc80000 = 0.09765625
  if (d > 1) *uOut -= 0x142a97348;    // 3cf00000 = 0.029296875
  *wOut    = (d == 1) ? 0x3d480000    // 0.048828125
                       : 0x3da00000;  // 0.078125
  *adjOut  = (d == 1) ? 0x3e400000    // 0.1875
                       : 0.0f;
}
```
`uOut`/`vOut` are written as **32-bit integers** (`mov %r10d,(%rcx)` / `mov %r10d,(%r9)`),
i.e. the two zero cases store the *integer* 0, which is bit-identical to float 0.0. Writes are
`movss` — no rounding is applied anywhere; the scale factors are the exact binaries of
200/2048, 60/2048, 100/2048, 160/2048, 204/2048, 3/4, 384/2048, 320/2048.

**2048-px cells (horizontal strip):** digit `d` occupies
- `d = 0` → `[0,160)`
- `d = 1` → `[200,300)` (u = 200, w = 100, adj = 384)
- `d ≥ 2` → `[200d − 60, 200d + 100)`

Matches the shipped `u0001p1_back.ftex` (2048×256, ten upright digits 0–9, OBS — rendered and
examined; cell 0 `x[9,118] w=110`, cell 1 `x[13,70] w=58`, cell 2 `x[10,116] w=107` … all inside
the predicted 200-px pitch, ink height 180 of the 256).

**Vertical strip (4cc packs):** cell `d` = `v ∈ [204.8 d, 204.8 (d+1))`, u ∈ [0,1), and
`adj = 320/2048` only for digit 1. The ink height is 178–180 of 204.8 (measured) — the cell is
tight-cropped in v, so **the vertical strip holds the glyphs pre-rotated 90° clockwise and
cropped to the cell height**, which is what mode 1 un-does (see §3).

> **RESOLVED 2026-10-13 (orchestrator; LegNumberTruth agent RE + frames).** The leg number is drawn upright, with no net rotation, and the clockwise claims in §1, §3, §4 and §6 item 1 are wrong. Evidence:
>
> - The emitter `FUN_1419c0290` writes the position rectangle into slots 0..3 and the UV rectangle into slot `table[i]`. Its mode-1 permutation table at `0x142a91cd8` is `{1,3,0,2}`, which solves to `fu = fx, fv = fy`, the identity, not `U = fy, V = 1 − fx`.
> - `mode != 0` in `FUN_1419e2c10` only swaps which dividend goes on which axis. That produces the landscape quad: one-digit stores at `0x1419e2efc..f0e` give pos `(13, 32, 102, 64)` on the 128² canvas (pad 26), which matches `LegQuad`.
> - Both strip layouts hold upright glyphs (decoded and examined): vanilla `a0150p1_leg` is 1024×128 with ten cells along u; the 4cc `u0767p1_leg` is 64×1024 with ten cells down v, pitch 102.4 px.
> - Frames from the repo's existing composer (+90° blit into its own quad convention) show an upright 7 on the vanilla kit (team 1, vanilla root) and on the 4cc kit (team 767), RX 6900 XT. The −90° rewrite drew the vanilla 7 upside down and was dropped.


## 2. Number layout inside a region — OBS `FUN_1419e2c10` (0x1419e2c10)

Signature (register order, verified from the prologue's stack offsets):
`(rt, bool twoPass /*dl*/, tex, int n /*r9d*/, float spacing /*xmm1*/, bool zeroPad,
 float x0, float y0, float rw, float rh, uint W, uint H, int mode /*param_13*/,
 float pad, uint8 r, uint8 g, uint8 b)`.

Stack slots in the frame: `0x190(%rbp)` = spacing, `0x198` = zeroPad, `0x1a0` = x0,
`0x1a8` = y0, `0x1b0` = rw, `0x1b8` = rh, `0x1c0` = W, `0x1c8` = H, `0x1d0` = mode,
`0x1d8` = pad, `0x1e0/0x1e8/0x1f0` = r/g/b.

### 2.1 Preamble — OBS
```
pad = pad / rh;                                  // divss xmm12,0x1d8(rbp)
f   = (spacing * 0.16f - 0x16f) * 0.5f;          // 0x1425cd2f0 = 3e23d70a = 0.16
                                              // 0x142565600 = 3f000000 = 0.5
```
`f` is the spacing expansion; `s = kit BackNumberSpacing/14` (FUN_141e58c80), so
`f = 0.08 (s − 1)` as documented, but computed exactly as `((s·0.16) − 0.16)·0.5`.
Set-blend = `0x1416f0890`, value 2 on pass 1 and 4 on pass 2 (`mov $2,%edx / mov $4,%edx`
at 0x1419e2dc0), i.e. mode 2 then mode 4 as documented (the first pass is mode 2).

### 2.2 `n < 1` — OBS
Draws nothing (both output streams zeroed at 0x1419e2ca8). `n ≥ 1000` also draws nothing
(the `else if (n < 1000)` chain falls through with no call).

### 2.3 The per-digit source rect — OBS
`FUN_1419e4a90(&u,&v,&w,&adj)` is called with `local_240 = bVar1` = `stripWidth < stripHeight`
(`FUN_14167e1d0`/`FUN_14167e190` give the bound texture's dimensions; `cmp %bx,%ax; seta`).
`h` is **not** from the digit function: it is
```
h = 0x142a96c08  // 0.099609375, vertical
  = 0x14256ae1c  // 1.0, horizontal
```
chosen by the same test at 0x1419e2df5 and stored to `0x30(%rsp)`.

### 2.4 One digit (`n < 10 && !zeroPad`) — OBS (0x1419e2e2b–0x1419e2f65)
Registers: `W` = xmm6, `H` = xmm0, `rw` = xmm15, `rh` = xmm12, `x0` at `0x1a0(%rbp)`,
`y0` = xmm10, `pad` at `0x1d8(%rbp)`.
```
a1 = (float)((W>>1) - (W>>2)) + (float)W * adj * 0.5f;   // W/4 + W·adj/2
b1 = pad * 0.5f;                                          // pad/2
c1 = (float)(W>>1) - (float)W * adj;                      // W/2 - W·adj
d1 = (float)H - pad;
if (mode == 0) {
  dstRect = { x0 + (a1/W)*rw,  y0 + (b1/H)*rh,  (c1/W)*rw,  (d1/H)*rh };
} else {
  dstRect = { x0 + (b1/H)*rw,  y0 + (a1/W)*rh,  (d1/H)*rw,  (c1/W)*rh };
}
```
(`a1`/`b1` swap axes when `mode != 0`; note the axis-swap is applied to the *dividends*, so the
`/W` and `/H` divisors stay with their own axis.) In normalised form, mode 0 gives
`width = 0.5 − adj`, `x = 0.25 + adj/2`, `y = pad/2/H`, `h = (H−pad)/H` — exactly §2.2 of the
report. `W/4` and `W/2` are computed as **integer** `(W>>1) − (W>>2)` and `W>>1` converted to
float (`shr`/`sub`, then `cvtsi2ss`), so a CPU rasteriser must use the same integer shifts.

### 2.5 Two digits (`n < 100`) — OBS (0x1419e2fb4–0x1419e3318)
`oT` (fVar15) and `oU` (fVar10) come from:
```
n == 11            :  oT = b − f,  oU = f
n % 10 == 1        :  oT = a − f,  oU = f + a
n / 10 == 1 (else) :  oT = a − f,  oU = f − a
otherwise          :  oT = −f,      oU = f
```
with `a = 0x142a96c00` (bits 3d555555 = 0.0520833320915699, the float nearest 1/19.2),
`b = 0x142a96c0c` (bits 3dd55555 = 0.104166664, = 2a exactly), `−a = 0x142a96c1c`
(bits 0xbd555555). Digit cells are read twice (tens then units).
```
adjT = adj(tens), adjU = adj(units)
mode 0: tens at { x0 + oT*rw,      y0 + (pad/2)/H*rh, (0.5−adjT)*rw, ((H−pad)/H)*rh }
        units at { x0 + (0.5+oU)*rw,  same y/h,          (0.5−adjU)*rw, same h }
mode 1: tens at { x0 + (pad/2)/H*rw, y0 + oT*rh,  ((H−pad)/H)*rw, (0.5−adjT)*rh }
        units at { same x,              y0 + (0.5+oU)*rh, same w,    (0.5−adjU)*rh }
```
Note the constants `0.5` here are `0x142565600` (0.5f) as in §2.4.

### 2.6 Three digits (`n < 1000`) — OBS (0x1419e3372–0x1419e398b)
`hundreds = n/100`, `tens = (n/10)%10`, `units = n%10`, with `fVar14 = f`, `fVar2 = a`
(`0x142a96c00`), `fVar19 = b` (`0x142a96c0c`), `fVar9` initialised to `−a`
(`0x142a96c1c`), and `LAB_1419e3455: oT = 0.0`.
```
fVar12 = oH, fVar10 = oT, fVar11 = oU.
if (tens == 1) {
  if (units == 1) {
    oH = b − f;
    if (hundreds == 1) { oU = f − b; oT = 0; }
    else               { oU = f − a; oT = a; }
  } else {
    oH = a − f;
    if (hundreds == 1) { oU = f − b; oT = −a; }   // oT keeps fVar9 = −a
    else               { oU = f − a; oT = 0; }
  }
} else {
  oU = f;                                          // default
  if (units == 1) {
    oH = a − f; oT = a' (0x142a96c04, bits 3d5558d4);
    if (hundreds == 1) { oU = f − a; oT = 0; }
  } else {
    oH = −f;                                       // xor of the sign bit
    oU = f;  oT = 0;                               // hundreds != 1
    if (hundreds == 1) { oU = f − a; }             // oT still 0
  }
}
```
All eight rows of §2.2's table reproduce exactly.
```
w1 = W/3 − W·adjH,  w2 = W/3 − W·adjT,  w3 = W/3 − W·adjU      (W/3 = W * (1/3f), 0x14258c264 = 40400000 = 3.0f, used as a divisor)
aH = W·adjH/2, aT = W/3 + W·adjT/2, aU = 2W/3 + W·adjU/2
mode 0: hundreds { x0 + oH*rw + (aH/W)*rw,  y, (w1/W)*rw, ((H−pad)/H)*rh }
        tens     { x0 + oT*rw + (aT/W)*rw,  y, (w2/W)*rw, h }
        units    { x0 + oU*rw + (aU/W)*rw,  y, (w3/W)*rw, h }
mode 1: hundreds { x0 + (pad/2)/H*rw, y0 + oH*rh, ((H−pad)/H)*rw, (w1/W)*rh }
        … axis-swapped the same way as §2.4.
```
Matching §2.2's table (`oT`/`oU`/`oH` columns are oT/oU/oH in that order).

### 2.7 What differs from the documented table — OBS
Nothing in §2.2 of the report is contradicted; two clarifications:
- the "hundreds==1" column of the three-digit table does not change `oH` at all — `b−f` when
  tens==1&&units==1, `a−f` in every other case except `tens!=1&&units!=1`, where it is
  `−f` regardless of the hundreds digit. The PDF's table has that right but implies a
  distinction that does not exist;
- the divisor for `W/3` is the float `3.0f` (`divss 0x14258c264`), not an integer shift, and
  `W/4`, `W/2` **are** integer shifts. A CPU rasteriser must reproduce both.

## 3. The quad emitter and its UV permutations — OBS `FUN_1419c0290` (0x1419c0290, 320 B)

`(builder /*rcx*/, float* pos /*r8*/, float* uv /*rdx*/, int mode /*r9d*/)`
where `pos = {x, y, w, h}` and `uv = {u, v, uw, vh}`.

The four destination corners are written at fixed slots, and their UVs are indexed by a
mode-specific table:

| mode | table VA | `(i0,i1,i2,i3)` | mapping `dest (fx,fy) → uv (U,V)` |
|---|---|---|---|
| 0 | 0x142a91cc8 | (0,1,2,3) | `U = fx, V = fy` |
| 1 | 0x142a91cd8 | (1,3,0,2) | **`U = fy, V = 1 − fx`** |
| 2 | 0x142a91ce8 | (2,0,3,1) | `U = 1 − fy, V = fx` |
| 3 | 0x142a91cf8 | (3,2,1,0) | `U = 1 − fx, V = 1 − fy` |

Read from the stores: corner k gets position `(x + (i_k&1)*w, y + (i_k>>1)*h)` and UV
`(u + (j_k&1)*uw, v + (j_k>>1)*vh)` with `i = table`, `j = 0..3` in order — i.e. slot `i_k`
receives UV corner `k`. Solving the four corners gives the table above.

**Mode 1 is the leg-number rotation: `U = fy`, `V = 1 − fx`.** Since the vertical strip holds
pre-rotated glyphs (ink 178–180 px of a 204.8-px cell, i.e. the glyph's *height* runs along the
strip's u), the composition "cell → mode-1 map" is a net **90° clockwise** rotation of an
upright glyph. Verified by rendering: `u0767p1_leg.ftex` (64×1024) mapped through mode 1 with
the §2.4 mode-1 quad produces upright, correctly proportioned digits stacked top-to-bottom
(`leg3_10.png`, `leg3_23.png` — OBS renders, examined).

The `+0x88/+0x89` bytes are `pos[4] == 0` and `uv[4] == 1` — bools on a 5th float slot, not
geometry.

## 4. The leg number — OBS, closes the `ponytail` guess

`FUN_1419e3a30` (0x1419e3a30) builds the 128×128 leg target and calls
`FUN_1419e2c10(..., mode = 1, pad = 26.0f, W = H = 0x80, rw = rh = 1.0f, x0 = y0 = 0)`
(all five constants read from the call site at 0x1419e3b9x: `0x80,0x80,1,DAT_14259857c`,
where `0x14259857c = 41d00000 = 26.0f`; `DAT_14256ae1c = 1.0f` for rw/rh).

So, exactly:
```
canvas  : 128 × 128, cleared 0x00FFFFFF
pad'    = 26 / 1.0 = 26
per digit d (cells from §1, vertical layout):
  x  = (pad/2)/W * rw            = 13/128      → 13 px
  y  = (base + adj/2) * rh + y0  where base = 0.25 (1 digit) | oT/oT/oH tables (2–3 digits)
  w  = (H − pad)/W * rw          = 102/128     → 102 px
  h  = (0.5 − adj) * rh          (1 digit)  |  (0.5 − adj)*rh (2 digits, per digit)
                                    |  (1/3 − adj)*rh (3 digits, per digit)
  uv = FUN_1419c0290(..., mode 1)  →  U = fy, V = 1 − fx
```
With `y` in *fraction of the region* terms: one digit sits at `y = 0.25 + adj/2`, i.e. 32 px
down on the 128 canvas, 64 px tall — the digit occupies the whole height, centred.

**Direction (the `ponytail` item):** the leg number is rotated **90° clockwise** — the glyph's
top points to the right (+x). In quad terms: strip-u runs down the quad's **x** axis
(`U = fy`), strip-v runs right-to-left along the quad's **y** axis (`V = 1 − fx`). The
`Blit(..., +M_PI/2)` in `src/gdext/pes_marking.cpp` produces the mirror image
(`V = +fx`, i.e. 90° counter-clockwise); the correct call is a −90° rotation, or equivalently
`angle = +M_PI/2` with the u axis mirrored. Because the glyphs in a vertical strip are
pre-rotated, "90° clockwise in the quad" is what makes them upright — see `leg3_*.png`.

Scaling: `w = (H − pad)/W` = 102 px wide for a 128-px strip → the strip is *down-sampled*
0.797× horizontally; `h = 0.5` of the 128 canvas = 64 px for a 204.8-px cell → 0.3125×
vertically. The strip is anisotropic-squashed onto the quad; the CPU rasteriser must not
"correct" this.

## 5. Shirt-marking regions — OBS `FUN_1419e3c60` (0x1419e3c60)

Canvas: `0x640 × 0x100` (1600×256), clear `0x00FFFFFF`, two-pass blend, codec/format path as
documented. The four draws, with the exact register values read at each call site:

| region | callee | x0 | y0 | rw | rh | pad | mode | W×H |
|---|---|---|---|---|---|---|---|---|
| name | `FUN_1419e1df0` | 0 | 0 | `0x142a96c10` = 0.64f | `0x14256ae1c` = 1.0f | — | — | 1600×256 |
| back | `FUN_1419e14b0` → `FUN_1419e2c10` | **0.64f** | 0 | `0x1425c4bf4` = 0.24f | 1.0f | 0 | 0 | 1600×256 |
| chest | `FUN_1419e2c10` | `0x1425ddf70` = 0.88f | 0 | `0x1425cb294` = 0.12f | `0x142565600` = 0.5f | `0x14259857c` = 26.0f | 0 | 1600×256 |
| badge | `FUN_1419e1610` | `0x1425ddf70` = 0.88f | `0x142565600` = 0.5f | `0x14259bdc8` = 0.08f | `0x142565600` = 0.5f | — | — | 1600×256 |

**§2.3 of the report is confirmed verbatim** — I initially mis-read Ghidra's shuffled parameter
names and wrote a correction above; re-deriving the stack slots at each call site
(`FUN_1419e14b0` forwards `x0=0.64, y0=0, rw=0.24, rh=1.0`; the chest pushes `x0=0.88, y0=0,
rw=0.12, rh=0.5, pad=26`; the leg pushes `x0=0, y0=0, rw=1, rh=1, W=H=128, mode=1, pad=26`)
puts every value back on the documented one. The back-number region really is
`(0.64, 0, 0.24, 1.0)`.

- The name's region is `x0 = 0, y0 = 0, rw = 0.64f, rh = 1.0f` — `DAT_142a96c10 = 0.64f`
  and `DAT_14256ae1c = 1.0f`, both OBS at the `FUN_1419e1df0` call (0x1419e3ef2).
- The badge is drawn by `FUN_1419e1610` into `(0.88, 0.5, 0.08, 0.5)` — same x as the chest
  number but the **lower half** of the RT, below the chest number; blend mode 6
  (`FUN_1416f0890(...,6)`) and a full-texture quad (`FUN_1419c0290(..., 0)`, identity
  permutation), src rect = the whole texture (`{0,0,1,1}`). This matches §3.4 of the report;
  the doc's `(0.88, 0.5, 0.08, 0.5)` is correct and my first pass at the region table had the
  y0 wrong — corrected here.
- Two-pass: `FUN_1419e2c10`'s `param_2` comes from `(kitFlags >> 1) & 1`; when set the loop runs
  twice with blend **mode 4 then mode 2**; otherwise one pass with mode 2. OBS at
  0x1419e2dc0 (`mov $2,%edx / mov $4,%edx`) and the `local_1a0`/`local_1a4` loop bounds.

---

## 6. Name layout — OBS `FUN_1419e1df0` (0x1419e1df0, 0x4D1E bytes)

Proven by raw disassembly plus a synthetic render against the decoded `u0001p1_name.ftex`
(`name_flat3.png`, `name_curve_A.png` in `<scratch>/MarkingPixelsRE/` — examined; the flat
render reads left-to-right `ABCDEF` and the curved render is an upward arch with each glyph
rotated along the arc).

### 6.1 Glyph table lookup — OBS `FUN_1419e4940` (0x1419e4940)

78 entries × 16 B at `DAT_142a96ce0`, linear search, entry `{i32 code, i32 page, u16 x,
u16 y, u16 w, u16 h}`:

```
hit  : page1 = (page == 1);  denom = page1 ? 512.0f : 256.0f;
       out.x = (float)x  * 0x1425f2120;   // 0.0009765625f = 1/1024
       out.y = (float)y  / denom;
       out.w = (float)w  * 0x1425f2120;   // /1024
       out.h = (float)h  / denom;
miss : all four outputs 0.0f              // invisible glyph, width 0
```

All 78 heights are 124 px (OBS from the table bytes); 29 entries are page 0, 49 page 1.

### 6.2 Width, scale, cursor — OBS (0x1419e1f62–0x1419e1fcc)

```
N       = sum(w_i) / 2                    // uVar2 = FUN_1419e4a20, integer sum
gap     = 0
if (NameShape != 0 && N < 0x200 && glyphCount > 1) { N += 6*(glyphCount-1); gap = 6; }
scale   = 1.0f;  if (512.0f/(float)N <= 1.0f) scale = 512.0f/(float)N;
if (N > 0x200) N = 0x200;
cursor  = (float)(N >> 1) + 256.0f;       // integer shift!
```

### 6.3 Per-glyph loop (right to left) — OBS

```
gw     = w_i * scale / 2                  // 0x1419e2125: w*scale, then *512, then *0.5
gh     = 62 * 0.98                        // h*128*0.98 (page-1 h doubles)
centre = cursor - gw/2
cursor = centre - gap - gw/2              // 0x1419e2afe
```

(`0.98` is `0x1425eee40`; the 62 is the glyph height in 512-space units.)

### 6.4 Placement — OBS

```
NameShape == 0 (flat):
  quad = (centre, 64.0, gw, gh)                    // 0x1419e296b: y = DAT_142a96c20 = 64.0
NameShape 1..3 (curved):
  R   = { 672.0f, 336.0f, 292.0f }[NameShape]      // 0x142a96c18, 0x1425c9070, 0x142a96c14
  th  = (centre - 256.0f) / R
  x   = 256.0f + 0.8f*R*sin th                     // 0.8f = 0x142572348
  y   = 0.89f*R - 0.8f*R*cos th                    // 0.89f = 0x1425c489c (0.09 + 0.8)
  quad = (x, y, gw*scale*0.95f, gh) rotated by th  // 0.95f = 0x1425ab124
```

### 6.5 Vertex emission — OBS (0x1419e29d0–0x1419e2a6e)

Four vertices at stride 0x1c. The local quad is emitted **mirrored in x** (local x = −gw/2
takes src u+w, +gw/2 takes src u) and **not** mirrored in y. The canvas write then flips y:

```
Y_rt = (128 - Y_name) * 2      // 0x1419e2a61: 128.0 - y, then *(1/128)*(H*rh)
X_rt = X_name * 2              // *(1/512)*(1600*0.64)
```

So in the finished texture the name reads left-to-right and is vertically flipped relative
to the source page — which is why the shirt shader's name sampling has a positive-y scale
while the back number's is negative (shader report §3.1). A CPU rasteriser applies the
`Y = 128 − y` flip when writing the canvas and must **not** mirror the source y again.

## 7. Divergences from `src/gdext/pes_marking.cpp` / `src/pes/marking.cpp`

Ordered by visual impact.

1. **Leg-number rotation direction — the `ponytail` comment is wrong** (this was the
   acknowledged approximation; it resolves against it).
   Current: `Blit(..., angle = +M_PI/2, mirror = false)` at `pes_marking.cpp:98`.
   PES uses quad **mode 1**, whose map is `U = fy, V = 1 − fx` (§3): strip-u runs down the
   quad's **x** axis, strip-v runs right-to-left along **y**. Because the vertical strip's
   glyphs are pre-rotated clockwise, the net leg-number rotation is **90° clockwise**
   (glyph top → +x). Fix: rotate −90° (or keep +90° and mirror the source in u).
   Renders: `leg3_10.png` (correct) vs `leg4.png` (current behaviour — mirrored digits).
   The current code also passes `hh` (the *vertical* stretch `(dh−pad)/dh*rh`) as the quad's
   **width** and `d.w*rh` as its **height** — width and height are swapped. PES's mode-1
   quad is `w = (H−pad)/W*rw` (the 102-px axis) and `h = (0.5−adj)*rh` (the 64-px axis).

2. **Leg-number y origin.** Current: `cy = ry + y0 + hh/2` (`pes_marking.cpp:97`).
   PES: `y = base + adj/2` with base = 0.25 for one digit — the quad's **top**, then `h`
   tall. One digit therefore spans rows 32..96 of 128, not 13..115.

3. **Name folding applied when `_name_ex` exists.** `ComposeMarking` always calls
   `pes::NameCodes` (`pes_marking.cpp:142`). The exe folds through `info_name_font.bin` only
   when the kit has **no** `_name_ex` (`FUN_1419e4760`, `param_4 == 0`); with `_name_ex`
   present the raw codes are used and page-1 glyphs resolve. 29 kits ship `_name_ex` (OBS VFS
   census, §8). For those kits the compositor folds accents to ASCII and drops the diacritics
   PES would draw. Fix: pass `page1.ok()` into the fold decision.

4. **Two-pass blend not reproduced.** The exe draws each number twice (mode 4 then mode 2,
   §2.7); the compositor draws once. Cosmetic only (same geometry, opaque destination) —
   noted, not a fix.

Checked and found **correct** (no divergence): digit-cell arithmetic and the `vertical` test;
the one/two/three-digit offset tables and `f = 0.08(s−1)` (computed as
`((s*0.16)−0.16)*0.5`); `pad ← pad/rh`; the integer `W/4`, `W/2` shifts vs the float `W/3`
divisor; the name's `N`, `gap`, `scale`, `cursor` and per-glyph `gw/gh`; the `Y = 128 − y`
canvas flip and the x-mirror; the missing-glyph → width-0 rule; the four region rectangles;
the badge's blend mode 6 and full-texture quad.

## 8. Static cross-checks

**Which kits ship which strips** (OBS, `pesfmt_dump ls` over the whole
`Asset/model/character/uniform/texture/#windx11` prefix, 1672 kit prefixes):

- `_back` 1671, `_chest` 1431, `_leg` 1593, `_name` 1670, `_name_ex` 29.
- 241 kits have no `_chest`, 79 no `_leg`, 2 no `_name`, 1 no `_back` — e.g. `u0758p4` has no
  back strip, `u0704p*`/`u0705p*` no chest, `u0714p*`/`u0707p1` no leg, `u0756p2/p3` no name.
  "File absent" must mean "draw nothing", not "draw blank": `Decode` returns a null `Rgba`
  and both `DrawNumber` and the name path already bail, so this is consistent.
- Horizontal strips (`w > h`); 4cc packs are vertical (`_back` 128×2048, `_chest`/`_leg`
  64×1024). `DigitCell(d, vertical)` keys off the same `w < h` test the exe uses, so a kit
  mixing layouts renders correctly.
- **Missing glyph** → the lookup returns zeros and the glyph contributes width 0 (OBS
  `FUN_1419e4940` miss path). Repo matches.

**Vanilla edit-kit fonts are blank** (OBS prior pass, dt32 `uniform/edit/font/nb%02da`,
`nb%02db`, `name%02d`: RGB 255, alpha 0) — an edit kit's marking texture is created but
empty, exactly as the compositor produces it.

## 9. Remaining marking kinds — status

- **Captain armband** — open; code path verified previously, geometry not re-derived here.
  Prior pass's claim stands OBS: the default armband is the kit-atlas panel
  `u ∈ [0,0.25], v ∈ [0,0.0625]` drawn by the `sleeve_*_cap_*` sleeve variants (visible only
  for the captain, `unit+0xD4`, `FUN_141a85a00`), and the `CL_captainmark_%02d` textures
  apply only to competition `0x21` via `cap_sleeve_*` (part slot `cap`, category 5). Not
  re-derived this pass (needs the sleeve binder `FUN_141a85770` and the `cap_sleeve_*.fmdl`
  UV dump). No compositor impact: it is a texture on a separate part, not the marking RT.
- **Badges** — partly closed. The chest-badge draw in the marking RT is now exact
  (`FUN_1419e1610`: region `(0.88, 0, 0.08, 0.5)`, blend 6, identity permutation,
  full-texture src). Sleeve badges (`FUN_141a874a0`, `Badge_Tex_LIN` from a per-kit
  composite, slots 0–3) and the badge composite builder (`BadgeLoadJob`) and the
  `FUN_1419a2a60` matrix maths remain open — same open item as the prior pass.
- **Goalkeeper kits** — closed as "identical to outfield for markings" (OBS prior pass):
  `g`-prefixed kit with its own `_back/_chest/_leg/_name`; only the armband colour and sleeve
  limits differ. The compositor needs no GK branch.
- **Referee / linesman markings** — closed as "none" (OBS prior pass, live): referees get no
  marking RT (`unit+0x1A8 != 0` skips the jobs) and their torso is `referee_collar_%03d`.
  Linesmen are the same character kind. The compositor must simply not build markings for
  referee units — same rule as the exe.
- **Sleeve/thigh badges** — the sleeve badges are the badge composite (open above); PES draws
  no extra thigh marking beyond the leg number.

## 10. Open items

1. The exact **matrix order** for the curved name — the reduced form in §6.4 is OBS for every
   constant and matches the render, but the composite rotation/translation rows were reduced
   with identity bases (INF, high). Closing it needs a live capture or a pixel-diff against a
   screenshot; the current form is visually correct in the render.
2. `BadgeLoadJob` / `FUN_1419a2a60` (sleeve-badge composite and matrices).
3. Fox format `0x4002` → DXGI mapping (INF high: BC3 from the byte size).

## 11. Reproduction

```
# decompile (works with a renamed script copy; the stock DecompAt.java fails to load)
/opt/ghidra/support/analyzeHeadless $HOME/ghidra-projects/scratch/main PES21 \
  -process pesmem.bin -noanalysis -readOnly -scriptPath <dir> -postScript DM2.java <ABSOUT> \
  0x1419e2c10-0x1419e3a21 0x1419e1df0-0x1419e2c0d 0x1419e3a30-0x1419e3c51 \
  0x1419e3c60-0x1419e4145 0x1419c0290-0x1419c03cf 0x1419e4a90-0x1419e4b68
# raw slices for objdump: image offset = VA - 0x140000000
objdump -D -b binary -m i386:x86-64 --adjust-vma=<VA> <slice>
```

Renders kept in `<scratch>/MarkingPixelsRE/`: `leg3_10.png`, `leg3_23.png`, `leg4.png`,
`name_flat3.png`, `name_curve_A.png`, `shirt_full.png`, `back_10.png`.
