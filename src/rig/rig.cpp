// The PES animation rig, built at runtime from the user's install. Port of
// GameplayFootball tools/pes21_import/retarget.py (_build_align,
// _extend_bind_with_fingers, resolve_bone). Every position comes from the
// .ask/.skl files; none is transcribed here.
#include "rig/rig.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "pes/ask.hpp"
#include "pes/fpk.hpp"
#include "pes/skl.hpp"
#include "pes/vfs.hpp"
#include "rig/rig_names.hpp"

namespace pesrig {
namespace {

constexpr const char* ANIM_SKEL = "common/anime/FoxAnim/Body/CharacterAssets/body_anim_skel.ask";
constexpr const char* HIGH_SKEL = "common/anime/FoxAnim/Body/CharacterAssets/body_high_skel.ask";
constexpr const char* COMMON_PACKAGE = "Asset/model/character/#Win/common_package.fpk";

// Below this a direction is degenerate (GF retarget.py uses the same 1e-9).
constexpr double DEGENERATE_LENGTH = 1e-9;
// Directions closer than this are treated as already aligned (retarget.py _minarc).
constexpr double PARALLEL_EPSILON = 1e-12;
// .ask bind rotations must be identity; the rig relies on world-aligned frames.
constexpr double IDENTITY_TOLERANCE = 1e-6;

struct D3 { double x = 0, y = 0, z = 0; };
struct DQ { double x = 0, y = 0, z = 0, w = 1; };

D3 operator+(D3 a, D3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
D3 operator-(D3 a, D3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
D3 operator*(D3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double Dot(D3 a, D3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
double Len(D3 a) { return std::sqrt(Dot(a, a)); }
D3 Norm(D3 a) { return a * (1.0 / Len(a)); }
D3 Cross(D3 a, D3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
D3 ToD(pes::Vec3 v) { return {v.x, v.y, v.z}; }
Vec3 ToF(D3 v) { return {float(v.x), float(v.y), float(v.z)}; }

DQ Mul(DQ a, DQ b) {
  return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
          a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
D3 Rot(DQ q, D3 v) {
  const DQ r = Mul(Mul(q, {v.x, v.y, v.z, 0}), {-q.x, -q.y, -q.z, q.w});
  return {r.x, r.y, r.z};
}

// Smallest rotation taking direction a onto direction b.
DQ MinArc(D3 a, D3 b) {
  a = Norm(a);
  b = Norm(b);
  const double d = Dot(a, b);
  if (d > 1.0 - PARALLEL_EPSILON) return {};
  const D3 c = Cross(a, b);
  const double s = std::sqrt((1.0 + d) * 2.0);
  return {c.x / s, c.y / s, c.z / s, s * 0.5};
}

// Minimal arc for the primary direction, then the twist about it that best
// matches the secondary: two vectors pin all three degrees of freedom.
DQ FrameAlign(D3 aPrimary, D3 tPrimary, D3 aSecondary, D3 tSecondary) {
  const DQ swing = MinArc(aPrimary, tPrimary);
  const D3 axis = Norm(tPrimary);
  auto flat = [&](D3 v) { return v - axis * Dot(v, axis); };
  D3 u = flat(Rot(swing, aSecondary));
  D3 w = flat(tSecondary);
  if (Len(u) < DEGENERATE_LENGTH || Len(w) < DEGENERATE_LENGTH) return swing;
  u = Norm(u);
  w = Norm(w);
  const double cosT = std::fmax(-1.0, std::fmin(1.0, Dot(u, w)));
  const double sinT = Dot(Cross(u, w), axis);
  const double theta = std::atan2(sinT, cosT);
  const double s = std::sin(theta * 0.5);
  return Mul({axis.x * s, axis.y * s, axis.z * s, std::cos(theta * 0.5)}, swing);
}

}  // namespace

std::optional<Rig> BuildRig(pes::PesVfs& vfs, std::string* error) {
  // Anim (T-pose) bind: body_anim_skel.ask, globals by chaining locals.
  auto animBytes = vfs.Read(ANIM_SKEL, error);
  if (!animBytes) return std::nullopt;
  auto anim = pes::ParseAsk(*animBytes, error);
  if (!anim) return std::nullopt;
  std::unordered_map<std::string, D3> animPos;
  std::vector<D3> animGlobal(anim->size());
  for (size_t i = 0; i < anim->size(); ++i) {
    const auto& b = (*anim)[i];
    const pes::Quat q = b.rotation;
    if (std::fabs(q.x) > IDENTITY_TOLERANCE || std::fabs(q.y) > IDENTITY_TOLERANCE ||
        std::fabs(q.z) > IDENTITY_TOLERANCE || std::fabs(q.w - 1) > IDENTITY_TOLERANCE) {
      *error = "body_anim_skel.ask: non-identity bind rotation on " + b.name;
      return std::nullopt;
    }
    animGlobal[i] = (b.parent >= 0 ? animGlobal[b.parent] : D3{}) + ToD(b.local);
    animPos[b.name] = animGlobal[i];
  }

  // Leaf helpers: sk_toe_*, tip_sk_hand_*, tip_sk_head locals from body_high_skel.ask.
  auto highBytes = vfs.Read(HIGH_SKEL, error);
  if (!highBytes) return std::nullopt;
  auto high = pes::ParseAsk(*highBytes, error);
  if (!high) return std::nullopt;
  std::unordered_map<std::string, D3> highLocal;
  for (const auto& b : *high) highLocal[b.name] = ToD(b.local);

  // Render bind: body.skl, hand_l.skl, hand_r.skl inside common_package.fpk.
  auto pkgBytes = vfs.Read(COMMON_PACKAGE, error);
  if (!pkgBytes) return std::nullopt;
  auto pkg = pes::FpkArchive::Parse(*pkgBytes, error);
  if (!pkg) return std::nullopt;
  std::unordered_map<std::string, D3> bodyRender, handRender;
  for (const char* suffix : {"/body.skl", "/hand_l.skl", "/hand_r.skl"}) {
    const std::string sfx = suffix;
    const pes::FpkEntry* found = nullptr;
    for (const auto& e : pkg->entries)
      if (e.path.size() >= sfx.size() && e.path.compare(e.path.size() - sfx.size(), sfx.size(), sfx) == 0) found = &e;
    if (!found) {
      *error = "common_package.fpk has no " + sfx.substr(1);
      return std::nullopt;
    }
    auto skl = pes::ParseSkl(std::span(*pkgBytes).subspan(found->offset, found->size), error);
    if (!skl) return std::nullopt;
    auto& dest = sfx == "/body.skl" ? bodyRender : handRender;
    for (const auto& b : *skl) dest[b.name] = ToD(b.Position());
  }

  const auto& names = RigNodeNames();
  for (const auto& n : names) {
    const bool finger = n.pesBone.rfind("skh_", 0) == 0;
    if (!finger && !animPos.count(n.pesBone)) { *error = "body_anim_skel.ask lacks " + n.pesBone; return std::nullopt; }
    if (finger && !handRender.count(n.pesBone)) { *error = "hand .skl lacks " + n.pesBone; return std::nullopt; }
    if (!finger && n.pesBone != ROOT_BONE && !bodyRender.count(n.pesBone)) {
      *error = "body.skl lacks " + n.pesBone;
      return std::nullopt;
    }
  }
  for (const char* need : {"sk_toe_l", "sk_toe_r", "tip_sk_hand_l", "tip_sk_hand_r", "tip_sk_head"})
    if (!highLocal.count(need)) { *error = std::string("body_high_skel.ask lacks ") + need; return std::nullopt; }
  for (const char* need : {"dsk_toe_l", "dsk_toe_r"})
    if (!bodyRender.count(need)) { *error = std::string("body.skl lacks ") + need; return std::nullopt; }

  // The mover has no render bone; it does not move between the two poses.
  auto renderOf = [&](const std::string& bone) {
    if (bone == ROOT_BONE) return animPos[bone];
    auto it = bodyRender.find(bone);
    return it != bodyRender.end() ? it->second : handRender.at(bone);
  };

  // W per body bone: render pose -> anim pose.
  std::unordered_map<std::string, DQ> align;
  for (const auto& n : names) {
    if (n.pesBone.rfind("skh_", 0) == 0) continue;
    const std::string& bone = n.pesBone;
    const auto& refs = AlignRefChild();
    auto ref = std::find_if(refs.begin(), refs.end(), [&](const auto& p) { return p.first == bone; });
    if (ref != refs.end()) {
      const D3 a = renderOf(ref->second) - renderOf(bone), t = animPos[ref->second] - animPos[bone];
      align[bone] = (Len(a) < DEGENERATE_LENGTH || Len(t) < DEGENERATE_LENGTH) ? DQ{} : MinArc(a, t);
    } else if (bone == "sk_foot_l" || bone == "sk_foot_r") {
      const std::string side = bone.substr(bone.size() - 2);
      align[bone] = MinArc(bodyRender["dsk_toe" + side] - renderOf(bone), highLocal["sk_toe" + side]);
    } else if (bone == "sk_head") {
      align[bone] = MinArc({0, 1, 0}, highLocal["tip_sk_head"]);
    }
  }
  // Hands: the palm pins the twist. Anim pose: fingers along the tip, back of
  // the hand +Y. Render pose: back-of-hand normal from the finger cluster.
  for (const char* side : {"_l", "_r"}) {
    const std::string hand = std::string("sk_hand") + side;
    const D3 fingers = handRender["skh_middle_dip" + std::string(side)] - renderOf(hand);
    const D3 spread = handRender["skh_index_mata" + std::string(side)] - handRender["skh_pinky_mata" + std::string(side)];
    D3 back = Norm(Cross(spread, fingers));
    if (std::string(side) == "_r") back = back * -1.0;  // mirrored winding flips the normal
    align[hand] = FrameAlign(fingers, highLocal["tip_sk_hand" + std::string(side)], back, {0, 1, 0});
  }
  // Forearms inherit the hand's twist, corrected by the minimal arc that keeps
  // their own position constraint exact.
  for (const char* side : {"_l", "_r"}) {
    const std::string forearm = std::string("sk_forearm") + side, hand = std::string("sk_hand") + side;
    const D3 a = renderOf(hand) - renderOf(forearm), t = animPos[hand] - animPos[forearm];
    align[forearm] = Mul(MinArc(Rot(align[hand], a), t), align[hand]);
  }

  // Fingers ride the hand: anim = hand_anim + W(hand) * (render - hand_render).
  for (const auto& n : names) {
    if (n.pesBone.rfind("skh_", 0) != 0) continue;
    const std::string hand = n.pesBone.back() == 'l' ? "sk_hand_l" : "sk_hand_r";
    align[n.pesBone] = align[hand];
    animPos[n.pesBone] = animPos[hand] + Rot(align[hand], handRender[n.pesBone] - renderOf(hand));
  }

  Rig rig;
  std::unordered_map<std::string, int> idOf;
  for (const auto& n : names) {
    RigBone b;
    b.gfName = n.gfName;
    b.pesBone = n.pesBone;
    b.parent = n.gfParent.empty() ? -1 : idOf.at(n.gfParent);
    const D3 global = animPos[n.pesBone];
    b.animGlobalPos = ToF(global);
    b.animLocalPos = ToF(b.parent < 0 ? global : global - animPos[rig.bones[b.parent].pesBone]);
    b.renderGlobalPos = ToF(renderOf(n.pesBone));
    const DQ w = align.at(n.pesBone);
    b.align = {float(w.x), float(w.y), float(w.z), float(w.w)};
    idOf[n.gfName] = int(rig.bones.size());
    rig.bones.push_back(std::move(b));
  }
  return rig;
}

std::optional<int> ResolveBone(const Rig& rig, std::string_view pesBone, const Vec3* renderPos) {
  auto byGf = [&](std::string_view gf) -> std::optional<int> {
    for (size_t i = 0; i < rig.bones.size(); ++i)
      if (rig.bones[i].gfName == gf) return int(i);
    return std::nullopt;
  };
  auto byPes = [&](std::string_view pes) -> std::optional<int> {
    for (size_t i = 0; i < rig.bones.size(); ++i)
      if (rig.bones[i].pesBone == pes) return int(i);
    return std::nullopt;
  };
  if (auto id = byPes(pesBone)) return id;
  for (const auto& [helper, gf] : HelperToGf())
    if (helper == pesBone) return byGf(gf);
  for (const auto& fam : HELPER_FAMILIES) {
    const std::string_view prefix = fam.prefix;
    if (pesBone.substr(0, prefix.size()) != prefix) continue;
    const std::string_view rest = pesBone.substr(prefix.size());
    auto ends = [&](std::string_view s) { return pesBone.size() >= s.size() && pesBone.substr(pesBone.size() - s.size()) == s; };
    if (ends("_l") || rest.find("_l_") != std::string_view::npos) return byGf(fam.left);
    if (ends("_r") || rest.find("_r_") != std::string_view::npos) return byGf(fam.right);
    return byGf(fam.plain ? fam.plain : fam.left);
  }
  // dsk_x -> sk_x: a twist helper sharing its master's name.
  if (pesBone.substr(0, 4) == "dsk_") {
    if (auto id = byPes("sk_" + std::string(pesBone.substr(4)))) return id;
  }
  if (!renderPos) return std::nullopt;
  int best = -1;
  double bestD = 0;
  for (size_t i = 0; i < rig.bones.size(); ++i) {
    const D3 d = ToD(rig.bones[i].renderGlobalPos) - ToD(*renderPos);
    if (best < 0 || Dot(d, d) < bestD) best = int(i), bestD = Dot(d, d);
  }
  return best;
}

PartPose ClassifyPartPose(const Rig& rig, const std::vector<pes::SklBone>& partSkeleton) {
  // Face packs agree with body.skl to 4e-5 m, boots with the .ask to 1e-7 m
  // (docs/re/pes-re-face.md, measured here); the poses differ by centimetres.
  constexpr float SAME_POSE_TOLERANCE_M = 1e-3f;
  auto near = [](const Vec3& a, const Vec3& b) {
    return std::abs(a.x - b.x) < SAME_POSE_TOLERANCE_M && std::abs(a.y - b.y) < SAME_POSE_TOLERANCE_M &&
           std::abs(a.z - b.z) < SAME_POSE_TOLERANCE_M;
  };
  // Only joints named exactly: collapsed helpers (dsk_toe -> ankle) sit elsewhere.
  bool render = true, anim = true, compared = false;
  for (const auto& sb : partSkeleton)
    for (const auto& b : rig.bones)
      if (b.pesBone == sb.name) {
        compared = true;
        render = render && near(sb.Position(), b.renderGlobalPos);
        anim = anim && near(sb.Position(), b.animGlobalPos);
      }
  if (!compared) return PartPose::Unknown;
  // Where the poses coincide (trunk), both hold and the alignment is identity anyway.
  return render ? PartPose::Render : anim ? PartPose::Anim : PartPose::Unknown;
}

}  // namespace pesrig
