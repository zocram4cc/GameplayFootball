#include "banner.hpp"

#include <algorithm>

#include "../../data/teamdata.hpp"
#include "../../onthepitch/match.hpp"
#include "../../onthepitch/team.hpp"
#include "bannerpresentation.hpp"
#include "captionfit.hpp"
#include "main.hpp"
#include "utils/gui2/windowmanager.hpp"

namespace blunted {
namespace {

// The strip is anchored to the scoreboard, which lives at 2, 2 and is 6.0 tall
// in the PES theme, 4.0 in the default one (see scoreboard.cpp).
constexpr float kScoreboardX = 2.0f;
constexpr float kScoreboardY = 2.0f;
constexpr float kScoreboardHeightPes = 6.0f;
constexpr float kScoreboardHeightDefault = 4.0f;

constexpr float kAccentWidth = 0.55f;  // the team-colour tab down the left edge
constexpr float kPadX = 0.8f;          // text inset, left of the text and right of it
// kAccentWidth + 2 * kPadX; kept in step with BannerPresentation's chrome
// allowance, which is what sizes the panel around the measured text.
static_assert(kAccentWidth + 2.0f * kPadX <= BannerPresentation::kNotificationChromeWidth,
              "the panel must be at least as wide as its own chrome");

// Deliberately small: this is a notification under the scoreboard, not the
// lower-third it replaced (whose title line alone was around 3.5 percent).
constexpr float kLineHeight = 2.6f;
constexpr float kSubtitleHeight = 2.2f;
constexpr float kMinTextHeight = 1.5f;  // floor before FitCaption starts cutting
constexpr float kInlineGap = 0.7f;      // between the team tag and the title

constexpr unsigned long kFadeIn_ms = 220;
constexpr unsigned long kFadeOut_ms = 400;

float ScoreboardHeight() {
  return GetConfiguration()->Get("scoreboard_theme", "default") == std::string("pes")
             ? kScoreboardHeightPes
             : kScoreboardHeightDefault;
}

// Centres a caption in its line: fitting only ever shrinks a caption, so a
// long message that had to be scaled down would otherwise hang off the top of
// its row.
void PlaceInLine(Gui2Caption* caption, float x, float lineTop, float lineHeight) {
  float width, height;
  caption->GetSize(width, height);
  caption->SetPosition(x, lineTop + std::max(0.0f, (lineHeight - height) * 0.5f));
}

}  // namespace

Gui2Banner::Gui2Banner(Gui2WindowManager* windowManager, const std::string& name, Match* match)
    : Gui2View(windowManager, name, 0, 0, 100, 100), match(match) {
  // Content is built in Init(), called by Match once this view is attached
  // to its final parent - see Gui2FormationGraphic::Init() for why.
}

Gui2Banner::~Gui2Banner() {}

void Gui2Banner::Init() {
  // Everything is positioned and sized for real in Show(), which is the only
  // place the message's width is known; these are placeholders under the
  // scoreboard so nothing is ever built at a zero size.
  const float y = kScoreboardY + ScoreboardHeight() + 0.9f;
  const float width = BannerPresentation::kNotificationMinWidth;
  const float height = kLineHeight + 1.0f;

  // AddView() before LoadImage(): a Gui2Image's Redraw() (called from
  // LoadImage) needs to already be attached to the view tree for its content
  // to actually reach the screen (see formationgraphic.cpp for the same fix).
  // Also, everything below stays Show()n for the widget's whole lifetime:
  // cycling Hide()/Show() on a freshly-created Gui2Image left it permanently
  // blank in testing (see Gui2FormationGraphic::Init()'s comment); visibility
  // is conveyed by alpha alone (see ApplyAlpha/Process).
  panel = new Gui2Image(windowManager, "banner_panel", kScoreboardX, y, width, height);
  this->AddView(panel);
  panel->LoadImage("media/ui/pes/banner_panel.png");
  panel->Show();

  accent = new Gui2Image(windowManager, "banner_accent", kScoreboardX, y, kAccentWidth, height);
  this->AddView(accent);
  accent->Show();

  const float textX = kScoreboardX + kAccentWidth + kPadX;

  teamTag = new Gui2Caption(windowManager, "banner_teamtag", textX, y, 8.0f, kLineHeight, "");
  teamTag->SetOutlineColor(Vector3(4, 4, 6));
  this->AddView(teamTag);
  teamTag->Show();

  title = new Gui2Caption(windowManager, "banner_title", textX, y, 20.0f, kLineHeight, "");
  title->SetColor(Vector3(255, 255, 255));
  title->SetOutlineColor(Vector3(4, 4, 6));
  this->AddView(title);
  title->Show();

  subtitle = new Gui2Caption(windowManager, "banner_subtitle", textX, y + kLineHeight, 20.0f,
                             kSubtitleHeight, "");
  subtitle->SetColor(Vector3(235, 190, 90));  // gold/orange, per spec section 4
  subtitle->SetOutlineColor(Vector3(4, 4, 6));
  this->AddView(subtitle);
  subtitle->Show();

  ApplyAlpha(0.0f);

  Gui2View::Show();  // the container itself; the panel stays alpha-0 until Show(...)
}

void Gui2Banner::Show(int teamID, const std::string& titleText, const std::string& subtitleText,
                      int time_ms) {
  const Vector3 team0Color = match->GetTeam(0)->GetTeamData()->GetColor1();
  const Vector3 team1Color = match->GetTeam(1)->GetTeamData()->GetColor1();
  const Vector3 accentColor = BannerPresentation::AccentColor(teamID, team0Color, team1Color);

  // Measure first, then size the panel around what was measured: player names
  // come out of a squad file and can be arbitrarily long, and a fixed panel
  // either cuts a substitution off mid-name or leaves "GOAL" adrift in it.
  const float textMax =
      BannerPresentation::kNotificationMaxWidth - BannerPresentation::kNotificationChromeWidth;

  teamTagVisible = (teamID == 0 || teamID == 1);
  float tagWidth = 0.0f;
  if (teamTagVisible) {
    teamTag->SetCaption(match->GetTeam(teamID)->GetTeamData()->GetShortName());
    teamTag->SetColor(accentColor);
    tagWidth = FitCaption(teamTag, textMax * 0.32f, kLineHeight, kMinTextHeight);
  }

  const float titleIndent = teamTagVisible ? tagWidth + kInlineGap : 0.0f;
  title->SetCaption(titleText);
  const float titleWidth = FitCaption(title, textMax - titleIndent, kLineHeight, kMinTextHeight);

  subtitleVisible = !subtitleText.empty();
  float subtitleWidth = 0.0f;
  if (subtitleVisible) {
    subtitle->SetCaption(subtitleText);
    subtitleWidth = FitCaption(subtitle, textMax, kSubtitleHeight, kMinTextHeight);
  }

  const int lineCount = subtitleVisible ? 2 : 1;
  const BannerPresentation::Rect rect = BannerPresentation::NotificationRect(
      kScoreboardX, kScoreboardY, ScoreboardHeight(),
      std::max(titleIndent + titleWidth, subtitleWidth), lineCount, kLineHeight);

  panel->SetPosition(rect.x, rect.y);
  panel->SetSize(rect.width, rect.height);

  accent->SetPosition(rect.x, rect.y);
  accent->SetSize(kAccentWidth, rect.height);
  int ax, ay, aw, ah;
  windowManager->GetCoordinates(rect.x, rect.y, kAccentWidth, rect.height, ax, ay, aw, ah);
  accent->GetImage2D()->DrawRectangle(0, 0, aw, ah, accentColor, 235);
  accent->GetImage2D()->OnChange();

  const float textX = rect.x + kAccentWidth + kPadX;
  const float textTop = rect.y + (rect.height - lineCount * kLineHeight) * 0.5f;
  if (teamTagVisible) PlaceInLine(teamTag, textX, textTop, kLineHeight);
  PlaceInLine(title, textX + titleIndent, textTop, kLineHeight);
  if (subtitleVisible) PlaceInLine(subtitle, textX, textTop + kLineHeight, kSubtitleHeight);

  shownAt_ms = match->GetActualTime_ms();
  hideAt_ms = shownAt_ms + (unsigned long)std::max(0, time_ms);
  currentAlpha = -1.0f;  // force a fresh ApplyAlpha on the next Process()
}

void Gui2Banner::ApplyAlpha(float alpha) {
  if (alpha == currentAlpha) return;
  currentAlpha = alpha;

  // Shown or hidden, never faded: Surface::SetAlpha multiplies into the
  // alpha channel (sdl_setsurfacealpha), so taking an image down to zero
  // erases its transparency for good and bringing it back up restores
  // nothing. See Gui2FormationGraphic::ApplyAlpha.
  if (alpha > 0.02f) {
    panel->Show();
    accent->Show();
  } else {
    panel->Hide();
    accent->Hide();
  }
  teamTag->SetTransparency(1.0f - (teamTagVisible ? alpha : 0.0f));
  title->SetTransparency(1.0f - alpha);
  subtitle->SetTransparency(1.0f - (subtitleVisible ? alpha : 0.0f));
}

void Gui2Banner::Process() {
  Gui2View::Process();
  if (!match || hideAt_ms == 0) return;  // never shown yet

  const unsigned long now = match->GetActualTime_ms();
  if (now >= hideAt_ms) {
    hideAt_ms = 0;
    ApplyAlpha(0.0f);
    return;
  }
  ApplyAlpha(BannerPresentation::FadeAlpha((long)now - (long)shownAt_ms,
                                          (long)hideAt_ms - (long)now, kFadeIn_ms, kFadeOut_ms));
}

void Gui2Banner::ApplyZOrder() {
  const int base = GetZPriority();
  if (panel) panel->SetZPriority(base);
  if (accent) accent->SetZPriority(base + 1);
  if (teamTag) teamTag->SetZPriority(base + 2);
  if (title) title->SetZPriority(base + 2);
  if (subtitle) subtitle->SetZPriority(base + 2);
}

void Gui2Banner::SetRecursiveZPriority(int prio) {
  Gui2View::SetRecursiveZPriority(prio);
  ApplyZOrder();
}

}  // namespace blunted
