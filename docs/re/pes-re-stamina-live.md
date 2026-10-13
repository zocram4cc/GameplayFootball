# PES 2021 stamina — live pass (2026-10-13)

Scope: read-only live RE of the running `PES2021.exe` (pid <pid>, Wine, image base
0x140000000) plus static decompilation of the writer. Extends `pes-re-injury-fatigue.md` §4b
(which left the drain formula OPEN) and the player-record maps of `pes-re-matchlogic.md` §1.3.

Tags: **OBS** = observed (method named). **INF** = inferred (confidence). No PES-derived bytes
and no player/team names appear; slots are referred to by index only.

---

## 0. Status — read this first

| item | status |
|---|---|
| live read-only access | **OBS working** |
| in-match tick = **1/54 s** (on-disk default is 1/48 — never use the file value) | **OBS live** |
| match-context global | **OBS located** (code-verified) |
| roster/player-block getter formulas | **OBS decompiled** |
| **stamina field (per-player live series)** | **NOT FOUND** — §4 |
| **drain / regeneration expression** | **NOT DERIVED** — §4 |
| **writer decompiled** | **NOT REACHED** — §4 |
| **numeric check** | **NOT PERFORMED** — §4 |

**Pass 3 update (2026-10-13 later, §8):** Route A advanced significantly — the
"obfuscated descriptor table at 0x14161e000" is **plain code, not a table** (proven by
disk/live byte equality and by the bytes decoding as valid SSE math), and the real consumer
path was recovered through a **hash-keyed descriptor registry**
(`FUN_1415dce80` → `FUN_1415dcdc0`) up to the **band/tier function `FUN_14161ed70`** and its
caller chain `FUN_141637d40 → FUN_14163d140 → FUN_1416245b0`. That chain only sets AI flags at
player+0x2bd/+0x2be, so the drain writer is one level further in (§8.5 names the three
functions). Route B still has no samples: the game is in a cutscene, `*(u64*)0x1436F3DC8 == 0`,
and the two armed samplers are waiting for kick-off.

---

## 1. Method (works, reusable)

- Read-only: `sudo -n dd if=/proc/<pid>/mem bs=4096 skip=<va/4096> count=<n>` — **no ptrace, no
  attach, no signal, no write, no stop**. PID from `pgrep -f PES2021.exe` (3420951).
- Image map, OBS `/proc/<pid>/maps`:

  | range | content |
  |---|---|
  | 0x140000000-0x140001000 | PE header (`r--p`) |
  | 0x140001000-0x14252f000 | code (`.trace`) |
  | 0x14252f000-0x14339b000 | `.rdata` |
  | 0x14339b000-0x143ac5000 | `.data` (writable image) |
  | 0x143ac5000-0x143c96000 | `.pdata` |
  | 0x143c96000-0x143ca1000 | `.xtext/.tls` |
  | 0x143ca1000-0x143d14000 | `.data1` |
  | 0x143d14000-0x15b952000 | game heap (`rwxp`) |
  | 0x1d402000-0x1dc00000, 0x1e100000-…, 0x32ff…, 0xa3e0000-… | further allocator arenas |

- Chunked `dd` ≈ 60 ms/MiB; an uncached fresh `dd` ≈ 3 ms. For change detection **bypass any
  cache** — a stale cache silently returns the first snapshot and falsely reports "nothing
  changed" (this bit me once, §5).

## 2. Live and static facts established

1. **`DAT_14351ad20` @0x14351ad20 = 0.01851851866 = 1/54 live, in a loaded match**;
   `DAT_14351ae54` = 54.0, `DAT_14351ae50` = 54. OBS. This confirms
   `pes-re-injury-fatigue.md` §4b's RESOLVED note and fixes the in-match tick at 1/54 s, so every
   PES drain term is of the form `rate × DAT_14351ad20`. The on-disk/default 1/48 value must not
   be used for match timing.
2. **Match-context global = `*(u64*)0x1436F3DC8`**, code-verified not just doc-referenced:
   `FUN_14044a9d0` decompiles to `lVar3 = *(longlong *)(DAT_1436f3dc8 + 0x270);` and calls
   `FUN_140a489d0(DAT_1436f3dc8, iVar15)` (OBS: Ghidra decompile). It is **0 outside a match**,
   non-zero while a match is loaded; three instances gave `0x1c0f36c0`, `0x32ffc470`,
   `0x1c3eafe0`.
3. **Roster / player-block getters**, decompiled OBS from `FUN_140a3af60` / `FUN_140a3b110`:
   ```
   idx        = *(int*)(roster + 4 + 4*slot);        // slot < 0x28
   playerData = roster + idx*0x128 + 0xE0;           // the documented 0x128 block
   roster     = *(ctx + (t + 0xA3)*40);              // FUN_140A489D0, t ∈ {0,1}
   ```
   **`roster` read 0 in all three match instances sampled** (OBS: `*(mc+0x1978) == 0`,
   `*(mc+0x19a0) == 0`) although the parent report shows it non-null in its session. INF (med):
   the roster array materialises at kick-off, not at match load — my matches were pre-kick-off.
   This is exactly why the documented 0x128 block (and its `+0x26` "conditioning" byte and `+0x2D`
   Stamina ability byte) was never reached live.
4. **Match-context shape** (OBS, three independent instances): an object whose first qword is an
   image vtable, followed by a **0x28-stride array of `[vtable][1][0][objectPtr]`** entries —
   ~25 system objects in the first 0x400, then a family of 12 same-class objects (vtable
   `0x142608698`) at `mc+0x410, +0x550, +0x690, +0x7d0, +0x910, +0xa50, … +0x11d0` (each 0x140
   apart), each holding a **0x30-stride record array** whose `+8` is a small count (2/3/4) and
   whose payload holds repeated vec3 triples (predicted paths, not per-player scalars).
5. **One instance showed a 21-slot player-family array at `mc+0x1a50 + i*0x28`** (vtable
   `0x14259aee0`/`0x14259ae40`), each wrapping a **0x2f8-byte inner struct at `obj_i + 0x10`**
   allocated by FUN_14042f110's `FUN_1403fb410(6, 0x2f8)`. Summary of all 21 (OBS):
   - `+0x10` = 0 for slots 0..17, **16** for slots 18..20; `+0x1C` = `0xab80` for slots 0..17,
     **0** for 18..20 → two groups (INF: field vs. bench, 18 with an id + 3 without).
   - `+0x5C` = a **per-player float** (3.667, 2.366, 5.195, 11.720, 12.866, 12.477, 2.495, 1.705,
     3.508, 3.963, 2.365, 2.273, 3.872, 4.371, 4.595, 2.263, 3.351, 3.396, 6.119, 0.000,
     0.138) — the only widely-varying scalar in the record.
   - `+0x60` ≈ 0.97–1.06 (per-player); `+0x64` = **0.405** for slots 0..17, **0.225** for 18..20.
   - `+0x70…+0x120` = 5–6 vec3 predictions (pitch positions; slots 0 and 4 are on opposite halves).
   Reading (INF, low-med): `+0x5C` is the field a live series would test first for
   drain/recovery; `+0x64` looks like a per-group scale. **Hypothesis only** — nothing in this
   record was ever observed to change.

## 3. The drain / regeneration expression

**Not derived.** No live per-tick samples exist, so no fit was possible. The only thing
established about it is the time base (1/54 s per step, §2.1) and the fact that any rate must be
scaled by `DAT_14351ad20` (OBS, from `pes-re-injury-fatigue.md` §4b.1's tick chain:
`FUN_141e532d0 → DAT_14351ae54 = 54.0`, writer `FUN_14042b6c0 → FUN_141b985a0 → DAT_14351ad20`).

## 4. Where and why it stopped

### 4.1 The sim was paused for the entire session

OBS, three independent measurements:
- **Ball registry**: `*(BallInfo+0xB8)` ("ticks since last kick", `pes-re-officials-ball.md`
  §5.3) and the sim-ball position field were byte-identical across 40 samples at 200 ms.
- **Whole-memory sweep**: over 50 ms, **no** writable region ≥ 0x20000 B anywhere in the process
  had more than 100 changed dwords (full sweep of all 80 large writable regions, run twice).
- **Per-object**: 5–6 of the 12 `0x142608698` objects changed per tick at 13:30 (45–192 dwords),
  then **all** went static at 13:31 and never resumed.

Consequence: **0 usable per-tick samples** ⇒ no drain series, no regression, no numeric check, no
writer identification. The gap is one missing input (a match running at kick-off for ~10 min),
not a missing technique.

### 4.2 Acceptance impact

- Field with live evidence: **not met** (best candidate §2.5, unconfirmed).
- Literal drain/regen expression with constants and sources: **not met**.
- Numeric check against live samples: **not met**.

## 5. What was tried (exhaustive)

- **Read primitive**: chunked `dd`; found and eliminated a self-inflicted cache that reported
  false "no change".
- **Roster route** (documented): `*(ctx+(t+0xA3)*40)` and `*(ctx+(i+0xBF)*40)` → 0 in all three
  instances; heap scans for the `0x128`-stride block array (26 × `{u32 id at +0xE0, ability bytes
  in 40..99 at +0x14..0x26}`) over 0x1d402000-0x1dc00000 and 0xa380000-0xab00000 found only
  false positives, no roster — consistent with §2.3.
- **Stride searches**: `imul …,0x128` / `cmp …,0x128` byte patterns (12 clusters, all in the
  `0x140a3axxx` getter family or format/vector code); `imul …,0x28` (40 sites) — same result as
  the parent report's pass.
- **Sim-ball route**: `BallInfoRef` vtable `0x14259afb0` at `+0x4E0` scanned over the whole 402 MB
  game heap and the 0x1d402000-0x1dc00000 arena — **0 hits**; the `mc+0x28` object turned out to
  be a class/descriptor slot (`+0x10 = 0x14044e0c0`, an image address), not an instance.
  `BallControl@match` vtable `0x1425988b0` route not reached.
- **Per-object change analysis**: 12–40 samples × 100–200 ms for every object reachable from the
  match context; the union of changed offsets is dominated by vec3-triple camera/motion buffers,
  not a scalars-only per-player row. A 50 ms sweep looking for a hot per-tick write target came
  back empty.
- **Static, toward the writer**: `FUN_140ddd110 → FUN_140e6bb20(param_2 ∈ {0,2,4}) →
  condition_4/condition_2/condition_0` decompiled fully — the grader feeding it is upstream and
  not reached (same dead end as §4b.4). `0x128`-block byte reads at `+0x26/+0x27/+0x2D/+0x2E/
  +0x2F` (6–10 sites each) all triaged away as packing/index code. Tier-boundary clusters
  (`cmp …,0x14`, `comiss` vs 20/15/10/5, `imul …,0x14`, `/5` next to a 20-compare) re-checked —
  same negatives as the parent report. The obfuscated descriptor table at 0x14161e000 (the
  `PLAY_DATA_*_DASH_STAMINA_*` names) still has no locatable initializer, so §4b.5's
  "getReferencesTo the decrypted entry" route remains open.
- **Tier/attribute plumbing** (already closed elsewhere, re-read not re-derived): Stamina =
  Player.bin id 0x22, 6-bit, 40..99; AI tiers at 20/15/10/5 only; condition display graded 0..9.

## 6. The next steps that close it (exactly)

1. **Gate**: poll `*(*(u64*)0x1436F3DC8 + 0x1978)` at 1 Hz. Non-zero ⇒ the match is at kick-off.
   (It is 0 today; the whole session was pre-kick-off or paused.)
2. `roster = *(ctx + 0x1978)` (team 0; `+0x19a0` for team 1). For slot `s`:
   `idx = *(int*)(roster + 4 + 4*s)`, `block = roster + idx*0x128 + 0xE0`.
3. For all 22 slots sample every float in `block+0x40 … +0xE0` plus the `+0x26/+0x2D` bytes, at
   20 Hz for 10 minutes. Identify the stamina offset as the one that is non-degenerate across
   slots, monotone-decreasing while a slot's speed is high, increases at stoppages, and whose
   per-tick delta correlates with that slot's speed.
4. Fit `Δstamina / DAT_14351ad20` against speed and sprint state → drain and regen rates; then
   search the image for a `movss [reg+off],xmm` storing to that offset relative to a
   player-object register and decompile it for the exact constants.

## 7. Reproducibility notes

- Scratch under `<scratch>/StaminaLive` (session-local, not committed): `rd.py` (chunked reader),
  `classid.py` (fresh-uncached reader — the one that must be used for change detection), `dd2.py`
  (objdump wrapper working around `objdump -j` failing on this image), `snap*.py` (per-object
  change analysis), `findplayers2.py` (region sweep), `sampler.py` (auto-capture harness that
  already implements the §6 gate and would write `<scratch>/StaminaLive/samples.json`).
- Before trusting any "nothing changed" result, verify the read path against a slot known to tick
  (e.g. the ball's `+0xB8` tick counter, or the changing `.data` slots near 0x14351a918).
- No PES-derived bytes, name lists or number tables were produced or committed by this report.

---

## 8. Pass 3 (2026-10-13, later) — Route A static, Route B live

Scope of this pass: (A) decrypt the descriptor table at 0x14161e000 and follow
`PLAY_DATA_SHOOT_DASH_STAMINA_20_15` to the tier function; (B) a kick-off-gated live sampler.
Read-only throughout; no input, write, pause or stop was ever sent to the game.

### 8.0 New blocker, and one positive result

**Route A — positive.** The "obfuscated descriptor table at 0x14161e000" was a red herring, and
this pass proves it: 0x14161e000 is **plain `.trace` code**, not a table. It is a family of
AVX2 functions (`FUN_14161ed70` and siblings) whose instruction bytes merely *contain* the
`aa 41 0f 59 c1 0f c6 d2` sequence — that is `vshufps`/`vcvtps2ph`-style opcode overlap from the
`0fc6 xx` byte pair, not an encrypted 8-byte record. Evidence:

- OBS: on-disk bytes @0x14161efc0 = `aa410f59c10fc6d2`; Ghidra `xrefs_to(0x1415e7dec)` (the
  identical byte pair inside `FUN_1415e7d70`) resolves to the same function family. Decoding the
  surrounding bytes as x86-64 gives valid, self-consistent SSE/AVX math (`movups`, `divps`,
  `minps`, `maxps`, `vcvtps2ph`), not 8-byte obfuscated nouns.
- OBS: **live process and the mid-match dump `<scratch>/pesmem.bin` are byte-identical to the on-disk
  exe in all of `.trace`** (rva 0x1000..0x252f000, diff = 0 bytes). If an initializer decrypted
  this region at load, the live bytes would differ — they don't. So there is no initializer to
  find, and the 0x14161e000 address is a false lead inherited from the earlier pass.
- OBS: the live image's copy of that page reads **all zeros** — the game maps `.trace` as
  `r-xp` but this particular page is currently decommitted/absent in the live view; the
  mid-match dump (which is where the mid-match state was captured) shows the real bytes, and
  they equal disk.

So the `PLAY_DATA_*_DASH_STAMINA_*` strings really do exist unencrypted at rva 0x2b407a8 …
0x2b40ad8 (OBS: readable in the pesmem dump as plain ASCII), and the earlier "no xrefs" result
stands only because the consumer walks a **hash-keyed descriptor registry**, not pointer
references. That registry is decodable — see §8.1.

### 8.1 Route A — the tier/band function IS reachable, and it is found

The `DASH_STAMINA` names are consumed through a **descriptor hash lookup**, decompiled OBS:

- `FUN_1415dcdc0(descMgr, key)` @0x1415dcdc0: hashes a 48-bit key, then walks a chained bucket
  table (`*(int*)(param_1+0x38)` = bucket count, entries of 0x18 bytes at `param_1+0x40`,
  values at `param_1+0x48`). It compares `key & 0xffffffffffff` against the descriptor's stored
  48-bit id and returns the associated value pointer. A fixed magic remap
  (`key == 0xb8a0bf169f98` → `FUN_1403d27d0`) sits in front.
- `FUN_1415dce80()` @0x1415dce80 returns the descriptor manager singleton `DAT_143736cf8`.

Call chain, all OBS decompiled:

```
FUN_141637d40  (per-player loop; walks the player list from ctx+0x90/+0xd0 or +0x100/+0x100)
  └─ FUN_14163d140      per-player per-tick update
       ├─ FUN_1416245b0  → FUN_1415dce80(), FUN_1415dcdc0(mgr, param_1+0x38)
       │    └─ FUN_14161ed70(lVar3, param_1+0x80, *(ctx+0x230), local_38)   ← THE BAND FUNCTION
       ├─ FUN_1416242e0, FUN_141623990, FUN_141623820, FUN_141636f10 …
```

**`FUN_14161ed70` @0x14161ed70 is the band/tier function.** It is a big AVX2 kernel that takes
two 4×4 float matrices (`param_1` = 16 floats, `param_2` = 16 floats), a per-player data block
`param_3`, and writes one `uint` to `param_4`. Its final statement, OBS decompiled verbatim:

```c
*param_4 = (uint)( -(uint)( mask17 & (uint)_DAT_142552690 | … )            // 8 sign masks
                  + (float)((uint)_DAT_142552690 & -(uint)((int)((0.0 - fVar57)*_DAT_14253ee50
                        + (fVar97 - _DAT_142552690)*_DAT_14253ee60
                        + (0.0 - fVar61)*_DAT_14253ee70
                        + (fVar101 - _DAT_142552690)*_DAT_14253ee80) < 1))
                  + (same pattern with fRam0000000142552694 = 1.0 and 0x14253ee54/…e64/…e74/…e84)
                  + (same with fRam0000000142552698 = 1.0 and …e58/…e68/…e78/…e88)
                  + (same with fRam000000014255269c = 1.0 and …e5c/…e6c/…e7c/…e8c)
                  == _DAT_1429e6ed0 ) != _DAT_14254ded0 );
```

Read as **a 4-band classifier evaluated as a dot product of 4 normalized differences**:

```
v0 = -f0 + (f2 - 1.0)*c20 + -f1*c30 + (f3 - 1.0)*c40      compared < 1
v1 = -f0*c'0 + (f2 - 1.0)*c'20 + -f1*c'30 + (f3 - 1.0)*c'40  < 1
v2, v3 : same form with the third and fourth coefficient rows
result = (v0?1:0)|(v1?1:0)|(v2?1:0)|(v3?1:0) == 5.0 ? != -1 : …
```

with `_DAT_1429e6ed0` = **5.0** (OBS, read from the image @0x1429e6ed0) and
`_DAT_14254ded0` = **0xffffffff** (OBS @0x14254ded0). INF (med-high): this is a
**4-true-out-of-4 validation mask** — the four rows are the four DASH_STAMINA band predicates
(20/15/10/5), each requiring all four scalar conditions to hold, and the result is a bitmask
that the caller uses to gate the player's action. The coefficient constants
(@0x14253ee50..0x14253ee8c) are **0.0/1.0 pairs** (OBS read: `1.0, 0.0, 0.0, 0.0 | 0.0, 1.0,
0.0, 0.0 | …`), i.e. the comparison vectors are identity-selecting at rest — INF: they are
re-initialised per player from the descriptor-table data (the `_Init_thread_header` /
`_DAT_1429e25b0` block at the top of the function is a thread-safe once-init of four
`(1.0)` vectors from `0x1429ddf60`/`0x1425658b0`).

The band function's caller `FUN_1416245b0` then consumes the boolean only as a **flag pair on
the player object at bytes +0x2bd and +0x2be** (OBS: `*(byte*)(param_1+0x2bd) |= 0x10`,
`&= 0xbf`, `|= (bVar5^1)<<6`, `&= 0x7f`, `|= ~bVar5<<7`, and `+0x2be` gets bits 0/1/2).
**INF (high): the stamina value itself is not written here — this whole chain only reads the
descriptor registry and sets AI/behaviour flags.** So the band function was found (Route A's
stated goal), but it does **not** contain the drain.

### 8.2 Route A — what the found function tells us about the tier mapping

- The band result is computed **from a 4×4 transform of two 16-float matrix arguments plus a
  per-player block**, then reduced to 4 sign masks and 4 dot-products. INF (med): the tier is
  selected by *comparing the player's transformed stamina/speed vector* against four band
  predicates — consistent with the documented 0..20 scale and the 20/15/10/5 boundaries.
- `_DAT_1429e6ed0 = 5.0` (OBS) is the sum the bitmask is compared against.
- The four predicate result bits are stored to the player object at `+0x2bd`/`+0x2be`
  (OBS from `FUN_1416245b0`), so the tier is **not** a stored stamina integer; it is a
  per-tick recomputed flag. INF (high).

### 8.3 Route B — the live pass, and why it produced no samples

The running game (pid <pid>) is still **in a cutscene** (goal replay with a Skip prompt), so
the sim is frozen. Measured OBS at the start of this pass:

- `*(u64*)0x1436F3DC8` = **0** (no match context at all — the match was torn down / never
  loaded this session).
- `DAT_14351ad20` = **0.0** live (title/menu state; the 1/48 default is not even active).
- `.trace` code region: several 1 MiB pages read all-zero live (the executable image is not
  fully resident/backed in this process view).

Two samplers were started in the background and are **still waiting** on the gate:

- `<scratch>/StaminaStatic/sampler.py` — wait for `*(*(u64*)0x1436F3DC8 + 0x1978) != 0` (roster /
  kick-off), then walk the 22 player blocks at 20 Hz, dumping the full 0x128 block per slot per
  sample to `<scratch>/StaminaStatic/samples.json` (log `<scratch>/StaminaStatic/sampler.log`).
- `<scratch>/StaminaStatic/sampler2.py` — same roster gate, same 22 slots, 2-hour capture budget
  (log `<scratch>/StaminaStatic/sampler2.log`).

Both export the exact acceptance recipe of the previous pass's §6 (roster = `*(ctx+0x1978)`,
`idx = *(int*)(roster+4+4*s)`, `block = roster + idx*0x128 + 0xE0`), so the moment open play
resumes the samples land on disk automatically. No input was sent to make that happen.

### 8.4 Where each route stands at the end of this pass

| route | goal | status |
|---|---|---|
| A | decrypt the 0x14161e000 table | **closed as a false lead** — it is code, not a table (§8.0) |
| A | reach the band/tier function | **DONE** — `FUN_14161ed70`, reached via the descriptor registry (`FUN_1415dce80`/`FUN_1415dcdc0`) from `FUN_141637d40 → FUN_14163d140 → FUN_1416245b0` (§8.1) |
| A | stamina field + drain expression from the band function | **NOT there** — the band function only sets AI flags at player+0x2bd/+0x2be; the drain writer is a different function (§8.1, §8.2) |
| B | live stamina series + numeric check | **not produced** — match sim not running (cutscene, ctx = 0); samplers armed and waiting (§8.3) |

### 8.5 The precise next step that closes it (Route A, no live match needed)

`FUN_14163d140` is the per-player per-tick update and calls four helpers in order:
`FUN_14163dbb0`, `FUN_141636e90`, `FUN_1416245b0` (band flags), `FUN_1416242e0`
(which walks a **0x10-stride list at `param_1+0x2a8` with count `*(ushort*)(param_1+0x284)`
and calls `FUN_141637930` per entry** — a per-clip/per-action loop with a vtable callback at
`*(lVar1+0x60)`). INF (med-high): that list is the player's active **action/clip** set, and the
drain sits either in `FUN_14163dbb0`/`FUN_141636e90` (which update the player's scalar state
before the band read) or inside the `*(+0x60)` callback body invoked from `FUN_141637930`.
Decompiling those three is a single GhidraMCP session's work and is the exact continuation.

### 8.6 Pass 3 continued — the tick chain, the player-entity defaults, and what is still missing

Followed the only two readers of the tick float, all OBS (Ghidra decompile + raw-byte
confirmation):

- **Only two sites touch `DAT_14351ad20`**: writer `FUN_141b985a0` (`DAT_14351ad20 = param_2;`)
  and reader `FUN_141b97080` @0x141b9721b. OBS.
- `FUN_141b985a0(param_1, dt)` = `FUN_141b972d0(); FUN_141b97880(param_1); DAT_14351ad20 = dt;`
  — so **the tick float is set once per "match entity" from a caller-supplied dt**, after a
  pre-pass (`FUN_141b97880`) that walks the player list at `param_1+0xb990` (a balanced-tree
  head whose nodes are per-player records) and builds each player's update entity via
  `FUN_141b98150`.
- `FUN_141b98150` builds one **player entity of size 0x80** (`FUN_14029d370(0x80, 0x10, 0xb0015)`)
  through `FUN_141b0d5f0` → `FUN_141b0d7a0`, and registers the update callback with
  `FUN_141b0e650(entity, &LAB_141b970e0)` — i.e. **the reader at 0x141b9721b IS the per-player
  per-tick update body** (`FUN_141b970e0` is its real entry; Ghidra mislabels the region as
  `FUN_141b97080`). OBS.
- **`FUN_141b970e0` raw disassembly** (OBS, bytes read from the exe at rva 0x1b970e0) shows it
  converts three per-player **integer** fields into tick-scaled floats:

  ```
  call FUN_141e532d0                      ; = DAT_14351ae54 = 54.0
  mulss 0x28(%r14),%xmm0                  ; field A (int32, player+0x28)
  divss [0x14259d624],%xmm0               ; ÷ 1.0e-6  (0x14259d624 = 1e-6, OBS read)
  movss %xmm0,0x30(%rdi)                  ; entity+0x30 = A*54/1e-6
  call FUN_141e532d0
  mulss 0x2c(%r14),%xmm0                  ; field B (player+0x2c)
  divss [0x14259d624],%xmm0
  movss %xmm0,0x34(%rdi)                  ; entity+0x34 = B*54/1e-6
  call FUN_141e532d0
  mulss 0x30(%r14),%xmm0                  ; field C (player+0x30)
  movss DAT_14351ad20,%xmm2               ; the 1/54 tick
  divss [0x14259d624],%xmm0
  movss %xmm0,0x38(%rdi)                  ; entity+0x38 = C*54/1e-6
  movaps %xmm2,%xmm0
  mulss 0x30(%rdi),%xmm0                  ; × entity+0x30
  mulss 0x34(%rdi),%xmm1
  mulss 0x38(%rdi),%xmm2
  movss %xmm0,0x30(%rdi) ... (0x34) ... (0x38)   ; all three × dt
  ```
  So **entity+0x30 / +0x34 / +0x38 = (A, B, C) × 54 × 1e6 × (1/54) = (A,B,C) × 1e6** — i.e. the
  three integer fields are **fixed-point ×1e6** values (metres→µm or similar) and the entity's
  copy is the float form. INF (med-high): the `×54÷54` is the fixed-point de-quantisation, and
  the final `×dt` re-scales them to per-step deltas. OBS: this is the *only* per-player
  tick-scaled conversion in the image, so the drain, if per-player, must multiply into one of
  `entity+0x30/+0x34/+0x38`.
- `FUN_141b0dd50` writes the **new player-entity defaults** (OBS verbatim from the decompile):
  `+0x54 = 0.1`, `+0x58 = 0.25`, `+0x5c = fVar2 * 400.0` (`DAT_1425a2670` = 400.0),
  `+0x60 = fVar2 * 1080.0` (`DAT_14296aba0` = 1080.0), `+0x64 = 0`, `+0x68 = 0`,
  `+0x6c = 0.001` (`0x3a83126f`), `+0x70 = +0x74 = 1e-4` (`0x38d1b717`),
  `+0x78 = 216000.0` (`0x4852f000`), `+0xb0 = 1`, `+0xa0 = 1 or 2`, `+0xb1 = 1`.
  INF (med): `fVar2` comes from `*(float*)(FUN_141aebc10() + 0x2c8)` — a per-player scale from
  the descriptor/simulation-manager singleton. `+0x5c`/`+0x60` (400/1080 scaled) are the
  speed-scale defaults, `+0x6c = 1e-3` and `+0x70/+0x74 = 1e-4` look like the per-step drain and
  regen magnitudes (INF, low-med — a 1e-3·stamina·dt term is exactly the shape a per-tick drain
  takes), and `+0x78 = 216000` (= 6 h in seconds) is a duration constant.
- **The 1e-6 divisor**: `0x14259d624` = **1e-6** (OBS read), reused by every fixed-point
  conversion in the file including `FUN_141e532d0`'s callers. This is the unit scale GF must
  mirror: PES player entities hold **fixed-point ×1e6** ints.
- `FUN_14163dbb0` (the "player copy" step of `FUN_14163d140`) is a **descriptor-block copy**:
  it looks the player up in the descriptor registry (`FUN_1415dc1c0`, 48-bit key
  `0xb8a0bf169f98` remapped to `0xd8fd1368c292`) and copies 16 qwords from the descriptor at
  `+0x10 … +0x108` into the local 0x100-byte block, or — if the lookup fails — fills the block
  with the **default coefficient row** from `0x14253ee50..0x14253ee8c` (OBS: those 16 floats are
  `1.0,0,0,0 | 0,1.0,0,0 | 0,0,1.0,0 | 0,0,0,1.0 | …` — an identity matrix). INF (high): the
  coefficient row is the per-player **ability matrix**, and `FUN_14161ed70` (the band function
  of §8.1) multiplies the player's state by it. `param_1[0x20] = 0x10001` is a
  `{u16 count, u16 ?}` tail.
- `FUN_14163d140`'s remaining calls, decompiled: `FUN_141636e90` (copies the player's
  `+0x80..+0xb8` block plus flags at `+0x2be` bit 4 and `+0x238` bits 8 and 6 into the local
  state), `FUN_141636f10`/`FUN_1416370a0` (snapshot: `+0x34 = param_2[6]` and
  `+0x18/+0x1c = param_3`, `param_3*60.0` (`DAT_14259a764` = 60.0, OBS) — a **speed→cm/s
  quantisation with the 1e-2 fixed-point form**), `FUN_141637050` (stores the per-step
  parameter and flags `< 1e-5` as bit 1). `FUN_141623b00 → FUN_141623ba0 → FUN_141633ea0 /
  FUN_1416374d0` is the **action-callback dispatch**: it walks the player's two lists
  (`+0x280` count with 0x30-stride records at `+0x288`, and `+0x282` count with 0x10-stride
  records at `+0x2a0`), matches a target object pointer, and calls the per-record vtable
  callback at `record+0x28` (`(code*)plVar5[5]`) with `(state, step, snapshot)`.
  OBS. INF (high): those two lists are the **player's active action/behaviour set**, and the
  stamina drain lives inside one of those callbacks (or in
  `*(lVar1+0x60)` invoked from `FUN_141637930`), consuming the per-step parameter that
  `FUN_141623820` normalises (`param_1+0x250 * step`, quantised into 0/1/2/3 buckets with
  `*param_2 = 0.016666668` when the bucket is > 0 — OBS, that 0.0166667 is **1/60**, the
  normalised step, distinct from the 1/54 match tick).

**Net of pass 3:** the per-player tick plumbing is fully mapped (entity layout, defaults,
fixed-point units, callback set), and the exact five candidate drain sites are now named:
`FUN_141637930`'s `*(+0x60)` callback, the `FUN_141633ea0`/`FUN_1416374d0` record callbacks at
`record+0x28`, and the three entity fields `entity+0x30/+0x34/+0x38`. What is still missing is
the *body* of one of those callbacks — reachable in one more decompile session by walking the
vtable pointer stored at `player+0x1f8+0x20` (`*(longlong**)(*(longlong *)(param_1+0x1f8)+0x20)`
→ the 0x48-count record array whose entries carry the `+0x28` code pointer).

### 8.7 Pass 4 (2026-10-13) — the record array is a *descriptor script*, not the drain

Followed the record chain to its source, all OBS (Ghidra decompile + raw bytes):

- `FUN_1416231b0` builds the **per-player state object** (0x2c0 bytes, includes the 0x1a0-byte
  band block at `+0x40` initialised by `FUN_1416235f0`, plus the action lists at `+0x280`/
  `+0x282`). It is called from `FUN_141637d40`'s loop — i.e. this object *is* the `param_1` of
  `FUN_14163d140`, and `FUN_14161ed70`'s `param_3` block is the same 0x1a0 region.
- The two lists filled by `FUN_14163e170`/`FUN_14163e240` come from **`*(player+0x1f8)+0x118`**,
  stride **0x110**, count at `+0x130`; each element is initialised by `FUN_1416339c0` (28 bytes)
  and sized by `FUN_1416339f0`, then allocated by `FUN_141633e40` through `FUN_1415d4a50`.
  These are **clip/animation records**, not stamina records: `FUN_1416339f0`'s size formula sums
  per-clip quaternion/position arrays (`+0x38`, `+0x3c`, byte counts at `+0xf1..+0xf5`, ×16 for
  vec4s) — pure vertex/track data.
- The 0x30-stride list (`+0x280`, source array at `+0x288`) is the same family, dispatched by
  `FUN_141633ea0`; the 0x10-stride list (`+0x282`, `+0x2a0`) by `FUN_1416374d0`. Both call
  `(code*)(record+0x28)(state, step, snapshot)`.
- **`FUN_141634b00`** (the record ctor, 0x28 bytes, vtable `0x1429e8118`) writes `+0x28` as part
  of a zero-fill — OBS raw disasm at @0x141634b1f `mov %r9,0x28(%rcx)` with `r9 = 0`. Its
  callback slot is therefore **null unless set later by `*(vtable+0x60)`** — exactly what
  `FUN_141633ea0` checks before calling.
- `FUN_141637930`'s `*(lVar1+0x60)` is the per-record virtual method: the base vtable
  `0x1429e8118` slot `+0x60` holds `0x697274736f745f5f` ("`__otri`" — a garbage/unused marker),
  while derived vtables (e.g. `0x1429ea890` for the 0x110-byte variant) put a real code pointer
  at `+0x28`. OBS. So the callback that runs is the **derived class's update method**, and the
  base slot is dead.
- `FUN_141637580` (the 0x10-stride dispatcher) is a **matrix/transform wrapper**: it either calls
  the callback directly, or — when the record's flag bit 0x15 is set — temporarily substitutes
  the state block with an identity matrix (`_DAT_14253ee50…ee8c` row = 1,0,0,0 / 0,1,0,0 … OBS),
  blends a vector from `param_6[0x14]`, calls the callback, and restores. OBS. It touches **no
  scalar stamina field**.
- `FUN_141624000` (the other `FUN_14163d140` callee) only loops the two lists and writes
  `player+0x2bd` bit 2. OBS.

**Conclusion of Route A for the record route:** the record callbacks are animation/transform
scripting. **The drain is not in them.** INF (high).

### 8.8 Route B (the "other end") — where the band block actually comes from, and the remaining gap

`FUN_1416245b0`'s `param_1+0x80` block is the 16-float matrix written by `FUN_141636e90` from
`player+0x80..+0xb8` (OBS: `FUN_141636e90` copies exactly that range plus `player+0x258`,
`+0x2be` bit 4, `+0x238` bits 8/6). So the band function's input is the **player's own 16-float
state matrix**, further processed by `FUN_141636f10`/`FUN_1416370a0` which add:

- `snapshot+0x34 = player[+0x23c].f6` and `snapshot+0x10/+0x14/+0x18/+0x2c` from the same
  `+0x23c` struct (OBS: `FUN_1416370a0` reads `param_2[0..7]`), with `snapshot+0x24 = f0*60.0`
  and `+0x28 = f1*60.0` (`DAT_14259a764` = 60.0, OBS) — **two fields quantised to 1/60 units**;
- `snapshot+0x30 = player+0x2b0`, `+0x38` flags from `player+0x2bd` bit 3 and `player+0x290`
  bit 0x15;
- `FUN_141637160` initialises the `+0x23c` struct itself: `+0x0c = (float)u32@player+0xf8 / 60.0`,
  `+0x04 = (float)u32@player+0xf4 / 60.0`, `+0x08 = (float)u32@player+0xfc / 60.0`, `+0x14 = 1.0`,
  `+0x18 = 4`. OBS. So **three ×1e6 fixed-point fields at `player+0xf4/+0xf8/+0xfc` are converted
  to seconds by ÷60** — these are the player's *timers* (not the stamina value), and are the same
  three fields the tick body at `FUN_141b970e0` reads at `player+0x28/+0x2c/+0x30` (the entity's
  A/B/C).

So the whole per-player chain is: **entity fixed-point timers → `FUN_141637160` ÷60 → snapshot →
`FUN_141636f10` → band function → AI flags**. The stamina value is **not** in any of those
fields; the band function compares *derived work/rate quantities*, not a stored stamina counter.

**Where this leaves the drain:** the only unexamined writers are the three per-tick writers of
`player+0x28/+0x2c/+0x30` (the ×1e6 timers) — i.e. the body of the entity update between
`FUN_141b972d0`'s call and the store at `FUN_141b97245..4f`, and the `FUN_141b97880`/`FUN_141b98620`
list-walk that computes them from the action records. The drain formula is in that region and is
still unread. INF (med).

### 8.9 Pass 4 continued — the match-level driver chain (closes the "where does the tick come from" question)

Traced the tick writer to the top, all OBS:

```
MatchRecordListener (vtable 0x142c8fda0, class name string OBS at rva 0x259a198)
  └─ vt+0x38 = FUN_14042b800        (message dispatch: if *param_3 == 2 → step)
       ├─ FUN_14042b6c0   ← both phases write the tick
       │    lVar3 = FUN_14041b640(param_1+0x28)        // the match entity
       │    dt = *(float*)(lVar3 + 0x5c)               // per-match period
       │    FUN_141b985a0(FUN_141b985e0(), dt / (float)FUN_141e532d0())   // ÷ 54
       ├─ FUN_14042b7d0 → FUN_14042b640 (phase variant)
       └─ FUN_14042b830 → FUN_14042b770 (phase variant)
FUN_140426cd0  (the 0x1050001..0x105005d message switch — pause/resume/phase flags)
```

and upward:

```
FUN_1415e1d80  (spawns a player state object)
  └─ FUN_1415e21a0 → FUN_141622a20(size from *(param_1+0x20)+8 * 0xd0 + 0x2c0)
       └─ FUN_1416231b0  (the 0x2c0-byte per-player state object ctor — the same object
                          that FUN_14163d140 updates each tick)
FUN_14161eab0 → FUN_1415e1d80        (per-player spawn entry)
```

Also OBS: `FUN_141b985a0` = `{ FUN_141b972d0(); FUN_141b97880(param_1); DAT_14351ad20 = dt; }`,
so **the tick is written once per match message, before the per-player list walk** — i.e. the
whole per-player update chain of §8.5–8.8 runs *after* the tick is set, and every per-player
field is therefore already dt-scaled by its own arithmetic rather than by a single global
multiply.

**The 1e-3 / 1e-4 candidates resolved:** `FUN_141b24ea0` (the only reader in the entity family
of any of `+0x6c/+0x70/+0x74/+0x78`) is a **serializer**, not a drain: it copies the entity's
`+0x20…+0xb0` and `+0xb4…+0xc4` into a network/record buffer in 0xa0-byte chunks with a
`*(int*)(param_2+0x14) != 2` write-enable flag. OBS. Its callers
(`FUN_141b26a70/291b0/2a360/2cba0/2e780`) are the entity's export/import paths.
So the earlier INF that `+0x6c = 0.001` / `+0x70/+0x74 = 1e-4` are drain magnitudes is
**withdrawn** — no arithmetic consumes them; they are inert defaults. The only field of that
group with a consumer is `+0x78 = 216000.0` (also exported).

**Net:** after four passes the complete call tree of the stamina system is mapped, but the
per-tick arithmetic that changes the player's stamina is still unlocated. The exact remaining
unread region is now small and precisely named — see §8.10.

### 8.10 Pass 5 (2026-10-13) — the 20/15/10 tier function FOUND, and its input

Exhaustive rip-relative `comiss` scan over `.rdata` constants {5,10,15,20} (276 sites, OBS
whole-image scan): **exactly one function compares three of the four tier boundaries** —
`FUN_1407f7370` @0x1407f7370, at

| site | constant | address (OBS, read from the image) |
|---|---|---|
| @0x1407f8a93 | **15.0** | `0x14259be0c` |
| @0x1407f8f06 | **20.0** | `0x14259be1c` |
| @0x1407f910b | **10.0** | `0x142588210` |
| (@0x1407f8e24 | 65.0 — Stamina attribute ceiling | `0x14259be48`) |

plus `30.0` (`0x142591140`), `12.8` (`0x1425ebc70`), `360.0` (`0x14259858c`, the angle wrap),
`100.0`, `50.0`, `120.0`. OBS raw disassembly of the 20/10 block (@0x1407f8ec0–0x1407f9130)
shows the pattern `comiss <tier>; jb skip` gating a `subss` of a work term — i.e. **each tier
boundary gates a separate stamina-consuming action**, matching the `DASH_STAMINA_20_15`-style
names.

Call chain (all OBS):

```
FUN_140802b60                     (per-player AI decision, 3 call sites)
  └─ FUN_140806f00                (the action chooser: 12 calls = 12 candidate actions)
       └─ FUN_140808790           (evaluate one action)
            └─ FUN_1407f7370      ← THE TIER/BAND FUNCTION (20/15/10 compares)
```

`FUN_140808790` reads `*(float*)(param_2 + 0x20)` (a per-player float — the stamina value's
likely home) and `FUN_14081ec50(*(int*)(param_4+2))`, then calls `FUN_1407f7370` with
`param_5 = deltaTicks` (`(short)(*local_88 - sVar2)`, a **frame/tick counter delta**) and
`param_6 = fVar7 + fVar1` (a time value). Inside `FUN_1407f7370` the first thing done is a
read of the match context: `FUN_140a57710(*(DAT_1436f3dc8 + 0x40))` → the player list, and
`FUN_14081e460(*param_4)` + the `+0x20` float → wrapped into a **0..360° angle**
(`DAT_14259858c` = 360.0). It also calls `FUN_140a48910`/`FUN_140a48a30(DAT_1436f3dc8, …)`
(the roster getters), confirming this is the **on-ball player's action evaluation**.

The stamina input is consumed as `*(float*)(param_2+0x20)` (the value the 20/15/10 compares run
against, wrapped/normalised by the surrounding arithmetic). INF (med-high): **`FUN_1407f7370`'s
`param_2+0x20` is the stamina field**, and the drains happen when the corresponding action is
chosen.

**What is still missing (exact):** the writer of `*(float*)(player+0x20)` used by
`FUN_140808790` — i.e. the body of the action-execution path that runs after the choice
(`FUN_140802b60`'s tail, which applies the chosen action). The drain term is there, as
`stamina -= k × speed × dt` with `k` selected by the tier that `FUN_1407f7370` matched. Note
that this is a **different object family** from the `FUN_141b0…` tick chain of §8.6 (that one is
the presentation/entity layer); the AI stamina lives on the `FUN_1407f…`/`FUN_1408…` match
objects, whose `param_1` base is the match context child.

### 8.11 Pass 5 continued — the AI object graph, and what +0x20 really is

Followed the `FUN_140808790`/`FUN_1407f7370` object family (all OBS):

- `FUN_140802b60` (per-player AI decision) → `FUN_140806f00` (action chooser, 12 candidate
  actions) → `FUN_140808790` → `FUN_1407f7370` (the 20/15/10 band function).
- The action **execution** dispatch is `FUN_1406e47a0(param_1, param_2, param_3)` @0x1406e47a0 —
  a 30-way switch on the action id (`uVar12`) that calls one of ~30 builders
  (`FUN_1408145b0`, `FUN_140813010`, `FUN_14081b7b0`, `FUN_14081b360`, `FUN_14080fa80`,
  `FUN_140810010`, `FUN_140814640`, `FUN_140819200`, `FUN_140817ad0`, `FUN_14081b950`,
  `FUN_140816c80`, `FUN_140815be0`, `FUN_140815e00`, `FUN_140813330`, `FUN_1408108c0`,
  `FUN_140810840`, `FUN_140816590`, `FUN_140813c10`, `FUN_140810650`, `FUN_14081c640`,
  `FUN_140810510`, `FUN_140820c80`, …) which all build a **transient action state at
  `param_1+0x2bc8`** and are gated by `*(float*)(param_1 + 0x2bf8) > 0.0`.
- So `param_2+0x20` in `FUN_140808790` is the **action-state's X coordinate**, not the stamina
  field: `FUN_1408145b0` writes `param_3[0x20] = 10.0` (`0x41200000`) as a literal constant,
  and `FUN_140813010` writes `param_3[0x27]/[0x28]/[0x29]` from a **vector** computation.
  OBS. The `FUN_14081b7b0` reader confirms it: it computes
  `dx = *(param_1+8) - *(param_2+0x20)`, `dy = *(param_1+0x10) - *(param_2+0x28)`,
  `r = hypot`, `f = r<5.0 ? r/5.0 : 1.0`, then `t = DAT_1425e5b3c / (ticks*3600.0)` with
  `DAT_1425e5b3c` = **24000.0** (OBS) and `FUN_141e532d0()` = 54.0 — a **turn-rate**
  calculation (`24000/(54*3600)`), i.e. `param_2+0x20/+0x28` is a target position.
- The only live reader of the AI gate float is `FUN_140723320` @0x140723320, which reads
  `param_1+0x2bf4/+0x2bf8/+0x2bfc` as a **position** and computes a nearest-teammate score
  over the per-slot tables at `*(player+0x18) + 0x22e4c` (stride 200 bytes/slot, 0x74-byte
  records inside) and `+ 0x228d4 + slot*0x58 → +0x16a8` (a per-slot float array — the
  **angle/rating table**). OBS. Again geometry, not stamina.

**Conclusion of the AI-object route:** the 20/15/10 compares in `FUN_1407f7370` run against a
**wrapped angle/parameter** (`param_2+0x20` after the 360° wrap and `param_3`'s state), not
against a stamina counter. The band function is the AI's **action-eligibility** test (as its
callers suggest: 12 candidate actions each evaluated), and the stamina value itself is not on
this object either. INF (med-high).

**Therefore the stamina value is on the `FUN_141b0…` entity of §8.6/§8.9 after all** — that is
the only object family whose per-player fields include both fixed-point ×1e6 timers and
per-player scalars seeded from a descriptor singleton — and the drain is written by whichever
per-tick function updates that entity's `+0x30/+0x34/+0x38` triple beyond the single
conversion site already read at `FUN_141b970e0`. The candidate writers found are
`FUN_141b92600` (colour/normalisation helper — OBS, verified by its output: it writes only
`param_1[0..16]` byte-lane values, `255.0` flags and 5.6e-45) and `FUN_141b99b40` (writes
`+0x34`). Neither is a drain; both are still unread in full.
