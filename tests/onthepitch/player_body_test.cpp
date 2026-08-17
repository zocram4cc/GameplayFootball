// Choosing the player body, and surviving its absence.
//
// The default body is the imported PES 2021 base model. Match already fell back
// to the legacy "fullbody" when <name>.object was missing - but the object is a
// wrapper, and the geometry it names is a separate file. Move
// models/fullbody_pes.ase aside and the object is still there, so no fallback
// fires and the ASE loader kills the process:
//
//   [FATAL ERROR] could not open media/objects/players/models/fullbody_pes.ase
//
// which is what happens to anyone who clones the repository, since none of the
// converted PES assets are shipped.
//
// Falling back also has a consequence beyond the filename: the PES body carries
// its own scalp and hair, while the legacy body needs the engine's separate
// hairstyle meshes. Deciding that from the *configured* name rather than the one
// actually loaded leaves a fallback body bald.

#include <gtest/gtest.h>

#include "onthepitch/playerbody.hpp"

TEST(PlayerBody, TheConfiguredBodyIsUsedWhenBothItsFilesAreThere) {
  EXPECT_EQ(PlayerBody::Resolve("fullbody_pes", true, true), "fullbody_pes");
  EXPECT_EQ(PlayerBody::Resolve("lcg_2701", true, true), "lcg_2701");
}

TEST(PlayerBody, AMissingObjectFallsBackToTheLegacyBody) {
  EXPECT_EQ(PlayerBody::Resolve("fullbody_pes", false, true), PlayerBody::kLegacyBody);
}

TEST(PlayerBody, AMissingModelFallsBackToo) {
  // The case that actually killed the process: object present, geometry gone.
  EXPECT_EQ(PlayerBody::Resolve("fullbody_pes", true, false), PlayerBody::kLegacyBody);
}

TEST(PlayerBody, AnEmptySettingIsTheLegacyBody) {
  EXPECT_EQ(PlayerBody::Resolve("", true, true), PlayerBody::kLegacyBody);
}

TEST(PlayerBody, TheLegacyBodyIsNeverFallenBackFrom) {
  // Nothing to fall back to; the caller has to let the loader complain.
  EXPECT_EQ(PlayerBody::Resolve(PlayerBody::kLegacyBody, false, false), PlayerBody::kLegacyBody);
}

TEST(PlayerBody, ThePathsAreWhereTheEngineKeepsThem) {
  EXPECT_EQ(PlayerBody::ObjectPath("fullbody_pes"), "media/objects/players/fullbody_pes.object");
  EXPECT_EQ(PlayerBody::ModelPath("fullbody_pes"),
            "media/objects/players/models/fullbody_pes.ase");
}

TEST(PlayerBody, OnlyTheLegacyBodyWantsTheEnginesHairstyleMeshes) {
  EXPECT_TRUE(PlayerBody::UsesLegacyHairstyles(PlayerBody::kLegacyBody));
  EXPECT_FALSE(PlayerBody::UsesLegacyHairstyles("fullbody_pes"));
  EXPECT_FALSE(PlayerBody::UsesLegacyHairstyles("lcg_2701"));
}

TEST(PlayerBody, AFallbackBodyGetsHairBecauseTheDecisionFollowsWhatLoaded) {
  // The bug this pins down: asking the configured name gives "fullbody_pes",
  // which says no hairstyles, and the legacy body that actually loaded is bald.
  const std::string loaded = PlayerBody::Resolve("fullbody_pes", true, false);
  EXPECT_TRUE(PlayerBody::UsesLegacyHairstyles(loaded));
}
