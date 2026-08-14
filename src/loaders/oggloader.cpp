#include "oggloader.hpp"

#include <cstring>
#include <vector>

#include "base/log.hpp"
#include "oggdecoder.hpp"

namespace blunted {

OGGLoader::OGGLoader() : Loader<SoundBuffer>() {}

OGGLoader::~OGGLoader() {}

void OGGLoader::Load(const std::string& filename,
                     boost::intrusive_ptr<Resource<SoundBuffer>> resource) {
  int channels = 0;
  unsigned int frequency = 0;
  std::vector<unsigned char> pcm;
  if (!DecodeOggFile(filename, channels, frequency, pcm)) {
    Log(e_FatalError, "OGGLoader", "Load",
        "Could not load " + filename + ": not a decodable ogg vorbis file");
    return;
  }

  WavData* data = new WavData();
  data->data = new unsigned char[pcm.size()];
  memcpy(data->data, pcm.data(), pcm.size());
  data->size = (int)pcm.size();
  data->channels = channels;
  data->bits = 16;
  data->frequency = frequency;

  resource->GetResource()->SetData(data);
}

}  // namespace blunted
