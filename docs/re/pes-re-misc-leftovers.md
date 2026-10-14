# PES RE misc leftovers — final sweep (MiscLeftoversRE, 2026-10-14)

Tags: **OBS** = observed (decompile/raw/live, VA named); **INF(conf)** = inferred.
Static image `pes_full.bin` base 0x140000000, decompiles via `dec.sh` into `out/`,
scratch the MiscLeftoversRE scratch dir. Live PES2021.exe is read-only
(`/proc/<pid>/mem` pread, perf only). The match was at a menu for most of this
session (`MI = *(G+0x270)` null; later `G` itself went 0), so live-dependent
items name the exact next live step instead of closing.

GF code replaced/affected per item is listed in each section's table.

---

## 1. Officials pick rule — chain CLOSED, pick writer OPEN (fixture-VM)

### 1.1 The consumer, fully mapped (OBS, `out/misc1.c`)

`FUN_1404187b0(match)` (sole caller `0x14041abfb`, inside a state machine that
also writes `rbx+8 = 2..6` phases):

1. `S = FUN_14041b640(match + 0x5e8)` — the lazy-proxy accessor (§1.2). Null →
   return 0.
2. Two `movzx` bytes at `S + 0x10` / `S + 0x12` (role codes `uVar1/uVar2`).
3. First loop (`0xb × 2` = 22 slots at `match + 0x10`): fill nulls via
   `FUN_1409211e0` (kit/appearance factory: `0x18/0x1a5e0` alloc +
   `FUN_14091e9f0` + `FUN_1409227d0`; OBS `out/misc2.c`).
4. Second loop (`i = 0..2`): `id = S.u32[0x30 + 4·i]` (OBS raw `0x1404188c9`:
   `mov edx,[r14]`, `r14 = S + 0x30`, step 4) →
   `FUN_140a35a00(i, id, role12, role10)` into `match + 0xc0[i]`.

`FUN_140a35a00` (OBS `out/misc2.c`): `i = 0` → chief referee object
(`0x30e0`/`FUN_140a37620`), `i = 1/2` → assistants (`0x3080`/`FUN_140a38d70`);
all funnel to `FUN_140a36870` then `FUN_140a35b20`, which maps the numeric id
to a `RefereeAppearance` name: `id < 0x16` → `PLAYER_%s%02d` with `H` (`id >
10`) else `A` and `id % 0xb` (OBS raw strings `0x14259b088/0x142596828/2c`,
`out/misc4.c`); `id ≥ 0x16` → `PLAYER_R%02d` with `FUN_141e53520(id)` (OBS
`out/misc5.c`: `id − 0x16` clamped to 4). No RNG anywhere in this chain (OBS).

### 1.2 The lazy proxy (OBS, `out/misc1.c` + raw)

* `FUN_14041b640(holder)`: flag `+0x08`; keys from vtable slots `+0x10`
  (no-arg) and `+8` (`this`-arg); `FUN_14041b870(holder, key2, key1)` once;
  `S = *(bundle) + 4` cached at `holder + 0x18`.
* `FUN_14041b870`: keys default to `DAT_142543e35` = `""` (OBS raw); otherwise
  the two vtable keys; single call to the registry
  `FUN_141b55730(&k1, &k2, holder+0x10, &SUB_140421f40, 3, FUN_14216a8a0, …)`.
  All 25 `FUN_14041b870` callers (OBS e8 census — 25 sites, §1.3) share this
  creator, so the registry is keyed by key-pair content only.
* Creator `FUN_14216a8a0` builds a 0x20 wrapper + `FUN_14041d3d0` 0x124 block
  (OBS, prior report) — a miss-placeholder, **not** the officials writer
  (size/layout mismatch stands).

### 1.3 The 25 holders (OBS e8 census + capstone)

Call sites of `FUN_14041b870`: `0x14041b66f` (self-thunk inside `14041b640`),
`0x1404284a9/0x14042b19c/0x14042f8db/0x14042fef3/0x1404357b3/0x140441a7a/
0x140443f5b/0x14044bd51/0x1404512ef/0x1404d5380/0x140877b36/0x1408865e3/
0x140922951/0x140a0ca7d/0x140a4a6d5/0x140a8a463/0x1420b013d/0x14214dc17/
0x1421730b4/0x142173436/0x142176837/0x14217c68e/0x14217d5ae/0x15b5c6caf`.
Key setup at every site is `call [vtable+0x10]` / `call [vtable+8]` (OBS raw,
e.g. `0x140428458…46c`); the few functions with direct string `lea`s nearby
(`ORDER_%s`, `PLAYER_%s%02d`, `CAMERA_TARGET_%02d%02d`, `""`) are the *other*
holders' keys, not officials'. The officials holder's keys come from the
vtable at `match + 0x5e8`, whose writer is fixture code (below).

### 1.4 The fixture side (OBS raw)

* `match + 0x5e8` is populated in the fixture builder `0x141c7a820` (VM-heavy):
  `lea rcx,[rbp + 0x5e8]; call FUN_14022e4c0` at `0x141c7c2ef` and `0x141c7d1fa`
  (OBS raw). `FUN_14022e4c0` is an SSO-string assign (OBS `pdis`: `+0x18/+0x10`
  length checks, 6566 call sites) — i.e. the fixture id/key string is *copied*
  into the holder area, then the registry materialises the bundle (with the
  `+0x34…` official ids) under that key.
* So the pick rule (random vs rotation vs table) lives in fixture creation
  (protected `0x141c…/0x142…` VM code), **not** at kickoff: the consumer
  contains no RNG (OBS), keys are fixture-id strings.

### 1.5 Status

**Pick rule still OPEN.** Closed: full consumer chain, id→appearance map, all
25 holders enumerated, fixture-key mechanism. Next step (exact): with a live
match, read `S.u32[0x30..0x38]` (`S = *(bundle)+4` via `G+0x5e8 → +0x10 →
+0x10`) across two fixtures with the same teams; equal rows ⇒ table/rotation
(then diff consecutive fixtures for rotation), differing rows with no pattern
⇒ fixture-time draw (then find the draw in fixture creation). One `dec.sh`
batch is already queued in the report (`out/misc1.c`–`misc5.c`).

**GF impact (INF, high):** unchanged from leftovers-2 — take 3 official ids
as match parameters (chief `id[0]`, assistants `id[1..2]`, `RefereeAppearance`
1..35 via the §1.1 map); no RNG/rotation. Replaces: GF has no officials
model; when added, ids feed the appearance lookup only.

---

## 2. The `injury.o` reader — registry FOUND, reader OPEN (needs live match)

### 2.1 Getter + count (OBS, `out/miscinj2.c`)

* `FUN_141e5ca20(reg, id)`: `if (id < FUN_141e5de80()) { p = *(*reg + id·8);
  if (p) return *(p + 8); } return 0`. `FUN_141e5de80()` returns `0xea` = 234.
* 227 direct `call` sites (OBS e8 census). `edx = 0x0b` at **0** sites (prior
  report stands); the non-immediate sites compute
  `lea edx,[reg+K]` enum arithmetic — none statically equal to `0x0b`.

### 2.2 The registry object (OBS static + live)

* `rcx` (the `reg`) at 190/227 sites is `mem [rip → 0x1437f4808]`; 1 site uses
  `G` itself (`0x1436f3dc8`); rest are locals (OBS capstone census).
* Live (menu): `*0x1437f4808 = 0x29a5b00`, `*0x29a5b00 = 0x29a5b50` (array),
  but **all slots null, including `0x0b`** (OBS pread). `G` later read 0 —
  the game sat in a menu with the constant table torn down.
* So the table populates per-match/session; a menu-state scan cannot find the
  reader. The `mov reg,[table+0x58]` + `[reg+0x0c..0x30]` net (323 `0x58`-disp
  `mov`s scanned, forward 0xc0 B each) has **0** hits (OBS) — consistent with
  readers taking the pointer through the stack/vtable across functions.

### 2.3 Ruled out (OBS)

* Vtable-address live scan for the predecessor's `0x142df74d0`: that address
  is not a vtable (no `lea` xrefs); the real vtable is `0x142b58fb8` (points
  at the deserializer `0x141e78e50`). Live scan for `0x142b58fb8` over all
  5399 readable regions: **0 objects** — no `Injury` instance while at menu.
  Deserializer-pointer scan: exactly 1 hit = the static vtable itself.
* Registration bytes at `0x14e267c8b` (factory `0x15b47d400`): embedded in an
  encrypted/packed VM blob (no ASCII class names in ±4 KB; live-readable but
  opaque). Ghidra decodes `0x14e267c80` as 2 bytes of data. Static class-table
  recovery is a dead end.

### 2.4 Status

**OPEN.** Exact next step (live match required): break when slot `0x0b`
(`*(*0x1437f4808) + 0x58`) becomes non-null, record the `Injury*`, then perf
hardware-watch the object words `+0x0c…+0x30`; the first hitter is the reader.
Name its rule against the leftovers-2 §2.1 field set (severity read →
substitution/unavailable; symptom read → which weight).

**GF impact:** none yet (no reader ⇒ no rule to port). Replaces: nothing;
GF has no injury-constant reads.

---

## 3. FieldInfo F10–F12 — F11 NAMED, F10/F12 still open

Method: byte-pattern `movss` scan for float-pointer offsets with
backward-attributed `[x+0x2aa8]` loaders (loader must precede the site in the
same function with no intervening same-register load; validated against raw).
`_DAT_142565600` = 0.5 (OBS `.rdata` read).

### 3.1 F11 = 2.33984375 — goal-mouth back-plane half-factor (OBS ×2)

Two functions read float-pointer `+0x2c` (index `0xb`) off `*(G+0x2aa8)` with
`G` (`0x1436f3dc8`) as the loader base (OBS raw + `out/miscfi8.c`,
`out/miscfi9.c`):

* `FUN_1407e4f50` (sites `0x1407e57d7/0x1407e5803`, loader `0x1407e5796`
  `rcx = [rax+0x2aa8]`, `rax = [rip → G]`).
* `FUN_1409ef270` (sites `0x1409ef4e6/0x1409ef514`, loader `0x1409ef4a1`,
  same `G` base).

Both compute, gated on the ball near the goal line (`F0 <= ballx + 1`):

```
xFront = (F0 − F16) · side            = 52.44 · side
xBack  = (F0 + F11 · 0.5) · side      = 53.72991943 · side
z      = ±F2                          = ±3.66
```

and run the segment-intersection test `FUN_140a900e0` (OBS `out/miscfi9.c`:
returns 1 iff the two segments properly cross — same-sign-product test) of a
shot/pass path against the mouth-slab edges `{xFront, xBack} × {±F2}`.
Callers: `0x1409ef270` is called 4× from the attack-decision
`FUN_1409ebce0` (shot gate: alongside the shot-power computer `FUN_1409ef740`
and angle/speed checks `FUN_140458740`/`FUN_140477130`; a crossing hit vetoes
`bVar22`, OBS `out/miscfi10.c`); `FUN_140a900e0` itself has 56 call sites
(generic 2-D segment test).
So **F11/2 = 1.169921875 m is the goal-mouth slab depth**: the on-target
check volume runs from the post plane (`F0 − F16`) to `F0 + F11/2` inside the
net. Name: `goalMouthBackInset` (factor form: back-x = `F0 + F11·0.5`).
INF(medium) on the *why* (1.17 m ≈ 63% of the net depth; rationale open), OBS
on the use. GF: add `goalMouthBackX = F0 + F11·0.5 = 53.72991943` to the
goal-mouth on-target test (GF has no such test today).

### 3.2 F10 = 1.109375, F12 = 51.171875 — still UNNAMED

* Backward-attributed scan over the whole image: `+0x28` (F10): **0**
  attributed readers; `+0x30` (F12): 3 candidates, all refuted by raw check
  (`0x140823611`/`0x140828f87` read AI-cmd fields `[rdi/rbx+0x30]`, not FI;
  `0x140838aca` reads `[rcx+0x30]` with `rcx` = AI cmd, OBS decompile
  `out/miscfi11.c`).
* Sanity: the same scan recovers the known readers (`+0x18` F6 ×380 fns,
  `+0x1c` F7 ×323, `+0x20` F8 ×67 incl. `0x140623750` with the OBS clamp use,
  `+0x38` F14 ×9, `+0x3c` F15 / `+0x40` F16 ×8/×13 incl. the net manager
  `0x140425e20`, `+0x44` F17 ×5) — so the method works and F10/F12 truly have
  no same-function loader-reader pair in the image (they may be read via a
  pointer passed across functions, which this scan misses by design).
* Numeric notes (INF, low — no code): `F11 − F10 = 1.23046875` (315/256);
  `F10 = 71/64`; no bit-exact derivation from siblings found
  (`F3 − 0.1 = 2.34000015 ≠ F11`; `F0 − F12 = 1.38812256`, no clean factor).

**Status:** F11 closed (OBS use, name + formula). F10/F12 open; exact next
step: perf hardware-watch on the live FieldInfo words `+0x2c/+0x34`
(float-pointer +0x28/+0x30) during a live match, or extend the attribution
scan across function boundaries from the 69 `FUN_140a72e70` / 1
`FUN_1404aa610` FieldInfo-pointer call sites. The scan script is saved in
scratch (`fi_reads.pkl` + loader census `fi_users.txt`).

**GF impact:** add `F11 = 2.33984375` with the §3.1 formula (replaces:
nothing — GF has no mouth-slab test). F10/F12: no change (still unused by any
confirmed consumer).

---

## 4. Ball-collision open items (§13 of `pes-re-ball-collision.md`)

### 4.1 (13.2) `FUN_140a8f8b0` — CLOSED (was a misreading, OBS)

* `FUN_140a8f8b0(A, out, B)`: `out[i] = A[i]·B[i]` — component-wise product
  (OBS 3-line decompile `out/misc8b0.c`).
* Raw at the two call sites (`0x1404aaad4` soft, `0x1404aaba1` hard, OBS
  `pdis`): hard path dots `r14 = elem + 0x40` with velocity
  (`xmm6 = [rbx+4]·[r14+4] + [r14]·[rbx] + [rbx+8]·[r14+8]`, i.e. `d = v·P`)
  and passes `rax = elem + 0x4c` as the scale — so it is **the `⊙ S` step
  already in the replayed formula** (`v = (v + (P/10)·1.2·f) ⊙ S`), not an
  extra no-op step. The §10 item 4 worry dissolves; no live tick needed.

### 4.2 Runtime net-element layout CORRECTED (OBS — supersedes §9.1's 0x44)

The shared In/Out tables are **heap-built with 0x60-byte records** (OBS
`FUN_1404be8c0`: `0x60·count` alloc, `base + i·0x60` loop), not 0x44:

| offset | writer (OBS `out/miscdf.c`) | content |
|---|---|---|
| `+0x10/+0x1c/+0x28` | `FUN_1404bdfb0` | 3 world-space verts |
| `+0x40` | `FUN_1404bdf70` (from file `+0x24`) | push vector `P` |
| `+0x4c` | `FUN_1404bdf90` (from file `+0x30`) | axis scale `S` |
| `+0x58` | `FUN_1408c7710` (from file `+0x3c`) | flags |

So `P = elem + 0x40`, `S = elem + 0x4c` at runtime (validating the raw call
setup); the file's `0x44`-stride records (OBS live dump of `0x1434465f0`:
`P = (-0.4472, 0.8944, 0)`, `S = (0.5, 0.5, 0.5)`, flags `0x31` for rec 0)
are only the *input*. GF ports must use the runtime layout. The `0x44`
stride in §9.1 describes the file tables only.

### 4.3 (13.4) Contact-tag + frame-event unit — CLOSED (OBS)

* Contact writer `FUN_141b970e0` (OBS `out/misccoll.c`) lays the record:
  `+0` A.id, `+2` B.id (`u16` from `obj+0x68`), `+4` B.kind (`obj+0x6c`),
  `+5` part (`(B.kind==7) ? FUN_141e53860(info+0x40…) : *(B+0x7c)`),
  `+6` **shape tag** (`info+0x38`-family byte; `(A.kind==7)` variant from
  `param_3[0xf]`), `+8…` contact point, `+0x18/0x1a/0x1c` B-velocity terms
  (`54`-scaled: `fps·info[…] / DAT_14259d624`), then list-append to A. So
  `rec+6` = the fox::ph shape tag (sensor bones carry 1, capsules 0, ragdoll
  sub-parts 0/5/6 — matches the live census: kind 2→1, kind 4→0, kind 7→0/5/6).
  Intended touch = tag ∈ {1,2,6} stands.
* Frame-hit event `FUN_140a4c410(simBall-ish, id)` (OBS): scans 0x18 slots of
  stride 0x74 for a free one and fires `FUN_1414a4340(slot+0x10, id + 100)`.
  Bands (OBS raw): `X = 54·3.6·|v_in| ≤ 70 / ≤ 90 / else → id 0x13/0x14/0x15`
  (events 119/120/121). Unit: with `|v|` in m/s, `X = 194.4·|v|`, i.e. the
  0x13/0x14 bands are taps below 0.36/0.46 m/s; anything harder (every real
  shot) is 0x15. GF: three-tier frame-hit sound/event by `194.4·|v|`.

### 4.4 Still open (need a live match; menu state confirmed)

* **(13.1) `Q_slot` source** (`human+0xdc/+0xec/+0xfc/+0x10c`): static
  `movss`-store census (40/24/20/19 fns) collapses — the only multi-slot
  writers decompiled (`0x141e79ce0`, `0x141e6ebf0`) are constant-table
  *deserializers* filling same-offset fields of a different struct (OBS).
  Exact next step: one tick sampling the four slots alongside the
  AnimePlayer bone world quaternions (perf watch or `samp.py` + bone read).
* **(13.3) State-2 (Out table) replay**: loop decoded (§10 of the report);
  Out `P` is static on disk (OBS §9.2); needs a ball-outside-net tick.

**GF impact:** replace `ApplyGoalNettingCollision` with the §3.5/§10 sheet
loop using the §4.2 runtime layout (`P@+0x40`, `S@+0x4c`, flags `@+0x58`,
0x60 stride) instead of the file layout; add frame-hit events 119–121 by
`194.4·|v|` bands; gate intended touches on ph shape tag ∈ {1,2,6} per §4.3.
`pesballcollision.*` (frame part, already ported) is unchanged.

---

## 5. Re-touch lockouts — NOT closable today (no live match)

* Live state (OBS): `MI = *(G+0x270)` null for the whole session (menus
  only); `G` briefly read 0 mid-session, then reappeared at a new address
  (`0x2d4d9570`, was `0x379365c0`) with `MI` still null — confirmed by 6×1 s,
  6×9 s, and 100 s background polls.
* The lockout formulae (`FUN_1409fa720`: 0.05/0.26/0.16+0.1(1−s²)/0.5/0.75,
  ×3/×2, shot-through) are OBS static; only the *minimum-interval live
  confirmation* (ring-0 `+0x10` age ≥ `fps·L` at each TRAP start) is missing,
  exactly as in `pes-re-receiving.md` §5.4.
* Exact next step: fresh `findhist2.py` handle for the live match, then
  `samp.py OUT 120 54 HIST` around kickoff/restarts; measure ring-0 age at
  every TRAP start (expect ≥ `fps·L` per kind) and the shortest re-trap gap
  per player. The ReceivingRE `samp.py`/`findhist2.py` path is verified
  working (5896 contiguous 54 Hz records previously).

**GF impact:** none until confirmed (GF has no re-touch timer; do not add
one on static evidence alone).

---

## 6. Ledger (new/updated entries)

| id | claim | conf. | evidence |
|---|---|---|---|
| EV-MISC-0001 | Officials consumer = `1404187b0`: `S.u32[0x30+4i]` ids → `140a35a00(i,id,role12,role10)`; chief = id[0] | Confirmed (code) | `out/misc1.c`, raw `0x1404188c9` |
| EV-MISC-0002 | Id→appearance map: `id<0x16` → `PLAYER_[AH]%02d`, `id%0xb`; else `PLAYER_R%02d` | Confirmed (code) | `out/misc4.c`, `out/misc5.c`, strings `0x14259b088/828/82c` |
| EV-MISC-0003 | 25 `14041b870` holders enumerated; keys always from vtable `+8/+10` | Confirmed (code) | e8 census (25 sites), raw key setup |
| EV-MISC-0004 | Pick rule lives in fixture creation (`0x141c7a820` sets `match+0x5e8` key via `14022e4c0`); consumer has no RNG | Confirmed (code) | raw `0x141c7c2ef/0x141c7d1fa`, `pdis 14022e4c0` |
| EV-MISC-0005 | Constant registry = `*0x1437f4808` (190/227 getter sites); 234 slots; slot `0x0b` null at menu | Confirmed (live) | `live_reg.py` pread |
| EV-MISC-0006 | No live `Injury` object at menu (vtable `0x142b58fb8` scan over 5399 regions: 0 hits) | Confirmed (live) | `live_injury2/3.py` |
| EV-MISC-0007 | `0x14e267c8b` factory ptr sits in an opaque VM blob (no class names ±4 KB) | Confirmed (bytes) | blob string dump |
| EV-MISC-0008 | F11 = 2.33984375 names the goal-mouth back plane: `xBack = F0 + F11·0.5 = 53.72991943` in two shot-veto fns | Confirmed (code) | `out/miscfi8.c`, `out/miscfi9.c`, raw loaders from `G` |
| EV-MISC-0009 | F10/F12 have no same-function FI-loader reader in the image (method validated on F6/F7/F8/F14/F15/F16/F17) | High (static census) | backward-attribution scan, `fi_users.txt` |
| EV-MISC-0010 | `140a8f8b0` = component-wise `out = A⊙B` = the replayed `⊙S` step, not an extra no-op | Confirmed (code) | `out/misc8b0.c`, raw `0x1404aaad4/a1` |
| EV-MISC-0011 | Runtime net elements are 0x60 stride: world verts `+0x10–0x30`, `P@+0x40`, `S@+0x4c`, flags `@+0x58` (file = 0x44 input only) | Confirmed (code) | `out/miscdf.c`, `FUN_1404be8c0` alloc |
| EV-MISC-0012 | Contact record: `rec+6` = ph shape tag; frame-hit ids `0x13/14/15` (events 119–121) by `194.4·\|v\|` ≤ 70/90 | Confirmed (code) | `out/misccoll.c` |
| EV-MISC-0013 | `Q_slot` multi-slot writers found statically are constant deserializers (different struct) | Confirmed (code) | `out/miscquat.c` |
| EV-MISC-0014 | No live match during session (`MI` null throughout; `G` realloc'd once, still no match) ⇒ lockouts, Q-slot source, state-2 replay stay live-open | Confirmed (live) | `G/MI` polls |

## 7. Open list (exact next steps)

| item | status | next step |
|---|---|---|
| Officials pick rule | open | two-fixture `S.u32[0x30..]` diff live (§1.5) |
| Injury reader | open | slot-`0x0b` watch + `+0x0c…+0x30` watch live (§2.4) |
| F10/F12 names | open | live FieldInfo-word watch or cross-function attribution (§3.2) |
| Q-slot source / state-2 / lockouts | open | live match capture (§4.4, §5) |

Recommended next highest-value step: re-run this ticket's live items (§2.4,
§4.4, §5, F10/F12 watch) in one live-match session — all static groundwork
is laid and the scripts exist.
