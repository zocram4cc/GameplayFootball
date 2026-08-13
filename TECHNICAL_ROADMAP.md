# GameplayFootball: Technical Analysis & Implementation Roadmap

**Date:** January 26, 2026
**Project:** GameplayFootball (C++ / SDL2 / OpenGL / Boost)
**Status:** High Potential Legacy Codebase

---

## 1. Executive Summary

The codebase represents a robust, albeit "scruffy," foundation for a football simulation. Unlike many open-source sports games, it possesses a sophisticated physics engine, a working AI decision tree (`ElizaController`), and a flexible animation system.

**Verdict:** **Strong Go.** Recreating the physics and AI logic present here would take years. The "scruffiness" is mostly in state management (match phases) and lack of polish, which are solvable problems.

---

## 2. Codebase Architecture

### Key Directories
*   **`src/onthepitch/`**: The core game loop.
    *   `match.cpp`: The central hub managing the game state.
    *   `player/`: Contains `Player` (state), `Humanoid` (animation/physics), and `ElizaController` (AI).
    *   `teamAIcontroller.cpp`: High-level tactical logic.
*   **`src/data/`**: Data loading.
    *   `teamdata.cpp` / `playerdata.cpp`: Loads XML stats and configurations.
*   **`src/utils/objectloader.cpp`**: Handles `.object` (XML) and `.ase` (Geometry) loading.
*   **`src/menu/`**: GUI system (Game Plan, Scoreboard, etc.).

---

## 3. Core Feature Implementation Plans

### A. Arbitrary Model Loading
**Goal:** Unique models for every player, ball, and stadium.
*   **Current State:** Uses a shared `player.object` template.
*   **Implementation:**
    1.  Modify `PlayerData` to include a `std::string model_url`.
    2.  Update `Team::InitPlayers` to read this URL.
    3.  Pass the specific URL to `ObjectLoader::LoadObject`.
*   **Effort:** **Low**. The engine already supports loading arbitrary XML/ASE files.

### B. AI vs. AI Manager Mode
**Goal:** Two human managers controlling tactics/subs for AI teams.
*   **Current State:** "Game Plan" menu exists but modifies a static `Team` state.
*   **Implementation:**
    1.  **UI:** Expose the `GamePlan` menu for both teams during a match, regardless of controller input.
    2.  **Substitutions:** Implement `Team::Substitute(Player* out, Player* in)`.
        *   Must handle deactivating the old `Humanoid` and activating the new one at the next stoppage.
    3.  **Tactics:** Bind UI sliders to the `liveTeamTactics` Property set in `TeamAIController`.
*   **Effort:** **Moderate**. UI work is the primary task.

### C. Extra Time & Penalties
**Goal:** Full match progression including shootouts.
*   **Current State:** Extra time logic exists but is buggy. Penalties are a stub that triggers `GameOver`.
*   **Implementation (Penalties):**
    1.  **Controller:** Create `PenaltyShootoutController` to manage the state machine (`POSITIONING`, `EXECUTION`, `RESOLUTION`).
    2.  **Dice-Roll Logic:** Instead of complex physics, determine outcomes based on stats:
        *   **Shooter:** `mental_vision` + `technical_shot` = Cone of accuracy (Random point generation).
        *   **Keeper:** `physical_reaction` + `mental_defensivepositioning` = Dive success chance.
    3.  **Visuals:** Play pre-canned animations (`penalty_kick_goal`, `penalty_save_left`) matching the calculated outcome.
*   **Effort:** **High**. Requires a dedicated sub-system and specific animations.

### D. Scriptable Cutscenes
**Goal:** JSON/XML defined cinematics (Intro, Goal Celebration).
*   **Implementation:**
    1.  **Schema:** Define an XML timeline format (tracks for Camera, Actors, Audio).
    2.  **`Director` Class:** Takes control of the `Camera` and disables `ElizaController` for involved players.
    3.  **Interpolation:** Calculate frame-by-frame positions/rotations based on XML keys.
*   **Effort:** **Moderate**. Leveraging existing assets makes this easier.

---

## 4. Gameplay Mechanics Deep Dive

### A. Tactics System
The engine uses a 3-layer property system (`Base`, `Multiplier`, `Live`).
**Editable Parameters (found in `teamdata.cpp`):**
*   **Depth:** `position_offense_depth_factor`, `position_defense_depth_factor`
*   **Width:** `position_offense_width_factor`, `position_defense_width_factor`
*   **Midfield:** `position_offense_midfieldfocus`, `position_defense_midfieldfocus`
*   **Aggression:** `position_offense_sidefocus_strength` (Forward Drive)
*   **Compactness:** `position_offense_microfocus_strength`
*   **Dribbling:** `dribble_offensiveness`, `dribble_centermagnet`

### B. Player Statistics (`playerdata.cpp`)
*   **Physical:** Balance, Reaction, Acceleration, Velocity, Stamina, Agility, ShotPower.
*   **Technical:** StandingTackle, SlidingTackle, BallControl, Dribble, ShortPass, HighPass, Header, Shot, Volley.
*   **Mental:** Calmness, WorkRate, Resilience, DefPositioning, OffPositioning, Vision.

### C. Formations & Roles
*   **Formations:** Defined via `formation_xml` in the database. Supports arbitrary XY coordinates.
*   **Roles:** `GK`, `CB`, `LB`, `RB`, `DM`, `CM`, `LM`, `RM`, `AM`, `CF`.
*   **Adding New Roles (e.g., SS, WF):**
    1.  Add to `e_PlayerRole` enum.
    2.  Define default coordinates in `TeamData::GetDefaultRolePosition`.
    3.  **Crucial:** Implement behavioral logic in `ElizaController` (e.g., "SS looks for space between lines").

### D. Traits & Playstyles (PES-Style Cards)
**Concept:** Conditional stat modifiers or logic overrides.
1.  **Cards (Modifiers):**
    *   *One-Touch Pass:* In `ElizaController`, if `time_possessed < 200ms`, negate the default accuracy penalty.
    *   *First-Time Shot:* Boost `shot_power` and `technique` if hitting a rolling ball first-time.
2.  **Playstyles (Logic):**
    *   *Goal Poacher:* In `TeamAIController`, override the Z-position to stay glued to the opponent's offside line.
    *   *Creative Playmaker:* In `CalculateSpotRating`, heavily weight "distance from opponent" to find pockets of space.

---

## 5. Remote Web-Based Manager Mode

**Concept:** Streamer runs the C++ Engine; Managers control tactics via a Web Dashboard.

### Architecture
`[C++ Engine] <-> (WebSocket) <-> [Cloud Relay Server] <-> (HTTPS/WS) <-> [Web Browser]`

### Implementation Steps
1.  **C++ Networking (The Hard Part):**
    *   Integrate `Boost.Beast` or `websocketpp`.
    *   **Thread Safety:** Create a `NetworkManager` running on a separate thread.
    *   **Command Queue:** Incoming JSON commands (`SET_TACTIC`, `SUBSTITUTION`) are pushed to a queue. `Match::Process()` (Main Thread) consumes this queue at safe moments (e.g., stoppages).
2.  **State Relay:**
    *   Engine serializes `MatchState` (Score, Time, Possession) to JSON every ~1 second.
3.  **Web Server (Python/Node):**
    *   Handles Authentication (Manager A vs Manager B).
    *   Enforces Rules (e.g., Max 3 tactic changes per half).
4.  **Frontend:**
    *   Replicates `GamePlan` UI using HTML Ranges and Drag-and-Drop.

### Effort Estimate
*   **Total:** ~3-4 Weeks for a single developer.
*   **Risk:** High (Concurrency/Crashing in C++). Recommendation: Start with Substitutions only.

---

## 6. Implementation Checklist (Prioritized)

1.  [ ] **Fix Match State:** Clean up Extra Time logic in `Referee`.
2.  [ ] **Arbitrary Loading:** Update `ObjectLoader` to support per-player models.
3.  [ ] **Penalty Logic:** Implement the "Dice Roll" controller.
4.  [ ] **Cutscene Director:** Build the XML parser and Camera override.
5.  [ ] **Remote API:** Implement the C++ WebSocket client and Command Queue.
