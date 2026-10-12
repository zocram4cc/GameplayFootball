// Inspects a PES 2021 install through the native readers; JSON on stdout.
//   pesfmt_dump mounts --pes21-root DIR
//   pesfmt_dump ls <prefix> --pes21-root DIR
//   pesfmt_dump cat <path> --pes21-root DIR > file   (dev only; never into the repo)
//   pesfmt_dump ask <path> --pes21-root DIR
//   pesfmt_dump skl <fpk path> <inner suffix> --pes21-root DIR
//   pesfmt_dump rig --pes21-root DIR
//   pesfmt_dump fmdl <fpk path> --pes21-root DIR   (every .fmdl inside; summary)
//   pesfmt_dump ftex <path> --pes21-root DIR
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "pes/ask.hpp"
#include "pes/fmdl.hpp"
#include "pes/fpk.hpp"
#include "pes/ftex.hpp"
#include "pes/skl.hpp"
#include "pes/vfs.hpp"
#include "rig/rig.hpp"

namespace {

std::string Json(const std::string& s) {
  std::string out = "\"";
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') out += '\\', out += char(c);
    else if (c == '\n') out += "\\n";
    else if (c < 0x20) { char buf[8]; std::snprintf(buf, sizeof buf, "\\u%04x", c); out += buf; }
    else out += char(c);
  }
  return out + "\"";
}

std::string V3(pes::Vec3 v) {
  char buf[96];
  std::snprintf(buf, sizeof buf, "[%.9g, %.9g, %.9g]", v.x, v.y, v.z);
  return buf;
}

std::string Q4(pes::Quat q) {
  char buf[128];
  std::snprintf(buf, sizeof buf, "[%.9g, %.9g, %.9g, %.9g]", q.x, q.y, q.z, q.w);
  return buf;
}

int Usage() {
  std::fprintf(stderr, "usage: pesfmt_dump <mounts|ls PREFIX|cat PATH|ask PATH|skl FPK SUFFIX|rig> --pes21-root DIR\n");
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<std::string> pos;
  std::string root;
  if (const char* env = std::getenv("PES21_ROOT")) root = env;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--pes21-root" && i + 1 < argc) root = argv[++i];
    else pos.push_back(a);
  }
  if (pos.empty() || root.empty()) return Usage();
  const std::string& cmd = pos[0];

  pes::PesVfs vfs;
  std::string err;
  if (!vfs.Mount(root, &err)) {
    std::fprintf(stderr, "mount failed: %s\n", err.c_str());
    return 1;
  }

  if (cmd == "mounts") {
    std::printf("{\"mounts\": [");
    const std::string& r = vfs.MountReport();
    size_t start = 0;
    bool first = true;
    for (size_t nl; (nl = r.find('\n', start)) != std::string::npos; start = nl + 1) {
      std::printf("%s\n  %s", first ? "" : ",", Json(r.substr(start, nl - start)).c_str());
      first = false;
    }
    std::printf("\n]}\n");
    return 0;
  }
  if (cmd == "ls" && pos.size() == 2) {
    std::printf("[");
    bool first = true;
    for (const auto& p : vfs.List(pos[1])) {
      std::printf("%s\n  {\"path\": %s, \"size\": %llu}", first ? "" : ",", Json(p).c_str(),
                  static_cast<unsigned long long>(vfs.Size(p).value_or(0)));
      first = false;
    }
    std::printf("\n]\n");
    return 0;
  }
  if (cmd == "cat" && pos.size() == 2) {
    auto bytes = vfs.Read(pos[1], &err);
    if (!bytes) {
      std::fprintf(stderr, "%s\n", err.c_str());
      return 1;
    }
    std::fwrite(bytes->data(), 1, bytes->size(), stdout);
    return 0;
  }
  auto fail = [&] {
    std::fprintf(stderr, "%s\n", err.c_str());
    return 1;
  };
  if (cmd == "ask" && pos.size() == 2) {
    auto bytes = vfs.Read(pos[1], &err);
    if (!bytes) return fail();
    auto bones = pes::ParseAsk(*bytes, &err);
    if (!bones) return fail();
    std::printf("[");
    for (size_t i = 0; i < bones->size(); ++i) {
      const auto& b = (*bones)[i];
      std::printf("%s\n  {\"name\": %s, \"parent\": %d, \"local\": %s, \"rotation\": %s}", i ? "," : "",
                  Json(b.name).c_str(), b.parent, V3(b.local).c_str(), Q4(b.rotation).c_str());
    }
    std::printf("\n]\n");
    return 0;
  }
  if (cmd == "skl" && pos.size() == 3) {
    auto bytes = vfs.Read(pos[1], &err);
    if (!bytes) return fail();
    auto fpk = pes::FpkArchive::Parse(*bytes, &err);
    if (!fpk) return fail();
    const std::string& sfx = pos[2];
    for (const auto& e : fpk->entries) {
      if (e.path.size() < sfx.size() || e.path.compare(e.path.size() - sfx.size(), sfx.size(), sfx) != 0) continue;
      auto bones = pes::ParseSkl(std::span(*bytes).subspan(e.offset, e.size), &err);
      if (!bones) return fail();
      std::printf("[");
      for (size_t i = 0; i < bones->size(); ++i) {
        const auto& b = (*bones)[i];
        std::printf("%s\n  {\"name\": %s, \"parent\": %d, \"position\": %s}", i ? "," : "", Json(b.name).c_str(),
                    b.parent, V3(b.Position()).c_str());
      }
      std::printf("\n]\n");
      return 0;
    }
    err = "no fpk entry ends with " + sfx;
    return fail();
  }
  if (cmd == "rig") {
    auto rig = pesrig::BuildRig(vfs, &err);
    if (!rig) return fail();
    std::printf("[");
    for (size_t i = 0; i < rig->bones.size(); ++i) {
      const auto& b = rig->bones[i];
      std::printf("%s\n  {\"gf\": %s, \"pes\": %s, \"parent\": %d, \"animLocal\": %s, \"animGlobal\": %s, "
                  "\"renderGlobal\": %s, \"align\": %s}",
                  i ? "," : "", Json(b.gfName).c_str(), Json(b.pesBone).c_str(), b.parent,
                  V3(b.animLocalPos).c_str(), V3(b.animGlobalPos).c_str(), V3(b.renderGlobalPos).c_str(),
                  Q4(b.align).c_str());
    }
    std::printf("\n]\n");
    return 0;
  }
  if (cmd == "ftex" && pos.size() == 2) {
    auto bytes = vfs.Read(pos[1], &err);
    if (!bytes) return fail();
    auto img = pes::ParseFtex(*bytes, &err);
    if (!img) return fail();
    std::printf("{\"pixelFormat\": %d, \"width\": %d, \"height\": %d, \"mips\": %d, \"cube\": %s, \"mip0Bytes\": %zu}\n",
                img->pixelFormat, img->width, img->height, img->mipCount, img->cube ? "true" : "false",
                img->mips.empty() ? size_t(0) : img->mips[0].size());
    return 0;
  }
  if (cmd == "fmdl" && pos.size() == 2) {
    auto bytes = vfs.Read(pos[1], &err);
    if (!bytes) return fail();
    auto fpk = pes::FpkArchive::Parse(*bytes, &err);
    if (!fpk) return fail();
    std::printf("[");
    bool firstModel = true;
    for (const auto& e : fpk->entries) {
      if (e.path.size() < 5 || e.path.compare(e.path.size() - 5, 5, ".fmdl") != 0) continue;
      auto m = pes::ParseFmdl(std::span(*bytes).subspan(e.offset, e.size), &err);
      std::printf("%s\n {\"file\": %s", firstModel ? "" : ",", Json(e.path).c_str());
      firstModel = false;
      if (!m) {
        std::printf(", \"error\": %s}", Json(err).c_str());
        continue;
      }
      std::printf(",\n  \"bones\": [");
      for (size_t i = 0; i < m->bones.size(); ++i)
        std::printf("%s%s", i ? ", " : "", Json(m->bones[i].name).c_str());
      std::printf("],\n  \"textures\": [");
      for (size_t i = 0; i < m->textures.size(); ++i)
        std::printf("%s\n    [%s, %s]", i ? "," : "", Json(m->textures[i].directory).c_str(),
                    Json(m->textures[i].filename).c_str());
      std::printf("],\n  \"materials\": [");
      for (size_t i = 0; i < m->materials.size(); ++i) {
        const auto& mat = m->materials[i];
        std::printf("%s\n    {\"name\": %s, \"shader\": %s, \"technique\": %s, \"textures\": {", i ? "," : "",
                    Json(mat.name).c_str(), Json(mat.shader).c_str(), Json(mat.technique).c_str());
        for (size_t t = 0; t < mat.textures.size(); ++t)
          std::printf("%s%s: %d", t ? ", " : "", Json(mat.textures[t].first).c_str(), mat.textures[t].second);
        std::printf("}}");
      }
      std::printf("],\n  \"meshes\": [");
      for (size_t i = 0; i < m->meshes.size(); ++i) {
        const auto& mesh = m->meshes[i];
        pes::Vec3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
        for (const auto& p : mesh.pos) {
          lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
          hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
        }
        std::printf("%s\n    {\"material\": %d, \"vertices\": %zu, \"indices\": %zu, \"boneGroup\": %zu, "
                    "\"normals\": %zu, \"tangents\": %zu, \"uv0\": %zu, \"weights\": %zu, \"min\": %s, \"max\": %s}",
                    i ? "," : "", mesh.materialIndex, mesh.pos.size(), mesh.indices.size(), mesh.boneGroup.size(),
                    mesh.nrm.size(), mesh.tan.size(), mesh.uv0.size(), mesh.weights.size(), V3(lo).c_str(),
                    V3(hi).c_str());
      }
      std::printf("]}");
    }
    std::printf("\n]\n");
    return 0;
  }
  return Usage();
}
