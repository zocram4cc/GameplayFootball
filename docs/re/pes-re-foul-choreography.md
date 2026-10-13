# PES 2021 foul / card / offside choreography (RE report, FoulChoreographyRE)

Tags: **OBS** = read in raw bytes / disassembly / decompile at the named address, or decoded from the
install's data file; **INF(conf)** = inferred. Addresses are VAs in `pesmem.bin` (base 0x140000000).
Scratch (never in the repo): `~/ghidra-projects/scratch/FoulChoreographyRE/` — `dec/*.c` decompiles,
`dis_*.txt` disassembly, `tbl.py` (table_foul.bin decoder, consumes 100 % of every stream),
`table_foul.json`, `atab.py` (Animetable row dumper), `fdc/` (78 foul fdcs, extracted at runtime).

`G` = `*(0x1436f3dc8)` (match context). `MI` = `*(G+0x270)` (match-info; the restart/foul block lives
at MI+0x1550..0x15c4, reset by `FUN_140a3c560`).

---

## 1. Foul / card scene selection (`FUN_140480910` → `FUN_14047f170`)

### 1.1 Who calls the builder (OBS)

The match-flow object (call it `Flow`; step array `Flow+0x878` int[], read index `Flow+0x8f0`, write index
`Flow+0x8f1`, sub-state `Flow+0x8f2`, frame counter `Flow+0x900`) runs one step per frame in
`FUN_14044c880` (switch on `Flow+0x878[Flow+0x8f0]`). Steps 3..0xb go to `FUN_140446390`:

| step | handler | role (OBS from the handler body) |
|---|---|---|
| 3 | `FUN_140447230` | whistle / stop step (always pushed first, see §4) |
| 4 | `FUN_140446b20` | goal-type stop (pushed when `MI+0x1574 == 3`) |
| **5** | **`FUN_140446960`** | **foul stop + foul/card scene** (`FUN_14047feb0(…, p5=0, p6=0)`) |
| 6 | `FUN_140446520` | card-only scene (`FUN_14047feb0(…, p5=1, p6=0)` → always `FOUL_CMN_CARD_Y_B`) |
| 7 | `FUN_140447290` | pushed when `MI+0x1554 == 7` |
| 9 | `FUN_1404466f0` | fatigue / injury scene (`p6=1`) |
| 0xb | `FUN_140447690` | stop without foul scene (pushed when `MI+0x1588 == 1`, see §5) |

`FUN_14047feb0` (wrapper) → `FUN_140480910(req, match, Flow+0x2790, Flow, p5, p6)`:
- p5≠0 → `FUN_14047ea10`: scene list `{0x20002}` (OBS: `local[rand(100)/100]` with a 1-entry array ⇒ always index 0).
- p6≠0 → `FUN_14047e060`: `0x2001f + (playerDB(+0x10 player)->byte+1 == 'R')` = `FOUL_INJURY_FATIGUE01` or `…_OUT`.
- else → `FUN_14047e940` → **`FUN_14047f170`** (the real selector, below).
- Result `0x20022` = **no scene** (sentinel: foul table has 33 scenes 0x20001..0x20021; `FUN_140480910` returns without
  queuing anything when the first id is 0x20022, OBS).
- If the scene is an EXCITE scene (`FUN_140a3fd20(id,0)`: 0x20015..0x20018) the per-match counter `req+0xd` is
  incremented (OBS) — it is what limits EXCITE to once (see §1.3).
- `req+0x8` = `gameSpeed*3 / speedRate` frames (3 s), or ×6 (6 s) for injury scenes 0x20019..0x20020 (`FUN_140a3fe70`)
  (OBS `FUN_14047feb0` @0x14047ff23..). `FUN_14102cad0(rec, sceneId)` and `FUN_140a607e0(rec, &player)` record the
  scene id and the subject player (fouler `MI+0x1590`, or `match+0x2470c` when `MI+0x1590>21` and `MI+0x1588==1`).
