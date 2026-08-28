// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#include "planmap.hpp"

#include <SDL2/SDL.h>

#include "../../data/teamdata.hpp"
#include "../../gamedefines.hpp"
#include "menu/ingame/captionfit.hpp"
#include "planmapcard.hpp"
#include "utils/gui2/widgets/image.hpp"
#include "utils/gui2/windowmanager.hpp"
#include "utils/playermodelmap.hpp"

namespace blunted {
namespace {

// Portrait, as the broadcast draws it: the goal at the bottom, the attack at the top.
// The old diagram was landscape, which left eleven cards no room to stand apart.
constexpr float kPitchDiagramAspectRatio = 420.0f / 580.0f;

// The card, as the broadcast draws it: a square portrait, a strip under it with the
// position on the left and the rating on the right, the name beneath that. Sized in
// percent of the map, which is itself sized in percent of the page.
//
// kStripH/kNameH were 1.7 - under caption.cpp's own documented 22px legibility
// floor ("anything asking for a line under about 22px ... lost the thin white
// fill") at both the 720p capture resolution this fork verifies at and 1080p.
// Captured and read on a rendered frame (tasks/28-08-26.md's rule): eleven
// names like "JOHN HELLD." rendered as an illegible smear. 2.2 clears the
// floor at 1080p and comes close at 720p without the cards' personal-space
// separation (TeamData's minDistanceFraction) letting two rows touch.
constexpr float kCardW = 4.6f;
constexpr float kStripH = 2.2f;
constexpr float kNameH = 2.2f;

// Grabbing a card moves it this many pitch-percent per directional press,
// and a drop within this radius of another card swaps the two instead of
// just relocating the one being dragged.
constexpr float kDragStepPercent = 4.0f;
constexpr float kDropSwapRadiusPercent = 9.0f;
constexpr float kPitchMarginPercent = 6.0f;

const Vector3 kSelectedColor(0.0f, 172.0f, 193.0f);  // #00ACC1, the prefab's own selected-tab teal
const Vector3 kHeldColor(255.0f, 196.0f, 40.0f);      // gold: unmistakably "picked up"

Vector3 ToVector3(const PlanMapCard::Colour& colour) {
  return Vector3(colour.r, colour.g, colour.b);
}

}  // namespace

// ENTRIES

Gui2PlanMapEntry::Gui2PlanMapEntry(Gui2WindowManager* windowManager, const std::string& name,
                                   float x_percent, float y_percent, float width_percent,
                                   float height_percent, e_PlayerRole role,
                                   PlayerData* playerData, float portraitHeight)
    : Gui2View(windowManager, name, x_percent, y_percent, width_percent, height_percent),
      role(role) {
  const std::string roleName = GetRoleName(role);
  const std::string playerName = playerData ? playerData->GetLastName() : "";
  const PlanMapCard::e_Aptitude aptitude =
      PlanMapCard::AptitudeFor(role, playerData ? playerData->GetRoles()
                                                : std::vector<e_PlayerRole>());

  // The portrait, when the player has one. A squad with no imported portraits draws
  // the strip and the name alone rather than a row of empty frames.
  const std::string& portrait =
      playerData ? GetPlayerPortrait(playerData->GetDatabaseID()) : std::string();
  if (!portrait.empty()) {
    portraitImage = new Gui2Image(windowManager, name + "_portrait", 0, 0, width_percent,
                                 portraitHeight);
    portraitImage->LoadImage(portrait);
    this->AddView(portraitImage);
    portraitImage->Show();
  }

  const float stripY = portrait.empty() ? 0.0f : portraitHeight;

  // Position on the left, coloured by line, and the rating on the right.
  roleNameCaption =
      new Gui2Caption(windowManager, name + "_role", 0, stripY, width_percent * 0.6f, kStripH,
                      roleName);
  roleNameCaption->SetColor(ToVector3(PlanMapCard::LineColour(PlanMapCard::LineOf(role))));
  this->AddView(roleNameCaption);
  roleNameCaption->Show();

  const std::string rating = playerData ? PlanMapCard::RatingText(playerData->GetAverageStat())
                                        : std::string();
  if (!rating.empty()) {
    ratingCaption = new Gui2Caption(windowManager, name + "_rating", width_percent * 0.55f, stripY,
                                    width_percent * 0.45f, kStripH, rating);
    this->AddView(ratingCaption);
    ratingCaption->Show();
  }

  // The name, tinted by whether this is a position he plays. NameText's own
  // character-budget cut keeps a sane fallback; FitAndCentreCaption then
  // guarantees the box the card actually has at kStripH/kNameH's legible
  // size is never overrun, whatever the name's real width turns out to be
  // (the character budget is a length guess, not a pixel measurement).
  const unsigned int kNameBudget = 14;
  playerNameCaption = new Gui2Caption(windowManager, name + "_name", 0, stripY + kStripH,
                                     width_percent, kNameH,
                                     PlanMapCard::NameText(playerName, kNameBudget));
  playerNameCaption->SetColor(ToVector3(PlanMapCard::AptitudeColour(aptitude)));
  this->AddView(playerNameCaption);
  playerNameCaption->Show();
  FitAndCentreCaption(playerNameCaption, width_percent * 0.5f, width_percent, kNameH,
                      kNameH * 0.6f);

  // The selection/drag border: a plain image the same size as the whole
  // card, drawn on with DrawRectangle exactly as Gui2Button borders its own
  // focus state (button.cpp) - no separate art asset, hidden until a
  // highlight is actually set.
  highlightBorder = new Gui2Image(windowManager, name + "_highlight", 0, 0, width_percent,
                                  height_percent);
  this->AddView(highlightBorder);
}

Gui2PlanMapEntry::~Gui2PlanMapEntry() {}

void Gui2PlanMapEntry::SetHighlight(e_Highlight highlight) {
  if (!highlightBorder) return;
  if (highlight == e_Highlight_None) {
    highlightBorder->Hide();
    return;
  }
  boost::intrusive_ptr<Image2D>& border = highlightBorder->GetImage2D();
  const int w = (int)border->GetSize().coords[0];
  const int h = (int)border->GetSize().coords[1];
  const Vector3& color = highlight == e_Highlight_Held ? kHeldColor : kSelectedColor;
  const int thickness = highlight == e_Highlight_Held ? 4 : 3;
  border->DrawRectangle(0, 0, w, h, Vector3(0, 0, 0), 0);  // clear to transparent first
  border->DrawRectangle(0, 0, w, thickness, color, 255);
  border->DrawRectangle(0, h - thickness, w, thickness, color, 255);
  border->DrawRectangle(0, 0, thickness, h, color, 255);
  border->DrawRectangle(w - thickness, 0, thickness, h, color, 255);
  border->OnChange();
  highlightBorder->Show();
}

// MAP

Gui2PlanMap::Gui2PlanMap(Gui2WindowManager* windowManager, const std::string& name,
                         float x_percent, float y_percent, float width_percent,
                         float height_percent, TeamData* teamData)
    : Gui2View(windowManager, name, x_percent, y_percent, width_percent, height_percent),
      teamData(teamData) {
  isSelectable = true;

  pitchWidth = windowManager->GetWidthPercentForHeight(height_percent, kPitchDiagramAspectRatio);
  pitchX = (width_percent - pitchWidth) * 0.5f;
  Gui2Image* bg =
      new Gui2Image(windowManager, "image_planmap_bg", pitchX, 0, pitchWidth, height_percent);
  bg->LoadImage("media/ui/pes/plan_pitch.png");
  this->AddView(bg);
  bg->Show();

  RebuildEntries();
  UpdateHighlights();
}

Gui2PlanMap::~Gui2PlanMap() {}

void Gui2PlanMap::Process() {}

void Gui2PlanMap::CardTopLeft(int index, float* x_percent, float* y_percent) const {
  // Square in pixels, not in percent - a portrait is square and the page is 16:9.
  const float cardH = windowManager->GetHeightPercentForWidth(kCardW, 1.0f);
  const PlanMapInteraction::PitchPoint& p = points.at(index);
  *x_percent = pitchX + (p.xPercent / 100.0f) * pitchWidth - kCardW * 0.5f;
  *y_percent = (p.yPercent / 100.0f) * height_percent - (cardH + kStripH + kNameH) * 0.5f;
}

void Gui2PlanMap::RepositionEntry(int index) {
  float ex, ey;
  CardTopLeft(index, &ex, &ey);
  entries.at(index)->SetPosition(ex, ey);
}

void Gui2PlanMap::RebuildEntries() {
  for (Gui2PlanMapEntry* entry : entries) {
    entry->Exit();
    delete entry;
  }
  entries.clear();
  points.clear();

  const float cardH = windowManager->GetHeightPercentForWidth(kCardW, 1.0f);

  for (int i = 0; i < playerNum; i++) {
    const FormationEntry& entry = teamData->GetFormationEntry(i);
    points.push_back(PlanMapInteraction::DatabaseToPitch(entry.databasePosition, entry.role));
    PlayerData* playerData = teamData->GetPlayerData(i);

    float ex, ey;
    CardTopLeft(i, &ex, &ey);
    Gui2PlanMapEntry* card = new Gui2PlanMapEntry(
        windowManager, "planmap_player1_entry" + int_to_str(i), ex, ey, kCardW,
        cardH + kStripH + kNameH, entry.role, playerData, cardH);
    this->AddView(card);
    card->Show();
    entries.push_back(card);
  }

  if (selectedIndex >= (int)entries.size()) selectedIndex = 0;
}

void Gui2PlanMap::Refresh() {
  RebuildEntries();
  UpdateHighlights();
}

void Gui2PlanMap::UpdateHighlights() {
  for (int i = 0; i < (int)entries.size(); i++) {
    if (i == heldIndex) {
      entries.at(i)->SetHighlight(Gui2PlanMapEntry::e_Highlight_Held);
    } else if (i == selectedIndex && IsFocussed()) {
      entries.at(i)->SetHighlight(Gui2PlanMapEntry::e_Highlight_Selected);
    } else {
      entries.at(i)->SetHighlight(Gui2PlanMapEntry::e_Highlight_None);
    }
  }
}

void Gui2PlanMap::OnGainFocus() { UpdateHighlights(); }

void Gui2PlanMap::OnLoseFocus() {
  if (heldIndex == -1) UpdateHighlights();
}

void Gui2PlanMap::ProcessWindowingEvent(WindowingEvent* event) {
  const Vector3 direction = event->GetDirection();
  const bool hasDirection = direction.GetLength() > 0.3f;

  if (heldIndex == -1) {
    if (hasDirection) {
      selectedIndex =
          PlanMapInteraction::NextSelectionInDirection(points, selectedIndex, direction);
      UpdateHighlights();
    } else if (event->IsActivate()) {
      heldIndex = selectedIndex;
      dragStartPoint = points.at(heldIndex);
      UpdateHighlights();
    } else {
      event->Ignore();
      return;
    }
    event->Accept();
    return;
  }

  // A card is being dragged.
  if (event->IsEscape()) {
    points.at(heldIndex) = dragStartPoint;
    RepositionEntry(heldIndex);
    heldIndex = -1;
    UpdateHighlights();
  } else if (hasDirection) {
    PlanMapInteraction::PitchPoint moved = points.at(heldIndex);
    moved.xPercent += direction.coords[0] * kDragStepPercent;
    moved.yPercent += direction.coords[1] * kDragStepPercent;
    points.at(heldIndex) = PlanMapInteraction::ClampToPitch(moved, kPitchMarginPercent);
    RepositionEntry(heldIndex);
  } else if (event->IsActivate()) {
    const int target = PlanMapInteraction::NearestCardWithinRadius(
        points.at(heldIndex), points, heldIndex, kDropSwapRadiusPercent);
    if (target != -1) {
      const int idHeld = teamData->GetPlayerData(heldIndex)->GetDatabaseID();
      const int idTarget = teamData->GetPlayerData(target)->GetDatabaseID();
      teamData->SwitchPlayers(idHeld, idTarget);
      selectedIndex = target;
      heldIndex = -1;
      RebuildEntries();
    } else {
      FormationEntry updated = teamData->GetFormationEntry(heldIndex);
      updated.databasePosition =
          PlanMapInteraction::PitchToDatabase(points.at(heldIndex), updated.role);
      updated.position =
          updated.databasePosition * 0.6f + GetDefaultRolePosition(updated.role) * 0.4f;
      teamData->SetFormationEntry(heldIndex, updated);
      // Re-derive from the canonical stored position rather than trusting the
      // dragged point's float accumulation, so display and data can never
      // silently drift apart.
      points.at(heldIndex) = PlanMapInteraction::DatabaseToPitch(updated.databasePosition,
                                                                 updated.role);
      selectedIndex = heldIndex;
      heldIndex = -1;
      RepositionEntry(selectedIndex);
    }
    UpdateHighlights();
  } else {
    event->Ignore();
    return;
  }
  event->Accept();
}

void Gui2PlanMap::ProcessKeyboardEvent(KeyboardEvent* event) {
  if (heldIndex == -1 && event->GetKeyOnce(SDLK_x)) {
    sig_OnOpenPlayerMenu(selectedIndex);
    event->Accept();
    return;
  }
  event->Ignore();
}

}  // namespace blunted
