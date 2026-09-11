#include "goaldirector.hpp"

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace GoalDirector {

namespace {

// The situation flags read as an OCCASION ladder: 0x1 any goal, 0x3 one that
// matters, 0x7 / 0xf / 0x1f / 0x3f bigger still (the hugs start at 0x3, the
// fence-jumping OVERBOARD rows are mostly 0x3f, a plain run is 0x1). A shot
// admits an occasion when the occasion's bit is in its mask. The high bits
// (0x200000, 0x800000, 0x1400000) mark the special situations - own goal,
// offside, coach - and never appear on an ordinary celebration row.
constexpr unsigned kOccasionBits = 0x3f;
constexpr unsigned kSpecialBits = ~kOccasionBits;

unsigned OccasionBit(const Situation& s) {
  // Teammates arriving is what makes a goal an occasion here; the match can
  // grow this once it knows a late winner from a consolation.
  if (s.teammatesNear >= 5) return 0x20;
  if (s.teammatesNear >= 3) return 0x8;
  if (s.teammatesNear >= 1) return 0x2;
  return 0x1;
}

int Quadrant(const Shot& shot) { return (shot.record >> 16) & 0xff; }

bool Admits(const Shot& shot, const Situation& s) {
  const unsigned occasion = shot.flags & kOccasionBits;
  // A row carrying a special bit is gated by its situation, not the occasion
  // ladder (the own-goal rows read 0x20103e: every occasion but the plain one,
  // because an own goal is never plain).
  const bool special = (shot.flags & kSpecialBits) != 0;
  if (!special && occasion != 0 && !(occasion & OccasionBit(s)) && !(occasion & 0x1)) return false;
  // A row pinned to a quadrant plays in that quadrant; 0 is anywhere.
  const int quadrant = Quadrant(shot);
  if (quadrant != 0 && quadrant != s.quadrant) return false;
  return true;
}

std::string RunState(const Situation& s, const Director& director) {
  // PES keeps two run lengths, 30 m and 40 m, and a dozen ways to run each.
  // Which run he does is his to draw; how far is the pitch's.
  const char* length = s.runDistance >= 35.0f ? "GOAL_RUN_40_" : "GOAL_RUN_30_";
  std::vector<const State*> runs;
  for (const State& state : director.states)
    if (state.phase == Phase::Run && state.name.compare(0, 12, length) == 0 && !state.shots.empty())
      runs.push_back(&state);
  if (runs.empty()) return "";
  return runs[std::abs(s.seed) % (int)runs.size()]->name;
}

std::string HugState(const Situation& s) {
  if (s.teammatesNear <= 0) return "";
  // A or B is the side the teammates come from, as PES names them; LV2 a few,
  // LV3 the mob.
  const char* side = (s.seed & 1) ? "GOAL_HUGB_LV" : "GOAL_HUGA_LV";
  return std::string(side) + (s.teammatesNear >= 4 ? "3" : "2");
}

const State* PickByPhase(const Director& director, Phase phase, const Situation& s,
                         const char* prefix) {
  std::vector<const State*> candidates;
  for (const State& state : director.states) {
    if (state.phase != phase || state.shots.empty()) continue;
    if (prefix && state.name.compare(0, std::string(prefix).size(), prefix) != 0) continue;
    if (ChooseShot(state, s)) candidates.push_back(&state);
  }
  if (candidates.empty()) return nullptr;
  return candidates[std::abs(s.seed / 7) % (int)candidates.size()];
}

}  // namespace

std::string Shot::Track() const {
  for (const Layer& layer : layers)
    if (layer.kind == Layer::Track) return layer.base;
  return "";
}

const Layer* Shot::FollowCamera() const {
  for (const Layer& layer : layers)
    if (layer.kind == Layer::FollowCamera) return &layer;
  return nullptr;
}

std::string Shot::Actors() const {
  for (const Layer& layer : layers)
    if (layer.kind == Layer::Actors) return layer.base;
  return "";
}

int Shot::Frames() const {
  if (const Layer* follow = FollowCamera())
    if (follow->follow.durationFrames > 0.0f) return (int)follow->follow.durationFrames;
  return 0;
}

const State* Director::Find(const std::string& name) const {
  for (const State& state : states)
    if (state.name == name) return &state;
  return nullptr;
}

Phase PhaseOf(const std::string& word) {
  if (word == "start") return Phase::Start;
  if (word == "run") return Phase::Run;
  if (word == "celebrate") return Phase::Celebrate;
  if (word == "hug") return Phase::Hug;
  if (word == "approach") return Phase::Approach;
  if (word == "finish") return Phase::Finish;
  if (word == "end") return Phase::End;
  if (word == "situation") return Phase::Situation;
  if (word == "insert") return Phase::Insert;
  return Phase::Other;
}

Director Parse(const std::string& text) {
  Director director;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream words(line);
    std::string first;
    words >> first;
    if (first == "state") {
      State state;
      std::string key;
      words >> state.name >> key;
      std::string phase;
      words >> phase;
      state.phase = PhaseOf(phase);
      director.states.push_back(state);
    } else if (first == "shot" && !director.states.empty()) {
      Shot shot;
      std::string key;
      while (words >> key) {
        if (key == "weight") words >> shot.weight;
        else if (key == "flags") { std::string hex; words >> hex; shot.flags = std::strtoul(hex.c_str(), nullptr, 16); }
        else if (key == "on") words >> shot.on;
        else if (key == "rec") { std::string hex; words >> hex; shot.record = std::strtoul(hex.c_str(), nullptr, 16); }
      }
      director.states.back().shots.push_back(shot);
    } else if (!director.states.empty() && !director.states.back().shots.empty()) {
      Layer layer;
      words >> layer.base;
      if (first == "track") layer.kind = Layer::Track;
      else if (first == "follow") {
        layer.kind = Layer::FollowCamera;
        std::string key;
        while (words >> key) {
          if (key == "dur") words >> layer.follow.durationFrames;
          else if (key == "angle") words >> layer.follow.angleDeg;
          else if (key == "turn") words >> layer.follow.turnDeg;
          else if (key == "dist") words >> layer.follow.distance;
          else if (key == "damp") words >> layer.follow.damping;
          else if (key == "offset") words >> layer.follow.offsetDeg;
        }
      } else if (first == "actors") layer.kind = Layer::Actors;
      else if (first == "props") layer.kind = Layer::Props;
      else layer.kind = Layer::Empty;
      director.states.back().shots.back().layers.push_back(layer);
    }
  }
  return director;
}

const Shot* ChooseShot(const State& state, const Situation& situation) {
  std::vector<const Shot*> admitted;
  for (const Shot& shot : state.shots)
    if (Admits(shot, situation)) admitted.push_back(&shot);
  if (admitted.empty()) return nullptr;
  // The heaviest few are the ones PES favours; the seed picks among them so a
  // state does not always open on the same angle.
  std::stable_sort(admitted.begin(), admitted.end(),
                   [](const Shot* a, const Shot* b) { return a->weight > b->weight; });
  const size_t top = std::min<size_t>(admitted.size(), 4);
  return admitted[std::abs(situation.seed) % top];
}

std::vector<Beat> Plan(const Director& director, const Situation& s) {
  std::vector<Beat> beats;
  auto add = [&](const State* state) {
    if (!state) return;
    const Shot* shot = ChooseShot(*state, s);
    if (shot) beats.push_back({state, shot});
  };
  if (director.empty()) return beats;

  // The situations that ARE the whole scene.
  if (s.ownGoal) {
    add(PickByPhase(director, Phase::Situation, s, "GOAL_S_OWNGOAL"));
    return beats;
  }
  if (s.offside) {
    add(PickByPhase(director, Phase::Situation, s, "GOAL_S_OFFSIDE"));
    return beats;
  }

  // The walk.
  add(director.Find(RunState(s, director)));
  const State* celebrate = s.celebration.empty() ? nullptr : director.Find(s.celebration);
  if (!celebrate) celebrate = PickByPhase(director, Phase::Celebrate, s, "GOAL_CELEBRATE_");
  add(celebrate);
  add(director.Find(HugState(s)));
  // Not every goal goes to the fence: the approach is for the occasions the
  // rows themselves admit (their masks start at 0x7), and the seed decides
  // whether this one does.
  if (OccasionBit(s) >= 0x8 || (s.seed % 3) == 0)
    add(PickByPhase(director, Phase::Approach, s, "GOAL_A_"));
  add(PickByPhase(director, Phase::Finish, s, "GOAL_C_FINISH_SUCCESS"));
  return beats;
}

}  // namespace GoalDirector
