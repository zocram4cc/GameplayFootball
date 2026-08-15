# Match animation: what GameplayFootball's selector reads, and what an imported clip has to say

PES 2021 ships 4,389 mocapped body animations; GameplayFootball ships 388
hand-authored ones and has almost nothing for tackling, keeper saves, or
tricks. Dropping PES clips into `data/media/animations` does not make them
play, though — GF does not have an animation *player* so much as an animation
*matcher*. Every frame a player needs a move, the engine builds a query out of
his current state and the thing he wants to do, sweeps the whole collection,
throws out everything that does not fit, sorts what is left, and takes the
first clip that can reach the ball. A clip whose metadata describes it wrongly
is either never picked or picked in situations it looks ridiculous in.

This document is the map of that matcher, written while importing PES
tackling and keeper animation. Companion documents:
[PES21_IMPORT.md](PES21_IMPORT.md) for the decode/retarget pipeline.

## The shape of a selection

`Humanoid::SelectAnim` (src/onthepitch/player/humanoid/humanoid.cpp) runs four
stages:

1. **Crude selection** — `AnimCollection::CrudeSelection` walks every loaded
   animation and applies hard filters. This is where most imported clips die.
2. **Direction narrowing** — `_KeepBestDirectionAnims` /
   `_KeepBestBodyDirectionAnims` for movement, ballcontrol, trap and
   interfere. Sliding, deflect, passes and shots skip this.
3. **Sorting** — a chain of `std::stable_sort`s, cheapest signal last, so the
   last sort dominates: priority, then idlelevel, then foot, then incoming
   body direction, then incoming velocity, then base-anim-ness, then
   catch-over-deflect.
4. **Reachability** — `GetBestCheatableAnimID` walks the sorted set and takes
   the first clip whose ball keyframe can be dragged onto the actual ball
   within an allowance ("smuggle"). Movement, trip and special skip this and
   just take the head of the list.

## Where the numbers come from

Only some of what the selector reads is written in the file. The rest it
computes from the curves, in `src/utils/animation.cpp`:

| Quantity | How the engine derives it |
|---|---|
| incoming velocity | length of the **first** `player` key interval, ×100, then bucketed: <1.8 → idle, <4.2 → 3.5 dribble, <6.0 → 5.0 walk, else 7.0 sprint |
| outgoing velocity | same over the **last** key interval |
| incoming movement | that first interval as a 2D vector |
| incoming body angle | `body` node quaternion at the first key → `Quaternion::GetAngles().Z` |
| outgoing angle | if outgoing velocity ≥1.8, the direction of the last root move; otherwise the `body` heading at the last key |
| outgoing body angle | `body` heading at the last key minus the outgoing angle (zero when the clip ends at rest) |
| quadrant | `AnimCollection::GetQuadrantID` snaps outgoing movement to one of 34 (velocity, angle) cells; a requeue is rejected if it lands in the same cell |
| touch frame / touch body part | `AddExtraTouches` takes the first `extension,football` key, poses the utility skeleton at that frame, and records the nearest body part |
| difficulty | `CalculateAnimDifficulty` from the velocity and angle changes |

`Animation::Load` also runs `ConvertToStartFacingForwardIfIdle`: any clip
whose incoming velocity buckets to idle is rotated so its body starts facing
straight ahead, taking the root track, the `body` curve, the ball keys and the
direction variables with it. Idle-start clips therefore cannot carry an
incoming body angle at all.

Everything is loaded twice — once as authored, once through
`Animation::Mirror`, which swaps every `left_*` node with its `right_*` and
negates the ball keys' X. Left and right versions come free.

The tool that reproduces all of the above outside the engine, and derives the
rest of the metadata from the same curves, is
`tools/pes21_import/anim_metrics.py`, with `test_anim_metrics.py` holding it
to the engine's own numbers and to GF's own hand-authored ball keyframes.

## Crude selection, filter by filter

Applied in order; the first failure drops the clip.

- **type** — `<type>` must match the requested function type exactly
  (`movement`, `ballcontrol`, `trap`, `shortpass`, `longpass`, `highpass`,
  `shot`, `deflect`, `catch`, `interfere`, `trip`, `sliding`, `special`).
  Long passes are queried as short passes.
- **incoming velocity** — never strict except for movement and ballcontrol.
  The rules that always hold: an idle clip is refused to a walking or
  sprinting player, and a moving clip is refused to an idle one. On top of
  that, tackling/keeper/trap/interfere queries set *force linearity*, which
  requires the clip's own incoming velocity to lie between the player's
  current velocity and the clip's outgoing velocity — a slide that decelerates
  from a sprint is only offered to someone sprinting.
- **outgoing velocity** — only filtered when the caller asks.
- **rotational side** — a clip is dropped if its turn would sweep the body
  through the opposite of the direction the player wants to look.
- **pickup** — with `onlyDeflectAnimsThatPickupBall`, only clips with an
  `<outgoing_retain_state>` survive. This is how a keeper is made to catch
  rather than parry.
- **last ditch** — `<lastditch>true</lastditch>` clips are hidden unless the
  player is in a last-ditch state.
- **incoming body direction** — non-idle clips may not start from a body angle
  further off centre than the player's own, and must be within 90° of it
  (strict: within ~11°). Idle-incoming clips only need the player to be within
  45° of straight ahead. Force linearity additionally requires the clip's own
  turn and the player's error to lie on opposite sides.
- **incoming ball direction** — for trap, interfere and deflect. The clip's
  `<incomingballdirection>` (a **movement** vector in clip space, so a ball
  arriving at a forward-facing player travels +Y) is compared with the ball's
  predicted travel, heights flattened to 40%. Allowed deviation is
  `<incomingballdirection_maxdeviation>` × π, defaulting to 0.25π (0.4π for
  deflects). **A trap/interfere/deflect clip with no
  `<incomingballdirection>` is a fatal error at selection time.**
- **outgoing ball direction** — when the command carries a kick power, the
  clip's `<balldirection>` must point within
  `<outgoingballdirection_maxdeviation>` × π (default 0.25π) of the intended
  one.
- **special / retain state** — the clip's `<incoming_special_state>` must
  equal whatever the *current* clip left behind in its
  `<outgoing_special_state>`, and likewise for the retain states. This is the
  mechanism that makes a player who has just slid play a stand-up next: the
  slide ends `lay_back`, and only a clip declaring `<incoming_special_state>
  lay_back` can follow. GF ships stand-ups for exactly two states, `lay_back`
  and `lay_front`, in `movement_special/idle/special/`.
- **specialvar1 / specialvar2** — numeric equality. Celebrations are selected
  entirely by this pair.
- **trip type** — `<triptype>` must equal the requested one.
- **forced foot** — `<forcedfoot>strong|weak</forcedfoot>` combined with
  `<touchfoot>` and the clip's mirrored-ness.

## Reachability: the ball keyframe

`GetBestCheatableAnimID` is where a clip earns its place. For each ball key in
the clip (`extension,football,<frame>,x,y,z`, in clip space with the origin at
the first root key):

- the ball's predicted position at `frame × 10 ms` is compared with the clip's
  ball key, moved by the root path the physics will actually take;
- the height difference must come under 0.22 m after a set of leniencies
  (higher balls are cheated more; deflects get an extra 20%);
- the horizontal difference is scored by `GetBodyBallDistanceAdvantage`
  against a per-type radius: **deflect ×1.8 with +0.4 m of free offset**,
  interfere ×1.4 +0.2 m, passes and shots ×1.3 +0.15 m, and **sliding ×0.2
  with no offset** — the engine deliberately prefers slides that do *not*
  touch the ball.

Two consequences for imported clips:

1. **Every non-movement clip must have a ball keyframe.** `AddExtraTouches`
   returns −1 when there is none and `GetBestCheatableAnimID` asserts on
   `touchframe` being inside the clip. A tackle or save without one is a
   crash in a debug build.
2. **The ball key has to be on the limb that hits it**, at the frame it hits.
   The engine drags the ball onto the key, so a key floating half a metre off
   the glove is a keeper punching air.

## What each family needs

| Family | `<type>` | Required | Notes |
|---|---|---|---|
| tackle, going to ground | `sliding` | ball key, `<balldirection>`, `<outgoing_special_state>` | `balldirection` is the direction the ball is kicked (×6 m/s, +6 up) at contact; the special state must match the pose the clip actually ends in |
| tackle, staying upright | `interfere` | ball key, `<incomingballdirection>` | no special state — declaring one drops a standing player to the grass |
| keeper save | `deflect` | ball key, `<incomingballdirection>` | `<outgoing_retain_state>right_elbow</outgoing_retain_state>` turns a parry into a catch and makes the clip eligible under `onlyDeflectAnimsThatPickupBall`; the sort prefers catches |
| dribble / trick | `ballcontrol` | ball key | filtered hard on direction and velocity, and requeued constantly — the riskiest family to import in bulk |
| celebration | `special` | `specialvar1`, `specialvar2` | no ball, no matching; the easy case |

## The root-motion scale (a correction to the import pipeline)

PES position tracks were being read as millimetres with the half-float
exponent rebased ×128, i.e. 1/128000 m per raw unit. Playing converted clips
back shows that cannot be right:

- a sprinting player's ankles never come within 15 cm of the pitch;
- a sliding tackle keeps its pelvis at standing height and swims forward at
  0.65 m/s;
- no clip in the whole 4,389 moves its pelvis more than 11 cm vertically,
  though hundreds of them end up lying on the ground.

For most of the pipeline that only mattered cosmetically. For match animation
it is fatal, because GF reads velocity off the root track and places every
ball contact by forward kinematics over it.

`tools/pes21_import/calibrate_pos_scale.py` measures the scale instead,
against three things true of any human animation: the foot a runner stands on
does not slide, the lowest an ankle ever gets is the bind pose's flat stance
(0.107 m), and a player lying on the pitch has his pelvis about 0.14 m up.
The three agree on a shallow optimum near 1/20000; the shipped constant is
`retarget.PES_POS_TO_M_GAMEPLAY = 1/20480`, which sits inside it. At that
scale the same sliding tackle covers 3.9 m at 10.6 m/s and drops its pelvis to
0.23 m, and a keeper's dive travels 2.2 m sideways with the hips at 0.31 m.

The legacy `retarget.PES_POS_TO_M` is left alone and still the default, so the
entrance/cutscene export is unaffected; `gani_to_anim.convert(pos_scale=…)`
and its `--gameplay-scale` flag select the calibrated one.

## Two conversions PES clips need before GF will have them

**Approach velocity.** A keeper's dive starts at 6 m/s sideways on frame zero
— PES blends into the launch, so the clip has no wind-up. Read literally by
`GetIncomingVelocity`, every dive claims to need a keeper already running, and
crude selection then refuses it to the standing keeper who needs it. The fix
is not to relabel the clip but to read it properly: nobody *approaches*
anything sideways at 6 m/s, so only the component along the body's own facing
counts as approach, and the perpendicular part is the action launching
(`anim_metrics.approach_velocity`). `condition_root` then eases the first 120
ms of the root track from that approach velocity into the clip's own, which
both fixes the reading and replaces an instantaneous glide with a push-off.
A sprinting slide, whose root motion *is* along its facing, keeps its sprint.

**Trimming.** PES ships one long take per action: approach, action, land,
stand up again — 96 frames for a slide against GF's 62. GF wants only the
action, because it has its own stand-ups keyed off `<outgoing_special_state>`
and because a clip that plays its own get-up holds the player hostage for a
second and a half. `trim_to_rest` cuts where the hips stop falling and start
coming back up.

## Deriving the metadata from the curves

Nothing below is hand-labelled; all of it is measured by `anim_metrics.py`.

- **lie state** — the direction the chest faces at the last frame, once the
  hips are below 0.55 m: up is `lay_back`, down is `lay_front`. Checked
  against every stock clip that declares an `<outgoing_special_state>`.
- **sliding contact** — a slide meets the ball on the way down, with the
  extended leg at full stretch and still off the grass, while the hips pass
  roughly 55% of standing height. All six of GF's own sliding clips key their
  ball within three frames of that crossing. Standing block tackles never get
  there and fall back to the furthest forward reach of a low foot. The ball
  goes one radius beyond the toe along the foot's sweep, at ball height, and
  that sweep direction becomes `<balldirection>`.
- **keeper contact** — the first crest of arm extension measured from the
  keeper's own hips. Two traps: a ready stance already has the arms half out,
  so the saving arm is the one that *gains* the most extension rather than the
  one that starts furthest out; and measuring from the take-off point instead
  of the hips lets a long dive's roll outscore the save. Hands within 0.45 m
  of each other catch the ball between them.
- **incoming ball direction** — reconstructed from where the clip catches the
  ball: a shot travelling from ~12 m in front of the goal to that point. A
  dive to the keeper's left (+X in clip space) necessarily meets a ball that
  was already heading +X.

Validation lives in `test_anim_metrics.py`. The load-bearing one is
`test_forward_kinematics_lands_on_the_stock_ball_keyframes`: posing GF's own
sliding and deflect clips at the frames their authors keyed the ball on, this
tool's FK puts the relevant limb a median of **0.12 m** from that ball (worst
0.37 m). If that ever drifts, every imported contact drifts with it.

## Replacing the stock set, a family at a time

Installing a clip is not the same as it being used. The selector's sorts are
stable and each later one outranks the earlier, so where a stock clip and an
imported one both fitted, the stock ordering decided and the mocap was
decoration. `CompareImportedPreference` sorts near the end -- after everything
except catch-over-parry -- so for the families named in `anim_prefer_imported`
(default `sliding,interfere,deflect`) the imported clip gets first refusal.

It is a preference, not a substitution, and that is the point: the stock clip
stays queued directly behind it, and `GetBestCheatableAnimID` walks the list
and takes the first that can actually reach the ball. A gap in the imported
coverage therefore falls back to the old animation rather than leaving a
defender with no tackle. A family joins the list once its imported clips cover
what the stock ones did; removing it from the config restores the old
behaviour exactly.

Clips are marked imported by `AnimCollection::Load` from the `pes`
subdirectory they install into, so nothing has to be written into the
generated files.

Families that are not ready ship **dark**: `AnimCollection::Load` skips any
path containing `experimental` unless `anim_experimental` is set, the same way
it has always skipped `luxury`. That is where the tricks and the locomotion
live -- they land in `ballcontrol` and `movement`, the types the possession
and locomotion games run through, and judging them needs play sessions rather
than a headless smoke.

## Imported so far

Converted with `--gameplay-scale`, analysed and installed by
`install_anims.py`; installed files are `pes_*.anim` and git-ignored.

| Batch | Source family | Installed | Where |
|---|---|---|---|
| tackling, to ground | `sliding_*`, `tacklefoot_*`, `tacklehold_*` | 26 | `sliding/<velocity>/pes/` |
| tackling, upright | same, the ones that stay on their feet | 70 | `interfere/<velocity>/pes/` |
| keeper saves | `gkdeflect*`, `gkcatch*`, `gkblock*`, `gkpunch*`, `gkscoop*` | 378 | `deflect/<velocity>/pes/` |
| dribbles & tricks (dark) | `feint_*`, `js_*` | 352 | `ballcontrol/<velocity>/experimental/` |

Rejected on purpose: keeper distribution (throws, kicks, drop-balls), shuffles
along the line, get-ups, set-up hops and clips that exist to be beaten — PES
keeps a keeper's whole repertoire in one family, and selected as deflects they
would have him throwing the ball at an incoming shot.

## Still to do

- **Play-test the dark families.** Run with `anim_experimental "true"` and
  watch whether players still keep the ball; then add `ballcontrol` (and later
  `movement`) to `anim_prefer_imported` a batch at a time.
- **Enriched movement** is converted but not yet installed in bulk; the
  `movement` install class exists and counts `<steps>` off the foot plants.
- **Inspect clips in the viewer.** `src/onthepitch/modelviewer.hpp` cycles the
  collection past an orbiting camera; `debug_model_viewer_anim` filters by
  clip path, so `sliding/sprint/pes` walks just the imported slides.
  `data/menu_smoke_animview_slide.config` does that. Note the engine opens a
  real window on the desktop display (`SDL_VIDEODRIVER=x11`, `DISPLAY=:0`) and
  the window manager places it wherever it likes, so a capture has to read the
  rectangle out of `xwininfo -root -tree | grep "Gameplay Football"` rather
  than assume the top-left corner.
