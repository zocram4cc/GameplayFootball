# PES 2021 "Fox registry" — resolved: it is MSVC RTTI, and the vtables are static

Scope: the dispatch blocker named in `pes-re-setpiece-referee.md` §1.8 / §7 item 2,
`pes-re-tactics-clock.md` §9 item 2 and `pes-re-ball-contact-2.md` §6.
Image: `<scratch>/pesmem.bin` (mid-match memory image, base 0x140000000, file offset = VA − base).
Scratch: `~/ghidra-projects/scratch/FoxRegistryRE/`.
Tags: **OBS** = raw bytes or a decompile at an address. Every formula quoted below was re-checked
against capstone disassembly of the raw bytes. **INF** = inference, with a confidence level.
No live process was used. Everything here is static.

---

## 1. Headline (OBS)

1. **There is no Fox object/interface registry and no runtime name-hash method lookup.** The
   records that the earlier report called "class-info" and "interface records" are the standard
   **MSVC x64 RTTI** structures:
   - The "class-info record" `{u32 td, u32 n, u32 0, u32 −1, u32 0, u32 0x40, u32 rva}` is an
     `_RTTIBaseClassDescriptor {pTypeDescriptor, numContainedBases, PMD{mdisp=0, pdisp=−1,
     vdisp=0}, attributes=0x40 (BCD_HASPCHD), pClassHierarchyDescriptor}`.
     - `n` is the number of bases *below* that class, **not** a method count.
     - The last field is a CHD RVA, **not** a vtable RVA.
   - The "vtable block" `{u32 0, u32 1, u32 count, u32 entriesRva}` is an
     `_RTTIClassHierarchyDescriptor {signature=0, attributes (bit0 = multiple inheritance),
     numBaseClasses, pBaseClassArray}`. Its "interface record" entries are the BCDs of the class
     and of every base.
   - The COL is `{u32 sig=1, u32 offset, u32 cdOffset, u32 tdRva, u32 chdRva, u32 selfRva}`.

   Raw example (OBS), `match::anime::action::CornerKick`:
   - COL `0x142c9c730` = `{1, 0, 0, 0x361aad0, 0x2c9c758, 0x2c9c730}`.
   - CHD `0x142c9c758` = `{0, 0, 3, 0x2c9c770}`.
   - BCDs:
     - `0x142c9c790` = `{0x361aad0, 2, 0, −1, 0, 0x40, 0x2c9c758}` (CornerKick itself);
     - `0x142c9c680` (Restart, n=1);
     - `0x142c9bd58` (Base, n=0).
   - The old `classinfo.json` entry for CornerKick is `["0x142c9c790", 2, "0x40", "0x142c9c758"]`. That is exactly this BCD, and its "vtableRva" is the CHD.
2. **The vtables exist in `.rdata` and are found the normal MSVC way.** The qword just before
   every vtable holds the absolute VA of its COL (`vtable[-1] = &COL`). The previous attempt
   looked for code pointers *next to the COL* and for `lea` references *to the COL*. By design,
   neither exists. Searching `.rdata` for the 8-byte COL VA finds the vtable.
   - OBS: all **13,816** COLs in `.rdata` (validated by `selfRva == own RVA`) have at least one
     vtable found this way. None has zero.
   - Example: `match::anime::action::DemoAction` COL `0x142c9bda8`. The qword at `0x1425dcf90` is
     `0x142c9bda8`, so the vtable is at `0x1425dcf98` = `{0x1406e2740, 0x141794410, 0x1406b5ba0, …}`.
3. **Calls are ordinary C++ virtual calls** (`mov rax,[rcx]; call [rax+slot*8]`). Every method
   can be reached statically as vtable → slot → code VA. No live walk is needed.
4. **The one real limit is protected code, not dispatch.** A few slots are a 5-byte
   `jmp rel32` into `.impdata` (`0x143d14000…`, mutation-protected, mostly outside the 64 MB
   dump). Examples:
   - `CornerKick` slot 2 `0x1407335c0` → `jmp 0x143fb8d40`;
   - `Base` slot 22 `0x1406deb20` → `jmp 0x143fb3990`;
   - `collision::Manager`'s per-frame code calls `thunk_EXT_FUN_15b227450`.

   These must be read from the on-disk exe (`fileoff = 0x391b400 + (VA − 0x143d14000)`, per
   `pes-re-setpiece-referee.md` §8) and may be obfuscated. The resolver flags them (§3).

---

## 2. The action dispatch chain ("set-piece state machines")

### 2.1 Objects and fields (OBS)

| Object | Where | Evidence |
|---|---|---|
| `match::AnimePlayer` (one per player/official) | ctor `FUN_1406e03d0`; vtables `0x1425e34d8` (primary), `0x1425e3508` (base at **+0xab8**, the clip-player interface), `0x1425e35b0` (+0xac0) | RTTI COL offsets 0 / 2744 / 2752 |
| `match::anime::ActionManager` | **AnimePlayer+0xc40**; ctor `FUN_1406e0540` → pool ctor `FUN_1406df630(+8)` + table init `FUN_1406d9040`; vtable `0x1425e34c8` | decompiles |
| action pool | ActionManager+0x8: one inline instance of every action class (pool offsets in §3) | `FUN_1406df630` |
| action table | ActionManager+**0x1528**: `Action*[94]`, one entry per `ANIME_*` id | `FUN_1406d9040` (zeroes 0x2f0 B, then fills) |
| `ANIME_*` id names | `char*[94]` at **0x1434a5470** (`ANIME_NOTHING` … `ANIME_ANIME_DATA_DRIVEN`) | raw pointers; 94 = table length |
| current action id | `u8` **AnimePlayer+0xac8**; sub-id `+0xac9`; previous `+0xaca/+0xacb` | `FUN_1406eebb0` |
| current action phase | `u8` **AnimePlayer+0x245c** (previous phase `+0x245d`); the interface getter `0x1406eade0` is `lea rax,[rcx+0x19a4]` (= ap+0xab8+0x19a4) | raw |
| current clip id | `u16` AnimePlayer+0xae0; set by interface slot 2 `0x1406ef320` (`cmp dx,[rcx+0x28]; mov [rcx+0x28],dx; mov byte[rcx+0x2ca6],1`) | raw |

### 2.2 Functions (OBS; each vcall slot re-checked in raw disassembly)

- **`ActionManager::GetAction(id)` = `0x1406d9010`**:
  `if (id < 0x5e && table[id]) return table[id]; return table[1];` (raw:
  `cmp edx,0x5e; … mov rax,[rcx+rax*8+0x1528]`). 87 call sites.
- **`AnimePlayer::ChangeAction(ap, newId, subId=0x5e)` = `0x1406eebb0`**:
  - calls the old action's **slot 5 (+0x28) = Exit(ap)** at `0x1406eecab`;
  - stores `+0xac8 = newId` (sub-id `+0xac9 = subId`, or `newId` if `subId == 0x5e`);
  - calls the new action's **slot 3 (+0x18) = Enter(ap)** at `0x1406eedb2`.
- **`AnimePlayer::TryChangeAction(ap, id)` = `0x1406eea90`**: calls the new action's **slot 2
  (+0x10) = CanEnter(ap) → bool**, then `ChangeAction` (raw `0x1406eeae7/…b1e/…b55`).
- **Per-tick `AnimePlayer` step `0x1406e5fc0`**: calls the current action's **slot 4 (+0x20) =
  Update(ap, ctx)** at `0x1406e60c6`.
- **Reset `0x1406ee6a0`**: calls slot 5 (Exit) on the current action, then sets all four id bytes.
- Other slots with a fixed call site (OBS for the call, INF med for the meaning):
  - slot 6 (+0x30) at `0x1406e4806` → bool;
  - slot 8 (+0x40) at `0x1406e2ea0`;
  - slot 10 (+0x50) at `0x1406f0c93` → bool; true means skip the default locomotion;
  - **slot 12 (+0x60)**: "set the kick request" (position, target, …), called with fixed ids by
    the six `0x1406b3xxx` wrappers listed in §2.3;
  - slot 15 (+0x78) at `0x1406f9b70`;
  - slot 18 (+0x90) at `0x1406f6ce0`;
  - slot 23 (+0xb8) at `0x1406edce0`.
- Base defaults (OBS):
  - `0x141794410` = `ret`;
  - `0x141863350` = `xor al,al; ret` (false);
  - `0x140ca1a50` = `mov al,1; ret` (true).
- The shared "loop" CanEnter `0x140782330` returns true iff the interface `+0x28` value (the
  sub-id) is 1.

### 2.3 Who drives the restart kicker (OBS) — `FUN_14092e7e0`

This is called from 9 sites in `0x140926690…0x140928fc0`, the match-side per-player step. For
the restarting player it reads `*(*(gCtx+0x270)+0x1554)`, the `SituationData.MODE`
(`pes-re-tactics-clock.md` §6). It then calls the kick-request wrapper on that player's
AnimePlayer (`player+0x7d8`):

| MODE | wrapper | action id | class |
|---|---|---|---|
| 2 THROW_IN | `0x1406b44f0` | 0x1a | ThrowinLoop |
| 3 GOAL_KICK | `0x1406b3f70` | 0x3e | GoalKickStandby |
| 4 CORNER_KICK | `0x1406b3db0` | 0x31 | CornerKickLoop |
| 5 FREE_KICK | `0x1406b3ee0` / `0x1406b3e40` | 0x1e / 0x22 | FreeKickLoop / FreeKick2ndLoop |
| 6 PENALTY | `0x1406b43e0` | 0x35 | PenaltyKickLoop |

Each wrapper is `GetAction(ap+0xc40, id)->slot12(ap, pos, dir, &req)` (raw: `0x1406b3e1e call
[r10+0x60]`). It is skipped while `*(…+0x116c) == 1`.

**So the "S0…Sn" restart machine is one level up, in the AnimePlayer.**
- The *action id* (`+0xac8`) selects the class: e.g. `CornerKickLoop` → `CornerKick` → `MOVE`.
- The class's own **phase byte** (`+0x245c`, values 0/1/2) sequences the clip steps inside one action.

---

## 3. Resolved method table (OBS) — every ANIME id

The full table is generated by `resolve_actions.py` (§6). 90 of 94 ids resolve. Ids 0
`NOTHING`, 55 `KP_MOVE`, 77 `FREE_TRAINING_BREAK` and 78 `PK_MATCH_NEXT_KEEPER` have a null
table entry, so `GetAction` falls back to id 1.

Set-piece and official rows (columns are vtable slots, VAs without the `0x`):

| id | ANIME_* | pool+ | class | vtable | CanEnter[2] | Update[4] | Exit[5] | [10] | SetKick[12] |
|---|---|---|---|---|---|---|---|---|---|
| 22 | KICKOFF_KICKER_LOOP | 0x238 | KickoffKickerLoop | 0x1425e1050 | 140782330 | 1407819f0 | ret | false | 1406deb60 |
| 23 | KICKOFF_RECEIVER_LOOP | 0x240 | KickoffReceiver | 0x1425e1220 | 140782330 | 140781fc0 | ret | false | 1406deb60 |
| 24 | KICKOFF_OTHER_LOOP | 0x250 | KickoffOther | 0x1425e1308 | 140782360 | 140781b30 | ret | 1407823d0 | 1406deb60 |
| 25 | KICKOFF | 0x258 | KickoffKicker | 0x1425e1138 | 140782310 | 140781780 | ret | false | 1406deb60 |
| 26 | THROWIN_LOOP | 0x260 | ThrowinLoop | 0x1425e1b30 | 140794fa0 | 140793590 | ret | 1407952d0 | 140794280 |
| 27/28 | THROWIN / _LONG | 0x280/0x288 | ThrowinNormal / ThrowinLong | 0x1425e1c18 / 0x1425e1d00 | 140794f20 | 140792e10 | ret | 1407951f0 | 1406deb60 |
| 29 | SEAMLESS_THROWIN | 0x290 | SeamlessThrowin | 0x1425e1de8 | 140794e30 | 140791ec0 | ret | 140795030 | 1406deb60 |
| 30 | FREEKICK_KICKER_LOOP | 0x2b0 | FreeKickLoop | 0x1425df908 | 140782330 | 14073e150 | ret | 140742d90 | 1407411f0 |
| 31–33, 37–39 | FREEKICK[2ND]_SHORTPASS/LONGPASS/SHOOT | 0x2c8–0x310 | FreeKick (6 instances) | 0x1425df9f0 | 140742030 | 14073dfc0 | ret | 140742c50 | 1406deb60 |
| 34 | FREEKICK2ND_KICKER_LOOP | 0x2e0 | FreeKick2ndLoop | 0x1425dfad8 | 140741fe0 | 14073de80 | ret | 140742bc0 | 1407410a0 |
| 35 | FREEKICK2ND_CEDE | 0x2f0 | FreeKick2ndCede | 0x1425dfbc0 | 140741f90 | 14073dfc0 | ret | 140742c50 | 1406deb60 |
| 36 | FREEKICK2ND_PASS_TO_KICKER | 0x2f8 | FreeKick2ndPassToKicker | 0x1425dfca8 | 140742000 | 14073dfc0 | ret | 140742c50 | 1406deb60 |
| 40 | QUICK_RESTART_READY | 0x318 | QuickRestartReady | 0x1425e0240 | 140742180 | 14073f750 | ret | 140742f00 | 1406deb60 |
| 41–43 | QUICK_RESTART_SHORT_PASS/LONGPASS/SHOOT | 0x328 | QuickRestartKick | 0x1425e0328 | 140742150 | 14073efb0 | ret | false | 1406deb60 |
| 44 | WALL_LOOP | 0x330 | WallLoop | 0x1425dfd90 | 1407421b0 | 140740810 | ret | 140743090 | 1406deb60 |
| 45–48 | WALL_JUMP / NOT_JUMP / RUN_OUT / NOT_RUN_OUT | 0x340–0x370 | WallJump / WallNotJump / WallRunOut / WallNotRunOut | 0x1425dfe80 / 0x1425dff70 / 0x1425e0060 / 0x1425e0150 | 1407421b0 | 1407400d0 | ret | 140743090 | 1406deb60 |
| 49 | CORNERKICK_KICKER_LOOP | 0x380 | CornerKickLoop | 0x1425df568 | 140782330 | **140732eb0** | ret | 1407343a0 | **1407331a0** |
| 50/51 | CORNERKICK / _SHORT | 0x388 | CornerKick | 0x1425df650 | 1407335c0 (`.impdata` thunk) | **140732e30** | ret | **1407835c0** | 1406deb60 |
| 52 | POSTMAN_LOOP | 0x390 | PostManLoop | 0x1425df738 | 140782330 | 140733000 | ret | 140734430 | 1407333e0 |
| 53 | PENALTYKICK_KICKER_LOOP | 0x398 | PenaltyKickLoop | 0x1425e14d8 | 140782330 | 140782770 | ret | 140742bc0 | 140782930 |
| 54 | PENALTYKICK | 0x3a0 | PenaltyKick | 0x1425e15c0 | 140782af0 | 1407826f0 | ret | 1407835c0 | 1406deb60 |
| 62 | KP_GOALKICK_KICKER_LOOP | 0x408 | GoalKickStandby | 0x1425e0bb0 | 140782330 | 1407748f0 | ret | 140775730 | 140774ae0 |
| 63 | KP_GOALKICK | 0x418 | GoalKickLongPass | 0x1425e0e78 | 140774c50 | 140774890 | ret | 1407756e0 | 1406deb60 |
| 64 | KP_GOALKICK_SHORT | 0x430 | GoalKickShortPass | 0x1425e0d88 | 140774c50 | 140774890 | ret | 1407756e0 | 1406deb60 |
| 65/66 | KP_PUNTKICK / KP_THROWPASS | 0x448/0x490 | goal_keeper::PuntKick / Throw | 0x1425e08b8 / 0x1425e0aa8 | 140749130 | 140748b20 | ret | 140749aa0 | 1406deb60 |
| 68 | KP_DROP_BALL | 0x4e8 | goal_keeper::DropBall | 0x1425e04f8 | 140744140 | 140743af0 | ret | 140744220 | 1406deb60 |
| 79 | DEMO_OUTOFPLAY | 0xdb8 | OutofPlay | 0x1425e2810 | 1407e5f10 | 1407e1c60 | 1407e1b10 | 1407e6b30 | 1406deb60 |
| 90 | LINESMAN | 0x568 | LinesmanAction | 0x1425e2270 | 1407a3160 | 1407a2920 | ret | false | 1406deb60 |
| 91 | LINESMAN_MOVE | 0x550 | LinesmanMove | 0x1425e2188 | 1407a3160 | 1407a1f30 | ret | false | 1406deb60 |
| 92 | JUDGE (referee) | 0x538 | Judge | 0x1425e20a0 | 1407a3160 | 1407a14e0 | ret | 1407a1dc0 | 1406deb60 |

Notes:
- **Enter (slot 3) is `ret` for every set-piece class.** The first-tick setup lives in Update,
  gated on the phase byte (§4).
- The `classinfo.json` "n" values used earlier as method counts (ThrowinLoop n=4, FreeKickLoop
  n=3, …) are base-class counts. For example, ThrowinLoop's bases are Throwin, Restart, Base and
  ThrowinLoopRetryWork.
- `*RetryWork` is a second base class with its own vtable at COL offset ≠ 0. It is not a runtime
  struct that replaces the vtable.

---

## 4. Proof: one decompiled state body — the corner kick (OBS, raw-verified)

### 4.1 `CornerKickLoop::Update` `0x140732eb0` (id 49, waiting kicker)

- If phase `*(ap+0x245c) != 1`:
  1. `side = FUN_1406e6660(ap, 0x34)`.
  2. Select the clip with key `{0x507, 9}` through `FUN_1406abb30(FUN_1406abba0(), …)`.
  3. Play it through interface slot 2.
  4. Set `ap+0x2472 = mirror` and flag `ap+0xad0 |= 0x1000000000` when mirrored.
  5. `FUN_1406f0f70(ap, clip, 0, mirror, 1.0, 0, 0)`.
  6. Set phase `= 1`.
- Then, if `*(*(gCtx+0x270)+0x116c) == 1`: `ChangeAction(ap, 2 /*MOVE*/, 0x5e)`.

`CornerKickLoop::SetKick` (slot 12) `0x1407331a0` does the following:
- picks the run-up clip `{0x509, 9}`;
- builds the request via `FUN_140732d40` with the float 315.0 or 45.0 (`0x1425aa1c0` / `0x14259be30`; INF: a direction in degrees), chosen by side;
- clamps the request position x/z (`param_3[0]`, `param_3[2]`) against a bounds record from `FUN_141e5d4c0(…, *(*(gCtx+0x2c0)+0x20))` (floats +0x40/+0x44/+0x50/+0x64/+0x70/+0x74/+0x80/+0x94; INF med: field/stadium bounds) with a 0.5 m margin (`0x142565600`);
- sets `ap+0x2460 |= 0x40000000` when it moved the spot.

### 4.2 `CornerKick::Update` `0x140732e30` (ids 50/51) — the 3-phase machine

Raw:
```
140732e52: call [rax+0x20]          ; phase ptr = ap+0x245c
140732e5b: movzx r8d, byte [rax]
140732e5f: sub r8d,1 ; je 0x140732e96 -> jmp 0x140733a30   (phase 1)
140732e65: cmp r8d,1 ; je 0x140732e7f -> jmp 0x1407335e0   (phase 2)
140732e7a: jmp 0x1407344f0                                  (phase 0 / other)
```

**Phase 0 — `0x1407344f0` (approach).**
- Clip key `{0x509, 9}`.
- For `CORNERKICK_SHORT` (sub-id `== 0x33`) and a ball-to-spot distance `< 25.0` m
  (`0x14259be20`), the key is `0x50a` instead.
- Play the clip; `FUN_1406f0f70(ap, clip, 0, mirror, 1.0, 0.2, 0)`.
- Set phase `0 → 1` (`+0x245d = old`, `+0x2471 = +0x2478 = 0`).
- If `ap+0x2460` bit 30 is set (the spot was clamped by SetKick): build a root-motion warp toward
  the spot (`FUN_1406e85b0`, `FUN_141eb3910(ap+0x428, …)`).

**Phase 1 — `0x140733a30` (run-up; wait for the strike window).**
The phase advances when

$$\text{curFrame} \ge \text{evtFrame} - \big\lfloor \text{GameSpeed}\cdot 14/60 + 0.5 \big\rfloor$$

- Raw: `140733a95 mulss [0x1425c48b8]=14.0`, `140733aa0 divss [0x14259a764]=60.0`,
  `140733aa8 addss [0x142565600]=0.5`, `cvttss2si`, `sub ebx,ecx`, `call [rdx+0x38]`
  (current frame), `cmp eax,ebx; jl → stay`.
- `evtFrame = FUN_1406af8e0(motionTbl, clip, 1.0, 0)` (INF high: the clip's event/contact frame).
- At GameSpeed 54 this is a **13-frame lead**.

On entry to the window, it chooses the strike clip:
- If sub-id `== 0x33` (short corner): `0x4e3` when the distance from the target point
  (`FUN_140a57800(req+0x870)`) to `(ap+0x2bf4, ap+0x2bfc)` is > 25 m. Otherwise `0x4e7 − (|ap+0x8| > F)`
  (`0x4e6/0x4e7`), where `F = **(gCtx+0x2aa8)`.
- Else, if `bVar2`: `0x4e4`. `bVar2` means one of:
  - `ap+0x2bd0 ≤ 0` and the target-point distance `< 10.0` (`0x142588210`);
  - `0 < ap+0x2bd0 < 0.15` (`0x1425c48a0`), `ap+0x2d0c > 0` and `ap+0x2d04 < 0.75` (`0x14259bdd4`).
- Else `0x4e1 + bVar3`. `bVar3` = curl toward the goal:
  - `+0x2d04 > 0.4` when mirrored (side == 1), `+0x2d04 < −0.4` otherwise (`0x14259b89c` / `0x1425c7670`);
  - and the angle diff `> 0`;
  - and `ap+0x2bd0 > 0.85` (`0x1425ab120`).

The motion-table chain then remaps it:
- `FUN_1406abd60` may flip mirror;
- `FUN_1406b3560` / `FUN_1406b3780` remap by player style;
- clip `0xad2` plays at rate 0.8 (`0x142572348`).

After the remap:
- It requests the kick through `FUN_1406ecbb0(ap, &req)` (the request block's floats include 3.0, 3.0, 9.0, 9.0, 7.0 and 4.0).
- On success it:
  - plays the clip;
  - stores the facing angle at `ap+0x247c`;
  - sets up a root-motion warp to the ball (`FUN_141eb39a0(ap+0x428, …, evtFrame+1, …)`) and a yaw warp (`FUN_141eb35a0(ap+0x4d0, …)`);
  - sets `ap+0x2460 |= 0x800000`;
  - sets phase `= 2`.
- If the request fails and the clip has ended: `ChangeAction(ap, 2 /*MOVE*/)`.

**Phase 2 — `0x1407335e0` (follow-through).**
- For a short corner (`0x33`) whose yaw warp has finished, it re-aims the yaw toward the target.
  A correction of more than 45° (`0x14259be30`) switches the target to the alternative from
  `FUN_140a76240`.
- Exit: at clip end (`FUN_1406af620`), or earlier once `curFrame >` the frame of the last
  kind-5 clip event (`FUN_1406afbc0(clip,5)` count, `FUN_1406af8e0` frame). For the short corner
  that point moves to `ev + (end − ev)·25/100` (`0x14259be20` / `0x14253ee30`). The action then
  calls `ChangeAction(ap, 2 /*MOVE*/, 0x5e)`.

**`CornerKick` slot 10 `0x1407835c0` (shared by `PenaltyKick`): post-strike lock window.**

```
14078364d call GetGameSpeed ; 140783652 mulss [0x142588210]=10.0
14078365a divss [0x14259a764]=60.0 ; 140783662 addss [0x142565600]=0.5 ; cvttss2si ecx
14078366e add ecx,ebx (ebx = FUN_1406b03d0(..) event frame) ; 140783670 cmp ebp(curFrame),ecx
```

- If `curFrame > evt + ⌊GameSpeed·10/60 + 0.5⌋` (**9 frames** at 54), it releases with
  `FUN_1406fa9f0(ap,1)` + `FUN_1406fb3b0(ap, ballPos)`. Otherwise the lock stays.
- It always copies the vec3 from `FUN_140a6cf70(*(gCtx+0x40), 1)` (INF high: ball position) into `ap+0x3760` and sets `ap+0x2460 |= 0x4000000`.
- Return value true (OBS).
- INF med: "release" is the hand-back of locomotion to the normal mover.

The game-speed scaling means that **every action timing is in 60 Hz frames converted to 54 Hz
logic frames** (`GameSpeed/60`). This is the same pattern the clock report found.

---

## 5. Clock / period holds — reachable and **not** registry-blocked (OBS)

The situation sequencer `FUN_14044C880` (`pes-re-tactics-clock.md` §5.2) is a plain `switch` with
direct calls; no virtual dispatch is involved. Its state list is a **program**:
- The list is `u32 step[30]` at `ctx+0x878` (reset to 0x48 by `FUN_140451fd0`).
- The step index is `u8 ctx+0x8f0`; it advances on a handler returning true.
- Steps are appended by `[ctx+0x878 + (u8)ctx[0x8f1]*4] = code; ctx[0x8f1]++`.
- 40+ builder functions (`0x14044b8a0`, `0x14044e720`, `0x14044f1c0…0x14044f780`, `0x1404500c0`,
  `0x1404505d0`, `0x140452260`, `0x1404524e0`, `0x1404526a0`, `0x140452950`, `0x140453890`, …)
  write the sequences.
  - Typical sequence: `0xf, 0x45, 0x16, 0x12, 0x13, 0x14, 0x15, 0xc, 0x19, 0x1a`.
  - The full immediate list is in scratch `seqwriters.txt` (§6).

Hold durations recovered:

| step code | handler | hold (OBS) | at 54 Hz |
|---|---|---|---|
| 2 | `FUN_14044cda0` | `++ctx[0x900]; done when (float)ctx[0x900] > GameSpeed·2.5` (`0x14259bde4` = 2.5) | done on the **136th** tick (> 135) |
| 12, 13 | `FUN_1404489d0(…, 0.5, 0)` | sub-step 0 waits **5** frames (`ctx[0x8f3]=5`), then starts the transition with duration 0.5 and counts down `ctx[0x8f3] = (u8)(GetFps()·0.5)` | 5 + 27 frames |
| 14 | `FUN_1404489d0(…, 1.0, 1)` | same, duration 1.0 | 5 + 54 frames |
| 15 | `FUN_140448c90(…, 6, 0.5, 1)` | effect kind 6, countdown `(u8)(GetFps()·0.5)`; fires msg `0x1050028` at the end | 27 frames |
| 16 | `FUN_140448c90(…, 2, 0.1, 0)` | kind 2, `0x142588200` = 0.1 | 5 frames |
| 17 | `FUN_140448c90(…, 4, 0.1, 1)` | kind 4, 0.1 | 5 frames |

- Both wait helpers end early when `FUN_140a3c020(situation)` is true (INF med: skip/abort
  pressed). `FUN_140448c90` also sets `ctx[0x27d8] = 1` when it finishes.
- INF (med): the `FUN_141f159f0()` singleton they drive (`FUN_141f15fc0(kind, dur, …)`,
  `FUN_141f15f00(dur, …)`) is the screen fade/transition manager. Steps 12–17 are therefore the
  fade-out/fade-in holds around kick-off, half-time and full-time.
- The *semantic* name of each step code (which one is "kick-off whistle", which one is "half-time
  screen") is **not** decoded here. Decoding it needs the 40 sequence builders mapped to
  `MATCH_PHASE`/`RestartKind`. This is the remaining part of tactics-clock §9 item 2, and it is now
  plain static work.

---

## 6. Ball touch / post / net collision — partial (OBS + INF)

1. **The "virtual collision dispatch" hypothesis is refuted** (`pes-re-ball-contact-2.md` §6.5
   INF). With vtables resolvable:
   - `collision::Object` (vtable `0x142596818`) has only 2 slots (`0x140418310` scalar deleting
     dtor, `0x141978300`).
   - `collision::Manager` (`0x142ad21a0`) and `collision::Human` (`0x142ad2368`) have only their
     destructor.

   No collision response is dispatched through them.
2. **`match::BallControl` (vtable `0x1425988b0`, 18 slots; second base at +0x10, vtable
   `0x142598948`).** BallControl+0x20 is the sim ball (0xD68 B, ctor `FUN_14086ca60`).
   - Slot 2 `0x140424bc0` is the per-tick update:
     - `FUN_140871da0(simBall)`;
     - `FUN_140870e90(simBall, *(gCtx+0x2c0)+0x5c, …)`, the integrator: calls `FUN_140876200` N times, then `FUN_140875550`;
     - `FUN_1408735e0`;
     - `FUN_140873ba0`;
     - then its own slot 8 (+0x40).
   - Slot 6 `0x1404258a0` (→ `0x140871e00`) reads the ball-touch record (`kind == 0xe`,
     `kind−1 < 3`) and forwards it to `FUN_140a88e00`.
   - Slot 8 `0x140425dd0` (→ `0x1408724a0`) copies position and delta into the BallInfo copy.
   - Slot 13 `0x140424ca0` (→ `0x14086f940`).
   - Slot 9 `0x140424c90` (large; not read).
3. The sim ball embeds:
   - `GoalPostInfoRef` (vtable `0x1425983c0`) at **simBall+0x990** (data ptr +0x9a0);
   - `FieldInfoRef` (`0x142608810`) at **+0x530** (data +0x540);
   - `BallInfoRef` at +0x4e0.

   Goal-frame dimensions are not image floats: 3.66 and 2.44 have zero `.rdata` hits. They come
   from `GoalPostInfo` at runtime.
4. `collision::Manager`'s per-frame worker `FUN_141b97880` calls `thunk_EXT_FUN_15b227450` for
   objects with `kind == 5 && type ∈ {0x17, 0x18}`. That is **protected `.impdata` code outside the
   dump**. INF (med): the frame/net contact response lives behind that thunk or in the
   fox physics (`fox::ph::CollisionCallbackInside`, vtable `0x142a59c20`, 6 slots) rather than in
   `match::`. **Status: still open.** The next step is either:
   - disassemble `0x15b227450` from the on-disk exe; or
   - find the `GoalPostInfo` data consumers (scan for `[reg+0x9a0]` loads off a `BallControl+0x20`
     pointer in `0x14086…0x14088`; none was found in the first pass, so the access goes through the
     Ref's accessor).

---

## 7. `pes-re-stamina-live.md` §8.15 (DataElement class registry)

- That section is no longer in the current file (it now starts at §9).
- `0x143736d38` holds a heap pointer (`0x1be0780` in this image; stale/heap, not static).
- The real Fox *entity* class registry exists and is a different mechanism:
  - `fox::impl::EntityClassDictionary` (vtable `0x142564b10`, ctor `0x140302ea0`);
  - `RegisterEntity::<Class>CreateFunc` objects for `match::SituationData`,
    `SituationBallData`, `SituationPlayerData`, `cobra::rd::GoalnetData`, … ;
  - `fox::DataElement` (`0x142554d00`).
- INF (high): that dictionary maps entity/data-class names to create functions and property
  tables for serialised Fox data (`.fox2`/DataSet). It is **not** a method-dispatch registry, and
  none of the three blockers needs it.

---

## 8. Reproduction recipe

```
cd ~/ghidra-projects/scratch/FoxRegistryRE
python3 rtti.py dump                       # -> rtti_all.json: 13,816 COLs, bases, vtables, slots (~2 s)
python3 rtti.py cls 'CornerKick'           # any class regex -> vtable + slot VAs
python3 resolve_actions.py > actions.md    # ANIME id -> class -> vtable -> CanEnter/Enter/Update/Exit/[10]/[12],
                                           #   flags slots that are .impdata thunks; also actions.json
python3 resolve_actions.py 'CORNER|Wall'   # filtered
# decompile any resolved slot (works for addresses Ghidra has no function for):
/opt/ghidra/support/analyzeHeadless $HOME/ghidra-projects/scratch/main PES21 -process pesmem.bin \
  -noanalysis -readOnly -scriptPath $PWD/ghk -postScript DecompAt.java $PWD/dec/out.txt 0x140732e30 0x140733a30
```

- Both scripts take an optional image path as the first argument (default `<scratch>/pesmem.bin`).
  They use only `capstone` and the image.
- `resolve_actions.py` reads these anchors from the image and contains no PES numeric tables:
  - the pool layout from the raw ctor `0x1406df630`, using a small register tracker that follows
    nested member ctors;
  - the table from `0x1406d9040`;
  - the names from `0x1434a5470`.
- Saved decompiles are in `dec/`: `ck_family.txt`, `sp_bodies.txt`, `coll.txt`, `job.txt`, plus
  one `.c` file per function.
- `seqwriters.txt`: every instruction that writes or reads the sequencer program
  `ctx+0x878[]`, grouped by function (the §5 builders), with the immediate step codes.

---

## 9. Ledger entries (for docs/re integration)

| ID | Claim | Conf. | Evidence |
|---|---|---|---|
| EV-DISP-0001 | "Fox class-info/interface records" = MSVC RTTI BCD/CHD; `n` = numContainedBases; last field = CHD RVA | Confirmed | raw COL/CHD/BCD bytes for CornerKick (§1) |
| EV-DISP-0002 | Every COL has a static vtable at `&COL`-qword + 8; methods resolvable statically | Confirmed | `rtti.py`: 13,816/13,816 |
| EV-DISP-0003 | ActionManager at AnimePlayer+0xc40; `Action*[94]` at +0x1528; `GetAction` 0x1406d9010; id = `ANIME_*` index (0x1434a5470) | Confirmed | raw `0x1406d9010`, `FUN_1406d9040`, 94-entry name table |
| EV-DISP-0004 | slot 2 CanEnter, 3 Enter, 4 Update, 5 Exit | Confirmed (call sites) / High (names) | raw vcalls at 0x1406eeae7, 0x1406eedb2, 0x1406e60c6, 0x1406eecab |
| EV-SP-0001 | Corner: phase byte ap+0x245c 0→1→2; strike window lead ⌊GameSpeed·14/60+0.5⌋; post-strike lock ⌊GameSpeed·10/60+0.5⌋ | Confirmed | raw 0x140733a95…abe, 0x14078364d…670 |
| EV-SP-0002 | Restart kicker dispatch by SituationData.MODE (+0x1554) → loop action 0x1a/0x3e/0x31/0x1e,0x22/0x35 via slot 12 | Confirmed | `FUN_14092e7e0` + wrappers 0x1406b3xxx |
| EV-CLK-0001 | Sequencer step 2 holds until tick > GameSpeed·2.5; steps 12–17 fade holds 5+⌊54·{0.5,1.0}⌋ / ⌊54·{0.5,0.1,0.1}⌋ frames | Confirmed (numbers) / Medium (fade meaning) | `FUN_14044cda0`, `FUN_1404489d0`, `FUN_140448c90` |
| EV-BALL-0001 | collision::Object/Manager/Human have no response vfuncs; post/net response not virtual-dispatched | Confirmed | vtables §6.1 |
| EV-BALL-0002 | frame/net response behind protected `.impdata` thunk 0x15b227450 or fox::ph | Tentative | `FUN_141b97880` |

**Docs to correct:** `pes-re-setpiece-referee.md` §1.8 ("n = method count", "interface-record
dispatch, no static function pointers") and §7 item 2; `pes-re-ball-contact-2.md` §6.5 INF
("dispatched through the collision system's virtual calls").
