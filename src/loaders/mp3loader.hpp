// MP3 loader: fills a SoundBuffer with 16-bit PCM, the same shape the WAV
// and OGG loaders produce, so rigdio music exports (data/music/*/*.mp3,
// docs/RIGDIO.md) play through the existing OpenAL path. Decoding is
// dr_mp3.h (public domain, single header), following the stb_vorbis
// precedent - no new link-time dependency on any platform.

#ifndef _HPP_LOADERS_MP3
#define _HPP_LOADERS_MP3

#include "defines.hpp"
#include "managers/resourcemanager.hpp"
#include "scene/objects/sound.hpp"
#include "scene/resources/soundbuffer.hpp"

namespace blunted {

class MP3Loader : public Loader<SoundBuffer> {
public:
  MP3Loader();
  virtual ~MP3Loader();

  virtual void Load(const std::string& filename,
                    boost::intrusive_ptr<Resource<SoundBuffer>> resource);
};

}  // namespace blunted

#endif
