// Lower-third banner slot/colour selection (docs/PRESENTATION_SPEC.md
// section 4). Pure logic - see src/menu/ingame/bannerpresentation.hpp.

#include <gtest/gtest.h>

#include "base/math/vector3.hpp"
#include "menu/ingame/bannerpresentation.hpp"

using blunted::Vector3;
using BannerPresentation::AccentColor;
using BannerPresentation::FadeAlpha;
using BannerPresentation::Slot;
using BannerPresentation::SlotForTeam;

TEST(BannerPresentationTest, TeamsGetOppositeSlotsMatchingTheHudConvention) {
  EXPECT_EQ(SlotForTeam(0), Slot::Left);
  EXPECT_EQ(SlotForTeam(1), Slot::Right);
}

TEST(BannerPresentationTest, TeamlessMessagesGoCenter) {
  EXPECT_EQ(SlotForTeam(-1), Slot::Center);
}

TEST(BannerPresentationTest, AccentColorUsesTheTeamsOwnColor) {
  const Vector3 t0(255, 0, 0);
  const Vector3 t1(0, 0, 255);
  EXPECT_EQ(AccentColor(0, t0, t1), t0);
  EXPECT_EQ(AccentColor(1, t0, t1), t1);
}

TEST(BannerPresentationTest, TeamlessMessagesGetAFixedNeutralAccent) {
  const Vector3 t0(255, 0, 0);
  const Vector3 t1(0, 0, 255);
  const Vector3 neutral = AccentColor(-1, t0, t1);
  EXPECT_NE(neutral, t0);
  EXPECT_NE(neutral, t1);
}

TEST(BannerFadeAlphaTest, FullyOpaqueInTheMiddleOfItsLifetime) {
  EXPECT_NEAR(FadeAlpha(1000, 1000, 250, 400), 1.0f, 0.001f);
}

TEST(BannerFadeAlphaTest, RampsUpFromZeroAtTheInstantItAppears) {
  EXPECT_NEAR(FadeAlpha(0, 3000, 250, 400), 0.0f, 0.001f);
  EXPECT_NEAR(FadeAlpha(125, 3000, 250, 400), 0.5f, 0.01f);
}

TEST(BannerFadeAlphaTest, RampsDownToZeroAtTheInstantItHides) {
  EXPECT_NEAR(FadeAlpha(3000, 0, 250, 400), 0.0f, 0.001f);
  EXPECT_NEAR(FadeAlpha(3000, 200, 250, 400), 0.5f, 0.01f);
}

TEST(BannerFadeAlphaTest, PastItsHideTimeIsFullyInvisible) {
  EXPECT_EQ(FadeAlpha(3500, -1, 250, 400), 0.0f);
}

TEST(BannerFadeAlphaTest, ShorterThanBothFadesStillStaysWithinZeroToOne) {
  // A very short-lived banner (fade windows overlap): never exceeds 1, never
  // goes negative.
  for (long t = 0; t <= 300; t += 10) {
    const float a = FadeAlpha(t, 300 - t, 250, 400);
    EXPECT_GE(a, 0.0f);
    EXPECT_LE(a, 1.0f);
  }
}

