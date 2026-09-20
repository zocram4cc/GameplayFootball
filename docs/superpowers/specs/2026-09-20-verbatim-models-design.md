# Verbatim models: importer keeps PES bytes, engine carries the rest

Status: approved by owner 2026-09-20 (approach A; C only if strictly necessary).
Spec for `writing-plans`. Evidence: `/tmp/verb_roshi_09.png`, `/tmp/verb_roshi_10.png`
(verbatim staff whole) vs `/tmp/roshi_close_09.png`, `/tmp/roshi_close_10.png`
(installed, cylinders only); `/tmp/bowser_close_05.png` (legs telescoped),
`/tmp/bowser_all.png` (unskinned, legs whole) vs `/tmp/bowser_skinned.png`
(skinned, legs collapsed); `/tmp/pole_back.png`, `/tmp/pole_straight.png`,
`/tmp/pole_base.png`, `/tmp/pole_pose.png`.

## Problem

Two owner-reported defects with one root cause class: the importer reshapes
PES geometry to fit the engine instead of carrying it 1:1 (AGENTS.md:
PES wins, engine adapts).

1. **dbg power pole**: source meshes 24/25 carry 19,808 faces each.
   `stretched_cut.limit_for` gave 0.085 on that mesh; 160 genuine shaft
   triangles run to 0.79 m (tip 0.23,1.18 joining hand −0.34,1.73) and were
   cut. Installed 203,765 faces vs verbatim 203,925 — exactly the logged
   drop count. Verbatim reconvert (`--max-edge 0`, smooth/weld/reconcile
   stubbed, `/tmp/verbatim/dbg_2009`, `data/` untouched) renders the full
   staff: red tip over the shoulder, patterned shaft down the back.
2. **Bowser legs**: authored boots mesh0 and converted mesh10 agree
   vertex-for-vertex in the shin band (0.2:833, 0.3:988, 0.4:35, 0.5:45,
   0.6:29, 0.7:35, 0.8:390 — 9,518 verts both). Geometry kept; the
   weight/bake folded the legs (unskinned whole, skinned collapsed). The
   installed model also composites the stock body underneath (import block:
   `ships no body of its own; composited`, base parts dropped,
   `face_bsm_alp NOT FOUND`), which is the grey "placeholder body" look.

Killed theories: smoothing mangled the pole (`smooth_field` touches
weights only, never deletes faces); textures missing (powerpole art is
opaque, present, converted); HDG bodies lost (2402/2411 render whole;
owner cleared after re-render).

## Design

**Importer becomes verbatim.** `tools/pes21_import/fmdl_to_fullbody.py`
keeps, per source mesh: dedupe of byte-identical copies, kit-form select
(transparent-hider rule stays — it selects which form, never reshapes one),
texture resolve, basis `(x,y,z)→(x,−z,y)`, authored weights carried through
`build_bone_map` untouched. Deleted stages: `whole_body` gate driving a
`fullbody_pes.ase` composite *as a gate consequence*, `seams.smooth_field` /
`seams.weld` / `seams.reconcile`, `stretched_cut` limit + `max_edge` cut.
`--max-edge` flag stays as an explicit operator override (default 0,
verbatim); nonzero is never the default path.
**Assembly vs reshape (binding constraint).**
Head-only face-slot models (HDG's 22 XXX heads, SMBG's 5) are partial BY
AUTHORSHIP — their body is PES's own. Merging two intact parts (imported
head meshes + stock skinned body, each unreshaped, `--base` assembly with
`--drop-base-parts` for the replaced head) is allowed verbatim assembly,
not reshaping: neither part's verts, weights, or faces are altered to fit
the other. The Bowser-class composite (stock torso UNDER an authored shell
because a gate failed) is forbidden — that hides authored geometry behind
judgement, exactly the defect. `whole_body`/`body_coverage` survive only as
reporters (log what is partial), never as gates that trigger reshaping.

**Bowser diagnosis (prescribed, trunk-rigid not assumed).**
Stage isolated (unskinned whole / skinned collapsed) but mechanism open:
smoothing (`smooth_field` 8 passes) vs `rebind_stray` (fingertip /
`STRAY_RATIO` proximity reweight) vs the authoring→bind bake. Diagnose by
bisection on Bowser: reconvert with each stage independently disabled, diff
the skinned shin-band frame each time. The fix follows the guilty stage;
trunk-rigid is one candidate, not the prescription. `rebind_stray`'s
Wario-fingers guard stays green throughout — whatever moves must not move
Wario's 419 belly verts back onto fingertips.

**Engine learns to carry PES geometry.**
`src/onthepitch/player/humanoid/` (`skinning.*`, `humanoidbase.cpp`
`PrepareFullbodyModel`/`UpdateFullbodyModel`): rigid/prop tolerance for
geometry outside every bone envelope — trunk-rigid binding, never
reweighted by proximity (the Wario-fingers regression,
`STRAY_RATIO`/`rebind_stray`, is the guard to keep green). Hollow shells
(Bowser), rigid props (staff), shards render as authored rather than
deleted. No importer-side thresholds reintroduced through the back door.

## Acceptance

- Roshi: back-frame staff == `/tmp/verb_roshi_09.png` (tip + shaft), face
  counts match source minus dedupe/hiders only, per-pack verbatim test.
- Bowser: front-frame legs == authored (no telescoping), no stock body
  underneath, back-frame shell intact.
- Full suite green (`ctest`, importer unittests incl. new verbatim-count
  tests); proofs in `/tmp` until frames sign off, then one pack
  end-to-end (dbg, smallest closed loop) before the other six.
- Non-goals: no threshold retuning, no new importer heuristics, no
  `data/` writes until acceptance frames exist.
