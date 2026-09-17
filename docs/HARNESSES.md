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

    env -u WAYLAND_DISPLAY -u DISPLAY GF_NO_GAMEPADS=1 SDL_VIDEODRIVER=offscreen ./gameplayfootball <config>

`GF_NO_GAMEPADS=1` keeps the joystick subsystem closed, so a pad left plugged
into the workstation cannot steer a headless menu. Always set it.

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

## Never edit a script that is running

Bash reads a script file incrementally, by byte offset, as it executes - it does
not load it. Rewriting `tools/simbatch.sh` while an instance of it was in its
spawn loop shifted every offset after the edit: the running shell resumed
mid-token (`line 84: red:: command not found`) and **re-ran seeds it had already
finished**, truncating six good results to zero bytes on the way past.

Cost when it happens: a whole 12-match batch of CPU, plus the results it
overwrote. If a fix is needed mid-batch, write a copy, wait for the batch to
drain, then swap it in.

The same applies to `build/gameplayfootball`: `run_one` resolves `$bin` at exec
time, so a match that *starts* after a relink runs the new code. A batch that
spanned four relinks produced twelve matches on several different builds and
could not be used as a before/after comparison at all. Record the binary's
checksum beside the batch:

    md5sum build/gameplayfootball tools/simbatch.sh > /tmp/batch_frozen.txt

## A seed is only comparable at the same concurrency

`simbatch.sh` fixes `random_seed` per match, and its header says a before/after
pair over the same seeds is comparable. It is - but only if both runs were made
with the same `--concurrency`. Seed 5000 on one binary and one config gave
`shots 7-5 | xg 0.89-0.61` alone and `shots 6-5 | xg 0.87-0.58` inside a 3-way
batch: the match paces some of its stages off real time, so CPU contention
moves the tick at which things happen and the game diverges. Hold concurrency
constant across the arms of any comparison, and say which it was when quoting
a single seed.

## Never `git add` a directory a subagent might be writing into

A dispatched agent's file tools resolved against this repo rather than the
worktree it was told to use, so its draft landed in the main working tree
while its `bash` cwd was correct. A `git add src/ tools/ tests/` then swept
that draft into a commit about the save gate, and the two works had to be
separated by hand afterwards (their files out, their hunks out of four shared
files, my own later fix re-applied on top - it had been reverted by a
`git checkout` of the same files).

The rules that come out of it:

- **List files, never directories**, in `git add` while another agent is
  running against the repo. `git add src/` is an assertion that everything
  under `src/` is yours, and it is false for as long as a peer is alive.
- **Read `git status` before every commit** and account for every path in it.
  Four ambient `data/` files are expected here; a new `src/data/*.hpp` is not.
- Every agent gets **absolute paths** in its brief and its own build directory,
  and its acceptance includes `git status` in *both* trees showing only its own
  files.
- If it happens anyway: `git show --stat` the commit, keep the peer's work as a
  patch (`git show <commit> -- <their files> > /tmp/peer.patch`), `git reset
  --soft HEAD~1`, take their hunks out of the shared files, re-add **your files
  by name**, and hand the patch back. Do not rewrite a peer's commits for them
  and do not let them rewrite yours - both were rightly refused mid-incident.
- Then **rebuild and re-measure**. A binary built from a tree that briefly
  contained someone else's code is not evidence, and neither `nm` for their
  symbols nor the source diff settles it when the code may be inlined.

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
