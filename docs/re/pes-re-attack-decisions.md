# PES 2021 attacking decisions: offside detection, open carrier units, off-ball run scorers (AttackDecisionsRE)

Read-only RE against the full live image. Image base 0x140000000; all VAs below are image VAs.
Tools: `~/ghidra-projects/scratch/Unpacker/` (`resolve.py`, `dec.sh` on the PESLive project via
`proj.lock`, `annot.py`, capstone-over-`pes_full.bin` re-checks). Raw bytes re-checked for every
constant quoted; decompiler output used as structure hypothesis only.
Tags: **OBS** = raw bytes/decompile at the stated address. **INF** = inference with confidence.
Evidence decompiles kept in `~/ghidra-projects/scratch/AttackDecisionsRE/`
(`off.c`, `cross.c`, `through.c`, `shield.c`, `runs.c`, `run2.c`, `pull.c`, `pull2.c`,
`misc2.c`, `misc3.c`, `valid.c`, `setflag.c`, `writer2.c`, `xref1.c`).

Prior reports this closes items in: `pes-re-carrier-ai.md` §9, `pes-re-team-plans.md` §3.3/§6,
`pes-re-duels-fouls.md` §5.3.

Objects (names from usage; engine names unchanged from prior reports):
- `MA` = match-analysis object (`*(*(ctx)+0x18)`). Offside flags per team `t` (0/1) and category
  `c` (0..3): u32 bit sets at `MA+0x253c4 + 0x10·t + 4·c`, bit = player id (0..21).
  Flag frame `MA+0x253bc[t]`, float `MA+0x254ec[t]`, snapshot `MA+0x254fc+…[t]` (OBS readers
  `FUN_1405b6490` @0x1405b6490, `FUN_1405b5a00` @0x1405b5a00, `FUN_1405b6660` @0x1405b6660,
  `FUN_14050df20` @0x14050df20 — all clean `.trace`, decompiled in `xref1.c`).
- Ball-event record accessors used everywhere: `FUN_140a3e190` (event by idx), `FUN_140a6ce10`
  (event validity/test), `FUN_140a3e900`, `FUN_140a3e7d0`, `FUN_140a60300`, `FUN_140a62f20`,
  `FUN_140a62610` (team of / index remap), `FUN_140a627e0` (ball owner), `FUN_140a62690` (keeper).
- Player kinematics: pos `+0x55c/+0x560/+0x564`, vel `+0x574/+0x578/+0x57c`, facing `+0x554`,
  action-state `+0x5d0`, body-direction byte `+1`, role byte `+0x38`.
- `FUN_140a8f860(a, b)` = linear distance ‖a−b‖ (OBS: returns sqrt of the summed squares).
  `FUN_140439e50(out, from, to)` = bearing ° from→to. `FUN_140a92c80(θ)` = sin θ (deg).
  `FUN_1415a1780` = sqrt. `FUN_140a6c6d0(p, out, r/frames)` = velocity sample/average.
  `FUN_140458740(p, r/frames)` = heading average. `func_0x000141e532d0` = per-frame RNG u32
  (OBS call pattern; INF med on the exact distribution).
- Carrier ctx (`param_3` in carrier units, `bp` per carrier-ai.md): `ctx[0]` = carrier id,
  `ctx[1]` match state, `ctx[4]` carrier kinematics (`+0x38` facing, `+0x40` pos,
  `+0x6c` "ball at feet", `+0x6e`, `+0x6f`), `ctx[5]` = image A (receiver tables at `+0xb6c`),
  `ctx[7]` = plan (`+0xa0` plan id, `+0xc4` sub-kind).

---

## 1. Offside detection — the flag writer (RETRACTED by §9: this function is not the offside writer)

### 1.1 The writer is `FUN_14045e3db` (OBS)

`.pdata` begin 0x14045e3db, body `[[0x14045e3db,0x14045e469],[0x14045e470,0x14045e59e],
[0x14045e5a0,0x140461179]]` (OBS `.pdata` table). Clean native code (no protection junk);
decompiled in full in `AttackDecisionsRE/off.c` (1644 lines). It is the only code in the image
that writes the per-team offside bit sets: the 4-u32 scans at `MA+0x253c4 + 0x10·t + 4·c`
(0x14045e8f1/0x14045e9e5 `lea rcx,[r12+0x253c4]`) and the single direct caller of the
bit-writer `FUN_14045e030` at 0x14045f512 and 0x140461170 (OBS disasm; see §1.5).

### 1.2 When it runs / evaluation moment (OBS structure, INF med semantics)

- It runs over **both teams** (`r9d` = team index loop, `cmp r9d,2; jge` exit at 0x14045e472,
  repeat of the two-team pass at 0x14045f197 `cmp r9d,2; jl 0x14045e470`) and over **all 22
  player ids** in the middle pass (`ebx` = candidate id 0..21; `cmp ebx,0x16` guards;
  `mov r12,[rax+r12*8+0x40]` = the candidate's player object, OBS 0x14045e5b5).
- Entry gate (OBS 0x14045e4e5–0x14045e539): reads the newest ball event
  (`FUN_140a3e190`), `type = [rax]`, `ecx = type−1 <= 2`, else `type == 0x10`.
  If neither → skip to the flag-check-only path at 0x14045e922.
  **INF (high): the line evaluation runs only on ball events of type 1..3 and 0x10
  (passes/touches); on other events the function only re-checks already-flagged players.**
- Per-team enable byte at `*(G+0x2c0)+0x6e+t` is read in the flag scan (0x14045e8cb:
  `cmp byte [rcx+rax+0x6e],0; je 0x14045ea10` skips the punishment branch), confirming
  duels-fouls §5.1's "per-side enable".
- After the pass it stores the **flag frame** `MA+0x253bc[t]` analog in `MA+0x253b8[t]`
  (`mov [rsp+0x50],rax; movss [rax+rdi*4+0x253b8]` — see §1.5; the 4-byte index form
  `base + team*4` at 0x140460049… confirms the per-team slot).

### 1.3 The line computation — second-last defender + ball + halfway (OBS)

Three level branches per candidate (OBS 0x14045e5f9–0x14045ea08; register map: `r14` = the
candidate's player object, `rsi` = the line reference object, `rbx` = ball-event pointer,
playing-direction sign convention as in carrier-ai.md: `attack sign s = ±1`):

1. **Already-engaged test** (0x14045e5ff): `byte [rbp−0x70]` (the candidate's engagement
   flag loaded from `r14+0x5d0 == 0x7b` path) — if the candidate is the currently engaged
   player and his "active" byte `[r12+rax+0x1748]` is set, compute an **intercept-time race**:
   `t_p = 16000 / (rng·3600)` (0x14259d638 = **16000.0** @0x14045e6d6),
   `t_b = 25000 / (rng·3600)` (0x14259d63c = **25000.0** @0x14045e6ef),
   predicted positions via `FUN_140a6c6d0(p, …, 0.6944444/rng)` (0x14259bdd0 = **0.6944444**
   = 1/1.44 @0x14045e707), `d = ‖pred_p − pred_b‖`; if `d <= t_b` use `max(t_p, d)`.
   **INF (med): who-gets-there-first race between the candidate and the ball.**
2. **Ball-proximity gate** (0x14045e7ba): `comiss [rax+0x18], 1.5` — ball height > **1.5 m**
   (0x14259bddc @0x14045e7bf) skips the short path. Then the newest event must be
   type 1..3 or 0x10 (0x14045e809–0x14045e816), and the pass-target record at
   `MA+0x24838` must be valid (`cmp [r12+0x24838],0x15; ja 0x14045e927`).
3. **Line-vs-candidate test** (0x14045e830): `d1 = ‖cand.xz − target_xz‖`
   (`subss [r12+0x24834/0x2482c]`, sqrt), `t = d1 / [rbp−0x80]` (per-team
   divisor), minus a frame-rounded term `⌊rng·…+0.5⌋` (0x142565600 = **0.5**).
   If team byte `+0x6e` set → scan the 4 flag u32s for the candidate's bit; found and
   category < 2 → jump to the involvement comparison at 0x14045e914
   (`cmp eax,2; jge 0x14045ea10` — **category 2 falls through to the line test**).

4. **Line assembly** (0x14045ea05/0x14045ea08 → 0x14045ea10): `line = FUN_140439e50(...)`
   (bearing from the candidate's pos to the opponent reference at `+0x1414`) minus the
   ball-info angle at `+0x2944` (`FUN_140a57710` = `+0x2944` getter, OBS decompile).
   Then the **tolerance span** (0x14045ea3b–0x14045eb83, OBS):
   ```
   span = (|bearing_span| > 180) ? wrapped : raw
   if (|span| > 180) … wrap with 360
   S0 = (|span| > 40) ? 40 : span          # 0x14259be2c = 40.0 @0x14045eaba
   if (|S0| > 40 …)                        # second clamp against the same 40.0
   line_quality q = clamp01((40 − |S0|) / 50)   # 0x14259d670 = −50.0 @0x14045eaf0  (divisor)
   ```
   i.e. a ±40° span mapped linearly to a 0..1 quality (INF med: angular size of the
   passing lane as seen by the candidate).
5. **Per-player loop** (0x14045e470/0x14045f1db, `r9d` = team pass 0..1, `r14d` = slot pass,
   `ebx` = candidate id): for each candidate, fetch object
   (`[ctx+8]+idx*8+0x40`), skip if `action-state check 0x140a6e320` fails, compute
   `team_of_cand = FUN_140a62f20(ctx, cand)` and the **event-team coincidence test**
   `FUN_140a60300(event+4, &cand)` (0x14045f35b–0x14045f372: must be true to continue —
   **INF (high): the candidate must belong to the team that played the ball**).

### 1.4 The offside position test — second-last defender, ball, halfway, tolerance (OBS)

Per candidate (0x14045f378–0x14045f4df), with `rsi` = candidate object, `rdi` = defender
reference object, all in attack-signed metres:
- Fetch candidate base-x `rsi+0x55c`, defender base-x `rdi+0x55c` (`lea rbx,[rdx+0x55c]`
  0x14045f3b1), and the defender's predicted point via `FUN_140a6cf70(...)` slots.
- Bearings: `b1 = bearing(cand → defender_pred)`, `b2 = bearing(cand_pred → defender)`;
  `Δ = wrap(b1 − b2)`.
- **Projected line point**: `P = cand + step(Δ ∓ 360k)` via `FUN_140a8f7c0`
  (move-along-bearing by the span amount), then `Q = P + dir·d` with
  `d = ‖target − cand‖` (0x14045f456 `call 0x140a8f7c0`).
- **Gate 1 (0x14045f47d `call 0x140a8ff10`)**: must return true, else jump to the
  standing-check at 0x14045f4df. **INF (med): `0x140a8ff10` = "candidate beyond the
  second-last-defender/ball line" test** (it takes the projected point and the reference;
  exact internals not decoded — see open list).
- **Gate 2 (0x14045f486)**: `Δx = Q.x − ref.x`; `|Δx| <= 180` required
  (0x142598580 = **180.0** @0x14045f496), else the standing-check path.
- **Level tolerance (0x14045f49f–0x14045f4c0, OBS)**:
  ```
  if Δx < 0:   tol = −1.0                      # 0x142588218 @0x14045f4a5
  else:        tol = +1.0                      # 0x14256ae1c @0x14045f4af
  Δx' = Δx − tol · 8.0                         # 0x14259bdf4 = 8.0 @0x14045f45b
  L2 = (|Δx'|² as f32→f64→f32) vs 65.0        # 0x14259be48 = 65.0 @0x14045f4d2
  if 65.0 <= L2 → continue to flagging (0x14045f519); else standing-check (0x14045f4df)
  ```
  **So the level tolerance is 8.0 m of signed Δx before the 65.0 (m²) test — OBS.**
  (Units note: Δx is in metres; 8.0 m is large — see open item 1. The arithmetic is OBS;
  the unit reading is INF med.)
- **Standing-check fallback** (0x14045f4df): `Δh = cand_prog − ref_prog`
  (`[rbp+0x1bc] − [rbp−0x44]`); `rng·4.0` scaled (`0x1425656c0` = **4.0** @0x14045f4f1);
  flag only if `Δh > scaled` (0x14045f4ff `comiss; jbe 0x14045f548` = no flag).
  **INF (med): a candidate standing level/behind on progress is not flagged.**

### 1.5 The flag write — `FUN_14045e030` + the call sites (OBS)

`FUN_14045e030(rcx, edx=team, r8d=slot, r9d=cat)` (0x14045e030–0x14045e087, clean, 60-line
decompile in `setflag.c`): memmoves the two 11-entry id/distance arrays to insert the
candidate, then sets the per-team/per-category markers. Call sites:
- 0x14045f504–0x14045f512: `rcx=[rsp+0x40] (MA), r9d=0, r8d=r15d (slot pass 1/2),
  edx=r14d (team)` → category-0/1 style write with the line-quality float.
- 0x140461160–0x140461170: `rcx=rsi (MA), r9d=0, r8d=[rbp+0x1b8], edx=r14d` → the
  second write after the level test passes (`cmp eax,ecx; jl 0x14046115b` selects the
  store path; `test r15b,r15b; je 0x140460c5c` = skip when the category-2 flag is set).
- The line/frame stores: `MA+0x253b8[t]` (`movss [rax+rdi*4+0x253b8]`,
  0x140460049-region; index = team id) and the float/snapshot fields read back by
  `FUN_1405b6660` at `MA+0x253bc[t]` / `MA+0x254ec[t]` / `MA+0x254fc` (OBS readers).

Category mapping (OBS from the two call sites + `FUN_1405b6490`'s `c < 3` scan and the
`cmp eax,2; jge` at 0x14045e914): **0 = direct flag (involvement path 0x14045e914),
1 = line-test flag (0x14045f519 path), 2 = level/standing flag (0x14045f548 path),
3 = position-only, never punished** (duels-fouls §5.1's INF confirmed OBS by the scan
bounds). The byte at `[rbp+0x1a8]` (0/1/2) is the category accumulator.

### 1.6 Involvement re-check inside the writer (OBS)

- 0x14045f552–0x14045fb96: newest event type must be 1..3 or 0x10, else jump to the
  event-4 handler at 0x14045fb96 (`cmp eax,2; jbe` / `cmp ecx,0x10; je`).
- 0x14045f57b–0x14045f58a: `FUN_140a3e7d0(rsi, rbp+8)` + `FUN_140a62e40(r12, [rax])`;
  then `FUN_140a3e190` + `FUN_140a6ce10` gates per candidate (0x14045f595–0x14045fb2).
- 0x14045fb96–0x14045fc50: event `+4 == r14d` (team coincidence) required
  (`cmp r13d,r14d; jne 0x140460049` = abort); frame age
  `MI.frame(+0x1198) − event(+0x10)` must be ≥ 0 (`test edi,edi; js abort`);
  candidate byte `MA+0x249ba[cand]` must be non-zero (0x14045fb f8:
  `movzx r15d,[r12+rax+0x249ba]; test r15d; je abort`).
  **INF (high): `MA+0x249ba[cand]` = "candidate is an eligible attacker" flag.**
- Action-state filter 0x14045fc34–0x14045fc50: candidate action byte `+1` ∈ {0x52, 0x57}
  sets `[rbp+0x1a8] = 1` (INF med: these two action ids = receiving/trapping actions —
  the receiver is always flaggable).
- **Line-vs-candidate final** (0x14045fc63–0x14045fcc4, OBS):
  ```
  lhs = (byte)[rax+0x254] · [rdx+0x1414]        # team sign × defender line-ish
  if lhs < [rcx+0x18]: skip                     # 0x14045fc6e comiss/jb
  dif = 0.75 · [rdx+0x141c] − [rcx+0x1c]        # 0x14259bdd4 = 0.75 @0x14045fc81
  if dif <= 0: skip
  cand = [rdx+0x12bc] + [rdi+0x55c]             # candidate x + offset
  line = lhs + 2.0                              # 0x142565604 @0x14045fcad
  cand·s − line·s ?⇒ [rbp+0x1a8] = 1 (cmovae)  # 0x14045fcb9–0x14045fcc0
  ```
  **i.e. flag iff the candidate's x is strictly beyond (line + 2.0 m) in the attacking
  direction — OBS. The 2.0 m is the level tolerance of the final position test.**
- **Nearest-opponent race** (0x14045fd6d–0x14045fdf6, OBS): `d_cand = ‖line_ref −
  cand‖`, `d_def = ‖line_ref − defender‖` (both via sqrt); `d_def + 5.0 vs d_cand`
  (0x14259222c = **5.0** @0x14045fdde); flag only if `d_cand > d_def + 5`
  (0x14045fdea `comiss; jbe 0x14045fe26` = no flag). **INF (high): 5 m head-start rule —
  the candidate must beat the defender to the line point by 5 m.**
- **Action filter** (0x14045fdf8–0x14045fe20): candidate byte `+1` must be a receiving
  action (`FUN_140a6e5e0` true); then if byte `+0x38 == 3`, category = 1
  (`mov ecx,1; cmove eax,ecx` — **OBS: category 1 = first-time-receiving flag**).
- **Ball-event freshness** (0x14045fe52–0x14045fe88): `frames = rng·…+0.5`
  (0x142565600 @0x14045fe5d); flag only if `MI.frame − event.frame < frames`
  (0x14045fe86 `cmp ebx,esi; jae 0x140460001` = too old → no flag).
  **INF (high): the flag expires within a sub-second window after the event.**
- **Final proximity** (0x14045ffb7–0x14045ffdd): `d = ‖cand − event_spot‖` (sqrt);
  `tol = 17.0 − xmm2` (0x14259d614 = **17.0** @0x14045ff1a); flag only if
  `d > tol²`-form (`mulss; comiss; cmova` → `[rbp+0x1a8]`).
- **Urgency comparison** (0x14045ffb–0x14045ff30): `t = (17.0 − carry_dist)²`
  vs `d²`; `cmova` sets the flag byte.
- **Second write gate** (0x140460036–0x1404600b1): `comiss [rax+0x18], 1.5`
  (ball height ≤ 1.5 m), `xmm6 <= 15.0` (0x14259be0c @0x140461045), else abort;
  `[rax+0x1470] == 0` required; then the **frame-window test**
  `r13d − r12d vs ⌊rng·0.8⌋` (0x142572348 = **0.8** @0x1404600f8):
  `cmp eax,ecx; jg 0x14046113e` = window exceeded → no flag.
- **Event-table lookup** (0x1404600b1–0x140461105): scan 0x18 slots of stride 0x74 at
  `[rsi+0x20]` for `== edi` (candidate); slot byte `+0xd != 0` required
  (`cmp [rcx+rsi+0xd],0; setne al; je abort`).
- **Level arithmetic** (0x140461107–0x14046113a): `ecx = ⌊(r13d − r12d)·xmm14⌋`,
  `eax = ⌊|r13d − r12d|⌋`; flag only if `eax >= ecx` (`cmp eax,ecx; jl abort-path`).
  **INF (low): frame-count vs scaled-frame-count level comparison; exact inputs of
  xmm14 not traced.**

### 1.7 What "the line" is (OBS + INF)

- The halfway line is enforced by the sign test `s·cand.x <= s·halfLen − …` in the
  candidate loop (the `F[0] = 52.56` reads via `G+0x2aa8`) and the explicit
  `lhs < [rcx+0x18]` skip. **Own-half candidates are never flagged (OBS structure).**
- The second-last defender enters through the defender object `rdi` (fetched from the
  team table at `[ctx+8]+idx*8+0x40`) and the `+0x1414/+0x141c/+0x12bc` predicted-line
  fields. The ball enters through the event position and the `d_def + 5.0` race.
- **Level = onside**: the final test is strict (`cmovae` / `jg`), and every tolerance
  (2.0 m line margin, 8.0 m Δx margin, 5 m race margin) favours the attacker only in
  the sense that the flag needs a strict beyond — INF med on the phrasing, OBS on the
  strictness of the comparisons.

---

## 2. Carrier units now decoded

Answer form (carrier-ai.md §1): `{type, kind, a, b, f10…}`; pass = type 2, `a` = target
id, `b` = pass kind (0 short, 1 long/lofted, 2 centering, 3 through).

### 2.1 Cross — kind 7, `0x142129590` (OBS, `cross.c:1`)

```
gate: !FUN_140a4c930(ctx, carrier, ballinfo+0x88)   # "ball out / not in play" false
      and ctx4[0x71] != 0                            # ball owned / settled
carry = bearing(ball → carrier-predicted point)      # FUN_140474c40
if |wrap(carry − ctx4[0x38])| > 180: wrap ±360
if |carry − facing| <= 130.0:                        # 0x1425a9bf0 = 130.0
    aim = carrier.pos + attack_sign·F[4]             # F[4] = half-width 34.06
    aim += step(dir, 1.0)                            # FUN_140a8f7c0
    lane = FUN_14063cd90(aim → box point, len 5.0, wid 30.0)   # 5.0 @+0xe8, 30.0 @+0xe4
    if lane valid (FUN_1421b7d40 == 0):
        if !owned(ball at feet):
            recv = FUN_140a4ee40(other team, carrier-pred)     # predicted receiver
            if recv valid:
                lead = recv.pos + recv.vel · stat[0xba4]      # stat bytes +0xba4
                if dist(carrier-pred, lead) < 16.0: pass      # 0x14259be10 = 16.0
                else no answer
        tgt = FUN_142125ab0(ctx)                     # cross target picker (§2.5)
        if tgt != 0xff: answer = pass(tgt, b=2 centering)
        near-duel scan: over the 11 opponent slots (FUN_140507740 table),
            keep entries with quality < 100.0 and on-pitch;   # 0x14253ee30 = 100.0
            if entry == tgt and victim-duel-byte(+0x12a7)+10 <= carrier-duel-byte:
                f11 = 1                              # beaten-man flag, OBS
        else (f11 clear): if predicted-point-dist <= 900.0    # 0x1425c65f4 = 900.0 = 30²
            and carrier.x-advance <= 8.0:                     # 0x14259bdf4 = 8.0
            mate_slot = carrier%11; if mate record valid: f12 = 1   # OBS
```

### 2.2 EarlyCross — kind 8, `0x14212a2d0` (OBS, `cross.c:221`)

```
need ctx4[0x72] != 0, else return                     # early-cross armed flag
if !owned:
    recv = FUN_140a4ee40(other team, carrier-pred)
    if recv invalid: return
    lead = recv.pos + recv.vel · stat[0xba4]
    if dist(carrier-pred, lead) >= 100.0: return      # same 100.0 reach cap
aim = carrier.pos + attack_sign·F[0]                  # F[0] = half-length 52.56
reach = dist_along(carrier → aim)                     # FUN_140a8f820
lane_free = FUN_14063de20-along-attack(len = reach − 5.0, wid 50.0, h 5.0)
                                                     # 5.0 @0x14259222c, 50.0 @0x14259be34
carry_bearing = turn-predicted bearing (FUN_140458740, 0.6944444/rng)
lane_closed = FUN_14063dab0(width 3.0, budget 220, flag 1)  # 3.0 @0x14258c264, 220 @0x1425ca310
if lane_free or lane_closed: (either may be false — need both blockers below)
    if imageA[0xb8a2] or FUN_140a63950(ctx,team,7,0xff) or (plan[0x3c] != 1 and FUN_14063e960(ctx,carrier,0xe))
       or FUN_14063e830(ctx,carrier,3):
        tgt = FUN_142125bb0(ctx)                     # early-cross picker (§2.5)
        if tgt != 0xff and (owned or dist(carrier, tgt) >= 2.0):   # 2.0 @0x142565604
            answer = pass(tgt, b=2 centering)
```

### 2.3 BackSpace — kind 3, `0x142129110` (OBS, `cross.c:348`)

```
build shoot-zone struct (FUN_1402139630 on ctx5+0x898)
if zone says "shootable" (FUN_1406997a0(ctx5+0x9cc)):
    corridor_free = FUN_14063de20-along-team-angle(len 20.0, wid 80.0, h 1.0)
                                                     # 20.0/80.0/1.0 @+0x... (see §2.7)
    if corridor_free:
        recv = FUN_140a4ee40(other team, carrier-pred); if valid:
            lead dist vs 36.0 → return with NO answer if beyond   # 0x1425a9464 = 36.0
if ctx4[0x6f] == 0 and (owned or ball_height <= 1.0):             # 1.0 @0x14256ae1c
    if plan.id == 4 (PassThrough):                               # image plan +0xa0
        one_two_tgt = FUN_140211cb00(plan, 10)                   # plan slot 10
        if one_two_tgt valid and score_ok(FUN_1421226c0) and lane_ok(FUN_1421225e0 or dist<=225.0):
                                                     # 225.0 @0x14259be70
            answer = pass(one_two_tgt, b=3 through)
    else:
        tgt = FUN_142125140(ctx)                     # cut-back picker (§2.5)
        if tgt != 0xff:
            answer = pass(tgt, b=3 through)
            if mate pressured (FUN_14063e270) and (opp count>1 and mate openness>1):
                b = 0 (downgrade through→short)      # OBS: param_2[3] = 0
```
So BackSpace answers **through balls (b=3)**, usually to the cut-back picker target or the
plan's one-two target. This confirms carrier-ai.md §3.2 rule 5 ("cut-back beats the long
shot") structurally: kind 3 is listed right after Shoot.

### 2.4 Special — kind 12, `0x14212bb00` (OBS, `cross.c:497`)

```
need shoot-zone says "shootable" (FUN_1406997a0)
aim_dir = team_angle_table(ctx, attack_sign) + 180 (wrapped)
closing = FUN_140a4e720(ctx, carrier, aim_dir, 130.0, 8.0)   # 130.0/8.0
opp_line_val = MA[0x22e4c + team·0x16 …]                     # team-space table
cand_line_val = same-table entry for the candidate           # +4 float compare
aim_err = |wrap(aim_dir − carrier.facing)|; need >= 100.0    # 0x14253ee30 = 100.0
need closing and cand_line_val <= 16.0                       # 0x14259be10 = 16.0
need FUN_140a4c6e0(cand.pos, carrier.pos, sign, 0) == 0      # side test
scan the 11 opponent slots (FUN_140507740 table):
    skip invalid; slot_stat = rec[0xb6c+slot]
    need bearer-match: rec == carrier (0x558-field walk)
    need TB-angle <= 110.0 and TA-angle <= 80.0              # 110.0 @0x14259d628, 80.0 @0x142596e6c
    need FUN_14063e010(ctx, mate) (mate's own lane test)
    lo/hi = 6.0/25.0, or 10.0/3.0 when receiving (!owned)    # 6.0 @0x14259bdec, 25.0 @0x14259be20,
                                                             # 10.0 @0x142588210, 3.0 @0x14258c264
    need lo <= rec.dist <= hi
    need FUN_14061e7c0(line-value at mate) <= (imageA[0xb58] + 3.0)²
    need (!receiving or openness>2) and rec.shoot-zone <= 1.0
    need lane FUN_14063dab0(width 3.0, budget 120.0)          # 120.0 @0x14256ae30
    need FUN_14063e960(ctx, mate, 0x27) and FUN_14063e270(ctx, mate, 0)
    need bearing-match >= 45.0 or speed <= 15.0              # 45.0 @0x14259be30, 15.0 @0x14259be0c
    → answer = pass(mate, b=3 through)
```
**Special is a through-ball unit (b=3) for a mate breaking the opponent line
(TB/TA angle gates, line-value test).** The `0x27`/`0x0` skill-ish args to
`FUN_14063e960`/`FUN_14063e270` are passed as immediates (OBS).

### 2.5 Cross target pickers (OBS, `misc2.c`, `misc3.c`)

- `FUN_142125ab0` (Cross): over the 11 opponent-half mates (`uVar8 = team·0xb + i`,
  skip self), keep those passing `FUN_142123290`; among survivors pick the one with the
  **larger** `FUN_14063d8a0(ctx, mate, 0x31)` byte (strict `<` replacement → last max
  wins; 0xff if none).
- `FUN_142125bb0` (EarlyCross): same shape with `FUN_142123750`.
- `FUN_142123290` (Cross validator, 0x142123290, ~0x100 B): needs mate record valid
  (`FUN_1402118d20 == 0xb`), predicted-point match vs the carrier's predicted point,
  carry bearing within **90°** (0x14259be5c @0x14212397f — via the capstone const map),
  mate speed vs **60.0** (0x14259a764 @0x142123a0e), lane area **400.0**
  (0x1425a2670 @0x142123a99 = 20²).
- `FUN_142123750` (EarlyCross validator, 0x142123750, ~0x900 B): full box-presence test —
  builds the shoot-zone struct (`FUN_140698cc0`, `FUN_140698d10`),
  needs `FUN_140699710` **and** `FUN_1406997a0` (the two zone predicates, OBS tiny
  decompiles in `valid.c`: `+0x170 >= 0`, bit0 of `+0x16c` or `+0x17c/+0x178 < 3`,
  plus `+0x17c < 2` / `+0x178 == 0` respectively), predicted-point distance vs **400.0**,
  facing/line margins **5.0** (0x14259222c), **1.0** (0x14256ae1c), **30.0**
  (0x142591140), span divisor **25.0** (0x14259be20), angle wrap **180.0**, sign ±1.0,
  attribute-score gate vs **49.0** (0x14259d620 @0x142123e5a), duel-byte
  `+0x31/+5` comparison, packed-bytes test (`(b[0x2c]+b[0x28])&~1 <= 0xaa`;
  **0xaa = 170** immediate @0x142123f20 — OBS), target-quality `max(score,64.0)`
  (0x14259be44 = **64.0** @0x142123f7c), final lane call
  `FUN_14063de20(len 10.0, wid 6.0, h 2.0)` (10.0 @0x142124026, 6.0 @0x142124034,
  2.0 @0x142124018 — OBS).
- `FUN_142125140` (BackSpace cut-back picker, 0x142125140, `misc2.c:98`): over the 11
  mates, keep those passing `FUN_1421226c0`; needs mate-record + lane
  `FUN_14063de20(len = dist, wid 120.0, h 1.0)` (120.0 @0x14256ae30), target-quality
  `<= 225.0` (0x14259be70 @0x1421253ea) or `FUN_1421225e0`; tiebreaks prefer the
  **closer** mate (dist² `fVar26/fVar27` vs 15.0 = 0x14259be0c) and the better
  `FUN_14061e7c0` line-value; returns 0xff if none.
- `FUN_1421226c0` (mate score gate, 0x1421226c0, `misc2.c:396`): record valid
  (`FUN_1402118d20 == 0xb`), side test `s·m.x <= line+…`, danger byte clear,
  openness `rec[0x3c] <= 1.0`, then the corridor/pressure sub-tests with **6.0**
  (0x14259bdec), **0.3** (0x142590d98 = 0.3 @0x142122943), **0.5** (0x142565600),
  **5.0** (0x14259222c), **8.0** (0x14259bdf4), **0.6944444**, **45.0/20.0/40.0**
  (0x14259be30/0x14259be1c/0x14259be2c), **90.0/120.0** (0x14259be5c/0x14256ae30).
  Returns void; 0 = reject (early `return` before the accept label), accept falls to
  `LAB_14212322b`. (OBS structure; exact accept/reject polarity confirmed by the
  callers' `!= 0` tests.)
- `FUN_1421225e0` (0x1421225e0, tiny): `addss 0.5` only — a score nudge, not a gate
  (OBS).

### 2.6 Through-ball choosers (OBS, `through.c`)

- `FUN_142127ff0(ctx)` (PassForward's through, `through.c:1`): over the 11 mates
  (`uVar22 = team·0xb + i`), needs record valid + `FUN_142124cc0(ctx, mate, 1, 0)` +
  `dist >= 5.0` (0x14259222c); picks the survivor with **min carrier-facing error**
  under the caps:
  ```
  cap = 20.0; if (rec.openness >= 100.0 and rec.class < 4) cap = 15.0
                                                     # 20.0 @0x14259be1c, 15.0 @0x14259be0c,
                                                     # 100.0 @0x14253ee30
  need dist <= cap and s·mate.x <= s·carrier.x-limit and s·mate.x <= s·F[6]+…
                                                     # F = G+0x2aa8 pitch table, +0x18
  need dist(carrier, mate-pred) vs 10.0/15.0 windows # 10.0 @0x142588210, 15.0 @0x14259be0c
  need mate speed <= 60.0 or (dist<10 and receiving and duel-byte(+0x1b)+0x14 > 0x4b)
                                                     # 60.0 @0x14259a764, 0x4b = 75
  need facing_err < 135.0                            # 0x14259be60 = 135.0
  duel-table race: ≥3 entries with entry < 10.0, else keep current best when the
  new mate is not strictly more advanced (s·x compare)   # 10.0, 2.0 (@0x142565604) margins
  ```
  Returns mate id or 0xff. Tiebreak = more advanced (larger `s·x`); the
  `FUN_14063e960(ctx, carrier, 0xc)` result adds 0x14 to the duel byte (skill-12
  one-touch-pass bonus path, OBS).
- `FUN_142126b70(ctx, …)` (PassSafety's short-safe receiver, `through.c:192`): full
  cascade with the **explicit score formula** (OBS):
  ```
  spd = ‖carrier.vel‖
  w_spd = spd<5 ? 1 : spd<25 ? (spd−25)/(−20) : 0     # 5.0 @0x14259222c, 25.0 @0x14259be20,
                                                     # −20.0 @0x1425a946c
  w_dist = dist<10 ? 1 : dist<30 ? (dist−30)/(10−30) : 0   # 10.0 @0x142588210, 30.0 @0x142591140
  need = w_spd·30 + 60 + w_dist·30 (+20 if openness==4, +30 if ==5; =120 in danger)
                                                     # 30.0/60.0/20.0/30.0/120.0 constants above
  if openness==0: skip unless danger/pressure-count path
  need2: dist <= need  (else skip)
  … lane tests: width dist/5 clamp(4,12) via FUN_14063dab0 (220-frame budget),
  receiver-lead test (0.4·dist vs limits 10/3), keeper-race test,
  press-count test, side test s·m.x, duel-byte windows, final tiebreak:
  pressed (nearest opp ≤ 9 m) → smaller turn angle wins, else more advanced wins.
  ```
  (OBS arithmetic; field meanings INF med — matches carrier-ai.md's partial decode.)
- `FUN_142128490(ctx)` (PassSafety's safe through, `through.c:815`): needs the team-space
  value `> 1600.0` (0x14259ce40 = **1600.0** = 40², OBS `*pfVar1 <= 1600 → return 0xff`),
  carrier on the right side of the halfway sign test, then over the opponent table:
  keep entries with quality `<= 900.0` (0x1425c65f4), openness race
  (turn-error `< 70.0` → skip; 0x14259be4c = **70.0**), record valid, mate side test,
  `dist·0.5`-scaled lane origin, bearing `< 135.0`, openness > 2, shoot-range
  `<= 60.0`, `s·aim.x vs s·lead.x` order test, `dist <= 100.0`,
  `FUN_1421226c0` must pass, then the final lane
  `FUN_14063de20(len 10.0, wid 60.0, h 5.0)` (10.0/60.0/5.0 immediates) → return mate.
  0xff if none.

### 2.7 Dribble sub-kind + shield/hold (OBS, `shield.c`)

- `FUN_14212f1c0` (0x14212f1c0, 481-line decompile): the sub-kind chooser. OBS gates:
  - If `|wrap(team_angle − facing)| >= 30` or `|wrap(desired − facing)| >= 30`
    (0x142591140 = **30.0**) or the carrier is behind halfway (`s·x <= 0`), or
    `FUN_14212e970` fails → the keep/dribble branch is skipped (fall to `FUN_14212fdd3`
    = no sub-kind change).
  - If not turning (`FUN_140a6e300` false) and plan.id−1 > 2 (plans 1/2/3 excluded…
    polarity: `bVar1 = 2 < plan−1`): needs
    `|wrap(desired − team_angle)| <= 20·…+ 40` (0x14259be1c = **20.0**,
    0x14259be2c = **40.0**, 0x14253ee30 = **100.0** factor) else keep.
  - Else (turning): needs `|wrap(desired − facing)| <= 60` (0x14259a764 = **60.0**).
  - Then if the box-pressure test passes (`imageA+0xb54 vs +0xb3c`, sector `+0xb44`):
    the full corridor search with the **lane constants**: `dist/5 clamp(4,12)`
    width, **220**-frame budget (0x1425ca310), **12.0** min width (0x14259be04),
    `1.2·dist` lookahead (0x14259d5d4 = **1.2**), body-block test `FUN_140a53df0`,
    and the keep-vs-dribble economics:
    ```
    keep_value  = 10·w_face + dist + 6·w_box + 6·w_plan + 5·w_shoot
    dribble_val = 50·w_face + 60·w_spd + 0
    keep_bonus  = 10 if ball-zone test (FUN_140698950) else 0/5·…
    dribble_dir = face ± clamp(dribble_val·0.5 …)   # 0x142565600, 0x142590d98 = 0.3
    ```
    (OBS arithmetic; weight meanings INF med.)
- `FUN_142130ee0` (0x142130ee0, the shield branch): **OBS decision order**:
  1. If the last two ball events are both type 6 / sub 6 / subsub 0x11 and from
     different teams → plain dribble answer (type 3, sub = `FUN_14212f1c0`,
     dir = `+0x2b0`).
  2. If `unit+0x264 > 0x15` (no marked opponent) → plain dribble answer.
  3. Else compute the **marker's predicted intercept**: `P = marker.pos +
     marker.vel · t` (t = `ctx4[0x3c]`), `d = ‖P − ball_pred‖`; if `d² > 16.0`
     (0x14259be10 = **16.0** = 4²) → plain dribble answer.
  4. Else compare the marker's and carrier's `MA+0x24710` ratings: if carrier's
     `<= 6.25` (0x1425a9be4 = **6.25**) → plain dribble answer.
  5. Else the **shield geometry**: ball-pred vs marker-pred corridor
     (`FUN_140a8f7c0` steps of 5.0 = 0x14259222c, `FUN_140a90d30` corridor test);
     then **if `unit+0x2b8 == 0` and `FUN_14063e960(ctx, carrier, 0x25)`** →
     **answer type 4 (feint) sub 0x11** (OBS: `*param_2 = 4; param_2[2] = 0x11`) —
     the shield-feint.
  6. Else if the event is a tackle-like one (type−3 < 2…) or carrier progress < 2:
     **answer type 3 sub 3** (dribble toward `facing + 2·corridor/3`,
     0x14258c264 = **3.0** divisor) — the shield-dribble.
  7. Else **answer type 3 sub 3** with the plain direction (hold dribble).
  **So MOVE_STOP_FRONT/GOAL (sub 0/1) are NOT emitted here: the shield is sub 3
  (dribble-in-place toward the corridor) or type-4 sub-0x11 (shield-feint).**
  Sub 0/1/2 (stop-front/stop-goal/stand) come from the adopter/executor mapping, not
  from any think unit — OBS from the absence of 0/1/2 stores in all decoded units.
- `FUN_142131810` (0x142131810, "let it run" pre-check, 262-line decompile): OBS gates
  for the type-3 sub-5 answer — not-owned, team flag, last-event-pass-by-team,
  `0 <= s·x <= halfLen−20`, ball speed 40–60, within 20° of attack dir, ≥15 m room,
  mate level/ahead within 8 m lateral and <5 m ahead (matches carrier-ai.md §5.4).

---

## 3. Off-ball run scorers (OBS structure + literal score pieces)

Selector slots (team-plans.md §3.1 table confirmed in these decompiles):
- ChanceSpaceRun: score 0x140606060 (`runs.c:1`), gate 0x140607a60.
- Chance executor 0x140606cd0 (`run2.c:308`).
- CounterSpaceRun: score 0x14060ab00 (`run2.c:756`).
- SecondLine: score 0x14060d210 (`run2.c:1071`).
- Diagonal/LineBreak: score 0x140613910 (`run2.c:1176`).
- PullAway: score 0x14061dee0 (`pull.c:1`), scorer 0x14061e810 (`pull2.c:1`).
- Score: 0x14061a640 (`pull2.c:162`).
- Overlap gate 0x140601580 (`runs.c:1626`), executor 0x1406013d0 (`runs.c:1895`).
- PostPlay executor 0x14061f940 (`pull.c:139`).

### 3.1 ChanceSpaceRun score `0x140606060` — bitmask + timing, no attribute term (OBS)

- Reads the candidate's 12-byte slot record at `sel+0x60+slot·0xc`; **returns void
  (score 0) if the record's float at +0x68 is < 0** (0x140606060 head).
- Bitmask dispatch on the slot bit in three masks: `sel+0x288` → mode 2 path
  (`FUN_140609970` test), `sel+0x28c` → mode direct-write (record type 2, stamp
  `MI.frame`), `sel+0x290/0x294` → modes 4/… The executor `0x140606cd0` fills the
  `+0x2a0…+0x2c4/700` target slots and the `uStack_158` 11-bit candidate mask.
- **No stat/attribute read in either function** (OBS: no `FUN_140a3af90`/`0x141b59e20`
  skill-mask or stat-table calls in either body). Runner choice = first-listed valid
  candidate (bitmask order), not a max-score.

### 3.2 Chance executor `0x140606cd0` — the literal score formula (OBS)

Per candidate `uVar23` (team·0xb+i), after the gate cascade
(`FUN_1405a4830`, `FUN_1405938a0`, `FUN_1405969b0`, state checks, `FUN_140a40000`
slot-state, `FUN_1405a7fe0/…be0` free-lane tests, `FUN_14060a750`, `FUN_140593430`,
`FUN_140608570/…7a0/…9350/…8ac0` corridor tests, `FUN_1405a5b20/…0d20` duel tests):
```
dh = mate.z − line.z
if 12.0 < dh: score0 = (dh − 30.0) / −18.0     # 12.0 @0x14259be04, 30.0 @0x142591140,
                                               # −18.0 @0x1425c7e70
else score0 = 0
base = 15.0; if (no-chance-flag) base = 21.0   # 15.0 @0x14259be0c, 21.0 @0x1425c84bc
score = base + score0·5.0                      # 5.0 @0x14259222c
if (runners-needing-service > 0): score = max(score, 24.0)   # 24.0 @0x1425c48c4
if (gate6): score = 26.0                       # 26.0 @0x14259857c
score += awareness_bonus                       # fVar34: 0..18.0, see below
if (gateA and score <= 1000.0): score = max(score, 22.0)     # 1000.0 @0x14256ae34, 22.0 @0x1425c48c0
score = min(score, 37.0)                       # 37.0 @0x1425cd82c
if (cStack_14b and score >= 35.0): score = max(score, …)     # 35.0 @0x14259a760
…second path: dh2 vs 42.0 (0x1425cd830), dist² vs 400.0 (0x1425a2670)
   and vs 576.0 (0x1425c8824 = 24²); keeper-rate vs 49.0 (0x14259d620)…
accept (set bit uStack_150 in uStack_158) iff score+awareness < |mate.x − line.x|
   (OBS: `fVar30+fVar34 < |*pfVar18 − *pfStack_140|` → goto accept-label)
```
**Offensive-awareness term (OBS)**: `bVar19 = stat_byte(+0x24)` (the live-stat copy
byte — INF med = Offensive Awareness label), `+10 if skill-bit 0xd set`
(`(mask>>0xd)&1` on the `0x141b59e20` sub-object → `lVar16+0x48` — OBS bit test):
```
if 75.0 < aware: bonus = 18.0                 # 75.0 @0x1425aa1bc, 18.0 @0x14259be18
elif aware <= 75: bonus = 0
elif aware < 95.0: bonus = (aware − 75.0)/20.0 # 95.0 @0x1425c594c, 20.0 @0x14259be1c
bonus_total = bonus · 18.0 … (folded: max +18.0)
```
(OBS arithmetic; the skill-bit-0xd = "Goal Poacher"-family INF low — bit 13 in engine
order is unlabelled in carrier-ai.md §6.1's list.)
Other literal gates in the cascade: `−2.4` (0x1425cd834), `−5.0` (0x14259be7c),
`16.0` (0x14259be10), `45.0` (0x14259be30), `80.0` (0x142596e6c), `60.0/30.0`
box checks.

### 3.3 CounterSpaceRun `0x14060ab00` — count cap + reach line (OBS)

- Cap: `bVar22 = 2 + (byte)[b8a4]` (2…6 by the byte value); if more than `bVar22`
  of the 11 slots pass `FUN_1405a4830` → return current mask (no new runner).
  **So at most 2–6 counter-runners depending on team byte `+0xb8a4` (INF med:
  counter-attack instruction level).**
- Per candidate (from the `MA+0x2470c` nearest-opponent table): duel-byte test
  (`+0x4c00` entry > 2 required), side test `s·m.x`, then the **reach line**
  (OBS):
  ```
  reach = clamp01((s·opp_line − 5.0) / (5.0 − F[6])) · 30 + 15
                                                     # 5.0/30.0/15.0
  ```
  then the **awareness interpolation** (OBS):
  ```
  a = stat_byte(+0x14)                             # INF med: Offensive Awareness variant
  w = a>40 ? (a>99 ? 0 : (a−99)/−59) : 1           # 40.0 @0x14259be2c, 99.0 @0x1425c48cc,
                                                   # −59.0 @0x1425cd974
  score = base·m(…)·… ; accept iff rng/3·w <= uStack_e8 (frame budget)
  ```
- Side containment: `s·m.x <= F[6]−10` or lane tests `−17.0/−3.0`
  (0x1425c7224/0x14259d654); fallback `−8.0` (0x1425c7ba4), `8.0/17.0` windows
  (0x14259bdf4/0x14259d614) with divisor `−9.0` (0x1425a9680), scaled by **6.0**
  (0x14259bdec) + **3.0** (0x14258c264).
- Final: `score(via vtable slot 0x28)`, accept iff strictly greater than every
  other candidate's score (`fVar30 < fVar32 → drop uVar23 bit`) — **this selector
  IS a score max** (OBS `LAB_14060b360` loop), unlike Chance.

### 3.4 SecondLine `0x14060d210` — rating + 14.0 bonus (OBS)

```
if record(+4) <= 0: return 0
if already-runner (+0x29c == id): return 0x17 (23)
if !free-agent gate (FUN_140a64030(·,9,·)): 
    if no-service and !FUN_14060cb50: return 0
if +0x28c == id: write record type 9 …; if +0x290 == id: write link byte …
score = MA_rating_table[idx]                   # MA+0x24710-style via FUN_140a71500
if FUN_1405c0f00(ctx, id, 0x1d): score += 14.0 # 0x1425c48b8 = 14.0
return score
```
No attribute term (OBS).

### 3.5 Diagonal/LineBreak `0x140613910` — corridor + 100·… (OBS)

- Returns **100.0** (0x14253ee30) immediately if the slot bit in `sel+0x288` is set
  (already running) — OBS head.
- Gates: on-pitch, not the designated player, `FUN_140a53460` false, duel entry ≠ 0,
  slot-state `== 0x2f` and `FUN_140a40000(·,0x21,·)` and `FUN_1406171c0`…
  (`0x21`/`0x2f` immediates OBS), `+0xb748` entry `== 0x30`, `FUN_140a4c9a0(·,−8.0)`
  (0x1425c7ba4), no-veto style bits 8/0xb/0xd (`FUN_140a64030` ×3 — OBS).
- Geometry: `1.0·(m.x − line.x)` within pitch (`−1.5` variant when
  `FUN_140696ba0`; 0x14259d654 = **−1.5**), depth beyond `MA+0x2444/…` table +0x30,
  width `<= 8.0` (0x14259bdf4), angle gates, urgency `(line+0x48 …)·…`,
  `FUN_140615180/…5380/…7480/…134c0/…12fc0/…13620/…12d70` corridor battery,
  `FUN_1406962c0/…95d60` target write, `FUN_1405a3080` commit; the commit value is
  `fVar26` (the depth margin) — **OBS return = depth margin, not a 0..100 score**.
- Landing test: `(base+range)` comparisons with **7.5** (0x14259ce3c),
  **56.25** (0x14259be3c = 7.5²), `−3.0`, **9.0**, **8.0**, **100.0**,
  ±1.0, 0.1, 0.5, 15.0, 360/180.

### 3.6 PullAway `0x14061dee0` / `0x14061e810` (OBS)

- `0x14061dee0`: collects up to **12** lane candidates (`FUN_14061e2e0` per lane id
  0..11), picks index `(stat_byte + MI.frame + id) % count` — **OBS round-robin with
  the frame counter and a per-player stat byte** (`*(G+0x360)+4` frame-random byte +
  `MI+0x1198` frame, `% uVar7`), writes target + link byte, returns the **angle
  score** `180 − |wrap(target_bearing − facing)|`, scaled by **0.8**
  (0x142572348) when the MA rating `< 10.0`, and by **1.4** (0x14259d5d8) when the
  `0x2f`-lane test passes. Constants: `−4.0` (0x1425c7678), `10.0`, `0.5`, ±360/180/±1.
- `0x14061e810` (gate): returns 1 (reject) unless: open play (`MI+0x116c == 2` →
  reject… polarity OBS: `== 2 → return 1`), team byte `+0x255` set, MA rating
  `>= 49.0` (0x14259d620), `FUN_14061ece0` false, per-slot cooldown
  (`entry[8] − entry[4] >= rng/2` **and** `>= rng·2` with `team+0x78 != id`…) —
  OBS double-frame-gate.

### 3.7 Score `0x14061a640` (OBS)

Gate-only (returns 0/1): needs open play, `+0x255`, any of owner/target/key-player
valid, keeper-rush check (`stat 7` + `FUN_140a760e0` reach vs 0.5), not the
pass-target pair, `FUN_1405a7fe0/…be0` false, `FUN_14061b110`/`…1ad10` false, cooldown
`entry[8]−entry[4] >= rng`, `FUN_140645ba0`, the `rng·3+0.5` window test, corridor
`FUN_140a72fb0(…, 12, …, 10.0)` (12/10.0 immediates), `FUN_1406431e0`,
`FUN_140a53460` false, keeper-action `FUN_140a6e340` false, reach-vs-`110.0`
(0x14259d628), side tests, `196.0` (0x1425c4c18), `−1.5` (0x14259d650), `100.0`,
`30.0/5.0/2.0/12.0/−4.0/18.0` windows.

### 3.8 Overlap gate `0x140601580` + executor `0x1406013d0` (OBS)

- Gate: needs concept `0x1d` enabled (`FUN_140ca1a50`), pass-target on the same team,
  open play or set-play-mode-2 with the taker in action `0x28` (`'('` = 0x28, OBS
  char compare), `+0x255` set, `param_3[0x24] >= 1` (at least one runner slot),
  not already overlapping, fewer than `param_3[0x24]` slots already in action 0x27,
  newest event not (type 3, sub 3), side test `s·m.x`, `FUN_1406420b0`,
  `FUN_1405a2540(·,0x27)` when slots < 2, facing test `>= 30.0` when rushed
  (0x142591140), then the per-slot loop: slot-state machine `FUN_140a40000(·,0x27,·)`,
  busy checks, `+0x14a8/+0x8d40` assignment tests, then **timing race**:
  `behind = (m.z < c.z)` picks limit **120.0** (0x14256ae30) or **130.0**
  (0x1425a9bf0); needs `|wrap| >= limit` (else next slot); then distance-vs-`100.0`
  or facing-vs-`130.0`; `FUN_140640cc0` must fail; `m.z` within `F[1]−6.0`
  (0x14259bdec) and ahead of the candidate… → write action 0x27 + link `0x10`.
- Executor `0x1406013d0`: needs the runner's zone (`FUN_140a4d2c0`) to equal the
  team's `+0xb824` zone for either the runner or the carrier, slot `+0xb748 == 0x30`,
  `+0xb6f6 == 0`, `FUN_1405a4830`, `FUN_140a4c9a0(·,−4.0)` (0x1425c7678 = **−4.0**),
  sprint-reach `22.5·0.8 <= speed` (0x14259d618 = **22.5**, 0x142572348 = **0.8**),
  else `FUN_140640cc0`.

### 3.9 PostPlay executor `0x14061f940` (OBS, `pull.c:139`)

Writes link byte 0/1/2 per team-plans §3.2 (lane-1 test `0x140620ca0` / lane-2 test
`0x1406210e0` decompiled in `pull.c:181/316`; lane executors `0x14061f8b0/…fa40`
in `pull.c:416+`). No attribute terms (OBS).

### 3.10 Timing (run start cadence) (OBS)

- Chance/Counter gates share the `gameSpeed·0.2+0.5 ≈ 11`-frame cooldown
  (team-plans §3.1, confirmed in the gate bodies).
- SecondLine: 54-frame (1 s) cooldown. PullAway gate: double frame-gate
  (`rng/2` and `rng·2`, §3.6). Score: `rng`-frame cooldown.
- The per-slot timer block `ctx+0xa2cc`, 0x34 B/slot, `+4` = last-trigger,
  `+8` = now (OBS in all gate bodies).

---

## 4. Attribute/style bindings found (answers carrier-ai.md §9 item 5 in part)

- **Stat byte `+0x24`** (live-stat copy; INF med = Offensive Awareness): read in the
  Chance scorer (`run2.c`) with the `+10 if skill-bit 0xd` bonus and the
  75/95 → 0..18 bonus map (§3.2).
- **Stat byte `+0x14`** (INF med = a second awareness/role byte): read in the Counter
  scorer with the 40/99 → 0..1 weight map (§3.3).
- **Stat bytes `+0x1a/+0x1c`** (short `+0x1c−+0x1a` compared with 250/249-frame
  windows): read in `FUN_14045e860`-region code at 0x14045e856… (movd/cvtdq2ps/addss
  into the offside line-quality sum — OBS instructions; meaning INF low).
- **Skill-bit 0xd** (engine order): `+10` awareness bonus in the Chance scorer (OBS
  bit test; label INF low).
- **`FUN_140a63950` flag ids** (OBS immediates): 7 (EarlyCross fallback),
  0xe/3 (EarlyCross receiver checks), 0x27 (Special mate check), 0x25 (shield-feint
  arm), 0x31 (cross-picker quality byte via `FUN_14063d8a0`), 10 (Diagonal commit).
- **`FUN_140a64030` veto ids** (OBS immediates): 8/0xb/0xd (Diagonal),
  9/0x11 (Chance/SecondLine), 0x10 (Overlap timing), 0x1e/0x1f/0x21/0x2f (selector
  slot-states).
- **Stat words `+0xba4`** (carrier ctx5): velocity-lead scale for the predicted
  receiver in Cross/EarlyCross (OBS float math).
- No playing-style-bit (`style & mask`) test was found in any decoded carrier unit
  or run scorer (OBS absence over ~15 function bodies).

---

## 5. Live validation

**Not performed: the game is in MENU** (`pes_state.py` → `STATE=MENU` at session start;
`*(u64*)0x1436F3DC8 == 0`, no match loaded). Per the assignment, validation runs only
if OPEN_PLAY allows — it did not, so no `/proc/<pid>/mem` sampling was done and no
offside flag or cross decision was observed live. All formulas above are static-binary
OBS (raw bytes + decompile from the full image, byte-identical live/disk per
protection §1.2) awaiting live confirmation.

---

## 6. What is now implementable (formulas + constants with VAs)

1. **Offside flagging** (`FUN_14045e3db` @0x14045e3db, writer `FUN_14045e030`
   @0x14045e030): evaluate at ball events of type 1..3/0x10; per team, per candidate:
   team-coincidence (`FUN_140a60300`), `MA+0x249ba` eligibility, action filter
   {0x52,0x57}, strict-beyond test vs (line + **2.0** @0x142565604), 5 m race rule
   (**5.0** @0x14259222c), 8 m Δx + 65 m² level test (**8.0** @0x14259bdf4,
   **65.0** @0x14259be48), 17 m proximity update (**17.0** @0x14259d614),
   0.8·rng frame window (**0.8** @0x142572348), 1.5 m ball-height gate
   (**1.5** @0x14259bddc), ±40° span → quality (/−50.0 @0x14259d670, /−30.0
   @0x14259be88, clamp 15.0 @0x14259be0c), categories 0/1/2 punishable, 3 not.
2. **Cross** (`0x142129590`): 130° carry gate, F[4] aim, 5×30 lane, 16 m receiver
   lead, centering answer + f11/f12 flags.
3. **EarlyCross** (`0x14212a2d0`): F[0] aim, reach−5 × 50 × 5 corridor, 3.0/220 lane
   pair, 100 m reach cap, 2 m min, centering answer.
4. **BackSpace** (`0x142129110`): 20×80×1 corridor, 36 m reach abort, plan-4 one-two
   vs cut-back picker, through answer with short-downgrade rule.
5. **Special** (`0x14212bb00`): 100° aim gate, 16 m line gate, 110°/80° mate gates,
   6–25 m (10/3 receiving) window, (line+3)² test, 3.0/120 lane, 45°/15° commit,
   through answer.
6. **Through choosers**: `0x142127ff0` (20/15 caps, 135° facing, 10/15 windows,
   60-speed gate, duel-75 bonus), `0x142126b70` (the explicit need-score formula),
   `0x142128490` (1600 team-space gate, 900 quality cap, 70° race, 10×60×5 lane).
7. **Shield/hold** (`0x142130ee0` + `0x14212f1c0`): 16 m² intercept gate, 6.25 rating
   gate, shield-feint (type 4 / 0x11) vs shield-dribble (type 3 / sub 3) branches;
   sub 0/1 never emitted by any think unit.
8. **Run scorers**: Chance bitmask + 75/95 awareness→18 bonus; Counter 2–6 cap +
   40/99 awareness weight + score-max; SecondLine rating + 14.0; Diagonal depth
   margin with the corridor battery; PullAway 12-lane round-robin + 180−|Δ|
   (×0.8/×1.4); Score gate cascade; Overlap 120/130 timing race + 22.5·0.8 sprint
   executor; PostPlay 0/1/2 lanes. All cooldowns (11 f / 54 f / frame-gates).

## 7. Still open (with what was tried)

1. **Offside line internals `FUN_140a8ff10`** (the beyond-the-line predicate called at
   0x14045f47d) and the **xmm14 level comparison** (0x140461107–0x14046113a): not
   decompiled (time). Next: `./dec.sh out/x.c 0x140a8ff10` + annot.
2. **Unit of the 8.0 m Δx tolerance**: the arithmetic is OBS but 8 m is unfootball-like;
   live sampling of `MA+0x253b0..0x25510` around a scoring pass is still the way to
   confirm scale (needs OPEN_PLAY; game was in MENU all session).
3. **`FUN_140a60300` / `FUN_140a62f20` / `FUN_140a62610` semantics**: used as
   team-coincidence/index oracles throughout; confirmed by usage only.
4. **`MA+0x249ba[cand]` eligibility flag writer**: read at 6 sites in the writer, never
   traced to its writer. Next: disp-0x249ba store scan over the full image.
5. **Cross validator `FUN_142123290`** (~0x100 B, .impdata): only its const map was
   extracted (90°/60/400); the full gate sequence was not read. Next: dec.sh it.
6. **`FUN_140a4c9a0(·,−4.0)` / `(·,−8.0)` / `(·,−2.4)`**: recurring veto idiom in run
   scorers; internals not decoded.
7. **Playing-style consumers**: no style-bit test in any of the ~15 decoded bodies;
   styles may enter only through the `ai_selector` data files or the team-instruction
   bytes. Next: xref `[player+0x48]` shr-tests per team-plans §6 item 8.
8. **Shot sub-kinds 2–5 / wide evaluator `0x142120e60` / aim `0x14211f090` / lane
   `0x14211faf0`**: untouched (outside this ticket's scope; still open in carrier-ai).
9. **GKCoaching `0x142133950`, OnePattern `0x142133ed0`, RespondRequest
   `0x14212b230`, long-validity `0x1421240b0`, clear-angle `0x142108630`**: untouched,
   still open in carrier-ai §9.
10. **Live validation of everything above**: needs OPEN_PLAY; none done this session.


## 9. Orchestrator verification (2026-10-13, PESLive decompile + disk disassembly; live watcher running)

**§1's identification is wrong; offside detection (duels-fouls §5.3) is still OPEN.**

- **`FUN_14045e030` is not a flag write (OBS decompile).** It moves one entry of a per-team 11-slot list
  (`*(u64*)(param_1 + (team·11 + slot)·8)`) from slot `param_3` to slot `param_4`, shifting the others, then
  rebuilds a reverse rank table: bytes `param_1+0xb2..0xc7` are reset to 0x0a and `rank[id] = slot` for the
  first `count[t]` entries (`count` bytes at `param_1+0xb0/+0xb1`). Nothing near `+0x253c4` is written.
  INF(high): this is the per-team line/marking order (cf. duels-fouls' "line-sort writer `FUN_14044e230`").
- **`FUN_140a8ff10` is not a beyond-the-line test (OBS decompile).** It computes `d = p − q`, rotates it by
  `−angle` (`FUN_1404bcad0`), and returns `−a < d.x < a && −c < d.z < b`: point inside an oriented box.
- **The call at 0x14045f47d (OBS disk disassembly):** A = candidate `obj[edi]+0x55c`, B = `obj[esi]+0x55c`;
  `bearing1 = FUN_140439e50(A → AI_A+0x1414)`, `bearing2 = FUN_140439e50(B → AI_B+0x1414)` (each player's heading to
  his AI target point); `bearing2` wrapped into [0, 360) with 360.0 @0x14259858c; `Q = FUN_140a8f7c0(B, bearing2,
  xmm7)` (B moved along his heading); then `FUN_140a8ff10(A, Q, bearing2, a = xmm7, b = 50.0 @0x14259be34, c = 0)`:
  **A lies in a corridor of half-width xmm7, 0..50 m ahead of Q along B's heading.**
- **The "8.0 m Δx tolerance" does not exist (OBS).** 0x14045f45b loads **50.0** (the box depth), not 8.0.
  0x14045f486–0x14045f4dd: `Δ = bearing2 − bearing1`; if `|Δ| > 180.0` (@0x142598580) then `Δ −= ±360.0`
  (`mulss xmm0, xmm8`, xmm8 = 360.0); then **`|Δ| < 65.0°`** (@0x14259be48, `comiss; ja 0x14045f519`) → the
  re-order path. So §1.4's "level tolerance 8.0 m before a 65 m² test" is a heading-difference test of 65°.
- **Reading (INF high):** `FUN_14045e3db` re-sorts the line/marking order when one player overtakes another
  along a similar heading. The 2.0/5.0/17.0/0.8/1.5 gates of §1.6 belong to this re-ordering, not to offside.
- Killed hypotheses: "FUN_14045e3db is the offside flag writer"; "FUN_140a8ff10 = beyond-the-line predicate";
  "8.0 m Δx level tolerance"; "the 65.0 compare is in m²".
- Live: a read-only watcher (`<scratch>/OffsideLive/watch_offside.py`) logs every live offside call's MI fields
  (duels-fouls §5.1) and all 22 positions from a ring buffer at the flag frame. Results appended below when in.


> **Correction (OffBallRE, `pes-re-off-ball.md` §0):** §3's run "start gate" is the **stop** test and the "≈11-frame cooldown" is the minimum run time; `0x14061e810` is GoalGet's stop test, `0x14061a640` CenteringGet's, "Score" is the shared base class; every "rng" in §3 is the constant fps = 54.
