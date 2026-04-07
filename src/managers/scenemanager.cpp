// written by bastiaan konings schuiling 2008 - 2014
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#include "scenemanager.hpp"

#include "base/log.hpp"
#include "managers/systemmanager.hpp"

namespace blunted {

template <>
SceneManager* Singleton<SceneManager>::singleton = nullptr;

SceneManager::SceneManager() {}

SceneManager::~SceneManager() {}

void SceneManager::Exit() {
  std::unique_lock<std::mutex> lock(scenes.mutex);
  for (auto& scene : scenes.data) {
    scene->Exit();
  }
  scenes.data.clear();
}

void SceneManager::RegisterScene(std::shared_ptr<IScene> scene) {
  {
    std::lock_guard<std::mutex> lock(scenes.mutex);
    scenes.data.push_back(scene);
  }
  SystemManager::GetInstance().CreateSystemScenes(scene);
}

int SceneManager::GetNumScenes() {
  std::unique_lock<std::mutex> lock(scenes.mutex);
  return static_cast<int>(scenes.data.size());
}

std::shared_ptr<IScene> SceneManager::GetScene(int index, bool& success) {
  std::unique_lock<std::mutex> lock(scenes.mutex);
  if (index >= 0 && index < static_cast<int>(scenes.data.size())) {
    success = true;
    return scenes.data.at(index);
  } else {
    success = false;
    return nullptr;
  }
}

std::shared_ptr<IScene> SceneManager::GetScene(const std::string& name, bool& success) {
  std::unique_lock<std::mutex> lock(scenes.mutex);
  for (auto& scene : scenes.data) {
    if (scene->GetName() == name) {
      success = true;
      return scene;
    }
  }
  success = false;
  return nullptr;
}

  scenes.data.clear();
}

void SceneManager::RegisterScene(std::shared_ptr<IScene> scene) {
  scenes.Lock();
  scenes.data.push_back(scene);
  scenes.Unlock();
  SystemManager::GetInstance().CreateSystemScenes(scene);
}

int SceneManager::GetNumScenes() {
  std::unique_lock<std::mutex> blah(scenes.mutex);
  return scenes.data.size();
}

std::shared_ptr<IScene> SceneManager::GetScene(int index, bool& success) {
  std::unique_lock<std::mutex> blah(scenes.mutex);
  if ((signed int)scenes.data.size() > index) {
    success = true;
    return scenes.data.at(index);
  } else {
    success = false;
    return std::shared_ptr<IScene>();
  }
}

std::shared_ptr<IScene> SceneManager::GetScene(const std::string& name, bool& success) {
  std::unique_lock<std::mutex> blah(scenes.mutex);
  for (int i = 0; i < (signed int)scenes.data.size(); i++) {
    if (scenes.data.at(i)->GetName() == name) {
      success = true;
      return scenes.data.at(i);
    }
  }
  success = false;
  return std::shared_ptr<IScene>();
}

}  // namespace blunted
