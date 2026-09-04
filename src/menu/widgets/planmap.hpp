// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#ifndef _HPP_GUI2_VIEW_PLANMAP
#define _HPP_GUI2_VIEW_PLANMAP

#include <vector>

#include "gamedefines.hpp"
#include "gametypes.hpp"
#include "planmapcard.hpp"
#include "planmapinteraction.hpp"
#include "scene/objects/image2d.hpp"
#include "utils/gui2/view.hpp"
#include "utils/gui2/widgets/caption.hpp"
#include "utils/gui2/widgets/image.hpp"

class TeamData;
class PlayerData;
class Match;

namespace blunted {

class Gui2PlanMapEntry : public Gui2View {
public:
  // One starter's card: portrait, position and rating strip, name. portraitHeight is
  // in percent of the page and 0 when the player has no imported portrait.
  Gui2PlanMapEntry(Gui2WindowManager* windowManager, const std::string& name, float x_percent,
                   float y_percent, float width_percent, float height_percent, e_PlayerRole role,
                   PlayerData* playerData, float portraitHeight);
  virtual ~Gui2PlanMapEntry();

  e_PlayerRole GetRole() const { return role; }

  enum e_Highlight { e_Highlight_None, e_Highlight_Selected, e_Highlight_Held };
  // Selected: the card the cursor is on. Held: the card being dragged. Drawn
  // as a coloured border directly on an owned image, the way Gui2Button
  // draws its own focus/toggle border (button.cpp) - no extra art asset.
  void SetHighlight(e_Highlight highlight);

  // Re-prints the position strip, which is where the count of a player's other
  // registered roles lives ("CB+1"). Only the text: the card is not rebuilt,
  // because deleting views inside the event that asked for the change is the
  // use-after-free this file has already been fixed for twice.
  void SetRoleText(const std::string& text);

  // Re-draws the medal and re-tints the name: registering a player for the
  // position he is standing in is exactly what the medal reports, and it is
  // the only visible answer to the secondary button (the count beside the
  // position is of his OTHER positions, which a toggle of this one cannot
  // change).
  void SetAptitude(PlanMapCard::e_Aptitude aptitude);

protected:
  e_PlayerRole role;
  Gui2Image* portraitImage = nullptr;
  Gui2Caption* roleNameCaption = nullptr;
  Gui2Caption* ratingCaption = nullptr;
  Gui2Caption* playerNameCaption = nullptr;
  Gui2Image* highlightBorder = nullptr;
  Gui2Image* medalMark = nullptr;
  Gui2Image* stripBand = nullptr;
};

// The game plan's pitch schematic. Portraits and captions are read-only
// display; picking a card up, moving it and dropping it is handled here
// (see planmapinteraction.hpp for why grab/move/drop rather than a literal
// mouse drag - gui2 has no pointer device).
//
// A drop either lands in open space - the held player's own tactical
// position changes (TeamData::SetFormationEntry) - or on another card -
// the two players trade places (TeamData::SwitchPlayers), which is also
// how a bench player is substituted onto the pitch: the bench strip's
// entries are cards like any other, just beyond the touchline.
class Gui2PlanMap : public Gui2View {
public:

  // Rebuilds every card from TeamData's current FormationEntry array - after
  // an external change such as picking a new formation from the pitch's own
  // 'x' submenu, which the map has no other way to learn about.
  void Refresh();

  // Re-prints one card's position strip after its player's roles changed.
  void RefreshRole(int index);
  Gui2PlanMap(Gui2WindowManager* windowManager, const std::string& name, float x_percent,
              float y_percent, float width_percent, float height_percent, TeamData* teamData);
  virtual ~Gui2PlanMap();

  virtual void Process();
  virtual void ProcessWindowingEvent(WindowingEvent* event);
  virtual void ProcessKeyboardEvent(KeyboardEvent* event);
  virtual void OnGainFocus();
  virtual void OnLoseFocus();

  // Fires on the 'x' key with a card selected (not while dragging), naming
  // the squad index the caller should open a role/formation submenu for.
  boost::signals2::signal<void(int)> sig_OnOpenPlayerMenu;

  // A starter and a bench player were dropped onto each other: (starter slot,
  // bench squad index). Substituting needs the Match - remaining changes, a
  // stoppage, a sent-off player - which a widget has no business knowing, so
  // the page answers it and returns whether it happened. Before kick-off the
  // page just swaps the two in TeamData.
  boost::signals2::signal<bool(int, int)> sig_OnSubstitute;

  // The secondary button on a selected card: register or unregister the
  // position he is standing in (PlayerData::ToggleRole). Reported so the page
  // can say what happened in its hint line.
  boost::signals2::signal<void(int)> sig_OnToggleRole;

  // Whether the pitch has the cursor. The page's hint line says different
  // things depending on it - the cards are dragged, the buttons are pressed -
  // and the pitch can be reached with an arrow key as well as through LINE-UP.
  boost::signals2::signal<void(bool)> sig_OnFocus;

  // Whether the squad index is one of the eleven on the pitch.
  static bool IsStarter(int index) { return index >= 0 && index < playerNum; }

  // True while a card is being dragged - GamePlanPage's own escape handling
  // must not close the page out from under an in-progress drag.
  bool IsDragging() const { return heldIndex != -1; }

protected:
  void RebuildEntries();
  void RepositionEntry(int index);
  void UpdateHighlights();
  // Percent-of-window coordinates of the given slot's card, top-left corner.
  void CardTopLeft(int index, float* x_percent, float* y_percent) const;
  // Where a bench card sits: a strip down the right-hand side, outside the
  // pitch diagram, in squad order. Substituting by dragging is the only way to
  // do it on this screen (the owner asked for it), so the bench has to be on
  // the same map as the eleven rather than in a list of its own.
  PlanMapInteraction::PitchPoint BenchPoint(int benchOrder) const;

  // The bench is one row of cards under the pitch - two rows of them do not fit
  // the band the diagram leaves, and a card any smaller loses the 22px caption
  // floor. A squad with more substitutes than the row holds scrolls: reaching
  // the end of the row shifts the window along, and the cards outside it are
  // hidden rather than drawn on top of each other.
  int BenchWindowSize() const;
  void ScrollBenchTo(int squadIndex);
  void UpdateBenchVisibility();
  int benchWindowStart = 0;
  // Applies a drop of `held` onto `target`, whatever the two are.
  void ResolveDrop(int held, int target);

  boost::intrusive_ptr<Image2D> image;

  int w, h;

  SDL_Surface* bg;

  TeamData* teamData;

  float pitchX = 0.0f, pitchWidth = 0.0f, pitchHeight = 0.0f;

  std::vector<Gui2PlanMapEntry*> entries;
  std::vector<PlanMapInteraction::PitchPoint> points;

  int selectedIndex = 0;
  int heldIndex = -1;
  PlanMapInteraction::PitchPoint dragStartPoint;
};

}  // namespace blunted

#endif
