#include "statsoverlay.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "../../data/matchanalytics.hpp"
#include "../../data/teamdata.hpp"
#include "../../onthepitch/match.hpp"
#include "../../onthepitch/team.hpp"
#include "utils/gui2/windowmanager.hpp"
#include "utils/localization.hpp"

using namespace blunted;

namespace {

// Card proportions. Landscape, centred, sized off the window's aspect ratio
// so the artwork's rounded corners are never stretched - the same approach
// the pre-match panel takes (see formationgraphiclayout.hpp).
constexpr float kCardPixelAspect = 1.32f;  // width / height
constexpr float kCardHeight = 74.0f;

constexpr float kHeaderFraction = 0.13f;
constexpr float kRowHeight = 4.2f;
constexpr float kRowTextFraction = 0.60f;
constexpr float kBarHeight = 0.8f;

const Vector3 kLabelColor(168, 186, 214);
const Vector3 kValueColor(255, 255, 255);
const Vector3 kTitleColor(120, 232, 224);
const Vector3 kOutlineColor(6, 10, 24);
const Vector3 kBarBackColor(30, 42, 74);

// Draw order: both background plates first, then everything else on top of
// them (Gui2View defaults to 0, which would put content under the header).
constexpr int kZPanel = 0;
constexpr int kZHeader = 1;
constexpr int kZContent = 2;

std::string Percent(int value) { return int_to_str(value) + "%"; }

// real_to_str() is a plain "%f", which turns an xG of 0.14 into "0.140000".
std::string TwoDecimals(float value) {
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%.2f", value);
  return std::string(buffer);
}

}  // namespace

Gui2StatsOverlay::Gui2StatsOverlay(Gui2WindowManager* windowManager, Match* match)
    : Gui2View(windowManager, "statsoverlay", 0, 0, 1, 1), match(match) {
  const float cardWidth = kCardHeight * kCardPixelAspect / windowManager->GetAspectRatio();
  SetPosition((100.0f - cardWidth) * 0.5f, (100.0f - kCardHeight) * 0.5f);
  SetSize(cardWidth, kCardHeight);

  const float headerHeight = kCardHeight * kHeaderFraction;
  const float sideMargin = cardWidth * 0.05f;

  panelBg = new Gui2Image(windowManager, "statsoverlay_panel", 0, 0, cardWidth, kCardHeight);
  this->AddView(panelBg);
  panelBg->LoadImage("media/ui/pes/formation_panel.png");
  panelBg->Show();

  headerBg = new Gui2Image(windowManager, "statsoverlay_header", 0, 0, cardWidth, headerHeight);
  this->AddView(headerBg);
  headerBg->LoadImage("media/ui/pes/formation_header.png");
  headerBg->Show();

  // Header: each team's crest at its own end, its tag just inside, title
  // centred between them.
  const float crestHeight = headerHeight * 0.66f;
  const float crestWidth = windowManager->GetWidthPercentForHeight(crestHeight, 1.0f);
  const float tagHeight = headerHeight * 0.38f;
  for (int i = 0; i < 2; i++) {
    const float crestX = i == 0 ? sideMargin : cardWidth - sideMargin - crestWidth;
    crest[i] = new Gui2Image(windowManager, "statsoverlay_crest" + int_to_str(i), crestX,
                             (headerHeight - crestHeight) * 0.5f, crestWidth, crestHeight);
    this->AddView(crest[i]);
    crest[i]->LoadImage(match->GetTeam(i)->GetTeamData()->GetLogoUrl());
    crest[i]->Show();

    teamTag[i] = new Gui2Caption(windowManager, "statsoverlay_tag" + int_to_str(i), 0,
                                 (headerHeight - tagHeight) * 0.5f, cardWidth * 0.2f, tagHeight,
                                 match->GetTeam(i)->GetTeamData()->GetShortName());
    teamTag[i]->SetColor(kValueColor);
    teamTag[i]->SetOutlineColor(kOutlineColor);
    this->AddView(teamTag[i]);
    const float tagCentre =
        i == 0 ? crestX + crestWidth + cardWidth * 0.055f : crestX - cardWidth * 0.055f;
    teamTag[i]->SetPosition(tagCentre - teamTag[i]->GetTextWidthPercent() * 0.5f,
                            (headerHeight - tagHeight) * 0.5f);
    teamTag[i]->Show();
  }

  title = new Gui2Caption(windowManager, "statsoverlay_title", 0, (headerHeight - tagHeight) * 0.5f,
                          cardWidth * 0.5f, tagHeight,
                          Localization::GetInstance().Translate("stats_title"));
  title->SetColor(kTitleColor);
  title->SetOutlineColor(kOutlineColor);
  this->AddView(title);
  title->SetPosition((cardWidth - title->GetTextWidthPercent()) * 0.5f,
                     (headerHeight - tagHeight) * 0.5f);
  title->Show();

  // Three columns: home value, centred label, away value. The label owns the
  // middle third of the card - wide enough for the longest label, tight
  // enough that the numbers stay next to what they measure - which leaves
  // each value column comfortable for the longest value any row produces.
  labelWidth = cardWidth * 0.34f;
  labelLeft = (cardWidth - labelWidth) * 0.5f;
  valueMargin = cardWidth * 0.02f;
  rowTextHeight = kRowHeight * kRowTextFraction;

  Localization& text = Localization::GetInstance();
  float y = headerHeight + kCardHeight * 0.05f;
  const char* labels[] = {"stats_possession", "stats_shots",     "stats_shots_on_target",
                          "stats_passes",     "stats_pass_accuracy", "stats_fouls",
                          "stats_expected_goals"};
  for (size_t i = 0; i < sizeof(labels) / sizeof(labels[0]); i++) {
    rows.push_back(AddRow(text.Translate(labels[i]), y, i == 0));
    y += kRowHeight;
  }
  y += kRowHeight * 0.3f;

  // Ball heatmap: an actual picture of the pitch rather than four rows of
  // block characters, which is what this used to draw.
  heatmapLabel = new Gui2Caption(windowManager, "statsoverlay_heatmaplabel", 0, y, cardWidth * 0.6f,
                                 rowTextHeight, text.Translate("stats_ball_heatmap"));
  heatmapLabel->SetColor(kLabelColor);
  heatmapLabel->SetOutlineColor(kOutlineColor);
  this->AddView(heatmapLabel);
  heatmapLabel->SetPosition((cardWidth - heatmapLabel->GetTextWidthPercent()) * 0.5f, y);
  heatmapLabel->Show();
  y += rowTextHeight * 1.7f;

  const float heatmapHeight = std::max(1.0f, kCardHeight - y - kCardHeight * 0.055f);
  const float heatmapWidth = windowManager->GetWidthPercentForHeight(heatmapHeight, 105.0f / 68.0f);
  heatmap = new Gui2Image(windowManager, "statsoverlay_heatmap", (cardWidth - heatmapWidth) * 0.5f,
                          y, heatmapWidth, heatmapHeight);
  this->AddView(heatmap);
  heatmap->Show();

  // Order the card now rather than waiting for the next frame's tree-wide
  // reset - see Gui2View::SetRecursiveZPriority.
  ApplyZOrder();
}

Gui2StatsOverlay::StatRow Gui2StatsOverlay::AddRow(const std::string& label, float y,
                                                   bool withBar) {
  StatRow row;
  float cardWidth, cardHeight;
  GetSize(cardWidth, cardHeight);

  row.label = new Gui2Caption(windowManager, "statsoverlay_label_" + label, labelLeft, y,
                              labelWidth, rowTextHeight, label);
  row.label->SetColor(kLabelColor);
  row.label->SetOutlineColor(kOutlineColor);
  this->AddView(row.label);
  row.label->SetPosition(labelLeft + (labelWidth - row.label->GetTextWidthPercent()) * 0.5f, y);
  row.label->Show();

  row.home = new Gui2Caption(windowManager, "statsoverlay_home_" + label, valueMargin, y,
                             labelLeft - valueMargin * 2.0f, rowTextHeight, "0");
  row.home->SetColor(kValueColor);
  row.home->SetOutlineColor(kOutlineColor);
  this->AddView(row.home);
  row.home->Show();

  row.away = new Gui2Caption(windowManager, "statsoverlay_away_" + label,
                             labelLeft + labelWidth + valueMargin, y,
                             labelLeft - valueMargin * 2.0f, rowTextHeight, "0");
  row.away->SetColor(kValueColor);
  row.away->SetOutlineColor(kOutlineColor);
  this->AddView(row.away);
  row.away->Show();

  if (withBar) {
    row.bar = new Gui2Image(windowManager, "statsoverlay_bar_" + label, valueMargin,
                            y + rowTextHeight * 1.15f, cardWidth - valueMargin * 2.0f, kBarHeight);
    this->AddView(row.bar);
    row.bar->Show();
  }

  return row;
}

void Gui2StatsOverlay::SetRowValues(StatRow& row, const std::string& home,
                                    const std::string& away) {
  row.home->SetCaption(home);
  row.away->SetCaption(away);

  float x, y;
  // Home value right-aligned against the label column, away value left-
  // aligned against it: both read inward towards the label, which is what
  // keeps a "12" and a "100%" on the same axis down the card.
  row.home->GetPosition(x, y);
  row.home->SetPosition(labelLeft - valueMargin - row.home->GetTextWidthPercent(), y);
  row.away->GetPosition(x, y);
  row.away->SetPosition(labelLeft + labelWidth + valueMargin, y);
}

void Gui2StatsOverlay::DrawPossessionBar(float homeFraction) {
  if (rows.empty() || rows[0].bar == nullptr) return;
  Gui2Image* bar = rows[0].bar;
  float barWidth, barHeight;
  bar->GetSize(barWidth, barHeight);
  int x, y, w, h;
  windowManager->GetCoordinates(0, 0, barWidth, barHeight, x, y, w, h);
  if (w <= 0 || h <= 0) return;

  const Vector3 homeColor = match->GetTeam(0)->GetTeamData()->GetColor1();
  const Vector3 awayColor = match->GetTeam(1)->GetTeamData()->GetColor1();
  const int split = (int)std::round(w * clamp(homeFraction, 0.0f, 1.0f));

  bar->GetImage2D()->DrawRectangle(0, 0, w, h, kBarBackColor, 255);
  if (split > 0) bar->GetImage2D()->DrawRectangle(0, 0, split, h, homeColor, 235);
  if (split < w) bar->GetImage2D()->DrawRectangle(split, 0, w - split, h, awayColor, 235);
  bar->GetImage2D()->OnChange();
}

void Gui2StatsOverlay::DrawHeatmap() {
  float mapWidth, mapHeight;
  heatmap->GetSize(mapWidth, mapHeight);
  int x, y, w, h;
  windowManager->GetCoordinates(0, 0, mapWidth, mapHeight, x, y, w, h);
  if (w <= 0 || h <= 0) return;

  Image2D* image = heatmap->GetImage2D().get();
  image->DrawRectangle(0, 0, w, h, Vector3(18, 26, 48), 225);

  const MatchAnalytics::Heatmap& data = match->GetBallHeatmap();
  const float cellW = w / (float)MatchAnalytics::Heatmap::cellsX;
  const float cellH = h / (float)MatchAnalytics::Heatmap::cellsY;
  for (int cy = 0; cy < MatchAnalytics::Heatmap::cellsY; cy++) {
    for (int cx = 0; cx < MatchAnalytics::Heatmap::cellsX; cx++) {
      const float intensity = MatchAnalytics::GetNormalizedIntensity(data, cx, cy);
      if (intensity <= 0.01f) continue;
      // Cool blue where the ball rarely went, warming towards white where it
      // lived - the same reading the block characters gave, only legible.
      const Vector3 color(60.0f + intensity * 195.0f, 110.0f + intensity * 130.0f,
                          220.0f - intensity * 90.0f);
      image->DrawRectangle((int)(cx * cellW), (int)(cy * cellH), (int)std::ceil(cellW),
                           (int)std::ceil(cellH), color, (int)(60 + intensity * 175));
    }
  }

  // A halfway line, so the map reads as a pitch rather than a bare grid.
  image->DrawLine(Line(Vector3(w * 0.5f, 0, 0), Vector3(w * 0.5f, h, 0)), Vector3(200, 220, 255),
                  90);
  image->OnChange();
}

void Gui2StatsOverlay::UpdateStats() {
  MatchData* md = match->GetMatchData();

  const float poss1 = md->GetPossessionTime_ms(0);
  const float poss2 = md->GetPossessionTime_ms(1);
  const float total = poss1 + poss2;
  const int pct1 = (total > 0) ? int(std::round(poss1 / total * 100)) : 50;
  SetRowValues(rows[0], Percent(pct1), Percent(100 - pct1));
  DrawPossessionBar(total > 0 ? poss1 / total : 0.5f);

  SetRowValues(rows[1], int_to_str(md->GetShots(0)), int_to_str(md->GetShots(1)));
  SetRowValues(rows[2], int_to_str(md->GetShotsOnTarget(0)), int_to_str(md->GetShotsOnTarget(1)));

  const int attempts0 = md->GetPassAttempts(0);
  const int attempts1 = md->GetPassAttempts(1);
  SetRowValues(rows[3],
               int_to_str(md->GetPassesCompleted(0)) + "/" + int_to_str(attempts0),
               int_to_str(md->GetPassesCompleted(1)) + "/" + int_to_str(attempts1));
  SetRowValues(
      rows[4],
      Percent(attempts0 > 0 ? int(std::round(md->GetPassesCompleted(0) * 100.0f / attempts0)) : 0),
      Percent(attempts1 > 0 ? int(std::round(md->GetPassesCompleted(1) * 100.0f / attempts1)) : 0));

  SetRowValues(rows[5], int_to_str(md->GetFouls(0)), int_to_str(md->GetFouls(1)));

  const MatchAnalytics::ShotTally& tally = match->GetShotTally();
  SetRowValues(rows[6], TwoDecimals(MatchAnalytics::GetExpectedGoals(tally, 0)),
               TwoDecimals(MatchAnalytics::GetExpectedGoals(tally, 1)));

  DrawHeatmap();
}

void Gui2StatsOverlay::ApplyZOrder() {
  const int base = GetZPriority();
  if (panelBg) panelBg->SetZPriority(base + kZPanel);
  if (headerBg) headerBg->SetZPriority(base + kZHeader);
  for (int i = 0; i < 2; i++) {
    if (crest[i]) crest[i]->SetZPriority(base + kZContent);
    if (teamTag[i]) teamTag[i]->SetZPriority(base + kZContent);
  }
  if (title) title->SetZPriority(base + kZContent);
  if (heatmapLabel) heatmapLabel->SetZPriority(base + kZContent);
  if (heatmap) heatmap->SetZPriority(base + kZContent);
  for (StatRow& row : rows) {
    if (row.label) row.label->SetZPriority(base + kZContent);
    if (row.home) row.home->SetZPriority(base + kZContent);
    if (row.away) row.away->SetZPriority(base + kZContent);
    if (row.bar) row.bar->SetZPriority(base + kZContent);
  }
}

void Gui2StatsOverlay::SetRecursiveZPriority(int prio) {
  Gui2View::SetRecursiveZPriority(prio);
  ApplyZOrder();
}
