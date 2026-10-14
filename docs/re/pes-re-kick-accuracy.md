# PES 2021 kick execution error — evidence report (KickAccuracyRE)

Tags: **OBS** = raw bytes at the stated VA (exe base 0x140000000) or live read; **INF** = inference with confidence.
No player or team names. Scratch: the KickAccuracyRE scratch dir. Live process read-only (`/proc/<pid>/mem`, nothing written, no input sent).
Read first: `docs/re/pes-re-kick-launch.md`, `docs/re/pes-re-contact.md` §3, `docs/re/pes-re-rng-slots.md`, `docs/re/pes-re-rng.md`, `docs/re/pes-re-stamina-drain.md` §4, `docs/re/pes-re-matchlogic.md` §1–3, `docs/re/pes-re-carrier-ai.md` §6.

## 0. Headline

1. **OBS — Two error paths, one RNG.** The predictor `FUN_1407324b0` (2 callers: `0x140731ffb`, `0x140732316`) and the physics-side writer `FUN_1408398d5` (inside `FUN_1408397a0`, called from `Kick` +0x40 override `0x140717c50` at `0x140717dfe`, then `FUN_14083c590` at `0x140717e10`) both draw only **slot 2 of the per-player `math::Random` at `ap+0x32d0`** via `Range`/`Normal`/success-roll helpers. No match-RNG slot enters any kick-error draw.
2. **OBS — Per-kick-type stat selection lives in `FUN_14071d770`** (2 callers: `0x14071cf36`, `0x14072344c`): SHOOT→`0x1a` Finishing, short-pass family→`0x1b` Low Pass, lofted→`0x1c` Lofted Pass, header kinds→`0x1d` Heading (immediates `0x1b`@`0x14071d8de`, `0x1c`@`0x14071d8e5`, `0x1d`@`0x14071dc1e`), each read via `FUN_1406e6660(ap,idx)` = `block+idx` byte. Corroborates `pes-re-contact.md` §3.3.
3. **OBS — The error magnitude is logistic in the stat + one-sided exponential noise.** `errScale = 1/(1+exp(5.5·clamp01((S−40)/59)−2.5))` (VAs in kick-launch §5.1, re-confirmed here); noise = `Normal` quantile table `0x1426548c0`, sample `2·ln(u/512)+0.2`-family, sign from separate threshold rolls. Reference values: S=40→0.924, 60→0.654, 70→0.426, 80→0.226, 90→0.103, 99→0.047 (recomputed in scratch `kickacc.py`).
4. **OBS — Weak foot is a flat −5.0 on the stat** before the logistic, gated by `footSign·dirY>0` (`0x140732631` = 5.0@`0x14259222c`). **INF(med)** this is the weak-foot penalty; the Weak Foot Accuracy byte itself (`block+0x36`) was not traced into this path (open §7.2).
5. **OBS — Fatigue enters only through the live-stat pipeline (A5), not the launch.** A5 = `0.8+0.2·clamp((pct−10)/60)` on ids `{0x17,0x18,0x1a,0x1b,0x1c,0x28,0x29,0x2a,0x2b,0x2c}` (stamina-drain §4); breath penalty `p` on `{0x1a,0x1b,0x1c,0x2a}`. Note `0x1d` Heading is in neither set — headers do not fatigue by this path (OBS id lists).
6. **INF(med) — Playing styles do not enter the error.** No style-bit test in any decoded launch body (consistent with attack-decisions §9). Skills enter as +5 quality bonuses and gate rolls (§3).

## 1. Helpers (OBS raw disassembly)

| helper | VA | body | semantics |
|---|---|---|---|
| stat getter `FUN_1406e6660(ap,idx)` | `0x1406e6660` | `movsx ebx,[rcx+0x2458]`; `+0xab8` vtable call; `0x140a489f0(ebx)`; `0x14124dad0`; `movzx eax,[rdi+rax]` | returns `block+idx` ability byte |
| skill test `FUN_1406ead40(ap,bit)` | `0x1406ead40` | same prologue; `shr rax,cl; and al,1`; early `0` if teamIdx>`0x15` | engine skill-bit test, block+0x40 u64 (carrier-ai §6.1 bit order) |
| rng lea `FUN_1406e6be0(ap)` | `0x1406e6be0` | `lea rax,[rcx+0x32d0]; ret` | returns per-player RNG pointer |
| per-player pick `FUN_1406e6bf0(ap,m)` | `0x1406e6bf0` | `rcx+=0x32d0; edx=2; Range; … idiv` | `Range(slot2) % m` |
| success roll `FUN_1406ebf30(ap,thr)` | `0x1406ebf30` | `rcx+=0x32d0; edx=2; Range; …(ecx-eax); cmp ecx,ebx; setl` | `Range(slot2)→0..99 < thr` |
| `Normal(rng,amp,off)` | `0x140a92590` | two slot-2 LCG steps (`0x140a9261d`/`0x140a92646`, mult `0x2d22b2e3`, const `0x4513`, `btr ..,0x1f`); `u/1000`, `×512`, table `0x1426548c0`, lerp, `×2^-12 ×−2`, `sqrtf`, rejection; tail `cvttss2si; cmp 0x63; jbe ret` else retry ≤2 then restart draws | returns int `eax` 0..99 (callers `movd+cvtdq2ps` it); quantile table base OBS (`lea rsi,[rip+…]` → `0x1426548c0`), scale `0.000244140625`@`0x1426550dc`, `−2.0`@`0x14259a768`, `512.0`@`0x1426550e8`, `1000.0`@`0x14256ae34`, `360.0`@`0x14259858c`, `50.0`@`0x14259be34`, `0.5`@`0x142565600`, `90.0`@`0x14259be5c` |

## 2. Attribute index per kick type (OBS immediates in `FUN_14071d770`)

- Kick-kind resolve: `ap+0x2752` i16; `-1` = foot path. Vtable+0x28 action id maps `0x33→edi 0x2a`, `0x3f→0x1c`, `0x41→edi`, else action id; indexes 16 B kick-kind table `0x14344e380` (150 entries; byte+`0xc`∈{0,1} marks 28 header kinds 1–27,149 — re-verified by dump, matches contact §3.2).
- Foot path stat select (`r15d>1` branch): jump table on `(foot−1)` + action compares `0xa`(SHOOT)/`9`/`8` choose `edx` = `0x1a` (`lea edx,[rax+0x10]` after `cmp 0xa`), `0x1b`, `0x1c`. **OBS** the three immediates; **INF(high)** the SHOOT→0x1a/short→0x1b/lofted→0x1c mapping (branch→action direction not fully traced, but agrees with contact §3.3).
- Header path (`r15d≤1` branch, `0x14071dc1e`): `edx=0x1d` Heading; `+5.0` (`0x14259222c`@`0x14071dc8b`) iff header-SHOOT flag and skill bit 6 (`edx=6`@`0x14071dc76`) set. **OBS**.
- Quality curve: `q(S)` per contact §3.3 (`S<60 ? max(0,0.1·(S−40)/20) : S<95 ? 0.1+0.85·(S−60)/35 : 0.95+0.0125·(S−95)`); constants at this site re-resolved: `60.0`@`0x14259a764`, `360.0`, `120.0`@`0x14256ae30`, `−60.0`@`0x1425c48fc`, `8.0`@`0x14259bdf4`, `0.15`@`0x1425c48a0`, `2.0`@`0x142565604`, `0.3`@`0x142590d98`, `12.0`@`0x14259be04`, `22.0`@`0x1425c48c0`, `±3.0`. **OBS** constants; formula **OBS** per contact (not re-derived here).
- Skill +5 bonuses (each `addss xmm6,[0x14259222c]`, OBS): skill indices read `0xe,7,8,0x1f,0x27,0x28,0xa` with per-bonus condition bytes (`[rsp+0xe0]==0x13`, `[rsp+0xf0]==0xc`, action-`0xa` short-pass subkind, `r15d==4/8`, foot checks). Engine-bit meanings (carrier-ai §6.1): 6 HEADER, 7 LONG_RANGE_DRIVE, 8 KNUCKLE_SHOT, 10 HEEL_TRICK, 14 PINPOINT_CROSS, 31 CONTROL_LOOP_SHOT, 39 MIDDLE_SHOT, 40 THROUGH_PASS. **OBS** indices+`+5.0`; **INF(med)** situation labels.
- Tail reads `edx=0x34` (foot/compare byte) and `edx=0x26` with `12.0/22.0` vs `3.0/−3.0` clamp — **OBS** bytes; purpose open (§7.4).
- Stat-index map used: `0x1a` Finishing, `0x1b` Low Pass, `0x1c` Lofted Pass, `0x1d` Heading, `0x2a` Kicking Power (matchlogic §1.3 INF-high + contact OBS). Curl `0x21` and GK Clear `0x28`-family not seen as quality indices in this function (open §7.3).

## 3. Predictor launch `FUN_1407324b0` (OBS; constants re-resolved this session)

- Foot sign: `byte[rbx+0x34]`; `footSign = (v−0.5<0)?+1:−1` (`0.5`@`0x142565600`, `1.0`@`0x14256ae1c`, `−1.0`@`0x142588218`, sign-flip `−0.0`@`0x1425656d0`); stat `edx=0x20`; `base = S − (5.0 if footSign·dirY>0 else 0)`.
- Ball-height tiers on `h=f32[player+0x2bcc]`: thresholds `0.9`@`0x14258c260`, `0.4`@`0x14259b89c`, `−0.1`@`0x14259d648`, `−0.9`@`0x1425cb314`, `0.1`@`0x142588200`; A/B per tier `18.0/10.0/−15.0/−5.0/0.02`, B `1.3/1.2/−5.0(shared VA)/1.15` (resolved: `0x14259bdd8`=1.3, `0x14259d5d4`=1.2, `0x1425e5728`=1.15), adds `5.0/−2.0/3.0/−4.0`(`0x1425c7678`).
- Logistic: `40.0`@`0x14259be2c`, `99.0`@`0x1425c48cc`, `−59.0`@`0x1425cd974`, `5.5`@`0x1425c8378`, `2.5`@`0x14259bde4`, `expf`, `errScale=1/(1+exp)`.
- Width terms: `0.2`@`0x142598578` offset, `4.2`@`0x1425e61a0` divisor, gates `0.65`@`0x14259d5d0`/`0.35`@`0x14259d5c8`, `0.25`@`0x142592228`, spin picks `0.7`@`0x14257a194`/`0.8`@`0x142572348`, `0.01`@`0x14256ae18`, thresholds `70.0`@`0x14259be4c`/`40.0`, spin-scale `2.5`@`0x14259bde4`, clamps `1.5`@`0x14259bddc`, `2.0`@`0x142565604`/`4.0`@`0x1425656c0`, `0.1`@`0x142588200`, `−1.0`, `0.5`.
- Draws per call: `Range(2)`@`0x140732798`, `Range(2)`@`0x1407328a0`, `Range(100)`@`0x1407328c5`, `Normal`@`0x1407328e4`, `Range(2)`@`0x140732960`, `Normal`@`0x140732983`, `Range(100)`×2 (`0x1407329d0`,`0x1407329f6`), `Normal`@`0x140732a2d` — i.e. **3 Normal + 6 Range, all slot 2 per-player**. **OBS**.
- Spin tilt: ANIME-id−0x20 byte table `0x140732d20` + dword table `0x140732d10` (kick-launch §4, not re-dumped); tilt consts `2.5`@`0x14259bde4`/`3.5`@`0x14259bde8`/`0.04`@`0x1425a9334`/`0.15`@`0x1425c48a0`/`0.025`@`0x1425cb284`/`0.3`@`0x142590d98`/`4.0`@`0x1425656c0`/`4.5`@`0x1425a1ff4`/`0.02`@`0x1425c4894`.
- Rotation/normalise: `sqrtf`, wrap `±360.0`, `0x140a8fb00`/`0x140470d60`/`0x140a92c80`, `0x14046f230`; writes unit dir × speed back. **OBS**.

## 4. Physics scatter `FUN_1408398d5` (OBS; the actual execution error)

- **6 `Normal` draws**: `0x140839b51`, `0x140839bd5`, `0x14083a280`, `0x14083a550`, `0x14083a5f2`, `0x14083a7fe` — each preceded by `call 0x1406e6be0` (rng lea) with `xmm1/xmm2` amp args from stack slots (`[rbp+0xa0/0xa8/0xb0]`, `[rbp−0x38/−0x40/−0x3c]`, `[rbp+0x98]` family). **OBS**; amp→meaning mapping open (register flow, §7.5).
- **Range draws**: `edx=0x3e8(1000)`@`0x140839a87` + 3 more `0x3e9(1001)` (`0x14083a063,0x14083a15e,0x14083a1bc,0x14083a211`), `edx=0x32(50)`×2 (`0x14083a143,0x14083a173`), `edx=1` (`0x14083a3fd`), `edx=0x1f(31)` skill probe (`0x14083a729`), `edx=0x28(40)` stat read (`0x14083a8c9`), `edx=0x28005` bt word (AI-state mask, not RNG). Success rolls `FUN_1406ebf30` at `0x140839b00,0x140839ba0,0x140839c30,0x140839d95,0x140839fe0,0x14083a00b,0x14083a09c,0x14083a143–0x14083a1fe chain,0x14083a2ed,0x14083a57c,0x14083a649,0x14083a84f,0x14083ca61`. **OBS**.
- **Body/angle gates (OBS structure, INF(med) labels)**: `test [rdi+0x2d14],1`@`0x14083a300` (jostle/fall flag → skip to `0x14083a5aa`); action-`0xa` SHOOT check; AI-state bits via `bt edx(0x28005),ecx` with `ecx=[rbp−4]` (0x11-family); height gate `f32[rdi+0x2bd0]` vs `0.2`@`0x142598578` (`0x14083a3a1`); `0x1404c8dc0` km/h conversions; clamp chain to speed bounds.
- **Leg/condition terms (OBS)**: `double[rdi+0x2480]`, `u32[rdi+0x2488]` (`0x14083a6d2`); `GetGameSpeed`; `sqrtf`; skill `0x1f` test selects clamp rows `16.0/22.0` vs `11.0/19.0` (`0x14259be10`/`0x1425c48c0` vs `0x14259bdfc`/`0x1425c65e8`); curl factor `0.217371866`@`0x1425a9bd4`; final `min(u,1000/2001)`-family (`60.0`-scale `0x14259a764`, `1000.0`@`0x14256ae34`). **OBS** addresses/immediates; fatigue/leg semantics **INF(med)** (matches kick-launch §3.4).
- **Curl stat**: no `stat(ap,0x21)` call in this body (OBS: no `edx=0x21` site); Curl enters via the predictor tilt + assist curl below, not the scatter amps (open §7.3).

## 5. Pass assist `FUN_14083c590` (OBS; pass actions only)

- Gates: kick-category `r14d==9`?, `r15d==1`, action `0x14083c8a5`-family `==9`, team-instruction via `0x140a638c0(rbx+0x2bdc)` (`0x14083c9be`), `r14d==9` re-check at `0x14083c807`-family. **OBS** compares; exact gate semantics **INF(med)** (matches kick-launch §3.3 "pass actions only, rolled at 50%").
- Gap formula constants re-resolved: `100.0`@`0x14253ee30`, `120.0`@`0x14256ae30`, `−60.0`@`0x1425c48fc`, `0.7`@`0x14257a194`, `30.0`@`0x142591140`, `80.0`@`0x142596e6c`, `135.0`@`0x14259be60`, `−55.0`@`0x1425c6608`, `22.0`@`0x1425c48c0` (curl add), `0.9`@`0x14258c260`, `90.0`@`0x14259be5c`, `1000.0`, `3600.0`@`0x142598590`, `−360.0`, `−1.0`. Formula per kick-launch §3.3 (`a=clamp01((gap−120)/60)`, `b=clamp01((v−80)/55)`, curl `±22°·a·b`) — **OBS** constants, **INF(med)** assignment (register flow not fully traced).
- Draws: `Range(100)`@`0x14083ca35`, success roll `thr=50`@`0x14083ca61`; curl applied via `0x140a92c80` sin/cos ×4 and written to `[r13+0/4/8]`. **OBS**.

## 6. RNG: generator and slot (OBS)

Every kick-error draw is **slot 2 of the per-player `math::Random`** (`ap+0x32d0`, ctor defaults `193488…193491`, seeded once per player by `0x1406eb670` slot 2 — rng-slots §6/§9). Evidence: all `Range` sites in §3–5 use `edx=2` (or the `Range0`-free `Normal` fixed-slot-2 path); all success rolls go through `0x1406ebf30`/`0x1406e6bf0` which hard-wire `edx=2` on `rcx+0x32d0`. No `Range0` (match slot 1), no main-match `ctx+0x5708`, no sub-generator draws in either launch path. GF mapping: `pesrandom.cpp` kick draws must come from the per-player stream, never the match RNG (rng-slots §7/§13 corroborates: "kick launch / scatter: slot 2 (Normal)").

## 7. Live check (OBS — 2 launches captured 2026-10-14; requester attribution open)

Tool: scratch `kickcap2.py` (20 Hz poll of sim-ball `+0xc` speed, jump `prev<2.0 → sp≥2.0` = launch). Ball located via BallControl-vtable scan (`simBall+0x64`); MA mirror found at `MA+0x2482c` (validated by the +0x24840 nearest-list). Player slots read null that run (the brief's known null-window — slots must be polled to non-zero first, cf. stamina-drain §1), so kicker attribution is open.

| # | t (s) | pos (m) | vel (m/s) | speed |
|---|---|---|---|---|
| 1 | 3.4 | (−11.89, 0.22, −14.33) | (−4.45, 0.39, 3.15) | 5.47 |
| 2 | 7.4 | (−12.96, 0.19, −8.74) | (3.04, 2.00, 14.95) | 15.38 |

 v3 (`kickcap3.py`: 22/22 slots polled to non-zero, MA mirror + sim ball, guarded `ap` reads) ran ~85 s and captured **0** launches; the match was at MENU by the end of the window. Requester-vs-launch comparison remains open with the exact next step in §7 para 1.

## 8. GF counterparts (code each finding replaces)

- `src/sim/onthepitch/player/humanoid/humanoid.cpp` `GetBestPossibleTouch` (pass error: stat×0.80 difficulty, skill/style multipliers, `randomRotation`, `GetPassErrorLoft`) → replace with §2 stat select + §3 logistic/exponential + §5 assist.
- Same file shot path (~`shotSkills`, `sigma_m`, `random(±1)` aim jitter) → replace with §2 SHOOT stat + §4 scatter + §6 slot-2 stream.
- `gameplaytuning.hpp` `GetPassErrorLoft`, `teamphilosophy.cpp` `GetPassErrorMultiplier`, `playerskills.*` pass multipliers → replace with §5 constants/gates (PES has no philosophy term in the error; team style only enters AI target choice).
- `pesrandom.cpp` kick callers → rewire to per-player slot-2 (cf. rng-slots §13 GF mapping); `pesstamina.cpp` A5/breath already ported — wire its outputs as the stat multiplier input to §2 (A5 set includes `0x1a/0x1b/0x1c`, excludes `0x1d`).

## 9. Open (with exact next step each)

1. **Live validation** (§7): needs MA-ball capture as above.
2. **Weak Foot Accuracy byte** (`block+0x36`, Player.bin 0x2c): no consumer found in launch; candidate sites are the `0x14071d770` tail (`edx=0x34/0x26`) and the `footSign` gate. Next: decompile `0x14071d770` tail + `0x140719f20` (per-family index select) via `dec.sh`, then `pdis.py` the `0x34/0x26` readers.
3. **Curl `0x21` / Kicking Power `0x2a` in error**: no `stat(·,0x21/0x2a)` site in §4; Curl likely only scales tilt (§3) and KP only scales speed. Next: batch-decompile `0x1408397a0` mid-section + keeper punt `0x140748b20` and grep `edx` immediates.
4. **`0x14071d770` tail (`edx=0x34/0x26`, clamp `12/22 vs 3/−3`)**: bytes OBS, purpose unknown. Next: `dec.sh` that range + annotate.
5. **Scatter amp→angle mapping** (§4 `xmm1/xmm2` args): register flow from `[rbp±]` slots to degrees not traced. Next: static slice from each `Normal` call back to its amp load (all in `scatter_full.txt`).
6. **Pressure / Balance**: no `stat(·,0x29)` consumer in kick path; nearest proxies are `+0x2d14` flag bits, AI-state mask `0x28005`, `+0x2bd0` gate. Next: live-diff those bytes on pressured vs free kicks (MA-ball capture above + AI-state sample).
7. **Headers/clearances/keeper punt-throw/inplay-demo** (`0x140748b20`, `0x1407e7f20`, `FUN_1408397a0` tail from `0x14083b2d0`): not decoded. Next: `dec.sh` each + caller scan.

## 10. Live (OBS — 10 ball-acceleration events, 2026-10-14 window; match since ended)

Tool: scratch `kicklive.py` (read-only pread, ~18 Hz poll, no watchpoints, no decompile queue, nothing written): launch = sim-ball `+0xc` speed jump `prev<2.0 → ≥2.0`; per event, nearest-ap snapshot (idx, act, pos, `ap+0x2bc8` aim, `+0x2bcc` h, `+0x2bd0` pow, `+0x2bf4` tgt, `+0x2d04/+0x2d0c` curve, `+0x2752` kind, `+0x2754` cat, `+0x2d14` flags, hit floats `ap+0x68/+0x70`). **OBS**: sim ball only — the MA mirror was not used. Round-2 sampler `kicklive2.py` (adds roster-block + live-stat bytes per stamina-drain §1/matchlogic §1.3, and slot-2 state) acquired late and captured 0 events. Both samplers are ready for the next match. Slots only, no names.

| n | fr | act | kind | kick d (m) | sp | launch az° | tgt | dist (m) | err° | pow | h | c1/c2 | f14 | hr8,hr10 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 28118 | 9 | 75 | 0.93 | 17.52 | −125.9 | (−41.79,0,24.59) | 15.0 | −0.2 | 0 | 0 | 0,0 | 0 | 0.013,−0.037 |
| 2 | 28230 | 9 | 42 | 0.70 | 16.61 | 142.0 | (−34.00,0,13.36) | 14.8 | −8.6 | 0 | 0 | 0,0 | 0 | −0.016,0.030 |
| 3 | 28485 | 4 | −1 | 0.86 | 5.91 | 67.1 | (0,−1,0)† | — | — | 0 | 0 | 0,0 | 0 | −0.028,0.016 |
| 4 | 28534 | 4 | −1 | 0.66 | 3.61 | −95.2 | (0,−1,0)† | — | — | 0 | 0 | 0,0 | 0 | 0.066,−0.018 |
| 5 | 28552 | 4 | −1 | 0.60 | 4.75 | −121.2 | (0,−1,0)† | — | — | 0 | 0 | 0,0 | 0 | −0.025,−0.005 |
| 6 | 28876 | 4 | −1 | 0.62 | 6.33 | 26.4 | (0,−1,0)† | — | — | 0 | 0 | 0,0 | 0 | −0.022,0.024 |
| 7 | 29096 | 4 | −1 | 0.67 | 7.25 | 79.4 | (0,−1,0)† | — | — | 0 | 0 | 0,0 | 0 | 0.028,0.017 |
| 8 | 29280 | 9 | 129 | 0.84 | 2.09 | 73.7 | (36.30,0,4.26) | 7.2 | —‡ | 1.0 | 0 | 0,0 | 0 | −0.007,−0.018 |
| 9 | 29284 | 9 | 63 | 0.82 | 2.20 | 78.5 | (34.91,0,3.11) | 7.8 | —‡ | 1.0 | 0 | 0,0 | 0 | 0.031,0.003 |
| 10 | 29293 | 9 | 63 | 0.80 | 12.41 | 164.9 | (35.66,0,3.01) | 8.1 | +2.3 | 1.0 | 0 | 0,0 | 0 | 0.039,0.038 |

(az = `atan2(vx,vz)`; req = bearing(launch pos→tgt); err wrapped. † = sentinel tgt, comparison meaningless. ‡ = pre-kick settle wobble, see OBS 5.)

1. **OBS — `ap+0x2bf4` is the requested target.** On the 3 genuine kicks (act 9 + non-sentinel tgt + sp>10: n=1,2,10) the launch bearing matches the pos→tgt bearing within −0.2°, −8.6°, +2.3°. **INF(low)** the single-digit errors are consistent with the §3–4 predictor/scatter magnitude — a real check needs the kicker's stat bytes, which round 1 did not capture.
2. **OBS — the aim field is not a unit vec3.** Pre-contact reads (n=8–10, same kicker, same value across 13 frames) give `ap+0x2bc8` = [117.54, 0, 1.0]; post-contact reads (n=1,2) give zeros. **INF(med)** the block is consumed/cleared at contact, so post-jump polls race the clear — the exact next step is to arm on kicker action-entry/strike phase and snapshot pre-contact. Aim/power units stay open; only tgt is confirmed live.
3. **OBS — sentinel = no request.** All 5 act-4 events read tgt (0,−1,0) + kind −1 + cat 0xFFFF. Requested-vs-actual there is garbage (±70–130°) and must be gated out. **INF(med)** act-4 accelerations are traps/bounces/deflections, not kicks.
4. **OBS — detector trips on pre-kick settle.** n=8,9 (sp 2.1–2.2, just over threshold) are the same kicker's ball 13/9 frames before the real kick n=10 — threshold-edge wobbles, not separate kicks. A "few dozen kicks" count must cluster events by kicker+window first.
5. **OBS — constants on these kicks:** h=0/10 (ground balls, y≈0.11 ≈ contact radius); c1=c2=0/10 (no curl requested on short ground passes — Curl never enters here even as a request); f14 (`+0x2d14`)=0/10, so no jostle-flagged (pressured) kick was sampled — §9 item 6 still needs one.
6. **OBS — pow is not monotonic in distance/speed:** 0 on both 15 m passes launched at ~17 m/s, 1.0 on the 8 m pass launched at 12.4 m/s. Power-field semantics stay open.
7. **OBS — hit floats `ap+0x68/+0x70` read ±0.01–0.06, never launch speeds.** Either consumed before the poll or mis-scaled — open.
8. **OBS — attribution is sane:** nearest kicker ≤0.93 m from the ball at every event; all 10 kickers idx ≥11 (one side's possession phase).
9. **OBS — MA resolution failed this window:** the TacticsRE MC→Flow→MA chain's ap-scan took ~176 s under multi-agent load, and the re-acquired cached ball was a desynced shadow copy (frozen vel, teleporting pos). Next match: resolve MA first via the chain, then run `kicklive2.py` against the MA ball.

Open deltas: §9 item 1 PARTIAL (3 attributed kicks, tgt confirmed, errors single-digit; full validation needs pre-contact snapshots + kicker stats + MA ball). Items 2 (Weak Foot Accuracy byte), 3 (Curl `0x21`/KP `0x2a`), 6 (pressure/Balance) still OPEN — `kicklive2.py` is ready and names the exact next step (run it live, gate on act∈{8,9,10} + non-sentinel tgt + `+0x2d14` bit 0 set for the pressured sample). Items 4, 5, 7 untouched live.

Addendum — second window (new match, `kicklive2.py` with roster-block + live-stat + slot-2 snapshots): the sampler acquired (25 aps) but the match sat in stoppage and the 540 s window captured **0** events (`kicks_live3.jsonl`). Model-magnitude check on the 3 genuine kicks above: observed |err| = 0.2°/8.6°/2.3° (mean 3.7°) on ~15 m passes. Reference errScale (§0.3): S=70→0.426, 80→0.226, 90→0.103 — but errScale multiplies the exponential amps whose degree mapping is open (§9.5), and the kickers' stat bytes were never captured, so this is **INF(low)** consistency, not verification. **Settled: nothing beyond the §9-item-1 partial above.** Still open: Weak Foot Accuracy consumer (item 2), Curl `0x21`/KP `0x2a` (item 3 — note c1=c2=0 on all 10 events, so no curl was ever requested live), power-field semantics (§10 OBS 6), pressure/Balance (item 6 — no `+0x2d14`-flagged kick sampled), items 4/5/7 untouched. Exact next step: run `kicklive2.py` on a live in-play match, gate events on act∈{8,9,10} + non-sentinel tgt, and take one `+0x2d14`-bit-0 pressured sample.
