#include "scenegrade.hpp"

#include <algorithm>
#include <cmath>

namespace SceneGrade {

const char* const kStripPath = "media/textures/lut/grade.png";

int BandForConditions(float timeOfDay, float weather) {
  const float hour = std::clamp(timeOfDay, 0.0f, 1.0f);
  const float wet = std::clamp(weather, 0.0f, 1.0f);

  if (hour >= 0.75f) return e_Band_Night;
  if (hour >= 0.25f) return e_Band_Evening;
  return wet >= 0.5f ? e_Band_Cloudy : e_Band_Day;
}

bool StripDimensions(int width, int height, int& size, int& bands) {
  if (width < 4 || height < 2) return false;
  const int candidate = (int)(std::sqrt((double)width) + 0.5);
  if (candidate < 2 || candidate * candidate != width) return false;
  if (height % candidate != 0) return false;
  size = candidate;
  bands = height / candidate;
  return true;
}

}  // namespace SceneGrade
