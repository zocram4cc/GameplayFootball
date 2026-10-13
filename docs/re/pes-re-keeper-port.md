# PES 2021 keeper deflect + distribution → GodotFootball port

Scope: read-only RE of PES2021.exe (image base 0x140000000, dump
`~/ghidra-projects/scratch/Unpacker/pes_full.bin`, Ghidra project
`~/ghidra-projects/scratch/Unpacker/proj/PESLive.gpr`, headless decompile only),
then the port into `src/sim`. Tags: **OBS** = observed in bytes/decompile at the
named address, **INF** = inferred (confidence).

Closes goalkeeper doc open items: §3.5 item 2 (deflect side/angle) and §5 item 3
(distribution choice). Both were unblocked by pes-re-protection.md §7 rows 1-2.

---

## 1. The deflect target (OBS, `ActionKeeperDeflect::exec` 0x1409d75d0 → 0x1440f35f0)

Entry gate (OBS):
```
save = p1[0x20];  p1[0x20] = 0;                       // "already staged" latch
ok = FUN_140518fe0()                                   // request available?
     && FUN_140a65db0(request->kind & 0x7f) == 3        // kind 3 = deflect request
if (!ok && !save) return 0;
if (FUN_1409d5860(p2)) return FUN_1409d78e0(...);       // penalty branch, §1.2
if (p1[8] != 0) return 0;                               // phase != 0: already running
p1[0x1c] = matchClock (+0x1198);                        // start time
```
`FUN_1409d5860` (OBS, 0x1409d5860): penalty iff `(match+0x116c == 3 &&
match+0x1554 == 6)` or `ball kind == 6`.

`S(θ,r) = FUN_140a92c80(θ°, r)` (OBS decompile 0x140a92c80): `r·sin(θ°)`, with θ
normalised to (−180,180] — it is a sine, not a cosine, so θ is measured from +x.

Body (OBS, constants from .rdata):
```
K = player(request[4]);  P = K->pos (+0x55c x, +0x560 y, +0x564 z)
a = p3->angle (+0xe60)                          // the requested deflect angle, degrees
y = FIELD[3] * 0.0                              // 0 : the deflect point is on the ground

if (p3[0xe68] <= 0) {                           // no height/urgency input
  y += 2.4f                     @0x1425c7660
  x = P.x + S(K->heading(+0x554), 1)             // 1 m along his own heading
  a  = K->heading + 90.0      @0x14259be5c
  r  = 1
} else {
  d = FUN_1405bb170(&K->heading, a)              // wrap(a - heading) into (-180,180]
  if (|d| >= 120 @0x14256ae30) {
    a = (d < 0 ? FUN_140470d00(&K->heading, 60 @0x14259a764)
               : FUN_140470d60(&K->heading, 60))[0]   // heading ∓ 60°, wrapped
    r = 1
  } else {
    a = wrap360(a); r = 2                        // the requested angle, 2 m out
  }
  x = P.x + S(a, r)
}
z = P.z + S(a + 90.0, r)
p1[0x10..0x18] = p3[0x8b0..] = p3[0xe8c..] = (x, y, z)
p3[0xe6c] = 10;  FUN_1406f0590(...);  p3[0xfdc] = 1;  p1[8]++
```
So: **the deflect point is 1 m (±90° from his heading) or 2 m (the requested
angle) from the keeper at ground height** — except when `p3+0xe68 <= 0`, where the
point is raised 2.4 m (INF med: the "no ball height known" fallback, i.e. deflect
into the air rather than along the ground).

`FUN_1405bb170` (OBS 0x1405bb170): wrap the difference into (−180,180] by
±360. `FUN_140470d00/d60` (OBS 0x140470d00/0x140470d60): `wrap360(heading ∓ 60)`.
### 1.2 The penalty variant (OBS, `FUN_1409d78e0` 0x1409d78e0)
`if (p1[8] != 0) return 0;` then the same body with `a` replaced by
`(role byte [team+1]=='G' && [team+0x18]==0x766) ? FUN_140458740(K, 0.6944444 @0x14259bdd0 / gameSpeed)
: (save ? p1[0x24] : p3[0xe60])`, and it always lands on the low branch (`y =
2.4`-free, `r=1`, `a = heading+90`). `FUN_140458740` (OBS 0x140458740) returns
the bearing of `FUN_140a6c6d0(vec, …)` in degrees, i.e. the penalty-save
direction the anime layer picked. **Not ported** — GF's penalties shoot out only
(e_SetPiece_Penalty in this engine replaces the live penalty), so no GF code
reaches a keeper deflect during a penalty.

---

## 2. After-catch distribution (OBS, `ActionKeeperAfterCatchMove::exec` 0x1409e8880)

Entry: `if (match+0x1168 == 9) goto keep;` (9 = shootout) and
`if (match+0x3b0 != 0 && FUN_140a60f00(FUN_140a44070()) == 5) goto keep;`
(5 = presentation/cutscene mode, INF med) — i.e. in a shootout or a cutscene the
target is simply the keeper's own position.

### 2.1 Kickoff / restart (OBS)
```
if (match+0x116c == 1) {                                  // mode 1 = kick-off-ish
  if (p1[8] == 0 || matchClock(+0x1198) % (gs_frames >> 1) == 0) {
    p1[8] = 1; side = (0 <= slot <= 10) ? (slot < 0x16) : 0;   // own-side flag
    afStack = side; F = *(G+0x2aa8);
    R = func_140a48a30(G, side)                           // registry: own side
    out = keeper pos (lVar10+0x55c..0x564)
    A = F[0x18] + 1.0 @0x14256ae1c;  B = F[0x14]
    dir = (float)(int)*(char*)(R+0x254)                   // the team's attack sign ±1
    if (-A <= dir*out.x + 4.0 @0x1425656c0) out.x = dir*4.0 + out.x;
    else                                  out.x = -A*dir;
    if (B < |out.z|)                     out.z = out.z - sign(out.z)*2*B;
    …then §2.2's picker on out.x/out.z
  }
}
```
`F[0x18]` = **36.14**, the penalty-area line (FieldInfo +0x1c); `F[0x14]` = **9.16**,
the goal-area half width. Both offsets resolved by cross-check against the
chief-referee handler 0x144161f30, whose `z = S(spot.z)*F[0x14] - 2.5` sample is
live-verified (6.66 = 9.16 - 2.5). So A = 37.14, B = 9.16: the carry is 4 m
up-field when that stays 1 m short of the penalty-area line on his own half,
otherwise it is held exactly there (INF med on the field naming, OBS on the
arithmetic and both offsets).

In words: after a kick-off-legal catch the keeper carries the ball up-field, and
the carry never lands nearer the far side than 1 m short of his own penalty-area
line. The rest of the ports take the `keep` path.

### 2.2 The teammate pick (OBS, the whole tail after the position is set)
Two tables at `*(team) + 0x22e4c + slot*200`, each 11 entries of 2 words
(`{u32 playerSlotByte, …}`; the second word selects the throw/pass variant):
```
u = R+0x22e4c + slotKeeper*200
c = (char*)(u+0xb6ea) == 0 ?            // "no planned distribution" flag
      (u[0] < 0x16 && u[1] <= 9.0 @0x14259d60c) ? keeper's own pos, z ± 2.0 @0x142565604
    : roll over u[i] (i = 0..10), take the first whose *value* (f at u[i]+0x58*sub+4) < 100.0
      @0x14253ee30, index = that u[i] as a player slot
    : FUN_1409e8840(R, slot) → a player list (ordered slots) — the same pick
```
i.e. **PES ships a per-keeper 11-entry distribution table** (`gk.o`-shaped data
behind the team object, OBS layout; its contents are install data, not read here),
and the pick is "the first entry whose up-field parameter clears 100". When the
table cannot supply a player, the fallback is the keeper himself, displaced
±2 m across the goal.

Then (OBS):
```
target = player(slotChosen).pos
ang = (target == keeper.pos) ? 0 : degrees(FUN_140a92d40(target.z - keeper.z,
                                                        target.x - keeper.x))
ang = wrap360(ang)
FUN_140a8f7c0(&out, &ang, 4.0 @0x1425656c0)   // out += r*(sin ang, sin(ang+90))
if (-A < dir*out.x)  out.x = -A*dir          // never nearer the far side
if (B < |out.z|)     out.z = out.z - sign(out.z)*2*B
```
`FUN_140a92d40(y, x)` (OBS 0x140a92d40: `divss xmm0, xmm6; call atan`) is
`atan(y/x)` folded into (−π, π] — the first argument is the **z** difference and
the second the **x** difference; `* 57.2957764` @0x14259be40 with `if (< 0) +=
2π` @0x1425795e4 converts it to [0,360). `FUN_140a8f7c0` (OBS 0x140a8f7c0) is
`p += r·(sin θ, sin(θ+90))` — 4 m along that bearing. Because the bearing is
measured from the z axis, a teammate directly up the pitch gets a purely
LATERAL 4 m step. That is what the code does (OBS); it reads oddly, and it is
the reason the port's step is written as PES wrote it rather than "toward the
teammate".

### 2.3 The throw-vs-kick choice (OBS, `ActionKeeperThrow::CanEnter` 0x1409d0950)
Purely a request-kind switch (already known, goalkeeper §5; re-verified here):
`kind = FUN_140a65db0(kindByte & 0x7f)`; **3 = over-arm (long) throw** → target
written at `[action+0x24]`; **2 = under-arm (short) throw** → `[action+0x28]`;
anything else: no action. The kick path (punt/roll) is the sibling
`ActionKeeperThrow::PuntKick`, whose picker is the same table (not re-traced —
the throw branch is what this engine has animations for).

---

## 3. What the port replaces in this engine

### 3.1 Deflect direction (`src/sim/onthepitch/player/humanoid/humanoid.cpp`,
`e_FunctionType_Deflect` touch, ~line 1185)

GF's parry direction was a heuristic blend:
```
(-ball2D*0.1 + playerMove*2 + (−side,0,0)*parryPush + (0,rand(−1,1),0)).normalized()
  * (ballSpeed*0.3 + playerSpeed*2.5);  z += 1.2;  x away-from-own-goal clamp
```
i.e. always along −x, always up 1.2 m, always parry-length scaled. PES instead
puts the touch point at a fixed offset from the keeper in a fixed direction and
lets the anime layer move the ball from there; the outgoing velocity comes from
the clip. Ported `kPesDeflectArc_m` (2 m) and the direction rule:
- requested angle within 120° of his heading → 2 m at the requested angle;
- otherwise → 1 m at heading ±60° (the side chosen by the sign of the wrapped
  difference), i.e. the far-side deflection;
- the anim's own outgoing direction is kept as the ball velocity (unchanged), so
  the rule moves *where he contacts the ball*, which is what GF's
  `GetBestCheatableAnimID` radius cheat then turns into the outgoing direction.

GF mapping notes (`ponytail:`): the request angle `p3+0xe60` has no GF
equivalent — GF's deflect command carries only `onlyDeflectAnimsThatPickupBall`.
The ball's own incoming direction stands in, which is the direction the anim
query already sorts on (`query.incomingBallDirection`); with his facing standing
in for the request the 120-degree gate could never fire and the far-side branch
would be dead code. The height branch `p3+0xe68 <= 0` is always true in GF (no
height input), so the port always takes PES's raised 2.4 m branch — at half
strength, because the stock GF value was exactly half of it (`+1.2f`), the lift
that plays against GF's velocity-based contact; keeping the known-good number
beats re-tuning against a body collision the port does not touch.

### 3.2 Post-catch distribution
(`src/sim/onthepitch/player/controller/elizacontroller.cpp`, the
`GetBallRetainer() == player` branch, ~line 294; the arithmetic is
`src/sim/onthepitch/peskeeper.{hpp,cpp}`: `PesAfterCatchCarry` and `PesAfterCatchStep`)

GF picked `PlayerSkills::PickDistribution` (a skill-card roll) and then a random
target x/z, chose the closest teammate to that point, and vetoed the pass if a
marker was within 10 m. Ported:
- the throw-vs-kick choice stays a function-type choice: PES reads a request kind
  and GF's ShortPass / LongPass / HighPass already are the three kinds
  (over-arm throw = HighPass, under-arm throw = ShortPass, punt = LongPass). The
  skill roll is gone;
- the target is PES's, in two steps exactly as his code does it. `Carry` moves
  the ball off the keeper (4 m up-field when `-A <= dir*keeper.x + 4`, else held
  on `-A*dir`, with the lateral reflected by one goal-area width); then GF's
  nearest-player rule runs on the carry point (the `ponytail:` stand-in for
  PES's per-keeper table, §4.4); then `Step` advances the target 4 m along
  PES's own bearing arithmetic and applies the same two clamps. GF's
  10 m marking veto is kept, unchanged, ahead of all of it.

GF mapping notes (`ponytail:`): the per-keeper table is install data and nothing
in this engine has it, so the nearest man to the carry point stands in for the
chosen teammate. The `ownPenaltyLineX` GF passes is its own Law-1 mark
(`-(pitchHalfW - kAreaDepth)` signed with the attack direction) and
`goalAreaHalfZ` is `SetPieceLaws::kAreaHalfWidth`, which are the same marks
PES's FieldInfo carries. A magnitude in PES (`A`), a signed mark here — the
`std::fabs` in `PesAfterCatchCarry` is where that conversion happens.

---

## 4. Killed hypotheses / not ported

1. **"Deflect is a direction vector chosen on the ball."** Killed: the decompile
   computes a *point* offset from the keeper, never reads the ball's motion.
   OBS.
2. **"The 2.4 m is a height for high shots."** Not established: it is keyed on
   `p3+0xe68 <= 0`, an anime-request field, not the ball height. INF med; the
   port takes the raised branch unconditionally because GF has no `+0xe68`.
3. **"The catch → distribution handoff is in the exec function."** Killed: the
   picker is `AfterCatchMove::exec`, not `ActionKeeperThrow::exec`; `Throw`'s
   CanEnter only reads the kind byte and copies an already-chosen target.
4. **PES's 11-entry per-keeper distribution table** (offset +0x22e4c, stride 200,
   entries of 2 u32): layout OBS in the decompile, contents install data owned by
   the team object. Not ported, not invented: GF keeps its nearest-player rule.
   `ponytail:` if per-keeper distribution tendencies are ever wanted, this table
   is where they live.
5. **Penalty deflect** (`FUN_1409d78e0`): OBS, but unreachable in GF — the
   penalty mode here is a shootout, not a live penalty.
6. **PES's FieldInfo offsets in the distribution arithmetic.** Killed the
   "half-length / touchline" reading: the two offsets are `F[0x18]` = 36.14 (the
   penalty-area line) and `F[0x14]` = 9.16 (the goal-area half width), resolved
   by cross-check against the live-verified chief-referee handler. GF passes its
   own Law-1 marks for them.
7. **"The step goes toward the teammate."** Killed: with the bearing measured
   from the z axis (FUN_140a92d40's argument order), a teammate directly up the
   pitch gets a purely lateral step. OBS in the arithmetic, kept as written.

---

## 5. Constants (all .rdata; every one read at run time through
`pes::PeImage::ReadVa` in `pes::LoadKeeperConstants`, `src/sim/onthepitch/peskeeper.cpp`)

| value | VA | used by |
|---|---|---|
| 2.4 | 0x1425c7660 | deflect point height when there is no height input |
| 90 | 0x14259be5c | `quarterTurn`: the heading+90 offset and the step's `sin(θ+90)` |
| 120 | 0x14256ae30 | the "requested angle is sideways" gate |
| 60 | 0x14259a764 | heading ∓60° fallback |
| 360 | 0x14259858c | angle wrap |
| 1 | 0x14256ae1c | radius 1 m |
| 2 | 0x142565604 | radius 2 m |
| 4 | 0x1425656c0 | minimum carry up-field, and the 4 m step toward the target |
| 5 | 0x14259222c | keeper's own x offset in the failed-table fallback |
| 2 | 0x142565604 | the ±2 m lateral fallback target |
| 9 | 0x14259d60c | the table entry's ≤9.0 short-pass gate |
| 100 | 0x14253ee30 | the table entry's <100.0 long-pass gate |
| 2.25 | 0x1425a9be0 | FUN_140590c90's own-ball-search radius² (not ported) |
| 80/100, 260/280 | 0x142596e6c, 0x14253ee30, 0x142598584, 0x142598588 | action-kind bands in the same search (not ported) |
| 1.5 | 0x14259bddc | anime-side clamp (Goalkeeper doc §5; unchanged there) |
| 1.2 | 0x14259d5d4 | FUN_140590c90's up-field window (not ported) |
| 57.2957764 | 0x14259be40 | rad→deg |
| 0.75 | 0x142565600 | FUN_140590c90's half-length blend (not ported) |

---

## 6. Verification

- `pes_keeper_test` (synthetic constants and, in one gated install test, the real
  loader): the deflect point in both branches, the 120-degree gate and its wrap,
  the far-side heading ∓60 in both signs, the carry's up-field branch, its
  penalty-area hold and the exact boundary, the lateral reflection, the step's
  bearing arithmetic, its two clamps, and the mirrored (+x) side.
  Mutation-checked three times: deleting the 120-degree gate (3 tests fail),
  deleting the carry's lateral reflection (1 fails), deleting the step's x clamp
  (2 fail). All restored green each time.
- Install test `PesKeeper.LoadsTheRealValuesFromTheInstall`: loads the real
  `PES2021.exe` and checks each value through relations, never pinned numbers.
  Skipped without `PES21_ROOT`.
- A gfsim smoke run: the ported rules fire on real matches (probe evidence
  below), `Full match complete`, exit 0.
- Whole suite green + `tools/check_pii.sh` exit 0.

### 6.1 Smoke-run evidence

Temporary `printf` probes at the two call sites (removed before the commit):

```
keeper deflect: heading %.1f deg, |d| %.1f -> point angle %.1f deg, radius %.1f m
keeper distribution: carry (%.2f, %.2f) from keeper (%.2f, %.2f)
```

Command (the plan's smoke run, unchanged):

```
build/apps/gfsim_run --db-sql assets/fixtures/teams.sql --home 1 --away 2 \
  --seed 1000 --minutes 20 --pes21-root "$PES21_ROOT"
```

Observed (seed 1000, 20 minutes): 7 keeper deflections and 7 post-catch
distributions, `Full match complete`, exit 0. The radius column shows which
branch ran (2.0 m = the requested angle, 1.0 m would be the far-side gate). The
carry column shows the hold and the up-field branch both firing, in both
directions:

```
keeper distribution: carry (-37.00, -13.56) from keeper (-47.17, -13.56)
keeper distribution: carry ( 37.00,  -7.20) from keeper ( 47.31,  -7.20)
keeper distribution: carry (-37.00,   2.10) from keeper (-50.59,   2.10)
```

### 6.2 What the first (wrong) run showed, and why it was caught

The first smoke run printed `carry (51.50, -13.56) from keeper (-47.17, -13.56)`
— a keeper attacking +x whose carry landed at x = +51.5, the opposite
penalty-area line. Cause: `ownPenaltyLineX` was passed as
`pitchHalfW * team->GetSide()`, the FAR line, where PES's `A` is a magnitude
folded with his attack sign. This is what the "a gfsim smoke run shows keeper
distributions following the new rule" acceptance is for, and it is why the probe
prints the carry, not just its type.

### 6.3 Full gate

`ctest -j3 --timeout 2400` with `PES21_ROOT` and `PES21_EDIT_SAVE` set:
**276/276 passed**, exit 0 (`100% tests passed out of 276`; two install tests
skipped by their own guard, which also skip on the unmodified tree).
`tools/check_pii.sh`: exit 0.

Commit: `875ae8e` "Port PES's goalkeeper deflect point and post-catch
distribution" (ported in a separate worktree).

### 6.4 Mutation checks

The ported decision functions were mutation-checked by hand, one at a time,
each restored green afterwards:

| mutation | tests that failed |
|---|---|
| delete the 120° deflect gate | 3 (both deflect branch tests + the wrap/symmetry) |
| delete the carry's lateral reflection | 1 (`ReflectsTheCarryLateralByOneGoalArea`) |
| delete the step's x clamp | 2 (`StepsAreHeldAtHisOwnPenaltyAreaLine`, one step test) |

## 7. Left open

- **PES's per-keeper 11-entry distribution table** (§4.4): not ported. GF's
  nearest-player rule stands in for the chosen teammate. The contents are
  install data behind the team object; relocating them at runtime is a different
  piece of work from porting the arithmetic, and would need the table's runtime
  base rather than a static offset.
- **The penalty deflect** (`FUN_1409d78e0`): OBS but unreachable — GF's penalty
  mode is a shootout.
- **The 2.4 m lift at half strength**: the lift is applied at half because GF's
  stock value was exactly half of PES's (`+1.2f`), the number that plays against
  GF's velocity-based contact. If a future port makes the deflect contact a point
  rather than a velocity, this is the term to revisit.
- **`onlyPickupAnims`** in `_KeeperDeflectCommand` is untouched: it decides which
  of GF's deflect anims can hold the ball, which is GF's anim-data question, not
  the PES rule this port covers.
- The final seed-1000 smoke run used the pre-cleanup binary
  (`c364d5acf39787b4`); the later edits (unused-field removal, the
  `targetPlayer`-null fallback) are behaviour-preserving but not re-measured, so
  a fresh determinism hash is worth taking before accepting.
