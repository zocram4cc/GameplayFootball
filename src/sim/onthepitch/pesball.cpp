#include "onthepitch/pesball.hpp"

#include <cstring>

#include "pes/inflate.hpp"
#include "pes/uniform.hpp"

namespace pes {
namespace {

// The WESYS record container the game's constant::Ball::LoadRecord reads
// (docs/re/pes-re-ball-constants.md 1): u32 count at 0, u32 table offset at 4,
// 12-byte {off, size, nameOff} records.
constexpr size_t TABLE_HEADER_BYTES = 8, TABLE_RECORD_BYTES = 12;
constexpr const char* CONSTANT_MATCH_PATH = "common/match/constant/constant_match.bin";
// The object name Ball::LoadRecord looks up (class constant::Ball lower-cased
// plus ".o", the container's object-name rule).
constexpr const char* BALL_OBJECT = "ball.o";

// The scalar header field -> ball.o disk offset, from the loader's per-field
// table (report 6.1/3.2). `size` is 4 for every float slot.
struct ScalarSlot {
  uint32_t offset;
  size_t size;
  float* out;
};

template <typename T>
T Load(const uint8_t* p) {
  T v;
  std::memcpy(&v, p, sizeof v);
  return v;
}

bool ReadRecordFloat(const std::vector<uint8_t>& rec, uint32_t at, float* out) {
  if (at + sizeof(float) > rec.size()) return false;
  *out = Load<float>(&rec[at]);
  return true;
}

bool ReadRecordInt(const std::vector<uint8_t>& rec, uint32_t at, int* out) {
  if (at + sizeof(int) > rec.size()) return false;
  *out = Load<int>(&rec[at]);
  return true;
}

}  // namespace

int BallRecordConstants::AtInt(ArrayRow row, int mode) const { return int(rawArrays[row][mode]); }

std::optional<BallImageConstants> LoadBallImageConstants(const PeImage& exe, std::string* error) {
  BallImageConstants c;
  bool ok = true;
  auto f = [&](uint64_t va, float* out) {
    ok &= exe.ReadVa(va, sizeof(float), reinterpret_cast<uint8_t*>(out));
  };
  f(GRAVITY_VA, &c.gravity);
  f(AIR_DENSITY_VA, &c.airDensity);
  f(BALL_AREA_VA, &c.ballArea);
  f(BALL_RADIUS_VA, &c.ballRadius);
  f(BALL_RADIUS_SQUARED_VA, &c.ballRadiusSquared);
  f(DRAG_CRISIS_SQUARED_VA, &c.dragCrisisSquared);
  f(HALF_VA, &c.half);
  f(PI_VA, &c.pi);
  f(KMH_PER_MS_VA, &c.kmhPerMs);
  f(SECONDS_PER_HOUR_VA, &c.secondsPerHour);
  f(MILLI_VA, &c.milli);
  f(TICK_HZ_VA, &c.tickHz);
  f(NON_SPIN_TOP_MAX_VA, &c.nonSpinTopMax);
  f(NON_SPIN_TOP_PLATEAU_VA, &c.nonSpinTopPlateau);
  f(STATIC_RESTITUTION_VA, &c.staticRestitution);
  f(STATIC_RETENTION_VA, &c.staticRetention);
  f(FRAME_TILT_VA, &c.frameTilt);
  f(DEGREES_PER_RADIAN_VA, &c.degreesPerRadian);
  f(PUSH_GAIN_VA, &c.pushGain);
  f(RECENT_HIT_DECAY_VA, &c.recentHitDecayPerS);
  f(SPEED_UP_GATE_SQ_VA, &c.speedUpGateSq);
  f(SPEED_CAP_VA, &c.speedCap_mps);
  f(VERTICAL_CAP_VA, &c.verticalCap_mps);
  f(SPIN_BLEND_BASE_VA, &c.spinBlendBase);
  f(STATIC_RESTITUTION_VA, &c.spinBlendSpan);
  f(SPIN_BLEND_FLOOR_VA, &c.spinBlendFloor_kmh);
  f(SPIN_BLEND_RAMP_VA, &c.spinBlendRampPerKmh);
  f(SPIN_CAP_VA, &c.spinCap_revPerS);
  f(SNAP_SPEED_CHANGE_SQ_VA, &c.snapSpeedChangeSq_kmh2);
  if (!ok) {
    *error = "PES2021.exe: ball image constants unmapped";
    return std::nullopt;
  }
  // The spin scale is a code immediate written into .data at run time
  // (report 4.1: `movl $0x43f6172b,0x14349f780`): the .data slot holds 1.0f on
  // disk. Read the immediate out of the one-time initializer at
  // VA 0x140875fb7 (`c7 05 <rel32> 2b 17 f6 43`, imm32 at +6).
  uint8_t imm[4];
  if (!exe.ReadVa(0x140875fb7 + 6, sizeof imm, imm)) {
    *error = "PES2021.exe: ball spin-scale initializer unmapped";
    return std::nullopt;
  }
  std::memcpy(&c.spinScale, imm, sizeof c.spinScale);
  c.invSpinScale = 1.0f / c.spinScale;

  // The contact-mode table (report 3.1) is a 3x3 i32 block in the exe's
  // writable .data at VA 0x14349f718, read by FUN_1408723d0's
  // `mov ebx,[rax + (pitch*3 + weather)*4]`. Read it the same way: the table
  // is not transcribed, and it is not initialised by code (a full
  // rip-relative scan of .text finds no other reference to it, and the bytes
  // the game reads live match what the disk carries).
  for (int p = 0; p < kPitchCount; ++p)
    for (int w = 0; w < kWeatherCount; ++w) {
      const uint64_t va = MODE_TABLE_VA + uint64_t(p * kWeatherCount + w) * sizeof(uint32_t);
      uint32_t value = 0;
      if (!exe.ReadVa(va, sizeof value, reinterpret_cast<uint8_t*>(&value))) {
        *error = "PES2021.exe: contact mode table slot unmapped";
        return std::nullopt;
      }
      c.modeTable[p][w] = int(value);
    }
  return c;
}

int ContactModeFor(const BallImageConstants& c, int pitch, int weather) {
  if (pitch < 0 || pitch >= kPitchCount || weather < 0 || weather >= kWeatherCount)
    return kContactModeOutOfRange;
  return c.modeTable[pitch][weather];
}

std::optional<BallRecordConstants> LoadBallRecordConstants(const PesVfs& vfs, std::string* error) {
  auto blob = vfs.Read(CONSTANT_MATCH_PATH, error);
  if (!blob) return std::nullopt;
  auto table = ParseUniformTable(*blob, error);  // the same WESYS object container
  if (!table) return std::nullopt;
  auto span = table->Find(BALL_OBJECT);
  if (!span) {
    *error = std::string(BALL_OBJECT) + " missing from " + CONSTANT_MATCH_PATH;
    return std::nullopt;
  }
  const std::vector<uint8_t> rec(span->begin(), span->end());
  // The record is a 0x110-byte scalar header followed by 24 blocks of 32 bytes
  // (6 floats + 8 padding), reached by record-relative u32 pointers.
  if (rec.size() < BallRecordConstants::kScalarBytes) {
    *error = std::string(BALL_OBJECT) + ": record truncated";
    return std::nullopt;
  }
  BallRecordConstants c;
  // The scalars, at the loader's own disk offsets.
  struct Slot {
    uint32_t at;
    float* out;
  };
  const Slot kScalars[] = {
      {0x000, &c.airRegistNormal},      {0x004, &c.backSpinBuoyancyRate},
      {0x008, &c.backSpinLogRate},      {0x00C, &c.boundBoundSpeedMax},
      {0x010, &c.boundBoundSpeedMin},   {0x020, &c.boundFrictionSpeedMax},
      {0x024, &c.boundFrictionSpeedMin}, {0x02C, &c.boundRollMax},
      {0x030, &c.boundRotationSpeedMax}, {0x034, &c.boundRotationSpeedMin},
      {0x058, &c.curve},                {0x064, &c.dragSpeedMax},
      {0x068, &c.dragSpeedMin},         {0x088, &c.grounderBoundYAdd},
      {0x08C, &c.grounderCTan},         {0x090, &c.grounderFrictionRate},
      {0x094, &c.grounderSpeed},        {0x098, &c.magnusRate},
      {0x0A0, &c.natualBoundB},         {0x0A8, &c.naturalRoll},
      {0x0AC, &c.nonSpinMax},           {0x0B0, &c.nonSpinMin},
      {0x0B4, &c.nonSpinRate},          {0x0CC, &c.roll},
      {0x0D0, &c.rollToBoundAdd},       {0x0D4, &c.rollToBoundRollMax},
      {0x0D8, &c.rollToBoundRollMin},   {0x0F4, &c.stopSpeed},
  };
  for (const Slot& s : kScalars)
    if (!ReadRecordFloat(rec, s.at, s.out)) {
      *error = std::string(BALL_OBJECT) + ": scalar past end";
      return std::nullopt;
    }
  if (!ReadRecordInt(rec, 0x1c, &c.spinScaleSlot)) {
    *error = std::string(BALL_OBJECT) + ": spin-scale slot past end";
    return std::nullopt;
  }
  // The 24 blocks: each header pointer field holds the block's offset from the
  // start of the record (proven by the exact 0x20-step progression covering
  // [0x110, 0x410); report 1.1).
  for (size_t row = 0; row < BallRecordConstants::kArrayRowCount; ++row) {
    const uint32_t slot = BallRecordConstants::kArrayPointerSlots[row];
    const uint32_t block = Load<uint32_t>(&rec[slot]);
    const size_t blockBytes = kBallModeCount * sizeof(float);
    if (block > rec.size() || blockBytes > rec.size() - block) {
      *error = std::string(BALL_OBJECT) + ": block " + std::to_string(row) + " past end";
      return std::nullopt;
    }
    for (int m = 0; m < kBallModeCount; ++m) {
      c.arrays[row][m] = Load<float>(&rec[block + m * sizeof(float)]);
      c.rawArrays[row][m] = Load<uint32_t>(&rec[block + m * sizeof(float)]);
    }
  }
  return c;
}

std::optional<PesBallConstants> LoadPesBallConstants(const PeImage& exe, const PesVfs& vfs,
                                                     std::string* error) {
  PesBallConstants c;
  auto image = LoadBallImageConstants(exe, error);
  if (!image) return std::nullopt;
  c.image = *image;
  auto record = LoadBallRecordConstants(vfs, error);
  if (!record) return std::nullopt;
  c.record = *record;
  return c;
}

}  // namespace pes
