// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#include "gameplan.hpp"
#include "utils/playermodelmap.hpp"
#include <cstdlib>

#include <cmath>

#include "../main.hpp"
#include "data/formations.hpp"
#include "mainmenu.hpp"
#include "pagefactory.hpp"
#include "onthepitch/match.hpp"
#include "onthepitch/team.hpp"
#include "menu/ingame/hudindicators.hpp"
#include "onthepitch/coachmode.hpp"
#include "onthepitch/teaminstructions.hpp"
#include "onthepitch/teamphilosophy.hpp"
#include "utils/localization.hpp"



using namespace blunted;

GamePlanPage::GamePlanPage(Gui2WindowManager* windowManager, const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  teamID = pageData.properties->GetInt("teamID", 0);
  const int teamDatabaseID = pageData.properties->GetInt("teamDatabaseID", -1);

  // Wide enough for the pitch and the list side by side, as the broadcast lays them
  // out; it used to be a narrow centred column with the pitch stacked on top.
  constexpr float xOffset = 17.0f;
  // Before kick-off there is no match yet, so load the team straight from the
  // database; during a match the live team data is edited instead.
  Match* match = GetGameTask()->GetMatch();
  if (match) {
    teamData = match->GetTeam(teamID)->GetTeamData();
  } else {
    standaloneTeamData = std::make_unique<TeamData>(teamDatabaseID);
    teamData = standaloneTeamData.get();
  }

  Gui2Frame* frame = new Gui2Frame(windowManager, "gameplan_frame", xOffset, 10, 66, 82, true);
  this->AddView(frame);
  frame->Show();

  Gui2Caption* header = new Gui2Caption(
      windowManager, "gameplan_header", 1.5f, 1.5f, 32, 3,
      Localization::GetInstance().Translate("gameplan_header") + " " + int_to_str(teamID + 1));
  grid = new Gui2Grid(windowManager, "gameplan_grid", 1.5f, 6, 0, 0);
  gridNav = new Gui2Grid(windowManager, "gameplan_grid_navigation", 0, 0, 0, 0);

  // The pitch beside the list rather than above it, and tall enough for eleven cards
  // to stand apart - the broadcast gives it most of the panel.
  map = new Gui2PlanMap(windowManager, "gameplan_planmap", 0, 0, 30, 54, teamData);
  buttonLineup = new Gui2Button(windowManager, "gameplan_button_lineup", 0, 0, 32, 3,
                                Localization::GetInstance().Translate("gameplan_lineup"));
  buttonTactics = new Gui2Button(windowManager, "gameplan_button_tactics", 0, 0, 32, 3,
                                 Localization::GetInstance().Translate("gameplan_tactics"));
  buttonFormation = new Gui2Button(
      windowManager, "gameplan_button_formation", 0, 0, 32, 3,
      Localization::GetInstance().Translate("gameplan_formation") + ": " + GetFormationCaption());

  buttonPhilosophy = new Gui2Button(
      windowManager, "gameplan_button_philosophy", 0, 0, 32, 3,
      Localization::GetInstance().Translate("gameplan_philosophy") + ": " + GetPhilosophyCaption());
  buttonInstructions = new Gui2Button(
      windowManager, "gameplan_button_instructions", 0, 0, 32, 3,
      Localization::GetInstance().Translate("gameplan_instructions") + ": " +
          GetInstructionsCaption());
  buttonSubstitutions =
      new Gui2Button(windowManager, "gameplan_button_substitutions", 0, 0, 32, 3,
                     Localization::GetInstance().Translate("gameplan_substitutions"));

  buttonLineup->sig_OnClick.connect([this](...) { GoLineupMode(); });
  buttonTactics->sig_OnClick.connect([this](...) { GoTacticsMenu(); });
  buttonPhilosophy->sig_OnClick.connect([this](...) { GoPhilosophyMenu(); });
  buttonInstructions->sig_OnClick.connect([this](...) { GoInstructionsMenu(); });
  buttonSubstitutions->sig_OnClick.connect([this](...) { GoSubstitutionsMenu(); });

  buttonFormation->sig_OnClick.connect([this](...) { GoFormationMenu(); });

  this->sig_OnClose.connect([this](...) { OnClose(); });

  frame->AddView(header);
  header->Show();

  frame->AddView(grid);
  gridNav->AddView(buttonLineup, 0, 0);
  gridNav->AddView(buttonTactics, 1, 0);
  gridNav->AddView(buttonPhilosophy, 2, 0);
  gridNav->AddView(buttonInstructions, 3, 0);
  gridNav->AddView(buttonSubstitutions, 4, 0);
  gridNav->AddView(buttonFormation, 5, 0);
  gridNav->UpdateLayout(0.5);
  grid->AddView(map, 0, 0);
  map->sig_OnOpenPlayerMenu.connect([this](int slotIndex) { GoPlayerMenu(slotIndex); });
  map->sig_OnSubstitute.connect(
      [this](int starter, int bench) { return SubstituteFromMap(starter, bench); });
  map->sig_OnToggleRole.connect([this](int squadIndex) { ToggleRoleFromMap(squadIndex); });
  map->sig_OnFocus.connect([this](bool onPitch) {
    if (onPitch)
      ShowLineupHints();
    else
      ShowBrowsingHints();
  });

  // The hint line, lower left of the page, outside the panel: two lines of
  // "button - what it does", which is where the broadcast puts them and what
  // the owner asked for.
  hintLine1 = new Gui2Caption(windowManager, "gameplan_hint1", 2.0f, 88.0f, 40, 2.4f, "");
  hintLine2 = new Gui2Caption(windowManager, "gameplan_hint2", 2.0f, 91.0f, 40, 2.4f, "");
  this->AddView(hintLine1);
  this->AddView(hintLine2);
  hintLine1->Show();
  hintLine2->Show();
  grid->AddView(gridNav, kGamePlanNavRow, kGamePlanNavColumn);

  grid->UpdateLayout(0.0);
  grid->Show();

  BuildOpponentSheet();

  buttonTactics->SetFocus();
  ShowBrowsingHints();

  this->Show();

  if (UpdateNonImportableDB()) {
    namedb = std::make_unique<Database>();
    bool dbSuccess = namedb->Load("databases/names.sqlite");
    if (!dbSuccess) {
      // The names db is only used to mirror edits back into the importable
      // database; the page works fine without it, so do not kill the game.
      Log(e_Warning, "GamePlanPage", "GamePlanPage",
          "could not open names.sqlite; tactics edits will not be mirrored");
      namedb = nullptr;
    }
  } else {
    namedb = nullptr;
  }
}

void GamePlanPage::Process() {
  Gui2Page::Process();
}

GamePlanPage::~GamePlanPage() {}

void GamePlanPage::OnClose() {
  // Before anything else: every handler below has to know the page is going.
  tearingDown = true;
  // These are children; they die with the tree, and nothing may write to them
  // after this point.
  hintLine1 = nullptr;
  hintLine2 = nullptr;
  opponentMap = nullptr;
  opponentLabel = nullptr;
  buttonSwitchTeam = nullptr;
  namedb.reset();
  // The button column is detached whenever a submenu is open, so if the page
  // is closed at that moment nothing in the view tree owns it. Exit clears any
  // focus inside it on the way out.
  if (gridNav && gridNav->GetParent() == nullptr) {
    gridNav->Exit();
    delete gridNav;
    gridNav = nullptr;
  }
}

void GamePlanPage::Deactivate() {
  if (tearingDown || !gridNav) return;
  // Only ever the button column: a submenu that has taken the cell owns its
  // own removal (GamePlanSubMenu::ProcessWindowingEvent), and detaching it
  // from under itself left the grid holding a view it no longer parented.
  if (gridNav->GetParent() == grid) grid->RemoveView(gridNav);
}

void GamePlanPage::Reactivate(Gui2View* focusTarget) {
  lineupMode = false;
  ShowBrowsingHints();
  // A submenu closing during the page's teardown must not put the column back:
  // the grid it would go into is mid-delete, and gridNav itself may already be
  // gone (OnClose owns it once it is detached).
  if (tearingDown || !gridNav) return;
  grid->AddView(gridNav, kGamePlanNavRow, kGamePlanNavColumn);
  grid->UpdateLayout(0.0);
  gridNav->Show();
  // Restore keyboard/gamepad focus after returning from a sub-menu, otherwise
  // navigation can be left dangling. A caller resuming pitch interaction
  // (the per-player menu) passes the map itself instead of the default.
  (focusTarget ? focusTarget : static_cast<Gui2View*>(buttonTactics))->SetFocus();
}

Vector3 GamePlanPage::GetButtonColor(int id) {
  Vector3 color = windowManager->GetStyle()->GetColor(e_DecorationType_Bright1);
  if (id > 10)
    color = Vector3(240, 140, 60);
  if (id > 21)
    color = Vector3(80, 140, 255);
  return color;
}

bool GamePlanPage::CanSwitchTeams() const {
  // One pad running both benches: that is the case PES has no screen for.
  Match* match = GetGameTask()->GetMatch();
  if (!match) return true;  // before kick-off both sheets are the manager's own
  return CoachMode::IsManagerDuel(match->GetCoachSetup());
}

void GamePlanPage::BuildOpponentSheet() {
  const int otherID = teamID == 0 ? 1 : 0;
  TeamData* other = nullptr;
  Match* match = GetGameTask()->GetMatch();
  if (match) {
    other = match->GetTeam(otherID)->GetTeamData();
  } else {
    // Before kick-off there is no match, so the other side is loaded from the
    // database the same way this page loads its own.
    const int otherDatabaseID =
        GetConfiguration()->GetInt(otherID == 0 ? "showcase_team1" : "showcase_team2", -1);
    if (otherDatabaseID > 0) {
      opponentTeamData = std::make_unique<TeamData>(otherDatabaseID);
      other = opponentTeamData.get();
    }
  }
  if (!other) return;

  opponentLabel = new Gui2Caption(windowManager, "gameplan_opponent_label", 51.0f, 46.0f, 30, 2.4f,
                                  Localization::GetInstance().Translate("gameplan_header") + " " +
                                      int_to_str(otherID + 1) + ": " + other->GetName());
  this->AddView(opponentLabel);
  opponentLabel->Show();

  // Read-only: it is the other team's sheet, not this page's editing surface.
  // Swapping which side is edited is what the switch button is for.
  opponentMap = new Gui2PlanMap(windowManager, "gameplan_planmap_opponent", 51.0f, 49.0f, 24, 40,
                                other);
  opponentMap->SetSelectable(false);
  this->AddView(opponentMap);
  opponentMap->Show();

  if (!CanSwitchTeams()) return;
  buttonSwitchTeam =
      new Gui2Button(windowManager, "gameplan_button_switchteam", 0, 0, 32, 3,
                     Localization::GetInstance().Translate("gameplan_switch_team"));
  buttonSwitchTeam->sig_OnClick.connect([this](...) { SwitchTeam(); });
  gridNav->AddView(buttonSwitchTeam, 6, 0);
  gridNav->UpdateLayout(0.5);
  grid->UpdateLayout(0.0);
}

void GamePlanPage::SwitchTeam() {
  // The page is built entirely from its team, so the honest way to change
  // which one it edits is to build it again for the other.
  Properties properties;
  properties.Set("teamID", teamID == 0 ? 1 : 0);
  Match* match = GetGameTask()->GetMatch();
  if (!match) {
    const int otherDatabaseID =
        GetConfiguration()->GetInt(teamID == 0 ? "showcase_team2" : "showcase_team1", -1);
    properties.Set("teamDatabaseID", otherDatabaseID);
  }
  CreatePage((int)e_PageID_GamePlan, properties);
}

void GamePlanPage::SetHints(const std::string& text) {
  // "A - grab | B - back" on one line, wrapped onto the second at the last
  // separator that fits, so a long set of hints does not run off the page.
  //
  // Nothing is written once the page is going: the map's focus signal fires
  // during teardown (losing focus is part of being deleted), and by then these
  // captions are children that have already been destroyed.
  if (tearingDown || !hintLine1 || !hintLine2) return;
  const size_t half = text.size() / 2;
  size_t split = std::string::npos;
  size_t at = text.find(" | ");
  while (at != std::string::npos) {
    if (split == std::string::npos ||
        (size_t)std::abs((long)at - (long)half) < (size_t)std::abs((long)split - (long)half))
      split = at;
    at = text.find(" | ", at + 1);
  }
  if (text.size() < 46 || split == std::string::npos) {
    hintLine1->SetCaption(text);
    hintLine2->SetCaption("");
    return;
  }
  hintLine1->SetCaption(text.substr(0, split));
  hintLine2->SetCaption(text.substr(split + 3));
}

void GamePlanPage::ShowBrowsingHints() {
  SetHints(Localization::GetInstance().Translate("hint_gameplan_browse"));
}

void GamePlanPage::ShowLineupHints() {
  SetHints(Localization::GetInstance().Translate("hint_gameplan_lineup"));
}

void GamePlanPage::GoLineupMode() {
  // The pitch takes over: the button column steps aside so the cards have the
  // panel to themselves and the arrows move between players instead of between
  // buttons. This is the LINEUP the owner asked for - dragging cards is how the
  // formation is changed, and dragging a bench card onto a starter is how a
  // substitution is made.
  lineupMode = true;
  Deactivate();
  map->Refresh();
  map->SetFocus();
  ShowLineupHints();
}

bool GamePlanPage::SubstituteFromMap(int starterSlot, int benchIndex) {
  if (tearingDown) return false;
  PlayerData* starter = teamData->GetPlayerData(starterSlot);
  PlayerData* bench = teamData->GetPlayerData(benchIndex);
  if (!starter || !bench) return false;

  Match* match = GetGameTask()->GetMatch();
  if (!match) {
    // Before kick-off the eleven are simply the first eleven of the squad, so
    // exchanging the two rows *is* the team selection.
    teamData->SwitchPlayers(starter->GetDatabaseID(), bench->GetDatabaseID());
    SetHints(bench->GetLastName() + " -> " + starter->GetLastName());
    return true;
  }

  Team* team = match->GetTeam(teamID);
  const Substitutions::e_Result result = match->RequestSubstitution(
      teamID, team->GetPlayer(starter->GetDatabaseID()), team->GetPlayer(bench->GetDatabaseID()));
  std::string message;
  switch (result) {
    case Substitutions::e_Result_Accepted: message = "substitution_done"; break;
    case Substitutions::e_Result_NotAStoppage: message = "substitution_wait_for_stoppage"; break;
    case Substitutions::e_Result_NoSubstitutionsLeft: message = "substitution_none_left"; break;
    case Substitutions::e_Result_PlayerSentOff: message = "substitution_sent_off"; break;
    default: message = "substitution_unavailable"; break;
  }
  const std::string line = Localization::GetInstance().Translate(message);
  match->SpamMessage(line, 3000);
  SetHints(line);
  return result == Substitutions::e_Result_Accepted;
}

void GamePlanPage::ToggleRoleFromMap(int squadIndex) {
  if (tearingDown) return;
  PlayerData* playerData = teamData->GetPlayerData(squadIndex);
  if (!playerData) return;
  // The position he is standing in: for a starter that is his formation slot's
  // role, for a bench player the one his card shows.
  const e_PlayerRole role = Gui2PlanMap::IsStarter(squadIndex)
                                ? teamData->GetFormationEntry(squadIndex).role
                                : (playerData->GetRoles().empty() ? e_PlayerRole_CM
                                                                  : playerData->GetRoles().front());
  const bool has = playerData->ToggleRole(role);
  SetHints(playerData->GetLastName() + ": " + GetRoleName(role) + " " +
           Localization::GetInstance().Translate(has ? "role_registered" : "role_unregistered"));
}

void GamePlanPage::GoLineupMenu() {
  Deactivate();

  lineupMenu = new GamePlanSubMenu(windowManager, buttonLineup, grid, "lineup_submenu");
  lineupMenu->sig_OnClose.connect([this](...) { SaveLineup(); });
  lineupMenu->sig_OnClose.connect([this](...) { Reactivate(); });

  const auto& playerData = teamData->GetPlayerData();
  for (unsigned int i = 0; i < playerData.size(); i++) {
    Vector3 color = GetButtonColor(i);
    Gui2Button* button =
        lineupMenu->AddButton("playerbutton_id" + int_to_str(playerData.at(i)->GetDatabaseID()),
                              playerData.at(i)->GetLastName(), i, 0, color);
    button->sig_OnClick.connect([this](Gui2Button* btn) { LineupMenuOnClick(btn); });
    button->SetToggleable(true);
    if (i == 0)
      button->SetFocus();
  }

  // The imported player portrait (editable media/players/playerportraits.cfg), flush
  // against the panel's right edge and square in pixels rather than in percent - the
  // exports are square, and 16x22 percent of a 16:9 page stretched them wide.
  // Under the pitch, inside the panel: these are page coordinates, and the panel runs
  // from 17 to 83 percent across. Square in pixels rather than in percent - the
  // exports are square and the page is 16:9.
  const float kPortraitW = 11.0f;
  const float kPortraitH = kPortraitW * 16.0f / 9.0f;
  lineupPortrait = new Gui2Image(windowManager, "lineup_portrait", 22.0f, 71.0f, kPortraitW,
                                 kPortraitH);
  this->AddView(lineupPortrait);
  if (!playerData.empty())
    ShowLineupPortrait(playerData.at(0)->GetDatabaseID());

  lineupMenu->Show();
}

void GamePlanPage::ShowLineupPortrait(int databaseID) {
  if (!lineupPortrait) return;
  const std::string& path = GetPlayerPortrait(databaseID);
  if (path.empty()) {
    lineupPortrait->Hide();
    return;
  }
  lineupPortrait->LoadImage(path);
  lineupPortrait->Show();
}

void GamePlanPage::LineupMenuOnClick(Gui2Button* button) {
  // "playerbutton_id<N>" carries the database id
  const std::string& name = button->GetName();
  size_t at = name.find("_id");
  if (at != std::string::npos)
    ShowLineupPortrait(atoi(name.c_str() + at + 3));

  Gui2Button* selected = lineupMenu->GetToggledButton(button);
  if (selected) {
    // switch players
    selected->SetToggled(false);
    button->SetToggled(false);

    int rowSelected = lineupMenu->GetGrid()->GetRow(selected);
    int rowButton = lineupMenu->GetGrid()->GetRow(button);
    assert(rowSelected != -1 && rowButton != -1);
    lineupMenu->GetGrid()->RemoveView(rowSelected, 0);
    lineupMenu->GetGrid()->RemoveView(rowButton, 0);
    lineupMenu->GetGrid()->AddView(button, rowSelected, 0);
    button->Show();
    button->SetColor(GetButtonColor(rowSelected));
    lineupMenu->GetGrid()->AddView(selected, rowButton, 0);
    selected->Show();
    selected->SetColor(GetButtonColor(rowButton));
    lineupMenu->GetGrid()->UpdateLayout(0.5);
    selected->SetFocus();

    int id1 = atoi(
        selected->GetName().substr(selected->GetName().rfind("id") + 2, std::string::npos).c_str());
    int id2 = atoi(
        button->GetName().substr(button->GetName().rfind("id") + 2, std::string::npos).c_str());
    teamData->SwitchPlayers(id1, id2);
  }
}

void GamePlanPage::SaveLineup() {
  // The submenu's close signal reaches this during page teardown too, when
  // lineupMenu's own children are already being deleted.
  if (tearingDown || !lineupMenu) return;
  if (UpdateNonImportableDB() && namedb) {
    // saves to temp names db, which is used when importing the actual db.

    const std::vector<Gui2Button*>& allButtons = lineupMenu->GetAllButtons();

    for (unsigned int i = 0; i < allButtons.size(); i++) {
      Gui2View* button = lineupMenu->GetGrid()->FindView(i, 0);
      int id = atoi(
          button->GetName().substr(button->GetName().rfind("id") + 2, std::string::npos).c_str());
      PlayerData* playerData = teamData->GetPlayerDataByDatabaseID(id);
      unsigned int formationorder = i;

      // find player
      auto result = namedb->Query("select id from playernames where fakefirstname = \"" +
                                  playerData->GetFirstName() + "\" and fakelastname = \"" +
                                  playerData->GetLastName() + "\" limit 1;");
      int playerDatabaseID = -1;
      if (result->data.size() > 0) {
        playerDatabaseID = atoi(result->data.at(0).at(0).c_str());
        result =
            namedb->Query("update playernames set formationorder = " + int_to_str(formationorder) +
                          " where id = " + int_to_str(playerDatabaseID) + ";");
      } else {  // player does not yet exist in namedb
        if (Verbose())
          printf("WARNING: player does not exist in namedb: %s %s\n",
                 playerData->GetFirstName().c_str(), playerData->GetLastName().c_str());
      }
    }
  }

  teamData->SaveLineup();
}

void GamePlanPage::GoTacticsMenu() {
  Deactivate();

  tacticsSliders.clear();  // could still be here from previous tactics subpage visit. should clear
                           // this on leaving page but we have no mechanism to do so (<- todo, could
                           // use onclose probably)

  tacticsMenu = new GamePlanSubMenu(windowManager, buttonTactics, grid, "tactics_submenu");
  tacticsMenu->sig_OnClose.connect([this](...) { SaveTactics(); });
  tacticsMenu->sig_OnClose.connect([this](...) { Reactivate(); });

  const Properties& userProps = teamData->GetTactics().userProperties;
  const map_Properties* userPropMap = userProps.GetProperties();
  const Properties& factoryProps = teamData->GetTactics().factoryProperties;
  const map_Properties* factoryPropMap = factoryProps.GetProperties();

  map_Properties::const_iterator iter = userPropMap->begin();
  int i = 0;
  while (iter != userPropMap->end()) {
    const std::string& tacticName = (*iter).first;
    // Philosophy lives in these same properties and has the philosophy menu of its
    // own; on a slider it showed up unmarked at zero and overwrote itself.
    if (!TeamPhilosophy::IsSliderTactic(tacticName)) {
      iter++;
      continue;
    }
    if (Verbose())
      printf("adding %s\n", tacticName.c_str());
    TacticsSlider slider;
    slider.id = i;
    slider.tacticName = tacticName;
    slider.widget = tacticsMenu->AddSlider(
        "tacticsslider_" + slider.tacticName,
        teamData->GetTactics().humanReadableNames.Get(slider.tacticName.c_str(), slider.tacticName),
        i, 0);
    slider.widget->AddHelperValue(Vector3(80, 80, 250), "factory default for this team",
                                  factoryProps.GetReal(slider.tacticName.c_str()));
    slider.widget->SetValue(userProps.GetReal(slider.tacticName.c_str()));
    slider.widget->sig_OnChange.connect(
        [this, id = slider.id](Gui2Slider* s) { TacticsMenuOnChange(s, id); });
    if (i == 0)
      slider.widget->SetFocus();
    tacticsSliders.push_back(slider);
    i++;
    iter++;
  }

  tacticsMenu->Show();
}

std::string GamePlanPage::GetFormationCaption() const {
  return Formations::ShapeName(Formations::ParseShape(teamData->GetTactics().userProperties.Get(
      "formation", teamData->GetTactics().factoryProperties.Get("formation", "4-4-2"))));
}

void GamePlanPage::ApplyFormationShape(const Formations::Shape& shape) {
  const std::string name = Formations::ShapeName(shape);
  teamData->GetTacticsWritable().userProperties.Set("formation", name);
  buttonFormation->SetCaption(Localization::GetInstance().Translate("gameplan_formation") + ": " +
                              name);

  // The pitch map reads TeamData's FormationEntry array directly, not the
  // tactics string just written above - before this, picking a formation
  // here changed nothing the map showed until a live match's AI controller
  // separately reshaped itself. Reassign every slot from the new layout so
  // the map (and a save) agree with what was just chosen.
  const std::vector<Formations::Slot> layout = Formations::GetLayoutForShape(shape);
  for (int i = 0; i < playerNum && i < (int)layout.size(); i++) {
    FormationEntry entry;
    entry.role = layout.at(i).role;
    entry.databasePosition = layout.at(i).position;
    entry.position = entry.databasePosition * 0.6f + GetDefaultRolePosition(entry.role) * 0.4f;
    teamData->SetFormationEntry(i, entry);
  }
  map->Refresh();

  Match* match = GetGameTask()->GetMatch();
  if (match)
    match->GetTeam(teamID)->GetController()->ApplyFormationShape(shape);
}

void GamePlanPage::CustomFormationOnChange() {
  // Defenders and midfielders are chosen; the forwards are whatever is left, so
  // the eleven always adds up however the sliders are dragged.
  const int defenders =
      static_cast<int>(std::round(sliderCustomDefenders->GetValue() * Formations::outfieldPlayers));
  const int midfielders = static_cast<int>(
      std::round(sliderCustomMidfielders->GetValue() * Formations::outfieldPlayers));
  const Formations::Shape shape = Formations::MakeShapeClamped(defenders, midfielders, 0);

  sliderCustomDefenders->SetCaption(Localization::GetInstance().Translate("formation_defenders") +
                                    ": " + int_to_str(shape.defenders));
  sliderCustomMidfielders->SetCaption(
      Localization::GetInstance().Translate("formation_midfielders") + ": " +
      int_to_str(shape.midfielders) + "   " +
      Localization::GetInstance().Translate("formation_forwards") + ": " +
      int_to_str(shape.forwards) + "   (" + Formations::ShapeName(shape) + ")");

  ApplyFormationShape(shape);
}

void GamePlanPage::GoFormationMenu() {
  Deactivate();

  formationMenu = new GamePlanSubMenu(windowManager, buttonFormation, grid, "formation_submenu");
  formationMenu->sig_OnClose.connect([this](...) { SaveTactics(); });
  formationMenu->sig_OnClose.connect([this](...) { Reactivate(); });

  const Formations::Shape currentShape = Formations::ParseShape(GetFormationCaption());

  int row = 0;
  for (; row < Formations::GetCount(); row++) {
    const Formations::e_Formation candidate = Formations::GetFormationAt(row);
    const bool isCurrent = Formations::ShapeName(Formations::GetShape(candidate)) ==
                           Formations::ShapeName(currentShape);
    const Vector3 color = isCurrent ? Vector3(80, 160, 80) : Vector3(60, 60, 60);
    Gui2Button* button = formationMenu->AddButton("formationbutton_" + int_to_str(row),
                                                  Formations::GetName(candidate), row, 0, color);
    const int index = row;
    button->sig_OnClick.connect([this, index](Gui2Button* btn) { FormationMenuOnClick(index); });
    if (isCurrent)
      button->SetFocus();
  }

  // ...and a builder for anything else the manager fancies, from 6-4-0 to 1-0-9.
  sliderCustomDefenders = formationMenu->AddSlider(
      "formation_custom_defenders", Localization::GetInstance().Translate("formation_defenders"),
      row++, 0);
  sliderCustomMidfielders = formationMenu->AddSlider(
      "formation_custom_midfielders",
      Localization::GetInstance().Translate("formation_midfielders"), row++, 0);
  sliderCustomDefenders->SetValue(static_cast<float>(currentShape.defenders) /
                                  Formations::outfieldPlayers);
  sliderCustomMidfielders->SetValue(static_cast<float>(currentShape.midfielders) /
                                    Formations::outfieldPlayers);
  sliderCustomDefenders->sig_OnChange.connect([this](Gui2Slider*) { CustomFormationOnChange(); });
  sliderCustomMidfielders->sig_OnChange.connect([this](Gui2Slider*) { CustomFormationOnChange(); });

  formationMenu->Show();
}

void GamePlanPage::FormationMenuOnClick(int formationIndex) {
  const Formations::e_Formation formation = Formations::GetFormationAt(formationIndex);

  // Reshape the team straight away, so the plan map and the pitch both show it.
  ApplyFormationShape(Formations::GetShape(formation));

  // Keep the custom sliders in step with the preset that was picked.
  const Formations::Shape shape = Formations::GetShape(formation);
  if (sliderCustomDefenders)
    sliderCustomDefenders->SetValue(static_cast<float>(shape.defenders) /
                                    Formations::outfieldPlayers);
  if (sliderCustomMidfielders)
    sliderCustomMidfielders->SetValue(static_cast<float>(shape.midfielders) /
                                      Formations::outfieldPlayers);

  const std::vector<Gui2Button*>& buttons = formationMenu->GetAllButtons();
  for (unsigned int i = 0; i < buttons.size(); i++) {
    buttons.at(i)->SetColor(static_cast<int>(i) == formationIndex ? Vector3(80, 160, 80)
                                                                  : Vector3(60, 60, 60));
  }
}

void GamePlanPage::GoPlayerMenu(int slotIndex) {
  if (tearingDown) return;
  Deactivate();
  playerMenuSlotIndex = slotIndex;

  playerMenu = new GamePlanSubMenu(windowManager, map, grid, "player_submenu");
  playerMenu->sig_OnClose.connect([this](...) { Reactivate(map); });

  PlayerData* playerData = teamData->GetPlayerData(slotIndex);
  // A bench card has no formation entry; the position his card shows is the
  // first one he is registered for.
  const bool onPitch = Gui2PlanMap::IsStarter(slotIndex);
  FormationEntry currentEntry = teamData->GetFormationEntry(slotIndex);
  if (!onPitch && playerData && !playerData->GetRoles().empty())
    currentEntry.role = playerData->GetRoles().front();

  int row = 0;
  playerMenu
      ->AddButton("player_caption_name",
                  playerData ? playerData->GetLastName()
                             : Localization::GetInstance().Translate("gameplan_player"),
                  row++, 0, Vector3(40, 40, 40))
      ->SetActive(false);

  // Ten roles, current one highlighted; picking one keeps the player on the
  // same tactical spot and just changes what he is asked to do there.
  for (int i = 0; i < 10; i++) {
    const e_PlayerRole role = static_cast<e_PlayerRole>(i);
    const bool isCurrent = role == currentEntry.role;
    Gui2Button* button =
        playerMenu->AddButton("player_role_" + int_to_str(i), GetRoleName(role), row++, 0,
                              isCurrent ? Vector3(80, 160, 80) : Vector3(60, 60, 60));
    button->sig_OnClick.connect([this, role](Gui2Button*) { PlayerMenuRoleOnClick(role); });
    if (isCurrent)
      button->SetFocus();
  }

  // A shortcut into the existing whole-team formation picker: PES frames
  // this as one submenu ("change formation and role"; the pause modal's tab
  // row puts Preset Tactics beside Team Sheet/Edit Position - Enrichment
  // Addendum). GamePlanSubMenu occupies the nav cell itself, so opening a
  // second one requires closing this one first rather than nesting.
  Gui2Button* formationButton = playerMenu->AddButton(
      "player_menu_formation", Localization::GetInstance().Translate("gameplan_formation"), row++,
      0, Vector3(60, 60, 90));
  formationButton->sig_OnClick.connect([this](Gui2Button*) {
    grid->RemoveView(kGamePlanNavRow, kGamePlanNavColumn);
    playerMenu->Exit();
    delete playerMenu;
    playerMenu = nullptr;
    GoFormationMenu();
  });

  playerMenu->Show();
}

void GamePlanPage::PlayerMenuRoleOnClick(e_PlayerRole role) {
  if (Gui2PlanMap::IsStarter(playerMenuSlotIndex)) {
    FormationEntry entry = teamData->GetFormationEntry(playerMenuSlotIndex);
    entry.role = role;
    entry.position = entry.databasePosition * 0.6f + GetDefaultRolePosition(role) * 0.4f;
    teamData->SetFormationEntry(playerMenuSlotIndex, entry);
  } else if (PlayerData* playerData = teamData->GetPlayerData(playerMenuSlotIndex)) {
    // A bench player has no place on the pitch to change, so picking a role
    // registers it: the same thing the secondary button does on his card.
    playerData->ToggleRole(role);
  }
  map->Refresh();

  // buttons[0] is the inactive name caption and the last is the formation
  // shortcut; the ten in between are the role picks, in e_PlayerRole order.
  const std::vector<Gui2Button*>& buttons = playerMenu->GetAllButtons();
  for (unsigned int i = 1; i + 1 < buttons.size(); i++) {
    const e_PlayerRole candidate = static_cast<e_PlayerRole>(i - 1);
    buttons.at(i)->SetColor(candidate == role ? Vector3(80, 160, 80) : Vector3(60, 60, 60));
  }
}

std::string GamePlanPage::GetPhilosophyCaption() const {
  const TeamPhilosophy::e_Philosophy philosophy =
      TeamPhilosophy::Parse(teamData->GetTactics().userProperties.Get(
          "philosophy", teamData->GetTactics().factoryProperties.Get("philosophy", "balanced")));
  return Localization::GetInstance().Translate("philosophy_" + TeamPhilosophy::GetName(philosophy));
}

void GamePlanPage::GoPhilosophyMenu() {
  Deactivate();

  philosophyMenu = new GamePlanSubMenu(windowManager, buttonPhilosophy, grid, "philosophy_submenu");
  philosophyMenu->sig_OnClose.connect([this](...) { SaveTactics(); });
  philosophyMenu->sig_OnClose.connect([this](...) { Reactivate(); });

  const TeamPhilosophy::e_Philosophy current =
      TeamPhilosophy::Parse(teamData->GetTactics().userProperties.Get(
          "philosophy", teamData->GetTactics().factoryProperties.Get("philosophy", "balanced")));

  for (int i = 0; i < TeamPhilosophy::e_Philosophy_Count; i++) {
    const TeamPhilosophy::e_Philosophy philosophy = static_cast<TeamPhilosophy::e_Philosophy>(i);
    const Vector3 color = philosophy == current ? Vector3(80, 160, 80) : Vector3(60, 60, 60);
    Gui2Button* button = philosophyMenu->AddButton(
        "philosophybutton_" + int_to_str(i),
        Localization::GetInstance().Translate("philosophy_" + TeamPhilosophy::GetName(philosophy)),
        i, 0, color);
    button->sig_OnClick.connect([this, i](Gui2Button* btn) { PhilosophyMenuOnClick(i); });
    if (philosophy == current)
      button->SetFocus();
  }

  philosophyMenu->Show();
}

std::string GamePlanPage::GetInstructionsCaption() const {
  const TeamInstructions::State state =
      TeamInstructions::Load(teamData->GetTactics().userProperties);
  const std::string line = HudIndicators::InstructionsText(state.instructions);
  return line.empty() ? Localization::GetInstance().Translate("instructions_none") : line;
}

void GamePlanPage::GoInstructionsMenu() {
  Deactivate();

  instructionsMenu =
      new GamePlanSubMenu(windowManager, buttonInstructions, grid, "instructions_submenu");
  instructionsMenu->sig_OnClose.connect([this](...) { SaveTactics(); });
  instructionsMenu->sig_OnClose.connect([this](...) { Reactivate(); });

  const TeamInstructions::State current =
      TeamInstructions::Load(teamData->GetTactics().userProperties);
  for (int i = 0; i < TeamInstructions::instructionCount; i++) {
    const TeamInstructions::e_Instruction instruction = TeamInstructions::GetInstructionAt(i);
    const bool on = TeamInstructions::Has(current, instruction);
    Gui2Button* button = instructionsMenu->AddButton(
        "instructionbutton_" + int_to_str(i),
        Localization::GetInstance().Translate("instruction_" + int_to_str(i)), i, 0,
        on ? Vector3(80, 160, 80) : Vector3(60, 60, 60));
    button->sig_OnClick.connect([this, i](Gui2Button* btn) { InstructionsMenuOnClick(i); });
    if (i == 0)
      button->SetFocus();
  }
  instructionsMenu->Show();
}

void GamePlanPage::InstructionsMenuOnClick(int instructionIndex) {
  TeamInstructions::State state = TeamInstructions::Load(teamData->GetTactics().userProperties);
  TeamInstructions::Toggle(state, TeamInstructions::GetInstructionAt(instructionIndex));
  TeamInstructions::Save(state, teamData->GetTacticsWritable().userProperties);
  buttonInstructions->SetCaption(
      Localization::GetInstance().Translate("gameplan_instructions") + ": " +
      GetInstructionsCaption());
  const std::vector<Gui2Button*>& buttons = instructionsMenu->GetAllButtons();
  for (unsigned int i = 0; i < buttons.size(); i++) {
    const bool on =
        TeamInstructions::Has(state, TeamInstructions::GetInstructionAt(static_cast<int>(i)));
    buttons.at(i)->SetColor(on ? Vector3(80, 160, 80) : Vector3(60, 60, 60));
  }
}

void GamePlanPage::PhilosophyMenuOnClick(int philosophyIndex) {
  const TeamPhilosophy::e_Philosophy philosophy =
      static_cast<TeamPhilosophy::e_Philosophy>(philosophyIndex);
  teamData->GetTacticsWritable().userProperties.Set("philosophy",
                                                    TeamPhilosophy::GetName(philosophy));
  buttonPhilosophy->SetCaption(Localization::GetInstance().Translate("gameplan_philosophy") + ": " +
                               GetPhilosophyCaption());

  // Reflect the new choice in the button colours.
  const std::vector<Gui2Button*>& buttons = philosophyMenu->GetAllButtons();
  for (unsigned int i = 0; i < buttons.size(); i++) {
    buttons.at(i)->SetColor(static_cast<int>(i) == philosophyIndex ? Vector3(80, 160, 80)
                                                                   : Vector3(60, 60, 60));
  }
}

void GamePlanPage::GoSubstitutionsMenu() {
  Deactivate();

  substitutionsMenu =
      new GamePlanSubMenu(windowManager, buttonSubstitutions, grid, "substitutions_submenu");
  substitutionsMenu->sig_OnClose.connect([this](...) { Reactivate(); });

  Match* match = GetGameTask()->GetMatch();
  int row = 0;

  if (match) {
    Team* team = match->GetTeam(teamID);

    std::vector<Player*> onPitch;
    team->GetActivePlayers(onPitch);
    std::vector<Player*> bench;
    team->GetBenchPlayers(bench);

    substitutionsMenu
        ->AddButton(
            "subs_caption_remaining",
            Localization::GetInstance().Translate("substitutions_remaining") + ": " +
                int_to_str(Substitutions::GetRemaining(match->GetSubstitutionState(), teamID)),
            row++, 0, Vector3(40, 40, 40))
        ->SetActive(false);

    for (Player* player : onPitch) {
      Gui2Button* button = substitutionsMenu->AddButton(
          "subsbutton_out_id" + int_to_str(player->GetID()), player->GetPlayerData()->GetLastName(),
          row++, 0, Vector3(120, 70, 70));
      button->sig_OnClick.connect([this](Gui2Button* btn) { SubstitutionsMenuOnClick(btn); });
      button->SetToggleable(true);
      if (row == 2)
        button->SetFocus();
    }

    for (Player* player : bench) {
      Gui2Button* button = substitutionsMenu->AddButton(
          "subsbutton_in_id" + int_to_str(player->GetID()), player->GetPlayerData()->GetLastName(),
          row++, 0, Vector3(70, 100, 130));
      button->sig_OnClick.connect([this](Gui2Button* btn) { SubstitutionsMenuOnClick(btn); });
      button->SetToggleable(true);
    }
  }

  substitutionsMenu->Show();
}

void GamePlanPage::SubstitutionsMenuOnClick(Gui2Button* button) {
  Match* match = GetGameTask()->GetMatch();
  if (!match)
    return;

  // A substitution needs one player from the pitch and one from the bench, so
  // wait until a second button of the other kind has been toggled.
  Gui2Button* other = substitutionsMenu->GetToggledButton(button);
  if (!other)
    return;

  const bool buttonIsOut = button->GetName().find("_out_") != std::string::npos;
  const bool otherIsOut = other->GetName().find("_out_") != std::string::npos;
  if (buttonIsOut == otherIsOut)
    return;

  Gui2Button* outButton = buttonIsOut ? button : other;
  Gui2Button* inButton = buttonIsOut ? other : button;
  outButton->SetToggled(false);
  inButton->SetToggled(false);

  const int outID = atoi(
      outButton->GetName().substr(outButton->GetName().rfind("id") + 2, std::string::npos).c_str());
  const int inID = atoi(
      inButton->GetName().substr(inButton->GetName().rfind("id") + 2, std::string::npos).c_str());

  Team* team = match->GetTeam(teamID);
  const Substitutions::e_Result result =
      match->RequestSubstitution(teamID, team->GetPlayer(outID), team->GetPlayer(inID));

  std::string message;
  switch (result) {
    case Substitutions::e_Result_Accepted:
      message = "substitution_done";
      break;
    case Substitutions::e_Result_NotAStoppage:
      message = "substitution_wait_for_stoppage";
      break;
    case Substitutions::e_Result_NoSubstitutionsLeft:
      message = "substitution_none_left";
      break;
    case Substitutions::e_Result_PlayerSentOff:
      message = "substitution_sent_off";
      break;
    default:
      message = "substitution_unavailable";
      break;
  }
  match->SpamMessage(Localization::GetInstance().Translate(message), 3000);

  if (result == Substitutions::e_Result_Accepted) {
    // Rebuild the lists so the new line-up is shown.
    substitutionsMenu->Hide();
    GoSubstitutionsMenu();
  }
}

void GamePlanPage::SaveTactics() {
  if (tearingDown) return;
  if (UpdateNonImportableDB() && namedb) {
    // saves to temp names db, which is used when importing the actual db.

    std::string tactics_xml;

    for (unsigned int i = 0; i < tacticsSliders.size(); i++) {
      tactics_xml += "<" + tacticsSliders.at(i).tacticName + ">" +
                     real_to_str(tacticsSliders.at(i).widget->GetValue()) + "</" +
                     tacticsSliders.at(i).tacticName + ">\n";
    }
    printf("tactics:\n%s\n", tactics_xml.c_str());

    // find club
    auto result = namedb->Query("select id from clubnames where faketargetname = \"" +
                                teamData->GetName() + "\" limit 1;");
    int teamDatabaseID = -1;
    if (result->data.size() > 0) {
      teamDatabaseID = atoi(result->data.at(0).at(0).c_str());
      result = namedb->Query("update clubnames set tactics_xml = \"" + tactics_xml +
                             "\" where id = " + int_to_str(teamDatabaseID) + ";");
    } else {  // team does not yet exist in namedb
      if (Verbose())
        printf("WARNING: team does not exist in namedb: %s\n", teamData->GetName().c_str());
    }
  }

  teamData->SaveTactics();
}

void GamePlanPage::TacticsMenuOnChange(Gui2Slider* slider, int id) {
  // printf("slider %i (%s) altered\n", id, slider->GetName().c_str());
  Properties& userProps = teamData->GetTacticsWritable().userProperties;
  userProps.Set(tacticsSliders.at(id).tacticName.c_str(), tacticsSliders.at(id).widget->GetValue());
}
