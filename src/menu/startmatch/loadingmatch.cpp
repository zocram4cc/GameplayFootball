// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#include "loadingmatch.hpp"

#include "../ingame/menuicons.hpp"

#include <filesystem>

#include "../pagefactory.hpp"
#include "main.hpp"
#include "utils/gui2/widgets/frame.hpp"

using namespace blunted;

namespace {

const char* kLoadingFallbackLogo = "media/menu/league.png";

std::string ResolveTeamLogo(const TeamData* teamData) {
  const std::string& logoPath = teamData->GetLogoUrl();
  if (!logoPath.empty() && std::filesystem::exists(logoPath)) {
    return logoPath;
  }
  return kLoadingFallbackLogo;
}

}  // namespace

LoadingMatchPage::LoadingMatchPage(Gui2WindowManager* windowManager, const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  // logos
  MatchData* matchData = new MatchData(GetMenuTask()->GetTeamID(0), GetMenuTask()->GetTeamID(1));
  GetMenuTask()->SetMatchData(matchData);
  TeamData* teamData1 = matchData->GetTeamData(0);
  TeamData* teamData2 = matchData->GetTeamData(1);
  constexpr float kTeamLogoHeight = 12.5f;

  Gui2Frame* loadingPanel =
      new Gui2Frame(windowManager, "frame_loading_match", 18, 24, 64, 52, true);
  this->AddView(loadingPanel);
  loadingPanel->Show();

  Gui2Caption* header =
      new Gui2Caption(windowManager, "main_loading_header", 18, 28, 64, 4, "MATCH DAY");
  header->SetPosition(50.0f - header->GetTextWidthPercent() * 0.5f, 28);
  this->AddView(header);
  header->Show();

  const float plateH = kTeamLogoHeight * 1.24f + 4.5f;
  const float vsY = 44.0f + (plateH - 6.0f) * 0.5f;
  Gui2Image* versusPlate =
      new Gui2Image(windowManager, "main_loading_versus_plate", 45.5f, vsY, 9.0f, 6.0f);
  this->AddView(versusPlate);
  MenuIcons::PaintPlate(versusPlate, Vector3(18, 22, 34), 190);
  versusPlate->Show();
  Gui2Caption* versus =
      new Gui2Caption(windowManager, "main_loading_versus", 47, vsY + 1.0f, 6, 4, "VS");
  versus->SetPosition(50.0f - versus->GetTextWidthPercent() * 0.5f, vsY + 1.0f);
  this->AddView(versus);
  versus->Show();

  Gui2Caption* status = new Gui2Caption(windowManager, "main_loading_status", 18, 68, 64, 3,
                                        "Preparing the pitch...");
  status->SetPosition(50.0f - status->GetTextWidthPercent() * 0.5f, 68);
  this->AddView(status);
  status->Show();

  // Each crest on a plate with its name inside, the same plate the opening
  // graphic uses.
  MenuIcons::MakeCrestPlate(windowManager, this, "main_loading_team0", 30.0f, 44.0f,
                            kTeamLogoHeight, ResolveTeamLogo(teamData1), teamData1->GetName());
  MenuIcons::MakeCrestPlate(windowManager, this, "main_loading_team1", 70.0f, 44.0f,
                            kTeamLogoHeight, ResolveTeamLogo(teamData2), teamData2->GetName());
  this->ShowAllChildren();

  this->SetFocus();

  this->Show();

  GetMenuTask()->SetActiveJoystickID(0);
  GetMenuTask()->EnableKeyboard();
  windowManager->GetPagePath()->Clear();

  sentStartGameSignal = false;
}

LoadingMatchPage::~LoadingMatchPage() {}

void LoadingMatchPage::Process() {
  Gui2Page::Process();

  if (!sentStartGameSignal) {
    sentStartGameSignal = true;
    // Allow one rendered loading frame before the heavier match setup begins.
    EnvironmentManager::GetInstance().Pause_ms(100);
    GetMenuTask()->SetMenuAction(e_MenuAction_Game);
  }
}

void LoadingMatchPage::Close() {
  this->Exit();

  Properties properties;
  windowManager->GetPageFactory()->CreatePage((int)e_PageID_Game, properties, 0);

  delete this;
}
