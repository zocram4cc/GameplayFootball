// Inspects a PES 2021 install through the native readers; JSON on stdout.
//   pesfmt_dump mounts --pes21-root DIR
//   pesfmt_dump ls <prefix> --pes21-root DIR
//   pesfmt_dump cat <path> --pes21-root DIR > file   (dev only; never into the repo)
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "pes/vfs.hpp"

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

int Usage() {
  std::fprintf(stderr, "usage: pesfmt_dump <mounts|ls PREFIX|cat PATH> --pes21-root DIR\n");
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
  return Usage();
}
