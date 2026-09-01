# The verification harnesses

Every claim in this fork that says "verified" was produced by one of these. The
recording harness is now `tools/showcase.sh` and lives in the repository; what
follows is the reasoning it encodes, and the traps that cost real runs before it
did.

## Check what is rasterising before you believe a frame

`xvfb-run` gives an X display with no GPU behind it, so the engine renders through
**llvmpipe**, in software. That cost two thirds of the frame rate (32 fps against a
recorder it now saturates at 60) and it also *changed the picture*: the pitch came
out covered in red-mauve speckle that survived a long hunt through the shaders and
was never in the engine at all. One second of checking would have said so:

    xvfb-run -a glxinfo -B | grep "OpenGL renderer"    # llvmpipe (LLVM 22.1.8)

Render on the card instead. SDL's offscreen driver goes through EGL to
`/dev/dri/renderD128` with no X server and no window, which is what
`tools/showcase.sh` uses:

    env -u WAYLAND_DISPLAY -u DISPLAY SDL_VIDEODRIVER=offscreen ./gameplayfootball <config>

Confirm it took, rather than assuming - the process should hold the render node open:

    ls -l /proc/$(pgrep -f "gameplayfootball .*config")/fd | grep dri

## Why frames go through a fifo

The engine writes every presented frame to `frame_recording_path` as raw RGBA, and
ffmpeg reads that and encodes. A window grab is no good: under gamescope's headless
backend the X pixmap goes stale and the video shows frames from seconds ago.

    mkfifo $FIFO
    ffmpeg -f rawvideo -pixel_format rgba -video_size 1280x720 -framerate 60 -i $FIFO ... out.mp4 &
    gamescope --backend headless -W 1280 -H 720 -- ./gameplayfootball <config> &

**The harness must write `frame_recording_path` itself.** A harness that copies a config
and renames only its own fifo leaves the config pointing at the old path. If nothing is
reading there, the engine creates a *regular file* and fills it with uncompressed frames
— 32 GB in about forty minutes at 1280x720x60. So: strip the key from the config, append
the real one, and refuse to start unless the target is a fifo:

    grep -vE "frame_recording_path" base.config > run.config
    echo "\"frame_recording_path\" \"$FIFO\"" >> run.config
    [ -p "$FIFO" ] || { echo "no fifo, refusing to record"; exit 1; }

## Never write to `data/` while a run records

Two showcase runs died because an importer rewrote a stadium `.ase` and deleted its
`.geomcache` while the engine was loading them. Assets are stable or the run is not
happening; there is no third option.

## Killing a run

`pkill -f <pattern>` matches **this shell's own command line**, which contains the
pattern, so it kills the tool that issued it. Kill by PID:

    for p in $(pgrep -f "gameplayfootball menu_smoke_x"); do kill -9 $p; done

Equally, `pgrep -c` counting "leftovers" often counts the querying shell. Check the
actual command lines before believing a process survived.

## The harness

`tools/showcase.sh` records a full match and lives in the repository, because the
scratch versions of it were rewritten from memory every session and three runs were
lost to their mistakes.

    tools/showcase.sh --team1 16 --team2 13 --minutes 10 --out /tmp/match.mp4

`--stadium`, `--base`, `--bin` and `--limit-mb` cover the rest; `--limit-mb 0`
keeps the raw encode. It owns the recording path, writes fragmented mp4 so an
interrupted run still plays, drains the encoder rather than killing it, refuses to
report a run that never reached `destroying scenemanager`, and fits the result into
a size limit in one pass.

## Reading a run without watching it

- `debug_cutscene_report true` logs, for every cutscene, its anchoring and how far the
  camera ended up from the incident. That one line found the substitution bug.
- `StartCutscene` logs the category and the match clock unconditionally, so a cutscene
  can be located in the video by reading the scoreboard clock in a sampled frame.
- The clock maps to video time linearly within a half; sample four frames, crop the
  clock, read them, interpolate.
- A run that reached teardown prints `destroying scenemanager`. A harness that does not
  check for it is not evidence of anything.

## Looking at frames

    ffmpeg -v error -ss <seconds> -i out.mp4 -frames:v 1 -y frame.png

Contact sheets beat single frames for finding a moment. Crop before scaling when
checking something small - a board's text, a name on the ticker - and remember that
`convert('L')` or `convert('RGB')` throws the alpha away, which once hid a capture that
was entirely transparent.
