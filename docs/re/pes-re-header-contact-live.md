# PES 2021 live sweep: header band, kick-request gate, contact levels, b10 stickiness

Agent: HeaderContactLiveRE. RE only. Scratch: the HeaderContactLiveRE scratch dir.
Tags: **OBS** = read live via read-only pread, or read in raw instructions and
re-checked at the named VA; **INF(conf)** = inferred. VAs are PES2021.exe,
base `0x140000000`. No player or team names below (P0..P21 are side-agnostic ids).

Ticket items:
1. Header band 9 live (`docs/re/pes-re-header.md`): headers vs `ap+0x2754` vs ball height.
2. AI-side writer of the kick request struct + which request byte gates the header
   block at `0x14082b3e0` (that report's §7 next step).
3. Live check of the contact level formula (`docs/re/pes-re-contact.md` open list).
4. `ap+0xb10` stickiness (`docs/re/pes-re-contact-2.md`).

## 0. Session

- OBS: match was live for most of the sweep (`G=0x2d4d9570`,
  MA `0x7ffffd450018` via TacticsRE's `find_ma7.py`, `MI=0x1cc04608`, ball
  addr `0x1a99f2e4`, `STATE=OPEN_PLAY` mid-sweep). `MI+0x116c` cycled
  1→3→1→2 over an 80 s watch (frames 31623→34161): 0–14 s in 1, 14–16 s in 3,
  16–45 s in 1, then 2. So state 1 is in-play; 2/3 are stoppages/replays.
- OBS (sampler lesson): `G+(i+0xbf)*0x28` and `G+(i+0xa5)*0x28` are null
  outside the tick (same as MI). Three sampler runs returned polls=0: the
  first two resolved players once at start (menu phase); the state-gated run
  coincided with a long out-of-play stretch (`MATCH_FROZEN`, then both MI
  null for minutes — likely half-time/extended stoppage). Fix used: lazy
  per-tick resolve + `MI+0x116c==1` gate (the scratch dir `sample.py`).
- OBS: MA ball (`MA+0x2482c`) reads stale/frozen outside state 1 (fixed bytes
  while state=3); only state-1 samples are usable for item 1.
- OBS (final window): third match produced ~5% state-1 duty cycle (13/250
  polls over 50 s; one 110 s watch saw a single 12-poll state-1 burst) and
  `STATE=MENU` at close. Four sampler runs total (unified 20 Hz + trigger
  mode, the scratch dir `sample.py`): 0 state-1 rows captured. The window was
  NOT usable for live items 1, 3, 4 — all stay open with the ready sampler
  named below. No new long sampler started per orchestrator instruction.

## 1. Header band 9 live — OPEN (no usable window; ready sampler named)

No state-1 kick samples captured in any of the four runs (0 state-1 rows).
Ready sampler: the scratch dir `sample.py` (trigger mode: 10 Hz MI polls,
54 Hz bursts inside state-1 windows) logs every kick entry / band change with
`ap+0x2754`, `ap+0x3b18`, MA ball Y, clip id, AI action and abilities.

## 2. Kick-request writer + the `0x14082b3e0` gate — static CLOSED, live OPEN

### 2.1 The AI-side writer (OBS, decompile `out-req833cd0.c`)

- OBS: the request struct is built by **`FUN_140833cd0`** (builder decompile:
  the scratch dir `out-req833cd0.c`).
  Called from Kick slot 10 `FUN_1407253d0` (`0x140725e72` first pass,
  `0x140725f57` retry), i.e. Kick vtable `0x1425e2380` slot +0x50.
  Slot 10's params: `(param_1=cmd, param_2=ap, param_3=AI request)`. The AI
  request arrives from the vtable dispatch — its writer is the anime command
  fill (open, next: filter the 30 `call [reg+0x50]` sites at `0x1406c5c41…`
- OBS: `FUN_140833cd0(req, ap, ctx, ...)` copies the AI request header
  (`*req = *ctx`, kind via `FUN_1407204e0`) and derives the gate flags itself —
  so the "AI-side writer" of `+0x5b` is this builder, one hop below the AI.
  - `req+0x5b = FUN_1406ec200(ctx, 4)` — reachability at slot 4 (see §2.2).
  - `req+0x5d`: `0`, then `= 1` via stamina/RNG path or when anime action is
    STAGGER (`0x10`), copying `param_8` into `req+0x18`.
  - `req+0x56 = 1` when the target-height term `< 90.0` (`0x14259be60`);
- OBS: the selector copies these through (`FUN_140821170` field copy), so the
  `+0x5b` read at `0x14082b3e0` is the builder's `FUN_1406ec200(ctx,4)` answer.
- OBS: `req+0x5c` is zeroed by the request initializer `FUN_140821050`
  (decompile `out-reqinit.c`: `param_1[0x17]` word-pair covers `+0x5c`) and
  **never written by `FUN_140833cd0`** (no `+0x5c` store in `out-req833cd0.c`).
  So the `0x140829ec1` early-out is dead on this path: the header ladder
  always runs.

### 2.2 What `+0x5b` means (OBS, raw `0x1406ec200`)

```
FUN_1406ec200(ctx, slot):
  FUN_141eb51a0(ctx, slot, &bonePos, 10)   # world pos of bone `slot` (+0x20/0x24 out)
  thresh = *(float*)(table + slot*4) * ap+0x3b18   # FUN_140a6d400 (raw 0x140a6d409)
  return bonePos_component >= thresh ? 1 : 0       # cmovae (raw 0x1406ec24e)
```
- OBS: reads a bone world position (`FUN_141eb51a0` — the same helper the
  contact report's §8.5 names for the displacement check) and compares against
  a physique-scaled per-slot threshold. INF(hi): `+0x5b` = "the body part for
  this kick slot can reach the ball cleanly" — a reachability flag, not an
- OBS (§2.1): `req+0x5c` is zeroed by the initializer and never written by
  the builder — the early-out is dead on this path (closed, supersedes "open").

### 2.3 The `0x14082b3e0` gate, verbatim (OBS, live-image disassembly)

```
0x14082b3e0  cmp byte ptr [rdi + 0x5b], 0   # request reachability flag
0x14082b3e4  je  0x14082b3f7                 # skip header-only block
0x14082b3e6  cmp byte ptr [rsp + 0x6c], 0   # "offered clip is a header kind"
0x14082b3eb  je  0x14082b3f7                 # (set 0x14082a948 from FUN_1407245d0/
0x14082b3ed  cmp r13d, 9                     #  1407247d0/14071e520==6)
0x14082b3f1  je  0x14082c209                 # band 9 → header-only accept path
```

- OBS: the third conjunct is `band == 9` (r13d = `FUN_140838ca0` return).
  The `je 0x14082c209` target skips the `FUN_140821e00` accept/reject gate and
  the `FUN_14071e6f0` clip lookup, joining the commit path directly — a
  fast-accept for *reachable band-9 balls when the offered clip is already a
  header kind*.
- INF(hi): no AI "header" decision anywhere on this path — the gate is
  (reachability) AND (clip-table says header) AND (ball in the jump band).
  This confirms EV-HDR-0010 at the source: the header is forced by geometry +
  clip table, gated by a body-reach flag.
- OPEN (live): needs `req+0x5b` vs band-9 headers — expect `+0x5b==1` on every
  accepted band-9 header. Needs a perf write-watchpoint on a live request
  struct or state-1 sampling; not attempted (match ended).

### 2.4 Kind predicates on the neighbouring branches (OBS, raw)

- `FUN_140724590`: kinds 10–13 true (header family subset).
- `FUN_140726610`: kinds 14–16 true.
- `FUN_1407240f0`: table-driven over kind−0x1d (large family).
- `FUN_1407241a0`: kinds {5,6,9,0x48} true.
- The `0x14082a325` branch (`req+0x5b` vs loop counter) with kind∉{10–13,14–16}
  jumps to the reject tail `0x14082c4a3` — consistent with 2.3.

## 3. Contact level formula live — OPEN (match ended before capture)

No state-1 STAGGER/FALL entries were captured (same stoppage windows as §1).
Next step: same `sample.py` run logs contact entries with the contact record,
tracked parts (`ap+0x508`), abilities and MA ball; the displacement axis
still needs `FUN_141eb51a0` bone world outputs per the contact report's §8.5
(this sweep confirms that helper's address and its use in `FUN_1406ec200`).

## 4. `ap+0xb10` stickiness — OPEN (match ended before census)

No nonzero `b10` was ever sampled (all state-1 polls were empty). Static
result (contact-2 §1.3) predicts: once 2, stays 2 for the AnimePlayer's
lifetime. Next step: same `sample.py` run includes a per-minute `b10` census
plus transition logging.

## Evidence rows

| ID | Claim | Confidence | Evidence |
|---|---|---|---|
| EV-HCLIVE-0001 | kick request built by `FUN_140833cd0`, called from Kick slot 10 at `0x140725e72`/`0x140725f57` | Confirmed (static) | `ks.c:208` call; decompile `out-req833cd0.c` header/signature |
| EV-HCLIVE-0002 | `req+0x5b = FUN_1406ec200(ctx,4)`; `+0x5d`/`+0x56`/`+0x57`/`+0x5a` writers as in §2.1 | Confirmed (static) | `out-req833cd0.c:899-906`, `:705-767`, `:893-895` |
| EV-HCLIVE-0003 | `FUN_1406ec200` = bone-world-pos (`FUN_141eb51a0`) vs `scale × per-slot threshold` (`FUN_140a6d400`), booleanised by `cmovae` | Confirmed (static) | raw `0x1406ec200-0x1406ec256`, `0x140a6d400-0x140a6d411` |
| EV-HCLIVE-0004 | `0x14082b3e0` gate = `req+0x5b` AND header-clip-offered AND band==9 → fast-accept at `0x14082c209` | Confirmed (static) | live-image disassembly §2.3 |
| EV-HCLIVE-0005 | kind predicates: 10–13 / 14–16 / table(kind−0x1d) / {5,6,9,0x48} | Confirmed (static) | raw `0x140724590`, `0x140726610`, `0x1407240f0`, `0x1407241a0` |
| EV-HCLIVE-0006 | `MI+0x116c` 1=in-play, 2/3=stoppage; player slots + MA ball only valid in state 1 | OBS (live) | 80 s state watch frames 31623→34161; null-slot polls + frozen MA bytes |

## 5. Cross-check vs the dt18 Constants Deep Dive (user-supplied, 2026-10-14)

- OBS (no conflict): the wiki's examined `constant_player.bin` list
  (ballplayerShoot/throughpass/playStyle/motivation/trap/ballplayer/shoot/
  ballplayerGk) does not include `contact.o` or `matchup.o` — our `contact.o`
  stock-vs-mod diff (contact-2 §2: only `+0x04` 56→88, `+0x30` 24→42,
  `+0x34` 16→36) fills a gap the wiki never covers. No contradiction.
- OBS (consistent): wiki `ballplayerShoot.o` empty in PES21 matches EV-HDR-0008
  (no header/shot-decision params in the data files; decisions live in code).
- INF(hi) (distinct systems, not a contradiction): wiki `pairAnime.o`
  (duel timing, stamina.effect 0.5, first-contact victory off, PES15 test mode)
  governs jostle/duel timing; our `contact.o` governs knock-down levels with
  no stamina input observed in `FUN_140842d20` or the nine vetoes. Both can hold.
- OPEN: wiki `motivation.o` ±5% stat modifiers vs our raw-byte stat reads
  (`FUN_1406e6660` = byte `ab+k`, EV-CONTACT-0014). If motivation scales stats,
  it applies downstream of the byte read — next step: live-compare `ab` bytes
  against the values `FUN_1406e6660` returns during positive/negative emotion.
- Scope note: wiki style bonuses (Box-to-Box/Dummy Runner/`_good`), `trap.o`
  reaction/touch tables, `shoot.o` 6-level gauge, GK/emotion/prediction-frame
  sections touch none of this ticket's four items; no changes adopted from them.
