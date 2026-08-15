// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#ifndef _HPP_GUI2_VIEW_SCOREBOARD
#define _HPP_GUI2_VIEW_SCOREBOARD

#include "scene/objects/image2d.hpp"
#include "utils/gui2/view.hpp"
#include "utils/gui2/widgets/bitmaptext.hpp"
#include "utils/gui2/widgets/caption.hpp"
#include "utils/gui2/widgets/image.hpp"

using namespace blunted;

class Match;

class Gui2ScoreBoard : public Gui2View {
public:
  Gui2ScoreBoard(Gui2WindowManager* windowManager, Match* match);
  virtual ~Gui2ScoreBoard();

  void GetImages(std::vector<boost::intrusive_ptr<Image2D>>& target);

  virtual void Redraw();

  void SetTimeStr(const std::string& timeStr);
  void SetGoalCount(int teamID, int goalCount);

protected:
  // "scoreboard_theme" config: current default look
  void ConstructDefaultTheme();
  // "scoreboard_theme" "pes": compact centered bar + clock, PES21 art
  void ConstructPesTheme();

  Match* match;
  bool pesTheme = false;

  std::string timeStr;
  int goalCount[2];

  // default theme
  Gui2Caption* timeCaption = nullptr;
  Gui2Caption* goalCountCaption[2] = {nullptr, nullptr};
  Gui2Image* leagueLogo = nullptr;
  Gui2Image* tvLogo = nullptr;

  // both themes
  Gui2Caption* teamNameCaption[2] = {nullptr, nullptr};
  Gui2Image* teamLogo[2] = {nullptr, nullptr};

  // pes theme
  Gui2Image* barImage = nullptr;
  Gui2Image* clockPanel = nullptr;
  Gui2Image* addedTimePanel = nullptr;
  Gui2BitmapText* scoreText = nullptr;
  Gui2BitmapText* clockText = nullptr;
  Gui2BitmapText* addedTimeText = nullptr;
  bool addedTimeVisible = false;
};

#endif
