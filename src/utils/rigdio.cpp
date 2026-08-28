// rigdio .4ccm parsing, condition grammar, and selection — see rigdio.hpp
// and docs/RIGDIO.md. Semantics mirror rigdio v2.2.0 line by line.

#include "rigdio.hpp"

#include <cstdlib>
#include <cmath>

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

namespace {

std::string Strip(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n\f\v");
  if (a == std::string::npos) return "";
  size_t b = s.find_last_not_of(" \t\r\n\f\v");
  return s.substr(a, b - a + 1);
}

std::string Lower(std::string s) {
  for (char& c : s)
    if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
  return s;
}

std::vector<std::string> Split(const std::string& s, char sep) {
  std::vector<std::string> out;
  size_t start = 0;
  for (;;) {
    size_t p = s.find(sep, start);
    if (p == std::string::npos) { out.push_back(s.substr(start)); return out; }
    out.push_back(s.substr(start, p - start));
    start = p + 1;
  }
}

// Python float(): the whole (stripped) string or nothing.
bool ParseDouble(const std::string& s, double& out) {
  const std::string t = Strip(s);
  if (t.empty()) return false;
  char* end = nullptr;
  out = std::strtod(t.c_str(), &end);
  return end == t.c_str() + t.size();
}

// Python int(): optional sign, digits only.
bool ParseInt(const std::string& s, long& out) {
  const std::string t = Strip(s);
  if (t.empty()) return false;
  size_t i = (t[0] == '+' || t[0] == '-') ? 1 : 0;
  if (i == t.size()) return false;
  for (size_t j = i; j < t.size(); j++)
    if (t[j] < '0' || t[j] > '9') return false;
  out = std::strtol(t.c_str(), nullptr, 10);
  return true;
}

// rigdio_util.timeToSeconds: ":"-separated floats, weighted s/m/h/d from the
// right. More than four fields IndexErrors in rigdio; any non-float returns
// None — both fail the load via StartInstruction.
bool TimeToSeconds(const std::string& s, double& out) {
  std::vector<std::string> parts = Split(s, ':');
  if (parts.size() > 4) return false;
  static const double kMulti[4] = {1, 60, 3600, 86400};
  out = 0;
  for (size_t i = 0; i < parts.size(); i++) {
    double v;
    if (!ParseDouble(parts[parts.size() - 1 - i], v)) return false;
    out += kMulti[i] * v;
  }
  return true;
}

bool ParseOp(std::string t, Op& op) {
  if (t == "=") t = "==";  // rigdio rewrites it
  if (t == "<") op = Op::LT;
  else if (t == ">") op = Op::GT;
  else if (t == "<=") op = Op::LE;
  else if (t == ">=") op = Op::GE;
  else if (t == "==") op = Op::EQ;
  else if (t == "!=") op = Op::NE;
  else return false;
  return true;
}

// buildCondition (condition.py): one token list -> a condition or an
// instruction. Returns false with `error` set at exactly rigdio's failure
// points (ValueError / IndexError / KeyError / TypeError -> load fails).
bool BuildCondition(const std::vector<std::string>& tokens, Condition& cond,
                    Instruction& instr, bool& isInstruction, std::string& error) {
  const std::string key = Lower(tokens[0]);
  std::vector<std::string> args(tokens.begin() + 1, tokens.end());
  isInstruction = false;

  auto need = [&](size_t n) {
    if (args.size() >= n) return true;
    error = key + ": missing argument";
    return false;
  };

  if (key == "goals" || key == "teamgoals" || key == "lead" || key == "time") {
    if (!need(2)) return false;
    if (!ParseOp(args[0], cond.op)) {
      error = "invalid " + key + " operator " + args[0];
      return false;
    }
    cond.type = key == "goals" ? Cond::Goals
              : key == "teamgoals" ? Cond::TeamGoals
              : key == "lead" ? Cond::Lead : Cond::Time;
    if (cond.type == Cond::Time) {
      long v;
      if (!ParseInt(args[1], v)) {  // TimeCondition: int() or ValueError
        error = "invalid time " + args[1] + "; must be integer";
        return false;
      }
      cond.value = (double)v;
    } else if (!ParseDouble(args[1], cond.value)) {
      // GoalCondition interpolates unchecked; explodes at check time.
      cond.crashesOnCheck = true;
    }
  } else if (key == "every") {
    if (!need(1)) return false;
    cond.type = Cond::Every;
    if (!ParseDouble(args[0], cond.value)) cond.crashesOnCheck = true;
    if (cond.value == 0) cond.crashesOnCheck = true;  // ZeroDivisionError
  } else if (key == "opponent") {
    cond.type = Cond::Opponent;
    for (const std::string& t : args) {  // re-split bracket-joined tokens
      if (t.find(' ') != std::string::npos)
        for (const std::string& p : WhitespaceSplit(t)) cond.args.push_back(p);
      else
        cond.args.push_back(t);
    }
  } else if (key == "match") {
    cond.type = Cond::Match;
    if (args.size() == 1 && Lower(args[0]) == "knockouts")
      cond.args = {"ro16", "quarterfinal", "semifinal", "final", "third-place"};
    else
      for (const std::string& t : args) cond.args.push_back(Lower(t));
  } else if (key == "home") {
    cond.type = Cond::Home;
  } else if (key == "first") {
    cond.type = Cond::First;
  } else if (key == "comeback") {
    cond.type = Cond::Comeback;
  } else if (key == "once") {
    cond.type = Cond::Once;
  } else if (key == "mostgoals") {
    cond.type = Cond::MostGoals;
    if (!args.empty()) cond.args.push_back(args[0]);
  } else if (key == "special") {
    cond.type = Cond::Special;
    cond.args.push_back(args.empty() ? "" : args[0]);
  } else if (key == "not") {
    // Deprecated list form: join remaining tokens, split on ",", build every
    // subcondition (a bad one fails the load); only the first is checked.
    cond.type = Cond::Not;
    std::string joined;
    for (size_t i = 0; i < args.size(); i++) {
      if (i) joined += ' ';
      joined += args[i];
    }
    bool first = true;
    for (const std::string& item : Split(joined, ',')) {
      std::vector<std::string> sub = Split(item, ' ');
      // Python "".split(" ") == [""]; empty token -> KeyError -> load fails.
      std::vector<std::string> subTokens;
      for (const std::string& t : sub) subTokens.push_back(t);
      if (subTokens.empty() || subTokens[0].empty()) {
        error = "condition/instruction  not recognised";
        return false;
      }
      Condition sc;
      Instruction si;
      bool subIsInstr = false;
      if (!BuildCondition(subTokens, sc, si, subIsInstr, error)) return false;
      if (first) {
        first = false;
        if (subIsInstr) {
          // NotCondition.check on an instruction AttributeErrors at check.
          cond.crashesOnCheck = true;
        } else {
          cond.sub = std::make_shared<Condition>(std::move(sc));
        }
      }
    }
    if (first) {  // "not" with nothing after it: buildCondition([""]) fails
      error = "condition/instruction  not recognised";
      return false;
    }
  } else if (key == "start") {
    if (!need(1)) return false;
    isInstruction = true;
    instr.type = Instr::Start;
    if (!TimeToSeconds(args[0], instr.startSeconds)) {
      error = "invalid start time " + args[0];
      return false;
    }
  } else if (key == "speed") {
    if (!need(1)) return false;
    isInstruction = true;
    instr.type = Instr::Speed;
    if (!ParseDouble(args[0], instr.speed)) {
      error = "invalid speed " + args[0];
      return false;
    }
  } else if (key == "randomise") {
    isInstruction = true;
    instr.type = Instr::Randomise;
  } else if (key == "pause") {
    if (!need(1)) return false;
    isInstruction = true;
    instr.type = Instr::Pause;
    if (args[0] != "continue" && args[0] != "restart") {
      error = "unrecognised pause type";
      return false;
    }
    instr.pauseRestart = args[0] == "restart";
    if (args.size() > 1 && args[1] == "every") {
      long v;
      if (args.size() < 3 || !ParseInt(args[2], v)) {
        error = "invalid pause every";
        return false;
      }
      instr.pauseEvery = (int)v;
    }
  } else if (key == "end") {
    if (!need(1)) return false;
    isInstruction = true;
    instr.type = Instr::End;
    if (args[0] == "loop") {
      instr.endStop = false;  // explicit no-op
    } else if (args[0] == "stop") {
      instr.endStop = true;
    } else {
      error = "unrecognised end type";
      return false;
    }
  } else if (key == "warcry") {
    isInstruction = true;
    instr.type = Instr::Warcry;
  } else if (key == "unrandom") {
    isInstruction = true;
    instr.type = Instr::Unrandom;
  } else if (key == "louder") {
    isInstruction = true;
    instr.type = Instr::Louder;
  } else if (key == "advance") {
    isInstruction = true;
    instr.type = Instr::Advance;
  } else if (key == "event") {
    if (!need(1)) return false;
    isInstruction = true;
    instr.type = Instr::Event;
    if (args[0] != "red" && args[0] != "yellow" && args[0] != "owngoal" &&
        args[0] != "sub") {
      error = "unrecognised event type " + args[0];
      return false;
    }
    instr.eventType = args[0];
  } else {
    error = "condition/instruction " + tokens[0] + " not recognised";
    return false;
  }
  return true;
}

// ConditionPlayer.__init__ + instruction prep: the entry's playback traits.
void ApplyInstructions(Entry& e) {
  e.loop = e.pname != "victory" && e.pname != "chant";
  for (const Instruction& in : e.instructions) {
    switch (in.type) {
      case Instr::Start:
        e.hasStart = true;
        e.startSeconds = in.startSeconds;
        break;
      case Instr::Speed: e.speed = in.speed; break;
      case Instr::Randomise: e.randomise = true; break;
      case Instr::Pause:
        e.pauseRestart = in.pauseRestart;
        e.pauseEvery = in.pauseEvery;
        break;
      case Instr::End:
        if (in.endStop) { e.endStop = true; e.loop = false; }
        break;
      case Instr::Warcry:
        e.warcry = true;
        e.loop = false;
        break;
      case Instr::Unrandom: e.unrandom = true; break;
      case Instr::Louder: e.louder = true; break;
      case Instr::Advance:
        e.advance = true;
        e.loop = false;
        break;
      case Instr::Event:
        e.eventType = in.eventType;
        e.loop = false;
        break;
    }
  }
}

const char* kReserved[] = {"anthem", "victory", "goal", "name",
                           "chant", ";event", "sync", "normalize"};

bool IsReserved(const std::string& s) {
  for (const char* r : kReserved)
    if (s == r) return true;
  return false;
}

// The default-filename map (rigparse `filenames`); reserved names outside it
// KeyError -> load fails.
bool DefaultFancyName(const std::string& player, std::string& fancy) {
  if (player == "goal") fancy = "Goalhorn";
  else if (player == "anthem") fancy = "Anthem";
  else if (player == "victory") fancy = "Victory Anthem";
  else if (player == "chant") fancy = "Chant";
  else return false;
  return true;
}

}  // namespace

ParseResult Parse(const std::string& text, const std::string& filenameStem) {
  ParseResult r;
  std::vector<std::string> lines;
  for (const std::string& raw : Split(text, '\n')) lines.push_back(Strip(raw));

  // Leading blanks/comments before the name line; an all-comment file
  // IndexErrors in rigdio.
  size_t i = 0;
  while (i < lines.size() && (lines[i].empty() || lines[i][0] == '#')) i++;
  if (i >= lines.size()) {
    r.error = "no content before end of file";
    return r;
  }

  // Name line: field 0 must be exactly "name" (unstripped, case-sensitive).
  {
    std::vector<std::string> nameline = Split(lines[i], ';');
    if (nameline.size() < 2 || nameline[0] != "name") {
      r.team.tname = filenameStem;  // rigparse: basename stem, as-is
      r.team.nameFromFile = true;
    } else {
      r.team.tname = Lower(nameline[1]);  // NOT stripped
      i++;
    }
  }

  // Flag lines: contiguous sync/normalize lines, any order, last wins.
  while (i < lines.size()) {
    std::vector<std::string> parts = Split(lines[i], ';');
    const std::string flag = Lower(Strip(parts[0]));
    if (flag != "sync" && flag != "normalize") break;
    const std::string val = parts.size() > 1 ? Lower(Strip(parts[1])) : "";
    const bool enabled = val != "no" && val != "off" && val != "false" && val != "0";
    (flag == "sync" ? r.team.sync : r.team.normalize) = enabled;
    i++;
  }

  // Entries.
  for (; i < lines.size(); i++) {
    const std::string& line = lines[i];
    if (line.empty() || line[0] == '#') continue;
    std::vector<std::string> data = Split(line, ';');
    for (std::string& f : data) f = Strip(f);
    const std::string player = data[0];
    if (data.size() == 1) {
      std::string fancy;
      if (IsReserved(player)) {
        if (!DefaultFancyName(player, fancy)) {
          r.error = "no default filename for reserved name " + player;
          return r;
        }
        data.push_back(r.team.tname + " - " + fancy + ".mp3");
      } else {
        data.push_back(r.team.tname + " - " + player + " Goalhorn.mp3");
      }
    }

    Entry e;
    e.pname = player;
    e.file = data[1];
    for (size_t f = 2; f < data.size(); f++) {
      std::vector<std::string> tokens;
      if (!ProcessTokens(data[f], tokens)) {
        r.error = "malformed [] quoting in: " + data[f];
        return r;
      }
      if (tokens.empty()) {
        // buildCondition returns None; rigdio AttributeErrors on it.
        r.error = "empty condition field in: " + line;
        return r;
      }
      Condition cond;
      Instruction instr;
      bool isInstruction = false;
      if (!BuildCondition(tokens, cond, instr, isInstruction, r.error)) return r;
      if (isInstruction)
        e.instructions.push_back(std::move(instr));
      else
        e.conditions.push_back(std::move(cond));
    }
    ApplyInstructions(e);

    if (!e.eventType.empty())
      r.team.events[e.eventType].push_back(std::move(e));
    else
      r.team.players[player].push_back(std::move(e));
  }

  // Default-goalhorn fallback: every non-reserved player gets the goal list
  // appended; its absence KeyErrors in rigdio.
  for (auto& kv : r.team.players) {
    if (IsReserved(kv.first)) continue;
    auto goal = r.team.players.find("goal");
    if (goal == r.team.players.end()) {
      r.error = "player entries but no goal entry ('goal' KeyError)";
      return r;
    }
    if (&kv.second != &goal->second) {
      const std::vector<Entry> copy = goal->second;
      kv.second.insert(kv.second.end(), copy.begin(), copy.end());
    }
  }

  r.ok = true;
  return r;
}

namespace {

// Python os.path.splitext stem: extension starts at the last '.', but a run
// of leading dots is not an extension.
std::string SplitextStem(const std::string& s) {
  size_t dot = s.rfind('.');
  if (dot == std::string::npos) return s;
  size_t firstNonDot = s.find_first_not_of('.');
  if (firstNonDot == std::string::npos || dot < firstNonDot) return s;
  return s.substr(0, dot);
}

}  // namespace

std::string SongCheck(const std::string& name, bool exactExists,
                      const std::vector<std::string>& listing,
                      bool applyNormalize) {
  const std::string normalizedStem = Lower(SplitextStem(name) + "_normalized");
  // Not normalizing: a pre-normalized file wins outright.
  if (!applyNormalize) {
    for (const std::string& f : listing)
      if (Lower(SplitextStem(f)) == normalizedStem) return f;
  }
  if (exactExists) return name;
  const std::string lowerName = Lower(name);
  for (const std::string& f : listing)
    if (Lower(f) == lowerName) return f;
  // Last resort either way: the pre-normalized variant.
  for (const std::string& f : listing)
    if (Lower(SplitextStem(f)) == normalizedStem) return f;
  return name;  // missing; the caller reports it
}

int GameState::PlayerGoals(const std::string& pname, bool home) const {
  const auto& m = scorers[home ? 0 : 1];
  auto it = m.find(pname);
  return it == m.end() ? 0 : it->second;
}

void GameState::Score(const std::string& pname, bool home) {
  score[home ? 0 : 1]++;
  scorers[home ? 0 : 1][pname]++;
}

namespace {

bool Compare(double lhs, Op op, double rhs) {
  switch (op) {
    case Op::LT: return lhs < rhs;
    case Op::GT: return lhs > rhs;
    case Op::LE: return lhs <= rhs;
    case Op::GE: return lhs >= rhs;
    case Op::EQ: return lhs == rhs;
    case Op::NE: return lhs != rhs;
  }
  return false;
}

CheckResult YesNo(bool b) { return b ? CheckResult::Yes : CheckResult::No; }

}  // namespace

CheckResult Check(Condition& c, const GameState& gs, const std::string& pname,
                  bool home) {
  if (c.crashesOnCheck) return CheckResult::Crash;
  switch (c.type) {
    case Cond::Goals:
      return YesNo(Compare(gs.PlayerGoals(pname, home), c.op, c.value));
    case Cond::TeamGoals:
      return YesNo(Compare(gs.TeamScore(home), c.op, c.value));
    case Cond::Lead:
      return YesNo(Compare(gs.TeamScore(home) - gs.OpponentScore(home), c.op,
                           c.value));
    case Cond::Opponent:
      for (const std::string& t : c.args)
        if (t == gs.OpponentName(home)) return CheckResult::Yes;
      return CheckResult::No;
    case Cond::Match: {
      const std::string type = Lower(gs.gametype);
      for (const std::string& t : c.args)
        if (t == type) return CheckResult::Yes;
      return CheckResult::No;
    }
    case Cond::Home:
      return YesNo(home);
    case Cond::First:
      return YesNo(gs.TeamScore(home) == 1);
    case Cond::Comeback:
      return YesNo(gs.TeamScore(home) <= gs.OpponentScore(home) &&
                   gs.OpponentScore(home) > 0);
    case Cond::Every: {
      const double goals = gs.PlayerGoals(pname, home);
      return YesNo(std::fmod(goals, c.value) == 0.0);
    }
    case Cond::MostGoals: {
      const std::string& who = c.args.empty() ? pname : c.args[0];
      const int mine = gs.PlayerGoals(who, home);
      for (const auto& kv : gs.TeamScorers(home))
        if (mine < kv.second) return CheckResult::No;
      return CheckResult::Yes;
    }
    case Cond::Once:
      if (c.spent) return CheckResult::Unload;
      c.spent = true;
      return CheckResult::Yes;
    case Cond::Time:
      // Past an unloadable threshold: drop the entry for good.
      if (gs.minute > c.value &&
          (c.op == Op::LT || c.op == Op::LE || c.op == Op::EQ))
        return CheckResult::Unload;
      return YesNo(Compare(gs.minute, c.op, c.value));
    case Cond::Special:
      return CheckResult::No;
    case Cond::Not: {
      CheckResult sub = Check(*c.sub, gs, pname, home);
      if (sub == CheckResult::Yes) return CheckResult::No;
      if (sub == CheckResult::No) return CheckResult::Yes;
      return sub;  // Unload / Crash propagate
    }
  }
  return CheckResult::No;
}

CheckResult CheckEntry(Entry& e, const GameState& gs, bool home) {
  // ConditionList.check: sequential, short-circuits on the first failure
  // (later side-effects — once, time unloads — do not fire).
  for (Condition& c : e.conditions) {
    CheckResult r = Check(c, gs, e.pname, home);
    if (r != CheckResult::Yes) return r;
  }
  return CheckResult::Yes;
}

Entry* Picker::Pick(const GameState& gs, bool home, const Rng& rng,
                    const Entry* skip) {
  // Assign stable identities once (Python compares object identity).
  for (size_t i = 0; i < entries_.size(); i++)
    if (entries_[i].uid < 0) entries_[i].uid = (int)i;
  const int skipUid = skip ? skip->uid : -1;
  auto isSkip = [&](const Entry& e) { return skip && e.uid == skipUid; };

  // Warcry mode with all-randomise warcries: unconditioned random warcry.
  if (warcryArmed) {
    std::vector<Entry*> warcries;
    bool allRandom = true;
    for (Entry& e : entries_)
      if (e.warcry && !isSkip(e)) {
        warcries.push_back(&e);
        if (!e.randomise) allRandom = false;
      }
    if (!warcries.empty() && allRandom)
      return warcries[rng((int)warcries.size())];
  }

  // The main walk: file order, first match, permanent removal on unload.
  for (size_t i = 0; i < entries_.size();) {
    if (isSkip(entries_[i])) { i++; continue; }
    const CheckResult checked = CheckEntry(entries_[i], gs, home);
    if (checked == CheckResult::Unload) {
      entries_.erase(entries_.begin() + i);  // gone for the whole match
      continue;
    }
    if (checked == CheckResult::Crash) return nullptr;  // whole pick aborts

    // All-randomise (same pname, warcries not counted): random pick from the
    // player's non-warcry entries, conditions notwithstanding.
    if (entries_[i].randomise && !entries_[i].warcry) {
      bool randomSong = true;
      for (const Entry& f : entries_)
        if (!f.randomise && !f.warcry && f.pname == entries_[i].pname) {
          randomSong = false;
          break;
        }
      if (randomSong) {
        std::vector<Entry*> pool;
        for (Entry& e : entries_)
          if (e.pname == entries_[i].pname && !e.warcry) pool.push_back(&e);
        warcryArmed = true;
        return pool[rng((int)pool.size())];
      }
    }

    if (checked == CheckResult::Yes) {
      if (warcryArmed) return &entries_[i];  // warcry or not
      if (!entries_[i].warcry) {
        warcryArmed = true;  // re-armed once a non-warcry entry is chosen
        return &entries_[i];
      }
    }
    i++;
  }

  // Advance fallbacks: first non-warcry that isn't the skipped entry,
  // conditions unchecked; then the skipped entry itself.
  if (skip) {
    for (Entry& e : entries_)
      if (!isSkip(e) && !e.warcry) {
        warcryArmed = true;
        return &e;
      }
    warcryArmed = true;
    for (Entry& e : entries_)
      if (e.uid == skipUid) return &e;
  }
  return nullptr;
}

MatchSession::MatchSession(TeamMusic homeTeam, TeamMusic awayTeam,
                           const std::string& gametype, Rng rng)
    : home_(std::move(homeTeam)), away_(std::move(awayTeam)), rng_(std::move(rng)) {
  gs_.gametype = gametype;
  gs_.names[0] = home_.tname;
  gs_.names[1] = away_.tname;
  sync_[0] = home_.sync;
  sync_[1] = away_.sync;
  for (int t = 0; t < 2; t++) {
    const TeamMusic& team = t == 0 ? home_ : away_;
    for (const auto& kv : team.players)
      sides_[t].pickers.emplace(kv.first, Picker(kv.second));
    auto chants = team.players.find("chant");
    sides_[t].chantPlayCounts.assign(
        chants == team.players.end() ? 0 : chants->second.size(), 0);
  }
}

MatchSession::Side& MatchSession::SideFor(bool home) { return sides_[home ? 0 : 1]; }


// ConditionPlayer.play(): position restore order is cache, then the
// first-play start seek on top of it.
std::optional<PlayAction> MatchSession::Play(bool home, Picker& picker,
                                             Entry* e, double now,
                                             bool isGoalhorn) {
  if (!e) return std::nullopt;
  Side& side = SideFor(home);
  double seek = e->position;
  if (sync_[home ? 0 : 1] && isGoalhorn && !e->warcry) {
    auto it = positionCache_.find(e->file);
    if (it != positionCache_.end()) seek = it->second;
  }
  if (e->firstPlay) {
    if (e->hasStart) seek = e->startSeconds;
    e->firstPlay = false;
  }
  e->position = seek;

  side.playing = &picker;
  side.playingUid = e->uid;
  side.playingIsGoalhorn = isGoalhorn;
  side.playStarted = now;
  side.playSeek = seek;
  side.playSpeed = e->speed;
  side.playFile = e->file;

  PlayAction act;
  act.file = e->file;
  act.seekSeconds = seek;
  act.speed = e->speed;
  act.loop = e->loop;
  act.isWarcry = e->warcry;
  act.advance = e->advance;
  act.endStop = e->endStop;
  act.home = home;
  act.pname = e->pname;
  return act;
}

namespace {

Entry* FindByUid(Picker* picker, int uid) {
  if (!picker || uid < 0) return nullptr;
  for (Entry& e : picker->entries())
    if (e.uid == uid) return &e;
  return nullptr;
}

}  // namespace

std::optional<PlayAction> MatchSession::OnGoal(bool home, const std::string& scorer,
                                               int minute, double now) {
  // playSong() pauses whatever was playing before picking.
  OnHornPaused(home, now);

  // The streamer presses the scorer's button if he has one, else "goal".
  const TeamMusic& team = home ? home_ : away_;
  std::string pname = "goal";
  const std::string wanted = Lower(scorer);
  for (const auto& kv : team.players) {
    if (IsReserved(kv.first)) continue;
    if (Lower(kv.first) == wanted) {
      pname = kv.first;
      break;
    }
  }

  // The goal is counted BEFORE the horn is chosen.
  gs_.Score(pname, home);
  gs_.minute = minute;

  Side& side = SideFor(home);
  auto it = side.pickers.find(pname);
  if (it == side.pickers.end()) return std::nullopt;  // no goal list at all
  Entry* e = it->second.Pick(gs_, home, rng_);
  return Play(home, it->second, e, now, true);
}

void MatchSession::OnHornPaused(bool home, double now) {
  Side& side = SideFor(home);
  Entry* e = FindByUid(side.playing, side.playingUid);
  side.playing = nullptr;
  side.playingUid = -1;
  if (!e) return;
  double pos = side.playSeek + (now - side.playStarted) * side.playSpeed;
  auto d = durations_.find(side.playFile);
  if (d != durations_.end() && d->second > 0 && e->loop)
    pos = std::fmod(pos, d->second);
  e->position = pos;
  // The sync cache is written BEFORE the pause-restart seek, which is why
  // pause;restart is inert while sync is on (docs/RIGDIO.md section 4).
  if (sync_[home ? 0 : 1] && side.playingIsGoalhorn && !e->warcry)
    positionCache_[side.playFile] = pos;
  if (e->pauseRestart && e->pauseEvery > 0) {
    e->pauseCount++;
    if (e->pauseCount % e->pauseEvery == 0) e->position = e->startSeconds;
  }
}

std::optional<PlayAction> MatchSession::OnHornEnded(bool home, double now) {
  Side& side = SideFor(home);
  Picker* picker = side.playing;
  Entry* e = FindByUid(picker, side.playingUid);
  side.playing = nullptr;
  side.playingUid = -1;
  if (!e) return std::nullopt;

  if (e->endStop) {
    // EndInstruction.run -> reloadSong: cache cleared, first play re-armed.
    e->position = 0.0;
    e->firstPlay = true;
    positionCache_.erase(e->file);
    return std::nullopt;
  }
  if (e->warcry) {
    // WarcryInstruction.run: warcry off, play the first non-warcry match.
    e->position = 0.0;
    picker->warcryArmed = false;
    Entry* next = picker->Pick(gs_, home, rng_);
    return Play(home, *picker, next, now, side.playingIsGoalhorn);
  }
  if (e->advance) {
    e->position = 0.0;
    Entry skipCopy = *e;  // Pick may reshuffle the vector under `e`
    Entry* next = picker->Pick(gs_, home, rng_, &skipCopy);
    return Play(home, *picker, next, now, side.playingIsGoalhorn);
  }
  // Looping entries are looped by the audio layer; nothing to do.
  return std::nullopt;
}

std::optional<PlayAction> MatchSession::Anthem(bool home, double now) {
  Side& side = SideFor(home);
  auto it = side.pickers.find("anthem");
  if (it == side.pickers.end()) return std::nullopt;
  Entry* e = it->second.Pick(gs_, home, rng_);
  return Play(home, it->second, e, now, false);
}

std::optional<PlayAction> MatchSession::Victory(bool home, double now) {
  Side& side = SideFor(home);
  auto it = side.pickers.find("victory");
  if (it == side.pickers.end()) return std::nullopt;
  Entry* e = it->second.Pick(gs_, home, rng_);
  return Play(home, it->second, e, now, false);
}

std::optional<PlayAction> MatchSession::Chant(bool home) {
  if (chantActive_) return std::nullopt;  // one at a time
  const TeamMusic& team = home ? home_ : away_;
  auto chants = team.players.find("chant");
  if (chants == team.players.end() || chants->second.empty())
    return std::nullopt;
  Side& side = SideFor(home);

  // Exponential-decay weighting over the non-unrandom pool:
  // weight = 0.3 ^ times_played (chantswindow.py).
  std::vector<size_t> pool;
  std::vector<double> weights;
  double total = 0.0;
  for (size_t i = 0; i < chants->second.size(); i++) {
    if (chants->second[i].unrandom) continue;
    pool.push_back(i);
    const double w = std::pow(0.3, side.chantPlayCounts[i]);
    weights.push_back(w);
    total += w;
  }
  if (pool.empty()) return std::nullopt;
  const double r = total * (double)rng_(1000000) / 1000000.0;
  size_t chosen = pool.back();
  double acc = 0.0;
  for (size_t k = 0; k < pool.size(); k++) {
    acc += weights[k];
    if (r < acc) { chosen = pool[k]; break; }
  }
  side.chantPlayCounts[chosen]++;
  chantActive_ = true;

  // Chants reload before playing: always from the top (or the start seek).
  const Entry& e = chants->second[chosen];
  PlayAction act;
  act.file = e.file;
  act.seekSeconds = e.hasStart ? e.startSeconds : 0.0;
  act.speed = e.speed;
  act.loop = false;
  act.home = home;
  act.pname = "chant";
  return act;
}

void MatchSession::ChantEnded() { chantActive_ = false; }

std::optional<PlayAction> MatchSession::OnEvent(bool home, const std::string& etype,
                                                const std::string& player,
                                                int minute) {
  const TeamMusic& team = home ? home_ : away_;
  auto clips = team.events.find(etype);
  if (clips == team.events.end()) return std::nullopt;
  Side& side = SideFor(home);
  auto last = side.lastEventMinute.find(etype);
  if (last != side.lastEventMinute.end() && minute <= last->second)
    return std::nullopt;  // once per game minute per type

  // event.py keys players upper-cased and picks a random clip.
  std::string upper = player;
  for (char& c : upper)
    if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
  std::vector<const Entry*> mine;
  for (const Entry& e : clips->second) {
    std::string p = e.pname;
    for (char& c : p)
      if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
    if (p == upper) mine.push_back(&e);
  }
  if (mine.empty()) return std::nullopt;
  side.lastEventMinute[etype] = minute;
  const Entry& e = *mine[rng_((int)mine.size())];
  PlayAction act;
  act.file = e.file;
  act.speed = e.speed;
  act.loop = false;
  act.home = home;
  act.pname = e.pname;
  return act;
}

void MatchSession::SetDuration(const std::string& file, double seconds) {
  durations_[file] = seconds;
}

double MatchSession::CachedPosition(const std::string& file) const {
  auto it = positionCache_.find(file);
  return it == positionCache_.end() ? 0.0 : it->second;
}

}  // namespace rigdio
