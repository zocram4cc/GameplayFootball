// Which celebration a scorer performs, and which camera films it.
//
// PES ships the two together. goal_2018_run_30_banzai.chor says its primary actor -
// the scorer - performs dml_goal_move3_0002.anim, and
// goal_2018_run_30_banzai_Z_fromL/R.camtrack are the two angles PES shot that
// performance from. tools/pes21_import/goal_cutscenes.py reads that straight out of
// PES's own files into media/cutscenes/goal/celebrations.txt: 30 celebrations, 19 of
// them filmed, 38 camera tracks, none left over.
//
// The tie is the point. A camera is only right for the celebration it was shot for -
// a lens that catches a player wheeling away with his arms out is not the lens for a
// knee slide - and the engine used to pick a track with
// `(teamID * 7 + score(0) + score(1) * 3) % count`, any camera for any celebration.

#ifndef _HPP_ONTHEPITCH_GOALCELEBRATION
#define _HPP_ONTHEPITCH_GOALCELEBRATION

#include <string>
#include <vector>

namespace GoalCelebration {

struct Celebration {
  std::string name;
  // What the scorer performs.
  std::string clip;
  // The specialvar2 the engine asks for this performance by - the manifest, the
  // installed clip and the controller all carry the same number. 0 for a celebration
  // PES never filmed, which is not individually askable.
  int var = 0;
  // The angles PES shot it from. Empty for the eleven it never filmed, which are
  // still perfectly good performances and are used when a player is assigned one.
  std::vector<std::string> cameras;
};

std::vector<Celebration> Parse(const std::string& text);

// -> an index into `set`, or -1 when there is nothing to choose from.
//
// `assigned` is a player's own celebration, by name, so the same man celebrates the
// same way twice. Without one the seed decides, and only from the filmed set: drawing
// an unfilmed celebration at random would leave the goal with no camerawork at all.
int Choose(const std::vector<Celebration>& set, const std::string& assigned, int seed);

// Which angle to use. `attackingSide` is the side of the pitch being attacked (-1 or
// +1): the camera to take is the one looking back up the pitch the scorer is running
// into, so his crowd is behind him rather than the half he came from.
std::string PickCamera(const Celebration& celebration, int attackingSide, int seed);

// The two halves of a celebration: PES authors an intro and a loop the scorer holds
// until it is over.
enum Phase_e {
  e_Intro = 0,
  e_Loop,
};

// How long the intro is given before the loop takes over. The 40 imported goal intros
// run 330 to 1850 ms (median 1220), so this is the longest of them and no intro is cut
// off; a short one holds its last pose for a moment first, which is the price of not
// threading per-clip lengths through the controller.
const unsigned long kIntroHold_ms = 1900;

Phase_e Phase(unsigned long elapsed_ms, unsigned long introHold_ms);

// The engine asks for a special animation by specialvar1/specialvar2. A loop is the
// same mood with specialvar2 raised by ten, which is how install_anims.py files it.
int LoopVariable(int introVariable);

}  // namespace GoalCelebration

#endif
