// The goal sequence's lower-thirds, as PES plays them.
//
// Frame by frame off the reference (youtu.be/ns5C3zpD6Ig, 0:05 and 0:44), a
// goal is not one held shot with the ordinary HUD on it. The picture cuts to
// the scorer and two graphics come up in order, both centred low in frame,
// both over the celebration rather than the pitch:
//
//   1. the SCORE bug   - both crests, both team names and the score, which
//                        ticks over to the new scoreline while it is on air
//                        (the reference shows 0-0 becoming 0-1 under the
//                        scorer, then 1-1 becoming 2-1 at the second goal)
//   2. the SCORER bug  - the scoring side's crest, his squad number and name,
//                        his height and age, and how many he has scored today
//
// They replace each other on the cut, which is why this is one view with two
// layouts rather than two views: only ever one is on screen.
//
// Ticks itself from Process() off the match's own celebration clock, like
// Gui2Banner and Gui2FormationGraphic - no wiring in match.cpp beyond
// construction.

#ifndef _HPP_GUI2_VIEW_GOALBUG
#define _HPP_GUI2_VIEW_GOALBUG

#include "utils/gui2/view.hpp"
#include "utils/gui2/widgets/caption.hpp"
#include "utils/gui2/widgets/image.hpp"

using namespace blunted;

class Match;

namespace blunted {

class Gui2GoalBug : public Gui2View {
public:
  Gui2GoalBug(Gui2WindowManager* windowManager, const std::string& name, Match* match);
  virtual ~Gui2GoalBug();

  // Builds the plates and captions. Call once, after this view is in its
  // final parent (see Gui2Banner::Init).
  void Init();

  virtual void Process();
  virtual void SetRecursiveZPriority(int prio);

  // Which of the two is on air. Public so a test - and the presentation
  // schedule - can name them.
  enum class Stage { None, Score, Scorer };
  static Stage StageAt(unsigned long celebration_ms, unsigned long celebrationLength_ms);

protected:
  void ApplyZOrder();
  void ShowStage(Stage stage);
  void FillScore();
  void FillScorer();

  Match* match;
  Stage stage = Stage::None;
  std::string lastScorerText;

  Gui2Image* plate = nullptr;
  Gui2Image* crest[2] = {nullptr, nullptr};
  Gui2Caption* leftText = nullptr;
  Gui2Caption* centreText = nullptr;
  Gui2Caption* rightText = nullptr;
  Gui2Caption* subText = nullptr;
};

}  // namespace blunted

#endif
