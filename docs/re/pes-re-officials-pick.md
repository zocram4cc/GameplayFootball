# PES RE — how the match officials are picked (OfficialsPickRE, 2026-10-14)

Closes `docs/re/pes-re-leftovers-2.md` §1 and `docs/re/pes-re-leftovers.md` §1.

Tags: **OBS** = observed (how is given); **INF(conf)** = inferred. Static image `pes_full.bin`, base 0x140000000.
Tools: GhidraMCP (GUI, PESLive), the headless `dec.sh` for `.impdata` bodies, capstone over `pes_full.bin`,
and read-only `/proc/<pid>/mem` preads during the live match the orchestrator confirmed (state 1, no writes,
two single-shot reads, no watchpoints). Scratch: the OfficialsPickRE scratch dir (`gui/`, `g2/`, `out/`,
`pick.asm`, `envlive.py`, `setuplive.py`).

## Answer

**It is a random draw at match-setup time**, made once per match before kick-off. It is not a fixture
table, a rotation, or a per-team assignment. The engine makes one uniform random permutation of an id pool
and takes the first 5 as `officials[0..4]`. The RNG is the **process-global utility xorshift128**, not the
match RNG.

Which pool is used depends on the mode and the competition:

| case | pool (RefereeAppearance ids) | evidence |
|---|---|---|
| mode ∉ 7..10, **or** competition id = 0xFFFF (exhibition-style; the live match) | **25 ids: 16–20, 1–5, 6–10, 21–25, 31–35** (groups 3,0,1,4,6; **11–15 and 26–30 are excluded**) | OBS disasm + live |
| mode 7..10 and the competition record's `+4 == -1` | all **35 ids** (1–35) | OBS disasm |
| mode 7..10, competition with a region record | a subset of the 7 groups, chosen by a `switch` on the competition's region code | OBS decompile (fall-through not disasm-checked) |

**Weighting:** none. Fisher–Yates (`std::shuffle`) gives a uniform random ordered 5-subset of the pool, so the
chief is uniform over the pool (1/25 in the exhibition case) and all 5 ids are distinct.

## 1. Writer chain (all OBS)

```
FUN_141551080(&compId)                         officials picker (callers below)
  └─ FUN_1414c1f00(setup, id, i)               setup.u32[0x58 + 4*i] = id, i = 0..4
        setup = *(*(u64*)0x143705e10 + 0x50) + 0x35960      (FUN_1414b6a60 → +0x50)
FUN_141fb9fb0(ctx)                              build the "MATCH"/"ENV" record from the setup
  └─ S1.u32[0x30 + 4*i] = FUN_1414c1080(setup, i) (u8), 0 → 1      S1 = block + 0x94 (staging copy)
FUN_141fb7e00 / LAB_14041b530                   commit: copy block+0x94 → block+0x04 (0x90 bytes)
FUN_14041b640(holder)                           S0 = block + 4 (committed copy)
FUN_1404187b0 (@0x1404187dc)                    official i created with id = S0.u32[0x30 + 4*i]
```

### 1.1 The holder is the "MATCH"/"ENV" record (OBS)
* `match+0x5e8` is an **embedded** holder object (`lea rcx,[rdi+0x5e8]` at 0x1404187d2; disasm).
  Its ctor store `mov [r14+0x5e8], rax` at 0x14041246b loads vtable **0x142596b40** (the lea at 0x140412464).
  The dtor stores base vtable 0x142596b00 at 0x140413876.
* Vtable 0x142596b40: slot `+8` = `0x140419d40` = `lea rax,"MATCH"; ret` (string at 0x142596908), and slot
  `+0x10` = `0x140419d70` = `lea rax,"ENV"; ret` (string at 0x142596b58). Raw disasm plus a string read.
  `FUN_14041b640` calls `FUN_14041b870(this, key1=slot8()="MATCH", key2=slot10()="ENV")`.
* A rip-relative scan of the whole image for every `ENV\0` and `MATCH\0` literal finds **one** code
  reference to this "ENV" (0x140419d73). The only other "ENV" literal, 0x1435cb2a0, is used at 0x14224944d and
  is unrelated. So no other code inserts this key pair; the record is created by the registry miss path.
* All 25 "callers" of `FUN_14041b870` are inlined copies of `FUN_14041b640`, and every one takes its keys from
  the vtable. Two of them (`FUN_140886560` and `FUN_14214cc10`) construct a 0x142596b40 holder directly.
  Step 1 of the old plan ("read literals in rdx/r8") therefore has no literals to read. The vtable route
  (old step 3) is what resolved it.

### 1.2 Block layout — correcting leftovers-2 §1.2 (OBS)
* `FUN_14041d3d0` / `FUN_140412260` allocate 0x124 bytes: a `u32` refcount, then **two 0x90-byte
  sub-blocks** at `+0x04` and `+0x94`. The stride is `puVar + 0x24` on a `u32*`, i.e. 0x90 bytes, not 0x24.
  Each sub-block is initialised by `FUN_14416e580` (`.impdata`, clean decompile). That function sets
  `+0x30..+0x40 = 1,1,1,1,1` (default ids), `+0x08 = 24.0f`, `+0x0c = 50.0f`, `+0x58 = 10.0f`, and so on.
* `FUN_14041b640` returns `block+4` (**S0**, committed). `FUN_1404512c0` returns `block+0x94` (**S1**,
  staging). `LAB_14041b530` (asynchronous, queued by `FUN_140445830`) and `FUN_141fb7e00` (synchronous) both
  copy S1 → S0 (0x90 bytes; disasm of 0x14041b530 and decompile of 0x141fb7e00).
* So the predecessor's "size/layout mismatch" (candidate (a)) is **wrong**. The miss-creator's block *is* the
  officials record. It only supplies defaults, and the real values arrive later through S1.
* **Live (OBS):** a scan for vtable 0x142596b40 found ~70 holders. Every one points at the **same** wrapper
  (vtable 0x142596aa0, refcount 0x47 = 71). Both sub-blocks held `+0x30 = (5, 34, 23, 7, 16)`. The
  predecessor's "71 coincidental hits" (candidate (b)) were these 71 references to the one real record, not
  coincidences.

### 1.3 The writer of S1+0x30 (OBS)
* `FUN_141fb9fb0` (decompile, and disasm 0x141fba53e–0x141fba56e):
  `rsi = S1+0x30; for i<5: al = FUN_1414c1080(setup,i); eax = al ? al : 1; [rsi] = eax`.
  `FUN_1414c1080` = `i>4 ? 1 : setup.u8[0x58+4*i]`. The same function fills the other ENV fields (`+0x14`,
  `+0x1a`, `+0x1c`, `+0x20`, `+0x24`, `+0x65`, and more) from the setup, and ends with the `FUN_141fb7e00`
  commit.
* Other S1 writers are not the pick:
  * `FUN_141fb1880`, `FUN_141fb1930`, `FUN_14087b440`, `FUN_141fb8af0` and `FUN_140447b50` write single
    flags at `+0x44..+0x6d`.
  * `FUN_141fe20a0`→`FUN_141fc0460` sets a flag only.
  * `FUN_14214cc10` overwrites the whole S1 from a 0x90-byte copy at `param_2+0x1eb78` inside a large match
    snapshot. This is a restore/resume path that also restores 24 player entries. **INF(med)**:
    replay/suspend restore.

### 1.4 The writers of setup+0x58 (xrefs to `FUN_1414c1f00`, OBS)
| site | role |
|---|---|
| `FUN_141551080` ×3 (0x1415519dc, 0x141551cff, 0x141551eaa) | **the picker** (§2) |
| loop at 0x1413028f0–0x141302905 (no Ghidra function) | reset: `officials[i] = i+1` (ids 1..5) |
| `FUN_1410682d0` | copies 5 ids from a received packet (`param_2+0x14c`): online sync, receive side |
| (reader) `FUN_140dd6520` | packs setup incl. the 5 ids for sending: online sync, send side |

Callers of the picker: `FUN_140aa4480` passes `compId = 0xffff` when `obj+0x10c != 0` or
`FUN_140c50300()` (mode ∈ {6, 0x19, 0x1a, 0x1b}). `FUN_1415251e0` passes `compId = setup.u16[+8]`
(`FUN_141854f00`). `FUN_1415251e0` is reached from ~12 mode-specific setup builders.

## 2. The pick rule — `FUN_141551080` (OBS: decompile, with the key paths checked in disasm)

```
// RECONSTRUCTION (OBS)
for i in 0..34: group[i/5].push(i+1)          // 0x14155113a: mul 0xcccccccd, shr 2 → i/5;
                                              // jump table 0x141551fb4 for groups 1..6, default = group 0
mode = modeTable[gameState+0xf0]              // FUN_14149f4f0, table 0x1434fc9c0 (stride 16)
if (mode-7 > 3 || compId == 0xffff):          // 0x141551901..0x14155191a
    pool = g3 ++ g0 ++ g1 ++ g4 ++ g6          // 0x141551dfc..0x141551e40 (5 appends)
else if (competition(compId)+4 == -1):        // 0x14155192f..0x14155193f
    pool = g0 ++ g1 ++ g2 ++ g3 ++ g4 ++ g5 ++ g6   // 0x141551945..0x1415519a5 (7 appends)
else:
    region = …(+0x80 of the competition / FUN_141544f50)
    pool = switch(region) { subsets of groups }      // decompile only
std::shuffle(pool, xorshift128_global)        // 0x1412722d0 → 0x14475d910
for i in 0..min(4, |pool|-1): setup.officials[i] = pool[i]   // FUN_1414c1f00
```

* **Group ↔ stack slot (disasm):** g0 `[rsp+0x70]`, g1 `[rsp+0x58]`, g2 `[rbp-0x30]`, g3 `[rbp-0x78]`,
  g4 `[rsp+0x38]`, g5 `[rbp-0x48]`, g6 `[rbp-0x60]`. These come from the `lea rcx` before each case's
  vector-grow call. The exhibition branch appends `[rbp-0x78],[rsp+0x70],[rsp+0x58],[rsp+0x38],[rbp-0x60]`,
  which excludes g2 (11–15) and g5 (26–30).
* **Shuffle (OBS, `dec.sh` on 0x14475d910 and 0x1412722d0, both the same body):** this is MSVC
  `std::shuffle`. For target index k = 1..n-1, `j = rng(k+1)` and then `swap(v[k], v[j])`. `_Rng_from_urng`
  has 32 bits and mask 0xffffffff (0x141551e4d–0x141551e6f), so one 32-bit draw per step, `% (k+1)`, with the
  standard MSVC bias rejection. The exhibition pool costs 24 draws plus rare retries.
* **URNG `FUN_141587690` (disasm):** Marsaglia xorshift128 on the state at `*(u32[4]*)0x1437066a8`:
  `t=x^(x<<11); x=y; y=z; z=w; w=(w>>19)^(t>>8)^w^t` (returns w). If the state pointer is null it falls back to
  `jmp [0x1425306f0]`. That target is a protected thunk; the API is unknown.
* **Seeding `FUN_1415870f0` (decompile):** lazily allocates the state with `x = FUN_141b34ad0(clock())`, the
  same clock read that seeds the match RNG (rng doc §2.1), and `y,z,w = 0x159a55e5, 0x1f123bb5, 0x05491333`
  (Marsaglia's defaults). The same state is consumed by `FUN_1415872f0` (`rand % n`), `FUN_141587400`
  (float) and others. It is shared process-wide, so the draws depend on everything that ran earlier in the
  session.
* **Competition region switch (decompile only; the fall-through chains are not disasm-checked):**

  | region code | groups |
  |---|---|
  | 1/0x1b/0x25 | all |
  | 0x1f/0x23/0x29/0x2c | 4,5,6 |
  | 2,3,4,9,10,0xc–0x10,… | 0,1 |
  | 100 | 1 |
  | 5,6,7,0xb,0x11,0x1e,… | 0,1,4 |
  | 0x15/0x18/0x28/0x2b/0x65 | 1,4 |
  | 0x77–0x79 | 4 |
  | 8 | 0,5,6 |
  | 0x20/0x24 | 3 |
  | 0x22 | 0,1,2,4 |
  | 0x1d | 1,2,4 |
  | 0x32–0x37/0x40/0x41 | 5 |
  | 0x66/0x7d–0x7f/0x8b–0x8d | 5,6 |
  | 0x7a–0x7c | 0,3,4 |
  | default | 0 |

  **INF(med):** the groups are referee regions (nationality pools) matched to the league's region.

## 3. Live check (OBS, the current match, read-only)
* `gameState+0xf0 = 8` → `mode = 6`; `setup.u16[+8]` (compId) = **0xFFFF** → exhibition branch.
* `setup+0x58` = **(5, 34, 23, 7, 16)**, equal to ENV S0/S1 `+0x30`. This shows the setup → ENV → consumer
  chain on one match.
* Every observed id is in the 25-id pool: the earlier match's (8, 23, 32, 35, 19) and this one's
  (5, 34, 23, 7, 16). None are in 11–15 or 26–30. With a uniform 35-id pool the chance of that is ≈ (25/35)^10 ≈ 3 %.
* The xorshift state pointer is non-null, so the fallback path is not taken.

## 4. Killed hypotheses
* **Fixture lookup / per-team assignment:** killed. The picker builds its pool from `i+1` for i<35 and
  shuffles it. No team input is used. The only competition input is the region → group subset.
* **Rotation:** killed. No state carries over between picks except the shared RNG.
* **Kick-off draw:** killed (unchanged). The consumer has no RNG; the draw happens in setup.
* **"Holder key literals passed in rdx/r8":** the keys come from the vtable, and there are no literals
  at the call sites (§1.1).
* **Leftovers-2 §1.2 discriminator** (0x24-byte sub-blocks): wrong stride. Corrected in §1.2.
* **VM-generated pick:** killed. The whole chain is native and clean. Only `FUN_14416e580` and the
  shuffle bodies live in `.impdata`, and they decompile cleanly.

## 5. GF impact
* Keep the ids as **match parameters**: chief = `id[0]`, assistants = `id[1]`, `id[2]`; `id[3]`/`id[4]` are
  used only by demo actors (leftovers §1). This is unchanged.
* A faithful default, if GF generates the ids itself: an ordered uniform random 5-subset of the pool given by
  the rule in §2 (exhibition: 25 ids, groups 3 and 6 of 7 excluded). Draw it from a **separate,
  non-match RNG**. PES draws from a process-global clock-seeded generator, so it is not reproducible from the
  match seed, and drawing it from GF's match RNG would shift the match stream. Read the group size/count rule
  as structure; do not bake an id table into code (fleet rule).
* Reproducing PES's exact pick for a given session is not feasible: the global RNG state depends on all
  earlier consumers.

## 6. Open
* The API behind the null-state fallback `[0x1425306f0]` (protected; never taken live).
* The disasm check of the competition-region fall-through chains, and what the region code at competition
  `+0x80` / `FUN_141544f50` means.

Summary: officials pick closed — setup-time uniform shuffle (global xorshift128) of a 25-id pool
(exhibition) / 35 / region subset, writer chain `FUN_141551080 → setup+0x58 → FUN_141fb9fb0 → ENV S1 →
commit → S0 → FUN_1404187b0`; report `local://pes-re-officials-pick.md`.
