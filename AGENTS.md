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

## Strict TDD

No production code without a failing test first. Write the test, run it,
watch it fail for the expected reason (feature missing, not a typo), then
write the minimal code that passes, then refactor while green. A test written
after the code proves nothing: it was never seen to catch anything.

Tests that need a PES install read it from `PES21_ROOT` and `GTEST_SKIP` when
it is unset; they assert on real decoded data, never on mocks.

## Nothing is done until it is verifiably correct

A task is closed only when it is correct on every axis that applies:

- **functionally**: its tests and verification commands pass, re-run by
  whoever closes it;
- **conceptually**: it does what PES does, not a look-alike (two skeletons,
  per-side file values, shaders as Fox names them);
- **visually**: anything that draws has a captured frame that was opened and
  examined. Subjective qualities get some leeway; "it renders at all" does not.

Numeric checks are not visual proof. A rig that passes parity is still open
until the viewer shows the T-pose. Record what was verified, and what was
not, in the commit message.

## Plan facts are claims until checked

A fact written in a plan is a hypothesis about the install. Check it against
the data before building on it, and record the correction in the commit when
it is wrong (e.g. the CharacterAssets file list, the "full-body" test face,
GF's transcribed rig tables, which differ from the files by up to 1.9e-3 in W).

## Git identity

Commits use the owner's GitHub identity, passed per command with `git -c
user.name=... -c user.email=...`. Never run `git config`.
