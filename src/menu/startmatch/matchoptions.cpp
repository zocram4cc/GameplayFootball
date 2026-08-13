// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#include "matchoptions.hpp"

#include <cmath>

#include "../pagefactory.hpp"
#include "main.hpp"
#include "onthepitch/matchduration.hpp"
#include "utils/localization.hpp"

using namespace blunted;

namespace {

constexpr unsigned long kMenuSmokeAdvanceDelay_ms = 250;

bool MenuSmokeQuickMatchEnabled() {
  return GetConfiguration()->GetBool("menu_smoke_test_quick_match", false);
}

bool MenuSmokeFullMatchEnabled() {
  return GetConfiguration()->GetBool("menu_smoke_test_full_match", false);
}

bool MenuSmokeAutoQuickMatchEnabled() {
  return MenuSmokeQuickMatchEnabled() || MenuSmokeFullMatchEnabled();
}

}  // namespace

namespace {

// Kits shipped per team: _kit_01 .. _kit_03.
const int kKitCount = 3;

std::string WeatherKey(float value) {
  if (value < 0.33f)
    return "weather_dry";
  if (value < 0.66f)
    return "weather_rain";
  return "weather_storm";
}

std::string TimeOfDayKey(float value) {
  if (value < 0.33f)
    return "time_of_day_day";
  if (value < 0.66f)
    return "time_of_day_evening";
  return "time_of_day_night";
}

int KitNumFromSlider(float value) {
  const int kitNum = 1 + static_cast<int>(std::round(value * (kKitCount - 1)));
  return std::max(1, std::min(kitNum, kKitCount));
}

}  // namespace

MatchOptionsPage::MatchOptionsPage(Gui2WindowManager* windowManager, const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData),
      buttonStart(nullptr),
      pageCreatedTime_ms(EnvironmentManager::GetInstance().GetTime_ms()),
      autoAdvanceTriggered(false) {
  Gui2Frame* frame = new Gui2Frame(windowManager, "matchoptions_frame", 25, 15, 50, 75, true);
  this->AddView(frame);
  frame->Show();

  Gui2Caption* header = new Gui2Caption(windowManager, "matchoptions_caption", 2, 2, 46, 3,
                                        TR("match_options_title"));
  frame->AddView(header);
  header->Show();

  Gui2Grid* grid = new Gui2Grid(windowManager, "matchoptions_grid", 2, 10, 46, 60);

  difficultySlider = new Gui2Slider(windowManager, "matchoptions_slider_difficulty", 0, 0, 29, 6,
                                    TR("match_difficulty"));
  matchDurationSlider = new Gui2Slider(windowManager, "matchoptions_slider_matchduration", 0, 0, 29,
                                       6, TR("match_duration"));
  matchDurationSlider->SetQuantization(kMatchDurationSliderSteps);
  buttonStart =
      new Gui2Button(windowManager, "matchoptions_button_start", 0, 0, 29, 3, TR("match_start"));
  Gui2Button* buttonBack = new Gui2Button(windowManager, "matchoptions_button_back", 0, 0, 29, 3,
                                          Localization::GetInstance().Translate("action_back"));

  float difficulty = GetConfiguration()->GetReal("match_difficulty", _default_Difficulty);
  float matchDurationMinutes = kDefaultMatchDurationMinutes;
  if (GetConfiguration()->Exists("match_duration_minutes")) {
    matchDurationMinutes =
        GetConfiguration()->GetReal("match_duration_minutes", kDefaultMatchDurationMinutes);
  } else {
    matchDurationMinutes = MatchDurationMinutesFromLegacySlider(
        GetConfiguration()->GetReal("match_duration", _default_MatchDuration));
  }
  difficultySlider->SetValue(difficulty);
  matchDurationSlider->SetValue(MatchDurationSliderFromMinutes(matchDurationMinutes));
  UpdateMatchDurationCaption();
  matchDurationSlider->sig_OnChange.connect([this](Gui2Slider*) { UpdateMatchDurationCaption(); });

  // Conditions for the coming match.
  weatherSlider = new Gui2Slider(windowManager, "matchoptions_slider_weather", 0, 0, 29, 6,
                                 TR("settings_weather"));
  weatherSlider->SetQuantization(3);
  weatherSlider->SetValue(GetConfiguration()->GetReal("match_weather", 0.0f));
  UpdateWeatherCaption();
  weatherSlider->sig_OnChange.connect([this](Gui2Slider*) { UpdateWeatherCaption(); });

  timeOfDaySlider = new Gui2Slider(windowManager, "matchoptions_slider_timeofday", 0, 0, 29, 6,
                                   TR("match_time_of_day"));
  timeOfDaySlider->SetQuantization(3);
  timeOfDaySlider->SetValue(GetConfiguration()->GetReal("match_time_of_day", 0.0f));
  UpdateTimeOfDayCaption();
  timeOfDaySlider->sig_OnChange.connect([this](Gui2Slider*) { UpdateTimeOfDayCaption(); });

  for (int teamID = 0; teamID < 2; teamID++) {
    kitSlider[teamID] = new Gui2Slider(
        windowManager, "matchoptions_slider_kit_" + int_to_str(teamID), 0, 0, 29, 6, "");
    kitSlider[teamID]->SetQuantization(kKitCount);
    kitSlider[teamID]->SetValue(
        GetConfiguration()->GetReal(("team" + int_to_str(teamID + 1) + "_kit").c_str(), 0.0f));
    kitSlider[teamID]->sig_OnChange.connect([this](Gui2Slider*) { UpdateKitCaptions(); });
  }
  UpdateKitCaptions();

  // Tactics for the coming match: the same game plan screen used in-match.
  Gui2Button* buttonGamePlan1 =
      new Gui2Button(windowManager, "matchoptions_button_gameplan_1", 0, 0, 29, 3,
                     TR("gameplan_header") + " 1");
  Gui2Button* buttonGamePlan2 =
      new Gui2Button(windowManager, "matchoptions_button_gameplan_2", 0, 0, 29, 3,
                     TR("gameplan_header") + " 2");
  buttonGamePlan1->sig_OnClick.connect([this](...) { GoGamePlan(0); });
  buttonGamePlan2->sig_OnClick.connect([this](...) { GoGamePlan(1); });

  int row = 0;
  grid->AddView(difficultySlider, row++, 0);
  grid->AddView(matchDurationSlider, row++, 0);
  grid->AddView(weatherSlider, row++, 0);
  grid->AddView(timeOfDaySlider, row++, 0);
  grid->AddView(kitSlider[0], row++, 0);
  grid->AddView(kitSlider[1], row++, 0);
  grid->AddView(buttonGamePlan1, row++, 0);
  grid->AddView(buttonGamePlan2, row++, 0);
  grid->AddView(buttonStart, row++, 0);
  grid->AddView(buttonBack, row++, 0);
  grid->UpdateLayout(0.5);

  frame->AddView(grid);
  grid->Show();

  buttonStart->sig_OnClick.connect([this](...) { GoLoadingMatchPage(); });
  buttonBack->sig_OnClick.connect([this](...) { GoBack(); });

  buttonStart->SetFocus();

  this->Show();
}

MatchOptionsPage::~MatchOptionsPage() {}

void MatchOptionsPage::UpdateWeatherCaption() {
  weatherSlider->SetCaption(TR("settings_weather") + ": " +
                            TR(WeatherKey(weatherSlider->GetValue())));
}

void MatchOptionsPage::UpdateTimeOfDayCaption() {
  timeOfDaySlider->SetCaption(TR("match_time_of_day") + ": " +
                              TR(TimeOfDayKey(timeOfDaySlider->GetValue())));
}

void MatchOptionsPage::UpdateKitCaptions() {
  for (int teamID = 0; teamID < 2; teamID++) {
    const int kitNum = KitNumFromSlider(kitSlider[teamID]->GetValue());
    kitSlider[teamID]->SetCaption(TRF("match_team_kit", {std::to_string(teamID + 1),
                                                        std::to_string(kitNum)}));
  }
}

void MatchOptionsPage::GoGamePlan(int teamID) {
  Properties properties;
  properties.Set("teamID", teamID);
  // No match exists yet, so the page loads the team from the database itself.
  properties.SetInt("teamDatabaseID", GetMenuTask()->GetTeamID(teamID));
  CreatePage(e_PageID_GamePlan, properties);
}

void MatchOptionsPage::UpdateMatchDurationCaption() {
  const int minutes =
      static_cast<int>(std::round(MatchDurationMinutesFromSlider(matchDurationSlider->GetValue())));
  matchDurationSlider->SetCaption(TRF("match_duration_minutes", {std::to_string(minutes)}));
}

void MatchOptionsPage::Process() {
  Gui2Page::Process();

  if (!autoAdvanceTriggered && MenuSmokeAutoQuickMatchEnabled() &&
      EnvironmentManager::GetInstance().GetTime_ms() >=
          pageCreatedTime_ms + kMenuSmokeAdvanceDelay_ms) {
    autoAdvanceTriggered = true;
    printf("[menu-smoke] Match options confirmed, starting match load\n");
    GoLoadingMatchPage();
  }
}

void MatchOptionsPage::GoLoadingMatchPage() {
  GetConfiguration()->Set("match_weather", weatherSlider->GetValue());
  GetConfiguration()->Set("match_time_of_day", timeOfDaySlider->GetValue());
  for (int teamID = 0; teamID < 2; teamID++) {
    GetConfiguration()->SetInt(("team" + int_to_str(teamID + 1) + "_kit_num").c_str(),
                               KitNumFromSlider(kitSlider[teamID]->GetValue()));
    GetConfiguration()->Set(("team" + int_to_str(teamID + 1) + "_kit").c_str(),
                            static_cast<float>(kitSlider[teamID]->GetValue()));
  }
  GetConfiguration()->Set("match_difficulty", difficultySlider->GetValue());
  GetConfiguration()->Set("match_duration_minutes",
                          MatchDurationMinutesFromSlider(matchDurationSlider->GetValue()));
  GetConfiguration()->SaveFile(GetConfigFilename());

  this->Exit();

  Properties properties;
  windowManager->GetPageFactory()->CreatePage((int)e_PageID_LoadingMatch, properties, 0);

  delete this;
}
