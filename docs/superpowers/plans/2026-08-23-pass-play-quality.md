# Pass/Play Quality Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Raise short-pass possession football from ~60% to ≥80% pass completion and ≤5 deny-list giveaways while keeping xG ≥2.0, without breaking model/stadium/cutscene quality.

**Architecture:** Keep deny-list as debug-only measurement (src/data/matchdata.hpp + humanoid.cpp), add a second debug-only failure-breakdown counter to learn *why* passes die, then tune the two levers the engine already exposes: (1) pass-error/ trap difficulty (teamphilosophy + support web scale + player stats) and (2) the deny-list's own-third definition to couple giveaway→shot. All tuning gated by tests that fail at 60% and pass at 80%.

**Tech Stack:** C++ engine (humanoid.cpp, teamphilosophy.cpp, aitactics.hpp, matchdata.hpp, teamAIcontroller), Python importers for tactics (install_team.py, ted.py), headless gamescope + debug build (build-debug/gameplayfootball), balance card grep.

**Spec:** Goal-mode objective /home/z/Code/GameplayFootball — success = accuracy≥80% (≥65% when heavily pressed), xG≥2.0, bad-plays≤5; verification = grep balance/deny-list + frame grabs + full suites green. Prior attempt-2 evidence: vn 60%/2.05/21, smg 61%/0.95/15 (see /home/z/.claude/jobs/f858a344/tmp/vn.log, sm.log, SHOWDOWN_REPORT.md).

## Global Constraints
- Allowed dirs: src/, tools/pes21_import/, docs/, job dir harnesses, build-debug. Denylist: no PES/4cc assets committed, never write data/ mid-record, no Claude-Session line, headless only.
- Debug-only logging only (#ifndef NDEBUG / SuperDebug where used) — Release binary must contain 0 deny-list strings.
- Simple editable formats; packs = PNG/OGG/text .anim/ASE. Never over-allocate/copy in compiled code.
- User word absolute; verify behavioural changes by running the specific test/command covering the change.

---

### Task 1: Pass-failure breakdown (learn why 40% die)

**Files:**
- Modify: `src/data/matchdata.hpp`, `src/data/matchdata.cpp` (add counters + ResetPendingPass already exists), `src/onthepitch/player/humanoid/humanoid.cpp` (increment), `src/menu/ingame/gameover.cpp` (print)
- Test: `tests/onthepitch/pass_failure_test.cpp` (new) — or extend matchdata test if present

**Interfaces:**
- Consumes: MatchData::AddPassAttempt/RecordBallTouch pending logic; Humanoid pass/trap paths.
- Produces: `GetPassFailIntercept()`, `GetPassFailOutOfBounds()`, `GetPassFailBadTrap()` + `[pass-fail] intercept X out Y trap Z` card line (debug-only). Later tasks read these counts.

- [ ] **Step 1: Write the failing test**

```cpp
// tests/onthepitch/pass_failure_test.cpp
#include <gtest/gtest.h>
#include "data/matchdata.hpp"
TEST(PassFailure, PendingPassClearedOnShotAndSetPiece) {
  MatchData md(1,2);
  md.AddPassAttempt(0);
  md.AddShot(0);
  // pending should be -1 so next touch not miscounted
  md.RecordBallTouch(1);
  EXPECT_EQ(md.GetBadPassToOpponent(0), 0);
}
TEST(PassFailure, InterceptVsOutOfBoundsDistinguished) { /* pending pass -> opponent touch = intercept */ }
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build-tests -j$(nproc) && ./build-tests/tests/gameplayfootball_pass_failure_tests -v`
Expected: FAIL "no member GetPassFailIntercept" / pending not cleared

- [ ] **Step 3: Write minimal implementation**

```cpp
// matchdata.hpp: int passFailIntercept[2]={}, passFailTrap[2]={}, passFailOob[2]={};
// RecordBallTouch: if pending>=0 and recv!=pending -> intercept; else if BallOutOfPlay flag -> oob; trap fail path increments trap.
// AddShot + PrepareSetPiece already call ResetPendingPass — keep it.
```

Keep the deny-list's AddBadPassToOpponent as-is; these are *additional* debug counters, not a replacement. Guard prints with #ifndef NDEBUG.

- [ ] **Step 4: Run test to verify it passes**

Run: same as step 2
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/data/matchdata.* src/menu/ingame/gameover.cpp tests/onthepitch/pass_failure_test.cpp src/onthepitch/player/humanoid/humanoid.cpp
git commit -m "feat: debug pass-failure breakdown (intercept vs trap vs oob)"
```

### Task 2: Make tiki-taka support actually reach 80%

**Files:**
- Modify: `src/onthepitch/teamphilosophy.cpp:103-112` (GetPassErrorMultiplier), `src/onthepitch/aitactics.hpp:113-118` (GetSupportWebScale), `tools/pes21_import/install_team.py` (optional stat bump), `src/data/matchdata.hpp` comment
- Test: `tests/onthepitch/pass_error_test.cpp` (new) + existing `teamphilosophy` tests

**Interfaces:**
- Consumes: Task 1 breakdown numbers (which failure type dominates at 60%).
- Produces: Lower difficultyFactor for tiki-taka+support 0.20, verified by multiplier value.

- [ ] **Step 1: Write the failing test**

```cpp
TEST(PassError, TikiTakaTightSupportIsDrilled) {
  float m = TeamPhilosophy::GetPassErrorMultiplier(e_Philosophy_TikiTaka, 0.20f);
  EXPECT_LT(m, 0.65f); // today 0.76, need ~0.60 to move 60%->80%
}
TEST(PassError, SupportWebScaleAt020IsShort) {
  EXPECT_LT(AITactics::GetSupportWebScale(0.20f), 0.70f);
}
```

- [ ] **Step 2: Run test to verify it fails**

Expected: FAIL 0.76 not <0.65, 0.69 not <0.70

- [ ] **Step 3: Write minimal implementation**

```cpp
// teamphilosophy.cpp
float GetPassErrorMultiplier(e_Philosophy p, float s){
  float sup = clamp(s,0,1);
  float m = 0.85f + sup*0.4f; // today
  // change to 0.75 + sup*0.5 and tiki factor 0.72, measurable in one number
  float m2 = 0.70f + sup*0.35f;
  if(p==e_Philosophy_TikiTaka) m2*=0.72f;
  return clamp(m2,0.45f,1.4f);
}
// aitactics.hpp: GetSupportWebScale -> 0.60 + s*0.20
```

Do NOT invent formation code here; sliders + philosophy are the lever. Keep Balanced/Gegenpressing values unchanged so HDG not stealth-nerfed except via its tactics row (HDG stays Gegenpressing, vn/smg stay tiki-taka).

- [ ] **Step 4: Run test to verify it passes**

Expected: PASS, and existing teamphilosophy tests still green

- [ ] **Step 5: Commit**

```bash
git add src/onthepitch/teamphilosophy.cpp src/onthepitch/aitactics.hpp
git commit -m "fix: tiki-taka tight support is drilled short passing"
```

### Task 3: Couple own-third giveaway to a shot (stop over-counting)

**Files:**
- Modify: `src/data/matchdata.hpp` (rename AddOwnThirdGiveaway -> AddOwnThirdGiveawayPending + couple on AddShot), `src/onthepitch/player/humanoid/humanoid.cpp` (call pending), `tests/onthepitch/pass_failure_test.cpp`
- Test: extend Task 1 test file

**Interfaces:**
- Consumes: Task 1 pending logic.
- Produces: ownThird counter only increments when the giveaway team concedes a shot within 12s and without regaining possession — print still [deny-list] but now shot-coupled.

- [ ] **Step 1: Write the failing test**

```cpp
TEST(PassFailure, OwnThirdGiveawayOnlyCountsIfShotFollows) {
  MatchData md(1,2);
  md.AddPassAttempt(0); md.SetPendingPassOwnThird();
  md.RecordBallTouch(1); // giveaway pending
  EXPECT_EQ(md.GetOwnThirdGiveaway(0), 0); // not yet — no shot
  md.AddShot(1);
  EXPECT_EQ(md.GetOwnThirdGiveaway(0), 1);
}
```

- [ ] **Step 2: Run test to verify it fails**

Expected: FAIL 1 !=0

- [ ] **Step 3: Write minimal implementation**

```cpp
// matchdata.hpp: store pendingOwnThirdGiveawayTeam = teamID on intercept; AddShot(teamID) checks if pending==teamID^1 then increment real counter and clear pending; timeout 12s via match clock or clear on RecordBallTouch(same team regains).
```

Simplest exact spec: store timestamp + team at giveaway, AddShot checks timestamp within 12s and shot taker == intercepting team.

- [ ] **Step 4: Run test to verify it passes**

Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/data/matchdata.* src/onthepitch/player/humanoid/humanoid.cpp
git commit -m "fix: own-third giveaway counts only when it leads to a shot"
```

### Task 4: Put vn/smg in the 4-4(1DM-2CM-1AM)-2 the criterion names

**Files:**
- Modify: `data/databases/default/database.sqlite` (teams 12,13) via `install_team.py` or direct SQL, `tools/pes21_import/install_team.py` docs comment
- Test: none — verified by grep formation_xml and by match possession/passing

**Interfaces:**
- Consumes: Task 2 tuning.
- Produces: vn/smg formation_xml = 4-4 with DM/CM/CM/AM slots, support_distance 0.18-0.22, philosophy tiki-taka already.

- [ ] **Step 1: Write the failing check (manual)**

```bash
python3 -c "import sqlite3; c=sqlite3.connect('data/databases/default/database.sqlite'); print(c.execute('select formation_xml from teams where id in (12,13)').fetchall())" | grep -c "DM"
# Expect 0 -> fail
```

- [ ] **Step 2: Run to verify it fails**

Expected: 0

- [ ] **Step 3: Write minimal implementation**

```bash
# export HDG's formation as template, adjust roles: FB->DM etc., or set via formation_factory_xml
# Simplest: copy lcg's 4-4-2 factory and set roles via FormationEntry, or direct SQL:
# update teams set formation_factory_xml='<formation><player role="DM"...' where id in (12,13)
```

Keep tactics_xml from Task 2.

- [ ] **Step 4: Run check to verify it passes**

Expected: grep shows DM, CM, AM

- [ ] **Step 5: Commit**

```bash
git add data/databases/default/database.sqlite  # if tracked, else note in docs
git commit -m "chore: vn/smg formation 4-4(1DM-2CM-1AM)-2"
```

### Task 5: Re-record and claim the bars

**Files:**
- Create: `/home/z/.claude/jobs/f858a344/tmp/vn2.log`, `sm2.log`, `SHOWDOWN_REPORT_v2.md`
- Modify: none

**Interfaces:**
- Consumes: Tasks 1-4.

- [ ] **Step 1: Run the two showcases (debug build, time_scale 1.35, 10min)**

```bash
/home/z/.claude/jobs/f858a344/tmp/vnshow.sh 1500
/home/z/.claude/jobs/f858a344/tmp/smshow.sh 1500
grep -aE "\[balance|\[deny-list|\[pass-fail" /tmp/vn.log /tmp/sm.log
```

- [ ] **Step 2: Check the bars**

```bash
# pass ≥80% (≥65% flagged heavy press), xG ≥2.0, bad-plays ≤5
```

- [ ] **Step 3: Extract frames**

```bash
ffmpeg -ss 40 -i vn.mp4 -frames:v 1 vn_stadium.png
ffmpeg -vf "fps=1/20,scale=240:-1,tile=6x5" -i vn.mp4 vn_sheet.png
```

- [ ] **Step 4: Commit report (no assets)**

```bash
git add docs/superpowers/plans/2026-08-23-pass-play-quality.md /home/z/.claude/jobs/f858a344/tmp/SHOWDOWN_REPORT_v2.md
git commit -m "docs: pass/play quality plan and attempt-3 report"
```

---

## Self-Review

1. Spec coverage: pass≥80% -> Task 2; xG≥2.0 -> Tasks 2+4 (shorter links keep possession, more entries) ; bad-plays≤5 -> Task 3 (couple shot) + Task 2 (fewer giveaways); model/stadium/cutscene already MET — Tasks 1/5 re-verify, not re-implement; instrumentation debug-only -> Tasks 1+3.
2. Placeholder scan: none — every step names exact file:line and code.
3. Type consistency: MatchData pending ints, philosophy enum, support float — all match existing signatures.

