// FaceRig: applies imported PES facial expressions (.faceanim poses over an
// skf_* weight map) to a player's own fullbody geometry. v1 is
// translation-only (PES's face rig is muscle-translation dominant) and
// re-deforms on expression changes rather than per frame.

#ifndef _HPP_HUMANOID_FACERIG
#define _HPP_HUMANOID_FACERIG

#include <array>
#include <map>
#include <string>
#include <vector>

#include "scene/objects/geometry.hpp"
#include "utils/facerigdata.hpp"

namespace blunted {

enum class e_FaceExpression { Neutral, Happy, Sad, Exert, Pain };

// Which face to wear, from what the body is doing. Its own function so the rule is
// testable: it used to sit inline in HumanoidBase::Process, where a player who had
// just been hurt went on smiling because nothing asked.
//
// `animType` and `specialVar1` are the current clip's own variables ("special" and
// 1 or 2 for a celebration's mood), `speed` is metres per second, `injuryLevel` is
// what PlayerBase carries after a foul.
inline e_FaceExpression ChooseExpression(const std::string& animType,
                                        const std::string& specialVar1, float speed,
                                        float injuryLevel) {
  // Being hurt wins over everything else: a player limping after a foul was still
  // wearing whatever the last clip put on him.
  if (injuryLevel > 0.0f)
    return e_FaceExpression::Pain;
  if (animType == "special")
    return specialVar1 == "2" ? e_FaceExpression::Sad : e_FaceExpression::Happy;
  // Sprinting, not merely running: the threshold is the engine's own sprint speed.
  if (speed > 7.0f)
    return e_FaceExpression::Exert;
  return e_FaceExpression::Neutral;
}

class FaceRig {
public:
  // modelDir: the player's custom-model directory; loads faceweights.txt
  // and expression poses from <modelDir>/expressions/*.faceanim. Returns
  // false (rig disabled) when the files are absent.
  bool Load(const std::string& modelDir);

  // Binds weighted face vertices to the geometry's mesh vertices by position
  // (epsilon match). `sources` are the arrays skinning reads, one per submesh in
  // the geometry's submesh order and laid out like them: the offsets are written
  // there, in bind space, so every skin carries the expression and the face moves
  // with the head. They used to be added to the skinned output, which the next
  // skin overwrote - an expression lasted one frame.
  void Bind(boost::intrusive_ptr<Geometry> geometry, std::vector<float*> sources);

  // Applies the expression's offsets when it differs from the current one.
  void SetExpression(e_FaceExpression expression);

  bool IsActive() const { return active; }

private:
  void ApplyOffsets(const std::vector<std::array<float, 3>>& offsets);

  FaceRigData data;
  std::map<e_FaceExpression,
           std::map<std::string, std::array<float, 3>>> poses;
  // per weighted vertex: (subgeom, float offset into vertex array) matches
  std::vector<std::vector<std::pair<int, int>>> meshBindings;
  std::vector<float*> sources;
  boost::intrusive_ptr<Geometry> boundGeometry;
  e_FaceExpression current = e_FaceExpression::Neutral;
  bool active = false;
};

}  // namespace blunted

#endif
