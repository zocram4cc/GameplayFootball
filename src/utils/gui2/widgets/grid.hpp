// written by bastiaan konings schuiling 2008 - 2014
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#ifndef _HPP_GUI2_VIEW_GRID
#define _HPP_GUI2_VIEW_GRID

#include "../view.hpp"
#include "scene/objects/image2d.hpp"
#include "scrollbar.hpp"

namespace blunted {

struct GridContainer {
  int row;
  int col;
  Gui2View* view;
};

class Gui2Grid : public Gui2View {
public:
  Gui2Grid(Gui2WindowManager* windowManager, const std::string& name, float x_percent,
           float y_percent, float width_percent, float height_percent);
  virtual ~Gui2Grid();

  virtual void Process();

  virtual void AddView(Gui2View* view, int row = -1, int col = 0);
  virtual void RemoveView(Gui2View* view);
  virtual void RemoveView(int row, int col);
  Gui2View* FindView(int row, int col);

  virtual int GetRow(Gui2View* view);
  virtual int GetColumn(Gui2View* view);

  Gui2View* GetSelectedView() { return FindView(selectedRow, selectedCol); }

  virtual void SetQuickScroll(bool onOff) { quickScroll = onOff; }
  virtual void SetWrapping(bool rowOnOff = true, bool colOnOff = true) {
    rowWrap = rowOnOff;
    colWrap = colOnOff;
  }

  // Whether running off the top or bottom of a column steps into the next
  // one, at its far end - the reading order a list of columns actually has.
  //
  // A grid whose cells are sliders cannot be navigated across otherwise:
  // Gui2Slider takes the same directional event the grid navigates with and
  // spends every horizontal press on its own value, so left and right never
  // change column. The match options sheet - two columns, nine sliders on the
  // left and START on the right - was therefore a trap: reach the sliders and
  // there is no way back to the button that starts the match (owner, 06-09).
  virtual void SetColumnWrapping(bool onOff = true) { columnWrap = onOff; }

  virtual void SetMaxVisibleRows(int visibleRowCount);
  virtual void UpdateLayout(float margin_left_percent = 0.5f, float margin_right_percent = 0.5f,
                            float margin_top_percent = 0.5f, float margin_bottom_percent = 0.5f);
  void UpdateScrolling();
  void UpdateScrollbars();

  virtual void ProcessWindowingEvent(WindowingEvent* event);

  virtual void OnGainFocus();
  virtual void SetInFocusPath(bool onOff);

  virtual bool IsSelectable() { return hasSelectables; }

  virtual void Show();
  virtual void Hide();

protected:
  boost::intrusive_ptr<Image2D> image;

  std::vector<GridContainer> container;

  bool hasSelectables;

  int rows;
  int cols;

  int selectedRow;
  int selectedCol;

  int offsetRows;
  int maxVisibleRows;
  float margin_left_percent;
  float margin_right_percent;
  float margin_top_percent;
  float margin_bottom_percent;

  int switchDelay_ms;
  int minSwitchDelay_ms;

  bool quickScroll;  // left/right == pageup/pagedown (if 1 column)
  bool rowWrap;
  bool colWrap;
  bool columnWrap = false;

  Gui2Scrollbar* scrollX;
  Gui2Scrollbar* scrollY;
};

}  // namespace blunted

#endif
