#include "pes/camera.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "pes/uniform.hpp"

namespace pes {
namespace {

constexpr const char* CONSTANT_MATCH_PATH = "common/match/constant/constant_match.bin";
constexpr const char* CAMERA_INPLAY_OBJECT = "cameraInplay.o";

// cameraInplay.o layout (relative u32 pointers from the object start; JSON
// objects serialised in ASCII key order, report 4.5).
constexpr size_t ROOT_NEW_WIDE_CAMERA = 0x14;
constexpr size_t NWC_MARGIN_UNDER = 0x0, NWC_MARGIN_UPPER = 0x4, NWC_PARAM_MAX = 0xC, NWC_PARAM_MIN = 0x10;
constexpr size_t PARAM_TABLE_PTR = 0xC;
enum SortedKey { FOV, LOOK_Y, MANI, PAN, POS_Y, POS_Z, X_LIMIT, Z_MAX, Z_MIN, SORTED_KEYS };

// PES2021.exe .rdata (reports 4.3, 4.5, CameraTargetRE).
constexpr uint64_t PRESET_RECORDS_VA = 0x14260b720;
constexpr size_t PRESET_STRIDE = 0x24;
constexpr uint64_t FAR_FOV_ZOOMED_OUT_VA = 0x14259be18;   // 18
constexpr uint64_t FAR_POSY_HIGH_VA = 0x14259b8a0;        // 0.52
constexpr uint64_t NEAR_POSY_HEIGHT_SPAN_VA = 0x14260b8dc;
constexpr uint64_t NEAR_POSY_BASE_VA = 0x14260b8e0;
constexpr uint64_t Z_MAX_VA = 0x14260b8f8;
constexpr uint64_t NEAR_FOV_BASE_VA = 0x14260b8fc;
constexpr uint64_t NEAR_POSZ_VA = 0x14260b900;
constexpr uint64_t Z_MIN_VA = 0x1425c7ba4;
constexpr uint64_t PAN_BLEND_VA = 0x142598578;
constexpr uint64_t NEAR_FOV_ZOOM_SPAN_VA = 0x14258c264;   // 3, also the z-bound margin
constexpr uint64_t LIFT_BASE_VA = 0x1425656c0;            // 4
constexpr uint64_t LIFT_MAX_VA = 0x14259222c;             // 5
constexpr uint64_t LEAD_BLEND_VA = 0x14257a194;           // 0.7
constexpr uint64_t CLAMP_X_MARGIN_VA = 0x14256ae1c;       // 1
constexpr uint64_t CLAMP_Z_MARGIN_VA = 0x14258c264;       // 3 (same float as the Fov span)
constexpr uint64_t SMOOTH_STEP_UP_VA = 0x1425a3590;       // 1.1
constexpr uint64_t SMOOTH_CAP_VA = 0x14259d634;           // 10000
constexpr uint64_t K_SPRING_GAIN_VA = 0x1425c4894;        // 0.02
constexpr uint64_t K_SPRING_STEP_LIMIT_VA = 0x1425b8708;  // 0.005
// The per-frame cadence: .data 0x14351ae54 is the frame-rate reference the exe
// reads (its initialiser 0x141e532f0 stores the .rdata 0x14259d624 54.0); both
// are named here for provenance, the runtime value is the .data holder.
constexpr uint64_t FPS_REF_VA = 0x14351ae54;
constexpr uint64_t FPS_REF_SOURCE_VA = 0x14259d624;  // 54
// .data 0x1434a00b0 is the image default of the running smoothing weight (the
// exe's own initialiser value, everywhere else in [0, 1] land); the weight's
// floor of 1 is the clamp at 0x1408a3a39.
constexpr uint64_t LIFT_NUMERATOR_VA = 0x14259a764;  // 60, the lift's numerator

float Lerp(float a, float b, float t) { return a + (b - a) * t; }

template <typename T>
bool At(const std::vector<uint8_t>& d, size_t off, T* out) {
  if (off > d.size() || sizeof(T) > d.size() - off) return false;
  std::memcpy(out, &d[off], sizeof(T));
  return true;
}

bool ReadTable(const std::vector<uint8_t>& obj, size_t paramObj, CameraRecord* r) {
  uint32_t rel;
  float k[SORTED_KEYS];
  if (!At(obj, paramObj + PARAM_TABLE_PTR, &rel)) return false;
  for (int i = 0; i < SORTED_KEYS; ++i)
    if (!At(obj, rel + 4 * size_t(i), &k[i])) return false;
  *r = {k[MANI], k[FOV], k[POS_Y], k[LOOK_Y], k[POS_Z], k[Z_MIN], k[Z_MAX], k[X_LIMIT], k[PAN]};
  return true;
}

bool ReadFloat(const PeImage& exe, uint64_t va, float* out) {
  return exe.ReadVa(va, sizeof(float), reinterpret_cast<uint8_t*>(out));
}

}  // namespace

std::optional<DynamicWideParams> LoadDynamicWide(const PeImage& exe, const PesVfs& vfs, std::string* error) {
  auto blob = vfs.Read(CONSTANT_MATCH_PATH, error);
  if (!blob) return std::nullopt;
  auto table = ParseUniformTable(*blob, error);  // same WESYS object container
  if (!table) return std::nullopt;
  auto span = table->Find(CAMERA_INPLAY_OBJECT);
  if (!span) {
    *error = std::string(CAMERA_INPLAY_OBJECT) + " missing from " + CONSTANT_MATCH_PATH;
    return std::nullopt;
  }
  const std::vector<uint8_t> obj(span->begin(), span->end());
  DynamicWideParams p{};
  uint32_t nwc, maxObj, minObj;
  if (!At(obj, ROOT_NEW_WIDE_CAMERA, &nwc) || !At(obj, nwc + NWC_MARGIN_UNDER, &p.marginUnder) ||
      !At(obj, nwc + NWC_MARGIN_UPPER, &p.marginUpper) || !At(obj, nwc + NWC_PARAM_MAX, &maxObj) ||
      !At(obj, nwc + NWC_PARAM_MIN, &minObj) || !ReadTable(obj, maxObj, &p.far) || !ReadTable(obj, minObj, &p.near)) {
    *error = "cameraInplay.o: newWideCamera past end";
    return std::nullopt;
  }
  bool ok = true;
  for (size_t i = 0; i < p.presets.size(); ++i)
    ok &= exe.ReadVa(PRESET_RECORDS_VA + PRESET_STRIDE * i, sizeof(CameraRecord), reinterpret_cast<uint8_t*>(&p.presets[i]));
  auto f = [&](uint64_t va, float* out) { ok &= ReadFloat(exe, va, out); };
  f(FAR_FOV_ZOOMED_OUT_VA, &p.farFovZoomedOutDeg);
  f(FAR_POSY_HIGH_VA, &p.farPosYHigh);
  f(NEAR_POSY_BASE_VA, &p.nearPosYBase);
  f(NEAR_POSY_HEIGHT_SPAN_VA, &p.nearPosYHeightSpan);
  f(NEAR_FOV_BASE_VA, &p.nearFovBaseDeg);
  f(NEAR_FOV_ZOOM_SPAN_VA, &p.nearFovZoomSpanDeg);
  f(NEAR_POSZ_VA, &p.nearPosZ);
  f(Z_MIN_VA, &p.zMin);
  f(Z_MAX_VA, &p.zMax);
  f(PAN_BLEND_VA, &p.panBlend);
  f(LIFT_BASE_VA, &p.liftBaseM);
  f(LIFT_MAX_VA, &p.liftMaxM);
  f(LEAD_BLEND_VA, &p.leadBlend);
  f(CLAMP_X_MARGIN_VA, &p.clampXMarginM);
  f(CLAMP_Z_MARGIN_VA, &p.clampZMarginM);
  f(SMOOTH_STEP_UP_VA, &p.smoothStepUp);
  f(SMOOTH_CAP_VA, &p.smoothCap);
  f(K_SPRING_GAIN_VA, &p.kSpringGain);
  f(K_SPRING_STEP_LIMIT_VA, &p.kSpringStepLimit);
  f(FPS_REF_VA, &p.fpsRef);
  f(LIFT_NUMERATOR_VA, &p.liftNumerator);
  p.smoothFloor = 1.0f;                   // 0x3f800000 clamp at 0x1408a3a39
  if (!ok) {
    *error = "PES2021.exe: camera constants unmapped";
    return std::nullopt;
  }
  return p;
}

CameraShot DynamicWideShot(const DynamicWideParams& p, float zoom, float height, float targetZ, float W,
                           float liftTimeScale) {
  const float u = 1.0f - std::clamp(zoom, 0.0f, 1.0f), h = std::clamp(height, 0.0f, 1.0f);
  const float lo = -(W - p.marginUpper), hi = W - p.marginUnder;
  const float t = hi > lo ? std::clamp((targetZ - lo) / (hi - lo), 0.0f, 1.0f) : 0.0f;
  CameraRecord far = p.far, near = p.near;
  far.fov = Lerp(p.far.fov, p.farFovZoomedOutDeg, u);
  far.posY = Lerp(p.far.posY, p.farPosYHigh, h);
  near.fov = p.nearFovBaseDeg - p.nearFovZoomSpanDeg * u;
  near.posY = p.nearPosYBase + p.nearPosYHeightSpan * h;
  near.posZ = p.nearPosZ;
  CameraShot s;
  s.record = {Lerp(far.mani, near.mani, t), Lerp(far.fov, near.fov, t),   Lerp(far.posY, near.posY, t),
              Lerp(far.lookY, near.lookY, t), Lerp(far.posZ, near.posZ, t), p.zMin, p.zMax,
              Lerp(far.xLimit, near.xLimit, t), 0.0f};
  const float a = 1.0f - u, b = h;
  const auto& R = p.presets;
  s.record.pan = Lerp(Lerp(R[0].pan, R[1].pan, a), Lerp(R[2].pan, R[3].pan, a), b);
  s.panEff = 1.0f + (s.record.pan - 1.0f) * p.panBlend;
  // The lift's base and its time scale ride separately so that FramePose can
  // scale both the focus y and the base together (report CameraTargetRE item 1).
  s.liftBaseM = p.liftBaseM;
  s.liftTimeScale = liftTimeScale;
  s.liftMaxM = p.liftMaxM;
  s.clampXMarginM = p.clampXMarginM;
  s.clampZMarginM = p.clampZMarginM;
  return s;
}

// Report CameraTargetRE item 4: the bounds are wide (pitch + margin) at k = 0
// and collapse onto the record's own values at k = 1.
CameraPose FramePose(const CameraShot& s, float tx, float tz, float targetLift, float k, float L, float W) {
  const auto& r = s.record;
  const float xHi = (L + s.clampXMarginM) - ((L + s.clampXMarginM) - r.xLimit) * k;
  const float zWide = W + s.clampZMarginM;
  const float zHi = zWide - (zWide - r.zMax) * k;
  const float zLo = -zWide - (-zWide - r.zMin) * k;
  CameraPose p;
  p.tx = std::clamp(tx, -xHi, xHi);
  p.tz = std::clamp(tz, zLo, zHi);
  // lift = clamp((focusY + liftBase) * timeScale, 0, liftMax) (item 1: both the
  // y and the base ride the time scale, so neither is pre-scaled here).
  p.ty = r.lookY + std::clamp((targetLift + s.liftBaseM) * s.liftTimeScale, 0.0f, s.liftMaxM);
  p.cx = p.tx * (1.0f + k * (1.0f / s.panEff - 1.0f));
  p.cy = r.posZ * r.posY * k;
  p.cz = p.tz + r.posZ * k;
  p.vfovDeg = r.fov;
  return p;
}

ZoomSpring::ZoomSpring(float k0, float v0, float gain, float limit) : k(k0), v(v0), gain_(gain), limit_(limit) {}

void ZoomSpring::Step(float goal) {
  const float limit = limit_ * k;
  v += std::clamp((goal - k) * gain_ - v, -limit, limit);
  k += v;
}

void TargetSmoothing::Reset(const pes::Vec3& raw) {
  prev = raw;
  w = 1.0f;
}

TargetSmoothing::TargetSmoothing(const DynamicWideParams& p)
    : w(p.smoothFloor), stepUp_(p.smoothStepUp), cap_(p.smoothCap), floor_(p.smoothFloor) {}

void TargetSmoothing::Step(const pes::Vec3& raw, bool marginTimeActive) {
  w = marginTimeActive ? std::min(w * stepUp_, cap_) : std::max(w / stepUp_, floor_);
  // The exe skips the whole block once the weight reaches its cap (comiss at
  // 0x1408a3cd1 against the 10000 at .rdata 0x14259d634), so a capped weight
  // holds the stored target instead of blending it towards the raw focus.
  if (w >= cap_) return;
  prev.x = (prev.x + w * raw.x) / (1.0f + w);
  prev.y = (prev.y + w * raw.y) / (1.0f + w);
  prev.z = (prev.z + w * raw.z) / (1.0f + w);
}

pes::Vec3 LeadFocus(const pes::Vec3& focus, const pes::Vec3& ball, float leadBlend) {
  const float dx = focus.x - ball.x, dy = focus.y - ball.y, dz = focus.z - ball.z;
  const float dxz = std::sqrt(dx * dx + dz * dz);
  if (!(dxz > 0.0f)) return focus;  // no horizontal separation: the lead is undefined
  const float a = std::sqrt(dx * dx + dy * dy + dz * dz) / dxz;
  const float w = leadBlend * a;
  return {w * focus.x + (1.0f - w) * ball.x, w * focus.y + (1.0f - w) * ball.y, w * focus.z + (1.0f - w) * ball.z};
}

}  // namespace pes
