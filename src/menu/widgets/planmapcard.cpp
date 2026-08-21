#include "planmapcard.hpp"

#include <algorithm>
#include <cmath>

#include <sstream>

namespace PlanMapCard {

float Distance(const Colour& a, const Colour& b) {
  const float dr = a.r - b.r, dg = a.g - b.g, db = a.b - b.b;
  return std::sqrt(dr * dr + dg * dg + db * db);
}

e_Line LineOf(e_PlayerRole role) {
  switch (role) {
    case e_PlayerRole_GK:
      return e_Line_Keeper;
    case e_PlayerRole_CB:
    case e_PlayerRole_LB:
    case e_PlayerRole_RB:
      return e_Line_Defence;
    case e_PlayerRole_DM:
    case e_PlayerRole_CM:
    case e_PlayerRole_LM:
    case e_PlayerRole_RM:
    case e_PlayerRole_AM:
      return e_Line_Midfield;
    case e_PlayerRole_CF:
      return e_Line_Attack;
  }
  return e_Line_Midfield;
}

Colour LineColour(e_Line line) {
  // Sampled off the broadcast's own cards rather than chosen: the keeper's label is a
  // warm yellow, the full-backs and centre-backs a light cyan, the midfield a bright
  // green, the forwards a pink-red.
  switch (line) {
    case e_Line_Keeper:
      return Colour{240, 205, 70};
    case e_Line_Defence:
      return Colour{90, 200, 245};
    case e_Line_Midfield:
      return Colour{90, 225, 90};
    case e_Line_Attack:
      return Colour{240, 90, 130};
  }
  return Colour{230, 230, 230};
}

e_Aptitude AptitudeFor(e_PlayerRole slot, const std::vector<e_PlayerRole>& playerRoles) {
  // A roster entry with no positions recorded is missing data, and a card must not
  // accuse a player of being out of position on the strength of nothing.
  if (playerRoles.empty())
    return e_Aptitude_Natural;
  if (std::find(playerRoles.begin(), playerRoles.end(), slot) != playerRoles.end())
    return e_Aptitude_Natural;
  const e_Line wanted = LineOf(slot);
  for (unsigned int i = 0; i < playerRoles.size(); i++) {
    if (LineOf(playerRoles.at(i)) == wanted)
      return e_Aptitude_SameLine;
  }
  return e_Aptitude_OutOfPosition;
}

Colour AptitudeColour(e_Aptitude aptitude) {
  switch (aptitude) {
    case e_Aptitude_Natural:
      return Colour{250, 230, 120};  // his own position
    case e_Aptitude_SameLine:
      return Colour{230, 150, 70};  // near enough
    case e_Aptitude_OutOfPosition:
      return Colour{200, 200, 200};  // filling in
  }
  return Colour{230, 230, 230};
}

std::string RatingText(float stat) {
  if (stat <= 0.0f)
    return "";
  const int rating = static_cast<int>(std::floor(stat * 100.0f + 0.5f));
  if (rating <= 0)
    return "";
  // Three digits is what the strip holds; a stat far above 1.0 is a data fault, not
  // something to print over the next card.
  if (rating > 999)
    return "999";
  std::ostringstream text;
  text << rating;
  return text.str();
}

std::string NameText(const std::string& name, unsigned int budget) {
  if (budget == 0)
    return "";
  if (name.size() <= budget)
    return name;
  // A stop rather than three, because the budget is small enough that three dots cost
  // a readable letter each.
  return name.substr(0, budget - 1) + ".";
}

}  // namespace PlanMapCard
