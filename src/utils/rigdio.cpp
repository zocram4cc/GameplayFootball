// rigdio .4ccm parsing, condition grammar, and selection — see rigdio.hpp
// and docs/RIGDIO.md. Semantics mirror rigdio v2.2.0 line by line.

#include "rigdio.hpp"

namespace rigdio {

namespace {

// Python str.split(): whitespace runs, no empties.
std::vector<std::string> WhitespaceSplit(const std::string& s) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v') {
      if (!cur.empty()) { out.push_back(cur); cur.clear(); }
    } else {
      cur += c;
    }
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

}  // namespace

bool ProcessTokens(const std::string& field, std::vector<std::string>& out) {
  std::vector<std::string> data = WhitespaceSplit(field);
  size_t i = 0;
  while (i < data.size()) {
    if (data[i][0] == '\\') {
      data[i].erase(0, 1);
    } else if (data[i][0] == '[') {
      // Join following tokens until one ends with an unescaped ']'. rigdio
      // IndexErrors — the load fails — when the run is unterminated, when
      // '[x]' is a single token, or when the closing token is just ']'.
      std::string run = data[i];
      for (;;) {
        if (i + 1 >= data.size()) return false;  // IndexError: data[i+1]
        std::string& next = data[i + 1];
        const bool closes = next.back() == ']';
        if (closes && next.size() < 2) return false;  // IndexError: [-2]
        if (closes && next[next.size() - 2] != '\\') break;
        // Not a closer (or an escaped one): consumed into the run.
        std::string temp = next;
        if (temp.back() == ']' && temp[temp.size() - 2] == '\\')
          temp.erase(temp.size() - 2, 1);  // "\]" keeps its ']'
        run += ' ';
        run += temp;
        data.erase(data.begin() + i + 1);
      }
      run += ' ';
      run += data[i + 1];
      data.erase(data.begin() + i + 1);
      data[i] = run.substr(1, run.size() - 2);  // strip '[' and ']'
    }
    i++;
  }
  out = std::move(data);
  return true;
}

ParseResult Parse(const std::string&, const std::string&) {
  return {};  // not implemented
}

std::string SongCheck(const std::string& name, bool, const std::vector<std::string>&, bool) {
  return name;  // not implemented
}

int GameState::PlayerGoals(const std::string&, bool) const { return 0; }
void GameState::Score(const std::string&, bool) {}

CheckResult Check(Condition&, const GameState&, const std::string&, bool) {
  return CheckResult::No;  // not implemented
}

CheckResult CheckEntry(Entry&, const GameState&, bool) {
  return CheckResult::No;  // not implemented
}

Entry* Picker::Pick(const GameState&, bool, const Rng&, const Entry*) {
  return nullptr;  // not implemented
}

MatchSession::MatchSession(TeamMusic homeTeam, TeamMusic awayTeam,
                           const std::string& gametype, Rng rng)
    : home_(std::move(homeTeam)), away_(std::move(awayTeam)), rng_(std::move(rng)) {
  gs_.gametype = gametype;
}

MatchSession::Side& MatchSession::SideFor(bool home) { return sides_[home ? 0 : 1]; }

std::optional<PlayAction> MatchSession::OnGoal(bool, const std::string&, int, double) {
  return std::nullopt;  // not implemented
}
void MatchSession::OnHornPaused(bool, double) {}
std::optional<PlayAction> MatchSession::OnHornEnded(bool, double) { return std::nullopt; }
std::optional<PlayAction> MatchSession::Anthem(bool, double) { return std::nullopt; }
std::optional<PlayAction> MatchSession::Victory(bool, double) { return std::nullopt; }
std::optional<PlayAction> MatchSession::Chant(bool) { return std::nullopt; }
void MatchSession::ChantEnded() {}
std::optional<PlayAction> MatchSession::OnEvent(bool, const std::string&, const std::string&, int) {
  return std::nullopt;
}
void MatchSession::SetDuration(const std::string&, double) {}
double MatchSession::CachedPosition(const std::string&) const { return 0.0; }

std::optional<PlayAction> MatchSession::Play(bool, Picker&, Entry*, double, bool) {
  return std::nullopt;  // not implemented
}

}  // namespace rigdio
