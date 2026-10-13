#pragma once

namespace pes {

struct Vec2 {
  float x = 0, y = 0;
  bool operator==(const Vec2&) const = default;
};
struct Vec3 {
  float x = 0, y = 0, z = 0;
  Vec3 operator*(float s) const { return Vec3(x * s, y * s, z * s); }
  Vec3 operator+(const Vec3& o) const { return Vec3(x + o.x, y + o.y, z + o.z); }
};
struct Vec4 { float x = 0, y = 0, z = 0, w = 0; };
struct Quat { float x = 0, y = 0, z = 0, w = 1; };

}  // namespace pes
