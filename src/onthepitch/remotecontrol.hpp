// The remote-control protocol an attached control panel speaks to a running
// match: newline-delimited text commands in, one line of JSON state back.
//
// This header is the pure half - parsing, the property/state writes a command
// turns into, the queue that carries commands from the socket thread to the
// match thread, and the snapshot the panel reads. No sockets and no Match in
// here; the socket lives in remotecontrolserver.hpp and the glue that touches
// Match lives in match.cpp. The channel is an optional attachment: nothing in
// the engine depends on it existing.

#ifndef _HPP_REMOTE_CONTROL
#define _HPP_REMOTE_CONTROL

#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "base/properties.hpp"
#include "teaminstructions.hpp"

namespace RemoteControl {

enum e_CommandType {
  e_CommandType_None = 0,
  e_CommandType_Tactic,        // tactic <side> <name> <value 0..1>
  e_CommandType_Philosophy,    // philosophy <side> <name>
  e_CommandType_Mentality,     // mentality <side> <name>
  e_CommandType_Instruction,   // instruction <side> <name> on|off
  e_CommandType_Substitution,  // sub <side> <playerOutId> <playerInId>
  e_CommandType_State,         // state
  e_CommandType_Auth,          // auth <streamer key>
  e_CommandType_Schedule,      // schedule <t1> <t2> <mins> <kit1> <kit2> <stadium…>
  e_CommandType_Resume,        // resume: release a half-time / extra-time hold
};

// What a schedule command asks for: the same choices the quick-match menu
// makes, delivered over the wire instead.
struct Schedule {
  int team1Id = 0;
  int team2Id = 0;
  float durationMinutes = 0.0f;
  int team1KitNum = 1;
  int team2KitNum = 1;
  std::string stadiumObject;
};

struct Command {
  e_CommandType type = e_CommandType_None;
  int side = 0;  // 0 home, 1 away
  std::string name;
  float value = 0.0f;
  bool enable = false;
  int playerOutId = 0;
  int playerInId = 0;
  Schedule schedule;
};

// One protocol line into a command. Anything malformed is refused and cmd is
// left as e_CommandType_None; a remote peer never gets to crash a match.
bool ParseLine(const std::string& line, Command& cmd);

// A display name ("All-Out Attack") as it appears on the wire
// ("all_out_attack"). Matching is case- and separator-insensitive, so the
// panel may also send the display spelling.
std::string WireName(const std::string& displayName);

// The writes a command turns into - the exact writes the game-plan menu makes,
// so the recompose in TeamAIController::UpdateTactics picks them up unchanged.
// Each returns false when the command names something the engine doesn't know.
bool ApplyTactic(const Command& cmd, blunted::Properties& userProperties);
bool ApplyPhilosophy(const Command& cmd, blunted::Properties& userProperties);
bool ApplyInstruction(const Command& cmd, TeamInstructions::State& state);
bool ApplyMentality(const Command& cmd, TeamInstructions::State& state);

// Whether a keyed channel lets a line through. A channel with no key is open
// (the mode was entered without one); a keyed channel refuses everything from
// a connection until it authenticates with the streamer key.
enum e_GateResult {
  e_GateResult_Refuse = 0,
  e_GateResult_Authed,
  e_GateResult_Pass,
};
e_GateResult GateLine(const std::string& requiredKey, bool authed, const Command& cmd);

// Writes a schedule into the live configuration: the launch keys the
// self-driving menu path reads on its way into a match.
void ApplySchedule(const Command& cmd, blunted::Properties& config);

// Commands cross from the socket thread to the match thread through here.
class CommandQueue {
public:
  void Push(const Command& cmd);
  std::vector<Command> Drain();

private:
  std::mutex mutex;
  std::vector<Command> commands;
};

// Reassembles the byte chunks a TCP read produces into protocol lines.
// A peer that never sends a newline is capped, not accumulated.
class LineBuffer {
public:
  std::vector<std::string> Append(const char* data, size_t length);

private:
  std::string pending;
  bool discarding = false;
};

// ── State read-back ─────────────────────────────────────────────────────────

struct PlayerState {
  int id = 0;
  std::string name;
  std::string role;
  bool onPitch = false;
};

struct TeamState {
  std::string name;
  std::string philosophy;
  std::string mentality;
  std::vector<std::string> instructions;  // wire names of the set bits
  std::vector<std::pair<std::string, float>> tactics;  // live slider values
  std::vector<PlayerState> players;
};

struct Snapshot {
  int score[2] = {0, 0};
  unsigned long matchTime_ms = 0;
  int phase = 0;  // e_MatchPhase
  bool inPlay = false;
  bool substitutionWindow = false;
  // A half-time / extra-time hold is on; play resumes when the streamer says so.
  bool holding = false;
  TeamState teams[2];
};

// One line of JSON, so the socket reader can frame it by newline.
std::string ToJson(const Snapshot& snapshot);

}  // namespace RemoteControl

#endif
