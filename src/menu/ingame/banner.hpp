// In-match lower-third banner (docs/PRESENTATION_SPEC.md section 4): a
// team-colour-accented panel used for tactical instruction changes,
// substitutions, and referee decisions (booking/sending-off/offside). Three
// independent slots (left/team0, right/team1, center/neutral - see
// bannerpresentation.hpp) so e.g. a substitution banner for one side never
// clobbers a tactics banner for the other.
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

  // Builds the three slots' images/captions. Call once, right after this
  // view has been added to its final parent (root->AddView(...)) - see
  // Gui2FormationGraphic::Init() for why that ordering matters.
  void Init();

  virtual void Process();

  // The accent stripe and the text must stay above the panel plate even
  // though Gui2Task flattens the tree's z-priority every frame - see
  // Gui2View::SetRecursiveZPriority.
  virtual void SetRecursiveZPriority(int prio);

  // teamID -1 for a team-less message (referee/commentary cue, centered);
  // 0/1 for a team-tagged message (bottom-left/bottom-right, that team's
  // colour accent and short name).
  void Show(int teamID, const std::string& title, const std::string& subtitle, int time_ms);

protected:
  static constexpr int kSlotCount = 3;  // BannerPresentation::Slot: Left, Center, Right

  struct Slot {
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
    // Text column, kept so Show() can refit each caption to the panel.
    float textX = 0.0f;
    float textWidth = 0.0f;
  };

  void BuildSlot(int index, float x, float width, bool alignRight);
  void ApplyZOrder();
  void ApplySlotAlpha(Slot& slot, float alpha);
  float ComputeAlpha(const Slot& slot, unsigned long now_ms) const;

  Match* match;
  Slot slots[kSlotCount];
};

}  // namespace blunted

#endif
