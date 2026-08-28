#include "remotecontrolmode.hpp"

#include <atomic>
#include <memory>

#include "base/log.hpp"
#include "main.hpp"
#include "onthepitch/remotecontrolserver.hpp"

using namespace blunted;

namespace RemoteControlMode {

namespace {

std::unique_ptr<RemoteControl::Server> server;
std::atomic<bool> holding{false};
std::atomic<bool> resumeRequested{false};

}  // namespace

bool Enter(const std::string& streamerKey) {
  if (server) return true;
  const int port = GetConfiguration()->GetInt("remote_control_port", 44700);
  auto attempt = std::make_unique<RemoteControl::Server>();
  if (!attempt->Start(port, streamerKey)) return false;
  server = std::move(attempt);
  Log(e_Notice, "RemoteControlMode", "Enter", "remote control mode active");
  return true;
}

void Leave() {
  if (!server) return;
  server.reset();
  holding = false;
  resumeRequested = false;
  Log(e_Notice, "RemoteControlMode", "Leave", "remote control mode left");
}

bool IsActive() {
  return server != nullptr;
}

RemoteControl::Server* GetServer() {
  return server.get();
}

void SetHolding(bool newHolding) {
  holding = newHolding;
}

bool IsHolding() {
  return holding;
}

void RequestResume() {
  resumeRequested = true;
}

bool ConsumeResumeRequest() {
  return resumeRequested.exchange(false);
}

}  // namespace RemoteControlMode
