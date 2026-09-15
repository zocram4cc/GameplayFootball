// PES's goal director, read from media/cutscenes/goal/director.txt (written by
// tools/pes21_import/export_cutscenes.py from PES's own table_goal.bin).
//
// A goal in PES is not one celebration clip under one camera: it is a walk
// through STATES - the scorer's run (GOAL_RUN_30_*), the celebration itself
// (GOAL_CELEBRATE_NNNN), the teammates arriving (GOAL_HUGA/B_LV2/3), an
// approach to the fence, flag or camera (GOAL_A_*), a finish (GOAL_C_*) - and
// each state has a set of SHOTS it may play, a shot being the layers to load
// together: an authored camera track, a procedural follow camera, the actors'
// choreography, props. The table holds no edges; the order of phases is the
// game's, and this module walks it the way PES does, choosing within each
// phase by the shot's situation flags and weight.
//
// Pure: parses text and plans; the match stages what it returns.

#ifndef _HPP_ONTHEPITCH_GOALDIRECTOR
#define _HPP_ONTHEPITCH_GOALDIRECTOR

#include <string>
#include <vector>

namespace GoalDirector {

enum class Phase { Start, Run, Celebrate, Hug, Approach, Finish, End, Situation, Insert, Other };

struct Follow {
  // What a procedural camera's .fdc record actually carries (tag 0x06, see
  // tools/pes21_import/camera_cut.py): a duration in frames at 30 fps and the
  // clip planes. The record holds no camera placement - measured across PES's
  // whole goal library, the fields that looked like one carry the same values
  // on authored cuts, where the placement is in the .canm and does not follow
  // them. utils/camtrack.hpp FollowCameraFrame has the numbers.
  float durationFrames = 0.0f;
  float nearPlane = 0.5f;
  float farPlane = 400.0f;
};

struct Layer {
  enum Kind { Track, FollowCamera, Actors, Props, Empty };
  Kind kind = Empty;
  std::string base;  // the .fdc stem; .camtrack / .chor share it on disk
  Follow follow;     // when kind == FollowCamera
};

struct Shot {
  float weight = 0.0f;   // the row's f32: clip frames for the run/celebrate rows
  unsigned flags = 0;    // situation mask
  unsigned on = 0;
  unsigned record = 0;   // zone/shot word: byte 2 is the pitch quadrant
  std::vector<Layer> layers;
  // Convenience over layers: the first authored track, the first follow camera,
  // and the first actor set, or empty.
  std::string Track() const;
  const Layer* FollowCamera() const;
  std::string Actors() const;
  // Frames this shot runs: its weight when that reads as a clip length, else
  // the follow camera's duration, else 0 (the caller uses the track's own).
  int Frames() const;
};

struct State {
  std::string name;
  Phase phase = Phase::Other;
  std::vector<Shot> shots;
};

struct Director {
  std::vector<State> states;
  bool empty() const { return states.empty(); }
  const State* Find(const std::string& name) const;
};

Director Parse(const std::string& text);
Phase PhaseOf(const std::string& word);

// What the walk needs to know about this goal.
struct Situation {
  bool ownGoal = false;
  bool offside = false;
  int teammatesNear = 0;   // how many arrive: LV2 for a few, LV3 for the mob
  float runDistance = 30.0f;  // metres the scorer has to run before he celebrates
  int quadrant = 0;        // 0..3 as PES's cmnCam_S_{L,R}{B,M} name it
  std::string celebration;  // the scorer's own celebration state, if he has one
  int seed = 0;
};

struct Beat {
  const State* state = nullptr;
  const Shot* shot = nullptr;
};

// PES's walk for this goal: run -> celebrate -> hug -> approach -> finish, with
// the situation overrides (an own goal is GOAL_S_OWNGOAL_* and nothing else;
// an offside is GOAL_S_OFFSIDE* and nothing else). Each beat is a state and
// the shot chosen for it. Empty when the director has nothing to say.
std::vector<Beat> Plan(const Director& director, const Situation& situation);

// The shot a state plays in this situation: the best-weighted candidate whose
// flags admit the situation, rotated by the seed so the same goal does not
// always draw the same angle.
const Shot* ChooseShot(const State& state, const Situation& situation);

}  // namespace GoalDirector

#endif
