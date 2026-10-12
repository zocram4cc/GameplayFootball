#pragma once

#include <godot_cpp/classes/image.hpp>
#include <string>

#include "pes/vfs.hpp"

namespace godot {

// Decodes a PES .ftex from the VFS into a Godot Image (GPU block formats kept
// compressed). Returns null with a reason on failure; cube maps are refused.
Ref<Image> LoadPesImage(const pes::PesVfs& vfs, const std::string& path, std::string* error);

}  // namespace godot
