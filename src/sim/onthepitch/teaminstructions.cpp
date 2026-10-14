#include "teaminstructions.hpp"

namespace TeamInstructions {

State Load(const blunted::Properties& properties) {
  State state;
  // -1 for absent, because 0 is a mentality (balanced is 2) and reading a missing
  // key as 0 would silently set one.
  const int mentality = static_cast<int>(properties.GetReal("mentality", -1.0f));
  if (mentality >= 0 && mentality < e_Mentality_Count) state.mentality = static_cast<e_Mentality>(mentality);
  return state;
}

}  // namespace TeamInstructions
