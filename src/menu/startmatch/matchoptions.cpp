// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#include "matchoptions.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>

#include "../pagefactory.hpp"
#include "main.hpp"
#include "onthepitch/matchduration.hpp"
#include "systems/graphics/rendering/opengl_renderer3d.hpp"
#include "utils/localization.hpp"

using namespace blunted;

namespace {

constexpr unsigned long kMenuSmokeAdvanceDelay_ms = 250;

// How long the smoke test lingers here before confirming. Long enough to
// photograph the screen when something on it needs checking.
unsigned long MenuSmokeAdvanceDelay_ms() {
  return static_cast<unsigned long>(GetConfiguration()->GetInt(
      "menu_smoke_matchoptions_delay_ms", (int)kMenuSmokeAdvanceDelay_ms));
}

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

// Discovery lives here; the shaping of the lists is in prematchchoices.hpp.
std::vector<std::string> FilesUnder(const std::string& root, const std::string& extension,
                                    bool directoriesInstead) {
  std::vector<std::string> found;
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
    if (directoriesInstead) {
      if (entry.is_directory()) found.push_back(entry.path().filename().string());
      continue;
    }
    if (entry.is_directory()) {
      for (const auto& inner : std::filesystem::directory_iterator(entry.path(), ec))
        if (inner.path().extension() == extension) found.push_back(inner.path().string());
    } else if (entry.path().extension() == extension) {
      found.push_back(entry.path().filename().string());
    }
  }
  return found;
}

// Every scale on this screen has twenty positions (SliderStep draws a tick per
// step and prints "13/20"); the choice sliders below keep one step per choice.
constexpr int kDifficultySliderSteps = 20;

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
  // Two columns, and wide enough for them. Sixteen rows of a 6%-tall slider do
  // not fit a 75%-tall frame however they are stacked: at 720p the sheet ran
  // off the bottom of the screen and GAME PLAN, START and BACK were drawn below
  // it, reachable by focus and invisible - which is why a capture cropped to
  // the sliders never showed it (owner, 05-09: "the pre-match menu goes way
  // past off the screen").
  Gui2Frame* frame = new Gui2Frame(windowManager, "matchoptions_frame", 12, 14, 76, 74, true);
  this->AddView(frame);
  frame->Show();

  Gui2Caption* header = new Gui2Caption(windowManager, "matchoptions_caption", 2, 2, 72, 3,
                                        TR("match_options_title"));
  frame->AddView(header);
  header->Show();

  Gui2Grid* grid = new Gui2Grid(windowManager, "matchoptions_grid", 2, 9, 72, 60);

  difficultySlider = new Gui2Slider(windowManager, "matchoptions_slider_difficulty", 0, 0, 29, 6,
                                    TR("match_difficulty"));
  // Twenty positions, each a tick, with the step printed: the owner's floor for
  // a scale. It used to be the widget's default 51 - a bar somewhere along a
  // groove, with no way to tell what had been set or to set it again.
  difficultySlider->SetQuantization(kDifficultySliderSteps);
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

  // Where the match is played, and what plays around it.
  stadiumChoices = PrematchChoices::Stadiums(
      FilesUnder("media/objects/stadiums", ".object", false));
  entranceChoices = PrematchChoices::Entrances(
      FilesUnder(GetConfiguration()->Get("entrance_dir", "media/cutscenes/ent"), "", true));
  resultCutsceneChoices = PrematchChoices::ResultCutscenes(
      FilesUnder("media/cutscenes/result", ".camtrack", false));

  stadiumSlider = new Gui2Slider(windowManager, "matchoptions_slider_stadium", 0, 0, 29, 6,
                                 TR("match_stadium"));
  stadiumSlider->SetQuantization(std::max<int>(1, stadiumChoices.size()));
  stadiumSlider->SetValue(PrematchChoices::SliderFromIndex(
      PrematchChoices::IndexOfValue(stadiumChoices,
                                    GetConfiguration()->Get("stadium_object", "")),
      stadiumChoices.size()));
  UpdateStadiumCaption();
  stadiumSlider->sig_OnChange.connect([this](Gui2Slider*) { UpdateStadiumCaption(); });

  entranceSlider = new Gui2Slider(windowManager, "matchoptions_slider_entrance", 0, 0, 29, 6,
                                  TR("match_entrance"));
  entranceSlider->SetQuantization(std::max<int>(1, entranceChoices.size()));
  entranceSlider->SetValue(PrematchChoices::SliderFromIndex(
      PrematchChoices::IndexOfValue(entranceChoices,
                                    GetConfiguration()->Get("entrance_id", "")),
      entranceChoices.size()));
  UpdateEntranceCaption();
  entranceSlider->sig_OnChange.connect([this](Gui2Slider*) { UpdateEntranceCaption(); });

  resultCutsceneSlider = new Gui2Slider(windowManager, "matchoptions_slider_resultcutscene", 0, 0,
                                        29, 6, TR("match_result_cutscene"));
  resultCutsceneSlider->SetQuantization(std::max<int>(1, resultCutsceneChoices.size()));
  resultCutsceneSlider->SetValue(PrematchChoices::SliderFromIndex(
      PrematchChoices::IndexOfValue(resultCutsceneChoices,
                                    GetConfiguration()->Get("result_cutscene_id", "")),
      resultCutsceneChoices.size()));
  UpdateResultCutsceneCaption();
  resultCutsceneSlider->sig_OnChange.connect(
      [this](Gui2Slider*) { UpdateResultCutsceneCaption(); });

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
  Gui2Button* buttonGamePlan1 = new Gui2Button(windowManager, "matchoptions_button_gameplan_1", 0,
                                               0, 29, 3, TR("gameplan_header") + " 1");
  Gui2Button* buttonGamePlan2 = new Gui2Button(windowManager, "matchoptions_button_gameplan_2", 0,
                                               0, 29, 3, TR("gameplan_header") + " 2");
  buttonGamePlan1->sig_OnClick.connect([this](...) { GoGamePlan(0); });
  buttonGamePlan2->sig_OnClick.connect([this](...) { GoGamePlan(1); });

  // Grouped, with a heading per group: this was one undifferentiated stack of
  // nine sliders in which the match rules, the broadcast and the two teams'
  // kits all looked alike (owner, 04-09: "the pre-match screen has to be
  // revamped").
  auto sectionCaption = [this, windowManager](const std::string& name,
                                            const std::string& key) {
    Gui2Caption* caption =
        new Gui2Caption(windowManager, "matchoptions_section_" + name, 0, 0, 29, 2.6f, TR(key));
    caption->SetColor(windowManager->GetStyle()->GetColor(e_DecorationType_Bright1));
    return caption;
  };

  // Left column: the rules of the match and the broadcast around it. Right
  // column: the two teams, their game plans and the way out. Nine rows against
  // seven, so the tallest column is 47% of the screen and the whole sheet is on
  // it at 720p.
  int row = 0;
  grid->AddView(sectionCaption("match", "matchoptions_section_match"), row++, 0);
  grid->AddView(difficultySlider, row++, 0);
  grid->AddView(matchDurationSlider, row++, 0);
  grid->AddView(weatherSlider, row++, 0);
  grid->AddView(timeOfDaySlider, row++, 0);
  grid->AddView(sectionCaption("presentation", "matchoptions_section_presentation"), row++, 0);
  grid->AddView(stadiumSlider, row++, 0);
  grid->AddView(entranceSlider, row++, 0);
  grid->AddView(resultCutsceneSlider, row++, 0);
  row = 0;
  grid->AddView(sectionCaption("teams", "matchoptions_section_teams"), row++, 1);
  grid->AddView(kitSlider[0], row++, 1);
  grid->AddView(kitSlider[1], row++, 1);
  grid->AddView(buttonGamePlan1, row++, 1);
  grid->AddView(buttonGamePlan2, row++, 1);
  grid->AddView(buttonStart, row++, 1);
  grid->AddView(buttonBack, row++, 1);
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

void MatchOptionsPage::UpdateStadiumCaption() {
  if (stadiumChoices.empty()) {
    stadiumSlider->SetCaption(TR("match_stadium"));
    return;
  }
  const int index =
      PrematchChoices::IndexFromSlider(stadiumSlider->GetValue(), stadiumChoices.size());
  stadiumSlider->SetCaption(TRF("match_stadium_named", {stadiumChoices.at(index).label}));
}

void MatchOptionsPage::UpdateEntranceCaption() {
  const int index =
      PrematchChoices::IndexFromSlider(entranceSlider->GetValue(), entranceChoices.size());
  // "Automatic" and "None" are translated; a family is its own number.
  const std::string& label = entranceChoices.at(index).label;
  entranceSlider->SetCaption(TRF("match_entrance_named",
                                {label == "entrance_any" || label == "entrance_none" ? TR(label)
                                                                                    : label}));
}

void MatchOptionsPage::UpdateResultCutsceneCaption() {
  const int index = PrematchChoices::IndexFromSlider(resultCutsceneSlider->GetValue(),
                                                     resultCutsceneChoices.size());
  const std::string& label = resultCutsceneChoices.at(index).label;
  resultCutsceneSlider->SetCaption(
      TRF("match_result_cutscene_named", {label == "cutscene_any" ? TR(label) : label}));
}

void MatchOptionsPage::UpdateKitCaptions() {
  for (int teamID = 0; teamID < 2; teamID++) {
    const int kitNum = KitNumFromSlider(kitSlider[teamID]->GetValue());
    kitSlider[teamID]->SetCaption(
        TRF("match_team_kit", {std::to_string(teamID + 1), std::to_string(kitNum)}));
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
  // UI validation route: open the pre-match game plan, let it draw, then
  // screenshot it.
  if (GetConfiguration()->GetBool("menu_smoke_open_gameplan", false)) {
    if (!gamePlanShotTriggered) {
      gamePlanShotTriggered = true;
      gamePlanOpenedTime_ms = EnvironmentManager::GetInstance().GetTime_ms();
      GoGamePlan(0);
      printf("[menu-smoke] Game plan page opened for UI check\n");
      return;
    }
    return;
  }

  Gui2Page::Process();

  if (!autoAdvanceTriggered && MenuSmokeAutoQuickMatchEnabled() &&
      EnvironmentManager::GetInstance().GetTime_ms() >=
          pageCreatedTime_ms + MenuSmokeAdvanceDelay_ms()) {
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
  if (!stadiumChoices.empty()) {
    GetConfiguration()->Set(
        "stadium_object",
        stadiumChoices
            .at(PrematchChoices::IndexFromSlider(stadiumSlider->GetValue(), stadiumChoices.size()))
            .value);
  }
  GetConfiguration()->Set(
      "entrance_id",
      entranceChoices
          .at(PrematchChoices::IndexFromSlider(entranceSlider->GetValue(), entranceChoices.size()))
          .value);
  GetConfiguration()->Set("result_cutscene_id",
                          resultCutsceneChoices
                              .at(PrematchChoices::IndexFromSlider(
                                  resultCutsceneSlider->GetValue(), resultCutsceneChoices.size()))
                              .value);
  GetConfiguration()->Set("match_difficulty", difficultySlider->GetValue());
  GetConfiguration()->Set("match_duration_minutes",
                          MatchDurationMinutesFromSlider(matchDurationSlider->GetValue()));
  GetConfiguration()->SaveFile(GetConfigFilename());

  this->Exit();

  Properties properties;
  windowManager->GetPageFactory()->CreatePage((int)e_PageID_LoadingMatch, properties, 0);

  delete this;
}
