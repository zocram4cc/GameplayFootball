#include "remotecontrol.hpp"

#include <cctype>
#include <cstdlib>
#include <sstream>

#include "teamphilosophy.hpp"

namespace RemoteControl {

namespace {

// Case- and separator-insensitive comparison key, in the manner of
// TeamPhilosophy::Parse: only letters and digits, lowercased.
std::string MatchKey(const std::string& name) {
  std::string key;
  key.reserve(name.size());
  for (char c : name) {
    if (std::isalnum(static_cast<unsigned char>(c)))
      key += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return key;
}

bool ParseFloat(const std::string& token, float& value) {
  char* end = nullptr;
  value = std::strtof(token.c_str(), &end);
  return end != token.c_str() && *end == '\0';
}

bool ParseInt(const std::string& token, int& value) {
  char* end = nullptr;
  value = static_cast<int>(std::strtol(token.c_str(), &end, 10));
  return end != token.c_str() && *end == '\0';
}

bool ParseSide(const std::string& token, int& side) {
  return ParseInt(token, side) && (side == 0 || side == 1);
}

// JSON string escaping - quotes, backslashes and control characters. Names
// come out of a user-edited database, so nothing about them is trusted.
void AppendEscaped(std::string& out, const std::string& value) {
  for (char c : value) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += c;
        }
    }
  }
}

std::string JsonNumber(float value) {
  std::ostringstream stream;
  stream << value;
  return stream.str();
}

void AppendTeam(std::string& out, const TeamState& team) {
  out += "{\"name\":\"";
  AppendEscaped(out, team.name);
  out += "\",\"philosophy\":\"";
  AppendEscaped(out, team.philosophy);
  out += "\",\"mentality\":\"";
  AppendEscaped(out, team.mentality);
  out += "\",\"instructions\":[";
  for (size_t i = 0; i < team.instructions.size(); i++) {
    if (i) out += ',';
    out += '"';
    AppendEscaped(out, team.instructions[i]);
    out += '"';
  }
  out += "],\"tactics\":{";
  for (size_t i = 0; i < team.tactics.size(); i++) {
    if (i) out += ',';
    out += '"';
    AppendEscaped(out, team.tactics[i].first);
    out += "\":" + JsonNumber(team.tactics[i].second);
  }
  out += "},\"players\":[";
  for (size_t i = 0; i < team.players.size(); i++) {
    const PlayerState& player = team.players[i];
    if (i) out += ',';
    out += "{\"id\":" + std::to_string(player.id) + ",\"name\":\"";
    AppendEscaped(out, player.name);
    out += "\",\"role\":\"";
    AppendEscaped(out, player.role);
    out += "\",\"on_pitch\":";
    out += player.onPitch ? "true" : "false";
    out += '}';
  }
  out += "]}";
}

}  // namespace

bool ParseLine(const std::string& line, Command& cmd) {
  cmd = Command();

  std::istringstream stream(line);
  std::vector<std::string> tokens;
  std::string token;
  while (stream >> token) tokens.push_back(token);
  if (tokens.empty()) return false;

  const std::string& verb = tokens[0];

  if (verb == "state" && tokens.size() == 1) {
    cmd.type = e_CommandType_State;
    return true;
  }

  if (verb == "tactic" && tokens.size() == 4) {
    float value;
    if (!ParseSide(tokens[1], cmd.side) || !ParseFloat(tokens[3], value)) return false;
    cmd.type = e_CommandType_Tactic;
    cmd.name = tokens[2];
    cmd.value = std::max(0.0f, std::min(1.0f, value));
    return true;
  }

  if (verb == "philosophy" && tokens.size() == 3) {
    if (!ParseSide(tokens[1], cmd.side)) return false;
    cmd.type = e_CommandType_Philosophy;
    cmd.name = tokens[2];
    return true;
  }

  if (verb == "mentality" && tokens.size() == 3) {
    if (!ParseSide(tokens[1], cmd.side)) return false;
    cmd.type = e_CommandType_Mentality;
    cmd.name = tokens[2];
    return true;
  }

  if (verb == "instruction" && tokens.size() == 4) {
    if (!ParseSide(tokens[1], cmd.side)) return false;
    if (tokens[3] != "on" && tokens[3] != "off") return false;
    cmd.type = e_CommandType_Instruction;
    cmd.name = tokens[2];
    cmd.enable = tokens[3] == "on";
    return true;
  }

  if (verb == "sub" && tokens.size() == 4) {
    if (!ParseSide(tokens[1], cmd.side)) return false;
    if (!ParseInt(tokens[2], cmd.playerOutId) || !ParseInt(tokens[3], cmd.playerInId)) {
      cmd = Command();
      return false;
    }
    cmd.type = e_CommandType_Substitution;
    return true;
  }

  cmd = Command();
  return false;
}

std::string WireName(const std::string& displayName) {
  std::string wire;
  wire.reserve(displayName.size());
  bool pendingSeparator = false;
  for (char c : displayName) {
    if (std::isalnum(static_cast<unsigned char>(c))) {
      if (pendingSeparator && !wire.empty()) wire += '_';
      pendingSeparator = false;
      wire += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    } else {
      pendingSeparator = true;
    }
  }
  return wire;
}

bool ApplyTactic(const Command& cmd, blunted::Properties& userProperties) {
  // Keys with an editor of their own (philosophy, mentality, instructions,
  // formation) are not sliders; a numeric write would corrupt them.
  if (!TeamPhilosophy::IsSliderTactic(cmd.name)) return false;
  userProperties.Set(cmd.name.c_str(), cmd.value);
  return true;
}

bool ApplyPhilosophy(const Command& cmd, blunted::Properties& userProperties) {
  const TeamPhilosophy::e_Philosophy philosophy = TeamPhilosophy::Parse(cmd.name);
  // Parse yields Balanced for anything it does not know; only accept that
  // answer when balanced is what was actually asked for.
  if (philosophy == TeamPhilosophy::e_Philosophy_Balanced &&
      MatchKey(cmd.name) != MatchKey(TeamPhilosophy::GetName(philosophy)))
    return false;
  userProperties.Set("philosophy", TeamPhilosophy::GetName(philosophy));
  return true;
}

bool ApplyInstruction(const Command& cmd, TeamInstructions::State& state) {
  const std::string key = MatchKey(cmd.name);
  for (int i = 0; i < TeamInstructions::instructionCount; i++) {
    const TeamInstructions::e_Instruction instruction = TeamInstructions::GetInstructionAt(i);
    if (key == MatchKey(TeamInstructions::GetInstructionName(instruction))) {
      if (TeamInstructions::Has(state, instruction) != cmd.enable)
        TeamInstructions::Toggle(state, instruction);
      return true;
    }
  }
  return false;
}

bool ApplyMentality(const Command& cmd, TeamInstructions::State& state) {
  const std::string key = MatchKey(cmd.name);
  for (int i = 0; i < TeamInstructions::e_Mentality_Count; i++) {
    const TeamInstructions::e_Mentality mentality = static_cast<TeamInstructions::e_Mentality>(i);
    if (key == MatchKey(TeamInstructions::GetMentalityName(mentality))) {
      state.mentality = mentality;
      return true;
    }
  }
  return false;
}

void CommandQueue::Push(const Command& cmd) {
  std::lock_guard<std::mutex> lock(mutex);
  commands.push_back(cmd);
}

std::vector<Command> CommandQueue::Drain() {
  std::lock_guard<std::mutex> lock(mutex);
  std::vector<Command> drained;
  drained.swap(commands);
  return drained;
}

std::string ToJson(const Snapshot& snapshot) {
  std::string out;
  out.reserve(4096);
  out += "{\"score\":[" + std::to_string(snapshot.score[0]) + ',' +
         std::to_string(snapshot.score[1]) + "],\"time_ms\":" +
         std::to_string(snapshot.matchTime_ms) + ",\"phase\":" + std::to_string(snapshot.phase) +
         ",\"in_play\":";
  out += snapshot.inPlay ? "true" : "false";
  out += ",\"sub_window\":";
  out += snapshot.substitutionWindow ? "true" : "false";
  out += ",\"teams\":[";
  AppendTeam(out, snapshot.teams[0]);
  out += ',';
  AppendTeam(out, snapshot.teams[1]);
  out += "]}";
  return out;
}

}  // namespace RemoteControl
