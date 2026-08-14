// Ogg Vorbis decoder tests: the pes21 import pack stores audio as .ogg,
// which the engine must decode into the same PCM WavData shape WAV files use.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

#include "loaders/oggdecoder.hpp"

namespace {

std::string FixturePath() {
  const char* dir = std::getenv("GF_TEST_DATA_DIR");
  return std::string(dir ? dir : "tests/data") + "/sine440.ogg";
}

TEST(OggDecoder, DecodesSineFixtureToPcm16) {
  int channels = 0;
  unsigned int frequency = 0;
  std::vector<unsigned char> pcm;
  ASSERT_TRUE(blunted::DecodeOggFile(FixturePath(), channels, frequency, pcm));

  EXPECT_EQ(channels, 1);
  EXPECT_EQ(frequency, 44100u);
  // 0.2 seconds of mono 16-bit at 44.1kHz = ~17640 bytes
  EXPECT_GT(pcm.size(), 10000u);
  EXPECT_EQ(pcm.size() % 2, 0u);

  // a 440Hz sine is loud: RMS well above silence
  const short* samples = reinterpret_cast<const short*>(pcm.data());
  size_t count = pcm.size() / 2;
  double sum = 0.0;
  for (size_t i = 0; i < count; i++) sum += (double)samples[i] * samples[i];
  double rms = std::sqrt(sum / count);
  EXPECT_GT(rms, 1000.0);
}

TEST(OggDecoder, RejectsMissingFile) {
  int channels = 0;
  unsigned int frequency = 0;
  std::vector<unsigned char> pcm;
  EXPECT_FALSE(blunted::DecodeOggFile("/nonexistent/nothing.ogg",
                                      channels, frequency, pcm));
}

TEST(OggDecoder, RejectsNonOggFile) {
  int channels = 0;
  unsigned int frequency = 0;
  std::vector<unsigned char> pcm;
  // this source file itself is definitely not an ogg
  EXPECT_FALSE(blunted::DecodeOggFile(__FILE__, channels, frequency, pcm));
}

}  // namespace
