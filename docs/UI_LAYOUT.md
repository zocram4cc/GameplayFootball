# In-match UI layout notes

How the broadcast-presentation widgets are laid out, and the two Gui2
behaviours that dictate most of it. Read this before moving anything on
screen — both traps below cost a full debug cycle to rediscover.

## Trap 1: percentages are anisotropic

`Gui2View` positions and sizes are percentages of the **window**, with `x`
against its width and `y` against its height. A widget given `width 20,
height 20` is therefore not square, and its shape changes with the window's
aspect ratio.

Anything that has to keep a real-world shape — a card with rounded corners,
a portrait pitch schematic, a crest — has to be derived from the aspect
ratio via `Gui2WindowManager::GetWidthPercentForHeight()` /
`GetHeightPercentForWidth()` rather than given a fixed percentage. The
formation panel does this once, up front, in
`FormationGraphicLayout::ComputePanelGeometry()`, and every child box is
measured off the result.

Corollary for artwork: the PNGs under `data/media/ui/pes/` are baked at
exactly the pixel proportions the widget lays out at (see
`PANEL_PX` / `HEADER_PX` in `tools/pes21_import/export_formation_theme.py`,
and `kPanelPixelAspect` in `formationgraphiclayout.hpp`). Change one and
change the other, or `Gui2Image`'s scale-to-fit distorts the rounded ends.

## Trap 2: z-priority is reset every frame

`Gui2Task::ProcessPhase()` calls `SetRecursiveZPriority(0)` on the whole
tree **every frame**, before anything else runs. A `SetZPriority()` call
made while building a widget therefore survives exactly until the next
frame, after which draw order falls back to insertion order (`Scene2D`
stable-sorts on the flattened priority).

That is why the formation panel's team tag used to render *underneath* its
own header plate: the caption was created in `Init()` and the plate on the
first `Process()`, so the plate was inserted later and drew on top.

A widget that needs its own stacking order overrides
`Gui2View::SetRecursiveZPriority`, calls the base implementation, then
re-applies its own order — see `Gui2FormationGraphic::ApplyZOrder()`,
`Gui2StatsOverlay::ApplyZOrder()`, `Gui2Banner::ApplyZOrder()`. Setting
priorities at construction time alone does not work.

## Trap 3: image alpha is destructive

`Surface::SetAlpha` does not set an alpha *level*; `sdl_setsurfacealpha`
**multiplies** it into every pixel's alpha byte:

    p[3] = p[3] * alpha

So taking an image down to alpha 0 erases its transparency permanently — a
later `SetAlpha(1.0)` multiplies zero by one and the image never comes back.
It also flattens a PNG's own per-pixel alpha (rounded corners, the jersey
silhouette's cut-out) the first time it is faded at all.

That is why the formation panel's artwork and jersey icons went missing the
moment the presentation timeline started fading the panel in and out, while
its captions carried on drawing perfectly.

**Images are shown and hidden (`Show()` / `Hide()`), never faded.** Captions
cross-fade normally, because `SetTransparency` re-renders the text from
scratch each time. See `Gui2FormationGraphic::ApplyAlpha` and
`Gui2Banner::ApplySlotAlpha`.

A related consequence: an image created after the scene has started
rendering never reaches the screen, whatever its position, size and
visibility. Both together mean a widget must create every image it will ever
need up front and only re-point them afterwards — which is what
`Gui2FormationGraphic::BuildImages` does, with `FillForTeam` moving them.

## Text never clips — it overflows

`Gui2Caption::Redraw()` renders the text at whatever width it needs and, if
that is wider than the box it was given, **resizes itself** past the box
(mutating its own `width_percent`). Nothing clips, so a long name simply
runs over its neighbours. Squad names come from a database and can be
arbitrarily long, so every caption that displays one must be fitted:

    FitAndCentreCaption(caption, centreX, maxWidth, naturalHeight, minHeight);
    FitAndLeftAlignCaption(caption, leftX, maxWidth, naturalHeight, minHeight);

(`src/menu/ingame/captionfit.hpp`.) These shrink the type first and only cut
the text once shrinking hits the legibility floor, marking the cut with a
trailing `.`. The arithmetic is pure and unit-tested as
`FormationGraphicLayout::FitTextHeight` / `TruncateToFit`.

Also note `SetCaption()` upper-cases whatever it is given, so measure and
truncate `GetCaption()`, not the original string.

## Screen regions

| Region | Occupant |
|---|---|
| Top left | Scoreboard: league crest, clock pill, `[crest] TAG 0-0 TAG [crest]` bar, added-time chip (PES theme — spec §4) |
| Below the scoreboard | "Instant replay" header, while a replay is running |
| Centre | Pre-match formation panel; stats overlay card (TAB) |
| Lower third, centre | Banner slots (left/centre/right — see `bannerpresentation.hpp`) |
| Bottom left | Version caption |
| Bottom right | Radar |

The lower third's middle is deliberately kept clear of persistent chrome so
banners and the formation panel have somewhere to live: that is why the
radar sits bottom right rather than bottom centre.

## Judging layout headlessly

Both centred cards are gated on a debug config key so a headless capture can
hold them on screen instead of having to land a keypress or race a
pre-match window that is over in seconds:

    "debug_formation_graphic_always" "true"
    "debug_stats_overlay_always"     "true"

Capture recipe: `unset WAYLAND_DISPLAY; export SDL_VIDEODRIVER=x11` against
an `Xvfb` display, then `ffmpeg -f x11grab`. Wait on the
"Gameplay page reached" log line rather than on a timer — a cold stadium
load is minutes, not seconds.

## Formation panel

`Gui2FormationGraphic` draws one team at a time during the entrance
(`ComputeDisplayState` picks which, and its cross-fade alpha).

The XI is **not** drawn at its raw tactical coordinates. `ArrangeFormation`
clusters the outfield players into lines by depth, spaces those lines evenly
from the back to the forward line, and spreads each line across a width
scaled by how wide it actually plays — never tighter than
`kMinIconGapPercent`. That minimum is what the jersey icon and the nickname
width are then derived from, so no two icons or names can collide whatever
shape the formation is. The keeper is excluded from the clustering (his
depth is an outlier that would drag the back line down) and drawn alone in
the goal box.

`FormationLabel` re-reads those rows back out as "4-3-3" for the panel's
foot.
