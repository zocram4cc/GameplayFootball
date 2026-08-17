// Selectable team formations.
//
// The engine reads any shape it is given through formation_xml (see
// TeamData); this is the set of shapes the game offers in its menus, hands to
// CPU managers, and switches to when a team changes its approach mid-match.

#ifndef _HPP_FORMATIONS
#define _HPP_FORMATIONS

#include <string>
#include <vector>

#include "../gametypes.hpp"
#include "base/math/vector3.hpp"

namespace Formations {

enum e_Formation {
  e_Formation_442 = 0,
  e_Formation_433,
  e_Formation_451,
  e_Formation_352,
  e_Formation_343,
  e_Formation_424,
  e_Formation_532,
  e_Formation_Count,
};

// Ten outfield players, plus the keeper.
const int outfieldPlayers = 10;

// Outfield make-up; always ten players.
struct Shape {
  int defenders = 4;
  int midfielders = 4;
  int forwards = 2;
};

// One player's place in the formation, in the normalized [-1, 1] pitch space
// the database uses: x is -1 at the team's own goal, y is across the pitch.
struct Slot {
  e_PlayerRole role = e_PlayerRole_CM;
  blunted::Vector3 position;
};

// Any band split totalling ten outfield players is a legal formation, from a
// 6-4-0 wall to a 1-0-9 stampede; the presets above are just the offered ones.
Shape MakeShape(int defenders, int midfielders, int forwards);
// Same, but forced to add up: the remainder is taken off the other bands.
Shape MakeShapeClamped(int defenders, int midfielders, int forwards);
bool IsValidShape(const Shape& shape);

std::string ShapeName(const Shape& shape);
// Accepts any "d-m-f" that adds up; anything else gives 4-4-2.
Shape ParseShape(const std::string& name);

// The shape a lineup is actually in, counted off its roles. The keeper is
// ignored wherever he appears, and defensive and attacking midfielders count as
// midfield.
//
// This exists because the shape used to be read from a "formation" string in the
// tactics properties, and tactics_xml is parsed with atof - so that property can
// only ever hold a number and every team looked like a 4-4-2 whatever it was
// really lined up as. The lineup is the truth.
Shape ShapeFromRoles(const std::vector<e_PlayerRole>& roles);

std::vector<Slot> GetLayoutForShape(const Shape& shape);
std::string BuildFormationXmlForShape(const Shape& shape);

int GetCount();
e_Formation GetFormationAt(int index);

// "4-4-2" and so on; unknown names fall back to 4-4-2.
std::string GetName(e_Formation formation);
e_Formation Parse(const std::string& name);

Shape GetShape(e_Formation formation);
std::vector<Slot> GetLayout(e_Formation formation);

// The formation_xml TeamData parses, for seed data and for saving a choice.
std::string BuildFormationXml(e_Formation formation);

// Closest offered formation to a desired outfield shape.
e_Formation FromShape(const Shape& shape);

}  // namespace Formations

#endif
