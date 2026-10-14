# PES 2021: how a header is aimed — the kick-category byte, the clip selector, and the AI's (non-)decision

Agent: HeaderRE. Read-only RE. Scratch: the HeaderRE scratch dir (`out/*.c` = headless decompiles,
`out/*.s` = capstone disassembly dumps, `out/FUN_140829e80.s`, `out/sel_full.s`).

Tags: **OBS** = read in raw bytes / decompile re-checked against disassembly at the named VA;
**INF(conf)** = inferred. VAs are PES2021.exe, base `0x140000000`. Read the companion reports first:
`pes-re-contact.md` §3 (aerial duels, header lift, header quality), `pes-re-kick-launch.md`
(how a kick request becomes a ball launch), `pes-re-carrier-ai.md` §4–5 (shot/pass target choice).

---

## 0. Headline — three answers

1. **`ap+0x2754` is not a "kick category" in the abstract — it is the ball's height band index.**
   The writer is **`FUN_1407253d0`** (= `match::anime::action::Kick` vtable slot 10, vtable
   `0x1425e2380`+0x50 = `0x1425e23d0`); the store is at **`0x140726014`**
   (`mov byte ptr [r14+0x2754], cl`). The value is the **ball-height band** produced by
   `FUN_140a6d380` (thresholds at `0x142648668`…`0x142648694`), clamped to ≤ 11.
   This closes the open item "`pes-re-contact.md` §8.2: which category is the jumping header"
   as **Confirmed, and it re-labels the field**: *category 9 = height band 9 = ballY ∈
   (2.135, 2.36] × `ap+0x3b18`*. A jump header is not chosen by a flag; it is forced by geometry.

2. **PES has no AI "header" decision at all** (OBS). The AI answers *shoot / pass / clear* with a
   target point and a speed, exactly as for a foot kick. The header appears **downstream**, in the
   clip selector, because the (clip kind, height band, side) table has no foot clip in the upper
   bands. So "header at goal / header pass / clearance header" are the ordinary Shoot / Pass /
   Clear answers rendered with a header clip. **INF(hi)** — the AI side is one hop away from the
   anime command record and I could not find any header flag in it (§3, open item).

3. **Target point and power are unchanged by heading** (OBS): the header consumes the same
   aim-direction vector and the same speed as a foot kick; the only header-specific term is the
   lift `L` of `pes-re-contact.md` §3.2 and the `q(Heading)` quality of §3.3.

---

## 1. The writer of `ap+0x2754` — closed

### 1.1 The store (OBS)

`FUN_1407253d0` body spans `[0x1407253d0, 0x14072649d]` in three chained unwind chunks
(`.pdata`); Ghidra splits it into `FUN_1407253d0` + the continuation `FUN_1407254ef`, which is why
the target has **no direct `call`/`jmp` and no `.rdata` pointer** — it is reached through the
Kick vtable.

Kick vtable dump (OBS, `.rdata` at `0x1425e2380`):

| slot | VA | function |
|---|---|---|
| +0x10 | `0x140719bb0` | |
| +0x18 | `0x140717c50` | **the launch** (`FUN_1408397a0`, `pes-re-kick-launch.md`) |
| +0x30 | `0x140717a60` | action start → `FUN_140727230` |
| **+0x50** | **`0x1407253d0`** | **the writer (this report)** |
| +0x58 | `0x140727d90` | |
| +0x78 | `0x1407d6910` | |

Raw (OBS) at the tail of the store block:

```
0x140725fec  movzx ecx, word ptr [rbp + 0x110]   ; descriptor +0  (clip id, low word)
0x140725ffe  mov    word ptr [r14 + 0x2752], cx ; → ap+0x2752
0x14072600d  movzx ecx, byte ptr [rbp + 0x114]   ; descriptor +4  (the band)
0x140726014  mov    byte  ptr [r14 + 0x2754], cl ; → ap+0x2754      ← THE WRITER
0x14072601b  movzx ecx, byte ptr [rbp + 0x118]   ; descriptor +8  (tuned category)
0x140726022  mov    byte  ptr [r14 + 0x2755], cl ; → ap+0x2755
0x140726029  movzx ecx, byte ptr [rbp + 0x11c]   ; descriptor +0xc
0x140726030  mov    byte  ptr [r14 + 0x2756], cl ; → ap+0x2756
0x140726037  movss  dword ptr [r14 + 0x2758], xmm0 ; → ap+0x2758 (float)
```

The descriptor lives at `[rbp+0x110]` and is the out-parameter of the kind selector
`FUN_1408364d0(&desc, ap, request)` (`0x140725e72`, `0x140725f57`).

### 1.2 Where the descriptor bytes come from (OBS)

`FUN_1408364d0` (`[0x1408364d0, 0x140836791]`) is a thin wrapper: it builds a candidate struct
(`FUN_140820d80` initialises it; its fields run to `+0x104`) and copies it out with
`FUN_140821170` (`0x14083677a`, `0x14083713e`) — a field-by-field
copy, so the descriptor layout is exactly the candidate struct's.

Template initialisation (OBS, `hdr.c:764-783`, values read from `.rdata`):

| descriptor offset | init | `.rdata` source | meaning |
|---|---|---|---|
| +0x00 (u32) | `0x97` | `0x1425ef880` | **clip id; `0x97` = 151 = "none"** |
| +0x04 (u32) | `0x0c` | `0x1425ef884` | band; `0x0c` = 12 = **"no band / none"** sentinel |
| +0x08 (u32) | `0x12` | `0x1425ef888` | tuned category default |
| +0x0c (u32) | `0x02` | `0x1425ef88c` | |
| +0x10 (byte) | `0` | | |
| +0x14 (u32) | `0x18` (=1.0f) | `0x1425ef890` | |
| +0x18 (u32) | `7` | `0x1425ef894` | |
| +0x20 (u32) | `0x97` | `0x1425ef898` | |
| +0x28 (f32) | `2500.0` | `0x1425c65f8` | |

The real writers are in the clip selector `FUN_140829e80` (OBS raw, `0x14082c04f`…`0x14082c063`
and the mirror at `0x14082c257`…`0x14082c26b`):

```
mov eax, dword ptr [rbp - 0x54]     ; the *tuned* category (CATSCAN out-param)
mov dword ptr [rbx + 8], eax        ; descriptor +8  → ap+0x2755
mov dword ptr [rbx + 4], r13d       ; the *requested* band → ap+0x2754
mov eax, dword ptr [rdi + 0x2c]     ; request +0x2c
mov dword ptr [rbx + 0xc], eax      ; descriptor +0xc → ap+0x2756
```

`r13d` is the return value of **`FUN_140838ca0`** (`0x14082b368` → `0x14082b371 mov r13d, eax`).

### 1.3 What the band actually is (OBS)

`FUN_140838ca0` (`[0x140838ca0, 0x140838dbc]`) is called with `r8d = [rsp+0x74]`, and
`FUN_140829e80` sets that slot at **`0x14082a90f`**:

```
0x14082a8e2  movsd  xmm0, qword ptr [rbx]        ; rbx = predicted ball position
0x14082a8e9  mov    rbx, qword ptr [rdi + 0x110] ; rbx = the AnimePlayer (request+0x110)
0x14082a8fb  movss  xmm6, dword ptr [rbp + 0x1b4] ; = ball .y  (height)
0x14082a903  movaps xmm1, xmm6
0x14082a90a  call   0x1406e69f0                  ; band(ballY, ap+0x3b18)
0x14082a90f  mov    dword ptr [rsp + 0x74], eax  ; the band
0x14082a913  cmp    eax, 0xb
0x14082a916  jl     0x14082a9a5
   ...  if the clip is a header kind (FUN_1407245d0 / FUN_1407247d0 / FUN_14071e520 == 6):
0x14082a94c  mov    dword ptr [rsp + 0x74], 0xb   ; clamp to 11
```

`FUN_1406e69f0` (`0x1406e69f0`) is a one-line wrapper: `FUN_140a6d380(height, *(float*)(ap+0x3b18))`
— the **physique scale** as the divisor. `FUN_140a6d380` (`0x140a6d380`, the only call site is
`0x1406e6a11`) is a linear band search (OBS raw `0x140a6d393`…`0x140a6d3f9`):

```
band = 0;  for (i = 0; i < 11; i++)  if (h <= scale * T[i]) return band++, else band += 1
```

`.rdata` thresholds (OBS, `T = 0x14264866c`, compared from `[rcx-4]`):

| VA | value | band index returned when `h/scale ≤` this |
|---|---|---|
| `0x142648668` | 0.15 | 0 |
| `0x14264866c` | 0.335 | 1 |
| `0x142648670` | 0.56 | 2 |
| `0x142648674` | 0.785 | 3 |
| `0x142648678` | 1.01 | 4 |
| `0x14264867c` | 1.235 | 5 |
| `0x142648680` | 1.46 | 6 |
| `0x142648684` | 1.685 | 7 |
| `0x142648688` | 1.91 | 8 |
| `0x14264868c` | **2.135** | **9** |
| `0x142648690` | 2.36 | 10 |
| `0x142648694` | 2.585 | 11 |
| | | >2.585 → 11 (clamped) |

**So (OBS, arithmetic):**
```
ap+0x2754 = 9  ⇔  2.135·(ap+0x3b18) < ballY ≤ 2.36·(ap+0x3b18)
ap+0x2754 = 7  ⇔  1.685·scale      < ballY ≤ 1.91·scale
ap+0x2754 = 0xc (12)               ⇔  no clip found
```

For a 1.80 m player (`ap+0x3b18` ≈ 1.0): band 9 = **2.14–2.36 m** — exactly the jumping-header
window, and it sits inside the jump reach of `pes-re-contact.md` §3.1
(`2.08 m` at Jump 40 … `2.52 m` at Jump 99). Band 7 = 1.69–1.91 m, i.e. a standing header.
**This is why the lift formula of §3.2 only reads `ap+0x2754 == 9`: the lift is applied exactly
when the ball is in the band where only a jump can reach it.**

**Downgrade/upgrades of `pes-re-contact.md` §3.2:** the doc's "category 9 (INF: jumping header)"
is **Confirmed** and re-identified as the height band; its "header kind, category 7" is
**Confirmed** as band 7. The doc's reading of the field as an abstract *kick class* should be
replaced.

### 1.4 The readers (OBS)

`FUN_140727230` (`0x140727230`, the Kick action start; call sites `0x140717a82`, `0x14071ad27`,
`0x1407262e3`) reads them straight into the lift call:

```
0x140727…  iStack_154 = (int)*(char  *)(param_1 + 0x2754);   // the band
           iVar20     = (int)*(short *)(param_1 + 0x2752);   // the clip id
           iStack_158 = (int)*(char  *)(param_1 + 0x2756);
           …
           fVar23 = FUN_14071c910(uVar6 /*clip*/, iVar20 /*kind*/, iStack_154 /*band*/,
                                  iStack_158, ballY, clipHitY, *(float*)(ap+0x3b18),
                                  FUN_1406e6660(ap,0x2c) /*Jump*/, …);
```

`FUN_14071c910` (`0x14071c910`) then, OBS:

```
if (param_5 <= param_6) { if (param_3 < 8) return 0; }          // ball not above the clip
else {
  if (param_3 == 9) {                                          // ← JUMPING HEADER
      uVar3 = FUN_140a6dfd0(param_9, param_10, param_4 == 1);  // reach − H/100
      fVar5 = (uVar3.lo − 0.335) + (1.0 − param_7)·0.44369;    // L
  }
  if (param_2 < 0x3e && (0x3600440000000000 >> param_2 & 1) && param_3 > 1)
      fVar5 = 0.2174·max(1,scale);                              // two ball radii
  if (FUN_1407245d0(param_2) && param_3 == 7)                   // ← STANDING HEADER
      fVar5 = (param_2-10 < 4 || param_2-0x79 < 0xe) ? 0.0543·max(1,scale)
                                                   : 0.1087·max(1,scale);
}
```

`FUN_1407245d0` (`0x1407245d0`) is the **header-kind predicate**: it indexes the clip table at
`0x14344e380 + kind*0x10` and returns `byte(kind*0x10 + 0x14344e38c) < 2` (OBS raw: the `>> 0x20`
of the 8-byte load at `kind*0x10+0x14344e388` is byte `+0xc`). Dumping the 0x97-entry table gives
the **28 header kinds: 1–27 and 149** — exactly the set `pes-re-contact.md` §3.2 reports.

---

## 2. The clip selector — how the band becomes a header clip

This is the mechanism the AI does not control. Chain (OBS):

```
Kick vtable +0x50  FUN_1407253d0  (this report §1)
  └ FUN_1408364d0(&descriptor, ap, request)             0x140725e72 / 0x140725f57
       switch (*request)  == 8 → FUN_1408360f0   (long pass)
                          == 9 → FUN_140837670  (short pass)
                          ==10 → FUN_1408371a0  (shoot)
       each of those calls FUN_140829e80(clipKind, request[0x40], request, &descriptor, …)
         (≈250 call sites, 0x14082170b … 0x14082fd78)
         ├ computes the band            §1.3
         ├ FUN_140838ca0  band scan      0x14082b368
         │    for uVar25 in [band-2, band+2]:
         │        clip = FUN_14071e6f0(ap, *request, clipKind, uVar25, …)
         │        → FUN_1406abbb0(kind, category, side, angle)  → clip id or 0xbe5 ("none")
         │    on success *param_4 = tuned category; return the band
         ├ FUN_140833b60  fallback band scan 11→0 (0x14082b38b), same lookup
         ├ FUN_140821e00  accept/reject gate (0x14082b41c) — angle/geometry vetoes
         └ commit the descriptor fields  §1.2
```

### 2.1 The clip table is indexed by (kind, band, side) — OBS

`FUN_1406abbb0` (`0x1406abbb0`), raw (`lookup.c:12`, `71`…`77`):

```
if ((kind-1 < 0x96) && (band < 0xc) && (side < 2))
    return *(short*)(*(longlong*)(db + 8)
                     + (side + (band + kind*0xc)*2)*2);      // 2-D table, stride 0xc per kind
else return 0xbe5;                                            // 3045 = "no such clip"
```

So the table is **`[clipKind 1..150][band 0..11][side 0..1] → short clip id`**, and `0xbe5` is the
"absent" sentinel. **OBS consequence:** in the upper bands only header clip kinds have a non-`0xbe5`
entry, so the *selector* is what forces a header — no AI flag is consulted.

Availability companion: `FUN_1406abdd0` (`0x1406abdd0`, `lookup.c:83`) tests
`clip < 0xbe5 && category < 0x12` and reads bit 15 of the same table; `FUN_1406abd60`
(`0x1406abd60`) is the byte variant.

### 2.2 The tuned category `ap+0x2755` — OBS

`FUN_140838ca0` adjusts the accepted band to a neighbouring one when a better clip exists
(raw `0x140838e9d`…`0x140838fa4`, `hdr.c:171`…`211`):

- band ∈ {9, 0xd} and `22.5° < angle < 90°` and clip for band 0xb exists → band **0xb**;
- band ∈ {9, 4} and `−67.5° < angle < −22.5°` and clip for 6 exists → band **6**;
- band ∈ {9, 6} and `|angle| ∈ (15°, 25°)` and clip for 7 exists → band **7**.

Constants: `22.5 @0x14259d618`, `90 @0x14259be5c`, `−67.5 @0x1425dbdb8`, `−22.5 @0x1425cfa50`,
`−25 @0x1425c48f8`, `−15 @0x14259d668`.

This is the "header at goal vs header elsewhere" split: the same band-9 window is redirected to a
**different header clip family** depending on the shooting angle, and the choice is a pure table
lookup.

### 2.3 The per-band rank ladder — OBS

`FUN_140837f30` (`0x140837f30`) is a fall-through `switch` that maps a band to a **priority rank**:

```
rank: 0→1, 4→2, 1→3, 6→4, 5→5, 3→6, 0xd→7, 2→8, 0xa→9, 0xb→10, 0xc→11,
      8→12, 0xe→13, 0x10→14, 0xf→15, 9→16, 7→17, 0x11→18,  default→0
```

i.e. the bands are tried from **low ball** to **high ball**: `0, 4, 1, 6, 5, 3, 0xd, 2, 0xa, 0xb,
0xc, 8, 0xe, 0x10, 0xf, 9, 7, 0x11`. The selector's working copy starts at the *worst* band:

```
0x140829fc2  mov dword ptr [rsp + 0x68], 0x11     ; rank 18 — start at the bottom
```

and is pushed down the ladder (i.e. toward lower, more foot-like bands) whenever a clip fails
(`0x14082a99c` → 8, `0x14082ac2b`/`0x14082ad80` → 0x10, `0x14082b3be` → 8, `0x14082b3d7` → 7).
**INF(hi):** the AI's kick request carries no band preference; this ladder is the *only* thing
that decides foot-vs-head, and it is a pure function of `ballY / ap+0x3b18`.

---

## 3. The AI side — where the target point and the power are

### 3.1 The AI never names a header (OBS)

The AI action-name table `0x1434a4d50` (read `pes-re-carrier-ai.md` §1's table, dumped in full)
has **no header/head action**. Entries 200–226 are a *classification* enum
(`200 NORMAL, 201 HEAD, 202 DIVINGHEAD, 203 JUMPHEAD, 204 BACKHEAD, 205 KICK, 206 VOLLEY,
207 TRAP1, 208 OVERHEAD, …, 226 NUTMEG`) written by a small state machine
`FUN_142076780` (`0x142076780`, sole caller `0x142076989` inside the same module) that *labels a
kick after the fact* (`*(uint*)((longlong)param_1 + 0x24) = 0xc9;` etc.). **OBS:** this is
bookkeeping for stats/replay, not a decision input.

### 3.2 The kick request the AI fills (OBS, fields)

The request struct handed to the selector (`param_3` of `FUN_140829e80`, `param_3` of
`FUN_1408364d0`, `unaff_RDI` of `FUN_1407254ef`) has, as read by the selector:

| offset | type | read at | meaning (INF(med)) |
|---|---|---|---|
| +0x00 | u32 | `0x14082a5cf` | kick class: 8 LONG_PASS / 9 SHORT_PASS / 10 SHOOT |
| +0x04 | u32 | `0x14082a5f4` | sub-kind (drives which clip family is offered) |
| +0x0b | f32 | `0x14082a388` | aim/quality scalar |
| +0x10 | u32 | `0x140836b61` | |
| +0x11 | byte | `0x14082b3eb` | flag |
| +0x19 / +0x1d / +0x23 / +0x2c | vec3 | `0x1408364d0` `FUN_140470d60` calls | **candidate target points** |
| +0x1e | f32 | `0x14082a5cf` | used as a *height* in several gates |
| +0x24 | f32 | `0x140837670` | |
| +0x25 | byte | `0x1408364d0` | flag |
| +0x40 | u32 | `0x1408223f0` | second selector argument |
| +0x44 / +0x110 | ptr | `0x140829efc`, `0x1408223f0` | the **AnimePlayer** |
| +0x5b / +0x5c / +0x5d | byte | `0x14082b3e0`, `0x140829ec1` | flags (one of them gates the header path) |

**INF(med):** none of these is a "play as header" flag — they are pass/shoot/clear selectors.
I could **not** find the AI-side writer of this struct (open item 1 below).

### 3.3 The aim vector and the speed (OBS)

The anime command record (`FUN_1406f9b70`, the AI-command applier, called from `0x1406e6096`)
carries the aim as two 12-byte vectors and copies them into the AnimePlayer:

```
0x1406f9b…  func_0x0001404309c0(ap + 0x2498, cmd + 0x17e)   // secondary vec3
            FUN_1406e1d60    (ap + 0x2bc8, cmd + 0x17b)      // the kick aim direction
```

`FUN_140727230` then normalises `ap+0x2bc8` into `ap+0x2bd4` (`FUN_140a577c0`) and computes the
clip's own aim from it. The **speed** is the hit record at `ap+0x48`: the launch slot
`FUN_140717c50` reads it at `param_3+0x20` and `param_3+0x28`
(`param_3 == ap+0x48`, per `FUN_140717a60`: `FUN_140727230(param_2, param_1 + 0x48, …)`) and
passes it with the direction and the target to `FUN_1408397a0` — the writer of
`pes-re-kick-launch.md` §2.

**OBS conclusion for item 2 of the ticket:** the header path passes the *same*
`ap+0x2bc8` aim vector and the *same* `ap+0x48+0x20..0x28` speed into the launch. Nothing in
`FUN_140727230`, `FUN_14071c910` or `FUN_140829e80` rewrites the target or the power for a
header. The only header-specific quantities are the lift `L` (`FUN_14071c910`) and the quality
term `kickRecord+8 + 1 − 5q(Heading)²` (`pes-re-contact.md` §3.3, `FUN_14071d770`,
`FUN_14071cde0`).

---

## 4. Answering "header at goal / header pass / clearance header" directly

| case | AI answer type (`pes-re-carrier-ai.md` §1) | target point | power | clip family chosen |
|---|---|---|---|---|
| header at goal | **1 SHOOT** (`FUN_142121920`, headerRange `FUN_140699750`) | the Shoot unit's target, unchanged | unchanged | band-9 header of the shoot family |
| header pass | **2/7 PASS** (Cross / OneTwo / Safety …) | the receiver/spacing table's record, unchanged | unchanged | band-9/7 header of the pass family |
| clearance header | **5 CLEAR** (`ThinkUnitClear`) | the forced-clear target, unchanged | unchanged | band-9/7 header of the clear family |

**INF(hi)** — this table is my reading of §3.1–3.2 plus `pes-re-carrier-ai.md` §4–5: the AI does
not branch on headers, so the three cases collapse onto the three existing AI answers. The
*selector* then chooses a header clip because the band says so, and `FUN_140838ca0`'s angle
adjustments (§2.2) pick which header family within the band.

---

## 5. Constants and VAs (all OBS)

### 5.1 Height bands (`FUN_140a6d380`, table `0x142648668`)

| VA | value | band |
|---|---|---|
| `0x142648668` | 0.15 | 0 |
| `0x14264866c` | 0.335 | 1 |
| `0x142648670` | 0.56 | 2 |
| `0x142648674` | 0.785 | 3 |
| `0x142648678` | 1.01 | 4 |
| `0x14264867c` | 1.235 | 5 |
| `0x142648680` | 1.46 | 6 |
| `0x142648684` | 1.685 | 7 |
| `0x142648688` | 1.91 | 8 |
| `0x14264868c` | **2.135** | **9 (jumping header)** |
| `0x142648690` | 2.36 | 10 |
| `0x142648694` | 2.585 | 11 |

scale divisor = `ap+0x3b18` (physique scale; `pes-re-collision.md` §2.2).
clamp to 11: `0x14082a94c`. "no clip" band 12: immediate `0x0c` at `0x14082b39b`, default in the
descriptor at `0x1425ef884`.

### 5.2 Header lift (`FUN_14071c910`, `0x14071c910`)

| VA | value | use |
|---|---|---|
| `0x1425e5ac4` | 0.335 | subtracted from `reach − H/100` (band-9 branch) |
| `0x1425e5acc` | 0.443686 | `(1 − scale)` term |
| `0x1425a9bd4` | 0.217372 | two ball radii (default `fVar5` seed) |
| `0x1425cb288` | 0.0543430 | header kinds 10–13, 0x79–0x86 |
| `0x142598958` | 0.108686 | other header kinds |
| `0x142592228` | 0.25 | scale clamp in the mask branch |
| immediate | `0x3600440000000000` | clip kinds 42, 46, 57, 58, 60, 61 (two-ball-radius branch) |

### 5.3 Angle gates (`FUN_140838ca0`, `0x140838ca0`)

| VA | value |
|---|---|
| `0x14259d618` | 22.5 |
| `0x14259be5c` | 90.0 |
| `0x1425dbdb8` | −67.5 |
| `0x1425cfa50` | −22.5 |
| `0x1425c48f8` | −25.0 |
| `0x14259d668` | −15.0 |

### 5.4 Sentinels

| VA | value | meaning |
|---|---|---|
| `0x1425ef880` | `0x97` (151) | descriptor "no clip id" |
| `0x1425ef884` | `0x0c` (12) | descriptor "no band" |
| immediate `0xbe5` (`0x14082b37d`) | 3045 | clip lookup "absent" |
| `0x14344e380` | 0x97 × 16 B | the clip-kind table (`+0xc < 2` ⇒ header kind) |

### 5.5 Key function VAs

| VA | role |
|---|---|
| `0x1407253d0` | Kick vtable +0x50 — **the writer** (`0x140726014`) |
| `0x140727230` | Kick action start; reads `ap+0x2752/54/56`, calls the lift |
| `0x14071c910` | lift; band 9 = jump, band 7 + header kind = standing |
| `0x1407245d0` | "is this clip kind a header kind" |
| `0x1408364d0` | kick-kind selector front end (`*request` 8/9/10) |
| `0x1408360f0` / `0x140837670` / `0x1408371a0` | long-pass / short-pass / shoot front ends |
| `0x140829e80` | the clip selector proper (band + ladder + accept) |
| `0x140838ca0` | band scan (`band-2 … band+2`) + angle-based category tuning |
| `0x140833b60` | fallback band scan 11→0 |
| `0x140837f30` | band → priority rank |
| `0x140821e00` | accept/reject gate |
| `0x1406a6b…` `0x1406abbb0` | `[kind][band][side] → clip id` lookup |
| `0x1406abdd0` / `0x1406abd60` | clip availability (bit 15 / byte) |
| `0x140a6d380` / `0x1406e69f0` | the height band function and its wrapper |
| `0x140829e80` vtable | none (called ~250× directly, `0x14082170b`…`0x14082fd78`) |

---

## 6. Evidence ledger

| ID | Claim | Confidence | Evidence |
|---|---|---|---|
| EV-HDR-0001 | `ap+0x2754` is written only by `FUN_1407253d0` at `0x140726014` from descriptor+4 | **Confirmed** | raw `0x14072600d`/`0x140726014`; `.pdata` chunk split; Kick vtable `0x1425e23d0` |
| EV-HDR-0002 | descriptor+4 = `r13d` = return of `FUN_140838ca0` = the requested height band | **Confirmed** | raw `0x14082b368`, `0x14082b371`, `0x14082c05c` |
| EV-HDR-0003 | the band = `FUN_140a6d380(ballY, ap+0x3b18)`, 12 thresholds at `0x142648668`, clamped ≤ 11 | **Confirmed** | raw `0x14082a90a`/`0x14082a90f`/`0x14082a94c`; `FUN_1406e69f0`, `FUN_140a6d380` raw |
| EV-HDR-0004 | **band 9 ⇒ jumping header**; band 7 + header kind ⇒ standing header | **Confirmed** | `FUN_14071c910` `0x14071c920` region (`param_3 == 9`) and the `param_3 == 7` branch; band table arithmetic |
| EV-HDR-0005 | clip ids are looked up as `[kind 1..150][band 0..11][side 0..1]`, `0xbe5` = absent | **Confirmed** | `FUN_1406abbb0` raw `0x1406abd0…`, `lookup.c:12`/`71` |
| EV-HDR-0006 | header clip kinds = 28 (1–27, 149) = the set with table byte `+0xc < 2` | **Confirmed** | `FUN_1407245d0` raw + full 0x97-row table dump |
| EV-HDR-0007 | the selector's band ladder runs low→high, starting at band 0x11 (rank 18) | **Confirmed** | `FUN_140837f30` fall-through switch; `0x140829fc2`; stores at `0x14082a99c`/`0x14082ac2b`/`0x14082b3be` |
| EV-HDR-0008 | the AI action table contains no header action; entries 200–226 are a post-hoc classifier | **Confirmed** | full `0x1434a4d50` dump; `FUN_142076780` sole caller `0x142076989` |
| EV-HDR-0009 | the header uses the same aim vector (`ap+0x2bc8`) and the same speed (`ap+0x48+0x20`) as a foot kick | **Confirmed (code)** / INF(med) (no writer traced) | `FUN_1406f9b70` copies; `FUN_140727230`; `FUN_140717c50` reads `param_3+0x20`/`+0x28` and passes them to `FUN_1408397a0` |
| EV-HDR-0010 | the three header cases collapse onto the ordinary Shoot / Pass / Clear answers | **INF(hi)** | EV-HDR-0008 + EV-HDR-0005 (no foot clip in the upper bands) + `pes-re-carrier-ai.md` §4–5 |
| EV-HDR-0011 | `ap+0x2755` is the *tuned* category, adjustable from band 9 to 0xb/6/7 by shooting angle | **Confirmed** | `FUN_140838ca0` `hdr.c:171`–`211`; raw `0x14082c056` |

---

## 7. Open items (with the next step)

1. **The AI-side writer of the kick request struct** (the object with `+0 = 8/9/10`,
   `+0x44 = ap`). Next step: the struct is the third argument of `FUN_1407253d0`, so trace the
   Kick vtable `+0x50` call sites (`call qword ptr [reg+0x50]` where `reg` holds a Kick vtable —
   filter the 342 hits by whether `rcx` is the AnimePlayer's action interface `ap+0xab8`); the
   caller builds it. That builder should also confirm EV-HDR-0009/0010 at the source.

2. **Which request flag gates the header-only block at `0x14082b3e0`.**
   Raw: `cmp byte ptr [rdi + 0x5b], 0` / `cmp byte ptr [rsp + 0x6c], 0` / `cmp r13d, 9`. The
   `[rsp+0x6c]` byte is set at `0x14082a948` when `FUN_1407245d0(clipKind) ||
   FUN_1407247d0(clipKind) || FUN_14071e520(clipKind) == 6` — i.e. it marks "the *offered* clip is
   a header clip", not an AI choice. Next step: confirm by finding writers of request `+0x5b`
   and `+0x5c` (the latter is the early-out at `0x140829ec1`).

3. **Live check of band 9.** Not attempted this session. Next step: a read-only
   `/proc/<pid>/mem` sampler that records, per frame, `ap+0x2754`, the ball Y and `ap+0x3b18`
   for any player in a kick action, and asserts `0x2754 == FUN_140a6d380(ballY, ap+0x3b18)`
   — same shape as `duel_watch.py` in the contact report.

4. **The clip-kind ids the selector is offered** (the `0x7f / 0x79 / 0x6c / 0x84 / 0x85 / 0x27 /
   0x2e / 0x41 / …` first argument at the ~250 `FUN_140829e80` call sites, grouped per AI answer
   type). I read the sites in `FUN_1408360f0`/`0x140837670`/`0x1408371a0` but did not tabulate all
   of them. Next step: `grep 'call 0x140829e80'` over `out/*.c` and tabulate
   (AI answer type → offered clip kinds) — that table *is* the "which header for which action"
   answer.

5. **`FUN_140829e80`'s ~250 call sites** were read structurally, not exhaustively; the accept
   path was verified in raw bytes at `0x14082c04f`, but the reject reasons
   (`FUN_14082c5d0`, `FUN_140821e00`, `FUN_14082f6a0`, `FUN_140828a00`, …) were not enumerated.
   Next step: `FUN_14082c5d0` (`0x14082c5d0`) is the shared fallback veto and is the cheapest
   single function to read next.

---

## 8. What this changes in the other reports

| other report | change |
|---|---|
| `pes-re-contact.md` §3.2 | the "kick category (`ap+0x2754`)" column is **the ball-height band**; category 9 = 2.135–2.36 × `ap+0x3b18` m. Confirmed, and the field should be renamed "band". |
| `pes-re-contact.md` §8.2 | **closed** (was "the writer was not traced"). |
| `pes-re-contact.md` §8.3 (partly) | the header's target/power are **not** special: they are the AI's ordinary shoot/pass/clear target and speed. |
| `pes-re-kick-launch.md` | unchanged; the header adds no launch-side term beyond `q(Heading)` and the lift. |
| `pes-re-carrier-ai.md` §1 | worth adding: there is no header think-unit, and none is needed — `FUN_140829e80`'s `[kind][band][side]` clip table is what makes a kick a header. |