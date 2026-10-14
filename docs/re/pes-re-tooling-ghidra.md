# PES RE tooling — Ghidra 12.1.2 + GhidraMCP + analyzeHeadless

Status (2026-10-13, ~09:15 local): **both halves of the PES2021.exe toolchain work.** The GhidraMCP
HTTP backend on `127.0.0.1:8080` is live in the running GUI, and `analyzeHeadless -postScript`
compiles and runs scripts again. OBS = observed in this session; INFERENCE = derived, not observed.

---

## 1. Where the extension source lives

| Path | What it is |
|---|---|
| `~/ghidra-projects/scratch/GhidraMcpRebuild/src` | clean clone of `https://github.com/LaurieWired/GhidraMCP.git` at `main` (2.0.0, 3 commits past tag 1.4) — has the Gradle layout under `src/extension/` |
| `~/ghidra-projects/scratch/GhidraMcpRebuild/build14` | git worktree at **tag `1.4`**; this is the one that is built and deployed here |
| `~/ghidra-projects/scratch/GhidraMcpRebuild/backup-GhidraMCP-11.3.2/` | extension folder layout as found before any fix (Ghidra 11.3.2 build) |
| `~/tools/ghidramcp/bridge_mcp_ghidra.py` | the MCP server the harness actually mounts (`uv run … --ghidra-server http://127.0.0.1:8080/`) |

**Why tag 1.4 and not 2.0:** the deployed MCP bridge (`~/tools/ghidramcp/bridge_mcp_ghidra.py`,
1.4-era) speaks the 1.4 HTTP API — `GET /methods`, `GET /list_functions`, `POST /decompile`
(raw body), `POST /renameFunction`, `GET /searchFunctions`, `GET /disassemble_function`,
`GET /get_function_by_address`, `GET /strings`, `/xrefs_to`, `/xrefs_from`, … Upstream 2.0
renamed those endpoints (`/methods`→`/functions`, camelCase→snake_case, `POST /decompile`→
`GET /decompile?name=`), so shipping 2.0 would have broken every mounted `ghidra_*` MCP tool
mid-session for zero gain. 1.4 also keeps the tool names the whole RE agent fleet already uses.

## 2. The 12.1.2 incompatibility (root cause, two independent bugs)

### 2a. `CodeUnit.*_COMMENT` constants were removed in Ghidra 12

Ghidra 11 introduced the `CommentType` enum and **deleted** the `CodeUnit.EOL_COMMENT`,
`CodeUnit.PRE_COMMENT` and `CodeUnit.PLATE_COMMENT` integer constants. GhidraMCP 1.4 still used
them in five places, so the extension no longer compiled (and, where a 11.3.2-built jar was
already on disk, no longer loaded) against 12.1.2.

The fix is the 5-hunk patch in
`~/ghidra-projects/scratch/GhidraMcpRebuild/build14/src/main/java/com/lauriewired/GhidraMCPPlugin.java`
(local `git diff` there): `CodeUnit.EOL_COMMENT → CommentType.EOL`, `CodeUnit.PRE_COMMENT →
CommentType.PRE`, `CodeUnit.PLATE_COMMENT → CommentType.PLATE`, and
`setCommentAtAddress(..., int commentType, ...) → setCommentAtAddress(..., CommentType
commentType, ...)`. No behaviour change, no new dependency. OBS: `javap` of
`ghidra.program.model.listing.Program/Listing` confirms `getComment(CommentType, Address)` and
`setComment(Address, CommentType, String)` are the 12.1.2 signatures.

### 2b. Ghidra 12 only discovers an extension that is a **direct child** of an Extensions dir

This is the part that was actually stopping the plugin from ever appearing, both before and
after the source fix. Ghidra 12's `GhidraApplicationLayout.findGhidraModules()` walks
`findExtensionInstallationDirectories()` with a non-recursive `FileUtilities.forEachFile()`, so
the extension folder must sit *directly* inside an Extensions dir. OBS, from a GhidraScript
probe run against the running Ghidra:

```
Info  ProbeClass.java> LAYOUT ghidra.GhidraApplicationLayout
Info  ProbeClass.java> EXTDIRS [~/.config/ghidra/ghidra_12.1.2_DEV/Extensions,
                                /opt/ghidra/Ghidra/Extensions]
Info  ProbeClass.java> MODULE_COUNT 103          # extension installed at Extensions/Ghidra/GhidraMCP
Info  ProbeClass.java> GHIDRAMCP_MODULE 0
Info  ProbeClass.java> MODULE_COUNT 104          # extension installed at Extensions/GhidraMCP
Info  ProbeClass.java> GHIDRAMCP_MODULE 1
```

`/opt/ghidra/Extensions/Ghidra/GhidraMCP` (where a `Skeleton` sample sits, and where
`Skeleton` is *also* not discovered) is the **extension-archive** dir Ghidra unzips into, not a
dir it scans as an install root. The correct locations are:

* user: `~/.config/ghidra/ghidra_12.1.2_DEV/Extensions/GhidraMCP/`  ← **used here**
* install-level: `/opt/ghidra/Ghidra/Extensions/GhidraMCP/` (that dir does not exist yet)

Both are live in the running JVM's extension list after the move; the user dir is preferred
because it survives a Ghidra upgrade and needs no root.

### 2c. Headless postScript: one non-compiling `.java` broke the whole script directory

`Failed to get OSGi bundle containing script: <scratch>/ghk/scripts/DecompAt.java` — in Ghidra 12
`JavaScriptProvider` compiles **every `.java` in a `-scriptPath` directory into one OSGi
bundle**, so a single file that fails to compile makes the whole directory unusable and every
`-postScript` from it fails with that message.

The stale `.class` files are a red herring: OBS, adding `Decomp.class DecompAt.class FuncAt.class
Probe.class StrRefs.class XRefsAt.class` to a fresh directory still works fine.

OBS, bisecting `<scratch>/ghk/scripts` by copying files into a scratch dir one at a time:
```
ALL 15 java files                -> BROKEN
first half  (BytesAt Decomp Diag FindDisp FuncAt FuncsNear GetXrefs) -> BROKEN
second half (KitXrefs ListBlocks ListSyms Probe StrRefs Syms XRefsAt XRefsAt2) -> OK
BytesAt.java alone -> BROKEN      every other file alone -> OK
```
`BytesAt.java:13` called `getBytes(s, b)` — `GhidraScript.getBytes(Address, byte[])` does not
exist in 12.1.2 (OBS: `javap ghidra.app.script.GhidraScript` shows no `getBytes`). Fixed to
`currentProgram.getMemory().getBytes(s, b)` (OBS: `javap ghidra.program.model.mem.Memory` shows
`int getBytes(Address, byte[])`). `DecompAt.java` itself needed no change.

## 3. Exact commands now used

### Build + install the extension (reproducible)

```bash
~/ghidra-projects/scratch/GhidraMcpRebuild/build.sh            # user-level install (default)
~/ghidra-projects/scratch/GhidraMcpRebuild/build.sh --system    # /opt/ghidra/Ghidra/Extensions
```

which is:

```bash
cd ~/ghidra-projects/scratch/GhidraMcpRebuild/build14
GHIDRA_INSTALL_DIR=/opt/ghidra \
  ~/.gradle/wrapper/dists/gradle-8.9-bin/90cnw93cvbtalezasaz0blq0a/gradle-8.9/bin/gradle buildExtension
unzip -o -q ~/ghidra-projects/scratch/GhidraMcpRebuild/build14/dist/ghidra_*_GhidraMCP.zip \
  -d ~/.config/ghidra/ghidra_12.1.2_DEV/Extensions
```

Build time ~2 s clean. Output jar: `~/.config/ghidra/ghidra_12.1.2_DEV/Extensions/GhidraMCP/lib/GhidraMCP.jar`.
`extension.properties` in the install reads `version=12.1.2, ghidraVersion=12.1.2` (OBS).
Gradle 8.9 is present at `~/.gradle/wrapper/dists/gradle-8.9-bin/…/gradle-8.9/bin/gradle`
(8.9 satisfies Ghidra 12's `application.gradle.min=8.5`).

### Start / restart the GUI

```bash
GHIDRA_GUI_MAXMEM=4G /opt/ghidra/ghidraRun ~/ghidra-projects/PES21.gpr
```
OBS: the GUI service was restarted by me at 08:29 and again at 09:06 (old pids 3245896, now
3287245). The PES21 project reopened both times (`Opening project: ~/ghidra-projects/PES21`
then `Ghidra startup complete` in `~/.config/ghidra/ghidra_12.1.2_DEV/application.log`). The
CodeBrowser tool was opened for me by you; the tool config
`~/.config/ghidra/ghidra_12.1.2_DEV/tools/CodeBrowser.tcd` already contains
`<INCLUDE CLASS="com.lauriewired.GhidraMCPPlugin" />`, so any future CodeBrowser window starts
the backend automatically.

### Agent → Ghidra (GUI path, port 8080)

Use the MCP tools exactly as before — no change needed on the agent side. Raw HTTP equivalent:
```bash
curl -s -X POST --data-binary 'FUN_1419f1b00' http://127.0.0.1:8080/decompile
curl -s 'http://127.0.0.1:8080/decompile_function?address=0x1419f1b00'
curl -s 'http://127.0.0.1:8080/methods?limit=5'
```

### Agent → Ghidra (MCP tool-calling pattern — what good agents actually do)

Verified against a working agent's transcript (DefenderPort-3, 2026-10-14, while porting the
defender decisions). This is the pattern RE and port agents should copy, and the one to check
an agent against when its output looks thin.

1. **Decompile by name, not by address, when a report names the function.** The mounted tool
   `xd://mcp__ghidra_decompile_function` takes a symbol (`FUN_140a7b7a0`) and resolves it.
   Addresses are only needed when a report has no name or the body start differs from the label.
2. **Batch independent decompiles in one block.** Six calls issued together cost one round trip;
   the same six issued one per turn cost six. Dependencies still serialise — read the output
   before choosing the next function.
3. **Cross-check any branch with the disassembly.** Ghidra's C rendering has inverted conditions
   in this image (documented in `pes-re-defender-decisions.md` §12.2, where the previous agent
   shipped a flipped from-behind test). Confirming a `Jcc` on the raw instructions is cheap and
   is the only reliable way to get branch sense right.
4. **Read the port's own code before trusting any report.** The report said one thing, the code
   said another; the agent read both and decided from the code.
5. **Instrument the port to see which gate fires.** Add temporary probes around the candidate
   gate, run the smoke, read the counts. A hypothesis grounded in observed gate decisions is
   worth several inferred ones.
6. **Scratch files go on the mounted scratch partition, not `/tmp`** — `/tmp` is a RAM filesystem
   and is not the agreed location (see AGENTS.md: `gf-<name>-scratch`).

Misuse to watch for: decompiling one function per turn; reading disassembly for a function whose
C is unambiguous; writing "Ghidra says X" as evidence without the OBS tag; and treating a
decompile as final without a disassembly check when the result decides a branch.

### Agent → Ghidra (headless path, preferred per your instruction)

```bash
/opt/ghidra/support/analyzeHeadless $HOME/ghidra-projects/scratch/main PES21 \
  -process pesmem.bin -noanalysis -readOnly \
  -scriptPath <scratch>/ghk/scripts \
  -postScript DecompAt.java /abs/path/out.txt 0xADDR [0xADDR2-0xEND2 ...]
```
Scripts in `<scratch>/ghk/scripts` are the standard one-file `GhidraScript` utilities (DecompAt,
BytesAt, FuncAt, StrRefs, XRefsAt, Probe, GetXrefs, FuncsNear, ListBlocks, ListSyms, Syms, Diag,
FindDisp, KitXrefs). Output path must be absolute with no leading-dot path elements.
If a new headless failure says `Failed to get OSGi bundle`, check first that **every** `.java`
in that directory still compiles against 12.1.2's jars — that is what the error means.

## 4. Proof

### GUI load + port 8080 (`~/.config/ghidra/ghidra_12.1.2_DEV/application.log`, OBS)

```
2026-10-13 09:09:45 INFO  (GhidraMCPPlugin) GhidraMCPPlugin loading...
2026-10-13 09:09:45 INFO  (GhidraMCPPlugin) GhidraMCPPlugin loaded!
2026-10-13 09:09:45 INFO  (GhidraMCPPlugin) GhidraMCP HTTP server started on port 8080
```

### Decompile over 8080 (OBS)

```
$ curl -s -X POST --data-binary 'FUN_1419f1b00' http://127.0.0.1:8080/decompile
void FUN_1419f1b00(undefined8 param_1,longlong param_2,float *param_3,longlong param_4)
{
  float fVar1; float fVar2; float fVar3; undefined8 *puVar4; ...
```
(2352 bytes of real C, identical to the headless output for the same function.)

```
$ curl -s 'http://127.0.0.1:8080/decompile_function?address=0x1419f1b00'   # 2352 bytes, same output
$ curl -s 'http://127.0.0.1:8080/methods?limit=5'
FUN_140001000 / FUN_140001170 / FUN_140001480 / FUN_140001510 / FUN_1400015c0
$ curl -s 'http://127.0.0.1:8080/get_current_address'
140000000
$ curl -s 'http://127.0.0.1:8080/searchFunctions?query=1419f1b00&limit=3'
FUN_1419f1b00 @ 1419f1b00
$ curl -s 'http://127.0.0.1:8080/xrefs_to?address=0x1419f1b00&limit=3'
From 141a7733a in FUN_141a770f0 [UNCONDITIONAL_CALL]   (+2 more)
$ curl -s 'http://127.0.0.1:8080/disassemble_function?address=0x1419f1b00'
1419f1b00: TEST RDX,RDX
1419f1b03: JZ 0x1419f1be5
1419f1b09: MOV qword ptr [RSP + 0x8],RBX
$ curl -s 'http://127.0.0.1:8080/strings?limit=2'
14000004d: "!This program cannot be run in DOS mode.\r\r\n$"
1400001e0: ".trace"
```
`/` (and `/health`) return `404 Not Found` — the 1.4 plugin only registers the paths listed in
`bridge_mcp_ghidra.py`; that is expected, not an error.

### Headless postScript (OBS, after the `BytesAt.java` fix)

```
$ /opt/ghidra/support/analyzeHeadless $HOME/ghidra-projects/scratch/main PES21 \
    -process pesmem.bin -noanalysis -readOnly -scriptPath <scratch>/ghk/scripts \
    -postScript DecompAt.java <scratch>/htest/out7.txt 0x1419f1b00-0x1419f1be6 0x141a72250-0x141a72291
RC=0 ; SCRIPT ERROR count: 0
==== FUN_1419f1b00 @ 1419f1b00 body [[1419f1b00, 1419f1be5]]
==== FUN_141a72250 @ 141a72250 body [[141a72250, 141a72290]]      (94 lines total)

$ ... -postScript BytesAt.java <scratch>/htest/bytes.txt 0x1419f1b00
RC=0, no SCRIPT ERROR
== 0x1419f1b00 block ram(140000000 - 143d13fff)
   H........H.\$.WH..p.........W.........I...)|$PH.............A.(.
```
Before the fix the identical command produced:
`ERROR REPORT SCRIPT ERROR: DecompAt.java : The class could not be found ... Failed to get OSGi
bundle containing script: <scratch>/ghk/scripts/DecompAt.java`.

## 5. Known state / caveats

* The extension runs **inside a `PluginTool`**, so port 8080 is only up while a CodeBrowser (or
  any tool that includes the Developer package) has a program open. OBS: with the front-end
  alone and no tool, nothing listens. If 8080 is down, open `pesmem.bin` in CodeBrowser first.
* `bridge_mcp_ghidra.py` processes from earlier sessions are still running and were left alone
  as instructed: pids 1356691/1356700, 1957027/1957036, 3126429/3126438. Each will now reach a
  live backend; the 1.4 bridge takes no lock on 8080, so duplicates are harmless.
* The GUI pid <pid> that exited at 08:41 with SIGTERM was my restart (the extension install dir
  had to move). It is back up — pid <pid>, PES21 reopened — with a CodeBrowser loaded.
* Nothing under `<repo>/src`, `~/Code/GameplayFootball`, `godot/` was
  touched. Only `~/ghidra-projects/...`, `~/.config/ghidra/...` and `<scratch>/ghk` were modified.
* `~/tools/ghidramcp/bridge_mcp_ghidra.py` was not modified — the 1.4 extension matches it.
* Stale `.class` files left in `<scratch>/ghk/scripts` from a previous manual build are inert; they
  were deliberately left in place.
