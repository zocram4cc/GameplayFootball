// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#include "scoreboard.hpp"

#include "../../onthepitch/match.hpp"
#include "main.hpp"
#include "utils/gui2/windowmanager.hpp"

using namespace blunted;

Gui2ScoreBoard::Gui2ScoreBoard(Gui2WindowManager* windowManager, Match* match)
    : Gui2View(windowManager, "scoreboard", 2, 2, 96, 4), match(match) {
  x_percent = 2;
  y_percent = 2;
  width_percent = 96;
  height_percent = 4;

  goalCount[0] = 0;
  goalCount[1] = 0;

  pesTheme = GetConfiguration()->Get("scoreboard_theme", "default") == std::string("pes");
  if (pesTheme)
    ConstructPesTheme();
  else
    ConstructDefaultTheme();

  SetGoalCount(0, 0);
  SetGoalCount(1, 0);

  this->Show();
}

void Gui2ScoreBoard::ConstructDefaultTheme() {
  Vector3 textColor = 255;
  Vector3 textOutlineColor = 0;

  // percentages – score panel centered around 50%
  float xOffset[8];
  xOffset[0] = 1;   // league logo (far left)
  xOffset[1] = 5;   // time (near left)
  xOffset[2] = 34;  // t1 logo
  xOffset[3] = 38;  // t1 name
  xOffset[4] = 44;  // t1 score
  xOffset[5] = 48;  // t2 logo
  xOffset[6] = 52;  // t2 name
  xOffset[7] = 58;  // t2 score
  float content_xOffset = 0.2f;

  constexpr float kScoreboardBackgroundAspectRatio = 1024.0f / 64.0f;
  const float scoreboardBackgroundWidth = windowManager->GetWidthPercentForHeight(
      height_percent, kScoreboardBackgroundAspectRatio);
  const float scoreboardBackgroundX = (width_percent - scoreboardBackgroundWidth) * 0.5f;
  Gui2Image* bg = new Gui2Image(windowManager, "image_scoreboard_bg",
                                scoreboardBackgroundX, 0, scoreboardBackgroundWidth,
                                height_percent);
  bg->LoadImage("media/menu/scoreboard_bg.png");
  this->AddView(bg);
  bg->Show();

  const float squareLogoWidth = windowManager->GetWidthPercentForHeight(height_percent, 1.0f);
  leagueLogo = new Gui2Image(windowManager, "game_scoreboard_leaguelogo", xOffset[0], 0,
                             squareLogoWidth, height_percent);
  this->AddView(leagueLogo);
  leagueLogo->LoadImage("media/menu/league.png");
  leagueLogo->Show();

  timeCaption = new Gui2Caption(windowManager, "game_scoreboard_timecaption",
                                xOffset[1] + content_xOffset, 0, 5, height_percent * 0.9f, "0:00");
  teamNameCaption[0] =
      new Gui2Caption(windowManager, "game_scoreboard_team1name", xOffset[3] + content_xOffset, 0,
                      5, height_percent * 0.9f, match->GetTeam(0)->GetTeamData()->GetShortName());
  teamNameCaption[1] =
      new Gui2Caption(windowManager, "game_scoreboard_team2name", xOffset[6] + content_xOffset, 0,
                      5, height_percent * 0.9f, match->GetTeam(1)->GetTeamData()->GetShortName());
  goalCountCaption[0] =
      new Gui2Caption(windowManager, "game_scoreboard_team1goals", xOffset[4] + content_xOffset, 0,
                      5, height_percent * 0.9f, "0");
  goalCountCaption[1] =
      new Gui2Caption(windowManager, "game_scoreboard_team2goals", xOffset[7] + content_xOffset, 0,
                      5, height_percent * 0.9f, "0");

  timeCaption->SetColor(textColor);
  timeCaption->SetOutlineColor(textOutlineColor);
  teamNameCaption[0]->SetColor(textColor);
  teamNameCaption[0]->SetOutlineColor(textOutlineColor);
  teamNameCaption[1]->SetColor(textColor);
  teamNameCaption[1]->SetOutlineColor(textOutlineColor);
  goalCountCaption[0]->SetColor(textColor);
  goalCountCaption[0]->SetOutlineColor(textOutlineColor);
  goalCountCaption[1]->SetColor(textColor);
  goalCountCaption[1]->SetOutlineColor(textOutlineColor);

  const float tvLogoWidth = windowManager->GetWidthPercentForHeight(height_percent, 2.0f);
  tvLogo = new Gui2Image(windowManager, "game_scoreboard_tvlogo",
                         width_percent - tvLogoWidth, 0, tvLogoWidth, height_percent);
  this->AddView(tvLogo);
  tvLogo->LoadImage("media/menu/tvlogo.png");
  tvLogo->Show();

  this->AddView(timeCaption);
  timeCaption->Show();
  this->AddView(teamNameCaption[0]);
  teamNameCaption[0]->Show();
  this->AddView(teamNameCaption[1]);
  teamNameCaption[1]->Show();
  this->AddView(goalCountCaption[0]);
  goalCountCaption[0]->Show();
  this->AddView(goalCountCaption[1]);
  goalCountCaption[1]->Show();

  teamLogo[0] = new Gui2Image(windowManager, "game_scoreboard_team1logo", xOffset[2], 0,
                              squareLogoWidth, height_percent);
  this->AddView(teamLogo[0]);
  teamLogo[0]->LoadImage(match->GetTeam(0)->GetTeamData()->GetLogoUrl());
  teamLogo[0]->Show();

  teamLogo[1] = new Gui2Image(windowManager, "game_scoreboard_team2logo", xOffset[5], 0,
                              squareLogoWidth, height_percent);
  this->AddView(teamLogo[1]);
  teamLogo[1]->LoadImage(match->GetTeam(1)->GetTeamData()->GetLogoUrl());
  teamLogo[1]->Show();
}

void Gui2ScoreBoard::ConstructPesTheme() {
  // Compact centered top bar ([logo] NAME 0-0 NAME [logo]) with a clock pill
  // on its left, in the PES21 licence-skin art (data/media/ui/pes/, exported
  // by tools/pes21_import/export_scoreboard_theme.py).
  const Vector3 textColor = 255;
  const Vector3 textOutlineColor = Vector3(8, 12, 24);

  constexpr float kBarAspect = 2000.0f / 197.0f;
  constexpr float kClockAspect = 160.0f / 41.0f;
  constexpr float kChipAspect = 150.0f / 41.0f;

  const float barHeight = height_percent;  // 4%
  const float barWidth = windowManager->GetWidthPercentForHeight(barHeight, kBarAspect);
  const float barX = (width_percent - barWidth) * 0.5f;
  const float centerX = width_percent * 0.5f;

  barImage = new Gui2Image(windowManager, "game_scoreboard_pes_bar", barX, 0, barWidth, barHeight);
  barImage->LoadImage("media/ui/pes/scoreboard_bar.png");
  this->AddView(barImage);
  barImage->Show();

  // score digits, dead center
  const float scoreHeight = 2.4f;
  const float scoreWidth = 7.0f;
  scoreText = new Gui2BitmapText(windowManager, "game_scoreboard_pes_score", centerX - scoreWidth * 0.5f,
                                 (barHeight - scoreHeight) * 0.5f, scoreWidth, scoreHeight,
                                 "media/ui/pes/num_match.fnt");
  scoreText->SetText("0-0");
  this->AddView(scoreText);
  scoreText->Show();

  // 3-letter team codes either side of the score
  const float nameHeight = 2.0f;
  const float nameCenterOffset = 5.6f;
  for (int i = 0; i < 2; i++) {
    teamNameCaption[i] = new Gui2Caption(windowManager,
                                         i == 0 ? "game_scoreboard_team1name" : "game_scoreboard_team2name",
                                         0, (barHeight - nameHeight) * 0.5f, 8, nameHeight,
                                         match->GetTeam(i)->GetTeamData()->GetShortName());
    teamNameCaption[i]->SetColor(textColor);
    teamNameCaption[i]->SetOutlineColor(textOutlineColor);
    const float nameCenter = centerX + (i == 0 ? -nameCenterOffset : nameCenterOffset);
    teamNameCaption[i]->SetPosition(nameCenter - teamNameCaption[i]->GetTextWidthPercent() * 0.5f,
                                    (barHeight - nameHeight) * 0.5f);
    this->AddView(teamNameCaption[i]);
    teamNameCaption[i]->Show();
  }

  // team logos tucked into the bar's rounded ends
  const float logoHeight = 2.6f;
  const float logoWidth = windowManager->GetWidthPercentForHeight(logoHeight, 1.0f);
  const float logoMargin = 1.0f;
  for (int i = 0; i < 2; i++) {
    const float logoX =
        i == 0 ? barX + logoMargin : barX + barWidth - logoMargin - logoWidth;
    teamLogo[i] = new Gui2Image(windowManager,
                                i == 0 ? "game_scoreboard_team1logo" : "game_scoreboard_team2logo",
                                logoX, (barHeight - logoHeight) * 0.5f, logoWidth, logoHeight);
    teamLogo[i]->LoadImage(match->GetTeam(i)->GetTeamData()->GetLogoUrl());
    this->AddView(teamLogo[i]);
    teamLogo[i]->Show();
  }

  // clock pill left of the bar
  const float clockHeight = 3.0f;
  const float clockWidth = windowManager->GetWidthPercentForHeight(clockHeight, kClockAspect);
  const float clockX = barX - clockWidth - 0.8f;
  const float clockY = (barHeight - clockHeight) * 0.5f;
  clockPanel = new Gui2Image(windowManager, "game_scoreboard_pes_clockpanel", clockX, clockY,
                             clockWidth, clockHeight);
  clockPanel->LoadImage("media/ui/pes/clock_panel.png");
  this->AddView(clockPanel);
  clockPanel->Show();

  const float clockTextHeight = 1.7f;
  clockText = new Gui2BitmapText(windowManager, "game_scoreboard_pes_clock", clockX + 0.3f,
                                 clockY + (clockHeight - clockTextHeight) * 0.5f, clockWidth - 0.6f,
                                 clockTextHeight, "media/ui/pes/num_mid.fnt");
  clockText->SetText("00:00");
  this->AddView(clockText);
  clockText->Show();

  // added-time chip, only visible while the clock holds ("45:00 +0:12")
  const float chipHeight = 2.6f;
  const float chipWidth = windowManager->GetWidthPercentForHeight(chipHeight, kChipAspect);
  const float chipX = clockX - chipWidth - 0.5f;
  const float chipY = (barHeight - chipHeight) * 0.5f;
  addedTimePanel = new Gui2Image(windowManager, "game_scoreboard_pes_addedpanel", chipX, chipY,
                                 chipWidth, chipHeight);
  addedTimePanel->LoadImage("media/ui/pes/addedtime_panel.png");
  this->AddView(addedTimePanel);

  const float chipTextHeight = 1.5f;
  addedTimeText = new Gui2BitmapText(windowManager, "game_scoreboard_pes_addedtime", chipX + 0.3f,
                                     chipY + (chipHeight - chipTextHeight) * 0.5f, chipWidth - 0.6f,
                                     chipTextHeight, "media/ui/pes/num_mid.fnt");
  addedTimeText->SetText("+0:00");
  this->AddView(addedTimeText);
}

Gui2ScoreBoard::~Gui2ScoreBoard() {
  // widgets are cleaned up while deleting the gui2 tree
}

void Gui2ScoreBoard::GetImages(std::vector<boost::intrusive_ptr<Image2D>>& target) {
  Gui2View::GetImages(target);
}

void Gui2ScoreBoard::Redraw() {}

void Gui2ScoreBoard::SetTimeStr(const std::string& timeStr) {
  if (this->timeStr == timeStr)
    return;
  this->timeStr = timeStr;

  if (pesTheme) {
    // FormatClock: "12:34", or "45:00 +0:12" past the scheduled period end
    const std::string::size_type space = timeStr.find(' ');
    clockText->SetText(space == std::string::npos ? timeStr : timeStr.substr(0, space));
    const bool showAddedTime = space != std::string::npos;
    if (showAddedTime)
      addedTimeText->SetText(timeStr.substr(space + 1));
    if (showAddedTime != addedTimeVisible) {
      addedTimeVisible = showAddedTime;
      if (showAddedTime) {
        addedTimePanel->Show();
        addedTimeText->Show();
      } else {
        addedTimePanel->Hide();
        addedTimeText->Hide();
      }
    }
  } else {
    timeCaption->SetCaption(timeStr);
  }
}

void Gui2ScoreBoard::SetGoalCount(int teamID, int goalCount) {
  this->goalCount[teamID] = goalCount;

  if (pesTheme) {
    scoreText->SetText(int_to_str(this->goalCount[0]) + "-" + int_to_str(this->goalCount[1]));
    return;
  }

  std::string goalStr = "";
  if (goalCount < 10)
    goalStr.append(" ");
  goalStr.append(int_to_str(goalCount));
  goalCountCaption[teamID]->SetCaption(goalStr);
}
