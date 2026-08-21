// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#ifndef _HPP_GUI2_VIEW_PLANMAP
#define _HPP_GUI2_VIEW_PLANMAP

#include "gametypes.hpp"
#include "scene/objects/image2d.hpp"
#include "utils/gui2/view.hpp"
#include "utils/gui2/widgets/caption.hpp"
#include "utils/gui2/widgets/image.hpp"

class TeamData;
class PlayerData;
class Match;

namespace blunted {

class Gui2PlanMap : public Gui2View {
public:
  Gui2PlanMap(Gui2WindowManager* windowManager, const std::string& name, float x_percent,
              float y_percent, float width_percent, float height_percent, TeamData* teamData);
  virtual ~Gui2PlanMap();

  virtual void Process();

protected:
  boost::intrusive_ptr<Image2D> image;

  int w, h;

  SDL_Surface* bg;

  TeamData* teamData;
};

class Gui2PlanMapEntry : public Gui2View {
public:
  // One starter's card: portrait, position and rating strip, name. portraitHeight is
  // in percent of the page and 0 when the player has no imported portrait.
  Gui2PlanMapEntry(Gui2WindowManager* windowManager, const std::string& name, float x_percent,
                   float y_percent, float width_percent, float height_percent, e_PlayerRole role,
                   PlayerData* playerData, float portraitHeight);
  virtual ~Gui2PlanMapEntry();

protected:
  Gui2Image* portraitImage = nullptr;
  Gui2Caption* roleNameCaption = nullptr;
  Gui2Caption* ratingCaption = nullptr;
  Gui2Caption* playerNameCaption = nullptr;
};

}  // namespace blunted

#endif
