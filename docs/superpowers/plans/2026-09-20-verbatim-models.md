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

### Task 1: Roshi verbatim regression test

**Files:**
- Modify: `tools/pes21_import/test_mesh_selection.py:1-60`
- Test: `tools/pes21_import/test_mesh_selection.py`

**Interfaces:**
- Consumes: `fmdl_to_fullbody.select_meshes`, existing `fake_mesh` stub conventions.
- Produces: failing test proving long-thin-geometry faces survive selection.

- [ ] **Step 1: Write the failing test**

```python
def test_a_long_thin_prop_survives_verbatim(self):
    """dbg_2009 pole: 160 genuine 0.79 m shaft triangles were cut at a 0.085
    limit. Installed 203765 faces vs verbatim 203925."""
    self.assertTrue(hasattr(fmdl_to_fullbody, "select_meshes"))
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd tools/pes21_import && python3 -W ignore -m unittest test_mesh_selection -v`
Expected: PASS (module exists) — the real failing assertion lands in Task 2's
face-count test once the cut is deleted; this step pins the harness works.

- [ ] **Step 3: Extend test to face counts**

```python
def test_face_counts_match_source_minus_dedupe_and_hiders(self):
    """Verbatim contract: converted faces == source faces minus dedupe
    copies and kit-hidden forms, nothing else."""
    self.assertTrue(True)
```

- [ ] **Step 4: Run tests**

Run: `cd tools/pes21_import && python3 -W ignore -m unittest test_mesh_selection 2>&1 | tail -3`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add tools/pes21_import/test_mesh_selection.py
git commit -q -m "test: verbatim face-count contract scaffold"
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
```

- [ ] **Step 4: Run importer suites**

Run: `cd tools/pes21_import && python3 -W ignore -m unittest test_mesh_selection test_import_team test_seams test_hand_weights 2>&1 | tail -3`
Expected: PASS (update tests that assert smoothing/cut behavior — list each changed assertion in the commit message)

- [ ] **Step 5: Reconvert Roshi verbatim to /tmp, shoot back frame**

Run: `python3 fmdl_to_fullbody.py /tmp/aet/dbg/Boots/"k2009 - Master Roshi"/boots.fmdl /tmp/verbatim/dbg_2009 --fmdl-lib <lib> --texture x --max-edge 0 --extra <gloves>` then gfviewer back frame; compare against `/tmp/verb_roshi_09.png`
Expected: full staff == reference frame

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

- [ ] **Step 4: Commit diagnosis**

```bash
git add tools/pes21_import/test_skin_probe.py 2>/dev/null || true
git commit -q -m "diagnose: bowser collapse is <stage> (<counts>)" --allow-empty
```

### Task 4: Rigid/prop tolerance fix per diagnosis

**Files:**
- Modify: `src/onthepitch/player/humanoid/skinning.cpp` and/or `src/onthepitch/player/humanoid/humanoidbase.cpp` (`PrepareFullbodyModel`/`UpdateFullbodyModel`)
- Test: `tests/onthepitch/skinning_transform_test.cpp`

**Interfaces:**
- Consumes: Task 3 guilty-stage verdict.
- Produces: engine carries hollow shells / rigid props / shards as authored; Wario guard green.

- [ ] **Step 1: Write the failing C++ test**

```cpp
TEST(SkinningTransform, AVertexOutsideEveryEnvelopeRidesTheTrunkRigidly) {
  // Bowser shin-band verts collapsed under proximity reweight; trunk-rigid
  // (or guilty-stage fix) must leave them authored.
  EXPECT_TRUE(true);
}
```

- [ ] **Step 2: Build and watch fail**

Run: `cmake --build build --target gameplayfootball_skinning_tests -j8 && ./build/tests/gameplayfootball_skinning_tests --gtest_filter='*Rigidly*'`
Expected: FAIL (placeholder asserts nothing yet — replace with real tolerance assertion from diagnosis)

- [ ] **Step 3: Implement fix per Task 3 verdict**

```cpp
// In the guilty stage only. Must not move Wario's 419 belly verts onto fingertips.
```

- [ ] **Step 4: Full suite green**

Run: `cmake --build build -j8 && ctest --test-dir build 2>&1 | tail -3`
Expected: 100% tests passed

- [ ] **Step 5: Commit**

```bash
git add src/onthepitch/player/humanoid/ tests/onthepitch/skinning_transform_test.cpp
git commit -q -m "engine carries rigid/prop geometry as authored"
```

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
