// The in-match notification strip (docs/PRESENTATION_SPEC.md section 4): a
// small team-colour-accented panel under the scoreboard, used for tactical
// instruction changes, substitutions, and referee decisions
// (booking/sending-off/offside).
//
// There is one of these, not three. It used to be a set of lower-thirds along
// the bottom edge - team 0 left, team 1 right, neutral centre - but that edge
// is now the PES-style furniture: the player indicator sits bottom-left, its
// opposite number bottom-right, the radar bottom-centre. A banner there covers
// them, and a substitution drew two at once (the team-tagged card over the
// indicator plus the centre strip over the radar). The panel is also sized to
// its text rather than fixed, so a two-name substitution is no longer cut off
// mid-word.
//
// Owned by Match like Gui2ScoreBoard; Match::SpamMessage routes here (see
// match.cpp) so existing call sites (referee.cpp bookings/offside, the
// substitution and tactics-instruction announcements) get the new
// presentation for free. Ticks itself via Process() (see
// src/menu/ingame/formationgraphic.hpp for why that needs no extra
// match.cpp wiring).

#ifndef _HPP_GUI2_VIEW_BANNER
#define _HPP_GUI2_VIEW_BANNER

#include "scene/objects/image2d.hpp"
#include "utils/gui2/view.hpp"
#include "utils/gui2/widgets/caption.hpp"
#include "utils/gui2/widgets/image.hpp"

using namespace blunted;

class Match;

namespace blunted {

class Gui2Banner : public Gui2View {
public:
  Gui2Banner(Gui2WindowManager* windowManager, const std::string& name, Match* match);
  virtual ~Gui2Banner();

  // Builds the panel's images/captions. Call once, right after this view has
  // been added to its final parent (root->AddView(...)) - see
  // Gui2FormationGraphic::Init() for why that ordering matters.
  void Init();

  virtual void Process();

  // The accent stripe and the text must stay above the panel plate even
  // though Gui2Task flattens the tree's z-priority every frame - see
  // Gui2View::SetRecursiveZPriority.
  virtual void SetRecursiveZPriority(int prio);

  // teamID -1 for a team-less message (referee/commentary cue); 0/1 for a
  // team-tagged one, which adds that team's colour accent and short name.
  void Show(int teamID, const std::string& title, const std::string& subtitle, int time_ms);

protected:
  void ApplyZOrder();
  void ApplyAlpha(float alpha);

  Match* match;

  Gui2Image* panel = nullptr;
  Gui2Image* accent = nullptr;
  Gui2Caption* teamTag = nullptr;
  Gui2Caption* title = nullptr;
  Gui2Caption* subtitle = nullptr;
  bool teamTagVisible = false;
  bool subtitleVisible = false;
  unsigned long hideAt_ms = 0;
  unsigned long shownAt_ms = 0;
  float currentAlpha = -1.0f;
};

}  // namespace blunted

#endif
