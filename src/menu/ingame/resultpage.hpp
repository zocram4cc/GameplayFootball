// The post-match RESULT page - PES's second full-time screen, after the stats
// card has been confirmed (VGL 26 day 12 at 5:15:13; docs/VGL26_DAY12_REFERENCE
// §3.2). A header band with both crests, "Full Time", the score and the clock;
// Player Ratings in two columns of eleven; and five big icon buttons under the
// card: Highlights, Match Records, Rematch, Select Team, Top Menu.
//
// GameOverPage used to draw its buttons under the stats card itself, so the
// two screens PES keeps apart were one here (owner, 07-09).

#ifndef _HPP_MENU_INGAME_RESULTPAGE
#define _HPP_MENU_INGAME_RESULTPAGE

#include <vector>

#include "utils/gui2/page.hpp"
#include "utils/gui2/widgets/button.hpp"
#include "utils/gui2/widgets/caption.hpp"
#include "utils/gui2/widgets/image.hpp"

class Match;

using namespace blunted;

class ResultPage : public Gui2Page {
public:
  ResultPage(Gui2WindowManager* windowManager, const Gui2PageData& pageData);
  virtual ~ResultPage();

  virtual void ProcessWindowingEvent(WindowingEvent* event);

protected:
  void GoHighlights();
  void GoMatchRecords();
  void GoRematch();
  void GoSelectTeam();
  void GoTopMenu();

  Match* match = nullptr;
  std::vector<Gui2View*> owned;
};

#endif
