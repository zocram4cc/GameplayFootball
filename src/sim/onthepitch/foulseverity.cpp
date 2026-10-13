#include "foulseverity.hpp"

#include <cmath>

namespace FoulSeverity {

bool DeniesObviousChance(float attackDirSign, float fouledX, float fouledY,
                         float defensiveLineX, float penaltyAreaHalfWidth) {
  if (attackDirSign == 0.0f)
    return false;
  // Ahead of the last line of defence bar the keeper...
  if (fouledX * attackDirSign <= defensiveLineX * attackDirSign)
    return false;
  // ...and heading for goal rather than the corner flag.
  return std::fabs(fouledY) < penaltyAreaHalfWidth;
}

}  // namespace FoulSeverity
