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

enum class e_FaceExpression { Neutral, Happy, Sad, Exert };

class FaceRig {
public:
  // modelDir: the player's custom-model directory; loads faceweights.txt
  // and expression poses from <modelDir>/expressions/*.faceanim. Returns
  // false (rig disabled) when the files are absent.
  bool Load(const std::string& modelDir);

  // Binds weighted face vertices to the geometry's mesh vertices by
  // position (epsilon match), so offsets can be written in place.
  void Bind(boost::intrusive_ptr<Geometry> geometry);

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
  boost::intrusive_ptr<Geometry> boundGeometry;
  e_FaceExpression current = e_FaceExpression::Neutral;
  bool active = false;
};

}  // namespace blunted

#endif
