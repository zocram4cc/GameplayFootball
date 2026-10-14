# PES 2021 contact — open items 1/2/3 closed (ContactLeftoversRE, 2026-10-14)

Closes `docs/re/pes-re-contact.md` §8 items **1** (jostle result writers `ap+0xb10`), **2** (stock vs mod
`contact.o`) and **3** (kick category `ap+0x2754`, closed by HeaderRE, re-checked here). Items 3–5 of §8
(header direction/power, forced-fall second-hit branches, live displacement) are **not** in this ticket and
stay open.

Tags: **OBS** = read in raw instructions / decompile re-checked against the installed exe and the live image
dump; **INF(·)** = inferred. VA = PES2021.exe, base `0x140000000`. Scratch: the ContactLeftoversRE scratch dir
(`wesys.py` WESYS decoder, `scan.py` disp32 scanner, `out/*.c` decompiles).

---

## 1. Jostle result `ap+0xb10`: there is exactly one writer, and it is never cleared

### 1.1 Exhaustive writer scan (OBS)

ContactRE scanned `0x1406c0000–0x140a80000` for disp32 `0xb10` stores. I repeated it and widened it:

| scan | result |
|---|---|
| disp32 `0xb10`, **whole live image** (`pes_full.bin`, 0x1b97c000 bytes, 101 hits, every width, SIB and RIP forms) | **one** store to the field: `0x1406f7fda mov byte ptr [rbp+0xb10], 2` |
| disp32 `0xb10`, **whole installed `.text`** (same 101 refs, content identical) | same single store |
| neighbouring payload offsets `+0xaf0 … +0xb48` (step 8), anime module `0x1406c0000–0x140840000`, every mem-operand form | no other store anywhere near the field (only reads: `+0xb08` read `0x1406f1bce`, `+0xb38/+0xb40` float reads) |
| `lea reg,[reg+0x58]` (the `ap+0xab8`→`+0x58` addressing the doc hypothesised) in the anime module | 5 hits, all `[rsp+0x58]`/`[r15+0x58]` locals, none writing a byte |
| encoded `mov byte ptr [reg+0x58],*` image-wide (`c6`/`88` + modrm 01) | 114 hits; in the anime module only `0x1406d09a9` (a stack scratch struct, overwritten 2 instructions later by a dword from table `0x1425dd988`) and `0x140820e48`/`0x1408213af` (generic POD copy of `+0x58…+0x7c`) — none is the anime field |

**Conclusion (OBS): in this build `ap+0xb10` has exactly one write site and it stores the immediate `2`.**
The doc's open item 1 is therefore closed negatively: **values 1 and 3–7 have no writer in PES2021.exe.**

### 1.2 The reader, verbatim (OBS, raw `0x140844d3a…0x140844db7`)

```
0x140844d51 call qword ptr [rax + 0x28]      # current anime action id
0x140844d5f cmp  eax, 0x48                  # 0x48 = JOSTLE
0x140844d62 jne  0x140844dbf
0x140844d64 movzx eax, byte ptr [r15 + 0xb10]
0x140844d6c dec  al
0x140844d6e cmp  al, 6
0x140844d70 ja   0x140844dbf                 # only 1..7 pass
0x140844d8d call 0x1406ea340                # nearest opponent
0x140844d92 mov  dword ptr [rsi + 0xc], eax # cmd+0xc  = partner
0x140844d98 movsx eax, byte ptr [r15 + 0xb10]
0x140844da0 add  eax, -3
0x140844da3 cmp  eax, 1
0x140844da6 setbe cl
0x140844da9 add  ecx, 0x58
0x140844dac mov  dword ptr [rsi + 8], ecx  # cmd+8 = 0x58 STAGGER / 0x59 FALL_DOWN
0x140844daf movsx eax, byte ptr [r15 + 0xb10]
0x140844db7 mov  dword ptr [rsi + 0x10], eax# cmd+0x10 = cause byte
```

Per-value meaning, as far as the code can show:

| `ap+0xb10` | produced by | action issued (cmd+8) | cause (cmd+0x10) | confidence |
|---|---|---|---|---|
| 0 | initial state (live: 0 at all 50 sampled JOSTLE entries, contact §6) | jostle falls through to the normal contact path (§1.5/§1.6) | — | OBS |
| 1 | **no writer** | 0x58 STAGGER | 1 | OBS (action) / INF (meaning) |
| 2 | `0x1406f7fda`, when the partner is in JOSTLE, touching (`pl+0x661`), overlap > `_DAT_1425cb29c`, and the partner's published action is `'H'`(0x48) with clip id `0x8ec` (`js_run_3_2_090_tacklesholder`) or `0xced` (`tackleshoulder_3_3`) → shoulder-through | 0x58 STAGGER | 2 | OBS |
| 3 | **no writer** | **0x59 FALL_DOWN** | 3 | OBS (action) / INF (meaning) |
| 4 | **no writer** | **0x59 FALL_DOWN** | 4 | OBS (action) / INF (meaning) |
| 5 | **no writer** | 0x58 STAGGER | 5 | OBS (action) / INF (meaning) |
| 6 | **no writer** | 0x58 STAGGER | 6 | OBS (action) / INF (meaning) |
| 7 | **no writer** | 0x58 STAGGER | 7 | OBS (action) / INF (meaning) |

Note the correction to the ticket text: in the jostle branch **only 3 and 4 are falls**; 5, 6 and 7 give a
stagger, and 6/7 are *not* the "trip from behind / tackle-at-contact" falls of §1.6. The jostle field is its
own 1–7 sub-enum, not the global `cmd+0x10` cause numbering of §1.5–§1.6 (where 3, 6 and 7 are falls).

What each unreachable value would mean: INF(lo) — it is a reserved jostle-outcome enumeration whose members
were meant to select jostle-specific clip families. Supporting hint (OBS, names only): the motion tables carry
jostle-only families that no descriptor path in §4.2 selects —
`fall_lowbody_2_0_090_jostle`, `fall_lowbody_3_0_090_jostle(_v2)` (`0x142b7c2f8/0x142b7c478/0x142b7c4f8`),
`fall_upbody_3_0_090_jostle_push_lose(_v2)` (`0x142b6a5f0/0x142b6a678`),
`tacklefoot_jostle_rear_0_0_f045` (`0x142b6a998`), `head_y07_jostle_0_0_090_side_dive` (`0x142b897d8`),
`sliding_3_0_045_parallel_jostle_L_{block,kickout}` (`0x142b84f80/0x142b85008/0x142b85080`).
The two jostle-fall-shaped ones (`_2_0_090`, `_3_0_090`) are the natural occupants of 3 and 4. Nothing in the
image selects them by this field.

### 1.3 The field is never reset (OBS by absence + INF(hi) for the effect)

No store of `0` to `ap+0xb10` (nor any wider store covering it — see the `+0xaf0…+0xb48` scan) exists in the
image, and the per-action AnimePlayer reset that zeroes its neighbourhood (`0x1406eb85b mov dword ptr
[rdi+0x2820],0xf4240`, plus `+0x2813/+0x281c/+0x2824/+0x2826…+0x2838/+0x288c`) does not touch `+0xb10`.
The field is 0 from construction (live OBS) and becomes 2 for good on the first shoulder-through.

INF(hi): once a carrier has been shoulder-throughed, **every** later JOSTLE episode of that player ends in a
forced STAGGER (cause 2), because the reader never clears it. GF should either mirror the sticky behaviour or
clear it when the jostle episode ends — flagging it as a deliberate decision, not an accident.

### 1.4 What to port

Only the shoulder-through path exists: challenger in clip `js_run_3_2_090_tacklesholder` / `tackleshoulder_3_3`
while touching the carrier ⇒ carrier STAGGER immediately (bypass level/hold-up). Everything else in 1..7 is dead
code — do not implement 1, 3–7.

---

## 2. `contact.o`: stock (dt18) vs the live 4cc gameplay mod

### 2.1 Provenance (OBS)

- stock: extracted **by me** from `$PES21_ROOT/Data/dt18_all.cpk` member
  `common/match/constant/constant_player.bin` → 40 643 B, md5 `2ec1e7bece9e07eab8353ec964244525`,
  **byte-identical** to DefenderLiveRE's `stock_player.bin` (`cmp` clean).
- mod: the file the game actually loads for that path — `pesfmt_dump cat
  common/match/constant/constant_player.bin` returns one entry, 82 986 B, **byte-identical** (`cmp` clean) to
  DefenderLiveRE's `live2_player.bin`, which contact §1.4 already showed equals the live registry object.
- both are `WESYS` containers: 8-byte magic (`\xff\x10QWESYS` stock / `\x00\x10\x01WESYS` mod), `u32`,
  `u32`, zlib-deflate from offset 16; inflated layout `u32 count | count × (dataOff, size, nameOff) | names |
  payloads`. My decoder: `wesys.py` in the scratch dir. Stock = 34 objects, mod = 36 (mod adds
  `feint.o`, `ballplayerPlayImage.o`).
- runtime record = 8-byte header + payload, i.e. **runtime offset = payload offset + 8** (verified: the ten
  float/dword reads of `FUN_140842d20` are `rec+0x10,0x14,0x18,0x1c,0x20,0x24,0x40,0x44,0x48,0x4c`, which map
  exactly onto the doc's payload `+0x08…+0x44`).

### 2.2 Full field table — the complete diff

| payload off | runtime off | meaning (§1.4) | **stock dt18** | **live mod** | differs |
|---|---|---|---|---|---|
| `+0x00` | `+0x08` | **from-behind forced-fall enable (byte)** | **0** | **0** | no |
| `+0x04` | `+0x0c` | forced-fall closing speed, body hit (km/h) | 56 | **88** | **yes** |
| `+0x08` | `+0x10` | dF leg plain | 0.75 | 0.75 | no |
| `+0x0c` | `+0x14` | dF leg slide | 0.45 | 0.45 | no |
| `+0x10` | `+0x18` | dF leg tackle | 0.5 | 0.5 | no |
| `+0x14` | `+0x1c` | dS leg plain | 0.5 | 0.5 | no |
| `+0x18` | `+0x20` | dS leg slide | 0.15 | 0.15 | no |
| `+0x1c` | `+0x24` | dS leg tackle | 0.2 | 0.2 | no |
| `+0x20` | `+0x28` | forced-fall displacement #1 | 0.6 | 0.6 | no |
| `+0x24` | `+0x2c` | forced-fall displacement #2 | 0.5 | 0.5 | no |
| `+0x28` | `+0x30` | forced-fall displacement #3 (kick root-curve active) | 0.8 | 0.8 | no |
| `+0x2c` | `+0x34` | forced-fall displacement #4 | 0.8 | 0.8 | no |
| `+0x30` | `+0x38` | **vF** closing speed | 24 | **42** | **yes** |
| `+0x34` | `+0x3c` | **vS** closing speed | 16 | **36** | **yes** |
| `+0x38` | `+0x40` | dF body plain(=slide) | 0.2 | 0.2 | no |
| `+0x3c` | `+0x44` | dF body tackle | 0.04 | 0.04 | no |
| `+0x40` | `+0x48` | dS body plain(=slide) | 0.15 | 0.15 | no |
| `+0x44` | `+0x4c` | dS body tackle | 0.03 | 0.03 | no |
| `+0x48` | `+0x50` | (unread) | 0 | 0 | no |
| `+0x4c` | `+0x54` | (unread) | 0 | 0 | no |

**Exactly three fields differ: `+0x04` 56→88, `+0x30` 24→42, `+0x34` 16→36.** The other 17 are byte-identical
(dword-by-dword, both 80 B). This matches DefenderLiveRE's `diff_player.txt` independently.

### 2.3 The "from behind" switch (§8 item 6): OFF in stock too

OBS: the gate is a **signed char at runtime `+0x8` = payload `+0x00`**, `FUN_140844540`:
`FUN_141e5ca20(*(0x1437f4808),0x29) != 0 && *(char *)(rec + 8) != '\0'` (decompile `out/forcedfall.c:115`).
Payload `+0x00` is `00 00 00 00` in **both** stock and mod → **the from-behind forced fall is disabled in
stock PES 2021 as well; the 4cc mod did not enable it.** (The doc's stock table in §1.4 simply had no row for
it; the value is confirmed here.)

Payload `+0x00` is read **only** as that byte — no code path reads it as a float (the ten reads of §2.1 skip
runtime `+0x08`). INF(med): a mod can only switch the rule on by writing a non-zero *byte* there; writing the
float `1.0` (`3f 80 00 00`, little-endian low byte `00`) would leave the gate shut. GF should not implement
the rule at all.

---

## 3. Kick category `ap+0x2754` (§8 item 2) — closed by HeaderRE

Writer: `FUN_1407253d0` (Kick action vtable `0x1425e2380` slot 10), store at **`0x140726014`
`mov byte ptr [r14+0x2754], cl`**, value = descriptor byte `+4` of the descriptor built by `FUN_1408364d0`
(`[rbp+0x114]` of the `[rbp+0x110]` block), filled at `0x14082c05c` from `FUN_140838ca0`'s return.

The value is **not a kick class** — it is the **ball-height band index 0…11** from `FUN_140a6d380`
(table `0x14264866c`: `0.335 / 0.56 / 0.785 / 1.01 / 1.235 / 1.46 / 1.685 / 1.91 / 2.135 / 2.36 / 2.585 ×
ap+0x3b18 scale`), clamped to ≤ 11 at `0x14082a94c`. Full write-up: `local://pes-re-header.md` (HeaderRE).

**Consequence for `pes-re-contact.md` §3.2** (the doc's own INF(med) "category 9 = the jumping-header family" is
wrong): category 9 means *the ball is between 2.135 m and 2.36 m × body scale*, category 7 means
*(1.685, 1.91] × scale*. The jump-reach lift `L = reach − H/100 − 0.335 + (1 − max(1,scale))·0.4437` is
therefore selected by **how high the ball is**, not by which header clip was chosen — a category-9 ball can
arrive at a standing header too. Reading the lift table as a clip taxonomy is the one correction this report
makes to the contact doc.

---

## 4. Corrections to `docs/re/pes-re-contact.md` (for Main to merge)

1. §8 item 1 → closed: **no writer exists for `ap+0xb10` ∈ {1,3,4,5,6,7}**; only `= 2` (shoulder-through,
   `0x1406f7fda`). The jostle branch's fall set is `{3,4}` only.
2. §8 item 2 → closed: category = ball-height band (`local://pes-re-header.md`), §3.2's "jumping header family"
   reading is retracted.
3. §8 item 6 → closed: `contact.o` payload `+0x00` = 0 in **stock dt18** as well; the from-behind forced fall is
   off in retail PES 2021.
4. §1.4 table gains two rows: payload `+0x00` (byte, 0/0) and the three mod deltas `+0x04 / +0x30 / +0x34`.
5. §2.2 point 2 gains: the value is **sticky** (never cleared) — a second shoulder-through is not needed, the
   first one is permanent for the AnimePlayer's lifetime.

## 5. Evidence rows (append to the contact ledger)

| ID | Claim | Confidence | Evidence |
|---|---|---|---|
| EV-CONTACT-0016 | `ap+0xb10` has exactly one writer (`mov byte [rbp+0xb10],2`, `0x1406f7fda`); values 1,3–7 are unreachable in this build | Confirmed (static) | disp32 `0xb10` scan of the whole live image **and** the whole installed `.text` (101 refs each, identical), `+0xaf0…+0xb48` mem-operand scan, `lea +0x58` and `c6/88 …+0x58` scans in the anime module |
| EV-CONTACT-0017 | jostle branch: `b10 ∈ 1..7` ⇒ action `0x58 + (b10 ∈ {3,4})`, cause = `b10` | Confirmed | raw `0x140844d5f..0x140844db7` |
| EV-CONTACT-0018 | `contact.o` stock(dt18) vs live mod: only `+0x04` 56→88, `+0x30` 24→42, `+0x34` 16→36 differ | Confirmed | `wesys.py` on both WESYS containers; stock extracted from `Data/dt18_all.cpk` (md5 `2ec1e7be…`, identical to DefenderLiveRE's file) |
| EV-CONTACT-0019 | from-behind forced fall gated by signed char at runtime `+0x8` = payload `+0x00`, value 0 in stock **and** mod | Confirmed | `FUN_140844540` decompile line 115; byte dump of both payloads |
| EV-CONTACT-0020 | `ap+0x2754` = ball-height band 0…11, not a kick class | Confirmed | HeaderRE, `0x140726014`, `FUN_140a6d380`, `0x14264866c` |

## 6. Still open (not in this ticket, next step named)

- contact §8 item 3 (header direction/power request path + per-kind spin/tilt rows): next step — decompile
  `FUN_14071cde0`'s caller chain from the receive selectors.
- contact §8 item 4 (remaining `FUN_140844540` second-hit branches): next step — walk the branch table from
  `out/forcedfall.c`.
- contact §8 item 5 (live check of the level formula): needs bone world positions per player per tick; next
  step — extend the contact sampler to read `FUN_141eb51a0` outputs for the two feet + tracked parts.