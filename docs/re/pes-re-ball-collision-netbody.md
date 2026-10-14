# Net + player-body collisions: portable rebuild (NetBodyRE)

Extension of the session `pes-re-ball-collision` report (§§1–8 untouched): the runtime net-table formula,
the per-tick net checklist, the body-contact normals, and the ignore rules. Read-only RE of the live match;
image base `0x140000000`. Scratch: the NetBodyRE scratch dir (`netinit.a.c`, `body.a.c`, `kick.a.c`,
`misc2.a.c` = annotated decompiles).

## 9. Net tables at runtime: portable rebuild recipe

### 9.1 What is static on disk and what is computed (OBS)

File bytes of the user's exe vs the live dump at the two tables:

- Layout (confirmed live): VA `0x1434465f0` (In, count 48 at +0, transform fn `0x1404bece0` at +8, records from
  +0x14) and `0x1434472d0` (Out, count 32, same fn, records from +0x14). Record `0x44` B: verts +0x00–0x23
  (9 floats), `P` +0x24–0x2B (3 floats), `S` +0x30–0x3B (3 floats), flags u32 +0x3C, pad u32 +0x40.
- OBS — `S` and flags of **all 80 records are byte-identical on disk and live** (0/80 mismatches): static
  `.data`, readable from the user's exe file at load time (`.data` section RVA `0x339b000`, raw `0x339a200`;
  file offset = raw + (VA − base − RVA)).
- OBS — verts are **zero on disk, filled at start-up** (all 80 records' +0x00–0x23 differ disk-vs-live); `P`
  is split (next bullet). Writers are the CRT dynamic initializers `FUN_1401baba0` (In, body
  `0x1401baba0–0x1401bc89d`) and `FUN_1401bc8a0` (Out, body `0x1401bc8a0–0x1401bdca3`), decompiled with `dec.sh`.
- Refinement of the earlier §6 item 4 (OBS): not all of `P` is computed. **Out `P` (all 32) is static on disk**
  (e.g. rec0 `(6,0,0)`, rec12 `(3,0,10)`, rec24 `(1.5,2,0)`, rec28 `(4,4,0)` — identical live). **In `P` is
  split**: recs 14–19 (back, `(−8,0,0)`), 20–25 (side, `(0,0,∓15)`), 36–41 (roof, `(0,−8,0)`) are static; recs
  0–9, 26–27, 34–35, 42–47 (the hard sheets) are computed (zero on disk). GF reads Out `P` + In soft-sheet `P`
  from the file and computes the rest.

### 9.2 Closed formula (prefer over emulation; OBS decompile)

Every computed float is `base × K`, a copy, or (Out only) a midpoint `(a+b)/2 × K`:

- Bases are `.data` ±100 constants: In inputs `0x143446570–0x1434465ec` (32 floats, immediately before the In
  table); Out reuses the same In-region bases plus its own scratch.
- Multipliers are `.rdata` floats (read them from the user's exe too):
  - In verts: `0.5 (@0x142565600)`, `2.0 (@0x142565604)`, `0.8 (@0x142572348)`, `0.6 (@0x14257a190)`,
    `−0.5 (@0x14258d694)`, `1.3 (@0x14259bdd8)`, `1.5 (@0x14259bddc)`, `1.8 (@0x14259bde0)`, `6.0 (@0x14259bdec)`,
    `1.2 (@0x14259d5d4)`, `1.4 (@0x14259d5d8)`, `1.1 (@0x1425a3590)`, `1.6 (@0x1425ab12c)`,
    `2.2 (@0x1425ab130)`, `2.3 (@0x1425ab134)`, `2.6 (@0x1425ab138)`. Coverage OBS: all 432 vertex slots
    (48 × 9) are written by `FUN_1401baba0`.
  - In hard-sheet `P` (20 recs: 0–9, 26–27, 34–35, 42–47): unit-ish vectors over `sqrt(1.25) (@0x1425ab128)`
    or `sqrt(2) (@0x142565604)`. E.g. rec0 `P = (−0.5, 1, 0)/sqrt(1.25) = (−0.4472, 0.8944, 0)`; rec2
    `(0, 1, −0.5)/sqrt(1.25)`; rec6 `(−1, 0, −1)/sqrt(2)`. Decompiler note: the `CONCAT44(hi,lo)` store puts
    the *second* argument at the lower address (`[a]=lo, [a+4]=hi`). The `sqrt == 0` branches never take.
  - Out verts: all 288 slots (32 × 9) written by `FUN_1401bc8a0`; `.rdata` multipliers `0.5`, `0.8`, `0.9
    (@0x14258c260)`, `0.7 (@0x14257a194)`, `0.85 (@0x1425ab120)`, `0.95 (@0x1425ab124)`, plus midpoint forms
    `(a+b)/2 × K` where `a,b` are earlier table slots used as scratch (e.g. the slot holding 72 from two
    slots holding 90). One extra store `0x143447b64 = 0` (scratch terminator, not a record).
- World transform at load (OBS `FUN_1404bece0`, `FUN_1404aa610`): with FieldInfo `F0,F2,F9,F13,F14` (live
  `52.56, 3.66, 2.5, 54.32031, 54.43359`; cached at `.data 0x14344644c–0x14344645c` as `W 3.706875 =
  F2+0.046875 (@0x1425a966c)`, `H 2.5`, `X0 52.46 = F0−0.1 (@0x142588200)`, `XT F13`, `XB F14`):
  `fy=(Y+100)/200, fx=(X+100)/200; y=fy·H; z=Z/100·W; x=((XB−X0)(1−fy)+(XT−X0)·fy)·fx+X0`.
- Verification (OBS, read-only pread of the live match): live In rec0 verts `(120,−100,−600,…)` + `P`
  `(−0.4472,0.8944,0)` + `S (0.5,0.5,0.5)` + flags `0x31`; Out rec0 `(100,−100,−100,…)` + `P (6,0,0)`; header
  counts 48/32 and fn `0x1404bece0` — all equal the formula outputs and the earlier `net_tables.txt` dump.
  GF rebuild = read bases/multipliers/`S`/flags/static-`P` from the user's exe, evaluate the products, apply
  the transform. No PES bytes committed. Emulation (`emu.py` on the two initializers) is the fallback.

### 9.3 GF counterpart

Replaces `ApplyGoalNettingCollision` (`ballphysics.cpp`, a spring clamp PES never does) with the §3.5 sheet
loop over exe-loaded tables + the `FUN_1404bece0` world transform. The goal-frame part (`pesballcollision.*`)
is already ported and unchanged.

## 10. Net response per tick: port checklist

OBS `FUN_1404aa790` (re-read against the earlier §3.5; the loop stands, additions only):

1. Mirror: goal at −x negates x and z of pos/vel/spin on entry and exit (literal).
2. Reference point `R` (OBS `FUN_1404aa3d0`): state 1 `((XT+X0)/2, H/2, 0)`; state 2 `C + normalize(P−C)·2W` with
   `C = (X0 − (XT−X0)/1.5, H/−1.5, 0)` (`1.5 @0x14259bddc`, `W` = cached `0x14344644c`).
3. Hit collection (OBS `FUN_1404beda0` + `FUN_140a93a70`): test **every** triangle of the state's table for the
   segment `R→P`; keep those with `0 ≤ t < 1` (strict: `t == 1` excluded).
4. Soft sheets (`!(flags & 0x20)`, OBS order): `f = (N·v > 0) ? 0.25 (@0x142592228) : 1`;
   `v = (v + (P/10)·1.2·f) ⊙ S` (`10 @0x142588210`, `1.2 @0x14259d5d4`); the `1.0 (@0x143446460)` factor is
   literal. Then `v = FUN_140a8f8b0(v, R, Ŝ)` is applied (OBS decompile: component-wise product
   `v[i] = v[i]·Ŝ[i]` with the per-sheet world-space direction; the earlier replay without this step already
   matches to print precision, so it is a no-op on the replayed ticks — INF(high): axis-rescale guard).
5. Hard sheets (`flags & 0x20`, OBS): only if `N·v < 0` **and** (`!(flags & 0x10)` or `3.6·v.y ≥ −9
   (@0x1425a9680)`); `v_snap = v` saved *before each hard sheet*; `d = v·P`; if `d < 0`: `v += (−2d)·(P ⊙ S)·1.2`.
6. Spin (OBS): if ≥1 hit and `v != v_snap`: `spinRaw = (spinRaw + normalize(v × v_snap)·|v|·0.001)·0.9`
   (`0.001 @0x1425a9668`, `0.9 @0x14258c260`).
7. Gravity + floor (OBS): `v.y −= 9.80665/54 (@0x1425a9678)`; if `P.y ≤ 0.11868593` (double `@0x1425a9670`) and
   `v.y < 0`: `v.y *= −0.7 (@0x1425a967c)`. Return value = (≥1 triangle hit).
8. Flag-bit correction to the earlier §3.5 (OBS live tables): low bits are `1` back / `2` −z side / `4` +z side
   / `8` roof (the `0x10` gate above is separate). State 2 (Out table) flags use only low bits (no `0x20`/`0x10`):
   all 32 Out sheets are soft.
9. Net state `simBall+0x3c` (OBS live: 0 in open play) and the setter path are unchanged from the earlier §3.5.
   GF counterpart: same as §9.3 — the whole `FUN_1404aa790` replaces `ApplyGoalNettingCollision`.

## 11. Player-body contact normals

### 11.1 `Q_slot` source and runtime readability (OBS)

- `FUN_140a48910(G, id)` (decompiled): `id < 0x1a` → `*(G + (id + 0xa5)·0x28)`, else a fallback record. The
  body-normal branch reads a unit quaternion `Q_slot` at slot `+0xfc` (part 3 FOOT_L), `+0x10c` (part 6
  FOOT_R), `+0xdc` (part 7 BELLY), `+0xec` (part 8 CHEST) and multiplies `q = Q_slot ⊗ q_c` (`FUN_14086d210`,
  quaternion multiply — decompile OBS, form-checked against the Hamilton product).
- `q_c` (OBS `.rdata`): parts 3/7/8 `Q_03 = (−0.5982, 0.3764, 0.5964, 0.3805) @0x142608880`; part 6
  `Q_06 = (−0.5926, 0.3746, 0.6019, 0.3823) @0x142608890`. These are exactly the `Atari_Ball_Hit_03/_06` foot
  capsule quats in the pes-re-collision capsule table (bone-frame orientations).
- OBS live (read-only, `MI = *(G+0x270)` polled until non-null — it is null most of each tick): all four slots
  hold live unit quaternions differing per player and slot (e.g. id 0: `+0xdc (−0.1222,−0.3342,0.3554,0.8643)`,
  `+0xec (−0.1103,−0.3703,0.2539,0.8867)`, `+0xfc (−0.08982,0.1999,0.2698,−0.9376)`,
  `+0x10c (0.145,−0.5211,−0.3068,0.7831)`, all `|q| = 1.0000`).
- INF(high): `Q_slot` is live orientation state (rotates the bone-frame capsule quat into the world frame).
  Readable yes, at `human(id)+0xdc/0xec/0xfc/0x10c` while `MI` is up; which bone/object it is copied from is
  **open** (resolves the earlier §8 item 3 for readability, not source).
- Rest of the normal (OBS `FUN_14086f9b0`, decompiled): `a = rotate(q,(1,0,0))`; `m = normalize(n − a(n·a))`;
  `β = acos(clamp(n·m))` in degrees (`57.2957764 @0x14259be40`); `ψ = (1 − |90−β|/90)·(90−β)` — the `c = 0.1`
  static factor of §3.3 is **absent** here (the `0.5/π` factor is the deg→rad/2 of the final rotate).

### 11.2 Capsule → GF geometry map (OBS pes-re-collision §2.1 + earlier §4.1)

- Kind 4 (`Atari_Ball_Hit_*`, live radii: thighs/heads `0.095`, belly/chest `0.16`, legs `0.06`, upper arms
  `0.055`, feet `0.0475`, GK hands `0.05`/forearms `0.04`) and kind 7 (ragdoll/root, bone via table
  `0x142b45df0` = `{0,7,11,12,13,15,16,17,9,10,1,2,3,4,5,6}`) both run the §5 response with bone coefficients
  (hard bones `E0 = 0.45 − 0.05·s, T0 = 0.75`; others `E0 = 0.4, T0 = 0.7`; `s = clamp((3.6·|v|−40)/20)`).
- GF map: GF has no capsules — `Match::CheckBallCollisions` (`match.cpp`) tests the ball against
  player-geometry AABBs with a `boundingBoxSizeOffset` fudge. A port keeps its own broadphase but must use, per
  contacted bone: the PES radius above as contact distance, the §11.1 normal for parts 3/6/7/8 (else the §3.3
  tilt with `c = 1`), and the §4.1 E/T by bone. Kind 7 records with `rec+6 == 3` are dropped (OBS dispatch).
- GF counterpart: the response core of `CheckBallCollisions` (the `bounceVec`/`bias` heuristic and
  `resultVector *= 0.7` damping) is replaced by the §5 `R()` + accumulation with the PES normal and E/T; the
  AABB gathering stays as broadphase.

## 12. When a body contact is ignored

Exactly (OBS raw dispatch + `FUN_140870a40` kick application):

1. **Kicker mute** — the kick writes `simBall+0x40 = 1`, `+0x44 = kicker id`, but **only when `id < 0x16`
   (22)** (unsigned compare; other ids leave the fields untouched). Kind 4/7 records whose `B.id == +0x44`
   are skipped while `+0x40 != 0`. The response fn decrements `+0x40` once per call at the end — so the mute
   lasts exactly **one** response pass. OBS live: after a kick `+0x40/+0x44 = (0, 18)` (consumed, last kicker
   18). GF counterpart: `match.cpp`'s `lastBodyBallCollisionTime_ms + 150 ms` global gate and the
   `lastTouchBias` heuristic are replaced by this per-kick one-pass mute keyed on the kicker id.
2. **Intended-touch suppression** — when any record has tag `rec+6 ∈ {1, 2, 6}` (`(tag−1 & 0xfa) == 0 && tag
   != 5`) or is kind 2 (sensor bone, mode 2 via `FUN_1406f02f0`), the toucher (`+0x28c` / `+0x290`) is recorded
   and `bVar8` is set; at the end `bVar8` selects the record-only path (`+0x54` touch point, no velocity
   response) over the `R()` response. Kind 7 with `rec+6 == 3` is dropped entirely. GF counterpart: the
   `TriggerControlledBallCollision` branch (`CheckBallCollisions` → designated-possession-player path) is the
   intended-touch path — gate it on the PES conditions (sensor-kind or tag 1/2/6) instead of the
   possession-player heuristic.
3. **Held ball** (`+0x293 != 0`, OBS raw): no physics — scan the 22 slots at `simBall+0x5f8 + 0x28·i` for bit 5
   of `[slot+8]` and record that player as toucher. GF has no held-ball state; add the gate.
4. **Approach gate** (unchanged, restated for the port): every used record needs `(v − vB)·n ≤ 0`; kind 14
   records need `n.y < 0.99999`; goal-vs-other nearest selection (`dG ≤ dO` keeps goal records only) per §2.
   GF counterpart: `ball.cpp`'s predict-step contact test gains these gates.

## 13. Open items

1. `Q_slot` semantic source (which bone/object quaternion is copied to `+0xdc/+0xec/+0xfc/+0x10c`): readable
   live, origin open. Next: sample the slots alongside the AnimePlayer bone world rotations in one tick.
2. `FUN_140a8f8b0` step in the soft-sheet update (§10 item 4): no-op on all replayed ticks; needs a tick where
   it moves `v` to characterise.
3. State-2 (Out table) live replay: no ball hit the outside of a net while sampling; loop decoded (§10) but not
   replayed.
4. `rec+6` tag semantics and the frame-hit event id unit: carried over from the earlier §8 item 4.
