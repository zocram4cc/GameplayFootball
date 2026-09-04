// written by bastiaan konings schuiling 2008 - 2014
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#include "view.hpp"

#include "windowmanager.hpp"

namespace blunted {

Gui2View::Gui2View(Gui2WindowManager* windowManager, const std::string& name, float x_percent,
                   float y_percent, float width_percent, float height_percent)
    : windowManager(windowManager),
      name(name),
      x_percent(x_percent),
      y_percent(y_percent),
      width_percent(width_percent),
      height_percent(height_percent) {
  parent = 0;
  zPriority = 0;
  isVisible = false;
  isSelectable = false;
  isInFocusPath = false;
  isOverlay = false;
}

Gui2View::~Gui2View() {}

void Gui2View::Exit() {
  // printf("exiting %s.. ", name.c_str());

  // Once. Exit is called from the owner and again by the window manager when a
  // view is handed to MarkForDeletion, and a second pass fires sig_OnClose a
  // second time - so every close handler ran twice, on state the first pass
  // had already destroyed (GamePlanPage::OnClose read its deleted button
  // column: the monkey's fourth crash, seed 1 tap 358).
  if (exited) return;
  exited = true;

  this->sig_OnClose();

  // Anywhere in this subtree, not only this view itself. A focussed child
  // whose ancestor is being deleted leaves the manager holding a pointer whose
  // parent chain is freed, and the next SetFocus walks that chain
  // (Gui2View::SetInFocusPath) - a use-after-free the game-plan monkey found
  // in a second. Children clear it on their own way out too; this closes the
  // case where the handler that is deleting us re-focuses something first.
  windowManager->ForgetFocusIn(this);

  this->Hide();

  // Taken from the live vector one at a time, last first. A copy was made here
  // instead, on the reasoning that Exit removes the child from `children` and
  // would invalidate the loop - but a child's Exit fires sig_OnClose, and a
  // handler is free to delete *other* children (the game plan's submenu close
  // puts the button column back and takes it away again). The copy then held
  // pointers that had already been deleted, and Exit was called on freed
  // memory: the second crash the monkey found (seed 1, tap 69).
  //
  // A child's own Exit ends with parent->RemoveView(this), so the vector
  // shrinks as we go; the pop is only a guard for a child that does not.
  while (!children.empty()) {
    // Detached before it is exited, and taken from the live vector each time.
    // Both matter: a child's Exit fires sig_OnClose, and the handlers here add
    // and remove views in the tree that is being destroyed (the game plan's
    // submenu close puts the button column back into the very grid being torn
    // down). Popping first means no entry can be visited twice and no handler
    // can leave a freed pointer behind for this loop to read - the monkey's
    // second and third crashes, both at this line.
    Gui2View* child = children.back();
    children.pop_back();
    child->SetParent(0);
    child->Exit();
    delete child;
  }

  if (parent)
    parent->RemoveView(this);

  std::vector<boost::intrusive_ptr<Image2D>> images;
  GetImages(images);
  for (unsigned int i = 0; i < images.size(); i++) {
    boost::intrusive_ptr<Image2D>& image = images.at(i);
    if (image != boost::intrusive_ptr<Image2D>()) {
      windowManager->RemoveImage(image);
    }
  }

  // printf("exited %s\n", name.c_str()); // excited! XD
}

void Gui2View::UpdateImagePosition() {
  windowManager->UpdateImagePosition(this);
  auto iter = children.begin();
  while (iter != children.end()) {
    (*iter)->UpdateImagePosition();
    iter++;
  }
}

void Gui2View::UpdateImageVisibility() {
  if (IsVisible())
    windowManager->Show(this);
  else
    windowManager->Hide(this);
  auto iter = children.begin();
  while (iter != children.end()) {
    (*iter)->UpdateImageVisibility();
    iter++;
  }
}

void Gui2View::AddView(Gui2View* view) {
  if (!view) return;
  // A view has one parent. Adding one that already has a place put it in two
  // children lists (or twice in the same one), and teardown then deleted it
  // twice - the game plan's button column, which is taken out of its grid
  // whenever a submenu opens and put back when one closes, so two closes in a
  // row added it twice. Moving it is what every caller means.
  if (view->GetParent() == this) return;
  if (view->GetParent()) view->GetParent()->RemoveView(view);
  children.push_back(view);
  view->SetParent(this);
  view->UpdateImagePosition();
  view->UpdateImageVisibility();
}

void Gui2View::RemoveView(Gui2View* view) {
  auto iter = children.begin();
  while (iter != children.end()) {
    if (*iter == view) {
      view->Hide();
      view->SetParent(0);
      iter = children.erase(iter);
      return;
    } else {
      iter++;
    }
  }
  Log(e_FatalError, "Gui2View", "RemoveView", "Should not be here!");
}

void Gui2View::SetParent(Gui2View* view) {
  this->parent = view;
}

Gui2View* Gui2View::GetParent() {
  return parent;
}

void Gui2View::SetPosition(float x_percent, float y_percent) {
  this->x_percent = x_percent;
  this->y_percent = y_percent;
  UpdateImagePosition();
}

void Gui2View::GetSize(float& width_percent, float& height_percent) const {
  width_percent = this->width_percent;
  height_percent = this->height_percent;
}

void Gui2View::GetPosition(float& x_percent, float& y_percent) const {
  x_percent = this->x_percent;
  y_percent = this->y_percent;
}

void Gui2View::GetDerivedPosition(float& x_percent, float& y_percent) const {
  float tmp_x_percent = this->x_percent;
  float tmp_y_percent = this->y_percent;
  if (parent) {
    float tmp_parent_x_percent;
    float tmp_parent_y_percent;
    parent->GetDerivedPosition(tmp_parent_x_percent, tmp_parent_y_percent);
    tmp_x_percent += tmp_parent_x_percent;
    tmp_y_percent += tmp_parent_y_percent;
  }
  x_percent = tmp_x_percent;
  y_percent = tmp_y_percent;
}

void Gui2View::SnuglyFitSize(float margin) {
  if (IsSelectable())
    return;

  float maxW = 0;
  float maxH = 0;

  float x, y, w, h;

  for (unsigned int i = 0; i < children.size(); i++) {
    children.at(i)->SnuglyFitSize(margin);

    children.at(i)->GetPosition(x, y);
    children.at(i)->GetSize(w, h);
    if (x + w > maxW)
      maxW = x + w;
    if (y + h > maxH)
      maxH = y + h;
  }

  SetSize(maxW + margin * 2.0f, maxH + margin * 2.0f);
}

void Gui2View::CenterPosition() {}

void Gui2View::GetImages(std::vector<boost::intrusive_ptr<Image2D>>& target) {}

void Gui2View::Process() {
  for (unsigned int i = 0; i < children.size(); i++) {
    // printf("gui2view %s :: processing child %s\n", name.c_str(),
    // children.at(i)->GetName().c_str());
    children.at(i)->Process();
  }
}

bool Gui2View::ProcessEvent(Gui2Event* event) {
  event->Accept();

  switch (event->GetType()) {
    case e_Gui2EventType_Windowing:
      ProcessWindowingEvent(static_cast<WindowingEvent*>(event));
      break;

    case e_Gui2EventType_Keyboard:
      ProcessKeyboardEvent(static_cast<KeyboardEvent*>(event));
      break;

    case e_Gui2EventType_Joystick:
      ProcessJoystickEvent(static_cast<JoystickEvent*>(event));
      break;

    default:
      break;
  }

  if (!event->IsAccepted() && parent)
    parent->ProcessEvent(event);

  return true;
}

void Gui2View::ProcessWindowingEvent(WindowingEvent* event) {
  event->Ignore();
}

void Gui2View::ProcessKeyboardEvent(KeyboardEvent* event) {
  event->Ignore();
}

void Gui2View::ProcessJoystickEvent(JoystickEvent* event) {
  event->Ignore();
}

bool Gui2View::IsFocussed() {
  return windowManager->IsFocussed(this);
}

void Gui2View::SetFocus() {
  windowManager->SetFocus(this);
}

void Gui2View::Show() {
  if (!isVisible) {
    isVisible = true;
    UpdateImageVisibility();
  }
}

void Gui2View::Hide() {
  if (isVisible) {
    isVisible = false;
    UpdateImageVisibility();
  }
}

void Gui2View::ShowAllChildren() {
  auto iter = children.begin();
  while (iter != children.end()) {
    (*iter)->Show();
    iter++;
  }
}

void Gui2View::HideAllChildren() {
  auto iter = children.begin();
  while (iter != children.end()) {
    (*iter)->Hide();
    iter++;
  }
}

void Gui2View::SetRecursiveZPriority(int prio) {
  float adaptedPrio = prio;
  if (isOverlay)
    adaptedPrio++;
  SetZPriority(adaptedPrio);

  auto iter = children.begin();
  while (iter != children.end()) {
    (*iter)->SetRecursiveZPriority(adaptedPrio);
    iter++;
  }
}

void Gui2View::SetZPriority(int prio) {
  zPriority = prio;

  std::vector<boost::intrusive_ptr<Image2D>> images;
  GetImages(images);
  for (unsigned int i = 0; i < images.size(); i++) {
    images.at(i)->SetPokePriority(prio);
  }
}

void Gui2View::PrintTree(int depth) {
  for (int i = 0; i < depth; i++)
    printf("  ");
  printf("%s (prio %i)\n", GetName().c_str(), GetZPriority());

  auto iter = children.begin();
  while (iter != children.end()) {
    (*iter)->PrintTree(depth + 1);
    iter++;
  }
}

}  // namespace blunted
