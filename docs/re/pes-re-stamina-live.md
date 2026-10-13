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

**The blocker in one line:** every sampling window landed while the match sim was **paused**, so
there were **no per-tick writes anywhere in writable memory** (measured three ways, §4.1) and no
live stamina series could be captured. All infrastructure works; only an actively-running match is
missing. §6 gives the exact next steps.

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
