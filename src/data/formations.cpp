#include "formations.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

using blunted::Vector3;

namespace Formations {

namespace {

const e_Formation allFormations[e_Formation_Count] = {
    e_Formation_442, e_Formation_433, e_Formation_451, e_Formation_352,
    e_Formation_343, e_Formation_424, e_Formation_532,
};

// Depths of the bands, in the normalized pitch space the database uses.
const float keeperX = -1.0f;
const float defenceX = -0.7f;
const float deepMidfieldX = -0.35f;
const float midfieldX = 0.0f;
const float attackingMidfieldX = 0.25f;
const float attackX = 0.7f;

Slot MakeSlot(e_PlayerRole role, float x, float y) {
  Slot slot;
  slot.role = role;
  slot.position = Vector3(x, y, 0.0f);
  return slot;
}

// Spreads `count` players evenly across the width of the pitch, using `span` of
// it, and returns the y for the given index.
float SpreadY(int index, int count, float span) {
  if (count <= 1)
    return 0.0f;
  const float step = (span * 2.0f) / static_cast<float>(count - 1);
  return span - step * static_cast<float>(index);
}

// A back line: full backs on the flanks, centre backs inside.
void AddDefence(std::vector<Slot>& layout, int count) {
  for (int i = 0; i < count; i++) {
    // Crowded bands are staggered slightly in depth so nobody overlaps.
    const float y = SpreadY(i, count, 0.9f);
    e_PlayerRole role = e_PlayerRole_CB;
    if (count >= 4 && i == 0)
      role = e_PlayerRole_LB;
    else if (count >= 4 && i == count - 1)
      role = e_PlayerRole_RB;
    layout.push_back(MakeSlot(role, defenceX + (i % 2) * 0.06f, y));
  }
}

// A midfield band: wide men on the flanks, a holder or a playmaker inside
// depending on how many there are.
void AddMidfield(std::vector<Slot>& layout, int count) {
  for (int i = 0; i < count; i++) {
    const float y = SpreadY(i, count, 0.9f);
    e_PlayerRole role = e_PlayerRole_CM;
    float x = midfieldX + (i % 2) * 0.06f;

    const bool wide = count >= 3 && (i == 0 || i == count - 1);
    if (wide) {
      role = i == 0 ? e_PlayerRole_LM : e_PlayerRole_RM;
    } else if (count >= 5 && i == 1) {
      role = e_PlayerRole_DM;
      x = deepMidfieldX;
    } else if (count >= 4 && i == count - 2) {
      role = e_PlayerRole_AM;
      x = attackingMidfieldX;
    } else if (count == 2 && i == 0) {
      role = e_PlayerRole_DM;
      x = deepMidfieldX;
    }

    layout.push_back(MakeSlot(role, x, y));
  }
}

void AddAttack(std::vector<Slot>& layout, int count) {
  for (int i = 0; i < count; i++) {
    layout.push_back(
        MakeSlot(e_PlayerRole_CF, attackX + (i % 2) * 0.06f, SpreadY(i, count, 0.9f)));
  }
}

std::string PositionString(const Vector3& position) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%.2f,%.2f", position.coords[0], position.coords[1]);
  return std::string(buffer);
}

std::string RoleString(e_PlayerRole role) {
  switch (role) {
    case e_PlayerRole_GK:
      return "GK";
    case e_PlayerRole_CB:
      return "CB";
    case e_PlayerRole_LB:
      return "LB";
    case e_PlayerRole_RB:
      return "RB";
    case e_PlayerRole_DM:
      return "DM";
    case e_PlayerRole_LM:
      return "LM";
    case e_PlayerRole_RM:
      return "RM";
    case e_PlayerRole_AM:
      return "AM";
    case e_PlayerRole_CF:
      return "CF";
    default:
      return "CM";
  }
}

}  // namespace

Shape MakeShape(int defenders, int midfielders, int forwards) {
  Shape shape;
  shape.defenders = defenders;
  shape.midfielders = midfielders;
  shape.forwards = forwards;
  return shape;
}

bool IsValidShape(const Shape& shape) {
  if (shape.defenders < 0 || shape.midfielders < 0 || shape.forwards < 0)
    return false;
  return shape.defenders + shape.midfielders + shape.forwards == outfieldPlayers;
}

Shape MakeShapeClamped(int defenders, int midfielders, int forwards) {
  // Honour the bands in order, then put whatever is left up front.
  const int wantedDefenders = std::max(0, std::min(defenders, outfieldPlayers));
  const int wantedMidfielders =
      std::max(0, std::min(midfielders, outfieldPlayers - wantedDefenders));
  const int remaining = outfieldPlayers - wantedDefenders - wantedMidfielders;
  (void)forwards;
  return MakeShape(wantedDefenders, wantedMidfielders, remaining);
}

std::string ShapeName(const Shape& shape) {
  return std::to_string(shape.defenders) + "-" + std::to_string(shape.midfielders) + "-" +
         std::to_string(shape.forwards);
}

Shape ParseShape(const std::string& name) {
  int bands[3] = {0, 0, 0};
  int band = 0;
  bool sawDigit = false;

  for (char character : name) {
    if (character >= '0' && character <= '9') {
      bands[band] = bands[band] * 10 + (character - '0');
      sawDigit = true;
    } else if (character == '-' && band < 2) {
      band++;
    } else {
      return MakeShape(4, 4, 2);
    }
  }

  const Shape shape = MakeShape(bands[0], bands[1], bands[2]);
  if (!sawDigit || band != 2 || !IsValidShape(shape))
    return MakeShape(4, 4, 2);
  return shape;
}

std::vector<Slot> GetLayoutForShape(const Shape& shape) {
  const Shape safe = IsValidShape(shape) ? shape : MakeShape(4, 4, 2);

  std::vector<Slot> layout;
  layout.reserve(11);
  layout.push_back(MakeSlot(e_PlayerRole_GK, keeperX, 0.0f));
  AddDefence(layout, safe.defenders);
  AddMidfield(layout, safe.midfielders);
  AddAttack(layout, safe.forwards);
  return layout;
}

std::string BuildFormationXmlForShape(const Shape& shape) {
  const std::vector<Slot> layout = GetLayoutForShape(shape);

  std::string xml;
  for (size_t i = 0; i < layout.size(); i++) {
    const std::string tag = "p" + std::to_string(i + 1);
    xml += "<" + tag + "><position>" + PositionString(layout[i].position) + "</position><role>" +
           RoleString(layout[i].role) + "</role></" + tag + ">";
  }
  return xml;
}

int GetCount() {
  return e_Formation_Count;
}

e_Formation GetFormationAt(int index) {
  return allFormations[std::max(0, std::min(index, e_Formation_Count - 1))];
}

Shape GetShape(e_Formation formation) {
  Shape shape;
  switch (formation) {
    case e_Formation_433:
      shape.defenders = 4;
      shape.midfielders = 3;
      shape.forwards = 3;
      break;
    case e_Formation_451:
      shape.defenders = 4;
      shape.midfielders = 5;
      shape.forwards = 1;
      break;
    case e_Formation_352:
      shape.defenders = 3;
      shape.midfielders = 5;
      shape.forwards = 2;
      break;
    case e_Formation_343:
      shape.defenders = 3;
      shape.midfielders = 4;
      shape.forwards = 3;
      break;
    case e_Formation_424:
      shape.defenders = 4;
      shape.midfielders = 2;
      shape.forwards = 4;
      break;
    case e_Formation_532:
      shape.defenders = 5;
      shape.midfielders = 3;
      shape.forwards = 2;
      break;
    default:
      shape.defenders = 4;
      shape.midfielders = 4;
      shape.forwards = 2;
      break;
  }
  return shape;
}

std::string GetName(e_Formation formation) {
  return ShapeName(GetShape(formation));
}

e_Formation Parse(const std::string& name) {
  for (int i = 0; i < e_Formation_Count; i++) {
    if (name == GetName(GetFormationAt(i)))
      return GetFormationAt(i);
  }
  return e_Formation_442;
}

std::vector<Slot> GetLayout(e_Formation formation) {
  return GetLayoutForShape(GetShape(formation));
}

std::string BuildFormationXml(e_Formation formation) {
  return BuildFormationXmlForShape(GetShape(formation));
}

e_Formation FromShape(const Shape& shape) {
  // Pick the offered shape that differs least from the one asked for, weighing
  // the attacking end most heavily: a side that wants four forwards cares more
  // about getting them than about the exact back line.
  e_Formation best = e_Formation_442;
  int bestCost = -1;
  for (int i = 0; i < e_Formation_Count; i++) {
    const e_Formation candidate = GetFormationAt(i);
    const Shape option = GetShape(candidate);
    const int cost = std::abs(option.forwards - shape.forwards) * 3 +
                     std::abs(option.midfielders - shape.midfielders) * 2 +
                     std::abs(option.defenders - shape.defenders);
    if (bestCost == -1 || cost < bestCost) {
      bestCost = cost;
      best = candidate;
    }
  }
  return best;
}

}  // namespace Formations
