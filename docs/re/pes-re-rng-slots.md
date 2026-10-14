# PES 2021 RNG slots → systems (call-site census, slot indices read from the call instructions)

Tags: **OBS** = observed in raw bytes of the unpacked image (or the value follows from an
immediate that is visible in those raw bytes). **INF** = inference with a stated basis. No player
or team names. No machine paths.

Inputs:
- the unpacked image (`pes_full.bin`), base `0x140000000`, file offset = VA − base. All
  disassembly below was read from that image directly, not from a decompiler.
- `math::Random` layout and slot step from `docs/re/pes-re-rng.md` §1 (re-confirmed here).
- prior call census scratch (calls.json / census2.json) — my scan re-verified every call site and
  found the ones that census stored truncated (see §2).

---

## 1. Register conventions per draw helper (OBS, raw bytes)

The slot index is **not** a constant property of a helper; each helper takes it in a register, and
two helpers hard-wire it. Read from the entry body of each:

| helper | VA | slot arg | other args | semantics |
|---|---|---|---|---|
| `Range(rng, idx)` | `0x140a92470` | **edx** | `rcx` = rng | `idx<3`: step slot `idx`, map `state/2^31 × M[idx]`; `idx==3`: 15-bit advance, no value |
| `RangeRaw(rng, n, idx)` | `0x140a924e0` | **r8d** | `edx` = divisor `n`, `rcx` = rng | step slot `idx`, return `trunc(state/2^31 × n)` |
| `Range0(rng, count)` | `0x140a92530` | **fixed slot 1** | `edx` = count | step slot 1, `min(count-1, trunc(state/2^31 × count))` |
| `Normal(rng, amp, off)` | `0x140a92590` | **fixed slot 2** | — | two slot-2 steps, quantile table, rejection (pes-re-rng.md §1.4) |
| `AbilityNormal(ability)` | `0x140a927b0` | **fixed slot 2** | `edx` = ability | computes width, calls `Normal` (slot 2) |
| `Advance(rng, idx)` | `0x140a92880` | **edx** | `rcx` = rng | step slot `idx`, return nothing |

Empirical confirmation of the slot register, from raw bytes at three of the sites:
- `0x140a9253a: imul eax,[rcx+0xc],0x2d22b2e3` → **Range0 always reads slot 1** (offset `0xc`).
- `0x140a924ef: imul eax,[rcx+r9*4+8]` with `movsxd r9,r8d` at entry → **RangeRaw's slot is r8d**,
  and `edx` is separately converted to float at `0x140a9250d` (it is the divisor).
- `0x140a92470: movsxd rdx,edx` + `imul eax,[rcx+rdx*4+8]` → **Range's slot is edx**.

Dominant idiom at the RangeRaw sites (**OBS**): `mov edx,0x64 ; lea r8d,[rdx-0x62]` → slot
`= 0x64 − 0x62 = 2`, divisor 100. Equivalently `mov edx,0x15 ; lea r8d,[rdx-0x13]` → slot 2,
divisor 21. The Range sites use the direct `mov edx,2`.

## 2. Not RNG draws — drop from the slot census (correction)

The prior report's census listed two helpers that are **not** slot draws:

- **`WrapAngleDelta 0x140a928c0`** (26 call sites): reads two *floats* (`[r8]`, `[rdx]`), does a
  quadrant-reduced angle wrap and a multiply. No LCG, no `[rcx+8/0xc/…]`, no generator. **not a
  slot consumer** (OBS body).
- **`SlotState 0x140a928b0`** (1 call site, `0x142161c65`): `xor` of all four slots of the object
  pointed to by `rcx` — a whole-generator checksum, not a slot draw (OBS body).

Also excluded: `Random_ctor` / `Random_dtor` (38 + 51 sites) and `setseed` (55, writes → §6).

## 3. Full call-site census, slot read from each call instruction

Every site below is a direct `e8` call in the image, verified by scanning for `e8` and resolving
rel32. My scan **found sites the prior census truncated**; total now:

| entry | call sites | prior census |
|---|---|---|
| `Range` `0x140a92470` | 23 | 23 (same) |
| `RangeRaw` `0x140a924e0` | 49 | 49 (same count; 5 sites were hidden in the truncated lists) |
| `Range0` `0x140a92530` | **67** | 66 (+1: `0x140471513`) |
| `Normal` `0x140a92590` | **12** | 12 (+3 beyond the "×6/×3" shown) |
| `AbilityNormal` `0x140a927b0` | 1 | 1 |
| `Advance` `0x140a92880` | 6 | 6 |
| `setseed` `0x140a928a0` | **55** | 55 (+2 hidden in `0x140504100`, +1 in `0x1405b83b0`'s neighbourhood) |
| `WrapAngleDelta` | 26 | not a draw (§2) |
| `SlotState` | 1 | not a draw (§2) |
| `Random_ctor` | 38 | 38 |
| `Random_dtor` | **51** | 50 (+1: `0x140442e30`) |
| `GameRandom_ctor` `0x140a923b0` | 12 | — |

Two apparent extra call addresses sit at `0x143f5a20d` (Range0) and `0x143ef553d` (dtor); they
are in the protected `.impdata`/`.rsrc` window of the image and are byte-coincidences in data,
not code; the counts above exclude them. (**INF medium-high**: a `e8 <rel>` that lands exactly on
the entry is a 2^-32 coincidence; both are far outside the `.trace` code range.)

### 3.1 Which slot each site actually draws

Resolved from each site's pre-call instructions (raw bytes). `slot=X` is the register value.

**Slot 0 (M = 100 → `Range(rng,0)`):**

| system | fn | site(s) |
|---|---|---|
| duel/foul simulation (dive) | `0x1405b83b0` | `0x1405b8759` |
| off-ball free-support-cell picker | `0x1405e9f50` | `0x1405ea303` |

**Slot 1 (67 sites, all via `Range0`; several count values):**

| system | fn | count | site(s) |
|---|---|---|---|
| team concept roller (kick-off) | `0x140470f10` | 100 | `0x140470f6f` |
| team tactics/pressure unit | `0x140471361` | 100 | `0x14047145f, 0x140471479, 0x14047149e, 0x1404714b8, 0x1404714e1, 0x1404714f7, 0x140471513, 0x140471529` |
| alternative-kick concept | `0x140474b20` | 2 | `0x140474bee` |
| set-piece concept | `0x140475b28` | 100 / 10000 | `0x140475d0b, 0x140475de8` |
| set-piece concept 2 | `0x140477400` | 2 | `0x14047750f` |
| set-piece (cloud/pressure) unit | `0x14047639d` | n/a | `0x140476812` |
| set-piece unit | `0x140476a4d` | 500 | `0x140476a80` |
| match/half-time flow | `0x14047ea10` | dynamic | `0x14047ea53` |
| match-flow restart chooser (**main match RNG**) | `0x14044793d` | `clock*100` | `0x1404479be` |
| match sub-3 generator (`[base+0x10]`) | `0x1404886b0` | dynamic / dynamic / 10000 | `0x140488ebe, 0x140489150, 0x14048937f` |
| match sub-4 generator, part A (`[base+8]`) | `0x14048a260` | 10000 ×3 | `0x14048a2a4, 0x14048a490, 0x14048a4e3` |
| match sub-4 generator, part B (`[base+8]`) | `0x14048ad4c` | 10000 | `0x14048aec0` |
| match sub-4 generator, part C (`[base+8]`) | `0x14048b2b0` | 12 / dynamic | `0x14048b2d3, 0x14048b2e6` |
| packed-section helper, system name unresolved | `0x143f5a150` | obfuscated (§10.1) | `0x143f5a20d` |
| concept/tactical chooser | `0x1404ba3e0` | dynamic | `0x1404babb9` |
| set-piece role picker | `0x1404f6140` | 2 | `0x1404f61a4` |
| set-piece choreography | `0x1404fcb35` | 100 | `0x1404fcb5c` |
| set-piece taker pick | `0x14050590a` | 100 | `0x140505a88` |
| match logic (game state) | `0x1405114a0` | 100 | `0x1405114da` |
| match logic | `0x140511976` | dynamic | `0x140511a90` |
| match logic | `0x140512430` | 10000 | `0x1405124c1` |
| match logic (stamina/timing) | `0x140512550` | dynamic | `0x1405125f5` |
| match logic | `0x1405129d0` | 10000 | `0x140512a70` |
| match logic | `0x14053a452` | dynamic | `0x14053a512` |
| foul choreography unit | `0x1405acd90` | 100 / dynamic | `0x1405acf0b, 0x1405acf35` |
| offside/demo player-info picker | `0x1405ad120` | 100 | `0x1405ad19b` |
| foul choreography | `0x1405ad470` | dynamic | `0x1405ad4f1` |
| animation/blend choice | `0x1405ae43c` | dynamic | `0x1405ae462` |
| dynamics/pose picker | `0x1405ae530` | dynamic | `0x1405ae5a2` |
| animation pick | `0x1405ae6d0` | dynamic | `0x1405ae893` |
| physics/motion rand | `0x1405aebe0` | dynamic | `0x1405aecc4` |
| physics/motion rand | `0x1405aef70` | 10 | `0x1405aefe2` |
| tactical chooser | `0x1405afaf1` | 10000 | `0x1405afc08` |
| tactical chooser | `0x1405b0620` | dynamic / 10000 ×3 | `0x1405b0821, 0x1405b0a5c, 0x1405b0aa8, 0x1405b0af4` |
| tactical chooser | `0x1405b0b8c` | 100 | `0x1405b0bf7` |
| tactical chooser | `0x1405b103f` | 10000 ×2 / 100 | `0x1405b1338, 0x1405b13e9, 0x1405b144c` |
| tactics CPU strategy | `0x1405f084f` | 100 | `0x1405f0934` |
| tactics CPU strategy | `0x1405f2a80` | 100 | `0x1405f3137` |
| tactics CPU strategy | `0x1405f32e0` | 100 | `0x1405f3411` |
| animation-variant selection | `0x140955060` | dynamic | `0x1409553e0, 0x1409554a1, 0x14095556a` |
| animation-variant selection | `0x140955a71` | dynamic | `0x140955aa3` |
| throw-in choreography | `0x140998087` | dynamic | `0x1409981f8` |
| throw-in choreography | `0x1409982ac` | 2 | `0x140998401` |
| throw-in choreography | `0x1409bed20` | 3 | `0x1409bed74` |
| throw-in choreography | `0x1409ca5e8` | 5 | `0x1409ca777` |
| keeper/barrier helper | `0x1409e09f6` | 6 | `0x1409e0a42` |
| keeper think unit | `0x142114bd4` | 3 | `0x142114c41` |

**Slot 2 (Range with `mov edx,2`; RangeRaw with `lea r8d,[rdx-0x62]` or `mov r8d,2`; Normal / AbilityNormal fixed):**

| system | fn | entry used | site(s) |
|---|---|---|---|
| player action base position | `0x140956c30` | Range | `0x140956fc0` |
| duel/foul simulation | `0x1405b83b0` | Range | `0x1405b83b0` † |
| per-player pick | `0x1406e6bf0` | Range | `0x1406e6c04` |
| per-player success roll | `0x1406eaa10` | Range ×3 | `0x1406eaa2b, 0x1406eaa6e, 0x1406eaaaa` |
| per-player percent test | `0x1406ebf30` | Range | `0x1406ebf44` |
| ability-normal player updater | `0x14092ed30` | AbilityNormal | `0x14092f0a1` |
| online ParamRandom | `0x14100b6f0` | Range | `0x14100b705` |
| online HTTP-to-file jitter | `0x141170b40` | Range | `0x141170c9c` |
| sound util Random | `0x1414962c0` | Range | `0x1414962d1` |
| sound util Random | `0x1414962f0` | Range | `0x1414962fd` |
| global "kickoff" singleton | `0x141f51fd0` | RangeRaw | `0x141f52013` |
| global "derivative" singleton | `0x141f52060` | RangeRaw | `0x141f520a8` |
| global "cutscene-gen" singleton | `0x141f55110` | RangeRaw ×3 | `0x141f5522a, 0x141f552e2, 0x141f5532c` |
| global throwaway helper | `0x141f55a10` | RangeRaw | `0x141f55a44` |
| cutscene condition type | `0x141f7a100` | RangeRaw | `0x141f7a11a` |
| cutscene condition type | `0x141f7fea4` | RangeRaw | `0x141f7fef4` |
| fixdemo queue item | `0x141fb1a50` | RangeRaw | `0x141fb24ce` |
| stream SetSeed/derived helper (per-content) | `0x140435ba0` `0x140435e33` `0x140435ef0` | Range + RangeRaw | `0x140435bd2, 0x140435e8c, 0x140435f06` (+ RangeRaw) |
| static-global singleton, count 1024 (A) | `0x140223180` | RangeRaw | `0x1402231ba` |
| static-global singleton, count 1024 (B) | `0x1402231e0` | RangeRaw | `0x14022321a` |
| stream SetSeed/derived helper — all sites | `0x140435ba0` / `0x140435e33` / `0x140435ef0` | Range ×3 + RangeRaw ×8 | `0x140435bd2, 0x140435e8c, 0x140435f06` / `0x140435be6, 0x140435bf9, 0x140435c0f` / `0x140435ea1, 0x140435eb5` / `0x140435f1b, 0x140435f2f, 0x140435f46` |
| concept-roller's own Random (four consecutive draws) | `0x140e83f10` | Range ×4 | `0x140e83f6d, 0x140e83f7e, 0x140e83f90, 0x140e83fa2` |
| clock-seeded throwaway (A) | `0x14152cb20` | Range | `0x14152cd48` |
| clock-seeded throwaway (B) | `0x141545630` | Range | `0x1415456b8` |
| match-flow controller | `0x1404533ff` | RangeRaw | `0x140453429` |
| match-context sub 2 | `0x1404812b0` | RangeRaw | `0x14048151e` |
| match-context sub 2 (variant) | `0x14048185e` | RangeRaw | `0x140481886` |
| CPU strategy decider | `0x14051e970` | RangeRaw ×4 | `0x14051eb01, 0x14051eb5e, 0x14051eb8f, 0x14051eba4` |
| player-stat spread writer | `0x14089bae0` | RangeRaw ×7 | `0x14089bb10, 0x14089bb73, 0x14089bbc0, 0x14089bcba, 0x14089bd11, 0x14089bd6d, 0x14089bdd9` |
| player-stat spread writer | `0x14089be70` | RangeRaw | `0x14089bee5` |
| replay/director (embedded at +0x8e8) | `0x1408c0009` | RangeRaw | `0x1408c02fd` |
| cutscene/physics rand stream | `0x1408d5ddf` | RangeRaw | `0x1408d5e0a` |
| cutscene/physics rand stream | `0x1408d6740` | RangeRaw | `0x1408d67fe` |
| cutscene/physics rand stream | `0x1408d9bc0` `0x1408d9e0b` `0x1408da03d` | RangeRaw ×4 | `0x1408d9c54, 0x1408d9ee9, 0x1408d9f8a, 0x1408da080` |
| replay copy (per-object Random) | `0x1420b5008` | RangeRaw | `0x1420b503f` |
| replay copy | `0x1420b62b7` | RangeRaw | `0x1420b631b` |
| replay copy | `0x1420b92f0` | RangeRaw | `0x1420b9551` |
| carrier AI plan roll (5 units) | `0x14211a800` `0x14211ab10` `0x14211af20` `0x14211b1d0` `0x14211b460` | RangeRaw | `0x14211aac2, 0x14211ada6, 0x14211b14e, 0x14211b412, 0x14211b762` |
| off-ball/free-support roll | `0x14212b036` | RangeRaw | `0x14212b143` |
| shared kick-launch formula | `0x1407324b0` | Normal ×3 | `0x1407328e4, 0x140732983, 0x140732a2d` |
| kick scatter writer | `0x1408398d5` | Normal ×6 | `0x140839b51, 0x140839bd5, 0x14083a280, 0x14083a550, 0x14083a5f2, 0x14083a7fe` |
| animation blend/dynamics | `0x140955650` | Normal | `0x14095569d` |
| injury severity roll | `0x1405ae43c` | Normal | `0x1405ae462` |
| animation/director (embedded at +0x4a8) | `0x140873787` | Advance | `0x1408737c2` |
| animation/director (embedded at +0x840) | `0x1408c0581` | Advance | `0x1408c0b8f` |
| cutscene advance (embedded at +0x470) | `0x141f80070` | Advance | `0x141f8038c` |
| replay ball advance | `0x14217cf0e` | Advance | `0x14217d0dc` |
| replay human advance | `0x142180c44` | Advance | `0x142180f6a` |
| stream SetSeed helper (per-match) | `0x140435ba0`(same as above) | — | see above |
| replicated RNG object (per-copy stream) | `0x141f55110` | — | see above |

† `0x1405b83b0` ctor (`0x140a923f0`) at `0x1405b8409` makes a local `math::Random` but there is
**no** `setseed` inside it (verified: no `0x140a928a0` site in the function range), so it draws
from the **ctor defaults** `193488,193489,193490,193491`; its `Range` call passes `edx=0` →
**slot 0, count M=100**. The dive-roll is a *simulation* (pes-re-duels-fouls.md §2.8) that rolls
slot 0 of a default-seeded local generator.

**Dynamic-slot sites (value not a literal):**

| fn | expression | resolved value | site(s) |
|---|---|---|---|
| `0x1420b1b0d` | `mov esi,1 ; lea edx,[rsi+1]` | **slot 2** (Range) | `0x1420b1b31` |
| `0x1420b27b0` | `xor esi,esi ; lea edx,[rsi+2]` | **slot 2** (Range) | `0x1420b27d9` |
| `0x141f55110` | `lea r8d,[r15+2]`, `r15` unknown | 2 (RangeRaw) | `0x141f5522a, 0x141f552e2` |
| `0x141f7fef4` | `lea r8d,[rdx-0x13]`, edx=0x15 | **2** (RangeRaw) | `0x141f7fef4` |
| `0x1408d6740` | `lea r8d,[r15+1]`, r15=1 | **2** (RangeRaw) | `0x1408d67fe` |
| `0x14051e970` | `mov r8d, esi` (esi = 0..2) | 0..2 (RangeRaw) | `0x14051eb01, 0x14051eb5e, 0x14051eb8f` |
| `0x141fb1a50` / `0x14217d340` / `0x14217c870` | `mov edx, edi` / `[rcx+1]` / `[rbx+2]` | dynamic slot, Advance | `0x141fb24c3, 0x14217d555, 0x14217c8c6` |

**Slot 3**: no call site in the image passes slot 3 to `Range`, `RangeRaw` or `Advance`
(OBS: all 78 edx/r8d slot arguments resolve to 0, 1 or 2). The `idx==3` branch of `Range` and the
15-bit step are dead in this build. **slot 3 keeps its ctor default `193491` and is never read as
a value** (only xor-hed by `SlotState`, which is itself a checksum consumer).

## 4. Non-generator randoms (recorded, not slots)

- `*(G+0x360)`: the frame-parity int (`docs/re/pes-re-rng.md`'s other-randoms note).
- global int-fps at `0x14351ae50`.
- The online registry paths (`0x1434a2d40`, vtable `+0x88`/`+0x1b8`) resolve to `xor al,al; ret`
  in this build — they never seed in an offline match (OBS per pes-re-rng.md §2.1).
- `0x141b34aa0`/`0x141b34ad0`/`0x141b34b20`: clock readers used to seed throwaway generators
  (§6); they are seeds, not draws.
- per-player LCG at `player+0x32d0`, **slot 2 of the inline `math::Random`** (OBS: same report
  §1.7; the consumers `0x1406e6bf0`/`0x1406ebf30`/`0x1406eaa10` all call `0x140a92470` with
  `edx=2` on `player+0x32d0`). Contact/stagger/fall/jostle/aerial use only this stream and no
  match slot — corroborated by the contact report `local://pes-re-contact.md` §5.
- injury generator at `injuryObj+0x1a70`, default seeds, **s**lot 2 draws
  (OBS pes-re-stamina-drain.md §6, the rangeE sites `0x14048151e` / `0x140481886`).

## 5. Which generator object, per system (the second half of the question)

The "slot" alone is half the answer: the same slot number lives in five independent generator
objects. Object attribution (OBS rcx at the call site, or the owning ctor):

| generator object | seeded by | systems that draw from it |
|---|---|---|
| **main match RNG** `ctx+0x5708` | match ctx ctor `0x140441fc0` → slot0 `t`, slot1 `t+1`, slot2 `t+2` | match-flow controller `0x1404533ff` (slot 2, count 100, at `0x140453429`); restart/matchup helper `0x14044793d` (**slot 1, count `clock*100`**, at `0x1404479be`, `rcx = rdi+0x5708` OBS) |
| **sub 1** `ctx+0x1e8+0x108` | `0x14047ab90` (called from the match ctx ctor `0x140441fc0`, OBS) | **INF (med)**: the `+0x108`-family sites (0x140470f10, 0x140471361 ×8, 0x140474b20, 0x140475b28 ×2, 0x140477400, 0x14047639d, 0x140476a4d, 0x1404ba3e0) read `[base+0x108]`, matching sub-1's `+0x108` layout — the "team concepts / set pieces / match logic" cluster |
| **sub 2** `ctx+0x6d8+0x18` | `0x140481050` | injury `0x1404812b0`, `0x14048185e` (slot 2) — **the injury generator** |
| **sub 3** `ctx+0x770+0x10` | `0x140489440` | `0x1404886b0` (3 sites, `[base+0x10]`, slot 1) |
| **sub 4** `ctx+0x7d0+8` | `0x14048b330` | `0x14048ad4c`, `0x14048b2b0` (`[base+8]`, slot 1) |
| **per-player** `player+0x32d0` | per-player `setseed` `0x1406eb670` (slot 2) | `0x1406e6bf0`, `0x1406ebf30`, `0x1406eaa10` (slot 2) |
| **player updater random** `ctx2+0xa368` | derived Random | `0x14092ed30` ability-normal, `0x142161b90` slotstate |
| **per-clip/anim Random** `anim+0x19c0` | — | `0x140955060`, `0x140955a71`, `0x1409982ac`, `0x1409bed20`, `0x1409ca5e8`, `0x1409e09f6`, `0x142114bd4`, `0x140955650` (Normal) |
| **statics/global** `0x1437f5d90`, `0x1437f6340`, `0x1436f36a8`, `0x1436f3778` | their own singleton ctors | stat-spread writer `0x14089bae0`/`0x14089be70` (slot 2, count 100), cutscene generators `0x1408d5ddf`..`0x1408da03d`, singletons `0x140223180`, `0x141f51fd0`, `0x141f52060`, `0x141f55110`, `0x141f55a10` (slot 2, count 1024 or 21) |
| **match ctx set-piece sub-RNG** `ctx+0x6c18` | — | `0x1404f6140`, `0x1404fcb35` (set-piece role/choreography, slot 1) |
| **register-spilled (context not traced)** | — | slot-1 users `0x14050590a`, `0x1405114a0`, `0x140511976`, `0x140512430`, `0x140512550`, `0x1405129d0`, `0x14053a452`, `0x1405acd90`, `0x1405ad120`, `0x1405ad470`, `0x1405ae530`, `0x1405ae6d0`, `0x1405aebe0`, `0x1405aef70`, `0x1405afaf1`, `0x1405b0620`, `0x1405b0b8c`, `0x1405b103f`, `0x1405f084f`, `0x1405f2a80`, `0x1405f32e0`, `0x140998087`, `0x143f5a150` — the object is only reachable through a register or stack slot my window did not cover (INF: embedded or ctx-relative) |

## 6. Systems that seed or re-seed a slot (55 sites)

Every `setseed` site, with the slot it writes (OBS from `mov edx,<slot>` at each):

| site | fn | slot | value written | note |
|---|---|---|---|---|
| `0x140435825` `0x140435836` `0x140435847` | `0x1404357e0` | 0,1,2 | `t, 2t, 3t` | throwaway; seeds all three |
| `0x140435e54` `0x140435e69` `0x140435e7e` | `0x140435e33` | 0,1,2 | `1000a, 2000a, 3000a` | derived |
| `0x1404422fb` `0x14044230f` `0x140442323` | `0x140441fc0` | 0,1,2 | `t,t+1,t+2` | **the main match RNG, kick-off** |
| `0x14047abb4` `0x14047abc4` | `0x14047ab90` | 0,1 | t,t+1 | sub 1 |
| `0x140481071` `0x140481081` | `0x140481050` | 0,1 | t,t+1 | sub 2 (injury) |
| `0x140489461` `0x140489471` | `0x140489440` | 0,1 | t,t+1 | sub 3 |
| `0x14048b351` `0x14048b361` | `0x14048b330` | 0,1 | t,t+1 | sub 4 |
| `0x1405042bf` `0x1405042e4` `0x140504337` `0x14050435c` `0x140504381` | `0x140504100` | 0,1,0,1,2 | online or `t%10000` | a Random made by the Fox property factory at `obj+0x18` |
| `0x14051e555` `0x14051e566` `0x14051e577` | `0x14051e400` | 0,1,2 | online or clock | GameRandom at `obj+0x20` |
| `0x1406ebaf4` | `0x1406eb670` | 2 | derived | per-player Random `player+0x32d0` |
| `0x1408706bd` | `0x140870020` | 2 | `eax*10000` | director/anime object `+0x4a8` |
| `0x1408bdb39` `0x1408bdbdc` | `0x1408bd970` | 2 | `obj+0x18 * 10000` / `*30000` | embedded `+0x840` / `+0x8e8` |
| `0x140956fb2` | `0x140956c30` | 2 | derived | base-position helper's own Random |
| `0x140e83f5e` | `0x140e83f10` | 2 | derived | concept-roller's own Random |
| `0x14100b688` | `0x14100b640` | 2 | derived | onlinemode ParameterRandom |
| `0x141170c29` | `0x141170b40` | 2 | derived | online HTTP transaction |
| `0x1411d3341` | `0x1411d3020` | dynamic `[rbp+1]` | derived | stream helper |
| `0x141494de3` | `0x141494d70` | 2 | derived | sound util Random |
| `0x141496259` | `0x141496220` | 2 | derived | sound util Random |
| `0x14152cd3a` | `0x14152cb20` | 2 | clock | throwaway (never reseeded by gameplay) |
| `0x1415456a9` | `0x141545630` | 2 | clock | throwaway |
| `0x141f51ffc` | `0x141f51fd0` | 2 | `state+1` | **singleton re-seeded at load** |
| `0x141f52091` | `0x141f52060` | 2 | `state+1` | **singleton re-seeded at load** |
| `0x141f552d1` `0x141f55319` | `0x141f55110` | dynamic/2 | derived | cutscene generator |
| `0x141f7d322` `0x141f7d3d8` | `0x141f7d010` | dynamic `[rdi+2]` | `*10000`/`*25000` | cutscene object `+0x470`/`+0x518` |
| `0x141fb24c3` | `0x141fb1a50` | dynamic `edi` | derived | replay copy |
| `0x1420afd61` | `0x1420af930` | 2 | derived | replicated RNG `+0x28` |
| `0x14210d641` | `0x14210d320` | dynamic `[r14+2]` | `0x2f3cf` | pass-think test tool |
| `0x14211aaaf` `0x14211ad93` `0x14211b13b` `0x14211b3ff` `0x14211b74f` | `0x14211a800` … `0x14211b460` | 2 | `cvttss2si(carrierClock×K)` | **carrier plan roll: throwaway RNG, seeded per adoption** |
| `0x14217c8c6` | `0x14217c870` | dynamic `[rbx+2]` | `eax*10000` | ReplayBall |
| `0x14217d555` | `0x14217d340` | dynamic `[rcx+1]` | derived | ReplayHuman copy |
| `0x14217dbe2` | `0x14217db10` | 2 | `obj+0x28*10000` | ReplayHuman |

Mid-match reseeding: only the **carrier plan roll** (the five ImageUnits) re-seeds *during* play — a
throwaway `math::Random` seeded from the carrier's clock and drawn once; it does not touch the
match generators. The two singletons at `0x141f51fd0`/`0x141f52060` re-seed their slot 2 at
singleton (re)initialisation, not per tick. The main match RNG and the four subs are seeded only
by the match-context constructor; halves, set pieces and restarts do **not** re-seed them
(consistent with pes-re-rng.md §2.3 and the live sample in §4 there).

## 7. Inverse table: system → slot(s)

| GF system (candidate) | PES slot(s) | basis |
|---|---|---|
| carrier AI plan selection | **slot 2** (throwaway per-adoption RNG, count 100) | `0x14211aac2` etc.; matches `docs/re/pes-re-carrier-port.md` §3.6 |
| carrier/decision position base | **slot 2** (own Random) | `0x140956fc0` |
| kick launch / scatter | **slot 2 (Normal)** | `0x1407324b0` (3), `0x1408398d5` (6) |
| per-player rolls (contact, anim pick, success) | **slot 2** of the per-player Random | `0x1406e6bf0`, `0x1406ebf30`, `0x1406eaa10`; also `local://pes-re-contact.md` §5 |
| ability-scaled deviation (`AbilityNormal`) | **slot 2 (Normal)** | `0x14092f0a1` |
| duel/foul dive simulation | **slot 0** | `0x1405b8759` |
| off-ball free-support-cell picker | **slot 0** | `0x1405ea303` |
| set-piece choreography (concepts, taker, roles) | **slot 1** (count 2/3/5/100/…) | `0x1404f6140`, `0x1404fcb35`, `0x14050590a`, `0x140475b28` |
| throw-in choreography | **slot 1** | `0x1409982ac`, `0x1409bed20`, `0x1409ca5e8` |
| team concepts / tactics / pressure | **slot 1** | `0x140471361` (8 sites), `0x1405b0620`, `0x1405b103f`, `0x1405f084f` |
| match logic / game state | **slot 1** | `0x1405114a0`, `0x140512430`, `0x1405129d0`, `0x14053a452` |
| offside / demo player-info | **slot 1** | `0x1405ad120` |
| foul choreography | **slot 1** | `0x1405acd90`, `0x1405ad470` |
| keeper think / barrier | **slot 1** | `0x142114bd4`, `0x1409e09f6` |
| animation selection / blend / variants | **slot 1** (`Range0`) and **slot 2 (Normal)** | `0x140955060`, `0x140955a71`, `0x140955650` |
| motion/dynamics noise | **slot 1** | `0x1405ae530`, `0x1405ae6d0`, `0x1405aebe0`, `0x1405aef70` |
| cutscenes | **slot 2** (own singleton / embedded) | `0x141f55110`, `0x141f55a10`, `0x141f7a100`, `0x141f7fea4` |
| replay (ball/human/copy) | **slot 2 (Advance + RangeRaw)** | `0x14217cf0e`, `0x142180c44`, `0x1420b5008` |
| match flow (goal/celebrate path) | **slot 2** on the **main match RNG** | `0x140453429` |
| player-stat spreads (per-player ability scaling) | **slot 2** on the static global | `0x14089bae0` (7), `0x14089be70` |
| CPU strategy decider | **slot 0..2 (dynamic)** | `0x14051eb01` etc. — only dynamic-slot consumer |
| sound | **slot 2** (own Random) | `0x1414962c0`, `0x1414962f0` |
| online (ParamRandom, HTTP) | **slot 2** (own Random) | `0x14100b6f0`, `0x141170b40` |
| team plan "concept" roller | **dynamic slot** (2±) | `0x1420b1b31`, `0x1420b27d9` |
| injury severity | **slot 2** (sub-2 generator) | `0x14048151e`, `0x140481886` — matches `local://pes-re-stamina-drain.md` §6 |
| goalkeeper (behaviour) | no Random slot found in its decisions (only the shared helpers above) | — |
| offside detection | no Random slot | `local://pes-re-offside.md` |
| defender decisions | no Random slot | `local://pes-re-defender-decisions.md` |
| set-piece take timing | no Random slot | `local://pes-re-setpiece-choreography.md` §12 |

## 8. Open items

1. **`0x140223180` / `0x141f51fd0` / `0x141f52060` / `0x141f55110` / `0x141f55a10`** are
   singleton initialisers whose body was read but whose *system name* was not resolved beyond
   "cutscene/demo". Their draws are slot 2, count 1024, on statics `0x1437f5d90` / `0x1437f6340`
   — sufficient for GF slot mapping; naming them is optional.
2. **`0x143f5a150`** (Range0, dynamic count) sits at a VA whose function start was not resolvable
   in `.pdata`; **INF** it is data, not code.
3. `0x1408d9e0b` has two sites whose pre-call context blanked out in linear disasm; the slot is
   `mov r8d,2` at `0x1408d9f72`/`0x1408da06b`-style sites in the neighbouring region, so slot 2
   is INF-high for `0x1408d9ee9`.
4. The **team concept roller's rng object** at `+0x108` — I read the value as `team-vector+0x108`
   (INF) rather than the main `ctx+0x5708`. Only the match-flow controller draws the main RNG.

---

## 9. Completion and corrections (this session)

### 9.1 Correction — the two `.impdata` sites are **code**, not byte coincidences

§3 dismissed `0x143f5a20d` (Range0) and `0x143ef553d` (dtor) as "byte-coincidences in data"
and said the §3 counts exclude them. **That is wrong on both counts (OBS).**

1. An independent whole-image `e8`-scan (my own, not the predecessor's) reproduces
   **Range0 = 67 and dtor = 51 with those two sites included**. The §3 numbers were
   always inclusive; only the prose was wrong.
2. Both sites decode as ordinary x86-64 code with coherent prologues, not as data:
   - `0x143f5a150` — canary prologue (`mov [rsp+0x18],rbx; mov [rsp+0x20],rdi; push rbp;
     mov rbp,rsp; sub rsp,0x50`), a 18-case jump table on `r8d`, a 3-byte label built on
     the stack, then `Range0`. The `rel32` really is `0xfcb3831e` → `0x140a92530`
     (verified by decoding the 5 bytes at file offset `0x3f5a20d`).
   - `0x143ef553d` — a vtable store `mov [rdi],rax; lea rcx,[rdi+8]` immediately followed by
     `call 0x140a92430`: the destructor of an embedded `math::Random` at `rdi+8`, after a
     call into the `0x1404357e0` stream-helper family.

`.impdata` is `RWX` (`0xe0000020`, VA `0x3d14000`–`0x1b95194f`) and the unpacked image
**does** contain live code inside it (OBS, PE section table read from the image header).
Treat every `.impdata` VA as potentially real code in this image, not as data.

Consequence: `0x143f5a20d` is now listed as a **slot-1 draw** (§3.1) and `0x143f5a150` as a
census function (§10). §8 item 2 is superseded.

### 9.2 Seeder / reseed table completed: which object each seeder owns

§6 lists all 55 `setseed` sites and is complete as a site list (verified: 0 sites of the
census are missing from it). What it did not collect is the *object* side, so here is the
same data grouped by the generator that is written (OBS `rcx` at each site; INF-high on the
"owned by" column, which is read off the containing object's field).

| generator object | seeded by | slots written | values | when |
|---|---|---|---|---|
| **main match RNG** `ctx+0x5708` | `0x140441fc0` (match-ctx ctor) | 0,1,2 | `t`, `t+1`, `t+2` | once, at match-context construction |
| main match RNG (second object, GameRandom) | `0x140441fc0` via `GameR_ctor` `0x140a923b0` at `0x1404420a4`, `lea edx,[rdi+2]` | slot count only | — | construction |
| **sub 1** `ctx+0x1e8+0x108` | `0x14047ab90`, called from the same ctor | 0,1 | `t`, `t+1` | construction |
| **sub 2 (injury)** `ctx+0x6d8+0x18` | `0x140481050` | 0,1 | `t`, `t+1` | construction |
| **sub 3** `ctx+0x770+0x10` | `0x140489440` | 0,1 | `t`, `t+1` | construction |
| **sub 4** `ctx+0x7d0+8` | `0x14048b330` | 0,1 | `t`, `t+1` | construction |
| per-player `player+0x32d0` | `0x1406eb670` | 2 only | derived | per player |
| per-clip `anim+0x19c0` | — (no `setseed` site in the image) | — | ctor defaults | never reseeded |
| per-clip `anim+0x19c0` sibling `+0x470`/`+0x518` | `0x141f7d010` | `[rdi+2]` | `byte*10000` / `byte*25000` | cutscene object init |
| carrier plan roll, five throwaway Randoms | `0x14211a800`, `0x14211ab10`, `0x14211af20`, `0x14211b1d0`, `0x14211b460` | 2 | `trunc(carrierClock*K)` | **per plan adoption — mid-match reseed** |
| `+0x4a8` director/anime | `0x140870020` | 2 | `eax*10000` | object init |
| `+0x840` / `+0x8e8` embedded replay RNGs | `0x1408bd970` | 2,2 | `obj+0x18*10000` / `*30000` | object init |
| base-position helper's own Random | `0x140956c30` | 2 | derived | per call |
| concept-roller's own Random (`0x140e83f10`) | `0x140e83f10` | 2 | derived (clock) | per call |
| replicated RNG `+0x28` | `0x1420af930` | 2 | clock `% 10000` | object init |
| ReplayBall `+0xb0` / ReplayHuman `+0xf8` | `0x14217c870` / `0x14217d340` / `0x14217db10` | `[rbx+2]`, `[rcx+1]`, 2 | `eax*10000`, `edi*10000`, `obj+0x28*10000` | object init |
| cutscene singleton `0x141f51fd0` / `0x141f52060` | themselves | 2 | `state+1` | singleton (re)init, **not** per tick |
| cutscene generator `0x141f55110` | itself, twice | `[r15+2]`, 2 | `rax+1` | object init |
| `+0x8a8` stream helper | `0x1411d3020` | `[rbp+1]` | derived | object init |
| sound util | `0x141494d70`, `0x141496220` | 2 | derived | per call |
| online ParamRandom / HTTP | `0x14100b640`, `0x141170b40` | 2 | derived (clock) | per call |
| throwaway clock-seeded | `0x1404357e0`, `0x140435e33`, `0x14152cb20`, `0x141545630` | 0,1,2 / 0,1,2 / 2 / 2 | `t,2t,3t` / `1000a,2000a,3000a` / clock / clock | per call |
| pass-think test tool `+0x48` | `0x14210d320` | `[r14+2]` | `0x2f3cf` (constant) | object init |

**Mid-match reseeding, complete statement (OBS).** In a live match exactly one family
re-seeds during play: the five **carrier plan-roll** Randoms (`0x14211a800`…`0x14211b460`),
each a throwaway `math::Random` seeded from the carrier's own clock and drawn exactly once.
They do not touch the main match RNG or any of the four subs. Everything else in the table is
construction-time or per-call seeding of an object that is thrown away with the call.
Halves, restarts and set pieces do **not** re-seed the match generators.

## 10. Complete per-function census (slot → system → call-site VAs)

Every draw site in the image, grouped by helper and owning function, with the count and the
slot register read at each site. Totals: **Range 23 / RangeRaw 49 / Range0 67 / Normal 12 /
AbilityNormal 1 / Advance 6** = 158 helper call sites, of which one (`0x140a9284c`) is
`AbilityNormal` calling `Normal` internally, leaving **157 system draw sites in 105 distinct
functions**. Reproduced this session by an independent whole-image `e8` scan (script in the
RngSlotsRE scratch dir, same totals as the predecessor's census).
`rcx` is the expression the slot array was addressed through at the call, which is what
identifies the generator object (§5). `dyn` = the slot register is computed, not an
immediate; every `dyn` site that resolves to a constant is listed in §3.1's dynamic table.

**Range** `0x140a92470` — 23 sites in 18 functions (slot argument: `edx`)

| fn | n | sites | slot | rcx |
|---|---|---|---|---|
| `0x140435ba0` | 1 | `0x140435bd2` | 2 | `rbx` |
| `0x140435e33` | 1 | `0x140435e8c` | 2 | `[rbp+8]` |
| `0x140435ef0` | 1 | `0x140435f06` | 2 | `rcx+8` |
| `0x1405b83b0` | 1 | `0x1405b8759` | **0** (`xor edx,edx`) | `[rbp-0x20]` |
| `0x1405e9f50` | 1 | `0x1405ea303` | **0** (`xor edx,edx`) | `[rbp+0x98]` |
| `0x1406e6bf0` | 1 | `0x1406e6c04` | 2 | `player+0x32d0` |
| `0x1406eaa10` | 3 | `0x1406eaa2b`, `0x1406eaa6e`, `0x1406eaaaa` | 2 | `rdi` (the same per-player `+0x32d0`) |
| `0x1406ebf30` | 1 | `0x1406ebf44` | 2 | `player+0x32d0` |
| `0x140956c30` | 1 | `0x140956fc0` | 2 | `[rbp-0x21]` |
| `0x140e83f10` | 4 | `0x140e83f6d`, `0x140e83f7e`, `0x140e83f90`, `0x140e83fa2` | 2 | `[rsp+0x28]` |
| `0x14100b6f0` | 1 | `0x14100b705` | 2 | `rcx+8` |
| `0x141170b40` | 1 | `0x141170c9c` | 2 | `[rsp+0x40]` |
| `0x1414962c0` | 1 | `0x1414962d1` | 2 | not in the disasm window |
| `0x1414962f0` | 1 | `0x1414962fd` | 2 | not in the disasm window |
| `0x14152cb20` | 1 | `0x14152cd48` | 2 | `[rbp+0xf]` |
| `0x141545630` | 1 | `0x1415456b8` | 2 | `[rsp+0x30]` |
| `0x1420b1b0d` | 1 | `0x1420b1b31` | 2 (`esi=1`, `[rsi+1]`) | `[rdi+0x28]` |
| `0x1420b27b0` | 1 | `0x1420b27d9` | 2 (`esi=0`, `[rsi+2]`) | `rcx+0x28` |

**RangeRaw** `0x140a924e0` — 49 sites in 32 functions (slot argument: `r8d`)

| fn | n | sites | slot | rcx |
|---|---|---|---|---|
| `0x140223180` | 1 | `0x1402231ba` | 2 | static `0x1437f5d90`, count `0x400` |
| `0x1402231e0` | 1 | `0x14022321a` | 2 | static `0x1437f6340`, count `0x400` |
| `0x140435ba0` | 3 | `0x140435be6`, `0x140435bf9`, `0x140435c0f` | 2 / 2 (`mov r8d,edx`) / 2 | `rbx` |
| `0x140435e33` | 2 | `0x140435ea1`, `0x140435eb5` | 2 / 2 (`mov r8d,edx`) | `[rbp+8]` |
| `0x140435ef0` | 3 | `0x140435f1b`, `0x140435f2f`, `0x140435f46` | 2 / 2 / 2 | `[rdi+8]` |
| `0x1404533ff` | 1 | `0x140453429` | 2 (`0x64-0x62`) | **`ctx+0x5708`, the main match RNG** |
| `0x1404812b0` | 1 | `0x14048151e` | 2 | `[rbp+0x1a70]` (injury generator) |
| `0x14048185e` | 1 | `0x140481886` | 2 | `[r13+0x1a70]` |
| `0x14051e970` | 4 | `0x14051eb01`, `0x14051eb5e`, `0x14051eb8f`, `0x14051eba4` | **dyn `esi`, 0..2** | `[rdi+0x20]` (GameRandom) |
| `0x14089bae0` | 7 | `0x14089bb10`, `0x14089bb73`, `0x14089bbc0`, `0x14089bcba`, `0x14089bd11`, `0x14089bd6d`, `0x14089bdd9` | 2 | seven distinct `[rip+…]` statics |
| `0x14089be70` | 1 | `0x14089bee5` | 2 | `[rip+0x2e577c7]` |
| `0x1408c0009` | 1 | `0x1408c02fd` | 2 (`0x15-0x13`) | `[rdi+0x8e8]` |
| `0x1408d5ddf` | 1 | `0x1408d5e0a` | 2 | `[rip+0x2e1d97d]` |
| `0x1408d6740` | 1 | `0x1408d67fe` | 2 (`r15=1`, `[r15+1]`) | `[rip+0x2e1cf80]` |
| `0x1408d9bc0` | 1 | `0x1408d9c54` | 2 | `[rip+0x2e19bae]` |
| `0x1408d9e0b` | 2 | `0x1408d9ee9`, `0x1408d9f8a` | 2 | `[rip+0x2e1991a]`, `[rip+0x2e19879]` |
| `0x1408da03d` | 1 | `0x1408da080` | 2 | `[rip+0x2e19783]` |
| `0x141f51fd0` | 1 | `0x141f52013` | 2 | singleton `[rip+0x18a3d83]`, count `0x400` |
| `0x141f52060` | 1 | `0x141f520a8` | 2 | singleton `[rip+0x18a4298]`, count `0x400` |
| `0x141f55110` | 3 | `0x141f5522a`, `0x141f552e2`, `0x141f5532c` | **dyn `[r15+2]`** / 2 | `r12`, `rbx` |
| `0x141f55a10` | 1 | `0x141f55a44` | 2 | `rbx`, count `0x400` |
| `0x141f7a100` | 1 | `0x141f7a11a` | 2 | pass-through, count `0x2ac` |
| `0x141f7fea4` | 1 | `0x141f7fef4` | 2 (`0x15-0x13`) | `[rbx+0x518]` |
| `0x1420b5008` | 1 | `0x1420b503f` | 2 | `[rbp+0x28]` |
| `0x1420b62b7` | 1 | `0x1420b631b` | 2 | `[r13+0x28]` |
| `0x1420b92f0` | 1 | `0x1420b9551` | 2 | `[rbx+0x28]` |
| `0x14211a800` | 1 | `0x14211aac2` | 2 | throwaway `[rsp+0x38]` |
| `0x14211ab10` | 1 | `0x14211ada6` | 2 | throwaway `[rsp+0x50]` |
| `0x14211af20` | 1 | `0x14211b14e` | 2 | throwaway `[rsp+0x28]` |
| `0x14211b1d0` | 1 | `0x14211b412` | 2 | throwaway `[rsp+0x28]` |
| `0x14211b460` | 1 | `0x14211b762` | 2 | throwaway `[rsp+0x38]` |
| `0x14212b036` | 1 | `0x14212b143` | 2 | `[rbp+0x48]`, count `0x2710` |

Slot-2 constants inside the three stream helpers, read from raw bytes (OBS): the
`[rdx-0x62]` sites use `edx = 0x64` (divisor 100, slot 2); the `mov r8d,edx` sites use
`edx = 2` (divisor 2, slot 2); the final site of `0x140435ba0` (`0x140435c0f`) and of
`0x140435ef0` (`0x140435f46`) use `mov edx,0xf423f ; mov r8d,2` — divisor 999999, slot 2.

**Range0** `0x140a92530` — 67 sites in 46 functions (slot argument: **fixed 1**)

| fn | n | sites | count (the `edx` argument) | rcx |
|---|---|---|---|---|
| `0x14044793d` | 1 | `0x1404479be` | `clock*100` | **`[rdi+0x5708]` — the main match RNG** |
| `0x140470f10` | 1 | `0x140470f6f` | 100 | `[rdi+0x108]` |
| `0x140471361` | 8 | `0x14047145f`, `0x140471479`, `0x14047149e`, `0x1404714b8`, `0x1404714e1`, `0x1404714f7`, `0x140471513`, `0x140471529` | 100 | `[rbp+0x108]` |
| `0x140474b20` | 1 | `0x140474bee` | 2 | `[rbx+0x108]` |
| `0x140475b28` | 2 | `0x140475d0b`, `0x140475de8` | 100, 10000 | `[rdi+0x108]` |
| `0x14047639d` | 1 | `0x140476812` | dynamic | `[r15+0x108]` |
| `0x140476a4d` | 1 | `0x140476a80` | 500 | `[r15+0x108]` |
| `0x140477400` | 1 | `0x14047750f` | 2 | `[rdi+0x108]` |
| `0x14047ea10` | 1 | `0x14047ea53` | dynamic | `base+0x18` |
| `0x1404886b0` | 3 | `0x140488ebe`, `0x140489150`, `0x14048937f` | dynamic, dynamic, 10000 | `[r13+0x10]`, `[rbx+0x10]` (sub 3) |
| `0x14048a260` | 3 | `0x14048a2a4`, `0x14048a490`, `0x14048a4e3` | 10000 | `r12` — **base not traced** |
| `0x14048ad4c` | 1 | `0x14048aec0` | 10000 | `[rbx+8]` (sub 4) |
| `0x14048b2b0` | 2 | `0x14048b2d3`, `0x14048b2e6` | 12, dynamic | `base+8`, `[rsi+8]` (sub 4) |
| `0x1404ba3e0` | 1 | `0x1404babb9` | dynamic | `[r14+0x108]` |
| `0x1404f6140` | 1 | `0x1404f61a4` | 2 | `[rbx+0x6c18]` |
| `0x1404fcb35` | 1 | `0x1404fcb5c` | 100 | `[rbp+0x6c18]` |
| `0x14050590a` | 1 | `0x140505a88` | 100 | `rdi` |
| `0x1405114a0` | 1 | `0x1405114da` | 100 | `rbx` |
| `0x140511976` | 1 | `0x140511a90` | dynamic | `[rsp+0xf0]` |
| `0x140512430` | 1 | `0x1405124c1` | 10000 | `r9` |
| `0x140512550` | 1 | `0x1405125f5` | dynamic | `[rbp-0x10]` |
| `0x1405129d0` | 1 | `0x140512a70` | 10000 | `r9` |
| `0x14053a452` | 1 | `0x14053a512` | dynamic (float→int) | `[rsp+0x90]` |
| `0x1405acd90` | 2 | `0x1405acf0b`, `0x1405acf35` | 100, dynamic | `[rbp+0x48]` |
| `0x1405ad120` | 1 | `0x1405ad19b` | 100 | not in the disasm window |
| `0x1405ad470` | 1 | `0x1405ad4f1` | dynamic | `r13` |
| `0x1405ae530` | 1 | `0x1405ae5a2` | dynamic | `rbx` |
| `0x1405ae6d0` | 1 | `0x1405ae893` | dynamic | `[rsp+0x70]` |
| `0x1405aebe0` | 1 | `0x1405aecc4` | dynamic | `r14` |
| `0x1405aef70` | 1 | `0x1405aefe2` | 10 | `rsi` |
| `0x1405afaf1` | 1 | `0x1405afc08` | 10000 | `[rsp+0x60]` |
| `0x1405b0620` | 4 | `0x1405b0821`, `0x1405b0a5c`, `0x1405b0aa8`, `0x1405b0af4` | dynamic, 10000 ×3 | `[rsp+0x60]` |
| `0x1405b0b8c` | 1 | `0x1405b0bf7` | 100 | `r10` |
| `0x1405b103f` | 3 | `0x1405b1338`, `0x1405b13e9`, `0x1405b144c` | 10000, 10000, 100 | `[rsp+0x60]`, `r10` |
| `0x1405f084f` | 1 | `0x1405f0934` | 100 | `r15` |
| `0x1405f2a80` | 1 | `0x1405f3137` | 100 | `r12` |
| `0x1405f32e0` | 1 | `0x1405f3411` | 100 | `r13` |
| `0x140955060` | 3 | `0x1409553e0`, `0x1409554a1`, `0x14095556a` | dynamic (float→int) | **`anim+0x19c0`** |
| `0x140955a71` | 1 | `0x140955aa3` | dynamic | **`anim+0x19c0`** |
| `0x140998087` | 1 | `0x1409981f8` | dynamic | `r12` |
| `0x1409982ac` | 1 | `0x140998401` | 2 | **`anim+0x19c0`** |
| `0x1409bed20` | 1 | `0x1409bed74` | 3 | **`anim+0x19c0`** |
| `0x1409ca5e8` | 1 | `0x1409ca777` | 5 | **`anim+0x19c0`** |
| `0x1409e09f6` | 1 | `0x1409e0a42` | 6 | **`anim+0x19c0`** |
| `0x142114bd4` | 1 | `0x142114c41` | 3 | **`anim+0x19c0`** |
| `0x143f5a150` | 1 | `0x143f5a20d` | obfuscated, unresolvable (§11.1) | `r9` |

**Normal** `0x140a92590` — 12 sites in 5 functions (slot argument: **fixed 2**)

| fn | n | sites | rcx |
|---|---|---|---|
| `0x1405ae43c` | 1 | `0x1405ae462` | `r15` |
| `0x1407324b0` | 3 | `0x1407328e4`, `0x140732983`, `0x140732a2d` | `rax` (the shared kick-launch formula's Random) |
| `0x1408398d5` | 6 | `0x140839b51`, `0x140839bd5`, `0x14083a280`, `0x14083a550`, `0x14083a5f2`, `0x14083a7fe` | `rax` (the kick/scatter writer's Random) |
| `0x140955650` | 1 | `0x14095569d` | **`anim+0x19c0`** |
| `0x140a927b0` | 1 | `0x140a9284c` | internal call from `AbilityNormal` to `Normal` — not an independent system |

**AbilityNormal** `0x140a927b0` — 1 site: `0x14092f0a1` in `0x14092ed30`, slot 2, `ctx2+0xa368`.

**Advance** `0x140a92880` — 6 sites in 6 functions (slot argument: `edx`)

| fn | site | slot | rcx |
|---|---|---|---|
| `0x140873787` | `0x1408737c2` | 2 | `[rbx+0x4a8]` (director/anime) |
| `0x1408c0581` | `0x1408c0b8f` | 2 | `[rbx+0x840]` (embedded replay RNG) |
| `0x141f80070` | `0x141f8038c` | 2 | `[rbx+0x470]` (cutscene) |
| `0x141fb1a50` | `0x141fb24ce` | **dyn `edi`** | `[rbp-8]` (throwaway, same fn re-seeds it) |
| `0x14217cf0e` | `0x14217d0dc` | 2 | `[rbx+0xb0]` (ReplayBall) |
| `0x142180c44` | `0x142180f6a` | 2 | `[rdi+0xf8]` (ReplayHuman) |

## 11. Unresolved call sites (open — not attempted this session)

Each item states what is missing and the exact next step. Nothing here is required for the
GF slot mapping; all of it is attribution polish.

**11.1 `0x143f5a20d` — the `Range0` count cannot be resolved from the file image.**
The count is built at `0x143f5a1fc`: `mov ecx,[rip+0x4359ba2]` → VA `0x1437f5ba4`,
`xor ecx,0xee816ee6`. In the unpacked file that dword is **0**, so the static value would be
`0xee816ee6` — the same "load-then-xor" obfuscation as the neighbouring code, which means the
real constant is patched in at runtime inside `.impdata`.
*Next step:* read the live process at `0x1437f5ba4` (sudo pread of `/proc/<pid>/mem`, the
authorised read-only procedure) and XOR the value with `0xee816ee6`.

**11.2 Generator object not traced (base lives in a register).**
`0x14048a260` ×3 (`0x14048a2a4`, `0x14048a490`, `0x14048a4e3`) — `rcx = r12`, never
materialised against a context pointer in the window.
*Next step:* widen the linear window backwards to the function's `r12` definition, or decompile
`0x14048a260` and read the parameter.

**11.3 The register-spilled slot-1 cluster (23 functions).**
`0x14050590a`, `0x1405114a0`, `0x140511976`, `0x140512430`, `0x140512550`, `0x1405129d0`,
`0x14053a452`, `0x1405acd90`, `0x1405ad120`, `0x1405ad470`, `0x1405ae530`, `0x1405ae6d0`,
`0x1405aebe0`, `0x1405aef70`, `0x1405afaf1`, `0x1405b0620`, `0x1405b0b8c`, `0x1405b103f`,
`0x1405f084f`, `0x1405f2a80`, `0x1405f32e0`, `0x140998087`, `0x1404f6140`/`0x1404fcb35`
(these two do resolve: `ctx+0x6c18`). All are slot 1 (OBS, `Range0` hard-wires it) — only the
owning object is unknown.
*Next step:* one `callers` pass per function to find the context pointer that reaches the
register; they are all in the `0x1404xxxx`–`0x1405bxxxx` match-logic / tactics band and are
likely two or three shared sub-objects.

**11.4 Dynamic slot arguments that are not yet pinned.**
`0x14051e970` ×4 (`mov r8d,esi`, `esi ∈ 0..2`, on `GameRandom` at `obj+0x20`);
`0x141f55110` ×2 (`lea r8d,[r15+2]`, `r15` not tracked);
`0x141fb1a50` (`mov edx,edi`); `0x14217c870` (`lea edx,[rbx+2]`); `0x14217d340`
(`lea edx,[rcx+1]`); `0x141f7d010` ×2 (`lea edx,[rdi+2]`) — these four are the *seeding* slot,
so the same dynamic value also fixes the draw slot in each.
Closed since §3.1 was written: `0x1420b1b0d` → 2, `0x1420b27b0` → 2, `0x141f7fef4` → 2,
`0x1408d67fe` → 2, `0x1408d9ee9` → 2 (supersedes §8 item 3).
*Next step:* decompile each owner and read the `esi` / `r15` / `edi` definition.

**11.5 System names not resolved (slot and object are known).**
`0x140223180` and `0x1402231e0` (statics `0x1437f5d90` / `0x1437f6340`, count 1024, slot 2) —
each is a tiny singleton initialiser that stores a vtable and draws once; supersedes nothing
in §8 item 1, which stays open for the `0x141f…` singletons. Also unnamed:
`0x14048ad4c`, `0x14048b2b0`, `0x14048a260` (the sub-4/sub-3 band), `0x14212b036`
(`[rbp+0x48]`, count 10000, slot 2), `0x140e83f10` (four consecutive slot-2 draws after a
derived seed; "concept-roller" is a working label, not a resolved system name).
*Next step:* caller pass on each; `0x14212b036`'s `[rbp+0x48]` suggests a per-frame local
Random, so its object may be stack-resident and never worth naming.

**11.6 `0x140470f10` / `0x1404ba3e0` — the `+0x108` base is ambiguous** (still §8 item 4).
The `+0x108` cluster reads a `math::Random` embedded at `base+0x108`; the sub-1 generator is
`ctx+0x1e8+0x108`, but a team-vector object with the same offset also fits.
*Next step:* trace the `rdi`/`rbp`/`r14` back to the `ctx` pointer in one function
(`0x140470f10` is the cheapest — one `lea` from a parameter).

## 12. The contact system draws no match slot

From `local://pes-re-contact.md` §5, corroborated here by the census:

- **No draw site in the census belongs to a contact decision.** Stagger, fall, jostle, aerial
  challenge and body-to-body resolution are computed from state (closing speed, angle,
  balance, height) with **no `Range` / `RangeRaw` / `Range0` / `Normal` call on any match
  generator**. Nothing in §3.1 or §10 sits in the contact path.
- The **only** randomness a contact touches is the choice of *which cosmetic clip variant* to
  play, and that comes from the **per-player generator at `ap+0x32d0`** (`0x1406e6bf0`
  `n`-way pick, `0x1406ebf30` percent test, `0x1406eaa10` three-roll test) — **slot 2 of a
  generator that lives inside the player object and is never re-seeded during play.**
- Therefore the per-player `player+0x32d0` stream is *not* a match stream: replacing it
  changes clip flavour only, never a contact outcome.
- GF consequence: a GF system whose randomness is only "which animation variant", "which
  contact clip", or "how hard the stagger looks" should **not** draw from the match RNG at
  all. PES keeps it off the match generator entirely.

## 13. GF: `pesrandom.*` slot mapping, and what each GF system should really use

### 13.1 The mapping as implemented

`src/sim/onthepitch/pesrandom.hpp` exposes four draw shapes on `pes::Random` —
`Range(slot)`, `Unit(slot)` (=`RangeRaw`), `RangeRaw(slot,modulus)`, `Range0(count)` — plus
`Step` and `AdvanceWeak`. `src/sim/base/math/bluntmath.cpp` funnels GF's two generators
through `RandomSource::Unit(stream)` with `kRandomStream = 0` (`random()`) and
`kFastRandomStream = 1` (`fastrandom()`).

`src/sim/onthepitch/match.cpp` `Match::RngSource::Unit` maps
`SLOT_FOR_STREAM[] = {2, 0}` — so **`random()` → slot 2, `fastrandom()` → slot 0** — and its
own comment already flags this as a *convention*, not a PES observation.

Two facts from the census that bear on this (OBS):

1. **`fastrandom()` has no consumers in GF.** `grep` over `src/` finds it only in its own
   declaration and in the comment quoted above. Every GF draw goes through `random()`, i.e.
   **slot 2 of the main match RNG**.
2. **PES never uses slot 2 of the main match RNG for gameplay decisions.** The only consumer of
   `ctx+0x5708` slot 2 in the whole image is the match-flow controller (`0x140453429`, count
   100). Slot 2 of the main generator is essentially free; every gameplay decision PES makes
   draws from a *different object*.

### 13.2 Per GF system: what PES really does

"PES slot" is the slot of the draw; "PES generator" is the object. GF's column is what GF
does today (all of it `random()` → slot 2 of `ctx+0x5708` unless noted).

| GF system | GF draw site | PES's real slot | PES's real generator | verdict |
|---|---|---|---|---|
| carrier plan selection | `pescarrier.cpp:526` `PlanRoll` | **2** | throwaway per-adoption Random, `RangeRaw(2,100)` | **already correct** |
| ball-control / first-touch error | `humanoid_utils.cpp:230` (`GetBallControlVector`) | 2 | **per-player** `player+0x32d0`, via `0x1406ebf30` | right slot, wrong object (match vs per-player) |
| fumble touch | `humanoid.cpp:647` (`PlayerSkills::FumbleTouch`) | 2 | per-player `player+0x32d0`, `0x1406e6bf0` | same |
| shot aim / curve / loft scatter | `humanoid_utils.cpp:603,624,640,642,657` (`GetShotVector`), `humanoid.cpp:888,990,992` | **2 (Normal)** | the kick writer's Random (`0x1408398d5` ×6) and the shared kick-launch Random (`0x1407324b0` ×3) | right slot, wrong object |
| pass loft / touch direction error | `humanoid.cpp:3557,3578` (`GetBestPossibleTouch`) | **2 (Normal)** | `0x1407324b0` / `0x1408398d5` | right slot, wrong object |
| **animation frame / clip-variant pick** | `humanoidbase.cpp:476` (`ResetPosition`) | **1** | **per-clip** `anim+0x19c0` (`0x140955060`, `0x140955a71`) | **wrong slot and wrong object** |
| feint pick | `playercontroller.cpp:402,410` (`_SetPieceCommand`) | 2 | per-player `player+0x32d0`, `0x1406e6bf0` | right slot, wrong object |
| goalkeeper: clearance clamp | `elizacontroller.cpp:198` (`RequestCommand`) | **1** | per-clip `anim+0x19c0`, `0x1409e09f6` (count 6) / `0x142114bd4` (count 3) | **wrong slot and wrong object** |
| goalkeeper: catch / distribution aim | `elizacontroller.cpp:223,224,259,262,266,274,277,281,285,288,289` | **1** | per-clip `anim+0x19c0` | **wrong slot and wrong object** |
| goalkeeper: stumble / dive extension | `elizacontroller.cpp:1170,1343` (`GetOnTheBallCommands`) | **1** | per-clip `anim+0x19c0` | **wrong slot and wrong object** |
| player slip / footing | `player.cpp:478` | **no draw** | none — PES's footing test is a state function; the only per-player rolls (`0x1406ebf30`) are slot 2 and belong to contact/animation | GF-only randomness |
| counter-attack launch gate | `teamAIcontroller.cpp:340` | **1**, or **dyn 0..2** | tactics CPU strategy (`0x1405f084f`, `0x1405f2a80`, `0x1405f32e0`, slot 1) or the strategy decider (`0x14051e970`, `r8d = esi ∈ 0..2`) | **wrong slot** |
| team tactics / pressure / concept choice | no GF draw site | **1** | sub-1 generator `ctx+0x1e8+0x108`, `0x140471361` ×8 etc. | (PES-side reference) |
| match flow: goal / restart branch | no GF draw site | **2** | **main match RNG** `ctx+0x5708`, `0x140453429` | the one place GF's slot 2 on the match RNG *is* PES-faithful |
| match flow: restart / half-time choice | no GF draw site | **1** | main match RNG, `0x1404479be`, count `clock*100` | (PES-side reference) |
| set-piece choreography: concept, taker, role | no GF draw site | **1** | `ctx+0x6c18` (`0x1404f6140` count 2, `0x1404fcb35` count 100) and the `+0x108` cluster | (PES-side reference) |
| throw-in choreography | no GF draw site | **1** | per-clip `anim+0x19c0`, `0x1409982ac` (2), `0x1409bed20` (3), `0x1409ca5e8` (5) | (PES-side reference) |
| injury severity | no GF draw site | 2 | sub-2 / injury Random `+0x1a70`, `0x14048151e`, `0x140481886` | (PES-side reference) |
| duel / dive simulation | no GF draw site | **0** | a **default-seeded local** Random inside `0x1405b83b0` (no `setseed` in the function), `0x1405b8759` | (PES-side reference) |
| off-ball free-support cell pick | no GF draw site | **0** | a **default-seeded local** Random inside `0x1405e9f50` (`0x1405ea11e` ctor, `0x1405ea303` draw; no `setseed` anywhere in its `.pdata` range `0x1405e9f50`–`0x1405eaf6b` — verified this session) | (PES-side reference) |
| penalty shootout: first team, end side, aim, save | `penaltyshootoutcontroller.cpp:71,79,190,193` | **no draw site in the census** | — | GF-only randomness; no PES counterpart found |
| ball rotation after body contact | `match.cpp:1615` (`CheckBallCollisions`) | **no draw** | none — body contact is deterministic (§12) | GF-only randomness |
| wind direction / strength at match start | `match.cpp:216` | **no draw site in the census** | — | GF-only randomness |
| motion / dynamics noise | no GF draw site | **1** | `0x1405ae530`, `0x1405ae6d0`, `0x1405aebe0`, `0x1405aef70` (count 10) | (PES-side reference) |
| ability-scaled deviation | no GF draw site | **2 (Normal)** | `ctx2+0xa368`, `AbilityNormal` `0x14092f0a1` | (PES-side reference) |
| player-stat spread generation | no GF draw site | **2** | seven statics, `0x14089bae0` ×7, `0x14089be70` | (PES-side reference) |

**The two concrete mismatches worth acting on** (everything else is a cosmetic difference in
*which object* the same slot number is read from):

1. **Animation / clip-variant selection is slot 1 on the per-clip generator**, not slot 2 on the
   match generator (`humanoidbase.cpp:476`, and the same for the GK variant picks). PES keeps
   a separate `math::Random` per clip at `anim+0x19c0` that is **never re-seeded** — its stream
   is independent of the match seed, so it does not consume match entropy.
2. **The goalkeeper's randomness is also slot 1 / per-clip**, not slot 2 / match. PES's keeper
   decision randomness lives in the clip generator (`0x1409e09f6` count 6, `0x142114bd4`
   count 3), not on `ctx+0x5708`.

Conversely, GF's slot-2 choice is *correct* for the carrier plan roll and for the whole
Normal-shaped family (shot, pass, ball-control error) — those are the systems that really do
want `state/2^31 × 32768`-ish quality from one shared stream.
