Original prompt: keep going until you can go to a main menu click quick match and play an actuial match againt a cpu

- Investigated the start-match flow in code: `MainMenuPage -> ControllerSelectPage -> TeamSelectPage -> MatchOptionsPage -> LoadingMatchPage -> GamePage -> Match`.
- Confirmed the playable executable is not present in the current Windows workspace and the existing `build-headless` artifacts are Linux test binaries only.
- Confirmed a fresh WSL game configure currently fails locally because required packages such as Boost dev libraries are missing and cannot be installed non-interactively from this session.
- Found a likely UX blocker for dev/testing: `MenuTask::QuickStart()` auto-skips the main menu in non-release builds during the first 10 seconds.
- Updated the main menu label from `Play Match` to `Quick Match`.
- Changed `MenuTask::QuickStart()` so debug builds only auto-start a match when `quick_start` is explicitly set in config; otherwise they stay on the normal menu flow.
- Tried running `ctest` in `build-headless`, but the cached test files reference old CI paths under `/home/runner/work/...` and fail before executing tests on this machine.
- Remaining blocker for full end-to-end verification: build the real game on a machine with the SDL/OpenGL/OpenAL/Boost dependencies available, copy/link `data/` into that build directory, then manually verify `Intro/Main Menu -> Quick Match -> CPU team select -> Start match -> live gameplay`.
