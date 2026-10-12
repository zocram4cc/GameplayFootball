#include "pes/pack_rules.hpp"

#include <algorithm>
#include <cctype>
#include <regex>

namespace pes {
namespace {

constexpr const char* FACE_DIR = "Asset/model/character/face/real/";
constexpr const char* FACE_PACK_FILE = "/#Win/face.fpk";
constexpr const char* FACE_TEXTURE_DIR = "/sourceimages/#windx11/";
constexpr const char* FOX_ASSET_PREFIX = "/Assets/pes16/";  // as stored in fmdl texture paths
constexpr const char* VFS_ASSET_PREFIX = "Asset/";
constexpr const char* TEXTURE_PLATFORM_DIR = "#windx11/";
constexpr const char* TEXTURE_EXT = ".ftex";
constexpr const char* CHARACTER_ROOT = "Asset/model/character/";

const char* const NON_RENDER_PASSES[] = {"antiblur", "outline"};
constexpr const char* PLACEHOLDER_PREFIX = "dummy";
constexpr const char* KIT_SLOT_TEXTURE = "dummy_kit";
const std::regex BARE_KIT_SLOT(R"(^u0\w{3}p0$)", std::regex::icase);  // u0<team>p0, team "XXX" in 4cc packs
const char* const EFFECT_TEXTURES[] = {"aura", "effect", "fx_", "smoke", "flare"};

std::string Lower(std::string s) {
  for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

bool EndsWith(const std::string& s, const std::string& tail) {
  return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

}  // namespace

std::string FacePackPath(int faceId) { return FACE_DIR + std::to_string(faceId) + FACE_PACK_FILE; }
std::string FaceTextureDir(int faceId) { return FACE_DIR + std::to_string(faceId) + FACE_TEXTURE_DIR; }

std::string TextureStem(const std::string& file) {
  const size_t slash = file.find_last_of("/\\");
  std::string base = slash == std::string::npos ? file : file.substr(slash + 1);
  const size_t dot = base.find_last_of('.');
  return dot == std::string::npos ? base : base.substr(0, dot);
}

bool IsNonRenderPass(const std::string& material, const std::string& baseTextureStem) {
  for (const std::string& field : {material, baseTextureStem}) {
    std::string word;
    for (char c : Lower(field) + " ") {
      if (std::isalnum(static_cast<unsigned char>(c))) {
        word += c;
        continue;
      }
      for (const char* pass : NON_RENDER_PASSES)
        if (word == pass) return true;
      word.clear();
    }
  }
  return false;
}

bool IsEffectTexture(const std::string& stem) {
  const std::string s = Lower(stem);
  return std::any_of(std::begin(EFFECT_TEXTURES), std::end(EFFECT_TEXTURES),
                     [&](const char* m) { return s.find(m) != std::string::npos; });
}

bool IsKitSlotTexture(const std::string& stem) {
  return Lower(stem) == KIT_SLOT_TEXTURE || std::regex_match(stem, BARE_KIT_SLOT);
}

bool IsPlaceholderTexture(const std::string& stem) { return Lower(stem).rfind(PLACEHOLDER_PREFIX, 0) == 0; }

std::string TextureResolver::Resolve(const FmdlTexture& texture) {
  const std::string stem = TextureStem(texture.filename);
  if (texture.directory.rfind(FOX_ASSET_PREFIX, 0) == 0) {
    const std::string p = VFS_ASSET_PREFIX + texture.directory.substr(std::string(FOX_ASSET_PREFIX).size()) +
                          TEXTURE_PLATFORM_DIR + stem + TEXTURE_EXT;
    if (vfs_.Exists(p)) return p;
  }
  const std::string local = FaceTextureDir(faceId_) + stem + TEXTURE_EXT;
  if (vfs_.Exists(local)) return local;
  if (characterFiles_.empty()) characterFiles_ = vfs_.List(CHARACTER_ROOT);
  const std::string want = Lower(stem);
  for (const auto& p : characterFiles_)
    if (EndsWith(Lower(p), TEXTURE_EXT) && Lower(TextureStem(p)) == want) return p;
  return {};
}

}  // namespace pes
