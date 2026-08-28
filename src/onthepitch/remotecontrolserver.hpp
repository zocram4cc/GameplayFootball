// The socket half of the remote-control channel: a localhost TCP listener a
// control panel connects to. Lines from clients become commands on a queue the
// match thread drains; a "state" line is answered with the last snapshot the
// match thread published. Started only when the config names a port, so a
// build with no panel attached carries no thread and no socket.

#ifndef _HPP_REMOTE_CONTROL_SERVER
#define _HPP_REMOTE_CONTROL_SERVER

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include "remotecontrol.hpp"

namespace RemoteControl {

class Server {
public:
  ~Server();

  // Binds 127.0.0.1:<port> and starts the accept/read thread. A non-empty
  // streamer key makes every connection authenticate with "auth <key>" before
  // anything else is accepted. Returns false (and logs) when the port cannot
  // be bound; the engine carries on as if no channel was asked for.
  bool Start(int port, const std::string& streamerKey);
  void Stop();

  CommandQueue& GetQueue() { return queue; }

  // The match thread publishes; the socket thread answers "state" with it.
  void PublishState(const std::string& json);

private:
  void Run();

  int listenFd = -1;
  std::string streamerKey;
  std::thread thread;
  std::atomic<bool> stopping{false};
  CommandQueue queue;
  std::mutex stateMutex;
  std::string stateJson;
};

}  // namespace RemoteControl

#endif
