# PES 2021 code protection: characterisation, full live image for Ghidra, and the recipe (Unpacker)

Read-only RE. OBS = observed at the named address/bytes; INF(conf) = inferred. Addresses are VAs, image base 0x140000000.
Live process read only through `/proc/<pid>/mem` (sudo, O_RDONLY). Nothing in the repo was touched.
Scratch: `~/ghidra-projects/scratch/Unpacker/`.

## 1. What `.impdata` is

### 1.1 Identity (OBS)
- Section table (on disk and in the live header page): 19 sections with scrambled names. The real code is `.trace`
  (0x140001000, 0x252d800), `.rdata`, `.data`, `.pdata`; the protector owns `.impdata` (0x143d14000, vsize 0x17c3d94f
  = 380 MB, live rwx) and the small tail sections. Entry point 0x15b95f020 (in `.sbss`, rwx): a stub that touches one
  qword per 4 KB page from 0x140000000 to itself (pre-faults the image), sets a flag byte, then `jmp 0x15b95e000` →
  `jmp 0x1498f644c` (into `.impdata`).
- Protector strings: `"Denuvo Timing"` at 0x143e11658 (inside `.impdata`), RTTI `.?AVTaskAntiCheat@denuvo@@` at
  0x1436986d2, `"DODENUVO"` at 0x14e994aa6. No VMProtect/Themida/Arxan markers anywhere in the image.
  **INF(high): `.impdata` is the Denuvo Anti-Tamper section** (with the Denuvo anti-cheat task class in the game code).
- Import directory is intact (data dir 1 = 0x15b9625b8, 37 DLLs incl. kernel32 193 imports, steam_api64, d3d11);
  export directory (0x15b961bd0) exports 45 symbols (TinyXML `TiXmlString` + AMD `ags*`); exception directory
  (`.pdata`, data dir 3) = 158,648 RUNTIME_FUNCTION entries.

### 1.2 Live vs on-disk: no runtime decryption (OBS, `diff_disk.py`, mid-match, OPEN_PLAY)
Full live dump compared section-by-section with `PES2021.exe` (on-disk raw data):

| section | differing bytes | differing 4 KB pages |
|---|---|---|
| `.trace` | 0 / 38,983,680 | 0 / 9,518 |
| `.rdata` | 4,496 | 2 |
| `.data` (raw part) | 46,555 | 102 / 821 |
| `.pdata` | 0 | 0 |
| `.impdata` | **2,941 / 398,711,119** | 533 / 97,342 |
| `.tls$` | 5,057 | 2 |
| everything else | ≤1 | |

The 2,941 `.impdata` bytes form 554 runs, 382 of them exactly 4 bytes and 96 exactly 8 bytes, and every run inspected
sits **right after a `c3 cc` (ret; int3)** — i.e. state words embedded between protected functions (zero on disk,
non-zero live), not code. Example: 0x143d15f75 disk `00000000` → live `56a57ee8`. **So the executable code is
byte-identical on disk and in memory; nothing is decrypted per page at runtime.** This confirms the earlier
DuelsFoulsRE byte-identity finding for all of `.impdata`, not just a few functions. The live dump only matters for
`.data`'s zero-init tail (0x1436cf800..0x143ac5000, e.g. `gMatchContext` 0x1436f3dc8) and the state words.

### 1.3 How code reaches `.impdata` (OBS)
- `.pdata` was rewritten: **2,092** of its entries begin inside `.impdata` (e.g. 0x1440f35f0, 0x143fb8d40,
  0x144161790, 0x143f738a0), and the original `.trace` start of those functions is **not** a `.pdata` entry any more.
- The original `.trace` function start is now a 5-byte `jmp rel32` (`e9`) to the body: 2,273 such thunks
  (16-byte aligned `e9` preceded by `cc`/`c3`/`90`, target in `.impdata`), 2,273 distinct targets. Examples:
  `0x1409d75d0: e9 1bc07103 → 0x1440f35f0` (keeper deflect), `0x1407335c0 → 0x143fb8d40` (CornerKick slot 2),
  `0x1406deb20 → 0x143fb3990` (Base slot 22). Raw rel32 scan of `.trace` (not limited to function starts):
  9,831 `e9` and 11,541 `e8` whose target lands in `.impdata` (upper bound, includes byte-pattern false positives).
- 1,431 of the 2,273 thunk targets are `.pdata` entries (have unwind info); the other 842 are leaf functions with no
  unwind info or mutated bodies (next section).

### 1.4 Three kinds of protected body (OBS by disassembly + emulation; census `thunks.py` + `vmscan.py`)

| class | thunk targets | what it is | how to read it |
|---|---|---|---|
| **relocated, clean** | 1,022 with `.pdata` + 142 without; plus 823 `tiny` (≤12 insns) | plain MSVC output moved into `.impdata`, a few dead junk insns (`add edx,[rip+X]` after a call, `xchg [rip+…]`, `mov rbp,[rip+…]` into dead regs) and **XOR-obfuscated immediates** `mov r,[rip+X]; xor r,K` | Ghidra decompiles it directly (§3); `annot.py` folds the XOR immediates and labels float constants |
| **mutated native** | ~99 (e.g. 0x14415d330) | the same game logic, but every instruction is expanded into register/stack identities (`not/and/or/sub` of a reg with itself, `push X; add [rsp],-X`, `lea rsp,[rsp±8]`, `xchg [rsp],r`) and branches become **opaque returns**: `mov r,[rip+X]; cmovCC r,[rip+Y]; xchg [rsp],r; ret` (the two slots hold the taken / not-taken VAs; when X == Y the branch is unconditional). Some carry an **inline Denuvo integrity check** (0x14415d330's x<0 path reads the 201 code bytes at 0x1540fe270 8 at a time, 19,155 insns vs 176 for x≥0) | Ghidra produces garbage (`LOCK(); return 0xb02288c5;` for 0x14415d330). `emu.py` runs it; the junk only touches stack/regs and drops out of the log, leaving the real loads, float ops and stores (§3.3) |
| **Denuvo VM entry** | **183 of the 198 thunk targets that begin with `pushfq`** (187 total with a ≥16-target dispatch site) | entry saves flags/regs, reads `gs:[0x10]` (TEB StackLimit, OBS 0x143d36bbe) and enters a **shared bytecode VM**: dispatcher `jmp r9` at **0x15218fc1e** (`mov r9d,[rsi+r9*4]` at 0x15218fc08, table of 32-bit handler offsets at **≈0x144245200–0x14424bd00**, opcode decoded with a rolling key in r13/r11; 84–97 distinct handlers reached per run), a second dispatch site `jmp rcx` at 0x14dcaa237 (handler-inline copy), bytecode fetched forward 3–4 bytes at a time from **≈0x153311399…** (reads at 0x154626f07 / 0x1478dd267). A second VM instance: dispatcher `jmp r8` at **0x158472090**, table ≈0x15b61bb00 (0x143df7c10, 241 targets) | not decompilable. The same handler table and bytecode region are hit for 5/6 sampled entries with zero inputs, so **INF(med): these entries run Denuvo trigger/check bytecode** before (or instead of) the protected function; see §4 for what that means per open item |

Classifier caveat: `thunks.py`'s class is a heuristic on the first 80 instructions; `resolve.py` uses the same rules plus
`pushfq` = VM entry. `vmscan.py` (offline emulation, zeroed fake object) is the ground truth used for the 187/99 split
(`out/vmscan_all.txt`: VM = some indirect site reached ≥16 distinct targets).

### 1.5 The XOR-key constant pattern, generalised (OBS)
`mov reg, dword/qword [rip+d]` (or `movzx`, or the load folded into an ALU op) followed by `xor reg, K`; the slot
`[rip+d]` sits in `.impdata` next to the function and never changes at runtime (its page is byte-identical live/disk;
the only live-changing `.impdata` words are the §1.2 state words after `c3 cc`). Value = `u32(slot) ^ K`, then the
width of the use. In decompiles it shows as `_DAT_<slot> ^ 0xK`, e.g. `(byte)_DAT_1482b3dc4 ^ 0xd4` = **5**
(P_speed floor), `_DAT_14810f534 ^ 0x1c874803` = **7** and `_DAT_14810f530 ^ 0x28671b19` = **4** (free-kick referee
zone ids in 0x144161790). `annot.py` folds all of them; in raw disassembly DuelsFoulsRE's `ldis.py` does the same.
Marking `.impdata` non-writable in Ghidra (done by `SetupImage.java`) is not enough for the decompiler to fold them
itself (OBS: they still print as `_DAT_ ^ K`), hence the post-pass.

## 2. Full image for Ghidra (dump → PE → headless project)

All scripts in `~/ghidra-projects/scratch/Unpacker/`. Read-only toward the game.

| step | command | result |
|---|---|---|
| 1 dump | `sudo python3 dump_image.py pes_full.bin` | flat 0x1b97c000-byte file, offset = VA − 0x140000000, all 15 image mappings incl. all of `.impdata` (0 unreadable chunks, OBS); `pes_full.bin.maps` records pid + maps |
| 2 check | `python3 diff_disk.py pes_full.bin $PES21_ROOT/PES2021.exe [--pages .impdata]` | §1.2 table |
| 3 rebuild PE | `python3 build_pe.py pes_full.bin pes_live.exe` | same headers/directories, every section's raw data = live bytes of its whole page-rounded vsize (so `.data`'s zero-init tail with `gMatchContext` is present) |
| 4 import | `GHIDRA_HEADLESS_MAXMEM=6G /opt/ghidra/support/analyzeHeadless ~/ghidra-projects/scratch/Unpacker/proj PESLive -import ~/ghidra-projects/scratch/Unpacker/pes_live.exe -loader PeLoader -noanalysis -scriptPath ~/ghidra-projects/scratch/Unpacker/scripts -postScript SetupImage.java` | 342 s. PE loader: 19 blocks with real names/permissions, imports labelled. `SetupImage.java`: `.impdata` → r-x (not writable), disassemble + create functions at all **158,648 `.pdata` begins** → 158,723 functions (one CreateFunction error at 0x140477b63, thunk overlap) |
| 5 decompile | `./dec.sh out/x.c 0xADDR [0xADDR…]` then `python3 annot.py out/x.c` | `DecompAt.java` (copied from `<scratch>/ghk/scripts`, creates a function on demand), `-readOnly`, ~7–13 s per batch |

**New Ghidra project: `~/ghidra-projects/scratch/Unpacker/proj/PESLive.gpr`, program `pes_live.exe`**, image base
0x140000000, language x86:LE:64:default / windows. Same VAs as `pesmem.bin`, so every address in docs/re still holds.
Headless only (the GUI PES21 project and the GhidraMCP backend on :8080 are untouched; to browse it in the GUI open the
.gpr — do not open it in two Ghidra processes at once). Full auto-analysis was NOT run (443 MB; hours); xrefs exist
only from the `.pdata`-seeded disassembly. `.impdata` blocks are code; the protector's `.impdata` data words are
readable because the block is initialised.
Refreshing after a game update: repeat steps 1–4 (the protected layout is per-build).

## 3. Proof: previously unreachable functions now decompile

Files: `out/targets.c` / `out/targets.annot.c`, `out/batch2.c` / `out/batch2.annot.c` (raw / annotated decompiles),
`out/emu_ref_mode4*.txt` (emulation logs).

### 3.1 Keeper deflect — `ActionKeeperDeflect::exec` 0x1409d75d0 → **0x1440f35f0** (clean, decompiles; OBS)
CanEnter 0x1409d6c10 → 0x1409d6c20 → **0x1440ecf40** (double thunk; also decompiles, 398 lines). Exec, abridged:
```c
// p1 = action, p2 = request, p3 = keeper anime/ctx
save = p1[0x20]; p1[0x20] = 0;
if ((FUN_140518fe0() && FUN_140a65db0(*p2->kind & 0x7f) == 3) || save) {
  if (FUN_1409d5860(p2)) return FUN_1409d78e0(p1,p2,p3,&save);          // alternative path (decompiles too)
  if (p1[8] != 0) return 0;  p1[0x1c] = MI->frame(+0x1198);
  K = player(p2[4]);  P = K->pos(+0x55c..+0x564);  a = p3->angle(+0xe60);
  y = FIELD[3]*0.0;
  if (p3[0xe68] <= 0) { y += 2.4f;  x = P.x + S(K->heading(+0x554), 1);  az = K->heading + 90; r = 1; }
  else { d = FUN_1405bb170(&K->heading, a);
         if (|d| >= 120) a = (d < 0 ? FUN_140470d00(&K->heading,…,60) : FUN_140470d60())[0], r = 1;
         else { wrap a into [0,360); r = 2; }
         x = P.x + S(a, r);  az = a + 90; }
  z = P.z + S(az, r);       // S(θ,r) = FUN_140a92c80 = r·sin(θ°)  (emulated: S(0,2)=0, S(30,2)=1, S(90,2)=2)
  p1+0x10 = p3+0x8b0 = p3+0xe8c = (x, y, z);  p3[0xe6c] = 10; FUN_1406f0590(...); p3[0xfdc] = 1; p1[8]++;
}
```
So the deflect target is a point **1 m (or 2 m when the requested angle is within 120° of the heading) from the keeper
in direction θ, at height FIELD[3]·0 (+2.4 m when p3+0xe68 ≤ 0)** — a reading of the decompile, not live-verified
(INF med for the semantic names; the arithmetic is OBS in the decompile). Constants: 2.4 @0x1425c7660, 90 @0x14259be5c,
120 @0x14256ae30, 60 @0x14259a764, 360 @0x14259858c.

### 3.2 Post/net thunk — `collision::Manager` worker 0x141b97880 → 0x141b0e820 → **0x15b227450** (tiny; OBS)
```c
void FUN_15b227450(longlong obj, char on) {        // called with dl = 1 (mov dl,1 @0x141b9793e)
  if (obj->+8) { body = *(*(obj->+8)->+0x18)->+0x50;
                 if (on) body->flags(+0x1a0) |= 0x4000; else body->flags &= ~0x4000; } }
```
Call site (OBS disasm 0x141b9792d–0x141b97943): right after a new contact record is allocated
(`FUN_141b98150`), **only** when the collider has `kind(+0x6c) == 5` and `type(+0x68) ∈ {0x17, 0x18}`. It sets bit 14
of a physics-body flag word at creation. **It is not the post/net contact response** — that thunk is closed as a
flag toggle; the response must be in the fox physics callback or in code that tests that bit (INF med: bit 14 =
"report/handle contacts" for goal-frame/net colliders). Next: search the new project for `test …[+0x1a0],0x4000` /
`bt …,0xe` readers.

### 3.3 Referee set-piece positions (chief referee, `RefereeChief` vtable 0x1426420f0, slot 20 = 0x140a38c40)
Dispatcher 0x140a38c40 switches on `MI+0x1554` (restart mode) and calls `handler(this)`; each handler writes the
target to `this+0x950` (x) / `this+0x958` (z). `S(v)` = +1 if v ≥ 0 else −1; `MI+0x155c` / `+0x1564` = restart spot
x / z; `F = *(G+0x2aa8)` pitch floats (F[0] 52.56, F[5] 9.16, F[6] 36.14, F[7] 20.12).

- **MODE 3 goal kick** 0x140a38990 → **0x144161f30** (clean, decompiled, OBS):
  `x = −4.0·S(spot.x)`, `z = S(spot.z)·F[5] − 2.5` → (4.0, 6.66) / (4.0, −11.66) for F[5] = 9.16 — **matches both
  live samples** in pes-re-setpiece-choreography §8.
- **MODE 5 free kick** 0x140a384f0 → **0x144161790** (clean, decompiled, OBS). If `char(kicker+0x254)·spot.x ≤ 0`
  (FK in the kicking side's own half): |x| < F[6]·0.5 → x = −14·S(x); F[6]·0.5 ≤ |x| < F[6] → x = 2·S(x);
  then z = (F[5] + 2)·S(z); |x| ≥ F[6] → x = 3·S(x), z = (F[5] − 3)·S(z). Otherwise a zone id:
  |x| ≥ F[6] → (|z| < F[7]) + 1; F[6]/2 ≤ |x| < F[6] → |z| ≥ F[7] ? 3 : (|z| > F[7]/3 ? 5/6 by S(z·S(x)) : **4** = XOR
  immediate); |x| < F[6]/2 → |z| < F[7] ? 8/9 by S(z·S(x)) : **7** = XOR immediate. Writes (S = S(spot.x) unless noted):

  | zone | x | z |
  |---|---|---|
  | 1 | (F[6] − 4)·S | −(F[5] + 2)·S |
  | 2 | — (no write) | — |
  | 3 | F[0]·0.25·S | F[7]/3·S(spot.z) |
  | 4, 6 | (F[6] − 3)·S | −(F[5] + 3.5)·S |
  | 5 | (F[6] + 1.5)·S | −(F[7] + 1.5)·S |
  | 7 | spot.x + 2·S | spot.z − 10·S(spot.z) |
  | 8 | spot.x + 10·S | spot.z − 8·S |
  | 9 | spot.x + 5·S | spot.z − 15·S |

  Zone 3 for the live FK at (35.113, −30.729): (13.14, −6.71) — **matches the live sample**. Constants: −14
  @0x1425c7220, 1.5 @0x14259bddc, 3 @0x14258c264, 4 @0x1425656c0, 3.5 @0x14259bde8, 10 @0x142588210, 8 @0x14259bdf4,
  5 @0x14259222c, 15 @0x14259be0c, 0.25 @0x142592228. Only zone 3 is live-checked; the rest is OBS decompile.
- **MODE 4 corner** 0x140a38460 → **0x14415d330** (mutated: Ghidra output is garbage). Read by emulation (OBS,
  `emu.py`, both offline with a fake MI/FIELD and live with the real referee object 0x2e4ef280):
  ```
  spot.x = −51.78 → [this+0x950] = −35.64, [this+0x958] = +11.16   (19,155 insns: path with the integrity check)
  spot.x = +40.0  → [this+0x950] = +35.64, [this+0x958] = −11.16   (176 insns)
  effective ops:  x = (F[6] − 0.5 @0x142565600)·S(spot.x);  z = −(F[5] + 2.0 @0x142565604)·S(spot.x)
  ```
  i.e. **(S·(F[6] − 0.5), −S·(F[5] + 2.0)) with both signs from the spot's x** — this turns the setpiece report's
  "INF high from the arithmetic" into OBS. The report's live 35.63 vs the computed 35.64: INF(low) the referee had not
  quite reached the target when sampled; not checked.

### 3.4 Other named protected targets (OBS decompiles)
- `CornerKick` slot 2 0x1407335c0 → **0x143fb8d40**: `return (*(p2+0xab8))->vfunc[5]() == 0x31;`
- `Base` slot 22 0x1406deb20 → **0x143fb3990**: `return 0.3f;` (@0x142590d98)
- Judge P_speed **0x143fa8420**: identical to pes-re-duels-fouls §2.3 incl. the XOR floor 5 (folded by `annot.py`);
  level thresholds **0x143f71690** decompile too. These were previously only readable from hand dumps.
- 0x141939ed0 (`PrecomputeSkyDx11` slot, lighting report) → **0x15b012ab0**: `*out = (*(this+0x60) == 1 when +0x40==2) ? 0 : −1`.
- `ActionKeeperAfterCatchMove::exec` **0x1409e8880** is *not* a stub: it is ordinary `.trace` code that simply had
  no Ghidra function in `pesmem.bin`; it decompiles (`out/batch2.annot.c` lines 665–907).

## 4. Recipe: resolve any protected address to readable code or values

```
cd ~/ghidra-projects/scratch/Unpacker
python3 resolve.py 0xADDR            # follow e9 thunk chain, classify the body, print the next command
python3 resolve.py 0xADDR --dec      # clean/tiny: decompile from PESLive + annotate (floats, XOR immediates)
./dec.sh out/x.c 0xA 0xB …           # batch decompile (creates functions on demand, -readOnly)
python3 annot.py out/x.c             # fold `_DAT_x ^ K`, tag every _DAT_/DAT_ with f32|u32
venv/bin/python emu.py 0xBODY [--rcx …] [--poke ADDR=QWORD] [--pokef ADDR=FLOAT] [--depth N] [--full]
sudo venv/bin/python emu.py 0xBODY --live --rcx <live this> --poke <G+0x270>=<MI> --poke <G+0x2aa8>=<FIELD>
sudo python3 sample_ctx.py           # live G, last non-null MI (=*(G+0x270)), FIELD (=*(G+0x2aa8)), F[0..7]
sudo python3 findvt.py 0xVTABLE      # live heap objects of a class (vtable VA from FoxRegistryRE/rtti.py)
```

Decision rule:
1. **clean / tiny** (≈87 % of the 2,273 thunk bodies): Ghidra. Treat `func_0x…` callees as normal functions
   (`.trace` thunks that are not `.pdata` begins). Confirm quoted formulas against capstone as usual.
2. **mutated**: `emu.py`. Drive each branch with pokes (one run per sign/zone), read the `R/F/W` lines: the loads
   from `img` are the constants, the `heap` loads are the inputs, the final "non-stack writes" are the outputs.
   Without `--live` heap pages are zero; give the function a fake object by poking the fields it reads (the log
   tells you which: a `heap … = 0x0` read of `[base+off]` means "poke this"). `--live` needs root and reads
   heap pages from the game once (never writes); `G+0x270` is null ~92 % of the time, so poke the sampled MI.
3. **vm-entry** (`pushfq`): emulate with `--max` in the tens of millions; if the run returns, the non-stack writes
   are the function's effect; Ghidra cannot help. If it never leaves the dispatcher, fall back to live observation
   of the outputs.
4. Constants anywhere in protected code: `u32([rip+d]) ^ K` from the image (byte-identical live/disk).

Limits of `emu.py`: no Windows API (IAT calls and VM-decoded API pointers leave the image and stop/derail the run,
OBS §6); straddling accesses across two unmapped 64 KB heap chunks can still fault (image pages are pre-mapped, so
this only concerns heap; OBS fix for the image case); `rdtsc`/`cpuid` return unicorn's values, so a timing check may
branch differently than in the game (INF, not observed). Speed with per-instruction hooks ≈73 k insn/s (OBS: 3 M in 41 s).

## 5. Scripts (all in `~/ghidra-projects/scratch/Unpacker/`)

| file | purpose |
|---|---|
| `dump_image.py` | read-only live dump of 0x140000000–0x15b97c000 → `pes_full.bin` (+ `.maps`) |
| `diff_disk.py` | per-section / per-page live-vs-disk diff |
| `build_pe.py` | flat dump → loadable PE `pes_live.exe` |
| `scripts/SetupImage.java` | post-import: `.impdata` r-x, functions at all `.pdata` begins |
| `scripts/DecompAt.java`, `FuncAt.java`, `XRefsAt.java` | headless decompile / decompile-containing / xrefs |
| `dec.sh` | headless decompile wrapper for the PESLive project |
| `annot.py` | XOR-immediate folding + constant labels on decompiler output |
| `resolve.py` | thunk chain + class + next command |
| `thunks.py` | census → `out/thunks.tsv`, `out/mutated_targets.txt` |
| `emu.py` | unicorn emulation (offline or live-backed), effect log |
| `vmscan.py` | VM-vs-mutation probe over many bodies → `out/vmscan_all.txt` |
| `findvt.py`, `sample_ctx.py` | live object / context locators (read-only) |
| `venv/` | python venv with unicorn 2.1.4 + capstone (`--system-site-packages`) |

Disk: `pes_full.bin` 442 MB, `pes_live.exe` 442 MB, `proj/` 950 MB. Nothing under /tmp.

## 6. What the VM entries do (OBS emulation of 0x143d36980 = body of `.trace` 0x14013b3d0; fake TEB/PEB, zero heap)
- First instructions read `gs:[0x10]` (TEB StackLimit) twice (0x143d36bbe, 0x143d36bca).
- Then the shared VM runs (dispatcher 0x15218fc1e, handler table 0x144245xxx, bytecode 0x15331139x → 0x15331c071
  over ≈125 k insns), reads **KUSER_SHARED_DATA+0x330** (`0x7ffe0330`, the system cookie used by
  `RtlEncode/DecodePointer`) at 0x15825faa5, and ≈13 k insns later does `call rax` at **0x15b63cb86** to a computed
  address outside the image (0x6ffff93f7520 with our zero cookie). INF(high): it decodes an encoded OS-API pointer
  and calls Windows — i.e. this is **Denuvo runtime machinery (environment/licence/integrity trigger)**, not football
  logic. The 40 M-instruction run then executes zero pages; unicorn without Windows cannot get past this call.
- Zero argument accesses happened before the derailment (no read of the `rcx/rdx/r8/r9` fake objects).
- Consequence (INF med): for a `pushfq` body the football code, if any, sits after the trigger returns; it can be
  reached only by (a) emulating with a Windows layer (none installed), (b) finding the native continuation the VM
  returns to (bytecode exit handler not yet identified), or (c) observing outputs live. None of the named open items
  needed a VM body (§7).

## 7. Open items: status after this pass

| open item (doc) | protected address | status now |
|---|---|---|
| keeper deflect side/angle (goalkeeper §3.5, open 2) | 0x1409d75d0 → 0x1440f35f0; CanEnter 0x1409d6c10 → 0x1440ecf40 | **unblocked**: both clean, decompile (§3.1). Semantics of the deflect point given; live check still to do |
| keeper distribution choice (goalkeeper §5, open 3) | `AfterCatchMove::exec` 0x1409e8880 | **unblocked**: never protected; it was only missing as a Ghidra function. Decompiles (`out/batch2.annot.c`) |
| referee set-piece positions (setpiece-choreography §9 item 1) | 0x144161f30 (GK), 0x14415d330 (CK), 0x144161790 (FK) | **closed in this report**: GK + FK decompiled (live samples match), CK by emulation (§3.3) |
| post/net response thunk (fox-registry §6, EV-BALL-0002) | 0x141b0e820 → 0x15b227450 | **closed as not-the-response**: it sets bit 14 of a physics-body flag word for kind-5 type 0x17/0x18 colliders at contact-record creation (§3.2). Response still open; next lead = readers of `+0x1a0 & 0x4000` |
| Judge P_angle / P_speed (duels-fouls) | 0x143f738a0, 0x143fa8420, 0x143f71690, 0x143fa8760 | **now in Ghidra** (clean; P_speed and thresholds shown); no more hand dumps needed |
| `CornerKick` / `Base` slots into `.impdata` (fox-registry §1) | 0x143fb8d40, 0x143fb3990 | **unblocked**: CanEnter = `kickRequest->vfunc[5]() == 0x31`; Base slot 22 = `return 0.3f` |
| stamina A5 writer (stamina-live §10.6) | none found | **not unblocked by this pass**: a disassembly-validated scan of the whole live image (incl. all 380 MB of `.impdata`) for displacement 0xCB58 finds only the read at 0x14092b86d (OBS), so the writer is not a fixed-displacement store in protected code either. The arrays are also reached as a block: `lea r8,[rdi+0xc798]` at 0x14092b2a2 / 0x14092b619 / `lea rdx,…` 0x14092b767 with a 0xF0 stride over 5 arrays (OBS; these sites only reset arrays to 1.0). Next: in PESLive, search writes of `[reg+reg*4+0x3c0]` / `[reg+0x3c0]` off a `+0xc798` base, or the A5 page live-diffed against callers |
| camera settings / `cameraInplay.o` loader behind 0x15b0fb760 (camera-target) | 0x15b0fb760 | **unblocked, trivial**: tiny body `return *(this+0x10) + 8;` (OBS decompile, `out/cam.c`) — an accessor; the field table is whatever that pointer holds, so the next step is ordinary `.trace`/live work, not protection |
| `PrecomputeSkyDx11` slot (lighting) | 0x141939ed0 → 0x15b012ab0 | decompiles (§3.4); trivial getter |

Not done / limits: no Ghidra auto-analysis over the whole image; the VM bytecode semantics (handler meanings, exit
handler) were not decoded; `emu.py` cannot cross Windows API calls; only the zone-3 FK, the GK and the CK referee
formulas were checked against live samples.
