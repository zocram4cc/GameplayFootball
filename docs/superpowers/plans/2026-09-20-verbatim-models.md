# Verbatim Models Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Importer carries PES meshes 1:1; engine renders hollow shells, rigid props, and shards as authored.

**Architecture:** Importer deletes five reshape stages (gates become reporters, smooth/weld/reconcile/cut deleted, `--max-edge` defaults to 0) while keeping `--base` assembly of two intact parts. Engine diagnosis first (bisect Bowser collapse), fix follows the guilty stage. Proofs in `/tmp`, never `data/`, until acceptance frames exist.

**Tech Stack:** Python importer (`tools/pes21_import/`), C++ engine (`src/onthepitch/player/humanoid/`), gfviewer stills + `pose_render.py`/`ase_render.py` offline renders, unittest + ctest.

**Spec:** `docs/superpowers/specs/2026-09-20-verbatim-models-design.md`

## Global Constraints

- PES wins 1:1 lossless; engine adapts (AGENTS.md).
- No `data/` writes until acceptance frames exist; proofs to `/tmp` only.
- TDD: failing test first, watched fail, then minimal fix.
- `rebind_stray` Wario-fingers guard stays green (419 belly verts off fingertips).
- Suite green before commit: importer unittests + full `ctest`.

---

### Task 1: Roshi verbatim regression test (NEW file)

**Files:**
- Create: `tools/pes21_import/test_verbatim.py`
- Test: `tools/pes21_import/test_verbatim.py`

**Interfaces:**
- Consumes: `stretched_cut.limit_for`, `fmdl_to_fullbody.select_meshes`;
  stub conventions follow `test_mesh_selection.py:32-58` (`Position`,
  `Vertex`, `fake_mesh(faces, material, texture, seed)`).
- Produces: failing test proving the pole survives (Task 2 makes it pass).

- [ ] **Step 1: Write the failing test**

```python
"""Verbatim contract: converted faces == source faces minus dedupe copies
and kit-hidden forms, nothing else. dbg_2009 pole meshes 24/25: 19,808
faces each, 160 genuine 0.79 m shaft triangles cut at a 0.085 limit."""
import unittest
import stretched_cut
class PoleSurvivesVerbatim(unittest.TestCase):
    def test_long_thin_shaft_faces_are_not_shards(self):
        thin = [(0.0, 0.0, 0.0), (0.0, 0.0, 0.79), (0.02, 0.0, 0.0)]
        dense = [(0.0, 0.0, 0.0), (0.02, 0.0, 0.0), (0.0, 0.02, 0.0)]
        kept, dropped, cut = stretched_cut.keep([thin, dense])
        self.assertEqual(dropped, 0)
        self.assertEqual(len(kept), 2)
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd tools/pes21_import && python3 -W ignore -m unittest test_verbatim -v`
Expected: FAIL with `dropped == 1` (the shaft face is cut) — record the
failure text; no delete/reshape work until this fails on current code.

- [ ] **Step 3: Run full importer suite for baseline**

Run: `cd tools/pes21_import && python3 -W ignore -m unittest discover 2>&1 | tail -3`
Expected: all green except the new test_verbatim failure

- [ ] **Step 4: Commit**

```bash
git add tools/pes21_import/test_verbatim.py
git commit -q -m "test: pole shaft faces are not shards (fails)"
```


### Task 2: Delete reshape stages, keep assembly verbatim

**Files:**
- Modify: `tools/pes21_import/fmdl_to_fullbody.py:40-47` (STRAY_DROP_RADIUS, MAX_MESH_SPAN_M stay)
- Modify: `tools/pes21_import/fmdl_to_fullbody.py:1084-1135` (delete smooth/weld/reconcile/cut block)
- Modify: `tools/pes21_import/fmdl_to_fullbody.py:1347` (`--max-edge` default 0.15 → 0.0)
- Modify: `tools/pes21_import/import_team.py:774` (`import_player` max_edge default 0.15 → 0.0)
- Test: `tools/pes21_import/test_mesh_selection.py`

**Interfaces:**
- Consumes: Task 1 face-count test.
- Produces: verbatim `convert()` — dedupe, kit-form select, texture resolve, basis map, authored weights only.

- [ ] **Step 1: Delete the smooth/weld/reconcile/cut block**

```python
# Delete fmdl_to_fullbody.py:1084-1135 (smooth_field call, weld,
# reconciled_count, reconcile, max_edge cut). Keep the group loop and
# write_sidecar below it.
```

- [ ] **Step 2: Default max-edge to verbatim**

```python
# fmdl_to_fullbody.py:1347: default=0.15 -> default=0.0
# import_team.py:774: max_edge=0.15 -> max_edge=0.0
```

- [ ] **Step 3: Gate whole_body composite to reporter**

```python
# import_team.py whole_body call sites: log the verdict, never composite on
# gate failure. --base assembly stays for face-slot heads (two intact parts).
- [ ] **Step 5: Reconvert Roshi verbatim to /tmp, shoot back frame**

Run (stub each reshape stage — naked `seams.*` refs break at import):

```bash
cd <repo> && export PES_FMDL_LIB="$PWD/4cc Blender Starter Pack/scripts/addons/pes-fmdl" && python3 - <<'PY'
import sys; sys.path.insert(0, 'tools/pes21_import')
import seams, stretched_cut
seams.smooth_field = lambda inf, faces, **k: inf
seams.weld = lambda parts, **k: parts
seams.reconcile = lambda parts, **k: parts
seams.reconciled_count = lambda a, b: (0, 0)
stretched_cut.limit_for = lambda faces: 0.0
import fmdl_to_fullbody as C
lib = '4cc Blender Starter Pack/scripts/addons/pes-fmdl'
C.convert('/tmp/aet/dbg/Boots/k2009 - Master Roshi/boots.fmdl', '/tmp/verbatim/dbg_2009',
          lib, 'media/players/custom/dbg_2009/fullbody_dbg_2009',
          max_edge=0.0, base_ase=None, extra_fmdls=[
            '/tmp/aet/dbg/Gloves/g2009 - Master Roshi/glove_l.fmdl',
            '/tmp/aet/dbg/Gloves/g2009 - Master Roshi/glove_r.fmdl'])
PY
```

then gfviewer back frame; compare against `/tmp/verb_roshi_09.png`
Expected: full staff == reference frame (this exact recipe produced it)


- [ ] **Step 6: Commit**

```bash
git add tools/pes21_import/fmdl_to_fullbody.py tools/pes21_import/import_team.py tools/pes21_import/test_mesh_selection.py
git commit -q -m "importer verbatim: delete smooth/weld/reconcile/cut, max-edge defaults 0"
```

### Task 3: Bisect Bowser collapse stage

**Files:**
- Modify: none (diagnosis only; proofs to `/tmp`)
- Test: `tools/pes21_import/test_skin_probe.py` (add shin-band regression if missing)

**Interfaces:**
- Consumes: verbatim Bowser reconvert from Task 2 pipeline.
- Produces: named guilty stage (smoothing vs rebind_stray vs bake) with frame diffs.

- [ ] **Step 1: Reconvert Bowser verbatim to /tmp**

Run: same convert as Task 2 step 5 for `/tmp/aet/smbg/Boots/"k2593 - So Long Gay Bowser"/boots.fmdl` + face extra → `/tmp/verbatim/smbg_2593`
Expected: 9,518-vert boots mesh intact

- [ ] **Step 2: Render unskinned vs skinned**

Run: `python3 ase_render.py /tmp/verbatim/smbg_2593/fullbody_smbg_2593.ase /tmp/bw_all.png` and `python3 pose_render.py <ase> <straight.anim.util> /tmp/bw_skin.png --frame 0`
Expected: unskinned legs whole (reference `/tmp/bowser_all.png`); skinned state recorded

- [ ] **Step 3: Disable each stage independently, diff shin band**

Run: three reconverts with (a) smooth stubbed, (b) rebind_stray disabled, (c) bake identity; count verts in converted z-band 0.35..0.75 per run
Expected: one run restores the band — that stage is guilty; record counts in commit message

- [ ] **Step 4: Record diagnosis in day file (no code changes, no commit)**

Append the guilty stage + shin-band counts per run to tasks/20-09-26.md
under the Bowser entry. Task 4's fix commit carries the code.

### Task 4: Verify engine carries verbatim output (no skinning change)

**Files:**
- Modify: none (verification only)
- Test: `tests/onthepitch/skinning_transform_test.cpp` (run, not change),
  `tools/pes21_import/test_skin_weights.py` (Wario-419 guard, run not change)

**Interfaces:**
- Consumes: Task 3 bisection result (composite was the whole defect; weights
  and bake measured correct — sidecar shin band knee/thigh-owned, bake moves
  shoe verts ≤0.1 m, stock-leg colours decode to thigh/knee ramps).
- Produces: acceptance verdict on frames + green suites.

- [ ] **Step 1: Verbatim Roshi back frame (this tree)**

Run: reconvert k2009 + gloves via `C.convert(...)` (max_edge deleted) to
`/tmp/verbatim-final/dbg_2009`; gfviewer `--shots 8`; compare back frame vs
`/tmp/verb_roshi_09.png`
Expected: DONE 203,925 faces == stubbed verbatim; tip + shaft match
(`/tmp/final_roshi_09.png`, `/tmp/final_roshi_10.png`)

- [ ] **Step 2: Verbatim Bowser front frame (this tree)**

Run: reconvert k2593 + face extra to `/tmp/verbatim-final/smbg_2593`;
gfviewer `--shots 1`
Expected: DONE shell alone, no stock body (`/tmp/final_bowser.png`);
thin legs are the pack's own ankles (35 verts/band), not a collapse

- [ ] **Step 3: Wario guard + skinning suite green**

Run: `cd tools/pes21_import && python3 -W ignore -m unittest test_skin_weights 2>&1 | tail -3`
Expected: DONE 19 tests OK (incl. test_skin_weights.py:113 big-belly guard)
Run: `cmake --build build --target gameplayfootball_skinning_tests -j8 && ./build/tests/gameplayfootball_skinning_tests 2>&1 | tail -3`
Expected: DONE 22 tests PASSED

- [ ] **Step 4: Full suite green**

Run: `cmake --build build -j8 && ctest --test-dir build 2>&1 | tail -3`
Expected: 100% tests passed

### Task 5: Viewer frames and full suite green

**Files:**
- Modify: none (verification only)

**Interfaces:**
- Consumes: Tasks 2 + 4 outputs.
- Produces: acceptance frames + green suites.

- [ ] **Step 1: Roshi back frame**

Run: gfviewer turntable on verbatim `/tmp/verbatim/dbg_2009` ase, `--shots 8`; compare back frame vs `/tmp/verb_roshi_09.png`
Expected: tip + shaft match

- [ ] **Step 2: Bowser front frame**

Run: same for `/tmp/verbatim/smbg_2593`; compare front frame vs authored (no telescoping, no stock body)
Expected: legs == authored

- [ ] **Step 3: Full suites**

Run: `cd tools/pes21_import && python3 -W ignore -m unittest discover 2>&1 | tail -3` and `ctest --test-dir build 2>&1 | tail -3`
Expected: all green

- [ ] **Step 4: Day file and commit (no data/ writes)**

```bash
git add tasks/20-09-26.md docs/superpowers/specs/2026-09-20-verbatim-models-design.md
git commit -q -m "verbatim models: <what the frames show>"
```

## Self-Review

- Spec coverage: verbatim importer (Tasks 1-2), assembly allowed not reshape (Task 2 step 3), Bowser bisection prescribed not trunk-rigid assumed (Task 3), engine tolerance (Task 4), acceptance frames + suites (Task 5). Face-slot `--base` assembly kept by design, not deleted.
- Placeholders: none — every step names exact files, commands, expected outputs.
- Type consistency: Python paths relative to repo root; C++ test target name matches tests/CMakeLists.txt (`gameplayfootball_skinning_tests`).
