// The in-match notification strip (docs/PRESENTATION_SPEC.md section 4). Pure
// logic - see src/menu/ingame/bannerpresentation.hpp.
//
// This used to be three lower-thirds along the bottom of the screen, one per
// team plus a centre one. The bottom of the screen now belongs to the PES-style
// furniture - the player indicator bottom-left, its opposite number
// bottom-right, the radar bottom-centre - so a banner down there sat on top of
// them, and a substitution announced itself twice: once as a team-tagged card
// over the indicator, once as the centre strip over the radar. There is one
// notification now, under the scoreboard in the top-left corner, and it is only
// as wide as the message so nothing gets cut off.

#include <gtest/gtest.h>

#include "base/math/vector3.hpp"
#include "menu/ingame/bannerpresentation.hpp"

using blunted::Vector3;
using BannerPresentation::AccentColor;
using BannerPresentation::FadeAlpha;
using BannerPresentation::NotificationRect;
using BannerPresentation::Rect;

namespace {

// the PES-themed scoreboard: x 2, y 2, 4.6 tall (see scoreboard.cpp)
constexpr float kBoardX = 2.0f;
constexpr float kBoardY = 2.0f;
constexpr float kBoardHeight = 4.6f;

Rect UnderTheBoard(float textWidth, int lines, float lineHeight) {
  return NotificationRect(kBoardX, kBoardY, kBoardHeight, textWidth, lines, lineHeight);
}

}  // namespace

TEST(NotificationRectTest, SitsJustUnderTheScoreboardAndSharesItsLeftEdge) {
  const Rect r = UnderTheBoard(12.0f, 1, 2.4f);
  EXPECT_FLOAT_EQ(r.x, kBoardX);
  EXPECT_GT(r.y, kBoardY + kBoardHeight);              // below it, not over it
  EXPECT_LT(r.y, kBoardY + kBoardHeight + 3.0f);       // tucked under, not adrift
}

TEST(NotificationRectTest, ClearsThePlayerIndicatorsAndTheRadar) {
  // The indicators start at y 92 and the radar at y 79 (match.cpp); a two-line
  // notification must finish well above both, wherever the text takes it.
  const Rect r = UnderTheBoard(40.0f, 2, 2.4f);
  EXPECT_LT(r.y + r.height, 79.0f);
}

TEST(NotificationRectTest, IsOnlyAsWideAsTheMessage) {
  const Rect shortOne = UnderTheBoard(14.0f, 1, 2.4f);
  const Rect longOne = UnderTheBoard(32.0f, 1, 2.4f);
  EXPECT_GT(longOne.width, shortOne.width);
  // the text plus its chrome, not a fixed panel the text rattles around in
  EXPECT_NEAR(longOne.width - shortOne.width, 18.0f, 0.001f);
  EXPECT_GT(longOne.width, 32.0f);  // room for the accent tab and padding
}

TEST(NotificationRectTest, StaysWideEnoughToReadEvenForOneWord) {
  const Rect r = UnderTheBoard(0.5f, 1, 2.4f);
  EXPECT_GE(r.width, BannerPresentation::kNotificationMinWidth);
}

TEST(NotificationRectTest, WillNotRunOffAcrossTheScreen) {
  const Rect r = UnderTheBoard(300.0f, 1, 2.4f);
  EXPECT_FLOAT_EQ(r.width, BannerPresentation::kNotificationMaxWidth);
  EXPECT_LE(r.x + r.width, 100.0f);
  // and it must not reach the scoreboard's own right-hand end either
  EXPECT_LE(BannerPresentation::kNotificationMaxWidth, 50.0f);
}

TEST(NotificationRectTest, GrowsByALineWhenThereIsASubtitle) {
  const Rect one = UnderTheBoard(12.0f, 1, 2.4f);
  const Rect two = UnderTheBoard(12.0f, 2, 2.4f);
  EXPECT_NEAR(two.height - one.height, 2.4f, 0.001f);
  EXPECT_GT(one.height, 2.4f);  // padding above and below the line
}

TEST(NotificationRectTest, IsSmallerThanTheLowerThirdItReplaced) {
  // The old panel was 27 percent wide and about 11.6 percent tall at 16:9.
  const Rect r = UnderTheBoard(12.0f, 2, 2.4f);
  EXPECT_LT(r.height, 11.6f);
}

TEST(NotificationRectTest, ALineCountOfZeroIsNotANegativePanel) {
  const Rect r = UnderTheBoard(12.0f, 0, 2.4f);
  EXPECT_GE(r.height, 0.0f);
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

