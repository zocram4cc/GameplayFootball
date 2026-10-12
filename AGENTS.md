# Working agreements

## PES is the reference; the engine adapts to it

Where PES 2021 and this engine disagree about a format, a skeleton, a rig or a
convention, PES wins and the engine changes. Never shave the PES data to fit
the engine: a conversion described as "collapsing", "simplifying" or "good
enough for our rig" is the wrong direction.

## PES ships two skeletons

- **Anim skeleton**: `body_anim_skel.ask` (dt13). A strict T-pose with identity
  bind rotations. Every body gani is authored on it. Ganis apply verbatim.
- **Render skeleton**: `body.skl`, `hand_l.skl`, `hand_r.skl` (common_package).
  A relaxed pose with full bind matrices. Every mesh and skin weight is authored
  in it.

Meshes cross from render pose to anim pose exactly once, through the per-bone
world rotation `W` built in `src/rig/rig.cpp`. Never "calibrate" a bind offset
to fix a pose: a wrong pose means a wrong rest pose or mixed bind tables.

Health check: the viewer with no clip must show a perfect T-pose (arms dead
horizontal, palms down). Any droop or twist is a rig defect.

## Visual claims need an examined frame

"It renders", "the arm looks right" are claims about pixels. Capture with
`tools/capture_viewer.sh` (gamescope on the GPU; `godot --headless` gives no
pixels and llvmpipe is rejected), open the frame, look at it. A clean log is
not evidence.

## No PII, no PES bytes

- No personal data or machine paths in any tracked file. The PES install path
  arrives at runtime only (`PES21_ROOT` / `--pes21-root`), never as a default.
- No file extracted from PES, no name list mined from it, no number table
  transcribed from it. Everything PES-derived is read or built at runtime.
- `bash tools/check_pii.sh` runs as the `repo_pii_scan` test and must pass
  before every commit.

## No magic numbers

Every literal that carries meaning gets a named constant with units in the
name and its provenance in a comment (PES value, measured from what, or why).

## The suite is green

A failing test is fixed or deleted with a reason in the commit that noticed it.

## Agent output is reviewed, not trusted

Work produced by a subagent is accepted only after its full diff is read and
the step's verification commands are re-run by the reviewer.
