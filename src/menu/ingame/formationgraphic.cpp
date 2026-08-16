#include "formationgraphic.hpp"

#include <algorithm>
#include <cmath>

#include "../../data/teamdata.hpp"
#include "../../onthepitch/match.hpp"
#include "../../onthepitch/prematchtimeline.hpp"
#include "../../onthepitch/team.hpp"
#include "captionfit.hpp"
#include "formationgraphiclayout.hpp"
#include "main.hpp"
#include "utils/gui2/windowmanager.hpp"
#include "utils/localization.hpp"

namespace blunted {
namespace {

using FormationGraphicLayout::PanelGeometry;
using FormationGraphicLayout::PanelPoint;

const Vector3 kLineColor(190, 214, 255);
// "Faint connecting lines" (spec 1.1), but drawn over a live pitch rather
// than a flat plate - at the old 70 they were invisible against grass.
constexpr int kLineAlpha = 120;
const Vector3 kSubsHeaderColor(90, 225, 215);
const Vector3 kTextColor(255, 255, 255);
const Vector3 kSubsTextColor(232, 238, 248);
const Vector3 kOutlineColor(6, 10, 24);

// Type sizes, in percent of the window height. Captions are shrunk from
// these to fit their box and never below the matching floor.
constexpr float kTeamTagHeight = 4.0f;
constexpr float kTeamTagMinHeight = 2.4f;
constexpr float kNicknameHeight = 1.7f;
constexpr float kNicknameMinHeight = 1.1f;
constexpr float kSubsHeaderTextHeight = 2.2f;
constexpr float kSubsTextFraction = 0.78f;  // of the row height
constexpr float kSubsMinTextHeight = 1.2f;

// The jersey icon, as a fraction of the narrowest gap between two icons in
// the same row (so it can never touch its neighbour) and, independently, of
// the pitch's height (so a sparse formation does not blow the icons up).
constexpr float kIconOfGap = 0.62f;
constexpr float kIconOfPitchHeight = 0.105f;

std::string SubsRowText(int squadNumber, const std::string& name) {
  return int_to_str(squadNumber) + "  " + name;
}

// Draw order within the panel. Gui2View's default priority is 0, which is
// why the header artwork used to paint straight over the team tag: every
// piece of content now sits explicitly above both background plates.
constexpr int kZPanel = 0;
constexpr int kZHeader = 1;
constexpr int kZContent = 2;
constexpr int kZIcon = 3;
constexpr int kZIconText = 4;

}  // namespace

Gui2FormationGraphic::Gui2FormationGraphic(Gui2WindowManager* windowManager,
                                           const std::string& name, Match* match)
    : Gui2View(windowManager, name, 0, 0, 1, 1), match(match) {
  geometry = FormationGraphicLayout::ComputePanelGeometry(windowManager->GetAspectRatio());
  SetPosition(geometry.panelX, geometry.panelY);
  SetSize(geometry.panelWidth, geometry.panelHeight);

  enabled = GetConfiguration()->GetBool("prematch_formation_graphic", true);
  if (!enabled) this->Hide();
  // Content is built in Init(), called by Match once this view is attached
  // to its final parent - see formationgraphic.hpp for why.
}

// Widgets (panelBg, headerBg, captions, and whatever starters/subLines/
// pitchLines currently exist) are cleaned up by Match::Exit()'s call to
// Exit() before this runs, which recursively destroys the whole GUI2
// subtree - same pattern as Gui2ScoreBoard. Nothing left to do here; in
// particular, do NOT call ClearDynamicViews() here, since that would
// double-delete children Exit() already tore down.
Gui2FormationGraphic::~Gui2FormationGraphic() {}

void Gui2FormationGraphic::Init() {
  if (!enabled) return;

  // The backgrounds are built on the first Process(), not here: an image
  // created while Init() runs never reaches the renderer, while the per-starter
  // icons built later from Process() do. Same widget, same technique - only the
  // moment of creation differs.
  const float tagHeight = std::min(kTeamTagHeight, geometry.headerHeight * 0.62f);
  teamTagCaption = new Gui2Caption(windowManager, "formationgraphic_teamtag", 0,
                                   (geometry.headerHeight - tagHeight) * 0.5f,
                                   geometry.panelWidth, tagHeight, "");
  teamTagCaption->SetColor(kTextColor);
  teamTagCaption->SetOutlineColor(kOutlineColor);
  this->AddView(teamTagCaption);
  teamTagCaption->Show();

  subsHeaderCaption =
      new Gui2Caption(windowManager, "formationgraphic_subsheader", geometry.subsX, geometry.subsY,
                      geometry.subsWidth, kSubsHeaderTextHeight,
                      Localization::GetInstance().Translate("formation_graphic_substitutes"));
  subsHeaderCaption->SetColor(kSubsHeaderColor);
  subsHeaderCaption->SetOutlineColor(kOutlineColor);
  this->AddView(subsHeaderCaption);
  subsHeaderCaption->Show();

  BuildImages();

  // Captions convey visibility through transparency, which re-renders and is
  // reversible. Images cannot: see ApplyAlpha.
  ApplyAlpha(0.0f);

  this->Show();
}

void Gui2FormationGraphic::BuildImages() {
  if (imagesBuilt) return;
  imagesBuilt = true;

  // Every Gui2Image the panel will ever need is created here, before the
  // first frame is drawn, and is only re-pointed afterwards. Two engine
  // behaviours force this:
  //
  //  - Surface::SetAlpha MULTIPLIES into the alpha channel
  //    (sdl_setsurfacealpha), so fading an image to 0 erases its
  //    transparency for good; bringing the alpha back up restores nothing.
  //    Images are therefore hidden with Hide(), never with alpha.
  //  - An image created after the scene has started rendering never reaches
  //    the screen, however correct its position, size and visibility.
  //
  // Captions have neither problem (SetTransparency re-renders from the text),
  // which is why the substitutes list is still built per team.
  panelBg = new Gui2Image(windowManager, "formationgraphic_panel", 0, 0, geometry.panelWidth,
                          geometry.panelHeight);
  this->AddView(panelBg);
  panelBg->LoadImage("media/ui/pes/formation_panel.png");
  panelBg->Show();

  headerBg = new Gui2Image(windowManager, "formationgraphic_header", 0, 0, geometry.panelWidth,
                           geometry.headerHeight);
  this->AddView(headerBg);
  headerBg->LoadImage("media/ui/pes/formation_header.png");
  headerBg->Show();

  const float crestHeight = geometry.headerHeight * 0.72f;
  const float crestWidth = windowManager->GetWidthPercentForHeight(crestHeight, 1.0f);
  const float crestMargin = geometry.panelWidth * 0.02f;
  for (int i = 0; i < 2; i++) {
    crest[i] = new Gui2Image(windowManager, "formationgraphic_crest" + int_to_str(i), crestMargin,
                             (geometry.headerHeight - crestHeight) * 0.5f, crestWidth, crestHeight);
    this->AddView(crest[i]);
    crest[i]->LoadImage(match->GetTeam(i)->GetTeamData()->GetLogoUrl());
    crest[i]->Show();
  }

  pitchLines = new Gui2Image(windowManager, "formationgraphic_pitchlines", geometry.pitchX,
                             geometry.pitchY, geometry.pitchWidth, geometry.pitchHeight);
  this->AddView(pitchLines);
  pitchLines->Show();

  // The eleven jersey icons and their numbers. Their size does not depend on
  // the team - it comes off the tightest row spacing the arrangement is
  // allowed to produce - so it can be settled now and only the positions
  // change per side.
  const float gapWidth =
      FormationGraphicLayout::kMinIconGapPercent * 0.01f * geometry.pitchWidth;
  float iconWidth = gapWidth * kIconOfGap;
  const float maxIconHeight = geometry.pitchHeight * kIconOfPitchHeight;
  iconWidth = std::min(iconWidth, windowManager->GetWidthPercentForHeight(maxIconHeight, 1.0f));
  const float iconHeight = windowManager->GetHeightPercentForWidth(iconWidth, 1.0f);

  for (int i = 0; i < 11; i++) {
    StarterWidgets sw;
    sw.icon = new Gui2Image(windowManager, "formationgraphic_icon" + int_to_str(i), 0, 0, iconWidth,
                            iconHeight);
    this->AddView(sw.icon);
    sw.icon->LoadImage("media/ui/pes/jersey_icon.png");
    sw.icon->Show();

    sw.number = new Gui2BitmapText(windowManager, "formationgraphic_number" + int_to_str(i), 0, 0,
                                   iconWidth * 0.60f, iconHeight * 0.46f,
                                   "media/ui/pes/num_mid.fnt");
    sw.number->SetText("");
    this->AddView(sw.number);
    sw.number->Show();

    starters.push_back(sw);
  }

  ApplyZOrder();
}

void Gui2FormationGraphic::ApplyZOrder() {
  const int base = GetZPriority();
  if (panelBg) panelBg->SetZPriority(base + kZPanel);
  if (headerBg) headerBg->SetZPriority(base + kZHeader);
  for (int i = 0; i < 2; i++)
    if (crest[i]) crest[i]->SetZPriority(base + kZContent);
  if (teamTagCaption) teamTagCaption->SetZPriority(base + kZContent);
  if (subsHeaderCaption) subsHeaderCaption->SetZPriority(base + kZContent);
  if (pitchLines) pitchLines->SetZPriority(base + kZContent);
  if (formationLabel) formationLabel->SetZPriority(base + kZContent);
  if (formationShape) formationShape->SetZPriority(base + kZContent);
  for (Gui2Caption* c : subLines) c->SetZPriority(base + kZContent);
  for (StarterWidgets& s : starters) {
    if (s.icon) s.icon->SetZPriority(base + kZIcon);
    if (s.number) s.number->SetZPriority(base + kZIconText);
    if (s.nickname) s.nickname->SetZPriority(base + kZIconText);
  }
}

void Gui2FormationGraphic::SetRecursiveZPriority(int prio) {
  Gui2View::SetRecursiveZPriority(prio);
  ApplyZOrder();
}

void Gui2FormationGraphic::ClearTextViews() {
  // Only captions are torn down and rebuilt per team; every image the panel
  // owns lives for its whole lifetime (see BuildImages).
  for (Gui2Caption* c : subLines) {
    c->Exit();
    delete c;
  }
  subLines.clear();

  if (formationLabel) {
    formationLabel->Exit();
    delete formationLabel;
    formationLabel = nullptr;
  }
  if (formationShape) {
    formationShape->Exit();
    delete formationShape;
    formationShape = nullptr;
  }
}

void Gui2FormationGraphic::FillForTeam(int teamID) {
  ClearTextViews();

  TeamData* teamData = match->GetTeam(teamID)->GetTeamData();

  // --- header: this side's crest, then the team tag centred in what is left
  shownCrest = teamID;
  const float crestHeight = geometry.headerHeight * 0.72f;
  const float crestWidth = windowManager->GetWidthPercentForHeight(crestHeight, 1.0f);
  const float crestMargin = geometry.panelWidth * 0.02f;
  const float tagLeft = crestMargin + crestWidth;
  teamTagCaption->SetCaption(teamData->GetShortName());
  blunted::FitAndCentreCaption(teamTagCaption, (tagLeft + geometry.panelWidth - crestMargin) * 0.5f,
                      geometry.panelWidth - tagLeft - crestMargin, kTeamTagHeight,
                      kTeamTagMinHeight);

  // --- pitch schematic
  std::vector<Vector3> dbPositions;
  std::vector<e_PlayerRole> roles;
  for (int i = 0; i < 11; i++) {
    const FormationEntry entry = teamData->GetFormationEntry(i);
    dbPositions.push_back(entry.databasePosition);
    roles.push_back(entry.role);
  }
  // Rows, not raw tactical coordinates: see ArrangeFormation's comment for
  // why the schematic is drawn as lines rather than as the literal shape.
  const std::vector<PanelPoint> arranged =
      FormationGraphicLayout::ArrangeFormation(dbPositions, roles);
  const std::vector<FormationGraphicLayout::Connection> connections =
      FormationGraphicLayout::BuildConnections(arranged);

  int x, y, w, h;
  windowManager->GetCoordinates(geometry.pitchX, geometry.pitchY, geometry.pitchWidth,
                                geometry.pitchHeight, x, y, w, h);

  // The shape is redrawn into the image built in BuildImages, wiping the
  // previous side's lines first.
  pitchLines->GetImage2D()->DrawRectangle(0, 0, w, h, Vector3(0, 0, 0), 0);

  auto toPixel = [&](const PanelPoint& p) -> Vector3 {
    return Vector3(p.xPercent * 0.01f * w, p.yPercent * 0.01f * h, 0.0f);
  };

  Image2D* lines = pitchLines->GetImage2D().get();
  for (const auto& c : connections)
    lines->DrawLine(Line(toPixel(arranged[c.fromIndex]), toPixel(arranged[c.toIndex])), kLineColor,
                    kLineAlpha);

  // Goal box under the goalkeeper, forward arc over whoever sits highest up
  // the panel (the lone striker in a single-CF shape; the more advanced of
  // two in a twin-striker shape) - per spec section 1.1.
  int gkIndex = -1, forwardIndex = -1;
  float bestForwardY = 1e9f;
  for (int i = 0; i < 11; i++) {
    if (roles[i] == e_PlayerRole_GK) gkIndex = i;
    if (arranged[i].yPercent < bestForwardY) {
      bestForwardY = arranged[i].yPercent;
      forwardIndex = i;
    }
  }
  if (gkIndex != -1) {
    const Vector3 gk = toPixel(arranged[gkIndex]);
    const float boxHalfW = w * 0.16f;
    const float boxH = h * 0.075f;
    const float boxBottom = std::min((float)h, gk.coords[1] + boxH * 0.9f);
    const float boxTop = boxBottom - boxH;
    lines->DrawLine(Line(Vector3(gk.coords[0] - boxHalfW, boxTop, 0),
                         Vector3(gk.coords[0] - boxHalfW, boxBottom, 0)),
                    kLineColor, kLineAlpha + 40);
    lines->DrawLine(Line(Vector3(gk.coords[0] + boxHalfW, boxTop, 0),
                         Vector3(gk.coords[0] + boxHalfW, boxBottom, 0)),
                    kLineColor, kLineAlpha + 40);
    lines->DrawLine(Line(Vector3(gk.coords[0] - boxHalfW, boxTop, 0),
                         Vector3(gk.coords[0] + boxHalfW, boxTop, 0)),
                    kLineColor, kLineAlpha + 40);
  }
  if (forwardIndex != -1) {
    const Vector3 fwd = toPixel(arranged[forwardIndex]);
    const float radius = w * 0.14f;
    constexpr int kSegments = 8;
    Vector3 prev(fwd.coords[0] - radius, fwd.coords[1], 0);
    for (int seg = 1; seg <= kSegments; seg++) {
      const float t = (float)seg / kSegments * blunted::pi;  // 0..pi, semicircle opening downward
      const Vector3 next(fwd.coords[0] - radius * std::cos(t), fwd.coords[1] - radius * std::sin(t),
                         0);
      lines->DrawLine(Line(prev, next), kLineColor, kLineAlpha + 40);
      prev = next;
    }
  }
  lines->OnChange();

  // --- jersey icons + squad numbers + nicknames
  //
  // The icons already exist (BuildImages); this points them at this side's
  // rows. The width a nickname may occupy comes off the tightest gap the
  // arrangement produced, so names can never collide however the formation
  // is shaped.
  const float gapPercent = FormationGraphicLayout::MinHorizontalGap(arranged);
  const float gapWidth = gapPercent * 0.01f * geometry.pitchWidth;
  float iconWidth = 0.0f, iconHeight = 0.0f;
  if (!starters.empty() && starters[0].icon) starters[0].icon->GetSize(iconWidth, iconHeight);

  for (int i = 0; i < 11 && i < (int)starters.size(); i++) {
    const float px = geometry.pitchX + arranged[i].xPercent * 0.01f * geometry.pitchWidth;
    const float py = geometry.pitchY + arranged[i].yPercent * 0.01f * geometry.pitchHeight;

    StarterWidgets& sw = starters[i];
    sw.icon->SetPosition(px - iconWidth * 0.5f, py - iconHeight * 0.5f);
    // The number sits on the jersey's chest, which is the lower two thirds
    // of the silhouette (the top third is collar and shoulders).
    sw.number->SetPosition(px - iconWidth * 0.30f, py - iconHeight * 0.14f);
    sw.number->SetText(int_to_str(FormationGraphicLayout::SquadNumberForSlot(i)));

    if (sw.nickname) {
      sw.nickname->Exit();
      delete sw.nickname;
    }
    sw.nickname = new Gui2Caption(
        windowManager, "formationgraphic_nick" + int_to_str(i), px - gapWidth * 0.5f,
        py + iconHeight * 0.56f, gapWidth, kNicknameHeight,
        teamData->GetPlayerData(i)->GetLastName());
    sw.nickname->SetColor(kTextColor);
    sw.nickname->SetOutlineColor(kOutlineColor);
    this->AddView(sw.nickname);
    sw.nickname->Show();
    blunted::FitAndCentreCaption(sw.nickname, px, gapWidth * 0.96f, kNicknameHeight,
                                 kNicknameMinHeight);
  }

  // --- substitutes: a numbered list down the left column
  const int benchSize = std::max(0, teamData->GetPlayerNum() - 11);
  const FormationGraphicLayout::SubsLayout subs =
      FormationGraphicLayout::ComputeSubsLayout(benchSize, geometry.subsHeight);

  // Localised, so its width varies: fit it to the column like every other
  // caption on the panel.
  subsHeaderCaption->SetPosition(geometry.subsX, geometry.subsY);
  FitAndLeftAlignCaption(subsHeaderCaption, geometry.subsX, geometry.subsWidth,
                         kSubsHeaderTextHeight, kSubsMinTextHeight);

  const float rowTextHeight = std::max(subs.rowHeight * kSubsTextFraction, kSubsMinTextHeight);
  for (int row = 0; row < subs.rowCount; row++) {
    const int slot = 11 + row;
    const float rowY = geometry.subsY + subs.firstRowY + row * subs.rowHeight;
    Gui2Caption* line =
        new Gui2Caption(windowManager, "formationgraphic_sub" + int_to_str(slot), geometry.subsX,
                        rowY, geometry.subsWidth, rowTextHeight,
                        SubsRowText(FormationGraphicLayout::SquadNumberForSlot(slot),
                                    teamData->GetPlayerData(slot)->GetLastName()));
    line->SetColor(kSubsTextColor);
    line->SetOutlineColor(kOutlineColor);
    this->AddView(line);
    line->Show();
    FitAndLeftAlignCaption(line, geometry.subsX, geometry.subsWidth, rowTextHeight,
                           kSubsMinTextHeight);
    subLines.push_back(line);
  }

  // --- the shape those rows spell out, at the foot of the column
  //
  // A bench shorter than the column leaves a lot of empty panel; this is the
  // line a broadcast graphic would fill it with, and it is free - the rows
  // have already been computed.
  const std::string shape = FormationGraphicLayout::FormationLabel(arranged, roles);
  if (!shape.empty()) {
    const float shapeHeight = 4.4f;
    const float labelHeight = 1.9f;
    const float blockBottom = geometry.subsY + geometry.subsHeight;
    const float labelY = blockBottom - shapeHeight - labelHeight - 0.6f;

    formationLabel = new Gui2Caption(windowManager, "formationgraphic_formationlabel",
                                     geometry.subsX, labelY, geometry.subsWidth, labelHeight,
                                     Localization::GetInstance().Translate("formation_graphic_formation"));
    formationLabel->SetColor(kSubsHeaderColor);
    formationLabel->SetOutlineColor(kOutlineColor);
    this->AddView(formationLabel);
    formationLabel->Show();
    blunted::FitAndCentreCaption(formationLabel, geometry.subsX + geometry.subsWidth * 0.5f,
                        geometry.subsWidth, labelHeight, kSubsMinTextHeight);

    formationShape = new Gui2Caption(windowManager, "formationgraphic_formationshape",
                                     geometry.subsX, labelY + labelHeight + 0.6f,
                                     geometry.subsWidth, shapeHeight, shape);
    formationShape->SetColor(kTextColor);
    formationShape->SetOutlineColor(kOutlineColor);
    this->AddView(formationShape);
    formationShape->Show();
    blunted::FitAndCentreCaption(formationShape, geometry.subsX + geometry.subsWidth * 0.5f,
                        geometry.subsWidth, shapeHeight, 2.0f);
  }

  // Everything above was just created, so it carries Gui2View's default
  // priority until the next frame's tree-wide reset; order it now so the
  // panel never renders even one frame with the header over its own tag.
  ApplyZOrder();
}

void Gui2FormationGraphic::ApplyAlpha(float alpha) {
  if (alpha == currentAlpha) return;
  currentAlpha = alpha;

  // Images are shown or hidden, never faded. Surface::SetAlpha multiplies
  // into the alpha channel (see sdl_setsurfacealpha), so fading a panel or a
  // jersey icon down to zero would erase its transparency permanently and it
  // would never come back - which is exactly how the panel artwork went
  // missing while its captions still drew. Captions cross-fade normally,
  // because SetTransparency re-renders the text from scratch each time.
  const bool visible = alpha > 0.02f;
  auto setVisible = [visible](Gui2View* view) {
    if (!view) return;
    if (visible)
      view->Show();
    else
      view->Hide();
  };

  setVisible(panelBg);
  setVisible(headerBg);
  setVisible(pitchLines);
  for (int i = 0; i < 2; i++) setVisible(crest[i] && i == shownCrest ? crest[i] : nullptr);
  for (int i = 0; i < 2; i++)
    if (crest[i] && i != shownCrest) crest[i]->Hide();
  for (StarterWidgets& s : starters) {
    setVisible(s.icon);
    setVisible(s.number);
    if (s.nickname) s.nickname->SetTransparency(1.0f - alpha);
  }

  teamTagCaption->SetTransparency(1.0f - alpha);
  subsHeaderCaption->SetTransparency(1.0f - alpha);
  for (Gui2Caption* c : subLines) c->SetTransparency(1.0f - alpha);
  if (formationLabel) formationLabel->SetTransparency(1.0f - alpha);
  if (formationShape) formationShape->SetTransparency(1.0f - alpha);
}

void Gui2FormationGraphic::Process() {
  Gui2View::Process();
  if (!enabled) return;

  // Debug: hold the graphic on screen regardless of the entrance schedule, so
  // its artwork can be judged without racing a pre-match window that is over
  // in seconds. "debug_formation_graphic_always" "true".
  static const bool alwaysShow =
      GetConfiguration()->GetBool("debug_formation_graphic_always", false);
  if (alwaysShow) {
    if (builtForTeamID != 0) {
      FillForTeam(0);
      builtForTeamID = 0;
      currentAlpha = -1.0f;
    }
    ApplyAlpha(1.0f);
    return;
  }

  if (!match->IsInEntrance()) {
    if (builtForTeamID != -2) {
      builtForTeamID = -2;
      ApplyAlpha(0.0f);
      ClearTextViews();
    }
    return;
  }

  // Which side's lineup is on air, and how far up its cross-fade, is the
  // presentation timeline's call - a competition's own .timeline file decides
  // where (and whether) these graphics appear. See prematchtimeline.hpp.
  const PrematchTimeline::State beat = match->GetPrematchState();
  int teamID = -1;
  if (beat.overlay == PrematchTimeline::Overlay::FormationHome) teamID = 0;
  else if (beat.overlay == PrematchTimeline::Overlay::FormationAway) teamID = 1;

  if (teamID != builtForTeamID) {
    if (teamID == -1) {
      ClearTextViews();
    } else {
      FillForTeam(teamID);
    }
    currentAlpha = -1.0f;  // force a fresh ApplyAlpha (team/content just changed)
    builtForTeamID = teamID;
  }

  ApplyAlpha(teamID == -1 ? 0.0f : beat.overlayAlpha);
}

}  // namespace blunted
