#include "gdext/gf_match.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "data/matchdata.hpp"
#include "gametypes.hpp"
#include "onthepitch/match.hpp"
#include "onthepitch/matchsetup.hpp"
#include "onthepitch/player/player.hpp"
#include "simcontext.hpp"
#include "utils/database.hpp"

namespace godot {
namespace {

constexpr int TEAMS = 2;
constexpr float TICK_S = 0.01f;  // GF's 10 ms step (Match::kTick_ms)

// GF is Z up with the pitch in XY; Fox/Godot is Y up: (x, y, z) -> (x, z, -y).
Vector3 ToFox(const blunted::Vector3& v) { return Vector3(v.coords[0], v.coords[2], -v.coords[1]); }

}  // namespace

GfMatch::GfMatch() = default;
GfMatch::~GfMatch() = default;

void GfMatch::_bind_methods() {
  ClassDB::bind_method(D_METHOD("start", "library", "db_sql", "anims_dir", "home", "away", "seed", "minutes"),
                       &GfMatch::start);
  ClassDB::bind_method(D_METHOD("step"), &GfMatch::step);
  ClassDB::bind_method(D_METHOD("is_over"), &GfMatch::is_over);
  ClassDB::bind_method(D_METHOD("player_count"), &GfMatch::player_count);
  ClassDB::bind_method(D_METHOD("player_position", "index"), &GfMatch::player_position);
  ClassDB::bind_method(D_METHOD("player_facing", "index"), &GfMatch::player_facing);
  ClassDB::bind_method(D_METHOD("player_velocity", "index"), &GfMatch::player_velocity);
  ClassDB::bind_method(D_METHOD("player_team", "index"), &GfMatch::player_team);
  ClassDB::bind_method(D_METHOD("player_database_id", "index"), &GfMatch::player_database_id);
  ClassDB::bind_method(D_METHOD("ball_position"), &GfMatch::ball_position);
  ClassDB::bind_method(D_METHOD("match_time_ms"), &GfMatch::match_time_ms);
  ClassDB::bind_method(D_METHOD("score", "team"), &GfMatch::score);
  ClassDB::bind_method(D_METHOD("field_half_width"), &GfMatch::field_half_width);
  ClassDB::bind_method(D_METHOD("field_half_length"), &GfMatch::field_half_length);
}

Error GfMatch::start(const Ref<PesLibrary>& library, const String& db_sql, const String& anims_dir, int home,
                     int away, int seed, int minutes) {
  if (library.is_null() || !library->rig()) {
    UtilityFunctions::push_error("GfMatch: library is not open");
    return ERR_UNCONFIGURED;
  }
  library_ = library;
  match_.reset();
  db_ = std::make_unique<blunted::Database>();
  if (!db_->LoadSqlScript(FromGd(db_sql))) {
    UtilityFunctions::push_error("GfMatch: cannot load ", db_sql);
    return ERR_FILE_CANT_OPEN;
  }
  SetDB(db_.get());
  MatchSetup setup;
  setup.homeTeamDbId = home;
  setup.awayTeamDbId = away;
  setup.seed = unsigned(seed);
  setup.durationMinutes = minutes;
  setup.animationDir = FromGd(anims_dir);
  setup.rig = library->rig();
  // PES's match clock: its constants come from the install's own exe.
  setup.pesClockConstants = library->clock_constants();
  setup.pesBallConstants = library->ball_constants();
  setup.pesJudgeConstants = library->judge_constants();
  setup.pesRngConstants = library->rng_constants();
  setup.pesKeeperConstants = library->keeper_constants();
  setup.pesStaminaConstants = library->stamina_constants();
  setup.pesStoppageConstants = library->stoppage_constants();
  setup.pesKickErrorConstants = library->kick_error_constants();
  setup.pesCarrierConstants = library->carrier_constants();
  setup.pesSetPieceTakerConstants = library->setpiece_taker_constants();
  setup.pesSetPieceTeamConstants = library->setpiece_team_constants();
  setup.pesDefensiveShapeConstants = library->defensive_shape_constants();
  setup.pesDefenderDecisionConstants = library->defender_decision_constants();
  setup.pesContactConstants = library->contact_constants();
  setup.pesClips = library->pes_clips();
  setup.pesStrategyConstants = library->strategy_constants();
  setup.pesTeamConceptConstants = library->team_concept_constants();
  setup.pesSubstitutionConstants = library->substitution_constants();
  match_ = std::make_unique<Match>(new MatchData(home, away), setup, Properties());  // owns the MatchData
  RefreshPlayers();
  return OK;
}

void GfMatch::RefreshPlayers() {
  players_.clear();
  teamOf_.clear();
  for (int t = 0; t < TEAMS; ++t) {
    std::vector<Player*> team;
    match_->GetActiveTeamPlayers(t, team);
    for (Player* p : team) players_.push_back(p), teamOf_.push_back(t);
  }
}

void GfMatch::step() {
  if (!match_ || match_->IsGameOver()) return;
  SetDB(db_.get());  // the engine reads the global database; another match may have set it
  match_->Process();
  RefreshPlayers();  // substitutions change the active set
}

bool GfMatch::is_over() const { return !match_ || match_->IsGameOver(); }
int GfMatch::player_count() const { return int(players_.size()); }

Vector3 GfMatch::player_position(int i) const {
  return i >= 0 && i < player_count() ? ToFox(players_[i]->GetPosition()) : Vector3();
}

Vector3 GfMatch::player_facing(int i) const {
  return i >= 0 && i < player_count() ? ToFox(players_[i]->GetBodyDirectionVec()).normalized() : Vector3();
}

Vector3 GfMatch::player_velocity(int i) const {
  // GetMovement() is the displacement per tick.
  return i >= 0 && i < player_count() ? ToFox(players_[i]->GetMovement()) / TICK_S : Vector3();
}

int GfMatch::player_team(int i) const { return i >= 0 && i < player_count() ? teamOf_[i] : -1; }

int GfMatch::player_database_id(int i) const {
  return i >= 0 && i < player_count() ? players_[i]->GetPlayerData()->GetDatabaseID() : -1;
}

Vector3 GfMatch::ball_position() const { return match_ ? ToFox(match_->GetBall()->Predict(0)) : Vector3(); }
int GfMatch::match_time_ms() const { return match_ ? int(match_->GetMatchTime_ms()) : 0; }
int GfMatch::score(int team) const { return match_ && (team == 0 || team == 1) ? match_->GetScore(team) : 0; }

double GfMatch::field_half_width() const { return pitchHalfH + lineHalfW; }
double GfMatch::field_half_length() const { return pitchHalfW + lineHalfW; }

}  // namespace godot
