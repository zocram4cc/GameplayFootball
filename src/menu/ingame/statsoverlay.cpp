#include "statsoverlay.hpp"

#include <algorithm>

#include "../../data/matchanalytics.hpp"
#include "../../onthepitch/match.hpp"
#include "utils/localization.hpp"
#include "utils/gui2/windowmanager.hpp"

using namespace blunted;

Gui2StatsOverlay::Gui2StatsOverlay(Gui2WindowManager* windowManager, Match* match)
    : Gui2View(windowManager, "statsoverlay", 2, 6, 96, 26), match(match) {
  Vector3 textColor(220, 220, 220);
  Vector3 outlineColor(0, 0, 0);

  Gui2Frame* bg = new Gui2Frame(windowManager, "frame_statsoverlay_bg", 0, 0, 96, 26, true);
  this->AddView(bg);
  bg->Show();

  possessionCaption =
      new Gui2Caption(windowManager, "stats_possession", 1, 0, 94, 3, "");
  shotsCaption =
      new Gui2Caption(windowManager, "stats_shots", 1, 3, 94, 3, "");
  passCaption =
      new Gui2Caption(windowManager, "stats_passes", 1, 6, 94, 3, "");
  foulsCaption =
      new Gui2Caption(windowManager, "stats_fouls", 1, 9, 94, 3, "");

  expectedGoalsCaption = new Gui2Caption(windowManager, "stats_xg", 1, 12, 94, 3, "");

  // The heatmap is drawn as four rows of block characters, one per pitch band.
  for (int i = 0; i < 4; i++) {
    heatmapCaption[i] = new Gui2Caption(windowManager, "stats_heatmap_" + int_to_str(i), 1,
                                       15.0f + i * 2.5f, 94, 2.5f, "");
  }

  for (Gui2Caption* cap : {possessionCaption, shotsCaption, passCaption, foulsCaption,
                           expectedGoalsCaption, heatmapCaption[0], heatmapCaption[1],
                           heatmapCaption[2], heatmapCaption[3]}) {
    cap->SetColor(textColor);
    cap->SetOutlineColor(outlineColor);
    this->AddView(cap);
    cap->Show();
  }
}

void Gui2StatsOverlay::UpdateStats() {
  MatchData* md = match->GetMatchData();

  float poss1 = md->GetPossessionTime_ms(0);
  float poss2 = md->GetPossessionTime_ms(1);
  float total = poss1 + poss2;
  int pct1 = (total > 0) ? int(round(poss1 / total * 100)) : 50;
  int pct2 = 100 - pct1;

  possessionCaption->SetCaption(int_to_str(pct1) + "% | possession | " + int_to_str(pct2) + "%");

  int shots1 = md->GetShots(0);
  int shots2 = md->GetShots(1);
  int sot1 = md->GetShotsOnTarget(0);
  int sot2 = md->GetShotsOnTarget(1);
  shotsCaption->SetCaption(int_to_str(sot1) + "/" + int_to_str(shots1) +
                           " | shots on target | " +
                           int_to_str(sot2) + "/" + int_to_str(shots2));

  int pass1 = md->GetPassAttempts(0);
  int pass2 = md->GetPassAttempts(1);
  int comp1 = md->GetPassesCompleted(0);
  int comp2 = md->GetPassesCompleted(1);
  int pacc1 = (pass1 > 0) ? int(round(comp1 * 100.0f / pass1)) : 0;
  int pacc2 = (pass2 > 0) ? int(round(comp2 * 100.0f / pass2)) : 0;
  passCaption->SetCaption(int_to_str(pacc1) + "% | pass accuracy | " + int_to_str(pacc2) + "%");

  int fouls1 = md->GetFouls(0);
  int fouls2 = md->GetFouls(1);
  foulsCaption->SetCaption(int_to_str(fouls1) + " | fouls | " + int_to_str(fouls2));

  // Expected goals: how good the chances actually were.
  const MatchAnalytics::ShotTally& tally = match->GetShotTally();
  expectedGoalsCaption->SetCaption(
      real_to_str(round(MatchAnalytics::GetExpectedGoals(tally, 0) * 100.0f) / 100.0f) + " | " +
      Localization::GetInstance().Translate("stats_expected_goals") + " | " +
      real_to_str(round(MatchAnalytics::GetExpectedGoals(tally, 1) * 100.0f) / 100.0f));

  // Ball heatmap, condensed from the sampled grid into four rows of intensity.
  const MatchAnalytics::Heatmap& heatmap = match->GetBallHeatmap();
  const char* intensityChars[5] = {".", ":", "+", "*", "#"};
  const int rowsPerBand = MatchAnalytics::Heatmap::cellsY / 4;
  for (int band = 0; band < 4; band++) {
    std::string row;
    for (int x = 0; x < MatchAnalytics::Heatmap::cellsX; x++) {
      float intensity = 0.0f;
      for (int y = band * rowsPerBand; y < (band + 1) * rowsPerBand; y++)
        intensity = std::max(intensity, MatchAnalytics::GetNormalizedIntensity(heatmap, x, y));
      const int level = std::min(static_cast<int>(intensity * 5.0f), 4);
      row += intensityChars[level];
      row += " ";
    }
    heatmapCaption[band]->SetCaption(
        band == 0 ? Localization::GetInstance().Translate("stats_ball_heatmap") + "  " + row : row);
  }
}
