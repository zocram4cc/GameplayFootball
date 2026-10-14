# Which cutscene PES picks: entrance sequence + in-match scene pick, and the seeding

Target: `pes-re-cutscenes.md` §8 items 3–4 (entrance sequence choice; RNG/seed).
Out of scope: slot → player mapping (CutsceneSlotRE). No builds, no tests.
Base: image `pes_full.bin` at 0x140000000. Every branch claim below was
cross-checked against raw disassembly (pdis/capstone); decompiler-only
wording was corrected where the bytes disagreed.

## 1. Entrance: there is no entrance "scene choice" — the sequence is fixed data

OBS (0x141f64780 body + callers; 0x141f65bd0; 0x141f5a680):
- The entrance request list lives at manager+0x88 (up to 10 entries × 0x20 B).
  `FUN_141f5a680` (called from `FUN_141f57690`) copies those entries into the
  play-step vector; it never filters, scores, or rolls — straight `do {…}
  while (iVar4 < 10)` copy, early-out only on a null entry pointer.
- The list is filled by `FUN_141f64780`, which calls `FUN_141f65bd0` and then
  bulk-copies a 0xd8-byte config struct (fields +0x80…+0x158 of its input)
  into manager+0x80…+0x158 — including the +0x88 scene list. No condition
  key, no matcher call (`FUN_141f4b1d0`/`FUN_141f4fda0` are never called on
  this path), no RNG call.
- `FUN_141f65bd0` builds that struct by copying ~30 dwords from its input
  object (+0x78/+0x138…+0x200) plus a name hash
  (`FUN_1403d2930` + indirect call through `*(0x142530058)`).

INF (med): the entrance "choice" is made upstream of fixdemo — whatever
constructs the 0xd8 config (competition/stadium setup code) decides which
ENT_* scenes are listed. The fixdemo entrance manager only sequences them
in order (0,1,2,… via `FUN_141f61180` indexing, count via `FUN_141f611c0`).

Killed hypothesis: that the entrance scene is picked by the condition
matcher (condTypes 0x01 stadium / 0x02 competition / 0x12 time / 0x13
weather / 0x43 random). Those keys DO exist in the ent T1 cut-variant rows
and DO steer the *cut variant* per step — but the *scene sequence* itself
is fixed input, not a matcher output.

Next step for the residual: identify the caller that fills the 0xd8 config
(`FUN_141f64780`'s input object, +0x88 writer upstream). `FUN_141f64780`
itself has no direct callers by byte-scan (likely called via vtable slot);
the vtable at 0x142c701f8 is valid (slot +0x68 = 0x141f57440 etc.), and the
entrance ctor `FUN_1423f0890` sets `*param_1 = 0x142c701f8` (OBS). Trace from
whoever constructs the FixDemoEntrance (creator near 0x141f18290 sets a
different vtable 0x142b9ede0 first — that path is the generic manager, not
the entrance; the entrance-specific creator is still open).

## 2. In-match scene pick: deterministic rule + one RNG draw family

### 2a. Goal (the only event with a real "pick")
OBS (`FUN_140477400` @ 0x140477400, `FUN_140474b20` @ 0x140474b20,
cross-checked at 0x1404774fa–0x14047750f):
1. Collect pool: all goal scenes with flag bit 6 (`FUN_140a7cce0(...,3,6)`),
   then filter by bit 11/12/13 per goal-ctx[0] ∈ {0,1,2} ("level"), then
   optionally by bit 16 (`FUN_140a7ce90`).
2. Single draw: `FUN_140a92530(goalctx+0x108, poolCount)` —
   `lea rcx,[rdi+0x108]; call 0x140a92530` (OBS bytes at 0x140477508).
   Index = draw; out-of-range → trap (not a fallback).
3. Same shape for the hug scene (`FUN_140474b20`): pool bits 2/3 → filter
   12/13 → same `+0x108` draw.
4. Golazo special (`FUN_140475a90` region, 0x140475d0b): `rand(100) > 49`
   gate (`cmp eax,0x32; setge; +0x3013b`) — same `+0x108` object
   (`lea rcx,[rdi+0x108]; mov edx,100` OBS at 0x140475d0b−24B).

### 2b. Foul / card, substitution, end-of-match: NO randomness
OBS:
- Foul builder `FUN_140480910` takes its scene id from one of three helpers:
  `FUN_14047e940` (→ `FUN_14047f170`, a deterministic foul-point/card-rule
  evaluation — no RNG call in its 0x8d2-byte body, grep-confirmed),
  `FUN_14047ea10` (picks `table {0x20002, <0x20002+draw/100>}` — the draw IS
  `FUN_140a92530(param_1+0x18, 100)` (OBS capstone at 0x14047ea4c–53:
  `add rcx,0x18; lea edx,[rax+100]; call 0x140a92530`), but `iVar4/100` is
  0 for any draw 0–99, so the pick is always the first entry — dead roll),
  `FUN_14047e060` (card colour → 0x2001f/0x20020, deterministic).
- Substitution `FUN_140484a10`, end-of-match `FUN_14048ab10`: fixed scene
  constants (0x50010 / 0x7002b sentinels or direct picks); no RNG call.
- The 0x20022 / 0x50010 / 0x7002b / 0x301b6 out-of-range constants are
  handled by the builders as "no demo" sentinels (foul: `if (id==0x20022)
  free, return`; same shape for the others) — OBS in `FUN_140480910`
  (0x140480910 body) and `FUN_140484a10`.

### 2c. Cut-variant randomness (all categories, incl. entrance steps)
OBS (byte-verified):
- CondType 0x19 draw at 0x141f4e33a: `cmp [rcx+0x5228],2; mov edx,100;
  lea rcx,<rng>; je→0x1437f6340 else 0x1437f5d90; call 0x141f7a100;
  add eax,0x1900` — targets recomputed from raw LEAs
  (0x141f4f0f2→0x1437f6340, 0x141f4f0fb→0x1437f5d90; same pair at 0x141f4e32a/33).
- CondType 0x43 draw at 0x141f4f102: identical routing, `add eax,0x4300`.
- Per-manager draws: entrance `FUN_141f5d2e0` @ 0x141f5d31d:
  `mov edx,100; add rcx,mgr+0x150; call 0x141f7a100` (OBS) — the manager's
  OWN Random1000 at +0x150, not either static. Same for the ctor draws at
  0x141f5541f/36 (`lea rcx,[rdi+0x150]`). And `FUN_141f2de55` draws with
  `rcx=[rdi+8]+0x178` (a sub-object's generator).

## 3. The generators and the seeds (closes "RNG seeding: not traced")

### 3a. Which generator each draw uses
| draw | generator object | slot stepped |
|---|---|---|
| goal scene/hug/golazo (`140a92530`) | per-event goal ctx +0x108 (`GameRandom`, ctor `140a923b0` at 0x14047df29, `rcx=rdi+0x18`, `edx=2`) | slot 1 (0x140a92530 steps `[rcx+0xc]`) |
| foul dead roll (`140a92530`) | builder obj +0x18 (same call shape) | slot 1 |
| cond 0x19/0x43 (`141f7a100`) | static `Random1000` 0x1437f6340 if ctx+0x5228==2 (match) else 0x1437f5d90 | slot 2 + acc `+0x20` (OBS 0x141f7a100 body) |
| entrance per-manager (`141f7a100`) | manager+0x150 (embedded `Random1000`) | slot 2 + acc |
| manager ctor rolls | same manager+0x150 | slot 2 |

OBS notes:
- Both statics carry the `Random1000` vtable 0x142ba5668 (image qwords at
  0x1437f5d90/0x1437f6340, OBS).
- `FUN_140a92430` (the ctor the entrance dtor path calls at the end of
  `FUN_141f55cb0`) is a bare vtable store, NOT a seeder (OBS 3-instr body) —
  the real seeding is `FUN_140a928a0` (raw slot store:
  `movsxd rax,edx; mov [rcx+rax*4+8],r8d`, OBS capstone).

### 3b. Seed values — clock-derived at construction, then never reseeded
OBS (capstone-verified chain):
1. Match RNG commit @ 0x1404422c8–0x140442337 (MatchControl ctor region):
   - `t = clock() % 10000` via `call 0x141b34aa0; mov rcx,rax;
     call 0x141b34ad0` then `mul 0x346dc5d63886594b; shr rdx,0xb;
     imul rax,rdx,10000; sub rbx,rax` (OBS) — the multiply-shift-subtract
     idiom for mod 10000. (`0x141b34aa0` is a jmp-thunk to `0x141b34a30`,
     which fills 8 bytes via an IAT call — clock/time source; the exact
     import was not resolved, INF med: wall-clock/tick count.)
   - `t, t+1, t+2` committed to match RNG slots 0/1/2 at `rsi+0x5708`
     (`FUN_140a928a0` ×3, OBS).
2. Goal-ctx RNG seeded from the SAME triple: `FUN_14047ab90` stores
   `(slot0=edx_entry, slot1=ebx_saved, slot2=esi_saved)` into `rcx=rdi+0x108`
   (OBS capstone: `mov ebx,r8d; lea rdi,[rcx+0x108]; mov r8d,edx; xor edx,edx;
   call 928a0; mov r8d,ebx; mov edx,1; call; mov r8d,esi; mov edx,2; jmp 928a0`).
   Its single caller (wide byte-scan) is 0x140442337 — the instruction right
   after the match-RNG commit, passing `(edx=ebx=t, r8d=ebp=t+1, r9d=edi=t+2)`
   (OBS 0x140442328–33b). **So the goal scene draw is match-RNG-derived
   (same t,t+1,t+2), slot 1 of its own object — not a separate entropy
   source.**
3. Fixdemo statics seeded once at ctx construction (`FUN_141f4c950` @
   0x141f4cae1/0x141f4cb16 → `FUN_141f51fd0`/`FUN_141f52060`):
   - guard flags 0x1437f5d88/0x1437f5d84 (OBS lea targets); seed =
     `clock_frac % 10000` via `call 0x141b34aa0; call 0x141b34ad0;
     mul 0xd1b71759; shr edx,0xd; imul ecx,edx,10000; sub r8d,ecx`
     (OBS bytes) — same mod-10000 idiom on the fractional clock helper.
   - `FUN_141f51fd0`: stores seed+1 into slot 2 of 0x1437f5d90, then a
     `0x140a924e0` step + 11× `FUN_141f7a150` warm-up draws (OBS decompile).
   - `FUN_141f52060`: same for 0x1437f6340 plus history/histogram init.
   - Manager+0x150 seeded in the shared ctor `FUN_141f55110` (and the
     0x141f552a0 inlined copy): `FUN_140a928a0(slot2, FUN_141f50160(ctx,20000)+1)`
     where `FUN_141f50160` routes to the match static (0x1437f6340) if
     ctx+0x5228==2 else the other static (OBS: `cmp [rcx+0x5228],2; lea…;
     je→match` with recomputed targets 0x1437f6340/0x1437f5d90) — then a
     `0x140a924e0` step and two warm-up draws. **So the entrance
     per-manager generator is seeded from the match static's stream, not
     from the clock directly.**
4. Fixdemo ctx mode: `FUN_141f514d0` sets `ctx+0x5228 = 2` (OBS) for match —
   this is what routes the 0x19/0x43 draws to the match static.

## 4. What this means for the port
- Entrance: no scene-choice rule to port — play the configured list in
  order. The open part is who writes the list (upstream setup), not a
  random pick.
- Goal celebration: port the pool+filter+`Range(slot1)`+retry rule; seed
  the goal object with the match triple (t,t+1,t+2), where t is the
  session clock mod 10000. Deterministic given t.
- Foul/sub/end: no roll (foul's one roll is dead: `draw/100` over a
  2-entry table). Only the variant-level 0x19/0x43 draws use the statics.
- Variant draws: `FUN_141f7a100(rng,100)` = step slot-2 LCG, accumulate
  `(t + acc + 0xaa) & 0x3ff`, return `acc*n>>10`; seeded once per session
  from clock%10000 + warm-up. Reproducible only with the seed pinned.

## 5. Killed hypotheses (with killer)
- "Entrance scene picked by matcher on stadium/competition/weather keys" —
  killed: scene list is bulk-copied config input; matcher never called on
  the scene path (OBS call census in §1).
- "0x141f50160 returns a fixed table id" — killed: it routes between the
  two static RNGs on ctx+0x5228 (OBS branches + recomputed LEAs).
- "`FUN_140a92430` seeds the RNG" — killed: 3-instruction vtable store
  (OBS); seeding is `FUN_140a928a0`.
- "Foul scene 0x20002 involves a real 100-roll choice" — killed: index is
  `draw/100` ∈ {0} over a 2-entry table (OBS 0x14047ea4c–6d).
- "Goal RNG is separately seeded" — killed: seeded with the same (t,t+1,t+2)
  triple at 0x140442328–337 (OBS).

## 6. Remaining opens + next function
1. Upstream writer of the entrance 0xd8 config (which ENT_* list per
   competition): next = callers of `FUN_141f64780` via vtable (direct
   call-scan empty; go through FixDemoEntrance vtable 0x142c701f8 slot
   +0x68/0x70 users) or the object that calls `FUN_141f65bd0`.
2. Exact clock import behind `0x141b34a30` (IAT slot 0x141c2e400): next =
   resolve the import name from the on-disk PE import table.
3. `FUN_140a7ce90` filter semantics (bit-16 gate) — needed for exact goal
   pool; next = decompile it.
4. Slot → player mapping — CutsceneSlotRE's item, untouched here.
