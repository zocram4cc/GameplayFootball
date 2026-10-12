// written by bastiaan konings schuiling 2008 - 2014
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#ifndef _HPP_RESOURCE
#define _HPP_RESOURCE

#include <mutex>

#include "defines.hpp"
#include "types/refcounted.hpp"

namespace blunted {

template <typename T>
class Resource : public RefCounted {
public:
  Resource(const std::string& identString) : resource(0), identString(identString) { resource = new T(); }

  virtual ~Resource() {
    delete resource;
    resource = 0;
  }

  Resource(const Resource& src, const std::string& identString) : identString(identString) {
    // src.resourceMutex.lock(); terribly slow, why? (note: moved to resourcemanager's fetchcopy)
    this->resource = new T(*src.resource);
    // src.resourceMutex.unlock();
  }

  // todo: LoadStream / SaveStream

  T* GetResource() { return resource; }

  std::string GetIdentString() { return identString; }

  mutable std::mutex resourceMutex;

  T* resource;

protected:
  const std::string identString;
};

}  // namespace blunted

#endif
