// Ogg Vorbis -> 16-bit PCM decoding (stb_vorbis), dependency-free so the
// pes21 import pack's .ogg audio can be tested and loaded like WAV data.

#ifndef _HPP_LOADERS_OGGDECODER
#define _HPP_LOADERS_OGGDECODER

#include <string>
#include <vector>

namespace blunted {

// Decodes a whole Ogg Vorbis file into interleaved 16-bit little-endian PCM.
// Returns false (leaving outputs untouched) if the file is missing or not a
// valid ogg stream.
bool DecodeOggFile(const std::string& filename, int& channels,
                   unsigned int& frequency, std::vector<unsigned char>& pcm);

}  // namespace blunted

#endif
