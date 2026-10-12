#pragma once

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/texture.hpp>
#include <string>
#include <vector>

#include "pes/vfs.hpp"

namespace godot {

enum class PesTextureKind { Flat, Cube, Array };

// Decodes a PES .ftex into one Godot Image per ftex image (GPU block formats
// kept compressed): 2D one, cube 6 faces, array one per slice.
std::vector<Ref<Image>> LoadPesImages(const pes::PesVfs& vfs, const std::string& path, std::string* error,
                                      PesTextureKind* kind);
// A 2D ftex as one Image; null with a reason for layered textures or failures.
Ref<Image> LoadPesImage(const pes::PesVfs& vfs, const std::string& path, std::string* error);
// The matching Godot texture: ImageTexture, Cubemap or Texture2DArray.
Ref<Texture> LoadPesTexture(const pes::PesVfs& vfs, const std::string& path, std::string* error);

}  // namespace godot
