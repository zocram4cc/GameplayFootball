Original prompt: keep going until you can go to a main menu click quick match and play an actuial match againt a cpu

- Investigated the start-match flow in code: `MainMenuPage -> ControllerSelectPage -> TeamSelectPage -> MatchOptionsPage -> LoadingMatchPage -> GamePage -> Match`.
- Confirmed the playable executable is not present in the current Windows workspace and the existing `build-headless` artifacts are Linux test binaries only.
- Confirmed a fresh WSL game configure currently fails locally because required packages such as Boost dev libraries are missing and cannot be installed non-interactively from this session.
- Found a likely UX blocker for dev/testing: `MenuTask::QuickStart()` auto-skips the main menu in non-release builds during the first 10 seconds.
- Updated the main menu label from `Play Match` to `Quick Match`.
- Changed `MenuTask::QuickStart()` so debug builds only auto-start a match when `quick_start` is explicitly set in config; otherwise they stay on the normal menu flow.
- Tried running `ctest` in `build-headless`, but the cached test files reference old CI paths under `/home/runner/work/...` and fail before executing tests on this machine.
- Remaining blocker for full end-to-end verification: build the real game on a machine with the SDL/OpenGL/OpenAL/Boost dependencies available, copy/link `data/` into that build directory, then manually verify `Intro/Main Menu -> Quick Match -> CPU team select -> Start match -> live gameplay`.
- Hardened menu/controller stability for missing or hot-unplugged devices:
  `ControllerSelectPage` now handles zero controllers with an on-screen notice + back button, skips unsafe keyboard/gamepad casts, and no longer asserts when controller counts change in-game.
- Hardened settings subpages against unavailable devices/data:
  keyboard save now checks for a real keyboard controller before writing config; gamepad list/setup/calibration/mapping/function pages now show a safe fallback page instead of indexing stale controller ids; missing-language and missing-resolution situations now fall back to a usable menu state.
- Hardened `SetActiveController()` in `MenuTask` so stale controller ids from saved side-selection state no longer index past the current controller list.
- Verification attempt on Windows:
  `cmake -S . -B build-menu-check -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` fails during configure because Boost headers are not installed locally (`Could NOT find Boost (missing: Boost_INCLUDE_DIR)`).
- Verification attempt on existing Linux-style artifacts:
  `ctest --test-dir build-headless --output-on-failure` still fails immediately because the cached generated test includes point at CI paths under `/home/runner/work/...`.
