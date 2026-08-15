#include "formationgraphic.hpp"

#include <algorithm>
#include <cmath>

#include "../../data/teamdata.hpp"
#include "../../onthepitch/match.hpp"
#include "../../onthepitch/team.hpp"
#include "formationgraphiclayout.hpp"
#include "main.hpp"
#include "utils/gui2/windowmanager.hpp"

namespace blunted {
namespace {

// Baked at these exact pixel aspect ratios by
// tools/pes21_import/export_formation_theme.py - the Gui2Image box is sized
// to match so LoadImage's scale-to-fit never distorts the rounded corners.
constexpr float kPanelAspect = 440.0f / 1400.0f;   // width / height
constexpr float kHeaderAspect = 440.0f / 90.0f;    // width / height

const Vector3 kLineColor(180, 205, 255);
constexpr int kLineAlpha = 70;
const Vector3 kSubsHeaderColor(90, 225, 215);

// Recenters a caption horizontally within [left, left+width] - Gui2Caption
// itself left-aligns (see caption.cpp), so callers that want centered text
// reposition after SetCaption, same as scoreboard.cpp does for team names.
void CenterCaption(Gui2Caption* caption, float left, float width) {
  const float textWidth = caption->GetTextWidthPercent();
  float x, y;
  caption->GetPosition(x, y);
  caption->SetPosition(left + std::max(0.0f, (width - textWidth) * 0.5f), y);
}

float ComputeCenteredX(Gui2WindowManager* windowManager) {
  const float width = windowManager->GetWidthPercentForHeight(74, kPanelAspect);
  return (100.0f - width) * 0.5f;
}

}  // namespace

Gui2FormationGraphic::Gui2FormationGraphic(Gui2WindowManager* windowManager,
                                           const std::string& name, Match* match)
    : Gui2View(windowManager, name, ComputeCenteredX(windowManager), 10,
              windowManager->GetWidthPercentForHeight(74, kPanelAspect), 74),
      match(match) {
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
  headerHeight = windowManager->GetHeightPercentForWidth(width_percent, kHeaderAspect);

  const float tagHeight = std::min(headerHeight * 0.62f, 3.6f);
  teamTagCaption = new Gui2Caption(windowManager, "formationgraphic_teamtag", 0,
                                   (headerHeight - tagHeight) * 0.5f, width_percent, tagHeight, "");
  teamTagCaption->SetColor(Vector3(255, 255, 255));
  teamTagCaption->SetOutlineColor(Vector3(6, 10, 24));
  this->AddView(teamTagCaption);
  teamTagCaption->Show();

  bodyY = headerHeight + 1.2f;
  bodyHeight = height_percent - bodyY - 2.0f;
  subsColumnWidth = width_percent * 0.30f;
  bodyX = subsColumnWidth;
  bodyWidth = width_percent - subsColumnWidth;

  subsHeaderCaption = new Gui2Caption(windowManager, "formationgraphic_subsheader", 1.2f, bodyY,
                                      subsColumnWidth - 2.0f, 2.6f, "Substitutes");
  subsHeaderCaption->SetColor(kSubsHeaderColor);
  subsHeaderCaption->SetOutlineColor(Vector3(6, 10, 24));
  this->AddView(subsHeaderCaption);
  subsHeaderCaption->Show();

  // Everything above stays Show()n for the widget's entire lifetime; only
  // alpha conveys visibility (see ApplyAlpha/Process). NOTE: in this
  // session's testing, the panelBg/headerBg images (and the per-starter
  // jersey icons/pitch-lines built in BuildForTeam) did not visually render
  // in the headless verification runs despite correct position, size,
  // visibility and alpha state - see docs/PRESENTATION_SPEC.md follow-up
  // notes / the session report for what was ruled out. The caption-based
  // content (team tag, substitutes list, banner text) renders correctly.
  ApplyAlpha(0.0f);

  this->Show();
}

void Gui2FormationGraphic::ClearDynamicViews() {
  if (pitchLines) {
    pitchLines->Exit();
    delete pitchLines;
    pitchLines = nullptr;
  }

  for (StarterWidgets& s : starters) {
    if (s.icon) {
      s.icon->Exit();
      delete s.icon;
    }
    if (s.number) {
      s.number->Exit();
      delete s.number;
    }
    if (s.nickname) {
      s.nickname->Exit();
      delete s.nickname;
    }
  }
  starters.clear();

  for (Gui2Caption* c : subLines) {
    c->Exit();
    delete c;
  }
  subLines.clear();
}

void Gui2FormationGraphic::BuildForTeam(int teamID) {
  ClearDynamicViews();

  TeamData* teamData = match->GetTeam(teamID)->GetTeamData();

  teamTagCaption->SetCaption(teamData->GetShortName());
  CenterCaption(teamTagCaption, 0, width_percent);

  // Pitch schematic: gather the XI's database positions up front so the
  // tactical-shape connecting lines (drawn behind the icons) can be computed
  // in one pass (FormationGraphicLayout::BuildConnections).
  std::vector<Vector3> dbPositions;
  std::vector<e_PlayerRole> roles;
  for (int i = 0; i < 11; i++) {
    const FormationEntry entry = teamData->GetFormationEntry(i);
    dbPositions.push_back(entry.databasePosition);
    roles.push_back(entry.role);
  }
  const std::vector<FormationGraphicLayout::Connection> connections =
      FormationGraphicLayout::BuildConnections(dbPositions);

  int x, y, w, h;
  windowManager->GetCoordinates(bodyX, bodyY, bodyWidth, bodyHeight, x, y, w, h);

  pitchLines = new Gui2Image(windowManager, "formationgraphic_pitchlines", bodyX, bodyY, bodyWidth,
                            bodyHeight);
  this->AddView(pitchLines);
  pitchLines->Show();

  auto toPixel = [&](const Vector3& dbPos) -> Vector3 {
    const FormationGraphicLayout::PanelPoint p = FormationGraphicLayout::MapPosition(dbPos);
    return Vector3(p.xPercent * 0.01f * w, p.yPercent * 0.01f * h, 0.0f);
  };

  Image2D* lines = pitchLines->GetImage2D().get();
  for (const auto& c : connections) {
    lines->DrawLine(Line(toPixel(dbPositions[c.fromIndex]), toPixel(dbPositions[c.toIndex])),
                    kLineColor, kLineAlpha);
  }

  // Goal box under the goalkeeper, forward arc over whoever sits highest up
  // the panel (the lone striker in a single-CF shape; the more advanced of
  // two in a twin-striker shape) - per spec section 1.1.
  int gkIndex = -1, forwardIndex = -1;
  float bestForwardY = 1e9f;
  for (int i = 0; i < 11; i++) {
    if (roles[i] == e_PlayerRole_GK) gkIndex = i;
    const float yp = FormationGraphicLayout::MapPosition(dbPositions[i]).yPercent;
    if (yp < bestForwardY) {
      bestForwardY = yp;
      forwardIndex = i;
    }
  }
  if (gkIndex != -1) {
    const Vector3 gk = toPixel(dbPositions[gkIndex]);
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
    lines->DrawLine(
        Line(Vector3(gk.coords[0] - boxHalfW, boxTop, 0), Vector3(gk.coords[0] + boxHalfW, boxTop, 0)),
        kLineColor, kLineAlpha + 40);
  }
  if (forwardIndex != -1) {
    const Vector3 fwd = toPixel(dbPositions[forwardIndex]);
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

  // Jersey icons + squad numbers + nicknames, one per formation slot.
  const float iconWidth = 5.0f;
  const float iconHeight = windowManager->GetHeightPercentForWidth(iconWidth, 1.0f);
  for (int i = 0; i < 11; i++) {
    const FormationGraphicLayout::PanelPoint mapped = FormationGraphicLayout::MapPosition(dbPositions[i]);
    const float px = bodyX + mapped.xPercent * 0.01f * bodyWidth;
    const float py = bodyY + mapped.yPercent * 0.01f * bodyHeight;

    StarterWidgets sw;
    sw.icon = new Gui2Image(windowManager, "formationgraphic_icon" + int_to_str(i),
                            px - iconWidth * 0.5f, py - iconHeight * 0.65f, iconWidth, iconHeight);
    this->AddView(sw.icon);
    sw.icon->LoadImage("media/ui/pes/jersey_icon.png");
    sw.icon->Show();

    sw.number = new Gui2BitmapText(
        windowManager, "formationgraphic_number" + int_to_str(i), px - iconWidth * 0.32f,
        py - iconHeight * 0.65f + iconHeight * 0.22f, iconWidth * 0.64f, iconHeight * 0.5f,
        "media/ui/pes/num_mid.fnt");
    sw.number->SetText(int_to_str(FormationGraphicLayout::SquadNumberForSlot(i)));
    this->AddView(sw.number);
    sw.number->Show();

    sw.nickname = new Gui2Caption(windowManager, "formationgraphic_nick" + int_to_str(i),
                                 px - iconWidth * 1.4f, py + iconHeight * 0.42f, iconWidth * 2.8f,
                                 1.8f, teamData->GetPlayerData(i)->GetLastName());
    sw.nickname->SetColor(Vector3(255, 255, 255));
    sw.nickname->SetOutlineColor(Vector3(6, 10, 24));
    this->AddView(sw.nickname);
    sw.nickname->Show();
    CenterCaption(sw.nickname, px - iconWidth * 1.4f, iconWidth * 2.8f);

    starters.push_back(sw);
  }

  // Substitutes: a plain numbered list, formation-order 12.. continuing from
  // the XI (see FormationGraphicLayout::SquadNumberForSlot).
  const int playerCount = teamData->GetPlayerNum();
  const float lineHeight = 2.3f;
  float lineY = bodyY + 3.6f;
  for (int i = 11; i < playerCount && lineY + lineHeight < bodyY + bodyHeight; i++) {
    const std::string text = int_to_str(FormationGraphicLayout::SquadNumberForSlot(i)) + "  " +
                             teamData->GetPlayerData(i)->GetLastName();
    Gui2Caption* line = new Gui2Caption(windowManager, "formationgraphic_sub" + int_to_str(i), 1.2f,
                                       lineY, subsColumnWidth - 2.0f, lineHeight, text);
    line->SetColor(Vector3(235, 235, 235));
    line->SetOutlineColor(Vector3(6, 10, 24));
    this->AddView(line);
    line->Show();
    subLines.push_back(line);
    lineY += lineHeight;
  }
}

void Gui2FormationGraphic::BuildBackgrounds() {
  if (panelBg) return;

  panelBg = new Gui2Image(windowManager, "formationgraphic_panel", 0, 0, width_percent,
                          height_percent);
  this->AddView(panelBg);
  panelBg->LoadImage("media/ui/pes/formation_panel.png");
  panelBg->Show();
  panelBg->SetZPriority(0);        // behind the content it backs

  headerBg = new Gui2Image(windowManager, "formationgraphic_header", 0, 0, width_percent,
                           headerHeight);
  this->AddView(headerBg);
  headerBg->LoadImage("media/ui/pes/formation_header.png");
  headerBg->Show();
  headerBg->SetZPriority(1);

  currentAlpha = -1.0f;            // the new images need their alpha applied
}

void Gui2FormationGraphic::ApplyAlpha(float alpha) {
  if (alpha == currentAlpha) return;
  currentAlpha = alpha;

  if (panelBg) panelBg->GetImage2D()->SetAlpha(alpha);
  if (headerBg) headerBg->GetImage2D()->SetAlpha(alpha);
  teamTagCaption->SetTransparency(1.0f - alpha);
  subsHeaderCaption->SetTransparency(1.0f - alpha);

  for (StarterWidgets& s : starters) {
    if (s.icon) s.icon->GetImage2D()->SetAlpha(alpha);
    if (s.number) s.number->SetAlpha(alpha);
    if (s.nickname) s.nickname->SetTransparency(1.0f - alpha);
  }
  for (Gui2Caption* c : subLines) c->SetTransparency(1.0f - alpha);

  if (pitchLines) pitchLines->GetImage2D()->SetAlpha(alpha);
}

void Gui2FormationGraphic::Process() {
  Gui2View::Process();
  if (!enabled) return;

  // Debug: hold the graphic on screen regardless of the entrance schedule, so
  // its artwork can be judged without racing a pre-match window that is over
  // in seconds. "debug_formation_graphic_always" "true".
  static const bool alwaysShow =
      GetConfiguration()->GetBool("debug_formation_graphic_always", false);
  BuildBackgrounds();

  if (alwaysShow) {
    if (builtForTeamID != 0) {
      BuildForTeam(0);
      builtForTeamID = 0;
      currentAlpha = -1.0f;
    }
    ApplyAlpha(1.0f);
    return;
  }

  if (!match->IsInEntrance()) {
    if (entranceStart_ms != 0 || builtForTeamID != -2) {
      entranceStart_ms = 0;
      entranceDuration_ms = 0;
      builtForTeamID = -2;
      ClearDynamicViews();
      ApplyAlpha(0.0f);
    }
    return;
  }

  if (entranceStart_ms == 0) {
    entranceStart_ms = match->GetActualTime_ms();
    entranceDuration_ms = match->GetEntranceEndTime_ms() > entranceStart_ms
                             ? match->GetEntranceEndTime_ms() - entranceStart_ms
                             : 0;
  }

  const unsigned long elapsed = match->GetActualTime_ms() - entranceStart_ms;
  const FormationGraphicLayout::DisplayState state =
      FormationGraphicLayout::ComputeDisplayState(elapsed, entranceDuration_ms);

  if (state.teamID != builtForTeamID) {
    if (state.teamID == -1) {
      ClearDynamicViews();
    } else {
      BuildForTeam(state.teamID);
    }
    currentAlpha = -1.0f;  // force a fresh ApplyAlpha (team/content just changed)
    builtForTeamID = state.teamID;
  }

  ApplyAlpha(state.teamID == -1 ? 0.0f : state.alpha);
}

}  // namespace blunted
