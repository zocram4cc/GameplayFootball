# PES RE leftovers 2 — OfficialsInjuryRE (2026-10-14)

Two narrow open items from `docs/re/pes-re-leftovers.md`:
1. How the match officials' ids are picked (leftovers §1 / officials §4.3).
2. What reads `constant::Injury` (`injury.o`, leftovers §4).

Tags: **OBS** = observed (decompile/disasm/raw/live); **INF(conf)** = inferred (confidence stated).
Static image `pes_full.bin` base 0x140000000; decompiles via `dec.sh` into `out/`; scratch the OfficialsInjuryRE scratch dir.

---

## Session 2 — OfficialsInjury2RE (2026-10-14, continuation)

The predecessor ran ~4 h; its findings were only in its transcript. This section recovers them, adds what closed
during the continuation, and records the rejected candidates. **Neither item is fully closed.** Item 2's *access
path* is now definitively excluded, which retires the approach named in the original brief.

---

## 1. How the match officials are picked — still OPEN (chain fully mapped, pick-rule writer not found)

### 1.1 The lazy-proxy bundle chain (OBS, all decompiled)

* **OBS (`FUN_141b55730`, `out/bundle.c`)** — this is **not** a file deserialiser. It is a generic
  *two-string-keyed registry access*:
  * slot `+0x70` = `lookup(key1, key2)`; slot `+0x68` = `insert(key1, key2, obj)`; slot `+0x38` = self.
  * hit but a different cached object → adopt the found one via the `+7` hook;
  * miss → call the creator hook, cache and `insert` it;
  * old cache released via `FUN_141b55850` + the `+8` hook.
  Call shape from `FUN_14041b870`: `FUN_141b55730(&key1,&key2, holder+0x10, &SUB_140421f40, 3,
  FUN_14216a8a0, &UNK_14048b6a0, &UNK_141f53c60)`.
* **OBS (`FUN_14041b870`)** — the two keys default to **`DAT_142543e35`, which is the empty string `""`**
  (raw read). They are otherwise fetched from the holder's **vtable slot `+0x10`** (no args) and **slot `+8`**
  (`this` arg). The predecessor was scanning menu strings at that address: it is *not* a menu label, it is just
  this default. **Lead closed as a dead end.**
* **OBS (`FUN_14041b640`)** — holder layout: `+0x08` lazy flag, `+0x10` bundle pointer, `+0x18` cached `S`.
  `S = *(bundle) + 4`, so `S+0x30 = bundle_block + 0x34` — consistent with the officials consumer
  `FUN_1404187b0` reading `S.u32[0x30+4*i]`.
* **OBS (creator `FUN_14216a8a0`)** — allocates a **0x20-byte** wrapper (vtable `0x142596aa0`) and calls
  `FUN_14041d3d0(obj, 2, 1)`.
* **OBS (`FUN_14041d3d0`)** — allocates a **0x124-byte** block and initialises **two 0x24-byte sub-blocks** at
  `+0x04` and `+0x28` via `func_0x000140a3f410` (thunk to `FUN_14416e580`), which writes hardcoded defaults:
  `24.0f` at `+0x08`, `50.0f` at `+0x0c`, `10.0f` at `+0x58`, `1.0f`, plus
  `FUN_1415a1720(obj, 0, _DAT_1540fe274 ^ 0x4a08393)` (a hashed/derived key).

### 1.2 New discriminator — the officials holder is *not* the creator's bundle (OBS)

The registry-miss fallback block is **0x124 bytes with 0x24-byte sub-blocks**. The officials ids live at
`bundle_block + 0x34 … +0x44` of a block reached through `*(holder+0x10) + 4`. Sizes and layout do not match, so
**`FUN_14216a8a0` / `FUN_14041d3d0` cannot be the writer of the official ids** — it only builds an empty
placeholder for an unregistered fixture.

* **OBS:** `FUN_14041b870` (the materialiser) has **25 call sites** and `FUN_14041b640` (the `S` accessor) has
  **30 call sites**. `holder+0x34` is therefore a **generic lazy-proxy pattern shared by ~25 different bundles**,
  not an officials-specific structure. The officials consumer is the `FUN_14041b640` call at `0x1404187dc`
  (inside `FUN_1404187b0`).
* **OBS (unchanged from leftovers §1):** no RNG call in `FUN_1404187b0`; the ids are pure struct reads.

### 1.3 Candidates tried and rejected

* **(a) Bundle creator `FUN_14216a8a0` / its 0x124 block fills the officials.** Rejected — size/layout mismatch
  (§1.2). It only runs on a registry *miss*.
* **(b) Live heap scan for a bundle whose `+0x30` region held plausible ids.** Rejected — 71 apparent hits, all
  coincidental; no distinguishing signature available.
* **(c) Live vtable scan for the Injury constant object at `0x142df74d0`.** Rejected — that address is not a
  vtable pointer; it has **no** `lea` xrefs anywhere in the image. The predecessor's live vtable scan returned 0
  hits because the premise was wrong.
* **(d) Menu strings around `0x142543e35`.** Rejected and closed — the string is `""`.
* **(e) Writer of `match+0x5e8`.** Not located; this remains the real target.

### 1.4 Status and exact next step

**OPEN.** Answer is *not* a kickoff-time random draw (OBS: the consumer contains no RNG); still not separated
between a rotation through the officials list, a per-fixture table, or a one-off RNG at fixture creation.

**Exact next step (in order):**

1. Decompile the **25 callers of `FUN_14041b870`** and read the two string literals each passes in `rdx`/`r8`.
   The officials holder is the one whose key pair is consumed by `FUN_1404187dc`. NB: a naive 60-byte
   rip-relative `lea` scan in front of those calls returned **0 hits** — the key pointers are built further back
   in the frame, so read them in Ghidra (`dec.sh out/holders.c <addr> …`), not with the byte scan.
2. Grep the image for those two key literals. That names the fixture/competition record and leads directly to
   whoever calls the `+0x68` **insert** hook of `FUN_141b55730` with that key pair — the actual writer of
   `holder_block+0x34..+0x44`.
3. Equivalently, from the 30 `FUN_14041b640` callers take `lea rcx,[rip+disp]` (the holder object) → the
   holder's concrete vtable. **Slots `+8` and `+0x10` of that vtable are the key providers** and will identify the
   subclass directly.

**GF impact (INF, medium):** unchanged — take the 5 official ids as match parameters, chief = `id[0]`,
assistants = `id[1]`, `id[2]`; ids are `RefereeAppearance` ids 1..35 and are pure setup reads with no on-the-fly
RNG. Do **not** add a rotation or RNG model until step 1 lands.

---

## 2. What reads `injury.o` — field table CLOSED, reader still OPEN, access path EXCLUDED

### 2.1 The real field table (OBS — new, supersedes the brief's field names)

`FUN_141e78e50` reads 11 named `u32` keys. Names recovered from the string pool at `0x142b58e70`. The brief's
`levelDamage` / `symptom` are **prefixes, not fields**:

| offset | field name | key string |
|---|---|---|
| `+0x08` | `dummy` | `0x142b58e70` |
| `+0x0c` | `levelDamageMicro` | `0x142b58e78` |
| `+0x10` | `levelDamageMiddle` | `0x142b58e90` |
| `+0x14` | `levelDamageMinor` | `0x142b58ea8` |
| `+0x18` | `levelDamageSerious` | `0x142b58ec0` |
| `+0x1c` | `symptomDamageBruise` | `0x142b58ed8` |
| `+0x20` | `symptomDamageFracture` | `0x142b58ef0` |
| `+0x24` | `symptomDamageInflammation` | `0x142b58f08` |
| `+0x28` | `symptomDamageLaceration` | `0x142b58f28` |
| `+0x2c` | `symptomDamageLigament` | `0x142b58f40` |
| `+0x30` | `symptomDamageTearMuscle` | `0x142b58f58` |

* **OBS:** `0x142b58f70` = `__tostring` — the class also has a to-string virtual.
* **INF(high):** each `Injury` entry stores a damage amount per severity (Micro / Middle / Minor / Serious —
  the game's own naming; *Micro < Middle < Minor < Serious* is not a medical ordering) and a damage weight per
  symptom (Bruise, Fracture, Inflammation, Laceration, Ligament, TearMuscle). The six symptom types match the
  concrete injury vocabulary: OBS names `InjuryPart` @ `0x1425d3b48` and `InjuryCause` @ `0x1425d3d30`.

### 2.2 Decisive negative — id `0x0b` is never passed to the getter (OBS)

* **OBS (`FUN_141e5ca20`)** — a generic array accessor:
  `iVar2 = FUN_141e5de80(); if (id < iVar2 && (p = *(this->array + id*8)) != 0) return *(p + 8); return 0;`
  and **`FUN_141e5de80()` returns `0xea` = 234** (OBS, decompile). So it is a 234-entry `constant::<T>`-by-index
  table lookup returning `object->+8`.
* **OBS:** the image contains **227** direct `call 0x141e5ca20` sites. Scanning 40 bytes back for
  `mov edx, imm8` yields **0 sites with `edx = 0x0b`**. Full literal-id distribution:
  `0x2b`×30, `0x3e`×25, `0x35`×19, `0x3c`×14, `0xc0`×13, `0x2a`×10, `0x2f`×10, `0x31`×8, `0x3d`×6, `0x28`×6,
  `0x19`×6, `0xf`×5, `0x2c`×5, `0x12`×5, `0x3a`×4, `0x4`×4, `0x3b`×4, `0xc`×4, `0x36`×3; **28** sites pass a
  non-immediate.
* **INF(high):** the `0x0b` in the brief is the **class-registration id**, not a call-site literal.
  `FUN_141e5ca20` is *not* the Injury accessor at any static call site, so **xrefing the getter cannot find the
  reader** — the approach named in the brief is retired.
* **OBS:** the global that 191 sites load into `rcx` sits at `0x2984a40`, but a live read of the running PES shows
  it holds `0x0000000500000001`, **not** an array pointer. It is not the constant table.

### 2.3 Injury object shape and creation (OBS)

* **OBS:** RTTI names `.?AVInjury@constant@@` @ `0x1436b7918` and `.?AUInjuryBase@constant@@` @ `0x1436b7940`
  ⇒ `constant::Injury` derives from `constant::InjuryBase`.
* **OBS:** the Injury **vftable is at `0x142b58fb8`**, found as the only 8-byte VA slot pointing at the
  deserializer `0x141e78e50`. Slots: `+0x00` = `0x141e65aa0` (INF: deleting destructor),
  `+0x08` = `0x141e78e50` (deserialize), `+0x10` = `0x141e78e00`.
* **OBS:** exactly **one** `lea rcx,[rip → 0x142b58fb8]` in the whole image, at `0x15b47d427`, inside the factory
  **`0x15b47d400`**: `FUN_1403fb410(&{0x18}, 0x18)` then zeroes `+0x08 … +0x30`. So **no reader ever loads the
  vtable** — readers hold an `Injury*` and read `+0x0c … +0x30` directly.
* **OBS:** factory `0x15b47d400` has **0 direct `call` xrefs**. Its address appears exactly once in the image, at
  `0x14e267c8b`, as an 8-byte immediate inside a code stream (not an aligned table entry) ⇒ it is reached through
  the class registration / serialisation table, which is where id `0x0b` is assigned.

### 2.4 Candidates tried and rejected

1. **Resolve the id at every getter call site** (the brief's method) — *rejects the premise*: `0x0b` is never
   passed. Decisive, and the single most useful result of this session.
2. `mov rcx,[0x2984a40]` then walk the array live — rejected: the global is not the constant array
   (live value `0x500000001`).
3. Vtable-pointer (`0x142df74d0`) xref net, to catch virtual calls on the Injury object — rejected: no
   `lea`/`mov` xrefs; the address is not a vtable.
4. Load-at-field-offset net over all call sites (`scan3.py` / `scan4.py`, capstone-based) — rejected as written:
   capstone 5's `X86Op` has no `.base`/`.disp` (`scan3.py` died with `AttributeError`), and the regex rewrite
   missed `dword ptr [rdi + 0x20]`-style operands. Moot given (1).
5. Standard MSVC x64 RTTI Complete-Object-Locator search for `.?AVInjury@constant@@` — rejected: there is
   **no** 64-bit VA **and no** 32-bit RVA reference to the name string. This build does not emit standard x64 RTTI
   COLs for these classes (matches the predecessor's "RTTI COL points at type_info directly"). Worked around by
   finding the vtable from the deserializer pointer instead.
6. Decompile `FUN_1415a1720` (the sub-block base ctor reached with `_DAT_1540fe274 ^ 0x4a08393`) — queued, not read.

### 2.5 Status and exact next step

**OPEN** — no reader named yet — but the access path is excluded and the field set is fully named.

**Exact next step (in order):**

1. Resolve the code-stream site `0x14e267c8b` that embeds the factory pointer `0x15b47d400`. The byte stream is
   misaligned for linear disassembly (`pdis.py 0x14e267c6c` produced nothing), so hand it to Ghidra:
   `dec.sh out/reg.c 0x14e267c8b`. That function is the class-registration table builder; it assigns id `0x0b`.
2. With the id table, resolve the constant-table base and take slot `0x0b`. The reader is then any code that
   loads `+0x0c … +0x30` off a pointer taken from that slot.
3. **Cheapest remaining net, start here:** grep the image for the distinctive index displacement **`0x58`**
   (`0x0b * 8`) in a `mov reg, [table + 0x58]` immediately followed by a load at `[reg + 0x0c … 0x30]`.
   `0x58` is a rare constant for this table and is the only remaining discriminator that does not depend on step 1.
4. Once a reader is found, name its rule against the §2.1 field set — a severity read drives the
   substitution / unavailable decision (compare `levelDamage*`), a symptom read selects which `symptomDamage*`
   weight applies.