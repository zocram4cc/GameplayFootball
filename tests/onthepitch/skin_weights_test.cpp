// Where a skin weight comes from, and how many bones a vertex may ride.
//
// The engine has always read skin weights out of the ASE's vertex colours, three
// channels, each `jointID * 10 + weight * 9` over 255. That encoding has a hard
// ceiling: floor(255 / 10) = 25 joints, of which the body rig uses twenty. PES's
// hand rig is nineteen bones a hand - thirty-eight across two hands - so the
// fingers cannot be addressed at all through a vertex colour, whatever else is
// given up. The way out is a sidecar weight file beside the .ase, and these tests
// hold three things about it:
//
//   * a model with a sidecar skins from the sidecar, fingers and all;
//   * a model WITHOUT one decodes exactly as it always did, influence for
//     influence and bit for bit - ninety-odd already-converted bodies depend on
//     that, and a "close enough" here is a body that deforms differently;
//   * a sidecar that does not parse is ignored rather than fatal, so a truncated
//     or hand-edited file costs the fingers and not the match.
//
// Four influences is not a guess: it is PES's own maximum. Measured over the base
// package's parts - hand_l, hand_r, arm, glove_pl_short_l, eye, facial, 14,175
// vertices - the count of non-zero bone weights per vertex is 1, 2, 3 or 4 and
// never more (hand_l alone: 761 / 1259 / 772 / 878).

#include <gtest/gtest.h>

#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

#include "base/math/vector3.hpp"
#include "onthepitch/player/humanoid/skinweights.hpp"

using blunted::SkinInfluence;
using blunted::SkinWeights;
using blunted::Vector3;

namespace {

// The decode humanoidbase.cpp has always performed, kept here as the reference so
// the test is anchored to the engine's own arithmetic rather than to a restatement
// of the new code.
std::vector<SkinInfluence> LegacyDecode(const Vector3& colour) {
  float totalWeight = 0.0f;
  int ids[3];
  float weights[3];
  for (int c = 0; c < 3; c++) {
    int jointID = floor(colour.coords[c] * 0.1);
    float weight = (colour.coords[c] - jointID * 10.0) / 9.0;
    ids[c] = jointID;
    weights[c] = weight;
    totalWeight += weight;
  }
  std::vector<SkinInfluence> out;
  for (int c = 0; c < 3; c++) {
    if (weights[c] > 0.01f) out.push_back({ids[c], weights[c] / totalWeight});
  }
  return out;
}

// A colour channel as the writers encode it: joint * 10 + weight * 9, 0..255.
float Channel(int joint, float weight) { return joint * 10.0f + weight * 9.0f; }

std::string TempPath(const char* name) {
  return std::string("/tmp/gf_skinweights_test_") + name;
}

void Write(const std::string& path, const std::string& text) {
  std::ofstream out(path);
  out << text;
}

}  // namespace

TEST(SkinWeights, AVertexColourDecodesExactlyAsItAlwaysDid) {
  // every joint the encoding can name, at a spread of weights
  SkinWeights weights;
  for (int joint = 0; joint <= 25; joint++) {
    for (float w = 0.0f; w <= 1.0f; w += 0.05f) {
      Vector3 colour(Channel(joint, w), Channel((joint + 7) % 26, 1.0f - w),
                     Channel((joint + 13) % 26, 0.5f));
      const std::vector<SkinInfluence> expected = LegacyDecode(colour);
      const std::vector<SkinInfluence> got = SkinWeights::DecodeVertexColour(colour);
      ASSERT_EQ(expected.size(), got.size()) << "joint " << joint << " w " << w;
      for (size_t i = 0; i < expected.size(); i++) {
        EXPECT_EQ(expected[i].jointID, got[i].jointID);
        // bit for bit: a body that skins differently is a regression
        EXPECT_EQ(expected[i].weight, got[i].weight);
      }
    }
  }
}

TEST(SkinWeights, WithoutASidecarEveryVertexComesFromItsColour) {
  SkinWeights weights;
  const Vector3 position(0.5f, 0.25f, 1.0f);
  const Vector3 colour(Channel(15, 0.7f), Channel(14, 0.3f), 0.0f);
  weights.AddVertexColour(position, colour);
  ASSERT_FALSE(weights.HasSidecar());

  const std::vector<SkinInfluence>* found = weights.Find(position);
  ASSERT_NE(found, nullptr);
  const std::vector<SkinInfluence> expected = LegacyDecode(colour);
  ASSERT_EQ(found->size(), expected.size());
  for (size_t i = 0; i < expected.size(); i++) {
    EXPECT_EQ((*found)[i].jointID, expected[i].jointID);
    EXPECT_EQ((*found)[i].weight, expected[i].weight);
  }
}

TEST(SkinWeights, ASidecarCarriesJointsTheColoursCannotName) {
  const std::string path = TempPath("fingers.weights");
  Write(path,
        "# gfweights 1\n"
        "0.500000 0.250000 1.000000 15:0.6 44:0.25 57:0.15\n");
  SkinWeights weights;
  ASSERT_TRUE(weights.LoadSidecar(path));
  EXPECT_TRUE(weights.HasSidecar());

  const std::vector<SkinInfluence>* found =
      weights.Find(Vector3(0.5f, 0.25f, 1.0f));
  ASSERT_NE(found, nullptr);
  ASSERT_EQ(found->size(), 3u);
  EXPECT_EQ((*found)[0].jointID, 15);
  EXPECT_EQ((*found)[1].jointID, 44);   // a finger joint, past the colour ceiling
  EXPECT_EQ((*found)[2].jointID, 57);
  EXPECT_NEAR((*found)[0].weight, 0.6f, 1e-6f);
  EXPECT_NEAR((*found)[1].weight, 0.25f, 1e-6f);
  EXPECT_NEAR((*found)[2].weight, 0.15f, 1e-6f);
}

TEST(SkinWeights, ASidecarWinsOverTheVertexColour) {
  const std::string path = TempPath("override.weights");
  Write(path, "# gfweights 1\n0.000000 0.000000 0.000000 40:1.0\n");
  SkinWeights weights;
  weights.AddVertexColour(Vector3(0, 0, 0), Vector3(Channel(9, 1.0f), 0, 0));
  ASSERT_TRUE(weights.LoadSidecar(path));

  const std::vector<SkinInfluence>* found = weights.Find(Vector3(0, 0, 0));
  ASSERT_NE(found, nullptr);
  ASSERT_EQ(found->size(), 1u);
  EXPECT_EQ((*found)[0].jointID, 40);
}

TEST(SkinWeights, ASidecarKeepsAtMostFourInfluences) {
  // PES's own cap, and what the writers emit; a fifth is the strongest four
  // renormalised rather than a silent truncation at the wrong end.
  const std::string path = TempPath("five.weights");
  Write(path,
        "# gfweights 1\n"
        "1.000000 2.000000 3.000000 1:0.05 2:0.4 3:0.3 4:0.2 5:0.05\n");
  SkinWeights weights;
  ASSERT_TRUE(weights.LoadSidecar(path));
  const std::vector<SkinInfluence>* found = weights.Find(Vector3(1, 2, 3));
  ASSERT_NE(found, nullptr);
  ASSERT_EQ(found->size(), 4u);
  float total = 0.0f;
  for (const SkinInfluence& influence : *found) total += influence.weight;
  EXPECT_NEAR(total, 1.0f, 1e-5f);
  EXPECT_EQ((*found)[0].jointID, 2);   // strongest first
}

TEST(SkinWeights, WeightsAreNormalised) {
  const std::string path = TempPath("unnormalised.weights");
  Write(path, "# gfweights 1\n0.000000 0.000000 0.000000 3:2.0 4:2.0\n");
  SkinWeights weights;
  ASSERT_TRUE(weights.LoadSidecar(path));
  const std::vector<SkinInfluence>* found = weights.Find(Vector3(0, 0, 0));
  ASSERT_NE(found, nullptr);
  ASSERT_EQ(found->size(), 2u);
  EXPECT_NEAR((*found)[0].weight, 0.5f, 1e-6f);
  EXPECT_NEAR((*found)[1].weight, 0.5f, 1e-6f);
}

TEST(SkinWeights, AMissingSidecarIsNotAFailureOfTheModel) {
  SkinWeights weights;
  const Vector3 position(1, 1, 1);
  weights.AddVertexColour(position, Vector3(Channel(3, 1.0f), 0, 0));
  EXPECT_FALSE(weights.LoadSidecar(TempPath("does_not_exist.weights")));
  EXPECT_FALSE(weights.HasSidecar());
  // and the colours still answer
  ASSERT_NE(weights.Find(position), nullptr);
  EXPECT_EQ(weights.Find(position)->at(0).jointID, 3);
}

TEST(SkinWeights, AMalformedSidecarFallsBackToTheColours) {
  const std::string path = TempPath("malformed.weights");
  // no header, a truncated line, a non-numeric weight: nothing usable
  Write(path, "0.0 not-a-number\nrubbish\n");
  SkinWeights weights;
  const Vector3 position(1, 1, 1);
  weights.AddVertexColour(position, Vector3(Channel(3, 1.0f), 0, 0));
  EXPECT_FALSE(weights.LoadSidecar(path));
  EXPECT_FALSE(weights.HasSidecar());
  ASSERT_NE(weights.Find(position), nullptr);
  EXPECT_EQ(weights.Find(position)->at(0).jointID, 3);
}

TEST(SkinWeights, ASidecarWithARottenLineDropsTheLineNotTheFile) {
  const std::string path = TempPath("partial.weights");
  Write(path,
        "# gfweights 1\n"
        "0.000000 0.000000 0.000000 40:1.0\n"
        "1.000000 nonsense here\n"
        "2.000000 0.000000 0.000000 41:1.0\n");
  SkinWeights weights;
  ASSERT_TRUE(weights.LoadSidecar(path));
  ASSERT_NE(weights.Find(Vector3(0, 0, 0)), nullptr);
  ASSERT_NE(weights.Find(Vector3(2, 0, 0)), nullptr);
  EXPECT_EQ(weights.Find(Vector3(2, 0, 0))->at(0).jointID, 41);
}

TEST(SkinWeights, AVertexTheSidecarDoesNotNameStillSkins) {
  // a sidecar covering part of a body must not blank the rest of it
  const std::string path = TempPath("sparse.weights");
  Write(path, "# gfweights 1\n0.000000 0.000000 0.000000 40:1.0\n");
  SkinWeights weights;
  const Vector3 elsewhere(9, 9, 9);
  weights.AddVertexColour(elsewhere, Vector3(Channel(11, 1.0f), 0, 0));
  ASSERT_TRUE(weights.LoadSidecar(path));
  ASSERT_NE(weights.Find(elsewhere), nullptr);
  EXPECT_EQ(weights.Find(elsewhere)->at(0).jointID, 11);
}

TEST(SkinWeights, AnUnknownVertexIsNotFound) {
  SkinWeights weights;
  weights.AddVertexColour(Vector3(0, 0, 0), Vector3(Channel(1, 1.0f), 0, 0));
  EXPECT_EQ(weights.Find(Vector3(5, 5, 5)), nullptr);
}

TEST(SkinWeights, TheSidecarPathSitsBesideTheModel) {
  EXPECT_EQ(SkinWeights::SidecarPath("media/objects/players/models/fullbody_pes.ase"),
            "media/objects/players/models/fullbody_pes.weights");
  EXPECT_EQ(SkinWeights::SidecarPath("no_suffix"), "no_suffix.weights");
}

// A weight file is editable text, and an editable number ends up as an array index.
//
// The colour path could never name a joint past 25 - floor(255 * 0.1) - so the old
// decode was structurally in bounds. The sidecar has no such ceiling: "99:1.0"
// parses cleanly, lands in WeightedBone.jointID, and is used as a raw index into
// jointTransforms every skinning frame. skinweights.hpp promises a hand-edited file
// "costs the fingers, not the match", so the bound has to be enforced here.

TEST(SkinWeights, AJointPastTheSkeletonIsDropped) {
  const std::string path = TempPath("clamp");
  Write(path, "# gfweights 1\n0.500000 0.500000 0.500000 5:0.500000 99:0.500000\n");
  SkinWeights weights;
  ASSERT_TRUE(weights.LoadSidecar(path));
  weights.ClampToJointCount(58);
  const std::vector<SkinInfluence>* found = weights.Find(Vector3(0.5f, 0.5f, 0.5f));
  ASSERT_NE(found, nullptr);
  ASSERT_EQ(found->size(), 1u);
  EXPECT_EQ(found->at(0).jointID, 5);
  EXPECT_FLOAT_EQ(found->at(0).weight, 1.0f);  // renormalised over what survives
}

TEST(SkinWeights, AVertexLosingEveryInfluenceFallsBackToItsColour) {
  const std::string path = TempPath("clamp_all");
  Write(path, "# gfweights 1\n0.500000 0.500000 0.500000 99:1.000000\n");
  SkinWeights weights;
  weights.AddVertexColour(Vector3(0.5f, 0.5f, 0.5f), Vector3(Channel(3, 1.0f), 0, 0));
  ASSERT_TRUE(weights.LoadSidecar(path));
  weights.ClampToJointCount(58);
  const std::vector<SkinInfluence>* found = weights.Find(Vector3(0.5f, 0.5f, 0.5f));
  ASSERT_NE(found, nullptr);
  ASSERT_FALSE(found->empty());
  EXPECT_EQ(found->at(0).jointID, 3);  // the sidecar entry is gone, the colour is not
}

TEST(SkinWeights, ASidecarLineMatchingNoVertexJustDisappears) {
  // Every vertex the engine looks up has a colour - AddVertexColour runs for every
  // vertex read from the ASE - so a clamped-away sidecar line whose position matches
  // no vertex is never asked for. It must simply be gone, not shadow anything.
  const std::string path = TempPath("clamp_bare");
  Write(path, "# gfweights 1\n0.500000 0.500000 0.500000 99:1.000000\n");
  SkinWeights weights;
  ASSERT_TRUE(weights.LoadSidecar(path));
  weights.ClampToJointCount(58);
  EXPECT_FALSE(weights.HasSidecar());
  EXPECT_EQ(weights.Find(Vector3(0.5f, 0.5f, 0.5f)), nullptr);
}

TEST(SkinWeights, TheColourPathIsClampedForALegacySkeleton) {
  // floor(255 * 0.1) = 25, so a corrupt colour can name joint 24 on a 20-node
  // skeleton. Rare, but the same out-of-bounds index.
  SkinWeights weights;
  weights.AddVertexColour(Vector3(1, 2, 3), Vector3(Channel(24, 1.0f), 0, 0));
  weights.ClampToJointCount(20);
  const std::vector<SkinInfluence>* found = weights.Find(Vector3(1, 2, 3));
  ASSERT_NE(found, nullptr);
  ASSERT_EQ(found->size(), 1u);
  EXPECT_EQ(found->at(0).jointID, 0);
}

TEST(SkinWeights, InBoundsInfluencesAreUntouchedByTheClamp) {
  const std::string path = TempPath("clamp_none");
  Write(path, "# gfweights 1\n0.500000 0.500000 0.500000 5:0.250000 44:0.750000\n");
  SkinWeights weights;
  ASSERT_TRUE(weights.LoadSidecar(path));
  weights.ClampToJointCount(58);
  const std::vector<SkinInfluence>* found = weights.Find(Vector3(0.5f, 0.5f, 0.5f));
  ASSERT_NE(found, nullptr);
  ASSERT_EQ(found->size(), 2u);
  EXPECT_FLOAT_EQ(found->at(0).weight + found->at(1).weight, 1.0f);
}

TEST(SkinWeights, AFutureVersionIsRejectedNotHalfRead) {
  const std::string path = TempPath("v10");
  Write(path, "# gfweights 10\n0.5 0.5 0.5 5:1.0\n");
  SkinWeights weights;
  EXPECT_FALSE(weights.LoadSidecar(path));
}

TEST(SkinWeights, ACarriageReturnOnTheHeaderIsForgiven) {
  const std::string path = TempPath("crlf");
  Write(path, "# gfweights 1\r\n0.500000 0.500000 0.500000 5:1.000000\n");
  SkinWeights weights;
  EXPECT_TRUE(weights.LoadSidecar(path));
  EXPECT_TRUE(weights.HasSidecar());
}

// One weight file, two parsers, one truth.
//
// The writer's tests (test_hand_weights.py) and these each pinned their own side of
// the "%.6f position, joint:weight" contract, so the two could drift apart with both
// suites green - the header-exactness asymmetry this fixed was exactly that. The
// golden file under tests/golden/ is read by BOTH, and this asserts the engine's
// side of it; test_hand_weights.GoldenFile asserts the tools read the same numbers.
TEST(SkinWeights, TheGoldenFileReadsAsWritten) {
  SkinWeights weights;
  ASSERT_TRUE(weights.LoadSidecar(std::string(GF_GOLDEN_DIR) + "/gfweights_v1.weights"));
  ASSERT_EQ(weights.SidecarVertexCount(), 2u);
  const std::vector<SkinInfluence>* hand = weights.Find(Vector3(0.604f, -0.07f, 1.064f));
  ASSERT_NE(hand, nullptr);
  ASSERT_EQ(hand->size(), 4u);
  EXPECT_EQ(hand->at(0).jointID, 15);
  EXPECT_FLOAT_EQ(hand->at(0).weight, 0.4f);
  EXPECT_EQ(hand->at(3).jointID, 46);
  EXPECT_FLOAT_EQ(hand->at(3).weight, 0.1f);
  const std::vector<SkinInfluence>* chest = weights.Find(Vector3(0.0f, 0.0f, 1.0f));
  ASSERT_NE(chest, nullptr);
  ASSERT_EQ(chest->size(), 1u);
  EXPECT_EQ(chest->at(0).jointID, 9);
}
