// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#include "planmap.hpp"

#include "../../data/teamdata.hpp"
#include "../../gamedefines.hpp"
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
constexpr float kCardW = 4.4f;
constexpr float kStripH = 1.7f;
constexpr float kNameH = 1.7f;

Vector3 ToVector3(const PlanMapCard::Colour& colour) {
  return Vector3(colour.r, colour.g, colour.b);
}

}  // namespace

Gui2PlanMap::Gui2PlanMap(Gui2WindowManager* windowManager, const std::string& name, float x_percent,
                         float y_percent, float width_percent, float height_percent,
                         TeamData* teamData)
    : Gui2View(windowManager, name, x_percent, y_percent, width_percent, height_percent),
      teamData(teamData) {
  const float pitchWidth =
      windowManager->GetWidthPercentForHeight(height_percent, kPitchDiagramAspectRatio);
  const float pitchX = (width_percent - pitchWidth) * 0.5f;
  Gui2Image* bg =
      new Gui2Image(windowManager, "image_planmap_bg", pitchX, 0, pitchWidth, height_percent);
  bg->LoadImage("media/ui/pes/plan_pitch.png");
  this->AddView(bg);
  bg->Show();

  // Square in pixels, not in percent - a portrait is square and the page is 16:9.
  const float cardH = windowManager->GetHeightPercentForWidth(kCardW, 1.0f);

  for (int i = 0; i < 11; i++) {
    const FormationEntry& entry = teamData->GetFormationEntry(i);
    Vector3 pos = entry.databasePosition;
    if (entry.role != e_PlayerRole_GK) {
      pos.coords[0] *= 0.8f;
      pos.coords[0] += 0.1f;
    }  // compress field players' depth
    PlayerData* playerData = teamData->GetPlayerData(i);
    // Depth runs up the panel and width across it: the database's x is goal-to-goal
    // and its y is touchline-to-touchline, which is the opposite of the old landscape
    // diagram's axes.
    const float ex = pitchX + (pos.coords[1] * 0.42f + 0.5f) * pitchWidth - kCardW * 0.5f;
    const float ey = (pos.coords[0] * -0.42f + 0.5f) * height_percent -
                     (cardH + kStripH + kNameH) * 0.5f;
    Gui2PlanMapEntry* card = new Gui2PlanMapEntry(
        windowManager, "planmap_player1_entry" + int_to_str(i), ex, ey, kCardW,
        cardH + kStripH + kNameH, entry.role, playerData, cardH);
    this->AddView(card);
    card->Show();
  }
}

Gui2PlanMap::~Gui2PlanMap() {}

void Gui2PlanMap::Process() {}

// ENTRIES

Gui2PlanMapEntry::Gui2PlanMapEntry(Gui2WindowManager* windowManager, const std::string& name,
                                   float x_percent, float y_percent, float width_percent,
                                   float height_percent, e_PlayerRole role, PlayerData* playerData,
                                   float portraitHeight)
    : Gui2View(windowManager, name, x_percent, y_percent, width_percent, height_percent) {
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
      new Gui2Caption(windowManager, name + "_role", 0, stripY, width_percent * 0.6f, 1.7f,
                      roleName);
  roleNameCaption->SetColor(ToVector3(PlanMapCard::LineColour(PlanMapCard::LineOf(role))));
  this->AddView(roleNameCaption);
  roleNameCaption->Show();

  const std::string rating = playerData ? PlanMapCard::RatingText(playerData->GetAverageStat())
                                        : std::string();
  if (!rating.empty()) {
    ratingCaption = new Gui2Caption(windowManager, name + "_rating", width_percent * 0.55f, stripY,
                                    width_percent * 0.45f, 1.7f, rating);
    this->AddView(ratingCaption);
    ratingCaption->Show();
  }

  // The name, tinted by whether this is a position he plays.
  // Cards stand a card's width apart on the pitch, so a name wider than that runs
  // into its neighbours.
  const unsigned int kNameBudget = 11;
  playerNameCaption = new Gui2Caption(windowManager, name + "_name", 0, stripY + 1.7f,
                                     width_percent, 1.7f,
                                     PlanMapCard::NameText(playerName, kNameBudget));
  playerNameCaption->SetColor(ToVector3(PlanMapCard::AptitudeColour(aptitude)));
  this->AddView(playerNameCaption);
  playerNameCaption->Show();
}

Gui2PlanMapEntry::~Gui2PlanMapEntry() {}

}  // namespace blunted
