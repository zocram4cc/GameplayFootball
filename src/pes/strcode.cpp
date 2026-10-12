// Port of GameplayFootball tools/pes21_import/strcode.py (CityHash 1.0.x, the
// version the Fox Engine uses; 1.1 differs for every input length).
#include "pes/strcode.hpp"

#include <cstring>
#include <string>
#include <utility>

namespace pes {
namespace {

// CityHash 1.0 constants.
constexpr uint64_t K0 = 0xC3A5C85C97CB3127ULL;
constexpr uint64_t K1 = 0xB492B66FBE98F273ULL;
constexpr uint64_t K2 = 0x9AE16A3B2F90404FULL;
constexpr uint64_t K3 = 0xC949D7C7509E6557ULL;
constexpr uint64_t K_MUL = 0x9DDFEA08EB382D69ULL;
// Fox seeds the first character into the upper half of seed1.
constexpr uint64_t FIRST_CHAR_SEED_SHIFT = 0x10000;

uint64_t Rotate(uint64_t v, int s) { return s == 0 ? v : (v >> s) | (v << (64 - s)); }
uint64_t RotateAtLeast1(uint64_t v, uint64_t s) {
  s &= 63;
  return s == 0 ? v : (v >> s) | (v << (64 - s));
}
uint64_t ShiftMix(uint64_t v) { return v ^ (v >> 47); }
uint64_t Fetch64(const uint8_t* p) { uint64_t v; std::memcpy(&v, p, 8); return v; }
uint64_t Fetch32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }

uint64_t HashLen16(uint64_t u, uint64_t v) {
  uint64_t a = (u ^ v) * K_MUL;
  a ^= a >> 47;
  uint64_t b = (v ^ a) * K_MUL;
  b ^= b >> 47;
  return b * K_MUL;
}

uint64_t HashLen0to16(const uint8_t* s, size_t len) {
  if (len > 8) {
    uint64_t a = Fetch64(s), b = Fetch64(s + len - 8);
    return HashLen16(a, RotateAtLeast1(b + len, len)) ^ b;
  }
  if (len >= 4) {
    uint64_t a = Fetch32(s);
    return HashLen16(len + (a << 3), Fetch32(s + len - 4));
  }
  if (len > 0) {
    uint32_t y = s[0] + (static_cast<uint32_t>(s[len >> 1]) << 8);
    uint32_t z = static_cast<uint32_t>(len) + (static_cast<uint32_t>(s[len - 1]) << 2);
    return ShiftMix(y * K2 ^ z * K3) * K2;
  }
  return K2;
}

uint64_t HashLen17to32(const uint8_t* s, size_t len) {
  uint64_t a = Fetch64(s) * K1, b = Fetch64(s + 8);
  uint64_t c = Fetch64(s + len - 8) * K2, d = Fetch64(s + len - 16) * K0;
  return HashLen16(Rotate(a - b, 43) + Rotate(c, 30) + d, a + Rotate(b ^ K3, 20) - c + len);
}

std::pair<uint64_t, uint64_t> WeakHash32(const uint8_t* s, uint64_t a, uint64_t b) {
  uint64_t w = Fetch64(s), x = Fetch64(s + 8), y = Fetch64(s + 16), z = Fetch64(s + 24);
  a += w;
  b = Rotate(b + a + z, 21);
  uint64_t c = a;
  a += x + y;
  b += Rotate(a, 44);
  return {a + z, b + c};
}

uint64_t HashLen33to64(const uint8_t* s, size_t len) {
  uint64_t z = Fetch64(s + 24);
  uint64_t a = Fetch64(s) + (len + Fetch64(s + len - 16)) * K0;
  uint64_t b = Rotate(a + z, 52), c = Rotate(a, 37);
  a += Fetch64(s + 8);
  c += Rotate(a, 7);
  a += Fetch64(s + 16);
  uint64_t vf = a + z, vs = b + Rotate(a, 31) + c;
  a = Fetch64(s + 16) + Fetch64(s + len - 32);
  z = Fetch64(s + len - 8);
  b = Rotate(a + z, 52);
  c = Rotate(a, 37);
  a += Fetch64(s + len - 24);
  c += Rotate(a, 7);
  a += Fetch64(s + len - 16);
  uint64_t wf = a + z, ws = b + Rotate(a, 31) + c;
  uint64_t r = ShiftMix((vf + ws) * K2 + (wf + vs) * K0);
  return ShiftMix(r * K0 + vs) * K2;
}

uint64_t CityHash64(const uint8_t* s, size_t len) {
  if (len <= 16) return HashLen0to16(s, len);
  if (len <= 32) return HashLen17to32(s, len);
  if (len <= 64) return HashLen33to64(s, len);
  uint64_t x = Fetch64(s + len - 40);
  uint64_t y = Fetch64(s + len - 16) + Fetch64(s + len - 56);
  uint64_t z = HashLen16(Fetch64(s + len - 48) + len, Fetch64(s + len - 24));
  auto v = WeakHash32(s + len - 64, len, z);
  auto w = WeakHash32(s + len - 32, y + K1, x);
  x = x * K1 + Fetch64(s);
  size_t remaining = (len - 1) & ~static_cast<size_t>(63);
  do {
    x = Rotate(x + y + v.first + Fetch64(s + 8), 37) * K1;
    y = Rotate(y + v.second + Fetch64(s + 48), 42) * K1;
    x ^= w.second;
    y += v.first + Fetch64(s + 40);
    z = Rotate(z + w.first, 33) * K1;
    v = WeakHash32(s, v.second * K1, x + w.first);
    w = WeakHash32(s + 32, z + w.second, y + Fetch64(s + 16));
    std::swap(z, x);
    s += 64;
    remaining -= 64;
  } while (remaining != 0);
  return HashLen16(HashLen16(v.first, w.first) + ShiftMix(y) * K1 + z, HashLen16(v.second, w.second) + x);
}

}  // namespace

uint64_t StrCode64(std::string_view text) {
  if (text.empty()) return 0;
  std::string buf(text);
  buf.push_back('\0');
  const auto* bytes = reinterpret_cast<const uint8_t*>(buf.data());
  const int64_t first = static_cast<int8_t>(bytes[0]);
  const uint64_t seed1 = static_cast<uint64_t>(first * static_cast<int64_t>(FIRST_CHAR_SEED_SHIFT)) + text.size();
  return HashLen16(CityHash64(bytes, buf.size()) - K2, seed1);
}

}  // namespace pes
