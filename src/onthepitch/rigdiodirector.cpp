#include "rigdiodirector.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <random>

#include "base/log.hpp"
#include "base/utils.hpp"
#include "coachmode.hpp"
#include "main.hpp"
#include "managers/environmentmanager.hpp"
#include "managers/resourcemanagerpool.hpp"
#include "managers/usereventmanager.hpp"
#include "match.hpp"
#include "player/player.hpp"
#include "team.hpp"
#include "scene/objectfactory.hpp"

using namespace blunted;

namespace {

namespace fs = std::filesystem;

constexpr float kFadeSeconds = 2.0f;       // config.py fade time
constexpr float kChantTimerSeconds = 30.0f;  // chantswindow.py default
constexpr double kNormalizeTargetDb = -14.0; // config.py level target
constexpr double kLouderBoostDb = 5.0;       // songgui.py boostValue

std::string LowerStr(std::string s) {
  for (char& c : s)
    if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
  return s;
}

// install_team.py art_tag(): "/hdg/" -> "hdg", "2HUG" -> "2hug".
std::string ArtTag(const std::string& name) {
  std::string tag;
  for (char c : LowerStr(name))
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) tag += c;
  return tag.empty() ? "team" : tag;
}

// Where the music actually is. "rigdio_dir" is data-relative like every other
// asset path, but the engine does not always run with data/ as its working
// directory - a config given on the command line leaves it at the repo root
// (main.cpp AnchorWorkingDirectory), and every match played there found no
// export for either side and went silent: no anthems, no goal horns, no
// victory. Looked for beside the working directory and under data/, which is
// the same pair ResolveConfigFilename uses.
fs::path ResolveMusicDir(const std::string& configured) {
  std::error_code ec;
  // Whichever of these actually holds a team's export, tried in this order,
  // and reported absolutely so a silent match says where it looked.
  // A build tree runs with build/ as its working directory and reaches its
  // assets through the media symlink cmake/link_media.cmake makes, so the data
  // root is that link's target - which is how "music" was never found in any
  // match ever played from build/ (measured 06-09: silent anthems and no goal
  // horn in every showcase recorded).
  fs::path viaMedia;
  {
    const fs::path media = fs::weakly_canonical(fs::path("media"), ec);
    if (!ec && !media.empty()) viaMedia = media.parent_path() / configured;
  }
  for (const fs::path& candidate :
       {fs::path(configured), fs::path("data") / configured, viaMedia,
        fs::current_path(ec) / configured,
        fs::current_path(ec) / "data" / configured}) {
    if (candidate.empty() || !fs::is_directory(candidate, ec)) continue;
    bool anyExport = false;
    for (fs::recursive_directory_iterator it(candidate, ec), end;
         it != end && !anyExport; it.increment(ec)) {
      if (ec) break;
      if (it->path().extension() == ".4ccm") anyExport = true;
    }
    if (anyExport) return fs::absolute(candidate, ec);
  }
  return fs::absolute(fs::path(configured), ec);
}

// The team's .4ccm under <dir>/<tag>/: <tag>.4ccm preferred, else the
// lexicographically last (dbgvgl26 beats dbgvgl25).
fs::path FindFourccm(const fs::path& teamDir, const std::string& tag) {
  fs::path best;
  std::error_code ec;
  for (fs::recursive_directory_iterator it(teamDir, ec), end; it != end;
       it.increment(ec)) {
    if (ec) break;
    if (it->path().extension() != ".4ccm") continue;
    if (it->path().filename() == tag + ".4ccm") return it->path();
    if (best.empty() || it->path().filename().string() > best.filename().string())
      best = it->path();
  }
  return best;
}

bool DecodableExtension(const std::string& file) {
  const size_t dot = file.rfind('.');
  if (dot == std::string::npos) return false;
  const std::string ext = LowerStr(file.substr(dot + 1));
  return ext == "wav" || ext == "ogg" || ext == "mp3";
}

// Loudness over 16-bit PCM: whole-track RMS and peak, plus the RMS of the
// loudest 20% of one-second windows for chants (legacy.py analysis).
struct Loudness {
  double meanDb = 0.0, peakDb = 0.0, loudPartDb = 0.0;
  bool ok = false;
};

Loudness Analyze(const WavData* wav) {
  Loudness out;
  if (!wav || wav->bits != 16 || wav->size < 2) return out;
  const int16_t* samples = reinterpret_cast<const int16_t*>(wav->data);
  const size_t count = wav->size / 2;
  const size_t window = std::max<size_t>(1, (size_t)wav->frequency * wav->channels);
  double totalSq = 0.0;
  int peak = 0;
  std::vector<std::pair<double, size_t>> windows;  // (sumSq, n)
  for (size_t start = 0; start < count; start += window) {
    const size_t n = std::min(window, count - start);
    double sq = 0.0;
    for (size_t i = start; i < start + n; i++) {
      const int v = samples[i];
      sq += (double)v * v;
      peak = std::max(peak, v < 0 ? -v : v);
    }
    windows.push_back({sq, n});
    totalSq += sq;
  }
  if (totalSq <= 0.0 || peak == 0) return out;
  const double meanRms = std::sqrt(totalSq / count) / 32768.0;
  out.meanDb = 20.0 * std::log10(meanRms);
  out.peakDb = 20.0 * std::log10(peak / 32768.0);
  std::sort(windows.begin(), windows.end(),
            [](const auto& a, const auto& b) { return a.first / a.second > b.first / b.second; });
  const size_t nLoud = std::max<size_t>(1, windows.size() / 5);
  double loudSq = 0.0, loudN = 0.0;
  for (size_t i = 0; i < nLoud; i++) {
    loudSq += windows[i].first;
    loudN += windows[i].second;
  }
  const double loudRms = loudN > 0 ? std::sqrt(loudSq / loudN) / 32768.0 : meanRms;
  out.loudPartDb = loudRms > 0 ? 20.0 * std::log10(loudRms) : out.meanDb;
  out.ok = true;
  return out;
}

// Bakes the normalization gain into the PCM, capped so the peak stays at
// full scale (divergence #3: cap instead of rigdio's limiter).
void BakeGain(WavData* wav, double gainDb) {
  const double gain = std::pow(10.0, gainDb / 20.0);
  int16_t* samples = reinterpret_cast<int16_t*>(wav->data);
  const size_t count = wav->size / 2;
  for (size_t i = 0; i < count; i++) {
    const double v = samples[i] * gain;
    samples[i] = (int16_t)std::max(-32768.0, std::min(32767.0, v));
  }
}

}  // namespace

RigdioDirector::RigdioDirector(Match* match) : match_(match) {
  if (!GetConfiguration()->GetBool("rigdio_enabled", true)) return;
  const fs::path musicDir = ResolveMusicDir(GetConfiguration()->Get("rigdio_dir", "music"));
  const std::string gametype =
      GetConfiguration()->Get("rigdio_gametype", "group");
  normalize_ = GetConfiguration()->GetBool("rigdio_normalize", true);
  volume_ = GetConfiguration()->GetReal("rigdio_volume", 0.8f);

  rigdio::TeamMusic teams[2];
  bool any = false;
  for (int t = 0; t < 2; t++) {
    const std::string teamName = match_->GetMatchData()->GetTeamData(t)->GetName();
    const std::string tag = ArtTag(teamName);
    const fs::path teamDir = musicDir / tag;
    const fs::path fourccm = FindFourccm(teamDir, tag);
    if (fourccm.empty()) {
      Log(e_Notice, "RigdioDirector", "RigdioDirector",
          "no music export for /" + tag + "/ under " + teamDir.string());
      continue;
    }
    std::ifstream in(fourccm);
    const std::string text((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    rigdio::ParseResult parsed = rigdio::Parse(text, fourccm.stem().string());
    if (!parsed.ok) {
      // rigdio refuses the file the same way; say why and play nothing.
      Log(e_Warning, "RigdioDirector", "RigdioDirector",
          fourccm.string() + " failed to load (as it would in rigdio): " + parsed.error);
      continue;
    }
    // rigparse resolves every filename at load (songCheck); missing files
    // are reported like rigdio's FileNotFoundError, but the team still
    // plays (divergence #1).
    exportDir_[t] = fourccm.parent_path().string() + "/";
    std::vector<std::string> listing;
    for (const auto& e : fs::directory_iterator(fourccm.parent_path()))
      listing.push_back(e.path().filename().string());
    std::string missing;
    auto resolveAll = [&](std::vector<rigdio::Entry>& entries) {
      for (rigdio::Entry& e : entries) {
        const bool exact = fs::exists(fourccm.parent_path() / e.file);
        e.file = rigdio::SongCheck(e.file, exact, listing, parsed.team.normalize);
        if (!fs::exists(fourccm.parent_path() / e.file))
          missing += (missing.empty() ? "" : ", ") + e.file;
      }
    };
    for (auto& kv : parsed.team.players) resolveAll(kv.second);
    for (auto& kv : parsed.team.events) resolveAll(kv.second);
    if (!missing.empty())
      Log(e_Warning, "RigdioDirector", "RigdioDirector",
          "missing files for /" + parsed.team.tname + "/ (rigdio would refuse the export): " +
              missing);
    teams[t] = std::move(parsed.team);
    any = true;
    Log(e_Notice, "RigdioDirector", "RigdioDirector",
        "loaded /" + teams[t].tname + "/ from " + fourccm.string() + ": " +
            int_to_str((int)teams[t].players.size()) + " player keys");
  }
  if (!any) return;

  auto rngEngine = std::make_shared<std::mt19937>(std::random_device{}());
  rigdio::Rng rng = [rngEngine](int n) {
    return n <= 1 ? 0 : (int)((*rngEngine)() % (unsigned)n);
  };
  session_ = std::make_unique<rigdio::MatchSession>(teams[0], teams[1],
                                                    LowerStr(gametype), rng);
}

RigdioDirector::~RigdioDirector() {}

double RigdioDirector::NowSeconds() const {
  return EnvironmentManager::GetInstance().GetTime_ms() / 1000.0;
}

RigdioDirector::Track* RigdioDirector::Load(bool home, const std::string& file,
                                            bool louder) {
  const std::string path = exportDir_[home ? 0 : 1] + file;
  auto it = tracks_.find(path);
  if (it != tracks_.end()) return it->second.usable ? &it->second : nullptr;
  Track& track = tracks_[path];

  if (!DecodableExtension(file) || !fs::exists(path)) {
    Log(e_Warning, "RigdioDirector", "Load", "cannot play " + path +
        (DecodableExtension(file) ? " (missing)" : " (undecodable format)"));
    return nullptr;
  }

  bool alreadyThere = false;
  boost::intrusive_ptr<Resource<SoundBuffer>> buffer =
      ResourceManagerPool::GetInstance()
          .GetManager<SoundBuffer>(e_ResourceType_SoundBuffer)
          ->Fetch(path, true, alreadyThere, true);
  // GetData is const; the bake below runs before the PCM is handed to
  // OpenAL and only on a freshly loaded buffer that nobody else holds.
  WavData* wav = const_cast<WavData*>(buffer->GetResource()->GetData());
  if (!wav || wav->size <= 0) return nullptr;

  // Normalize before the PCM reaches OpenAL: gain toward -14 dBFS mean
  // (loud-part RMS for chants), capped at full-scale peak; louder-marked
  // tracks get rigdio's default +5 dB boost on top.
  if (normalize_ && !alreadyThere) {
    const Loudness loud = Analyze(wav);
    if (loud.ok) {
      const bool isChant = file.find("chant") != std::string::npos;
      const double reference = isChant ? loud.loudPartDb : loud.meanDb;
      double gainDb = kNormalizeTargetDb - reference + (louder ? kLouderBoostDb : 0.0);
      gainDb = std::min(gainDb, -loud.peakDb);  // never clip
      if (std::fabs(gainDb) > 0.1) BakeGain(wav, gainDb);
      Log(e_Notice, "RigdioDirector", "Load",
          path + ": mean " + real_to_str(loud.meanDb) + " dB, peak " +
              real_to_str(loud.peakDb) + " dB, gain " + real_to_str(gainDb) + " dB");
    }
  }

  track.durationSeconds =
      (double)wav->size / (wav->channels * 2) / (double)wav->frequency;
  track.sound = boost::static_pointer_cast<Sound>(
      ObjectFactory::GetInstance().CreateObject("rigdio|" + path, e_ObjectType_Sound));
  GetScene3D()->CreateSystemObjects(track.sound);
  track.sound->SetSoundBuffer(buffer);
  track.sound->SetGain(0.0f);
  track.sound->SetLoop(false);
  GetScene3D()->AddObject(track.sound);
  track.usable = true;
  return &track;
}

void RigdioDirector::Start(Channel& ch, bool home, const rigdio::PlayAction& act) {
  // Silence whatever the channel was doing.
  if (ch.active && ch.track && ch.track->sound) ch.track->sound->Pause();
  ch = Channel();

  Track* track = Load(home, act.file, act.louder);
  if (!track) return;
  session_->SetDuration(home, act.file, track->durationSeconds);

  ch.track = track;
  ch.action = act;
  ch.active = true;
  ch.startedWall_ms = EnvironmentManager::GetInstance().GetTime_ms();
  ch.gain = ch.target = volume_;

  track->sound->SetLoop(act.loop);
  track->sound->SetPitch((float)act.speed);
  track->sound->SetGain(volume_);
  track->sound->Seek((float)act.seekSeconds);
  track->sound->Poke(e_SystemType_Audio);
  Log(e_Notice, "RigdioDirector", "Start",
      std::string("rigdio: ") + (home ? "home" : "away") + " " + act.pname + " -> " +
          act.file + " at " + real_to_str((float)act.seekSeconds) + "s" +
          (act.loop ? " (loop)" : ""));
}

void RigdioDirector::FadeOut(Channel& ch) {
  if (ch.active) ch.target = 0.0f;
}

bool RigdioDirector::AtEnd(const Channel& ch, unsigned long now_ms) const {
  if (!ch.active || !ch.track || ch.action.loop) return false;
  const double played = (now_ms - ch.startedWall_ms) / 1000.0 * ch.action.speed;
  return ch.action.seekSeconds + played >= ch.track->durationSeconds;
}

void RigdioDirector::Advance(Channel& ch, float dt) {
  if (!ch.active || !ch.track) return;
  if (ch.gain != ch.target) {
    const float step = dt / kFadeSeconds * volume_;
    ch.gain = ch.gain < ch.target ? std::min(ch.gain + step, ch.target)
                                  : std::max(ch.gain - step, ch.target);
    ch.track->sound->SetGain(ch.gain);
  }
  if (ch.gain <= 0.0f && ch.target <= 0.0f) {
    ch.track->sound->Pause();
    if (ch.chant) session_->ChantEnded();
    ch.active = false;
  }
}

void RigdioDirector::Update() {
  if (!session_) return;
  const unsigned long now_ms = EnvironmentManager::GetInstance().GetTime_ms();
  const float dt = lastWall_ms == 0 ? 0.0f : (now_ms - lastWall_ms) / 1000.0f;
  lastWall_ms = now_ms;
  const double nowSec = now_ms / 1000.0;

  // --- the entrance: away anthem first, then home (songgui away-hook) ---
  const bool inEntrance = match_->IsInEntrance();
  const float elapsed = match_->GetEntranceElapsedSeconds();
  const float total = match_->GetEntranceTotalSeconds();
  switch (entrance_) {
    case EntrancePhase::Idle:
      if (inEntrance && elapsed > 0.0f) {
        auto act = session_->Anthem(false, nowSec);
        if (act) Start(anthem_[1], false, *act);
        entrance_ = EntrancePhase::Away;
      } else if (!inEntrance && match_->GetActualTime_ms() > 0) {
        entrance_ = EntrancePhase::Done;  // no entrance configured
      }
      break;
    case EntrancePhase::Away:
      // Back to back, as rigdio plays them: the away anthem runs to its own
      // end and only yields early when it is longer than the walkout can hold
      // (rigdio::AnthemHandsOver).
      if (!inEntrance ||
          rigdio::AnthemHandsOver(
              anthem_[1].track ? (now_ms - anthem_[1].startedWall_ms) / 1000.0 : 0.0,
              anthem_[1].track ? anthem_[1].track->durationSeconds : 0.0, elapsed, total,
              kFadeSeconds)) {
        FadeOut(anthem_[1]);
        entrance_ = inEntrance ? EntrancePhase::AwayFading : EntrancePhase::Done;
      }
      break;
    case EntrancePhase::AwayFading:
      if (!inEntrance) {
        entrance_ = EntrancePhase::Done;
      } else if (!anthem_[1].active) {
        auto act = session_->Anthem(true, nowSec);
        if (act) Start(anthem_[0], true, *act);
        entrance_ = EntrancePhase::Home;
      }
      break;
    case EntrancePhase::Home:
      if (!inEntrance) {
        FadeOut(anthem_[0]);
        entrance_ = EntrancePhase::Done;
      }
      break;
    case EntrancePhase::Done:
      break;
  }
  if (entrance_ == EntrancePhase::Done) {
    // Anything still ringing after the walkout fades away.
    if (anthem_[0].active && anthem_[0].target > 0.0f) FadeOut(anthem_[0]);
    if (anthem_[1].active && anthem_[1].target > 0.0f) FadeOut(anthem_[1]);
  }

  // --- goals: the scorer's music, own goals as rigdio events ---
  const int goals = match_->GetMatchData()->GetGoalCount(0) +
                    match_->GetMatchData()->GetGoalCount(1);
  if (goals > seenGoals_) {
    seenGoals_ = goals;
    Player* scorer = match_->GetLastGoalScorer();
    const int teamID = match_->GetLastGoalTeamID();
    if (scorer && (teamID == 0 || teamID == 1)) {
      const std::string name = scorer->GetPlayerData()
                                   ? scorer->GetPlayerData()->GetLastName()
                                   : std::string();
      const int minute = (int)(match_->GetMatchTime_ms() / 60000);
      const bool ownGoal = scorer->GetTeamID() != teamID;
      Log(e_Notice, "RigdioDirector", "Update",
          std::string("rigdio: ") + (ownGoal ? "own goal" : "goal") + " by " + name +
              " for the " + (teamID == 0 ? "home" : "away") + " team, minute " +
              int_to_str(minute));
      if (ownGoal) {
        // rigdio never plays goal music for an own goal; the owngoal event
        // clip of the player's own team fires instead.
        auto act = session_->OnEvent(scorer->GetTeamID() == 0, "owngoal", name, minute);
        if (act) Start(event_, scorer->GetTeamID() == 0, *act);
      } else {
        auto act = session_->OnGoal(teamID == 0, name, minute, nowSec);
        if (act) Start(horn_[teamID], teamID == 0, *act);
      }
    }
  }

  // --- kickoff: pause the horn, remembering where it was (sync) ---
  for (int t = 0; t < 2; t++) {
    if (horn_[t].active && horn_[t].target > 0.0f && !match_->IsGoalScored()) {
      session_->OnHornPaused(t == 0, nowSec);
      FadeOut(horn_[t]);
    }
  }

  // --- cards and substitutions become rigdio events (event.py) ---
  if (!match_->IsInEntrance()) {
    const int minute = (int)(match_->GetMatchTime_ms() / 60000);
    for (int t = 0; t < 2; t++) {
      std::vector<Player*> active;
      match_->GetTeam(t)->GetActivePlayers(active);
      for (Player* p : active) {
        const std::string name = p->GetPlayerData()
                                     ? p->GetPlayerData()->GetLastName()
                                     : std::string();
        // A player first seen after the roster settled came off the bench.
        if (!knownPlayers_.count(p)) {
          knownPlayers_[p] = true;
          if (rosterSeeded_) {
            auto act = session_->OnEvent(t == 0, "sub", name, minute);
            if (act) Start(event_, t == 0, *act);
          }
        }
        // GiveYellowCard adds 1, GiveRedCard adds 3 (player.hpp).
        const int cards = p->GetCards();
        int& seen = seenCards_[p];
        if (rosterSeeded_ && cards > seen) {
          auto act = session_->OnEvent(t == 0, cards - seen >= 3 ? "red" : "yellow",
                                       name, minute);
          if (act) Start(event_, t == 0, *act);
        }
        seen = cards;
      }
    }
    rosterSeeded_ = true;
  }

  // --- coach-mode manual chants: Z home, X away; same key stops early ---
  if (CoachMode::IsCoachMode(match_->GetCoachSetup())) {
    UserEventManager& events = UserEventManager::GetInstance();
    const SDL_Keycode keys[2] = {SDLK_z, SDLK_x};
    for (int t = 0; t < 2; t++) {
      if (!events.GetKeyboardState(keys[t])) continue;
      events.SetKeyboardState(keys[t], false);
      const bool home = t == 0;
      if (chantCh_.active && chantCh_.action.home == home) {
        Log(e_Notice, "RigdioDirector", "Update", "rigdio: chant stopped early");
        FadeOut(chantCh_);
      } else {
        auto act = session_->Chant(home);
        if (act) {
          Start(chantCh_, home, *act);
          chantCh_.chant = true;
        } else if (session_->ChantActive()) {
          Log(e_Notice, "RigdioDirector", "Update",
              "rigdio: chant denied, one already playing");
        }
      }
    }
  }
  // The chant timer: long chants fade out on their own (30 s, as rigdio).
  if (chantCh_.active && chantCh_.target > 0.0f &&
      (now_ms - chantCh_.startedWall_ms) / 1000.0f > kChantTimerSeconds)
    FadeOut(chantCh_);

  // --- natural track ends: warcry chains, advance, end stop, chant over ---
  for (int t = 0; t < 2; t++) {
    if (AtEnd(horn_[t], now_ms)) {
      horn_[t].active = false;
      auto act = session_->OnHornEnded(t == 0, nowSec);
      if (act) Start(horn_[t], t == 0, *act);
    }
    if (AtEnd(anthem_[t], now_ms)) anthem_[t].active = false;
  }
  if (AtEnd(victory_, now_ms)) {
    victory_.active = false;
    auto act = session_->OnHornEnded(victory_.action.home, nowSec);
    if (act) Start(victory_, victory_.action.home, *act);
  }
  if (AtEnd(chantCh_, now_ms)) {
    chantCh_.active = false;
    session_->ChantEnded();
  }
  if (AtEnd(event_, now_ms)) event_.active = false;

  Advance(anthem_[0], dt);
  Advance(anthem_[1], dt);
  Advance(horn_[0], dt);
  Advance(horn_[1], dt);
  Advance(victory_, dt);
  Advance(chantCh_, dt);
  Advance(event_, dt);
}

void RigdioDirector::OnFullTime() {
  if (!session_ || victoryPlayed_) return;
  victoryPlayed_ = true;
  const int diff = match_->GetMatchData()->GetGoalCount(0) -
                   match_->GetMatchData()->GetGoalCount(1);
  if (diff == 0) return;  // a draw has no victor
  for (int t = 0; t < 2; t++) FadeOut(horn_[t]);
  const bool home = diff > 0;
  auto act = session_->Victory(home, NowSeconds());
  if (act) {
    Start(victory_, home, *act);
    Log(e_Notice, "RigdioDirector", "OnFullTime",
        std::string("rigdio: victory anthem for ") + (home ? "home" : "away"));
  }
}

void RigdioDirector::Exit() {
  for (auto& kv : tracks_) {
    if (kv.second.sound) {
      kv.second.sound->Pause();
      GetScene3D()->DeleteObject(kv.second.sound);
    }
  }
  tracks_.clear();
  session_.reset();
}
