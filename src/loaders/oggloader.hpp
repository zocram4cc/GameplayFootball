// Ogg Vorbis loader: fills a SoundBuffer with 16-bit PCM, the same shape the
// WAV loader produces, so imported pack audio (data/imports/*/chants/*.ogg)
// plays through the existing OpenAL path.

#ifndef _HPP_LOADERS_OGG
#define _HPP_LOADERS_OGG

#include "defines.hpp"
#include "managers/resourcemanager.hpp"
#include "scene/objects/sound.hpp"
#include "scene/resources/soundbuffer.hpp"

namespace blunted {

class OGGLoader : public Loader<SoundBuffer> {
public:
  OGGLoader();
  virtual ~OGGLoader();

  virtual void Load(const std::string& filename,
                    boost::intrusive_ptr<Resource<SoundBuffer>> resource);
};

}  // namespace blunted

#endif
