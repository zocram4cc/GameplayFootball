// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#include "radar.hpp"

#include <cmath>
#include <SDL2/SDL.h>
#include <SDL2/SDL2_rotozoom.h>

#include "../../gamedefines.hpp"
#include "../../onthepitch/match.hpp"
#include "utils/gui2/windowmanager.hpp"

namespace blunted {
namespace {

constexpr float kRadarAspectRatio = 550.0f / 360.0f;
constexpr float kBallHeightPercent = 1.2f;
constexpr float kAvatarHeightPercent = 1.6f;

}  // namespace

Gui2Radar::Gui2Radar(Gui2WindowManager* windowManager, const std::string& name, float x_percent,
                     float y_percent, float width_percent, float height_percent, Match* match,
                     const Vector3& color1_1, const Vector3& color1_2, const Vector3& color2_1,
                     const Vector3& color2_2)
    : Gui2View(windowManager, name, x_percent, y_percent, width_percent, height_percent),
      match(match),
      color1_1(color1_1),
      color1_2(color1_2),
      color2_1(color2_1),
      color2_2(color2_2) {
  // todo: use colors!

  radarWidthPercent =
      windowManager->GetWidthPercentForHeight(height_percent, kRadarAspectRatio);
  radarXOffsetPercent = (width_percent - radarWidthPercent) * 0.5f;
  ballWidthPercent = windowManager->GetWidthPercentForHeight(kBallHeightPercent, 1.0f);
  avatarWidthPercent = windowManager->GetWidthPercentForHeight(kAvatarHeightPercent, 1.0f);

  bg = new Gui2Image(windowManager, "bg_radar", radarXOffsetPercent, 0, radarWidthPercent,
                     height_percent);
  this->AddView(bg);
  bg->LoadImage("media/menu/radar/radar.png");
  bg->Show();

  ball = new Gui2Image(windowManager, "radar_ball", 0, 0, ballWidthPercent,
                       kBallHeightPercent);
  this->AddView(ball);
  ball->LoadImage("media/menu/radar/ball.png");
  ball->Show();

  this->Show();
}

Gui2Radar::~Gui2Radar() {}

Gui2Radar::e_Mode Gui2Radar::ParseMode(const std::string& mode) {
  if (mode == "off") return e_Mode_Off;
  if (mode == "transparent") return e_Mode_Transparent;
  return e_Mode_On;
}

std::string Gui2Radar::ModeToString(e_Mode mode) {
  switch (mode) {
    case e_Mode_Off:
      return "off";
    case e_Mode_Transparent:
      return "transparent";
    default:
      return "on";
  }
}

float Gui2Radar::GetEffectiveOpacity() const {
  switch (mode) {
    case e_Mode_Off:
      return 0.0f;
    case e_Mode_Transparent:
      return transparentOpacity;
    default:
      return 1.0f;
  }
}

void Gui2Radar::ApplyOpacity(Gui2Image* image) const {
  if (!image) return;
  image->GetImage2D()->SetAlpha(GetEffectiveOpacity());
}

void Gui2Radar::SetMode(e_Mode newMode) {
  mode = newMode;
  ApplyOpacity(bg);
  ApplyOpacity(ball);
  for (unsigned int i = 0; i < team1avatars.size(); i++) ApplyOpacity(team1avatars.at(i));
  for (unsigned int i = 0; i < team2avatars.size(); i++) ApplyOpacity(team2avatars.at(i));
}

void Gui2Radar::SetTransparentOpacity(float opacity) {
  transparentOpacity = clamp(opacity, 0.0f, 1.0f);
  SetMode(mode);
}

void Gui2Radar::ReloadAvatars(int teamID, unsigned int playerCount) {
  if (teamID == 0) {
    for (unsigned int i = 0; i < team1avatars.size(); i++) {
      team1avatars.at(i)->Exit();
      delete team1avatars.at(i);
    }
    team1avatars.clear();
    for (unsigned int i = 0; i < playerCount; i++) {
      Gui2Image* avatar =
          new Gui2Image(windowManager, "radar_avatar_" + int_to_str(teamID) + "_" + int_to_str(i),
                        0, 0, avatarWidthPercent, kAvatarHeightPercent);
      this->AddView(avatar);
      avatar->LoadImage("media/menu/radar/p1.png");
      avatar->Show();
      ApplyOpacity(avatar);
      team1avatars.push_back(avatar);
    }
  }

  // oof ugly c/p'ed code
  if (teamID == 1) {
    for (unsigned int i = 0; i < team2avatars.size(); i++) {
      team2avatars.at(i)->Exit();
      delete team2avatars.at(i);
    }
    team2avatars.clear();
    for (unsigned int i = 0; i < playerCount; i++) {
      Gui2Image* avatar =
          new Gui2Image(windowManager, "radar_avatar_" + int_to_str(teamID) + "_" + int_to_str(i),
                        0, 0, avatarWidthPercent, kAvatarHeightPercent);
      this->AddView(avatar);
      avatar->LoadImage("media/menu/radar/p2.png");
      avatar->Show();
      ApplyOpacity(avatar);
      team2avatars.push_back(avatar);
    }
  }
}

void Gui2Radar::Process() {}

void Gui2Radar::Put() {
  Vector3 position = match->GetBall()->Predict(0).Get2D();
  Vector3 pos2d = position * Vector3(1 / (pitchHalfW * 2), -(1 / (pitchHalfH * 2)), 0);
  pos2d = pos2d + Vector3(0.5, 0.5, 0);
  pos2d = pos2d * Vector3(0.96f, 0.96f, 0) + Vector3(0.02f, 0.02f, 0);  // margin
  ball->SetPosition(radarXOffsetPercent + pos2d.coords[0] * radarWidthPercent -
                        ballWidthPercent * 0.5f,
                    pos2d.coords[1] * height_percent - kBallHeightPercent * 0.5f);

  // get player positions
  std::vector<Player*> team1players;
  match->GetActiveTeamPlayers(0, team1players);
  std::vector<Player*> team2players;
  match->GetActiveTeamPlayers(1, team2players);

  if (team1players.size() != team1avatars.size())
    ReloadAvatars(0, team1players.size());
  if (team2players.size() != team2avatars.size())
    ReloadAvatars(1, team2players.size());
  ball->SetZPriority(1);  // ball on top

  for (unsigned int i = 0; i < team1players.size(); i++) {
    Vector3 position = team1players.at(i)->GetPosition();
    Vector3 pos2d = position * Vector3(1 / (pitchHalfW * 2), -(1 / (pitchHalfH * 2)), 0);
    pos2d = pos2d + Vector3(0.5, 0.5, 0);
    pos2d = pos2d * Vector3(0.96f, 0.96f, 0) + Vector3(0.02f, 0.02f, 0);  // margin

    team1avatars.at(i)->SetPosition(
        radarXOffsetPercent + pos2d.coords[0] * radarWidthPercent - avatarWidthPercent * 0.5f,
        pos2d.coords[1] * height_percent - kAvatarHeightPercent * 0.5f);
  }

  for (unsigned int i = 0; i < team2players.size(); i++) {
    Vector3 position = team2players.at(i)->GetPosition();
    Vector3 pos2d = position * Vector3(1 / (pitchHalfW * 2), -(1 / (pitchHalfH * 2)), 0);
    pos2d = pos2d + Vector3(0.5, 0.5, 0);
    pos2d = pos2d * Vector3(0.96f, 0.96f, 0) + Vector3(0.02f, 0.02f, 0);  // margin

    team2avatars.at(i)->SetPosition(
        radarXOffsetPercent + pos2d.coords[0] * radarWidthPercent - avatarWidthPercent * 0.5f,
        pos2d.coords[1] * height_percent - kAvatarHeightPercent * 0.5f);
  }
}

}  // namespace blunted
