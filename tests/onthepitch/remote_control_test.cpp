// The remote-control protocol: the line commands an attached control panel
// sends, the property/state writes they turn into, the queue that carries them
// between the socket thread and the match thread, and the state snapshot the
// panel reads back. Pure logic - no sockets, no Match.

#include "onthepitch/remotecontrol.hpp"

#include <gtest/gtest.h>

#include <thread>
#include <vector>

#include "base/properties.hpp"
#include "onthepitch/teaminstructions.hpp"
#include "onthepitch/teamphilosophy.hpp"

using blunted::Properties;

// ── ParseLine ───────────────────────────────────────────────────────────────

TEST(RemoteControlParse, TacticCommand) {
  RemoteControl::Command cmd;
  ASSERT_TRUE(RemoteControl::ParseLine("tactic 0 team_pressure 0.8", cmd));
  EXPECT_EQ(RemoteControl::e_CommandType_Tactic, cmd.type);
  EXPECT_EQ(0, cmd.side);
  EXPECT_EQ("team_pressure", cmd.name);
  EXPECT_FLOAT_EQ(0.8f, cmd.value);
}

TEST(RemoteControlParse, TacticValueClamped) {
  RemoteControl::Command cmd;
  ASSERT_TRUE(RemoteControl::ParseLine("tactic 1 team_pressure 3.5", cmd));
  EXPECT_FLOAT_EQ(1.0f, cmd.value);
  ASSERT_TRUE(RemoteControl::ParseLine("tactic 1 team_pressure -2", cmd));
  EXPECT_FLOAT_EQ(0.0f, cmd.value);
}

TEST(RemoteControlParse, PhilosophyCommand) {
  RemoteControl::Command cmd;
  ASSERT_TRUE(RemoteControl::ParseLine("philosophy 1 gegenpressing", cmd));
  EXPECT_EQ(RemoteControl::e_CommandType_Philosophy, cmd.type);
  EXPECT_EQ(1, cmd.side);
  EXPECT_EQ("gegenpressing", cmd.name);
}

TEST(RemoteControlParse, MentalityCommand) {
  RemoteControl::Command cmd;
  ASSERT_TRUE(RemoteControl::ParseLine("mentality 0 all_out_attack", cmd));
  EXPECT_EQ(RemoteControl::e_CommandType_Mentality, cmd.type);
  EXPECT_EQ("all_out_attack", cmd.name);
}

TEST(RemoteControlParse, InstructionCommand) {
  RemoteControl::Command cmd;
  ASSERT_TRUE(RemoteControl::ParseLine("instruction 1 tiki_taka on", cmd));
  EXPECT_EQ(RemoteControl::e_CommandType_Instruction, cmd.type);
  EXPECT_EQ(1, cmd.side);
  EXPECT_EQ("tiki_taka", cmd.name);
  EXPECT_TRUE(cmd.enable);
  ASSERT_TRUE(RemoteControl::ParseLine("instruction 1 tiki_taka off", cmd));
  EXPECT_FALSE(cmd.enable);
}

TEST(RemoteControlParse, SubstitutionCommand) {
  RemoteControl::Command cmd;
  ASSERT_TRUE(RemoteControl::ParseLine("sub 0 104 117", cmd));
  EXPECT_EQ(RemoteControl::e_CommandType_Substitution, cmd.type);
  EXPECT_EQ(0, cmd.side);
  EXPECT_EQ(104, cmd.playerOutId);
  EXPECT_EQ(117, cmd.playerInId);
}

TEST(RemoteControlParse, StateCommand) {
  RemoteControl::Command cmd;
  ASSERT_TRUE(RemoteControl::ParseLine("state", cmd));
  EXPECT_EQ(RemoteControl::e_CommandType_State, cmd.type);
}

TEST(RemoteControlParse, TrailingWhitespaceTolerated) {
  // Lines arrive off a socket; \r\n and stray spaces are normal.
  RemoteControl::Command cmd;
  ASSERT_TRUE(RemoteControl::ParseLine("state\r", cmd));
  EXPECT_EQ(RemoteControl::e_CommandType_State, cmd.type);
  ASSERT_TRUE(RemoteControl::ParseLine("  tactic 0 team_pressure 0.5  ", cmd));
  EXPECT_EQ(RemoteControl::e_CommandType_Tactic, cmd.type);
}

TEST(RemoteControlParse, MalformedLinesRejected) {
  RemoteControl::Command cmd;
  EXPECT_FALSE(RemoteControl::ParseLine("", cmd));
  EXPECT_FALSE(RemoteControl::ParseLine("bogus 0 x 1", cmd));
  EXPECT_FALSE(RemoteControl::ParseLine("tactic", cmd));
  EXPECT_FALSE(RemoteControl::ParseLine("tactic 0", cmd));
  EXPECT_FALSE(RemoteControl::ParseLine("tactic 0 team_pressure", cmd));
  EXPECT_FALSE(RemoteControl::ParseLine("tactic 2 team_pressure 0.5", cmd));  // no such side
  EXPECT_FALSE(RemoteControl::ParseLine("tactic -1 team_pressure 0.5", cmd));
  EXPECT_FALSE(RemoteControl::ParseLine("tactic 0 team_pressure abc", cmd));
  EXPECT_FALSE(RemoteControl::ParseLine("instruction 0 tiki_taka maybe", cmd));
  EXPECT_FALSE(RemoteControl::ParseLine("sub 0 104", cmd));
  EXPECT_FALSE(RemoteControl::ParseLine("sub 0 x y", cmd));
  EXPECT_EQ(RemoteControl::e_CommandType_None, cmd.type);
}

// ── Appliers: the same writes the game-plan menu makes ─────────────────────

TEST(RemoteControlApply, TacticWritesUserProperty) {
  RemoteControl::Command cmd;
  ASSERT_TRUE(RemoteControl::ParseLine("tactic 0 team_pressure 0.8", cmd));
  Properties userProps;
  EXPECT_TRUE(RemoteControl::ApplyTactic(cmd, userProps));
  EXPECT_FLOAT_EQ(0.8f, userProps.GetReal("team_pressure", -1.0f));
}

TEST(RemoteControlApply, TacticRefusesNonSliderKeys) {
  // "philosophy", "mentality" and "instructions" live in the same property
  // map but have editors of their own; a numeric write would corrupt them.
  Properties userProps;
  userProps.Set("philosophy", "gegenpressing");
  RemoteControl::Command cmd;
  ASSERT_TRUE(RemoteControl::ParseLine("tactic 0 philosophy 0.4", cmd));
  EXPECT_FALSE(RemoteControl::ApplyTactic(cmd, userProps));
  EXPECT_EQ("gegenpressing", userProps.Get("philosophy"));
}

TEST(RemoteControlApply, PhilosophyWritesCanonicalName) {
  RemoteControl::Command cmd;
  ASSERT_TRUE(RemoteControl::ParseLine("philosophy 0 Tiki-Taka", cmd));
  Properties userProps;
  EXPECT_TRUE(RemoteControl::ApplyPhilosophy(cmd, userProps));
  // Whatever Parse would make of it is what lands in the property, so the
  // recompose in UpdateTactics reads back exactly this philosophy.
  EXPECT_EQ(TeamPhilosophy::GetName(TeamPhilosophy::e_Philosophy_TikiTaka),
            userProps.Get("philosophy"));
}

TEST(RemoteControlApply, PhilosophyRejectsUnknownName) {
  RemoteControl::Command cmd;
  ASSERT_TRUE(RemoteControl::ParseLine("philosophy 0 quadruple_pivot", cmd));
  Properties userProps;
  EXPECT_FALSE(RemoteControl::ApplyPhilosophy(cmd, userProps));
  EXPECT_FALSE(userProps.Exists("philosophy"));
}

TEST(RemoteControlApply, InstructionSetsAndClearsNamedBit) {
  TeamInstructions::State state;
  RemoteControl::Command cmd;
  ASSERT_TRUE(RemoteControl::ParseLine("instruction 0 frontline_pressure on", cmd));
  EXPECT_TRUE(RemoteControl::ApplyInstruction(cmd, state));
  EXPECT_TRUE(TeamInstructions::Has(state, TeamInstructions::e_Instruction_FrontlinePressure));

  // Idempotent: a second "on" must not toggle it back off.
  EXPECT_TRUE(RemoteControl::ApplyInstruction(cmd, state));
  EXPECT_TRUE(TeamInstructions::Has(state, TeamInstructions::e_Instruction_FrontlinePressure));

  ASSERT_TRUE(RemoteControl::ParseLine("instruction 0 frontline_pressure off", cmd));
  EXPECT_TRUE(RemoteControl::ApplyInstruction(cmd, state));
  EXPECT_FALSE(TeamInstructions::Has(state, TeamInstructions::e_Instruction_FrontlinePressure));
}

TEST(RemoteControlApply, InstructionRejectsUnknownName) {
  TeamInstructions::State state;
  RemoteControl::Command cmd;
  ASSERT_TRUE(RemoteControl::ParseLine("instruction 0 park_the_bus on", cmd));
  EXPECT_FALSE(RemoteControl::ApplyInstruction(cmd, state));
  EXPECT_EQ(TeamInstructions::instructionsNone, state.instructions);
}

TEST(RemoteControlApply, MentalitySetsNamedRung) {
  TeamInstructions::State state;
  RemoteControl::Command cmd;
  ASSERT_TRUE(RemoteControl::ParseLine("mentality 0 all_out_attack", cmd));
  EXPECT_TRUE(RemoteControl::ApplyMentality(cmd, state));
  EXPECT_EQ(TeamInstructions::e_Mentality_AllOutAttack, state.mentality);

  // Display-style spelling reaches the same rung: the wire name is matched
  // case- and separator-insensitively, like TeamPhilosophy::Parse.
  ASSERT_TRUE(RemoteControl::ParseLine("mentality 0 All-Out-Defence", cmd));
  EXPECT_TRUE(RemoteControl::ApplyMentality(cmd, state));
  EXPECT_EQ(TeamInstructions::e_Mentality_AllOutDefence, state.mentality);
}

TEST(RemoteControlApply, MentalityRejectsUnknownName) {
  TeamInstructions::State state;
  state.mentality = TeamInstructions::e_Mentality_Attacking;
  RemoteControl::Command cmd;
  ASSERT_TRUE(RemoteControl::ParseLine("mentality 0 yolo", cmd));
  EXPECT_FALSE(RemoteControl::ApplyMentality(cmd, state));
  EXPECT_EQ(TeamInstructions::e_Mentality_Attacking, state.mentality);
}

// ── CommandQueue: socket thread pushes, match thread drains ────────────────

TEST(RemoteControlQueue, DrainReturnsCommandsInOrderAndEmpties) {
  RemoteControl::CommandQueue queue;
  RemoteControl::Command a, b;
  ASSERT_TRUE(RemoteControl::ParseLine("tactic 0 team_pressure 0.1", a));
  ASSERT_TRUE(RemoteControl::ParseLine("philosophy 1 parkthebus", b));
  queue.Push(a);
  queue.Push(b);

  std::vector<RemoteControl::Command> drained = queue.Drain();
  ASSERT_EQ(2u, drained.size());
  EXPECT_EQ(RemoteControl::e_CommandType_Tactic, drained[0].type);
  EXPECT_EQ(RemoteControl::e_CommandType_Philosophy, drained[1].type);
  EXPECT_TRUE(queue.Drain().empty());
}

TEST(RemoteControlQueue, ConcurrentPushesAllArrive) {
  RemoteControl::CommandQueue queue;
  RemoteControl::Command cmd;
  ASSERT_TRUE(RemoteControl::ParseLine("tactic 0 team_pressure 0.5", cmd));

  const int perThread = 500;
  std::thread t1([&] {
    for (int i = 0; i < perThread; i++) queue.Push(cmd);
  });
  std::thread t2([&] {
    for (int i = 0; i < perThread; i++) queue.Push(cmd);
  });
  t1.join();
  t2.join();

  EXPECT_EQ(static_cast<size_t>(2 * perThread), queue.Drain().size());
}

// ── Snapshot → JSON ─────────────────────────────────────────────────────────

namespace {

RemoteControl::Snapshot MakeSnapshot() {
  RemoteControl::Snapshot snapshot;
  snapshot.score[0] = 2;
  snapshot.score[1] = 1;
  snapshot.matchTime_ms = 2700000;
  snapshot.phase = 1;
  snapshot.inPlay = true;
  snapshot.substitutionWindow = false;

  RemoteControl::TeamState& home = snapshot.teams[0];
  home.name = "HDG";
  home.philosophy = "gegenpressing";
  home.mentality = "attacking";
  home.instructions.push_back("frontline_pressure");
  home.tactics.push_back({"team_pressure", 0.75f});
  RemoteControl::PlayerState player;
  player.id = 104;
  player.name = "O\"Brien";  // exercises escaping
  player.role = "CM";
  player.onPitch = true;
  home.players.push_back(player);

  snapshot.teams[1].name = "2HUG";
  snapshot.teams[1].philosophy = "balanced";
  snapshot.teams[1].mentality = "balanced";
  return snapshot;
}

}  // namespace

TEST(RemoteControlSnapshot, ToJsonCarriesMatchAndTeamState) {
  const std::string json = RemoteControl::ToJson(MakeSnapshot());

  EXPECT_NE(std::string::npos, json.find("\"score\":[2,1]"));
  EXPECT_NE(std::string::npos, json.find("\"time_ms\":2700000"));
  EXPECT_NE(std::string::npos, json.find("\"phase\":1"));
  EXPECT_NE(std::string::npos, json.find("\"in_play\":true"));
  EXPECT_NE(std::string::npos, json.find("\"sub_window\":false"));
  EXPECT_NE(std::string::npos, json.find("\"name\":\"HDG\""));
  EXPECT_NE(std::string::npos, json.find("\"philosophy\":\"gegenpressing\""));
  EXPECT_NE(std::string::npos, json.find("\"mentality\":\"attacking\""));
  EXPECT_NE(std::string::npos, json.find("\"instructions\":[\"frontline_pressure\"]"));
  EXPECT_NE(std::string::npos, json.find("\"team_pressure\":0.75"));
  EXPECT_NE(std::string::npos, json.find("\"id\":104"));
  EXPECT_NE(std::string::npos, json.find("\"role\":\"CM\""));
  EXPECT_NE(std::string::npos, json.find("\"on_pitch\":true"));
  // One line, so the socket reader can frame it by newline.
  EXPECT_EQ(std::string::npos, json.find('\n'));
}

TEST(RemoteControlSnapshot, ToJsonEscapesStrings) {
  const std::string json = RemoteControl::ToJson(MakeSnapshot());
  // The quote inside O"Brien must arrive escaped, or the panel's JSON.parse dies.
  EXPECT_NE(std::string::npos, json.find("O\\\"Brien"));
  EXPECT_EQ(std::string::npos, json.find("O\"Brien\""));
}

// ── Wire names round-trip with the engine's display names ──────────────────

TEST(RemoteControlNames, EveryInstructionReachableByWireName) {
  // The panel builds its toggles from the snapshot's wire names; every
  // instruction the engine knows must round-trip through them.
  for (int i = 0; i < TeamInstructions::instructionCount; i++) {
    const TeamInstructions::e_Instruction instruction = TeamInstructions::GetInstructionAt(i);
    const std::string wire =
        RemoteControl::WireName(TeamInstructions::GetInstructionName(instruction));
    TeamInstructions::State state;
    RemoteControl::Command cmd;
    ASSERT_TRUE(RemoteControl::ParseLine("instruction 0 " + wire + " on", cmd)) << wire;
    EXPECT_TRUE(RemoteControl::ApplyInstruction(cmd, state)) << wire;
    EXPECT_TRUE(TeamInstructions::Has(state, instruction)) << wire;
  }
}

TEST(RemoteControlNames, EveryMentalityReachableByWireName) {
  for (int i = 0; i < TeamInstructions::e_Mentality_Count; i++) {
    const TeamInstructions::e_Mentality mentality = static_cast<TeamInstructions::e_Mentality>(i);
    const std::string wire =
        RemoteControl::WireName(TeamInstructions::GetMentalityName(mentality));
    TeamInstructions::State state;
    state.mentality = TeamInstructions::e_Mentality_Count;  // sentinel: must be overwritten
    RemoteControl::Command cmd;
    ASSERT_TRUE(RemoteControl::ParseLine("mentality 0 " + wire, cmd)) << wire;
    EXPECT_TRUE(RemoteControl::ApplyMentality(cmd, state)) << wire;
    EXPECT_EQ(mentality, state.mentality) << wire;
  }
}

// ── LineBuffer: TCP chunks into protocol lines ──────────────────────────────

TEST(RemoteControlLineBuffer, ReassemblesLinesAcrossChunks) {
  RemoteControl::LineBuffer buffer;
  std::vector<std::string> lines = buffer.Append("tactic 0 team_pr", 16);
  EXPECT_TRUE(lines.empty());
  lines = buffer.Append("essure 0.8\nstate\n", 17);
  ASSERT_EQ(2u, lines.size());
  EXPECT_EQ("tactic 0 team_pressure 0.8", lines[0]);
  EXPECT_EQ("state", lines[1]);
}

TEST(RemoteControlLineBuffer, KeepsTrailingPartialLine) {
  RemoteControl::LineBuffer buffer;
  std::vector<std::string> lines = buffer.Append("state\nsta", 9);
  ASSERT_EQ(1u, lines.size());
  lines = buffer.Append("te\n", 3);
  ASSERT_EQ(1u, lines.size());
  EXPECT_EQ("state", lines[0]);
}

TEST(RemoteControlLineBuffer, DropsAbsurdlyLongLines) {
  // A peer that never sends a newline must not grow the buffer forever.
  RemoteControl::LineBuffer buffer;
  const std::string garbage(8192, 'x');
  EXPECT_TRUE(buffer.Append(garbage.c_str(), garbage.size()).empty());
  // Once over the limit the junk is discarded, and the next real line works.
  std::vector<std::string> lines = buffer.Append("\nstate\n", 7);
  ASSERT_EQ(1u, lines.size());
  EXPECT_EQ("state", lines[0]);
}
