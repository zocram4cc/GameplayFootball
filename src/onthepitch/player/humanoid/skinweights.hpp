// Where a skin weight comes from.
//
// The engine has always read skin weights out of the ASE's vertex colours: three
// channels, each `jointID * 10 + weight * 9` over 255. That has a hard ceiling of
// floor(255 / 10) = 25 joints and three influences, and the body rig alone uses
// twenty. PES's hand rig is nineteen bones a hand, thirty-eight across two hands,
// so no rearrangement of a vertex colour can name a finger.
//
// A model may therefore ship a sidecar weight file beside its .ase - simple
// editable text, one line per vertex:
//
//     # gfweights 1
//     <x> <y> <z> <jointID>:<weight> ...      (up to four influences)
//
// Positions are the ASE's own vertex positions, written with the same six
// decimals, so the same decimal text parses to the same float and the lookup is
// exact. When the sidecar is present it answers for every vertex it names; every
// other vertex, and every model that has no sidecar at all, decodes from the
// vertex colours exactly as before. A sidecar that does not parse is ignored: a
// hand-edited or truncated file costs the fingers, not the match.
//
// Four influences is PES's own maximum, not a guess. Over the base package's parts
// (hand_l, hand_r, arm, glove_pl_short_l, eye, facial - 14,175 vertices) the number
// of non-zero bone weights per vertex is 1, 2, 3 or 4 and never more.

#ifndef _HPP_ONTHEPITCH_PLAYER_HUMANOID_SKINWEIGHTS
#define _HPP_ONTHEPITCH_PLAYER_HUMANOID_SKINWEIGHTS

#include <map>
#include <string>
#include <vector>

#include "base/math/vector3.hpp"

namespace blunted {

struct SkinInfluence {
  int jointID = 0;
  float weight = 0.0f;
};

// As many bones as PES itself weights a vertex to.
static const int kMaxSkinInfluences = 4;

class SkinWeights {
public:
  // The three-channel vertex colour decode, verbatim as humanoidbase.cpp has
  // always performed it: joint = floor(channel / 10), weight = the remainder over
  // nine, anything at or below 0.01 dropped, the rest renormalised over all three.
  static std::vector<SkinInfluence> DecodeVertexColour(const Vector3& colour);

  // "<dir>/<model>.ase" -> "<dir>/<model>.weights".
  static std::string SidecarPath(const std::string& aseFilename);

  // Records one vertex's colour. Called while reading the ASE.
  void AddVertexColour(const Vector3& position, const Vector3& colour);

  // Reads the sidecar for a model. Returns whether it holds anything usable;
  // false leaves this object on the vertex colours alone.
  bool LoadSidecar(const std::string& path);

  bool HasSidecar() const { return !sidecar.empty(); }
  size_t VertexColourCount() const { return colours.size(); }
  size_t SidecarVertexCount() const { return sidecar.size(); }

  // Drops every influence naming a joint the skeleton does not have, and
  // renormalises what survives.
  //
  // The colour decode could never name a joint past 25 - floor(255 * 0.1) - so the
  // old path was structurally in bounds. The sidecar is editable text with no such
  // ceiling, and an influence's jointID is used as a raw index into jointTransforms
  // every skinning frame: "99:1.0" parses cleanly and reads past the end of the
  // array. A sidecar vertex losing everything falls back to its colour; one with no
  // colour either rides the body root, because the engine asserts that every vertex
  // rides something.
  void ClampToJointCount(int jointCount);

  // The influences driving a vertex: the sidecar's if it names this one, else the
  // decoded vertex colour. Null when the model knows nothing about the position.
  const std::vector<SkinInfluence>* Find(const Vector3& position) const;

private:
  std::map<Vector3, std::vector<SkinInfluence>> sidecar;
  std::map<Vector3, std::vector<SkinInfluence>> colours;
};

}  // namespace blunted

#endif
