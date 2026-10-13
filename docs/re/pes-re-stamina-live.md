## 9. Pass 8 (2026-10-13, later) — live sampling in open play: record family FOUND, series not captured

Scope: whole-heap diffing at 1 s cadence in a live match (pid <pid>, open play, 77th
minute per the OBS frame), then structural anchoring of every candidate record family, plus a
static pass on the per-player getter chain. Read-only throughout: `/proc/<pid>/mem` preads
only, no input, no write, no ptrace, no stop. No player/team names — slots by index only.

### 9.0 Method that worked

- `sudo` + `os.pread` on `/proc/<pid>/mem` (no `dd`): **1870 MB/s** for sequential 4 MiB reads,
  1.2 s for a 2082 MB sweep of all 81 flat-address writable regions ≥ 1 MiB, 5.6 s for the full
  3138 MB including the Linux/GPU side. Three sweeps a minute is comfortable.
- Snapshot to `/dev/shm` (12 GB free), diff 4-byte words with `struct.unpack`, classify each
  changed dword as a float delta. `/tmp` was never used for snapshots.
- The decisive move was to stop hunting unnamed floats and instead **walk the static getter
  chain** (§9.2) to get the record base formula, then diff only that family.

### 9.1 Live evidence — the record family and what each one is

Measured OBS at 17:18–17:22 while the match was in open play.

| family | base / stride | found by | what it is |
|---|---|---|---|
| **player position/track array** | `*(ctx+0x40) + 0x88 + n*0x28`, n<0xFA | `FUN_140a6cf70` (§9.2) | **the live per-player record array** — 40-byte records, `FUN_140a57450` takes `rec[n]-rec[n-1]` as the per-tick displacement |
| per-player AI handle array | `ctx + 0x1928 + n*0x28`, n<0x1A | `FUN_140a48910` | per-player object pointer; this object is the `param_1` of `FUN_140802b60`/`FUN_1407f7370` (fields `+0x640`, `+0x67b`, `+0x67c`, `+0x676`, `+0x688`, `+0x68c`, `+0x690`) |
| 22 `0x14259aee0` entries | `mc+0x1a50 + i*0x28` | vtable scan of the heaps | class `0x14259ae40`, **0x2f8 bytes**, quaternion/euler blend data at `+0x150..+0x2f8` in 0x2c-stride 12-byte vec3 pairs; **animation blend buffer, changes in every offset — not stamina** |
| rosters | `*(ctx+0x1978)` / `*(ctx+0x19a0)` | `FUN_140a489d0` | 0x128-byte **metadata** blocks (ASCII names, kit ids). Live check: the pointer flickers non-zero 34650/1683194 samples (2%) over 3 s of polling; block bytes are static apart from an ASCII colour byte at `+0x54`. **Not live sim state** — the §6 recipe reads metadata, not state |
| ball | `BallInfo+0x4E0` via vtable `0x14259afb0` | prior passes | class live only in play; 32 hits at 17:18, 1 hit after the match ended |

The `**player position/track array**` is the family the acceptance test needs: 0x28 stride,
≥250 slots, and the displacement is read by `FUN_140a57450` which is exactly the function
`FUN_1407f7370` uses to compute its speed term.

Float-diff statistics for context (OBS, 1 s apart, whole heap): 3.34 M changed dwords total,
16 433 rising (>0.5/s) and 46 328 falling, 1.45 M movers inside the 0..1 interval. The largest
concentrations are graphics/animation regions (`0x177d0000`, `0x2c800000`, `0x23000000`,
`0x35800000`), so **an unnamed float diff cannot isolate stamina** — it must be pinned to the
record family. This is why the getter chain was the right move.

### 9.2 Static getter chain (all OBS, GhidraMCP + the on-disk image, base 0x140000000)

```
*(u64*)0x1436F3DC8                     = match context (ctx)      [live-verified]
*(ctx + 0x40)                          = player-list object
FUN_140a57710(param_1)                 = param_1 + 0x2944          (ball's offset getter)
FUN_140a6cf70(param_1, n)              = param_1 + (n*5 + 0x11)*8  if n < 0xfa
                                         else param_1 + 0x2770
                                         => record base = *(ctx+0x40) + 0x88, stride 0x28
FUN_140a57450(param_1, out, n)         if n == 0:  (ball) (p+0x2948/0x294c/0x2950) / 54.0
                                         else:       rec[n] - rec[n-1]   (player displacement)
FUN_140a489d0(ctx, t)                  t<2: *(ctx + (t+0xA3)*0x28) ; else *(ctx+0x1978)
FUN_140a48910(ctx, i)                  i<0x1a: *(ctx + (i+0xA5)*0x28) ; else *(ctx+0x19c8)
```

So **two** independent 0x28-stride arrays hangs off the context: the 26-slot handle array at
`ctx+0x1928` (objects) and the ≥250-slot record array at `*(ctx+0x40)+0x88` (positions).
`FUN_140a6cf70`'s `n < 0xfa` bound (250) means the record array is much larger than 22 — it
holds every tracked entity, not just players; the 22 players are the first slots.

### 9.3 The speed expression, with its constants

From `FUN_1407f7370` (the band fn) and `FUN_1407fba90`, both OBS:

```c
f = FUN_141e532d0();                       // = 54.0      (DAT_14351ae50, the tick rate)
k = (f * DAT_14259a764 * DAT_14259a764)    //   54 * 60 * 60
      / DAT_14256ae34;                     //     / 1000   => k = 194.4
dx = rec[n].x - rec[n-1].x;  dz = rec[n].z - rec[n-1].z;
term = k * sqrt(dx*dx + dz*dz);            // the "speed" the band test uses
```

and for the ball: `FUN_140a57450(*, out, 0)` divides `*(p+0x2948/0x294c/0x2950)` by `54.0`,
i.e. the ball's raw fields are **×54 fixed-point** (cm per tick, since 194.4× and 1000÷
compose to cm/s). INF (med-high): the player record's displacements are also per-tick
centimetres, so a drain term of the form `Δstamina = -k2 * term * DAT_14351ad20` is the shape
to fit — the `k2` is what still has to be read out of a live series.

Constants resolved OBS from the image: `DAT_14259a764 = 60.0`, `DAT_14256ae34 = 1000.0`,
`DAT_14351ae50 = 54.0`, `DAT_14351ad20 = 0.01851851866 = 1/54`,
`DAT_14259d624 = 1e-6` (fixed-point scale).

### 9.4 The band function is still geometry, not stamina — now with proof

Re-decompiled `FUN_1407f7370` in full this pass and re-read all 7 `param_2+0x20` uses and all
12 `param_1+0x690` uses:

- `param_2+0x20` and `param_2+0x10` are handed to `FUN_14043a020`, which is a **360° angle
  wrap** (`if v<0: v+=360; if v>360: v-=360;`). Every compare in the function is against an
  angle or a distance: `135.0`, `180.0` wrap bound, `360.0` modulus, `80.0` (m), `120.0`,
  `100.0`, `30.0`, `12.5`, `0.32606`.
- `FUN_1407f9b40(param_1)` — reads `*(u16*)(*(param_2+0xab8)+0x18)` and divides a count by a
  count — is a **team/player slot ratio**, not a stamina ratio; the `> 0.8` compare
  (`DAT_142572348`) gates "is this player on the ball's team", nothing more.
- The writes it performs are `*(u16*)(param_1+0x558) = 0` then a loop filling a
  **100-entry 8-byte array at `param_1+0x238`** (`*(u32*)(+0x238+idx*8) = ...`,
  `*(float*)(+0x23c+idx*8) = ...`, `+0x558 += 1`, `qsort` via `FUN_141e53540`) with a
  `LAB_142132000` comparator — a per-tick candidate list, sorted. Then `FUN_140802b60` picks
  from it.
- `DAT_14259be0c = 15.0`, `DAT_14259be1c = 20.0`, `DAT_14259be60 = 135.0`,
  `DAT_14259222c = 5.0`, `DAT_14259bdec = 6.0`, `DAT_14259bdfc = 11.0`,
  `DAT_1425ee908 = 13.2` — the 20/15/10/5 "tier" numbers exist as **angle/slot-id compares**,
  confirming §8.10–8.11: the band function is action **eligibility**, and no stamina
  accumulator is read or written by it. INF (high).

### 9.5 `FUN_1416231b0` (the 0x2c0 per-player state object) — one live data field found

OBS decompile; the ctor copies three floats from the player object into it:

```
+0x220 / +0x224 / +0x22c  <-  *(param_4 + 0x50) / +0x54 / +0x58
+0x230                     <- *(param_4 + 0x60)
+0x23c block               <- FUN_141637160, three ×1e6 fixed-point timers ÷60
```
and reads `*(param_4 + 0x40)`, `+0x48`, `+0xf0`, `+0x114`. So the per-player object's
`+0x50/+0x54/+0x58` triple is the per-player scalar state worth the live series — it is the
only per-player float triple that is **copied out** into the per-tick update object.

### 9.6 A reliable OPEN_PLAY signal

OBS, measured across the whole session:

- `*(u64*)0x1436F3DC8` (match context): **0 out of play**, non-zero in play. This is the best
  single gate, but it is not safe to sample once — it flickers only for handles, and it goes to
  0 during goal replays and between plays, so a sampler must tolerate gaps rather than exit.
- `DAT_14351ad20` (**1/54**) is **NOT a gate**: it still read 1/54 at 17:26, four minutes
  after the match context had gone to 0. Same for `DAT_14351ae50 = 54.0`.
- The strongest *combined* gate: `*(u64*)0x1436F3DC8 != 0` **AND** the ball-class vtable
  `0x14259afb0` findable in the flat regions (32 hits at 17:18 in play, 1 hit at 17:27 out).
- A count-style signal that is cheap and unambiguous: the number of
  `0x14259aee0`-vtable entries reachable from `mc+0x1a50` (22 while playing, all freed after).

### 9.7 Where it stopped, exactly

At **17:2x** the match left open play (the owner skipped / the half ended — not observed
directly, only its effect). Measured immediately after:

- `*(u64*)0x1436F3DC8 == 0` continuously for 240 s of 5 Hz polling (0 hits).
- Ball-class vtable `0x14259afb0`: 1 hit (was 32); its `+0x64` position reads 0.0.
- `mc+0x1a50`'s 22 object pointers now read freed/garbage; the old ctx at `0x2e76a9c0` has been
  recycled (its vtable qword now `0x1c3baad0`, a different object).
- `DAT_14351ad20` still 1/54 — the tick global outlives the match (§9.6).

Consequence: **the live per-slot series was not captured.** What is delivered instead is the
record-family formula (§9.1/§9.2, static and verified against the live spell), the speed
expression and its constants (§9.3), the three candidate per-player scalar offsets
(`player+0x50/+0x54/+0x58`, §9.5), and the OPEN_PLAY gate (§9.6). The remaining work is one
20 Hz capture of `*(ctx+0x40)+0x88+slot*0x28` and `player+0x50/+0x54/+0x58` for 22 slots
across ~10 minutes, which needs a match in open play again.

### 9.8 Reproducibility

Scratch (session-local, nothing committed): `~/ghidra-projects/scratch/StaminaLive2/` —
`snap.py`/`snap2.py` (sweep + `/dev/shm` snapshot), `fdiff.py`/`diff2.py`/`flt.py`
(4-byte-word diff and float classification), `fam*.py` (stride analysis),
`OPENPLAY.md` (this pass's raw notes plus the full resolved-constant table), and the
`chg*.json`/`rise*.json`/`unit*.json` intermediate lists. No PES-derived bytes, name lists or
number tables were produced or committed.
