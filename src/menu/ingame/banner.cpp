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

constexpr float kBannerAspect = 620.0f / 150.0f;  // width / height, baked asset
constexpr float kSlotWidth = 27.0f;
constexpr float kAccentWidth = 0.9f;
constexpr float kBottomMargin = 3.0f;
constexpr unsigned long kFadeIn_ms = 220;
constexpr unsigned long kFadeOut_ms = 400;

}  // namespace

Gui2Banner::Gui2Banner(Gui2WindowManager* windowManager, const std::string& name, Match* match)
    : Gui2View(windowManager, name, 0, 0, 100, 100), match(match) {
  // Content is built in Init(), called by Match once this view is attached
  // to its final parent - see Gui2FormationGraphic::Init() for why.
}

Gui2Banner::~Gui2Banner() {}

void Gui2Banner::Init() {
  // Mirrors the persistent player-HUD convention (team A bottom-left, team B
  // bottom-right); the centre slot is for team-less messages.
  BuildSlot(0, 2.0f, kSlotWidth, false);                          // Left  (team 0)
  BuildSlot(1, (100.0f - kSlotWidth) * 0.5f, kSlotWidth, false);   // Center (no team)
  BuildSlot(2, 100.0f - kSlotWidth - 2.0f, kSlotWidth, false);     // Right (team 1)

  Gui2View::Show();  // the container itself; slots stay alpha-0 until Show(teamID, ...)
}

void Gui2Banner::BuildSlot(int index, float x, float width, bool /*alignRight*/) {
  Slot& slot = slots[index];
  const float height = windowManager->GetHeightPercentForWidth(width, kBannerAspect);
  const float y = 100.0f - height - kBottomMargin;

  // AddView() before LoadImage(): a Gui2Image's Redraw() (called from
  // LoadImage) needs to already be attached to the view tree for its content
  // to actually reach the screen (see formationgraphic.cpp for the same fix).
  // Also, everything below stays Show()n for the widget's whole lifetime:
  // cycling Hide()/Show() on a freshly-created Gui2Image left it permanently
  // blank in testing (see Gui2FormationGraphic::Init()'s comment); visibility
  // is conveyed by alpha alone (see ApplySlotAlpha/Process).
  slot.panel = new Gui2Image(windowManager, "banner_panel" + int_to_str(index), x, y, width, height);
  this->AddView(slot.panel);
  slot.panel->LoadImage("media/ui/pes/banner_panel.png");
  slot.panel->Show();

  slot.accent =
      new Gui2Image(windowManager, "banner_accent" + int_to_str(index), x, y, kAccentWidth, height);
  this->AddView(slot.accent);
  slot.accent->Show();

  const float textX = x + kAccentWidth + width * 0.045f;
  const float textWidth = width - kAccentWidth - width * 0.09f;
  slot.textX = textX;
  slot.textWidth = textWidth;

  slot.teamTag = new Gui2Caption(windowManager, "banner_teamtag" + int_to_str(index), textX,
                                y + height * 0.11f, textWidth, height * 0.22f, "");
  slot.teamTag->SetOutlineColor(Vector3(4, 4, 6));
  this->AddView(slot.teamTag);
  slot.teamTag->Show();

  slot.title = new Gui2Caption(windowManager, "banner_title" + int_to_str(index), textX,
                              y + height * 0.36f, textWidth, height * 0.30f, "");
  slot.title->SetColor(Vector3(255, 255, 255));
  slot.title->SetOutlineColor(Vector3(4, 4, 6));
  this->AddView(slot.title);
  slot.title->Show();

  slot.subtitle = new Gui2Caption(windowManager, "banner_subtitle" + int_to_str(index), textX,
                                 y + height * 0.70f, textWidth, height * 0.21f, "");
  slot.subtitle->SetColor(Vector3(235, 190, 90));  // gold/orange, per spec section 4
  slot.subtitle->SetOutlineColor(Vector3(4, 4, 6));
  this->AddView(slot.subtitle);
  slot.subtitle->Show();

  ApplySlotAlpha(slot, 0.0f);
}

void Gui2Banner::Show(int teamID, const std::string& title, const std::string& subtitle,
                      int time_ms) {
  const int index = static_cast<int>(BannerPresentation::SlotForTeam(teamID));
  Slot& slot = slots[index];

  const Vector3 team0Color = match->GetTeam(0)->GetTeamData()->GetColor1();
  const Vector3 team1Color = match->GetTeam(1)->GetTeamData()->GetColor1();
  const Vector3 accentColor = BannerPresentation::AccentColor(teamID, team0Color, team1Color);

  int ax, ay, aw, ah;
  windowManager->GetCoordinates(0, 0, kAccentWidth,
                                windowManager->GetHeightPercentForWidth(kSlotWidth, kBannerAspect),
                                ax, ay, aw, ah);
  slot.accent->GetImage2D()->DrawRectangle(0, 0, aw, ah, accentColor, 235);
  slot.accent->GetImage2D()->OnChange();

  const float bannerHeight = windowManager->GetHeightPercentForWidth(kSlotWidth, kBannerAspect);

  slot.teamTagVisible = (teamID == 0 || teamID == 1);
  if (slot.teamTagVisible) {
    slot.teamTag->SetCaption(match->GetTeam(teamID)->GetTeamData()->GetShortName());
    slot.teamTag->SetColor(accentColor);
    FitAndLeftAlignCaption(slot.teamTag, slot.textX, slot.textWidth, bannerHeight * 0.22f,
                           bannerHeight * 0.13f);
  }

  // Player names come out of a squad file and can be arbitrarily long: fit
  // them to the panel rather than letting Gui2Caption resize itself out past
  // the artwork (see captionfit.hpp).
  slot.title->SetCaption(title);
  FitAndLeftAlignCaption(slot.title, slot.textX, slot.textWidth, bannerHeight * 0.30f,
                         bannerHeight * 0.17f);

  slot.subtitleVisible = !subtitle.empty();
  if (slot.subtitleVisible) {
    slot.subtitle->SetCaption(subtitle);
    FitAndLeftAlignCaption(slot.subtitle, slot.textX, slot.textWidth, bannerHeight * 0.21f,
                           bannerHeight * 0.12f);
  }

  slot.shownAt_ms = match->GetActualTime_ms();
  slot.hideAt_ms = slot.shownAt_ms + (unsigned long)std::max(0, time_ms);
  slot.currentAlpha = -1.0f;  // force a fresh ApplySlotAlpha on the next Process()
}

void Gui2Banner::ApplySlotAlpha(Slot& slot, float alpha) {
  if (alpha == slot.currentAlpha) return;
  slot.currentAlpha = alpha;

  // See Gui2FormationGraphic::ApplyAlpha for why images use a binary
  // threshold instead of a continuous fade.
  const float imageAlpha = alpha > 0.02f ? 1.0f : 0.0f;
  slot.panel->GetImage2D()->SetAlpha(imageAlpha);
  slot.accent->GetImage2D()->SetAlpha(imageAlpha);
  slot.teamTag->SetTransparency(1.0f - (slot.teamTagVisible ? alpha : 0.0f));
  slot.title->SetTransparency(1.0f - alpha);
  slot.subtitle->SetTransparency(1.0f - (slot.subtitleVisible ? alpha : 0.0f));
}

void Gui2Banner::Process() {
  Gui2View::Process();
  if (!match) return;

  const unsigned long now = match->GetActualTime_ms();
  for (Slot& slot : slots) {
    if (slot.hideAt_ms == 0) continue;  // never shown yet

    if (now >= slot.hideAt_ms) {
      slot.hideAt_ms = 0;
      ApplySlotAlpha(slot, 0.0f);
      continue;
    }
    const float alpha = BannerPresentation::FadeAlpha((long)now - (long)slot.shownAt_ms,
                                                       (long)slot.hideAt_ms - (long)now, kFadeIn_ms,
                                                       kFadeOut_ms);
    ApplySlotAlpha(slot, alpha);
  }
}

void Gui2Banner::ApplyZOrder() {
  const int base = GetZPriority();
  for (Slot& slot : slots) {
    if (slot.panel) slot.panel->SetZPriority(base);
    if (slot.accent) slot.accent->SetZPriority(base + 1);
    if (slot.teamTag) slot.teamTag->SetZPriority(base + 2);
    if (slot.title) slot.title->SetZPriority(base + 2);
    if (slot.subtitle) slot.subtitle->SetZPriority(base + 2);
  }
}

void Gui2Banner::SetRecursiveZPriority(int prio) {
  Gui2View::SetRecursiveZPriority(prio);
  ApplyZOrder();
}

}  // namespace blunted
