#include "mp3loader.hpp"

#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO  // file IO goes through our own read below
#include "dr_mp3.h"

#include <cstdio>
#include <vector>

#include "base/log.hpp"

namespace blunted {

MP3Loader::MP3Loader() : Loader<SoundBuffer>() {}

MP3Loader::~MP3Loader() {}

void MP3Loader::Load(const std::string& filename,
                     boost::intrusive_ptr<Resource<SoundBuffer>> resource) {
  // Community exports are untrusted input: a bad file must not take the
  // match down (the WAV/OGG loaders may FatalError - their inputs are
  // shipped assets). Decode failures log and yield a moment of silence.
  auto fail = [&](const std::string& why) {
    Log(e_Error, "MP3Loader", "Load", "Could not load " + filename + ": " + why);
    WavData* silent = new WavData();
    silent->channels = 1;
    silent->bits = 16;
    silent->frequency = 44100;
    silent->size = 4410 * 2;  // a tenth of a second, comfortably real for AL
    silent->data = new unsigned char[silent->size]();
    resource->GetResource()->SetData(silent);
  };

  // Read the whole file ourselves (fopen keeps UTF-8 paths working the same
  // way the other loaders do on every platform).
  FILE* f = fopen(filename.c_str(), "rb");
  if (!f) {
    fail("file not found");
    return;
  }
  fseek(f, 0, SEEK_END);
  const long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  std::vector<unsigned char> bytes(size > 0 ? (size_t)size : 0);
  const size_t got = bytes.empty() ? 0 : fread(bytes.data(), 1, bytes.size(), f);
  fclose(f);

  drmp3_config config;
  drmp3_uint64 frameCount = 0;
  drmp3_int16* pcm = drmp3_open_memory_and_read_pcm_frames_s16(
      bytes.data(), got, &config, &frameCount, nullptr);
  if (!pcm || frameCount == 0 || config.channels == 0) {
    if (pcm) drmp3_free(pcm, nullptr);
    fail("not a decodable mp3 file");
    return;
  }
  WavData* data = new WavData();
  data->size = (int)(frameCount * config.channels * sizeof(drmp3_int16));
  data->data = new unsigned char[data->size];
  memcpy(data->data, pcm, data->size);
  data->channels = (int)config.channels;
  data->bits = 16;
  data->frequency = config.sampleRate;
  drmp3_free(pcm, nullptr);

  resource->GetResource()->SetData(data);
}

}  // namespace blunted
