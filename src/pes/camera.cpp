#include "pes/camera.hpp"

#include <algorithm>
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

// PES2021.exe .rdata (report 4.3, 4.5).
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
// ponytail: the near-side Fov zoom span (`28.698 - 3u`, report 4.5) has no
// address yet (CameraTargetRE); provisional until it is read from the exe.
constexpr float NEAR_FOV_ZOOM_SPAN_DEG_PROVISIONAL = 3.0f;

constexpr float TARGET_LIFT_MAX_M = 5.0f;  // clamp in FUN_1408a2180 (report 4.3)
constexpr float SPRING_GAIN = 0.02f;       // k spring, FUN_1408a2180
constexpr float SPRING_STEP_LIMIT = 0.005f;

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
  auto f = [&](uint64_t va, float* out) { ok &= exe.ReadVa(va, sizeof(float), reinterpret_cast<uint8_t*>(out)); };
  f(FAR_FOV_ZOOMED_OUT_VA, &p.farFovZoomedOutDeg);
  f(FAR_POSY_HIGH_VA, &p.farPosYHigh);
  f(NEAR_POSY_BASE_VA, &p.nearPosYBase);
  f(NEAR_POSY_HEIGHT_SPAN_VA, &p.nearPosYHeightSpan);
  f(NEAR_FOV_BASE_VA, &p.nearFovBaseDeg);
  f(NEAR_POSZ_VA, &p.nearPosZ);
  f(Z_MIN_VA, &p.zMin);
  f(Z_MAX_VA, &p.zMax);
  f(PAN_BLEND_VA, &p.panBlend);
  p.nearFovZoomSpanDeg = NEAR_FOV_ZOOM_SPAN_DEG_PROVISIONAL;
  if (!ok) {
    *error = "PES2021.exe: camera constants unmapped";
    return std::nullopt;
  }
  return p;
}

CameraShot DynamicWideShot(const DynamicWideParams& p, float zoom, float height, float targetZ, float W) {
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
  return s;
}

// ponytail: the x/z clamp bounds blend in from wide defaults by k (report 4.3,
// start value unresolved); at the steady k ~ 1 they are the record's own.
CameraPose FramePose(const CameraShot& s, float tx, float tz, float targetLift, float k) {
  const auto& r = s.record;
  CameraPose p;
  p.tx = std::clamp(tx, -r.xLimit, r.xLimit);
  p.tz = std::clamp(tz, r.zMin, r.zMax);
  p.ty = r.lookY + std::clamp(targetLift, 0.0f, TARGET_LIFT_MAX_M);
  p.cx = p.tx * (1.0f + k * (1.0f / s.panEff - 1.0f));
  p.cy = r.posZ * r.posY * k;
  p.cz = p.tz + r.posZ * k;
  p.vfovDeg = r.fov;
  return p;
}

void ZoomSpring::Step(float goal) {
  const float limit = SPRING_STEP_LIMIT * k;
  v += std::clamp((goal - k) * SPRING_GAIN - v, -limit, limit);
  k += v;
}

}  // namespace pes
