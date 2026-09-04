// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#include "gameplansubmenu.hpp"

GamePlanSubMenu::GamePlanSubMenu(Gui2WindowManager* windowManager, Gui2View* parentFocus,
                                 Gui2Grid* mainGrid, const std::string& name)
    : Gui2View(windowManager, name, 0, 0, 100, 100), mainGrid(mainGrid), parentFocus(parentFocus) {
  // A sub-menu takes the cell the button column just vacated - beside the pitch, the
  // way the broadcast lays the game plan out. It used to take the cell below, which
  // ran the list off the bottom of the panel once the pitch grew.
  grid = new Gui2Grid(windowManager, "gameplan_grid_" + name, 0, 0, 0, 0);
  this->AddView(grid);
  mainGrid->AddView(this, kGamePlanNavRow, kGamePlanNavColumn);
  mainGrid->UpdateLayout(0.0);
  grid->SetQuickScroll(true);
  grid->Show();

  sig_OnClose.connect([this](...) { OnClose(); });
}

GamePlanSubMenu::~GamePlanSubMenu() {}

void GamePlanSubMenu::OnClose() {}

Gui2Button* GamePlanSubMenu::AddButton(const std::string& buttonName,
                                       const std::string& buttonCaption, int row, int column,
                                       Vector3 color = Vector3(-1)) {
  if (color.coords[0] < 0)
    color = windowManager->GetStyle()->GetColor(e_DecorationType_Bright1);
  Gui2Button* theButton =
      new Gui2Button(windowManager, "gameplan_button_submenu_" + name + "_" + buttonName, 0, 0, 34,
                     3, buttonCaption);
  theButton->SetColor(color);
  allButtons.push_back(theButton);
  grid->AddView(theButton, row, column);
  grid->SetMaxVisibleRows(11);
  grid->UpdateLayout(0.5);
  mainGrid->UpdateLayout(0.0);
  return theButton;
}

Gui2Slider* GamePlanSubMenu::AddSlider(const std::string& sliderName,
                                       const std::string& sliderCaption, int row, int column) {
  Gui2Slider* theSlider =
      new Gui2Slider(windowManager, "gameplan_slider_submenu_" + name + "_" + sliderName, 0, 0, 34,
                     6, sliderCaption);
  grid->AddView(theSlider, row, column);
  grid->SetMaxVisibleRows(6);
  grid->UpdateLayout(0.5);
  mainGrid->UpdateLayout(0.0);
  return theSlider;
}

Gui2Button* GamePlanSubMenu::GetToggledButton(Gui2Button* except) {
  for (int i = 0; i < (signed int)allButtons.size(); i++) {
    if (allButtons.at(i) != except)
      if (allButtons.at(i)->IsToggled())
        return allButtons.at(i);
  }
  return 0;
}

void GamePlanSubMenu::ProcessWindowingEvent(WindowingEvent* event) {
  if (!event->IsEscape()) {
    event->Ignore();
    return;
  }
  // Escape twice before the first close has been processed used to run all of
  // this twice: the second RemoveView took whatever occupied the cell by then
  // (another submenu, or the button column) and detached that instead, and the
  // second `delete this` freed a view the grid was still holding.
  if (closing) return;
  closing = true;

  // Detach this exact view rather than "whatever is in the nav cell": by the
  // time an escape arrives the cell may hold a different submenu.
  if (GetParent() == mainGrid) mainGrid->RemoveView(this);
  mainGrid->UpdateLayout(0.0);

  // sig_OnClose is what puts the button column back and restores focus
  // (GamePlanPage::Reactivate), so it has to run before the page is asked to
  // focus anything.
  this->Exit();
  if (parentFocus) parentFocus->SetFocus();

  // Deferred: this call is inside our own event dispatch, and the dispatcher
  // reads members of this view after the handler returns (view.cpp:199, the
  // parent walk for an unaccepted event). The window manager deletes it at
  // the top of the next frame instead.
  windowManager->MarkForDeletion(this);
}
