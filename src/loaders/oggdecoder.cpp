#include "oggdecoder.hpp"

#include <cstring>

#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.c"

namespace blunted {

bool DecodeOggFile(const std::string& filename, int& channels,
                   unsigned int& frequency, std::vector<unsigned char>& pcm) {
  int decodedChannels = 0;
  int sampleRate = 0;
  short* samples = nullptr;
  int frameCount = stb_vorbis_decode_filename(filename.c_str(),
                                              &decodedChannels, &sampleRate,
                                              &samples);
  if (frameCount <= 0 || samples == nullptr) {
    free(samples);
    return false;
  }

  size_t byteCount = (size_t)frameCount * decodedChannels * sizeof(short);
  pcm.resize(byteCount);
  memcpy(pcm.data(), samples, byteCount);
  free(samples);

  channels = decodedChannels;
  frequency = (unsigned int)sampleRate;
  return true;
}

}  // namespace blunted
