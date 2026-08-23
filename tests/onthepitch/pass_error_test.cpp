// Pass-error tuning: drilled tiki-taka short passing.
//
// GetPassErrorMultiplier scales pass execution error by philosophy and
// support_distance; GetSupportWebScale spreads the supporting web around the
// ball carrier. With the previous curves a tiki-taka side at its 0.20-0.25
// support preset played at multiplier ~0.76 over a ~0.69-scaled web, and pass
// completion measured near 60%. These pins are the retuned bar from
// docs/superpowers/plans/2026-08-23-pass-play-quality.md Task 2: tight support
// becomes genuinely drilled (<0.65) and the web short (<0.70) at support 0.20.

#include <gtest/gtest.h>

#include "onthepitch/aitactics.hpp"
#include "onthepitch/teamphilosophy.hpp"

// The multiplier feeds trap difficulty in humanoid.cpp, so this single number
// is where "drilled short passing" has to show up measurably.
TEST(PassError, TikiTakaTightSupportIsDrilled) {
  const float multiplier =
      TeamPhilosophy::GetPassErrorMultiplier(TeamPhilosophy::e_Philosophy_TikiTaka, 0.20f);
  EXPECT_LT(multiplier, 0.65f);
}

TEST(PassError, SupportWebScaleAt020IsShort) {
  EXPECT_LT(AITactics::GetSupportWebScale(0.20f), 0.70f);
}

// The retune pulls the tiki-taka lever only in kind, not in kind for the other
// styles' character: long-ball games must stay riskier than the neutral
// middle, and tiki-taka stays steadier than both at the same link length.
TEST(PassError, StyleCharacterIsPreserved) {
  const float neutral =
      TeamPhilosophy::GetPassErrorMultiplier(TeamPhilosophy::e_Philosophy_Balanced, 0.5f);
  const float longBall =
      TeamPhilosophy::GetPassErrorMultiplier(TeamPhilosophy::e_Philosophy_Balanced, 0.9f);
  const float tikiTaka =
      TeamPhilosophy::GetPassErrorMultiplier(TeamPhilosophy::e_Philosophy_TikiTaka, 0.5f);

  EXPECT_LT(tikiTaka, neutral);
  EXPECT_GT(longBall, neutral);
  EXPECT_GT(longBall, 1.0f);
}

TEST(PassError, MultiplierStaysWithinSaneBounds) {
  for (int i = 0; i < TeamPhilosophy::e_Philosophy_Count; i++) {
    for (float support = 0.0f; support <= 1.0f; support += 0.25f) {
      const float multiplier = TeamPhilosophy::GetPassErrorMultiplier(
          static_cast<TeamPhilosophy::e_Philosophy>(i), support);
      EXPECT_GE(multiplier, 0.45f);
      EXPECT_LE(multiplier, 1.4f);
    }
  }
}
