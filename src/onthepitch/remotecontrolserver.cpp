#include "remotecontrolserver.hpp"

#ifdef WIN32
// The control channel is a Linux-rig feature; the game itself never needs it.
namespace RemoteControl {
Server::~Server() {}
bool Server::Start(int, const std::string&) {
  return false;
}
void Server::Stop() {}
void Server::PublishState(const std::string&) {}
void Server::Run() {}
}  // namespace RemoteControl
#else

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <vector>

#include "base/log.hpp"
#include "base/utils.hpp"

using namespace blunted;

namespace RemoteControl {

namespace {

struct Client {
  int fd;
  LineBuffer buffer;
  bool drop;
  bool authed;
};

}  // namespace

Server::~Server() {
  Stop();
}

bool Server::Start(int port, const std::string& key) {
  streamerKey = key;
  listenFd = socket(AF_INET, SOCK_STREAM, 0);
  if (listenFd < 0) {
    Log(e_Warning, "RemoteControl", "Start", "socket() failed: " + std::string(strerror(errno)));
    return false;
  }

  int reuse = 1;
  setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  sockaddr_in address;
  std::memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // localhost only, never the network
  address.sin_port = htons(static_cast<uint16_t>(port));

  if (bind(listenFd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 ||
      listen(listenFd, 4) < 0) {
    Log(e_Warning, "RemoteControl", "Start",
        "could not listen on 127.0.0.1:" + int_to_str(port) + ": " +
            std::string(strerror(errno)));
    close(listenFd);
    listenFd = -1;
    return false;
  }

  stopping = false;
  thread = std::thread(&Server::Run, this);
  Log(e_Notice, "RemoteControl", "Start", "listening on 127.0.0.1:" + int_to_str(port));
  return true;
}

void Server::Stop() {
  if (listenFd < 0) return;
  stopping = true;
  if (thread.joinable()) thread.join();
  close(listenFd);
  listenFd = -1;
}

void Server::PublishState(const std::string& json) {
  std::lock_guard<std::mutex> lock(stateMutex);
  stateJson = json;
}

void Server::Run() {
  std::vector<Client> clients;

  while (!stopping) {
    std::vector<pollfd> fds;
    fds.push_back({listenFd, POLLIN, 0});
    for (const Client& client : clients) fds.push_back({client.fd, POLLIN, 0});

    // A finite timeout keeps the stop flag honoured even when nobody talks.
    const int ready = poll(fds.data(), fds.size(), 200);
    if (ready <= 0) continue;

    // clients and fds must stay pairwise (clients[i] <-> fds[i + 1]), so the
    // scan neither accepts into nor erases from `clients` while it runs: drops
    // are marked and erased afterwards, an accepted client is appended last.
    for (size_t i = 0; i < clients.size(); i++) {
      Client& client = clients[i];
      const pollfd& pfd = fds[i + 1];
      client.drop = (pfd.revents & (POLLERR | POLLHUP)) != 0;

      if (!client.drop && (pfd.revents & POLLIN)) {
        char chunk[1024];
        const ssize_t got = recv(client.fd, chunk, sizeof(chunk), 0);
        if (got <= 0) {
          client.drop = true;
        } else {
          for (const std::string& line : client.buffer.Append(chunk, got)) {
            Command cmd;
            if (!ParseLine(line, cmd)) {
              Log(e_Warning, "RemoteControl", "Run", "refused line: " + line);
              continue;
            }
            const e_GateResult gate = GateLine(streamerKey, client.authed, cmd);
            if (gate == e_GateResult_Refuse) {
              Log(e_Warning, "RemoteControl", "Run",
                  client.authed ? "refused line: " + line : "refused line before auth");
              // "err auth" strictly answers a failed handshake; anything else
              // refused gets its own word, so the panel's handshake state
              // machine never confuses the two.
              const bool authAttempt = cmd.type == e_CommandType_Auth;
              const char refusedAuth[] = "err auth\n";
              const char refused[] = "err refused\n";
              if (authAttempt)
                send(client.fd, refusedAuth, sizeof(refusedAuth) - 1, MSG_DONTWAIT | MSG_NOSIGNAL);
              else
                send(client.fd, refused, sizeof(refused) - 1, MSG_DONTWAIT | MSG_NOSIGNAL);
              continue;
            }
            if (gate == e_GateResult_Authed) {
              client.authed = true;
              Log(e_Notice, "RemoteControl", "Run", "panel authenticated");
              const char ok[] = "ok auth\n";
              send(client.fd, ok, sizeof(ok) - 1, MSG_DONTWAIT | MSG_NOSIGNAL);
              continue;
            }
            if (cmd.type == e_CommandType_State) {
              std::string reply;
              {
                std::lock_guard<std::mutex> lock(stateMutex);
                reply = stateJson.empty() ? "{}" : stateJson;
              }
              reply += '\n';
              // A slow reader loses its reply rather than stalling the match.
              send(client.fd, reply.data(), reply.size(), MSG_DONTWAIT | MSG_NOSIGNAL);
            } else {
              queue.Push(cmd);
            }
          }
        }
      }
    }

    for (size_t i = clients.size(); i-- > 0;) {
      if (!clients[i].drop) continue;
      close(clients[i].fd);
      clients.erase(clients.begin() + i);
      Log(e_Notice, "RemoteControl", "Run", "panel disconnected");
    }

    if (fds[0].revents & POLLIN) {
      const int clientFd = accept(listenFd, nullptr, nullptr);
      if (clientFd >= 0) {
        clients.push_back({clientFd, LineBuffer(), false, false});
        Log(e_Notice, "RemoteControl", "Run", "panel connected");
      }
    }
  }

  for (const Client& client : clients) close(client.fd);
}

}  // namespace RemoteControl

#endif
