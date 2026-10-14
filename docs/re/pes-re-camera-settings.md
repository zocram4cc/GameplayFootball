# PES 2021 — in-play camera settings: `registry::CameraSettings` layout, the `Copy<>` vtable + copy body, and the writer of `InplayCamera+8` (CameraSettingsRE)

Static only. No process was running (`pgrep -f PES2021.exe` empty), so everything below is from the on-disk `PES2021.exe` plus raw-disassembly cross-checks (`pdis.py`, capstone over the file). OBS = observed with the VA given; INF = inferred, with level. File offsets: `.trace`/`.rdata` = VA − `0x140000A00`; `.data` = VA − `0x140000E00`. The serializer field-name table is still behind mutation-obfuscated code (`0x14044e0?? → 0x15b0fb760`), but the ticket was closable **around** it, via the RTTI registry + the `Copy<>` vtable + the copy blob + the plain-code readers.

Prior reports mis-identified which registry block is which (the 12-qword block *after* a name belongs to the *next* setting, and the per-class slot-2 thunks were attributed to the wrong class). Corrected below; the old item-5/item-7 blocker ("need the serializer") is now bypassed for the layout, but still stands for the live save-value source.

## 1. Registry layout (OBS, `.rdata` file `0x259bf38…0x259c470`)

Each setting is a 4-part unit in file order (all OBS by linear walk + COL/TD decode; MSVC RTTI here uses 32-bit RVAs, which is why VA pointer-scans find nothing):

`[Copy<T> vtable: COL + 11 funcs] [Ref-block] [name] …` — concretely per setting: Copy-vt (12 qwords), then Ref-block (12 qwords: `Reference<Copy<T>>` COL, misc, `ReferenceCopy<T>` COL, misc, `TRef` COL, misc, name-thunk, `0x140a48990`), then the ASCII name. Verified chain for this ticket:

| file | content | VA |
|---|---|---|
| `0x259c0d8` | `Copy<CameraSettings>` vtable (12 qwords, first = COL) | `0x14259cae0` |
| `0x259c138` | `Reference<Copy<CameraSettings>>` block (12 qwords) | `0x14259cb38` |
| `0x259c198` | `CAMERASETTINGS` (+ `0x14044e060` name-thunk returns it, OBS decompile) | `0x14259cb98` |
| `0x259c1a8` | `Copy<PkMatch>` vtable (next setting's) | `0x14259cba8` |

COL of `Copy<CameraSettings>`: file `0x2c912d8`, VA `0x142c91cd8` (`sig=1,off=0,cd=0`, pTD RVA `0x3615160`, OBS). TD struct file `0x3614360` (`.data`), name at +16: `.?AV?$Copy@UCameraSettings@registry@match@@VLogger@impl@util@game@cobra@@…` (OBS). The vtable at `0x14259cae0` has `vt[-1] = 0x142c91cd8` (OBS) and its 11 slots are **qword-identical** to the registry block at file `0x259c0d8` (OBS `memcmp` of all 12 qwords incl. COL).

Killed hypothesis (was in the old item-5 notes): "the CAMERASETTINGS block's vtable is at `0x14259cba8`". That address (file `0x259c1a8`) is the block *after* the name = `Copy<PkMatch>`. Likewise the old "CAMERASETTINGS field thunks `0x140454110(+0x44) / 0x140454120(+0x1b4) / 0x140454130(+0x34)`": `454120` is `Copy<PkMatch>` slot 2, `454130` is `Copy<NoOperationLeave>` slot 2, `454110` is `Copy<GPAchievement>` slot 2 (all OBS from the corrected block ownership). Each `Copy<T>` vtable slot 2 is a per-class "serialized base" thunk, not a camera field. Only `0x1404540f0 (+0x13)` belongs to `Copy<CameraSettings>`.

## 2. `Copy<CameraSettings>` vtable (OBS, VA `0x14259cae0`, file `0x259c0e0`)

| slot | VA | body (OBS: decompile + raw-disasm cross-check) |
|---|---|---|
| −1 | `0x142c91cd8` | COL (RTTI locator, §1) |
| 0 | `0x140466c00` | `return [rcx+0x10]+4` — **live-data base** |
| 1 | `0x1404540f0` | `return [rcx+0x10]+0x13` — **serialized-data base** |
| 2 | `0x140421f40` | `return 0` |
| 3 | `0x1404437e0` | guarded init (calls `0x140442510`; `param_2&1` → alloc/commit path) |
| 4 | `0x141794410` | nop (`ret`) |
| 5 | `0x140454140` | one-shot (`xchg [rcx+0x18]`) registration of the copy blob `0x140450ff0` via `0x141b55830` + vt+`0x88` call |
| 6, 7 | `0x140a4c0f0` | `return 1` |
| 8 | `0x140454050` | nop |
| 9, 10 | `0x141794410` | nop |

`[rcx+0x10]` is the data pointer (OBS: every accessor thunk is `mov rax,[rcx+0x10]; add rax,IMM; ret`). Full accessor-thunk census (OBS exhaustive regex scan of `.trace` for that shape, 38 distinct IMMs): the only ones owned by `CameraSettings`' vtable are `+0x04` (slot 0, shared shape) and `+0x13` (slot 1).

## 3. Copy body = the layout (OBS, blob VA `0x140450ff0`, raw-disasm verified)

Registered by slot 5 (`0x140454140` passes `0x140450ff0`, OBS decompile; the PkMatch sibling `0x140454280` passes `0x140451060`, same shape). The blob is plain code (no obfuscation):

```
dst+0x04 [8B] <- src+0x13 [8B]   movsd [rcx+4],[rcx+0x13]   0x140450ff0
dst+0x0c [u32] <- src+0x1b [u32]  mov   [rcx+0xc],[rcx+0x1b] 0x140450ffa
dst+0x10 [u16] <- src+0x1f [u16]  movzx/mov [rcx+0x10]      0x140451000
dst+0x12 [u8]  <- src+0x21 [u8]   mov   [rcx+0x12],[rcx+0x21] 0x140451008
ret                                            0x14045100f
```

So `registry::CameraSettings` **data layout** (offsets are from the data pointer = `[CopyObj+0x10]`; the live struct starts at data+`0x04` per slot 0):

| data off | size | meaning | evidence |
|---|---|---|---|
| `+0x04` | 8 B (`movsd`, i.e. 2×f32 or 1×f64) | INF(high): **zoom + height slider pair** (two floats; matches `InplayCamera+0xC/+0x10` f32 pair the camera consumes) | copy blob; consumers `FUN_1408a1a70` read `[InplayCamera+0xC]` zoom (`0x1408a1e77`) and `[+0x10]` height (`0x1408a1e5b`) as f32 in [0,1] |
| `+0x0C` | u32 | INF(high): **camera mode int** (feeds the mode chain §4) | copy blob; mode chain consumes one int into `InplayCamera+8` |
| `+0x10` | u16 | other (unknown which) | copy blob only |
| `+0x12` | u8 | other (unknown which) | copy blob only |
| `+0x13` / `+0x1B` / `+0x1F` / `+0x21` | 8/4/2/1 B | serialized (save-file) mirror of the four live fields | copy blob src side; slot 1 returns `+0x13` |
| data size | ≥ `0x22` | — | highest touched offset `0x21`+1 |

"zoom/height pair" vs "one double" is INF(high), not OBS: the 8 B move could be a single f64, but the only two-float camera sliders in the consumer (`+0xC/+0x10`) fit exactly, and the ctor defaults both to `1.0f` (§4). The `+0x10/+0x12` (u16/u8) meanings are OPEN. Sibling classes show the same blob idiom at larger scale (PkMatch: `[rcx+4] ← [rcx+0x1b4]`, `0x1B0` B via 3×`0x80` + `0x30` tail, OBS `0x140451060`), confirming the blobs are serialized→live copy loops, not per-field code.

Related RTTI (OBS): `CameraSettingsRef` (`0x142c91a90`), `Reference<Copy<CameraSettings>>` (`0x142c91be8`), `ReferenceCopy<CameraSettings>` (`0x142c91bc0`), `ScopedRead<CameraSettingsRef>` / `ScopedWAndC<CameraSettingsRef>` TDs (file `0x36bcdd0`/`0x36bcd76`); `ScopedRead` has no vtable (stack guard, OBS: its COL `0x142e027a8` has no `vt[-1]` user). Small wrapper funcs `0x14087b334` / `0x14087d2b4` cache the live base (`[[rdi+0x10]+0x10]+4 → [rdi+0x18]`, OBS decompile); no direct callers (vtable/func-ptr dispatched, OBS E8 scan).

## 4. Writer of `InplayCamera+8` (the mode) — traced to the match-struct holder; one hop remains OPEN

OBS chain (every branch cross-checked against raw disasm):

- `InplayCamera` ctor `0x14087e480` stores its `edx` arg → `[rdi+8]` at **`0x14087e965`** (`mov [rdi+8],eax`, OBS), with defaults `[+0xC]=1.0f` (`0x14087e968`), `[+0x10/+0x14]={1.0f,0.0f}` (`0x14087e96f`), `k=[+0x68]=1.0f` (`0x14087e97b`), `[+0x24]=1`, `[+0x28/+0x2C/+0xA4]=0xff`, `[+0xB8]=1000.0`, `[+0xEC]=2` (OBS raw).
- Ctor callers (OBS exhaustive E8 scan): **`0x14087a4ba`** (demux), **`0x14087abfc`** (camera switcher `0x14087ab70`), **`0x140886619`** (holder creator `0x140886560`: stores `edx→[rcx+8]`, `r8b→[rcx+0xC]`, OBS).
- Mode source at each site is a byte at **`+0x44`** of a holder struct: demux `0x14087a47d` (`movzx ebx,[rcx+0x44]`, OBS), switcher `0x14087ab70` (`movzx ebx,[r9+0x44]`, OBS), `0x14087a640` (`movzx edi,[r9+0x44]`, OBS).
- Dispatch (OBS): demux: `1…6 → InplayCamera` ctor (size `0x2e8`), `7…9 → VerticalCamera` ctor `0x14087f870` (size `0x598`); mode `0` creates **neither** (falls through, OBS). Factory `0x14087ac70` range-guard OBS: `(mode−1) ≤ 8` or `mode == 0xC`, else save `[+0x18]` and return; inside it rewrites the holder's `[+0x10/+0x14/+0x18/+0x1C/+0x20/+0x24]` slots (OBS decompile + raw).
- Switcher `0x14087ab70` (OBS decompile `[[0x14087ab70,0x14087ac62]]`): early-out if `param_2 == 0x10` with match-state gating; rebuilds the plugin iff `[r9+0x44] != edx`; called from manager `0x14087a930` tail with `r9 = [rsp+0xA8]` (OBS raw `0x14087ab34…0x14087ab52`).
- Upstream: `0x140879cae` reads **`[r15+0x44]`** (wanted-mode byte, OBS) into the `0x14087a640 → 0x140886560` holder-creation path (OBS E8-confirmed callers `0x140879ce2`, `0x14087a69d`). `r15` is the match struct (same register carries `[+0x1168/+0x116C]` match-state words and `[+0x155C/+0x1564]` floats, OBS).
- **OPEN (one hop): the writer of `[matchstruct+0x44]`.** That byte is the live camera-mode holder; nothing in `.trace` writes `[reg+0x44]` on this path with an E8-visible caller in budget (vtable-driven manager update `0x14087a930` has no E8 callers, OBS). It is *not* `CameraSettings.data+0x44` (no such thunk exists for this class — killed hypothesis, §1). Next step: break on `0x14087a930`'s vtable slot in the live game (not allowed here) or decompile the match-update vtable owner of `0x14087a930` and chase `r15`'s `+0x44` stores there.

## 5. Field → reader VAs (OBS)

| `CameraSettings` live field | InplayCamera field | reader VA | note |
|---|---|---|---|
| `+0x04`[8B] (zoom/height, INF-high) | `+0xC` (zoom), `+0x10` (height) | `FUN_1408a1a70`: `0x1408a1e77` (zoom), `0x1408a1e5b` (height); ctor defaults `0x14087e968/0x14087e96f` | exact u/h expressions in `pes-re-camera-target.md` item 5a; the copy blob is 8 B `movsd`, so the pair travels together |
| `+0x0C`[u32] (mode, INF-high) | `+0x8` | ctor store `0x14087e965`; dispatch demux `0x14087a47d`, switcher `0x14087ab70`, factory `0x14087ac70`; consumers `InplayCamera::update` `0x14087f280` (`6/3 → FUN_1408a57f0`, else `FUN_1408a1a70`; `2 → rec#4`, `4 → rec#5`) | bridge `[matchstruct+0x44]` writer OPEN (§4) |
| `+0x10`[u16], `+0x12`[u8] | ? | no reader found | OPEN |

`InplayCamera+0x14` (non-mode-5 pan factor, OBS `0x1408a20cf`) and `+0x20` (smoothed tz, OBS `0x1408a1edf`) are runtime state, not settings.

## 6. Mode-int map (OBS ranges + INF names)

OBS: `1…6 = InplayCamera` (demux `0x14087a47d`/`0x14087a4ba` + switcher ranges), `7…9 = VerticalCamera`, `0 = no plugin`, factory also admits `0xC` (OBS `0x14087ac90`). `InplayCamera::update` behavior per int (OBS): `3,6 → FUN_1408a57f0` (stadium table `.data 0x1434a00c0`); `2 → rec#4`; `4 → rec#5`; `0,1,5 → bilinear rec#0…#3` with `5 → cameraInplay.o` branch. Exe enum keys (OBS string census): `PLAY_DATA_CAMERA_{MIDDLE, FAR, WIDE, PITCHSIDE, CUSTOM, DEMO, VERTICAL, PLAYER, SKY, DUMMY, NEW_WIDE, NEW_CUSTOM, TACTICAL, STADIUM, STADIUM_CUSTOM}` (+`_TARGET`, `NEW_CUSTOM_ZOOM/HEIGHT`, `STADIUM_CUSTOM_ZOOM/HEIGHT/PAN`). Menu order (OBS `eng/string/all.str`): Normal, Long, Vertical, Wide, Zoom, Tailing (Player/Ball), Blimp, Pitch Side, Mid Range, Live Broadcast, Fan View, Dynamic Wide, Dynamic Wide Custom, Stadium, Stadium (Custom) — 15 entries vs 10 ints, so not 1:1 (OBS).

| int | plugin (OBS) | code path (OBS) | menu name (INF) |
|---|---|---|---|
| 0 | none (demux falls through) | — | unset/default |
| 1 | InplayCamera | bilinear #0…#3 | Normal |
| 2 | InplayCamera | fixed rec #4 (Fov 14.9, PosZ 113.3) | Wide |
| 3 | InplayCamera | `FUN_1408a57f0`, PosZ=63 | Stadium (medium-high; old INF kept) |
| 4 | InplayCamera | fixed rec #5 (Fov 25.6, PosZ 70.5) | Mid Range |
| 5 | InplayCamera | `cameraInplay.o` | Dynamic Wide (high; live-fit) |
| 6 | InplayCamera | `FUN_1408a57f0`, 3 sliders | Stadium (Custom) (medium-high; old INF kept) |
| 7–9 | VerticalCamera | ctor `0x14087f870` | Vertical / Tailing / Blimp / Pitch Side / Fan View / Live Broadcast (unmapped; OPEN which 3 of these) |
| `0xC` | factory-accepted | holder save path `0x14087ac70` | unknown (OPEN) |

Only the plugin + code-path columns are OBS; **every name in the last column is INF** (medium-high for 3/5/6 from prior reports, medium-low for 1/2/4 from record shapes, e.g. rec#4's 113 m PosZ ≈ an end-to-end "Wide"). 0/1 exact names and 7–9/0xC need the serializer/menu mapping (same blocker as before).

## 7. What was tried and killed / left open

- Killed: "Copy-vt = the block after the name" (it is the block *before* the next name / *after* the previous name — owned via TD decode, not position); "`454110/454120/454130` are CAMERASETTINGS thunks" (they are GPAchievement/PkMatch/NoOperationLeave slot-2s); "demux `+0x44` = CameraSettings field" (no such offset exists for this class); "flat-image (`pes_full.bin`) pointer scan finds COLs" (file uses RVA32; use the disk exe).
- Tools: `pdis.py` on the disk exe for all branch-sensitive reads; `dec.sh`+`annot.py` for 10 functions (`0x14087ab70/0x14087ac70/0x14087b000/0x14087a930/0x1404437e0/0x1404439a0/0x140443a90/0x140443c10/0x141794410/0x140a4c0f0` + thunks/blobs); exhaustive E8 scans for ctor/wrapper/thunk callers; MSVC RVA-RTTI walk from TD → COL → vtable → `memcmp`.
- OPEN: (a) writer of `[matchstruct+0x44]` — next: decompile the vtable owner of manager `0x14087a930` (or live-break it); (b) `+0x10/+0x12` meanings + their readers; (c) whether live `+0x04` is 2×f32 or f64 (read the save-value path or the menu writer); (d) exact menu names for 0/1/7–9/`0xC`.
- Scratch: the <YourName> scratch dir (`notes.txt` + VAs above). No builds, no tests (per task). `docs/re` untouched.
