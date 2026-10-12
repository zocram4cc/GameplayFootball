// Assembles one PES 2021 stadium's model placements from its fox2 data files
// (docs: docs/re/pes-re-stadium-camera.md 1.2-1.3, docs/re/pes-re-fox2-format.md).
#include "pes/stadium.hpp"

#include <cstdio>
#include <cstring>

#include "pes/fpk.hpp"
#include "pes/fox2.hpp"

namespace pes {
namespace {

// Stadium.bin: one fixed record per stadium (OBS: the file is 21560 bytes =
// 55 records, and every folder field reads "st" + the id zero-padded to 3
// digits, 55/55).
constexpr size_t kStadiumRecordBytes = 392;
constexpr size_t kStadiumIdOffset = 4;        // u16 stadium id
constexpr size_t kStadiumFolderOffset = 370;  // NUL-terminated folder
constexpr int kStadiumFolderDigits = 3;       // "st002" for id 2

// OBS: every Stadium.bin folder is "st" + id zero-padded to 3 digits.
std::string StadiumFolder(int id) {
  std::string s = std::to_string(id);
  if (static_cast<int>(s.size()) < kStadiumFolderDigits)
    s.insert(0, kStadiumFolderDigits - static_cast<int>(s.size()), '0');
  return "st" + s;
}

// The .fox2 payload of an .fpkd: the member whose path ends ".fox2".
std::optional<std::span<const uint8_t>> Fox2In(const FpkArchive& fpkd,
                                               std::span<const uint8_t> blob,
                                               std::string* error) {
  for (const auto& e : fpkd.entries)
    if (e.path.size() > 5 && e.path.compare(e.path.size() - 5, 5, ".fox2") == 0) {
      if (e.offset + e.size > blob.size()) {
        *error = "fox2 member out of bounds";
        return std::nullopt;
      }
      return std::span<const uint8_t>(blob.data() + e.offset, e.size);
    }
  *error = "no .fox2 member";
  return std::nullopt;
}

// The model's TransformEntity: scale (WideVec3), rotation (Quat), translation
// (WideVec3). A missing or unresolvable transform is the Fox identity — PES
// leaves unrotated models exactly that way.
void ExtractTransform(const Fox2File& file, const Fox2Entity& model, Transform* t) {
  const Fox2Property* tr = model.Find("transform");
  if (!tr || tr->values.empty()) return;
  const Fox2Entity* te = file.ByAddress(tr->values[0].u);
  if (!te) return;
  if (const Fox2Property* p = te->Find("transform_scale"))
    if (!p->values.empty()) t->scale = p->values[0].v3;
  if (const Fox2Property* p = te->Find("transform_rotation_quat"))
    if (!p->values.empty()) t->rotation = p->values[0].q;
  if (const Fox2Property* p = te->Find("transform_translation"))
    if (!p->values.empty()) t->translation = p->values[0].v3;
}

// The fmdl a model places: its modelFile FilePtr resolved against the sibling
// .fpk's members. The FilePtr is "/Assets/.../x.fmdl"; FpkEntry::path is the
// same with the leading '/' stripped. Returns nullopt when the pack does not
// ship the fmdl — PES cannot place a model its pack lacks (e.g. a 4cc partial
// pack that defines a scene slot but ships only some of the meshes).
const FpkEntry* MemberFor(const FpkArchive& fpk, const Fox2Value& modelFile) {
  if (modelFile.s.empty()) return nullptr;
  std::string member = modelFile.s;
  member.erase(0, member.find_first_not_of('/'));
  for (const auto& e : fpk.entries)
    if (e.path == member) return &e;
  return nullptr;
}

// Adds every placed model from one data source: the .fpkd (the fox2) and its
// sibling .fpk (the fmdl pack). A missing source is skipped (PES did not ship
// it for this stadium); a corrupt fpkd/fox2 is an error. An empty (48-byte)
// override places nothing.
bool AddSource(const PesVfs& vfs, const std::string& fpkdPath,
               std::vector<PlacedModel>* out, std::string* error) {
  if (!vfs.Exists(fpkdPath)) return true;  // not shipped -> nothing to place
  const std::string fpkPath = fpkdPath.substr(0, fpkdPath.size() - 1);  // .fpkd->.fpk
  auto fpkdBlob = vfs.Read(fpkdPath, error);
  if (!fpkdBlob) return false;
  auto fpkd = FpkArchive::Parse(*fpkdBlob, error);
  if (!fpkd) return false;
  if (fpkd->entries.empty()) return true;  // 48-byte empty override
  auto fox2 = Fox2In(*fpkd, *fpkdBlob, error);
  if (!fox2) {
    *error = fpkdPath + ": " + *error;
    return false;
  }
  std::string ferr;
  auto file = ParseFox2(*fox2, &ferr);
  if (!file) {
    *error = fpkdPath + " fox2: " + ferr;
    return false;
  }
  FpkArchive fpk;
  if (vfs.Exists(fpkPath)) {
    auto fpkBlob = vfs.Read(fpkPath, error);
    if (!fpkBlob) return false;
    auto parsed = FpkArchive::Parse(*fpkBlob, error);
    if (!parsed) return false;
    fpk = std::move(*parsed);
  }
  for (const auto& ent : file->entities) {
    const Fox2Property* mf = ent.Find("modelFile");
    if (!mf || mf->values.empty()) continue;
    const FpkEntry* member = MemberFor(fpk, mf->values[0]);
    if (!member) continue;  // fmdl not in the pack -> not placed
    PlacedModel m;
    if (const Fox2Property* n = ent.Find("name"))
      if (!n->values.empty() && !n->values[0].s.empty()) m.entityName = n->values[0].s;
    m.className = ent.className;
    m.fpk = fpkPath;
    m.member = member->path;
    ExtractTransform(*file, ent, &m.transform);
    if (const Fox2Property* p = ent.Find("kind"))
      if (!p->values.empty()) m.kind = static_cast<uint32_t>(p->values[0].i);
    if (const Fox2Property* p = ent.Find("direction"))
      if (!p->values.empty()) m.direction = static_cast<uint32_t>(p->values[0].i);
    if (const Fox2Property* p = ent.Find("flags"))
      if (!p->values.empty()) m.flags = static_cast<uint32_t>(p->values[0].u);
    out->push_back(std::move(m));
  }
  return true;
}

// The corner-flag data source. A per-stadium override (the single .fpkd under
// <folder>/cornerflag/#Win/, whose NNNNN file number is a mined per-stadium id
// discovered by listing, never hard-coded) wins; otherwise the common pack.
std::string CornerFlagFpkd(const PesVfs& vfs, const std::string& folder) {
  const std::string per = "Asset/model/bg/" + folder + "/cornerflag/#Win/cornerflag";
  for (const auto& p : vfs.List(per))
    if (p.size() >= 5 && p.compare(p.size() - 5, 5, ".fpkd") == 0) return p;
  const std::string common = "Asset/model/bg/common/cornerflag/#Win/cornerflag.fpkd";
  return vfs.Exists(common) ? common : std::string();
}

}  // namespace

std::vector<StadiumInfo> ParseStadiumRecords(std::span<const uint8_t> blob,
                                             std::string* error) {
  std::vector<StadiumInfo> out;
  if (blob.size() % kStadiumRecordBytes != 0) {
    *error = "Stadium.bin size is not a multiple of the record size";
    return out;
  }
  for (size_t off = 0; off + kStadiumRecordBytes <= blob.size(); off += kStadiumRecordBytes) {
    const uint8_t* rec = blob.data() + off;
    StadiumInfo info;
    uint16_t id;
    std::memcpy(&id, rec + kStadiumIdOffset, sizeof id);
    info.id = id;
    const char* f = reinterpret_cast<const char*>(rec + kStadiumFolderOffset);
    size_t len = 0;
    while (kStadiumFolderOffset + len < kStadiumRecordBytes && f[len] != 0) ++len;
    if (kStadiumFolderOffset + len >= kStadiumRecordBytes) {
      *error = "Stadium.bin folder string overruns the record";
      return out;
    }
    info.folder.assign(f, len);
    out.push_back(std::move(info));
  }
  return out;
}

std::vector<StadiumInfo> ReadStadiumList(const PesVfs& vfs, std::string* error) {
  auto blob = vfs.Read("common/etc/pesdb/Stadium.bin", error);
  if (!blob) return {};
  return ParseStadiumRecords(*blob, error);
}

std::optional<std::vector<PlacedModel>> LoadStadiumScene(const PesVfs& vfs,
                                                         int stadiumId,
                                                         std::string* error) {
  const std::string folder = StadiumFolder(stadiumId);
  const std::string bg = "Asset/model/bg/";
  std::vector<PlacedModel> models;
  // The scene is the stadium's own pack and is required.
  const std::string sceneFpkd = bg + folder + "/#Win/" + folder + ".fpkd";
  if (!vfs.Exists(sceneFpkd)) {
    *error = "scene fpkd not found: " + sceneFpkd;
    return std::nullopt;
  }
  // Goal placement source: docs/re/pes-re-stadium-camera.md 1.3 — the common
  // goal_post.fox2 places goal.fmdl twice (identity at -X, and a 180-degree
  // rotation about Y to mirror it for +X) plus its shadow; the nets are the
  // separate common goalnet_{l,r} packs.
  if (!AddSource(vfs, sceneFpkd, &models, error)) return std::nullopt;
  // The rest are best-effort: a source PES did not ship for this stadium is
  // skipped, but a corrupt one is still an error.
  if (!AddSource(vfs, bg + folder + "/#Win/" + folder + "_df.fpkd", &models, error))
    return std::nullopt;  // day/fine overlay
  if (!AddSource(vfs, bg + folder + "/pitch/#Win/pitch_" + folder + ".fpkd", &models,
                 error))
    return std::nullopt;
  if (!AddSource(vfs, bg + "common/goal/#Win/goal_post.fpkd", &models, error))
    return std::nullopt;  // goal posts (+ shadow meshes)
  if (!AddSource(vfs, bg + "common/goal/#Win/goalnet_l.fpkd", &models, error))
    return std::nullopt;  // nets
  if (!AddSource(vfs, bg + "common/goal/#Win/goalnet_r.fpkd", &models, error))
    return std::nullopt;
  if (const std::string cflag = CornerFlagFpkd(vfs, folder); !cflag.empty())
    if (!AddSource(vfs, cflag, &models, error)) return std::nullopt;
  return models;
}

}  // namespace pes
