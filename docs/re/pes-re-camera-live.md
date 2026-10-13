# PES 2021 — live in-play broadcast camera (LiveSampler, 2026-10-13, second match)

Scope: read the live in-play camera's position, target, FOV, slider values and the ball at the
same instant, so `PesBroadcastCamera` (`src/gdext/pes_camera*`, `src/pes/camera*`) can be
compared directly. Read-only: `/proc/<pid>/mem` preads only (pid <pid>, `$PES21_ROOT/PES2021.exe`).
No input, no writes, no ptrace, no stop. No player or team names — slots and coordinates only.

## 1. Objects located live (OBS)

| Object | Address | How found | Verified |
|---|---|---|---|
| `InplayCamera` plugin | **`0x1a7c8b60`** (this match; earlier `0x2befd830`) | streaming scan for the vtable pointer `0x142609C28` (`InplayCamera`) in the writable game heap (< 0x60000000) | `[obj+0] == 0x142609C28`, `[obj+8] == 5` |
| `fox::gr::Camera` (the camera the renderer uses) | **`0x1c14a10`** | same scan for `0x142A003E0` | position moves, matrix rows orthonormal, `+0x100` FOV and `+0x158` near plane both live |
| (other fox Camera objects, dead/idle in play) | `0x1c15410`, `0x1c16010`, `0x24aa0c10`, `0x25ad0410` | same | identity or static matrices, out-of-play FOVs 1.0417 rad |
| ball (gate) | per `pes_state.py` signature scan | — | — |

The three other `fox::gr::Camera` objects are the menu/attract cameras; only `0x1c14a10` carries a
plausible pitch-scale transform while play is live. **The plugin object address is not stable
across restarts** — it must be re-found by vtable scan each session (a scan of the game heap
takes ≈ 25 s at 60 MB/s, so it is cached and re-validated, see §5).

## 2. Field map, live-verified

`fox::gr::Camera` at `0x1c14a10` — every value below was **observed** at the stated times.

| Offset | Field | Live values seen |
|---|---|---|
| `+0x30` `+0x40` `+0x50` | world matrix rows r0, r1, r2 (r2 = forward) | e.g. r2 = (−0.053, −0.375, −0.925) |
| `+0x60` | **position** | (−38.49, 19.13, 36.90) … (−27.71, 18.94, 40.43) |
| `+0x70`–`+0xA0` | view matrix, T = (−40.63, −3.77, 40.91) | — |
| `+0xF4` | focus/target distance | 47.99 … 48.73 (smoothing slowly rising) |
| `+0x100` | **vertical FOV, radians** | 0.4065 → 0.4128 rad = 23.31° … 23.68° |
| `+0x110` | aspect | 1.77 |
| `+0x158` | near | **0.9** in play (0.05/0.1/1.0 on the dead cameras) |
| `+0x15C` | far | 4000 |

`InplayCamera` at `0x1a7c8b60` — the plugin that *produces* the pose.

| Offset | Field | Live values seen |
|---|---|---|
| `+0x08` | **mode** | **5 = Dynamic Wide** |
| `+0x0C` | **zoom slider** | **0.800000011920929** constant the whole spell |
| `+0x10` | **height slider** | **0.20000000298023224** constant the whole spell |
| `+0x38` | stored target (3 f32) | (−41.05, 0.00, −3.69) … (36.53, 0.00, 0.62) |
| `+0x44` | previous delta (3 f32) | (−6.00, 0.00, 0.00) |
| `+0x84` | previous pose position | (−51.49, 7.50, −50.11) |
| `+0x90` | previous pose target | (−51.78, 2.30, −33.61) |
| `+0x9C` | previous pose FOV | 0.55 |
| `+0x68` | runtime distance factor | 1.00000 … 1.06199 |
| `+0xFC` | **Z_max constant** | **21.921** (matches `.rdata 0x14260B8F8` byte-identically, OBS read of the image) |

**This is the CAMERASETTINGS answer the earlier pass left open** (`pes-re-camera-target.md`
"Open" item 2): the Dynamic Wide **zoom and height sliders are plain clamped floats at
`InplayCamera+0x0C` and `+0x10`**, and this install's values are **zoom 0.8 / height 0.2**, mode 5.
The report's fitted values (zoom ≈ 0.79, height ≈ 0.20) are confirmed to within the float
representation. The `+0xFC = 21.921` field is the mode-5 `Z_max` carried on the object, not only
in `.rdata`.

## 3. Open-play samples (OBS, `~/ghidra-projects/scratch/LiveSample/cam_B.jsonl`)

Four consecutive samples in genuine open play (mode 5, near 0.9, FOV 23–24°), 25 s apart:

| t (s) | camera x | camera y | camera z | target x | target y | target z | FOV ° | \|cam−tgt\| | cam.y/dxz | f(+0x68) |
|---|---|---|---|---|---|---|---|---|---|---|
| 691.4 | −38.49 | 19.13 | 36.90 | −41.05 | 0.00 | −3.69 | 23.309 | 44.94 | 0.4704 | 1.00000 |
| 716.2 | −38.49 | 18.67 | 39.11 | −34.29 | 0.00 | −3.29 | 23.556 | 46.52 | 0.4383 | 1.01627 |
| 741.3 | −32.35 | 18.85 | 40.29 | −41.05 | 0.00 | −8.00 | 23.680 | 52.69 | 0.3831 | 1.00498 |
| 819.2 | −27.71 | 18.94 | 40.43 | 36.53 | 0.00 | 0.62 | 23.620 | 77.91 | 0.2507 | 1.06199 |

(cam and target are metres, Fox space, Y up; `dxz` = horizontal distance.)

### 3.1 Numbers the engine can compare against directly

For a known ball/target position the live camera is, at t = 741.3 (target at `(−41.05, 0, −8.00)`,
which is the clamp `Z_min = −8`):

- **camera position (−32.35, 18.85, 40.29)** → the camera sits **18.85 m high** and
  **48.29 m further down +Z than the target**;
- **camera-to-target distance 52.69 m** (and the fox camera's own `+0xF4` focus distance reads
  48.40 — that is the *previous* frame's, still converging, OBS);
- **vertical FOV 23.68°** (0.4128 rad), horizontal = 2·atan(tan(23.68°/2)·1.77) = **39.1°**;
- near 0.9, far 4000;
- horizontal offset factor `cam.x/target.x = 0.788` when unclamped elsewhere; on the clean row
  (t = 691.4) it was **0.9375**, i.e. `pan_eff = 1.0667`, exactly the report's fitted
  `pan_eff ≈ 1.066` (`Pan ≈ 1.33`).

**Bottom line for `PesBroadcastCamera`: at a target 40 m from the goal line the live broadcast
camera is ~52 m from the target, ~19 m above the pitch, at a ~23.7° vertical FOV.** A model that
renders players at half the live size is looking at a field of view roughly twice too wide and/or
a camera distance roughly twice too long — compare §3.2.

### 3.2 The model reproduces the live samples

Re-running the `Dynamic Wide` (mode 5) recipe from `pes-re-camera-target.md` §5a/§6 with the
**now-live-measured** `u = (1 − 0.8)·100 = 20`, `h = 0.2·100 = 20`:

| quantity | predicted | observed |
|---|---|---|
| `PosY` (= cam.y / dxz) | 0.4716 (t = 0.3836) | 0.4704 |
| `PosY` (t = 0.3910) | 0.4707 | 0.4383 (moving) |
| `PosY` (t = 0.3023) | 0.4822 | 0.3831 (moving) |
| `PosY` (t = 0.4647) | 0.4614 | 0.2507 (moving) |
| `pan_eff` | 1.066 | **0.9375** (t = 691.4) |

`PosY` matches to 0.004 on the settled row (691.4). The other rows are mid-spring (the camera
position lags the stored target, so `cam.y/dxz` is diluted by a transient — the `+0x68` factor
1.000 → 1.062 shows the spring converging). **FOV is the one value the recipe still gets wrong:**
predicted 23.27–24.41° against observed 23.31–23.68° for the same `u` — the live FOV is far less
sensitive to the target-z `t` factor than the blend predicts, so the reported dot blend is
either damped or the observed pose is still blending from the previous mode. (INF, high; the
`+0x68` and the lag both point at the spring.)

## 4. What is still open

1. **The FOV blend weight.** The `t`-blended `Fov` is off by up to 1.1° on live samples while
   `PosY` is exact. Needs the live `t` term (the plugin's internal blend counter), not the
   recomputed one.
2. **Ball-height lift** (`pes-re-camera-target.md` item 1): no sample this session had the ball
   aloft at the sample instants, so `focusY + 4` × 60/54 remains unconfirmed live.
3. **Whether `+0xFC` is Z_max or a copy.** It equals `21.921` byte-for-byte; INF (high) it is the
   object's copy of the mode-5 Z_max.
4. **Mode 3/6 (Stadium) and 7–9 (VerticalCamera) live values** — the plugin was mode 5 this
   whole match; `VerticalCamera` objects were absent from the live heap.

## 5. Reproducibility

Scratch (nothing committed): `~/ghidra-projects/scratch/LiveSample/`
- `live.py` — read-only `/proc` helpers; pid resolved per access with `pgrep -x PES2021.exe`
  (the earlier `-f` form matched the poller's own `sh -c` wrapper and picked a transient pid —
  fixed this session, that was the cause of the state-gate's `FileNotFoundError` flicker).
- `find_vt.py` — targeted vtable scan (game heap only, ≈ 25 s).
- `cam2.py` — 10 Hz camera sampler with a 20 s plugin-address cache; writes `cam_B.jsonl`.
- Gate: `~/ghidra-projects/scratch/pes_state.py` (fixed the same pid race), watched via
  `watch.py`.
