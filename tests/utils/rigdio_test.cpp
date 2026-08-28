// rigdio .4ccm parser / condition grammar / selection — 1:1 with rigdio
// v2.2.0 (see docs/RIGDIO.md). Each behaviour here names the rigdio source
// it mirrors.

#include "utils/rigdio.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>

#ifndef TESTDATA_EXPORTS
#define TESTDATA_EXPORTS ""
#endif
using namespace rigdio;

namespace {

std::vector<std::string> Tok(const std::string& s) {
  std::vector<std::string> out;
  EXPECT_TRUE(ProcessTokens(s, out)) << "tokenization failed for: " << s;
  return out;
}

// --- processTokens (condition.py) ------------------------------------------

TEST(RigdioTokens, SplitsOnWhitespace) {
  EXPECT_EQ(Tok("goals >= 2"), (std::vector<std::string>{"goals", ">=", "2"}));
}

TEST(RigdioTokens, EmptyFieldYieldsNoTokens) {
  EXPECT_EQ(Tok(""), std::vector<std::string>{});
  EXPECT_EQ(Tok("   "), std::vector<std::string>{});
}

TEST(RigdioTokens, BracketsJoinTokensWithSpaces) {
  EXPECT_EQ(Tok("special [Gogeta Da MVP]"),
            (std::vector<std::string>{"special", "Gogeta Da MVP"}));
}

TEST(RigdioTokens, BracketRunOfTwoTokens) {
  EXPECT_EQ(Tok("mostgoals [John 1000]"),
            (std::vector<std::string>{"mostgoals", "John 1000"}));
}

TEST(RigdioTokens, LeadingBackslashEscapesTokenStart) {
  // condition.py strips one leading escape character.
  EXPECT_EQ(Tok("opponent \\[hdg"),
            (std::vector<std::string>{"opponent", "[hdg"}));
}

TEST(RigdioTokens, EscapedClosingBracketStaysInString) {
  // "[a b\]]" -> tokens "a", "b\]" -> the \] keeps its ] and drops the escape,
  // and the run continues to the next real closer.
  EXPECT_EQ(Tok("special [a b\\] c]"),
            (std::vector<std::string>{"special", "a b] c"}));
}

TEST(RigdioTokens, SingleBracketTokenFailsLikeRigdio) {
  // "[x]" as one token IndexErrors in rigdio: the load fails.
  std::vector<std::string> out;
  EXPECT_FALSE(ProcessTokens("special [x]", out));
}

TEST(RigdioTokens, UnterminatedBracketFailsLikeRigdio) {
  std::vector<std::string> out;
  EXPECT_FALSE(ProcessTokens("special [a b", out));
}

// --- Parse (rigparse.py) -----------------------------------------------------

TEST(RigdioParse, NameLineFlagsAndEntries) {
  ParseResult r = Parse(
      "# team identifier\n"
      "name;HDG\n"
      "sync;no\n"
      "normalize;off\n"
      "\n"
      "anthem;anthem.mp3\n"
      "goal;goalhorn.mp3\n",
      "somefile");
  ASSERT_TRUE(r.ok) << r.error;
  EXPECT_EQ(r.team.tname, "hdg");  // lowercased
  EXPECT_FALSE(r.team.nameFromFile);
  EXPECT_FALSE(r.team.sync);
  EXPECT_FALSE(r.team.normalize);
  ASSERT_EQ(r.team.players.count("anthem"), 1u);
  EXPECT_EQ(r.team.players.at("anthem")[0].file, "anthem.mp3");
}

TEST(RigdioParse, TnameIsNotStripped) {
  // rigparse: tname = nameline[1].lower() with no strip.
  ParseResult r = Parse("name; dbg\ngoal;g.mp3\n", "f");
  ASSERT_TRUE(r.ok);
  EXPECT_EQ(r.team.tname, " dbg");
}

TEST(RigdioParse, MissingNameLineFallsBackToFilenameStem) {
  // The first line is NOT consumed: it parses as an ordinary entry.
  ParseResult r = Parse("goal;g.mp3\n", "dbgvgl26");
  ASSERT_TRUE(r.ok);
  EXPECT_EQ(r.team.tname, "dbgvgl26");
  EXPECT_TRUE(r.team.nameFromFile);
  EXPECT_EQ(r.team.players.at("goal").size(), 1u);
}

TEST(RigdioParse, NameFieldIsCaseSensitiveAndUnstripped) {
  // "Name;x" and "name ;x" are not name lines.
  ParseResult r = Parse("Name;dbg\ngoal;g.mp3\n", "stem");
  ASSERT_TRUE(r.ok);
  EXPECT_EQ(r.team.tname, "stem");
  // ...and "Name" became a player with a goalhorn.
  EXPECT_EQ(r.team.players.count("Name"), 1u);
}

TEST(RigdioParse, FlagsDefaultOnAndAcceptAnyOrder) {
  ParseResult r = Parse("name;x\nnormalize;NO\nsync;1\ngoal;g.mp3\n", "f");
  ASSERT_TRUE(r.ok);
  EXPECT_TRUE(r.team.sync);  // "1" is not in {no, off, false, 0}
  EXPECT_FALSE(r.team.normalize);
}

TEST(RigdioParse, FlagWithoutValueIsEnabled) {
  ParseResult r = Parse("name;x\nsync\ngoal;g.mp3\n", "f");
  ASSERT_TRUE(r.ok);
  EXPECT_TRUE(r.team.sync);
}

TEST(RigdioParse, DefaultFilenamesForBareNames) {
  ParseResult r = Parse(
      "name;dbg\n"
      "victory\n"
      "anthem\n"
      "chant\n"
      "goal\n"
      "Some Player\n",
      "f");
  ASSERT_TRUE(r.ok) << r.error;
  EXPECT_EQ(r.team.players.at("victory")[0].file, "dbg - Victory Anthem.mp3");
  EXPECT_EQ(r.team.players.at("anthem")[0].file, "dbg - Anthem.mp3");
  EXPECT_EQ(r.team.players.at("chant")[0].file, "dbg - Chant.mp3");
  EXPECT_EQ(r.team.players.at("goal")[0].file, "dbg - Goalhorn.mp3");
  EXPECT_EQ(r.team.players.at("Some Player")[0].file,
            "dbg - Some Player Goalhorn.mp3");
}

TEST(RigdioParse, ReservedNamesAreCaseSensitive) {
  // "Anthem" is a player, and gets the goal list appended.
  ParseResult r = Parse("name;x\ngoal;g.mp3\nAnthem;a.mp3\n", "f");
  ASSERT_TRUE(r.ok);
  ASSERT_EQ(r.team.players.count("Anthem"), 1u);
  ASSERT_EQ(r.team.players.at("Anthem").size(), 2u);  // own + goal fallback
  EXPECT_EQ(r.team.players.at("Anthem")[1].file, "g.mp3");
  EXPECT_EQ(r.team.players.at("Anthem")[1].pname, "goal");  // keeps pname
}

TEST(RigdioParse, GoalFallbackAppendsWholeGoalList) {
  ParseResult r = Parse(
      "name;x\n"
      "goal;g1.mp3;goals == 1\n"
      "goal;g2.mp3\n"
      "P;p.mp3;goals >= 2\n",
      "f");
  ASSERT_TRUE(r.ok) << r.error;
  ASSERT_EQ(r.team.players.at("P").size(), 3u);
  EXPECT_EQ(r.team.players.at("P")[1].file, "g1.mp3");
  EXPECT_EQ(r.team.players.at("P")[2].file, "g2.mp3");
  // anthem/victory/goal/chant do NOT get the fallback.
  EXPECT_EQ(r.team.players.at("goal").size(), 2u);
}

TEST(RigdioParse, PlayerWithoutGoalListFailsLikeRigdio) {
  // rigparse KeyErrors on players['goal'] when a player exists with no
  // goal entry: the load fails.
  ParseResult r = Parse("name;x\nP;p.mp3\n", "f");
  EXPECT_FALSE(r.ok);
}

TEST(RigdioParse, NoPlayersMeansNoGoalListNeeded) {
  ParseResult r = Parse("name;x\nanthem;a.mp3\n", "f");
  EXPECT_TRUE(r.ok) << r.error;
}

TEST(RigdioParse, CommentsAndBlanksSkippedEverywhere) {
  ParseResult r = Parse(
      "\n# hi\n\nname;x\n\n# mid\ngoal;g.mp3\n# tail\n\n", "f");
  ASSERT_TRUE(r.ok);
  EXPECT_EQ(r.team.players.size(), 1u);
}

TEST(RigdioParse, AllCommentFileFailsLikeRigdio) {
  EXPECT_FALSE(Parse("# a\n\n# b\n", "f").ok);
  EXPECT_FALSE(Parse("", "f").ok);
}

TEST(RigdioParse, EmptyConditionFieldFailsLikeRigdio) {
  // Trailing ';' builds a None condition -> AttributeError -> load fails.
  EXPECT_FALSE(Parse("name;x\ngoal;g.mp3;\n", "f").ok);
  EXPECT_FALSE(Parse("name;x\ngoal;g.mp3;;home\n", "f").ok);
}

TEST(RigdioParse, UnknownConditionFailsLikeRigdio) {
  EXPECT_FALSE(Parse("name;x\ngoal;g.mp3;sometimes\n", "f").ok);
}

TEST(RigdioParse, LateFlagLineFailsLikeRigdio) {
  // A bare "sync" below the entries hits the default-filename KeyError.
  EXPECT_FALSE(Parse("name;x\ngoal;g.mp3\nsync\n", "f").ok);
  // With a value it parses as player "sync" -> needs no default -> but then
  // it is a player entry and gets the goal fallback: it LOADS.
  ParseResult r = Parse("name;x\ngoal;g.mp3\nsync;no\n", "f");
  ASSERT_TRUE(r.ok);
  EXPECT_TRUE(r.team.sync);  // the flag did not change
}

TEST(RigdioParse, FieldsAreStripped) {
  ParseResult r = Parse("name;x\ngoal ; g.mp3 ; home \n", "f");
  ASSERT_TRUE(r.ok) << r.error;
  ASSERT_EQ(r.team.players.at("goal").size(), 1u);
  EXPECT_EQ(r.team.players.at("goal")[0].file, "g.mp3");
  ASSERT_EQ(r.team.players.at("goal")[0].conditions.size(), 1u);
  EXPECT_EQ(r.team.players.at("goal")[0].conditions[0].type, Cond::Home);
}

TEST(RigdioParse, EventEntriesRouteToEventTable) {
  ParseResult r = Parse(
      "name;x\n"
      "goal;g.mp3\n"
      "P;oops.mp3;event owngoal\n",
      "f");
  ASSERT_TRUE(r.ok) << r.error;
  EXPECT_EQ(r.team.players.count("P"), 0u);
  ASSERT_EQ(r.team.events.count("owngoal"), 1u);
  EXPECT_EQ(r.team.events.at("owngoal")[0].pname, "P");
  EXPECT_FALSE(r.team.events.at("owngoal")[0].loop);
}

TEST(RigdioParse, UnknownEventTypeFailsLikeRigdio) {
  EXPECT_FALSE(Parse("name;x\ngoal;g.mp3\nP;e.mp3;event dance\n", "f").ok);
}

// --- instruction / condition construction (condition.py) --------------------

TEST(RigdioParse, GoalsConditionOperators) {
  ParseResult r = Parse(
      "name;x\n"
      "goal;a.mp3;goals = 1\n"
      "goal;b.mp3;goals != 2\n",
      "f");
  ASSERT_TRUE(r.ok) << r.error;
  const auto& gs = r.team.players.at("goal");
  EXPECT_EQ(gs[0].conditions[0].type, Cond::Goals);
  EXPECT_EQ(gs[0].conditions[0].op, Op::EQ);  // "=" rewritten to "=="
  EXPECT_EQ(gs[0].conditions[0].value, 1);
  EXPECT_EQ(gs[1].conditions[0].op, Op::NE);
}

TEST(RigdioParse, InvalidOperatorFailsLikeRigdio) {
  EXPECT_FALSE(Parse("name;x\ngoal;a.mp3;goals => 1\n", "f").ok);
  EXPECT_FALSE(Parse("name;x\ngoal;a.mp3;lead ! 1\n", "f").ok);
  EXPECT_FALSE(Parse("name;x\ngoal;a.mp3;time => 1\n", "f").ok);
}

TEST(RigdioParse, NonNumericGoalsOperandCrashesAtCheck) {
  // rigdio only explodes at check time; the whole pick aborts there.
  ParseResult r = Parse("name;x\ngoal;a.mp3;goals >= x\n", "f");
  ASSERT_TRUE(r.ok) << r.error;
  EXPECT_TRUE(r.team.players.at("goal")[0].conditions[0].crashesOnCheck);
}

TEST(RigdioParse, NonIntegerTimeFailsLikeRigdio) {
  EXPECT_FALSE(Parse("name;x\ngoal;a.mp3;time >= x\n", "f").ok);
}

TEST(RigdioParse, StartInstructionParsesMinSecWithFraction) {
  ParseResult r = Parse("name;x\ngoal;a.mp3;start 0:30.5\n", "f");
  ASSERT_TRUE(r.ok) << r.error;
  const Entry& e = r.team.players.at("goal")[0];
  EXPECT_TRUE(e.hasStart);
  EXPECT_DOUBLE_EQ(e.startSeconds, 30.5);
}

TEST(RigdioParse, StartInstructionHoursAndDays) {
  ParseResult r = Parse("name;x\ngoal;a.mp3;start 1:02:03\n", "f");
  ASSERT_TRUE(r.ok) << r.error;
  EXPECT_DOUBLE_EQ(r.team.players.at("goal")[0].startSeconds, 3723.0);
}

TEST(RigdioParse, BadStartTimeFailsLikeRigdio) {
  EXPECT_FALSE(Parse("name;x\ngoal;a.mp3;start abc\n", "f").ok);
}

TEST(RigdioParse, InstructionTraits) {
  ParseResult r = Parse(
      "name;x\n"
      "goal;a.mp3;warcry\n"
      "goal;b.mp3;end stop;speed 1.5\n"
      "goal;c.mp3;pause restart every 2\n"
      "goal;d.mp3;advance;randomise;louder\n"
      "chant;e.mp3;unrandom\n"
      "victory;v.mp3\n",
      "f");
  ASSERT_TRUE(r.ok) << r.error;
  const auto& g = r.team.players.at("goal");
  EXPECT_TRUE(g[0].warcry);
  EXPECT_FALSE(g[0].loop);  // warcry disables repeat
  EXPECT_TRUE(g[1].endStop);
  EXPECT_FALSE(g[1].loop);
  EXPECT_DOUBLE_EQ(g[1].speed, 1.5);
  EXPECT_TRUE(g[2].pauseRestart);
  EXPECT_EQ(g[2].pauseEvery, 2);
  EXPECT_TRUE(g[2].loop);
  EXPECT_TRUE(g[3].advance);
  EXPECT_FALSE(g[3].loop);
  EXPECT_TRUE(g[3].randomise);
  EXPECT_TRUE(g[3].louder);
  EXPECT_TRUE(r.team.players.at("chant")[0].unrandom);
  EXPECT_FALSE(r.team.players.at("chant")[0].loop);    // chant never loops
  EXPECT_FALSE(r.team.players.at("victory")[0].loop);  // victory never loops
}

TEST(RigdioParse, EndLoopIsANoOp) {
  ParseResult r = Parse("name;x\ngoal;a.mp3;end loop\n", "f");
  ASSERT_TRUE(r.ok) << r.error;
  EXPECT_TRUE(r.team.players.at("goal")[0].loop);
  EXPECT_FALSE(r.team.players.at("goal")[0].endStop);
}

TEST(RigdioParse, BadPauseOrEndTypeFailsLikeRigdio) {
  EXPECT_FALSE(Parse("name;x\ngoal;a.mp3;pause sometimes\n", "f").ok);
  EXPECT_FALSE(Parse("name;x\ngoal;a.mp3;end fadeout\n", "f").ok);
}

TEST(RigdioParse, MetaConditionsOrAndIfAreNotRegistered) {
  EXPECT_FALSE(Parse("name;x\ngoal;a.mp3;or home first\n", "f").ok);
  EXPECT_FALSE(Parse("name;x\ngoal;a.mp3;and home first\n", "f").ok);
  EXPECT_FALSE(Parse("name;x\ngoal;a.mp3;if home first comeback\n", "f").ok);
}

TEST(RigdioParse, NotConditionWrapsSubcondition) {
  ParseResult r = Parse("name;x\ngoal;a.mp3;not home\n", "f");
  ASSERT_TRUE(r.ok) << r.error;
  const Condition& c = r.team.players.at("goal")[0].conditions[0];
  EXPECT_EQ(c.type, Cond::Not);
  ASSERT_TRUE(c.sub != nullptr);
  EXPECT_EQ(c.sub->type, Cond::Home);
}

TEST(RigdioParse, SpecialConditionKeepsBracketLabel) {
  ParseResult r = Parse(
      "name;x\nvictory;v.mp3;special [John 1000 MVP]\n", "f");
  ASSERT_TRUE(r.ok) << r.error;
  const Condition& c = r.team.players.at("victory")[0].conditions[0];
  EXPECT_EQ(c.type, Cond::Special);
  ASSERT_EQ(c.args.size(), 1u);
  EXPECT_EQ(c.args[0], "John 1000 MVP");
}

TEST(RigdioParse, ConditionNamesAreCaseInsensitive) {
  // buildCondition looks up tokens[0].lower().
  ParseResult r = Parse("name;x\ngoal;a.mp3;HOME;First\n", "f");
  ASSERT_TRUE(r.ok) << r.error;
  EXPECT_EQ(r.team.players.at("goal")[0].conditions.size(), 2u);
}

TEST(RigdioParse, MissingConditionArgumentsFailLikeRigdio) {
  EXPECT_FALSE(Parse("name;x\ngoal;a.mp3;goals\n", "f").ok);   // IndexError
  EXPECT_FALSE(Parse("name;x\ngoal;a.mp3;every\n", "f").ok);   // IndexError
  EXPECT_FALSE(Parse("name;x\ngoal;a.mp3;start\n", "f").ok);   // IndexError
}

// --- songCheck (rigparse.py) -------------------------------------------------

TEST(RigdioSongCheck, ExactFileWins) {
  EXPECT_EQ(SongCheck("a.mp3", true, {"a.mp3", "b.mp3"}, true), "a.mp3");
}

TEST(RigdioSongCheck, CaseInsensitiveFallback) {
  EXPECT_EQ(SongCheck("Goalhorn.MP3", false, {"goalhorn.mp3"}, true),
            "goalhorn.mp3");
}

TEST(RigdioSongCheck, MissingReturnsNameUnchanged) {
  EXPECT_EQ(SongCheck("a.mp3", false, {"b.mp3"}, true), "a.mp3");
}

TEST(RigdioSongCheck, NormalizedVariantPreferredWhenNotNormalizing) {
  // normalize off: the _normalized stem wins even over an existing exact file.
  EXPECT_EQ(SongCheck("a.mp3", true, {"a.mp3", "A_normalized.ogg"}, false),
            "A_normalized.ogg");
}

TEST(RigdioSongCheck, NormalizedVariantIsLastResortWhenNormalizing) {
  EXPECT_EQ(SongCheck("a.mp3", false, {"a_normalized.mp3"}, true),
            "a_normalized.mp3");
}

// --- GameState (gamestate.py) ------------------------------------------------

TEST(RigdioGameState, ScoreCreditsTeamAndPlayer) {
  GameState gs;
  gs.Score("P", true);
  gs.Score("P", true);
  gs.Score("Q", false);
  EXPECT_EQ(gs.TeamScore(true), 2);
  EXPECT_EQ(gs.TeamScore(false), 1);
  EXPECT_EQ(gs.OpponentScore(true), 1);
  EXPECT_EQ(gs.PlayerGoals("P", true), 2);
  EXPECT_EQ(gs.PlayerGoals("P", false), 0);
  EXPECT_EQ(gs.PlayerGoals("nobody", true), 0);
}

// --- condition evaluation (condition.py check methods) ------------------------

namespace evalhelp {

Entry ParseEntry(const std::string& fields) {
  ParseResult r = Parse("name;me\ngoal;g.mp3;" + fields + "\n", "f");
  EXPECT_TRUE(r.ok) << r.error;
  return r.team.players.at("goal")[0];
}

}  // namespace evalhelp

TEST(RigdioCheck, GoalsCountsThePnameNotTheTeam) {
  GameState gs;
  gs.Score("A", true);
  gs.Score("goal", true);
  Entry e = evalhelp::ParseEntry("goals == 1");
  // pname is "goal": only the generic tally counts.
  EXPECT_EQ(CheckEntry(e, gs, true), CheckResult::Yes);
  gs.Score("goal", true);
  EXPECT_EQ(CheckEntry(e, gs, true), CheckResult::No);
}

TEST(RigdioCheck, TeamGoalsAndLead) {
  GameState gs;
  gs.Score("A", true);
  gs.Score("B", true);
  gs.Score("C", false);
  Entry tg = evalhelp::ParseEntry("teamgoals >= 2");
  EXPECT_EQ(CheckEntry(tg, gs, true), CheckResult::Yes);
  EXPECT_EQ(CheckEntry(tg, gs, false), CheckResult::No);
  Entry lead = evalhelp::ParseEntry("lead == 1");
  EXPECT_EQ(CheckEntry(lead, gs, true), CheckResult::Yes);
  Entry behind = evalhelp::ParseEntry("lead < 0");
  EXPECT_EQ(CheckEntry(behind, gs, false), CheckResult::Yes);
}

TEST(RigdioCheck, FirstIsTeamScoreExactlyOne) {
  GameState gs;
  gs.Score("A", true);
  Entry e = evalhelp::ParseEntry("first");
  EXPECT_EQ(CheckEntry(e, gs, true), CheckResult::Yes);
  gs.Score("A", true);
  EXPECT_EQ(CheckEntry(e, gs, true), CheckResult::No);
}

TEST(RigdioCheck, ComebackNeedsOpponentGoalsAndNotLeading) {
  GameState gs;
  Entry e = evalhelp::ParseEntry("comeback");
  gs.Score("A", true);  // 1-0 up: no comeback
  EXPECT_EQ(CheckEntry(e, gs, true), CheckResult::No);
  gs.Score("X", false);
  gs.Score("X", false);  // 1-2 down
  EXPECT_EQ(CheckEntry(e, gs, true), CheckResult::Yes);
  gs.Score("A", true);  // 2-2: still counts (<=)
  EXPECT_EQ(CheckEntry(e, gs, true), CheckResult::Yes);
  gs.Score("A", true);  // 3-2 up: no
  EXPECT_EQ(CheckEntry(e, gs, true), CheckResult::No);
}

TEST(RigdioCheck, HomeAndNotHome) {
  GameState gs;
  Entry home = evalhelp::ParseEntry("home");
  EXPECT_EQ(CheckEntry(home, gs, true), CheckResult::Yes);
  EXPECT_EQ(CheckEntry(home, gs, false), CheckResult::No);
  Entry away = evalhelp::ParseEntry("not home");
  EXPECT_EQ(CheckEntry(away, gs, false), CheckResult::Yes);
  EXPECT_EQ(CheckEntry(away, gs, true), CheckResult::No);
}

TEST(RigdioCheck, EveryDividesPnameGoals) {
  GameState gs;
  Entry e = evalhelp::ParseEntry("every 2");
  gs.Score("goal", true);
  EXPECT_EQ(CheckEntry(e, gs, true), CheckResult::No);
  gs.Score("goal", true);
  EXPECT_EQ(CheckEntry(e, gs, true), CheckResult::Yes);
}

TEST(RigdioCheck, OpponentMatchesLoadedName) {
  GameState gs;
  gs.names[0] = "me";
  gs.names[1] = "hdg";
  Entry e = evalhelp::ParseEntry("opponent hdg dbg");
  EXPECT_EQ(CheckEntry(e, gs, true), CheckResult::Yes);
  EXPECT_EQ(CheckEntry(e, gs, false), CheckResult::No);  // our name is "me"
  // Case-sensitive: loaded names are lowercased, so "HDG" never matches.
  Entry upper = evalhelp::ParseEntry("opponent HDG");
  EXPECT_EQ(CheckEntry(upper, gs, true), CheckResult::No);
}

TEST(RigdioCheck, MatchTypeAndKnockoutsExpansion) {
  GameState gs;
  gs.gametype = "Final";
  Entry e = evalhelp::ParseEntry("match knockouts");
  EXPECT_EQ(CheckEntry(e, gs, true), CheckResult::Yes);
  gs.gametype = "group";
  EXPECT_EQ(CheckEntry(e, gs, true), CheckResult::No);
  Entry g = evalhelp::ParseEntry("match GROUP boss");
  EXPECT_EQ(CheckEntry(g, gs, true), CheckResult::Yes);
}

TEST(RigdioCheck, OnceIsTrueThenUnloads) {
  GameState gs;
  Entry e = evalhelp::ParseEntry("once");
  EXPECT_EQ(CheckEntry(e, gs, true), CheckResult::Yes);
  EXPECT_EQ(CheckEntry(e, gs, true), CheckResult::Unload);
}

TEST(RigdioCheck, TimeComparesGoalMinuteAndUnloadsWhenPast) {
  GameState gs;
  gs.minute = 10;
  Entry early = evalhelp::ParseEntry("time <= 15");
  EXPECT_EQ(CheckEntry(early, gs, true), CheckResult::Yes);
  gs.minute = 20;
  // past the threshold with an unloadable operator -> Unload
  EXPECT_EQ(CheckEntry(early, gs, true), CheckResult::Unload);
  Entry late = evalhelp::ParseEntry("time > 80");
  gs.minute = 85;
  EXPECT_EQ(CheckEntry(late, gs, true), CheckResult::Yes);
  gs.minute = 40;
  EXPECT_EQ(CheckEntry(late, gs, true), CheckResult::No);  // > never unloads
}

TEST(RigdioCheck, MostGoalsSelfAndSpecified) {
  GameState gs;
  gs.Score("A B", true);
  gs.Score("A B", true);
  gs.Score("B", true);
  ParseResult r = Parse(
      "name;me\n"
      "goal;g.mp3\n"
      "A B;a.mp3;mostgoals\n"
      "B;b.mp3;mostgoals\n"
      "C;c.mp3;mostgoals [A B]\n"
      "D;d.mp3;mostgoals B\n",
      "f");
  ASSERT_TRUE(r.ok) << r.error;
  Entry a = r.team.players.at("A B")[0];
  Entry b = r.team.players.at("B")[0];
  Entry c = r.team.players.at("C")[0];
  Entry d = r.team.players.at("D")[0];
  EXPECT_EQ(CheckEntry(a, gs, true), CheckResult::Yes);
  EXPECT_EQ(CheckEntry(b, gs, true), CheckResult::No);
  // mostgoals [A B]: the SPECIFIED player's tally is compared. Note the
  // brackets: only multi-word values may carry them (a single-token "[A]"
  // IndexErrors rigdio's tokenizer and fails the load).
  EXPECT_EQ(CheckEntry(c, gs, true), CheckResult::Yes);
  EXPECT_EQ(CheckEntry(d, gs, true), CheckResult::No);
}

TEST(RigdioCheck, SpecialIsAlwaysFalse) {
  GameState gs;
  // Single-word labels are written without brackets (rigdj only brackets
  // values containing spaces; "[MVP]" as one token fails the load).
  ParseResult r = Parse(
      "name;me\n"
      "victory;v.mp3;special MVP\n"
      "victory;w.mp3;special [John 1000 MVP]\n",
      "f");
  ASSERT_TRUE(r.ok) << r.error;
  EXPECT_EQ(CheckEntry(r.team.players.at("victory")[0], gs, true),
            CheckResult::No);
  EXPECT_EQ(CheckEntry(r.team.players.at("victory")[1], gs, true),
            CheckResult::No);
}

TEST(RigdioCheck, CrashingConditionAborts) {
  GameState gs;
  gs.Score("goal", true);
  Entry e = evalhelp::ParseEntry("goals >= x");
  EXPECT_EQ(CheckEntry(e, gs, true), CheckResult::Crash);
}

TEST(RigdioCheck, AllConditionsMustPass) {
  GameState gs;
  gs.Score("goal", true);
  Entry e = evalhelp::ParseEntry("goals == 1;home");
  EXPECT_EQ(CheckEntry(e, gs, true), CheckResult::Yes);
  EXPECT_EQ(CheckEntry(e, gs, false), CheckResult::No);
}

// --- selection (legacy.py PlayerManager.getSong) -------------------------------

namespace pickhelp {

// Deterministic "random": always picks index 0 unless told otherwise.
Rng Fixed(int v = 0) {
  return [v](int n) { return v < n ? v : n - 1; };
}

Picker From(const std::string& fourccm, const std::string& key) {
  ParseResult r = Parse(fourccm, "f");
  EXPECT_TRUE(r.ok) << r.error;
  return Picker(r.team.players.at(key));
}

}  // namespace pickhelp

TEST(RigdioPick, FirstPassingEntryInFileOrder) {
  GameState gs;
  Picker p = pickhelp::From(
      "name;x\n"
      "goal;one.mp3;goals == 1\n"
      "goal;two.mp3;goals == 2\n"
      "goal;any.mp3\n",
      "goal");
  gs.Score("goal", true);
  EXPECT_EQ(p.Pick(gs, true, pickhelp::Fixed())->file, "one.mp3");
  gs.Score("goal", true);
  EXPECT_EQ(p.Pick(gs, true, pickhelp::Fixed())->file, "two.mp3");
  gs.Score("goal", true);
  EXPECT_EQ(p.Pick(gs, true, pickhelp::Fixed())->file, "any.mp3");
}

TEST(RigdioPick, NothingMatchesMeansNothingPlays) {
  GameState gs;
  Picker p = pickhelp::From("name;x\ngoal;one.mp3;goals == 1\n", "goal");
  gs.Score("goal", true);
  gs.Score("goal", true);
  EXPECT_EQ(p.Pick(gs, true, pickhelp::Fixed()), nullptr);
}

TEST(RigdioPick, UnloadRemovesEntryPermanently) {
  GameState gs;
  Picker p = pickhelp::From(
      "name;x\n"
      "goal;once.mp3;once\n"
      "goal;fallback.mp3\n",
      "goal");
  gs.Score("goal", true);
  EXPECT_EQ(p.Pick(gs, true, pickhelp::Fixed())->file, "once.mp3");
  gs.Score("goal", true);
  // Second check of `once` raises UnloadSong: removed, fallback plays...
  EXPECT_EQ(p.Pick(gs, true, pickhelp::Fixed())->file, "fallback.mp3");
  EXPECT_EQ(p.entries().size(), 1u);  // ...and it is gone for good.
}

TEST(RigdioPick, CrashingEntryAbortsTheWholePick) {
  GameState gs;
  Picker p = pickhelp::From(
      "name;x\n"
      "goal;bad.mp3;goals >= x\n"
      "goal;good.mp3\n",
      "goal");
  gs.Score("goal", true);
  // rigdio's eval NameError propagates: nothing plays, nothing is removed.
  EXPECT_EQ(p.Pick(gs, true, pickhelp::Fixed()), nullptr);
  EXPECT_EQ(p.entries().size(), 2u);
}

TEST(RigdioPick, AllRandomisePicksRandomIgnoringConditions) {
  GameState gs;
  Picker p = pickhelp::From(
      "name;x\n"
      "goal;a.mp3;randomise;goals == 5\n"
      "goal;b.mp3;randomise\n"
      "goal;c.mp3;randomise\n",
      "goal");
  gs.Score("goal", true);
  // Conditions are ignored once the all-randomise rule engages: index 0 is
  // returned even though its goals == 5 fails.
  EXPECT_EQ(p.Pick(gs, true, pickhelp::Fixed(0))->file, "a.mp3");
  EXPECT_EQ(p.Pick(gs, true, pickhelp::Fixed(2))->file, "c.mp3");
}

TEST(RigdioPick, PartialRandomiseFallsThroughToPriority) {
  GameState gs;
  Picker p = pickhelp::From(
      "name;x\n"
      "goal;a.mp3;randomise;goals == 1\n"
      "goal;b.mp3;goals == 1\n"
      "goal;c.mp3\n",
      "goal");
  gs.Score("goal", true);
  EXPECT_EQ(p.Pick(gs, true, pickhelp::Fixed(1))->file, "a.mp3");
  gs.Score("goal", true);
  EXPECT_EQ(p.Pick(gs, true, pickhelp::Fixed(1))->file, "c.mp3");
}

TEST(RigdioPick, WarcryPlaysThenChainsToNonWarcry) {
  GameState gs;
  Picker p = pickhelp::From(
      "name;x\n"
      "victory;cry.ogg;warcry\n"
      "victory;anthem.mp3\n",
      "victory");
  // Armed: the warcry wins even though the anthem also passes.
  Entry* first = p.Pick(gs, true, pickhelp::Fixed());
  ASSERT_NE(first, nullptr);
  EXPECT_EQ(first->file, "cry.ogg");
  EXPECT_TRUE(first->warcry);
  // The warcry ended: the chain picks the first non-warcry entry...
  p.warcryArmed = false;
  Entry* second = p.Pick(gs, true, pickhelp::Fixed());
  ASSERT_NE(second, nullptr);
  EXPECT_EQ(second->file, "anthem.mp3");
  // ...and re-arms for the next trigger (PlayerManager sets warcry = True).
  EXPECT_TRUE(p.warcryArmed);
}

TEST(RigdioPick, RandomisedWarcriesPickRandomly) {
  GameState gs;
  Picker p = pickhelp::From(
      "name;x\n"
      "victory;cry1.ogg;warcry;randomise\n"
      "victory;cry2.ogg;warcry;randomise\n"
      "victory;anthem.mp3\n",
      "victory");
  EXPECT_EQ(p.Pick(gs, true, pickhelp::Fixed(1))->file, "cry2.ogg");
}

TEST(RigdioPick, AdvanceSkipsTheEndedEntry) {
  GameState gs;
  Picker p = pickhelp::From(
      "name;x\n"
      "goal;a.mp3;advance\n"
      "goal;b.mp3\n",
      "goal");
  gs.Score("goal", true);
  Entry* a = p.Pick(gs, true, pickhelp::Fixed());
  EXPECT_EQ(a->file, "a.mp3");
  // a.mp3 ended (advance): rerun with skip.
  Entry* b = p.Pick(gs, true, pickhelp::Fixed(), a);
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->file, "b.mp3");
}

TEST(RigdioPick, AdvanceFallsBackToSkippedWhenAlone) {
  GameState gs;
  Picker p = pickhelp::From("name;x\ngoal;a.mp3;advance\n", "goal");
  gs.Score("goal", true);
  Entry* a = p.Pick(gs, true, pickhelp::Fixed());
  ASSERT_NE(a, nullptr);
  // No other entry: the final fallback replays the skipped song itself.
  EXPECT_EQ(p.Pick(gs, true, pickhelp::Fixed(), a), a);
}

TEST(RigdioPick, AdvanceFallbackIgnoresConditions) {
  GameState gs;
  Picker p = pickhelp::From(
      "name;x\n"
      "goal;a.mp3;advance\n"
      "goal;b.mp3;goals == 99\n",
      "goal");
  gs.Score("goal", true);
  Entry* a = p.Pick(gs, true, pickhelp::Fixed());
  // b fails its condition, but the advance fallback takes the first
  // non-warcry entry that isn't the skipped one - conditions unchecked.
  Entry* b = p.Pick(gs, true, pickhelp::Fixed(), a);
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->file, "b.mp3");
}

// --- MatchSession: what the streamer's hands do ------------------------------

namespace sesshelp {

const char* kHome =
    "name;hteam\n"
    "anthem;h_anthem.mp3\n"
    "victory;h_victory.mp3\n"
    "goal;h_goal.mp3\n"
    "chant;h_chant1.mp3\n"
    "chant;h_chant2.mp3;unrandom\n"
    "Scorer;h_scorer.mp3;goals == 1\n"
    "Scorer;h_scorer2.mp3;goals >= 2\n";

const char* kAway =
    "name;ateam\n"
    "anthem;a_anthem.mp3\n"
    "victory;a_victory.mp3\n"
    "goal;a_goal.mp3\n";

MatchSession Make(const std::string& home = kHome,
                  const std::string& away = kAway, int rngPick = 0) {
  ParseResult h = Parse(home, "h");
  ParseResult a = Parse(away, "a");
  EXPECT_TRUE(h.ok) << h.error;
  EXPECT_TRUE(a.ok) << a.error;
  return MatchSession(h.team, a.team, "group",
                      [rngPick](int n) { return rngPick < n ? rngPick : n - 1; });
}

}  // namespace sesshelp

TEST(RigdioSession, TeamNamesAreLoadedIntoState) {
  MatchSession s = sesshelp::Make();
  EXPECT_EQ(s.State().names[0], "hteam");
  EXPECT_EQ(s.State().names[1], "ateam");
}

TEST(RigdioSession, GoalCreditsScorerBeforeSelecting) {
  MatchSession s = sesshelp::Make();
  auto act = s.OnGoal(true, "Scorer", 10, 100.0);
  ASSERT_TRUE(act.has_value());
  EXPECT_EQ(act->file, "h_scorer.mp3");  // goals == 1 passed: counted first
  EXPECT_EQ(s.State().TeamScore(true), 1);
  EXPECT_EQ(s.State().PlayerGoals("Scorer", true), 1);
}

TEST(RigdioSession, ScorerNameMatchIsCaseInsensitive) {
  MatchSession s = sesshelp::Make();
  auto act = s.OnGoal(true, "SCORER", 10, 100.0);
  ASSERT_TRUE(act.has_value());
  EXPECT_EQ(act->file, "h_scorer.mp3");
  // The tally is credited under the .4ccm pname.
  EXPECT_EQ(s.State().PlayerGoals("Scorer", true), 1);
}

TEST(RigdioSession, UnknownScorerUsesTheGoalButton) {
  MatchSession s = sesshelp::Make();
  auto act = s.OnGoal(true, "Nobody", 10, 100.0);
  ASSERT_TRUE(act.has_value());
  EXPECT_EQ(act->file, "h_goal.mp3");
  // Credited as pname "goal", exactly like the streamer's goal button.
  EXPECT_EQ(s.State().PlayerGoals("goal", true), 1);
  EXPECT_EQ(s.State().PlayerGoals("Nobody", true), 0);
  EXPECT_EQ(s.State().TeamScore(true), 1);
}

TEST(RigdioSession, HornResumesAcrossGoals) {
  // THE cross-goal persistence contract: two goals, one file, the second
  // play resumes where the kickoff pause left off.
  MatchSession s = sesshelp::Make();
  s.SetDuration(true, "h_goal.mp3", 300.0);
  auto first = s.OnGoal(true, "Nobody", 5, 100.0);
  ASSERT_TRUE(first.has_value());
  EXPECT_DOUBLE_EQ(first->seekSeconds, 0.0);
  s.OnHornPaused(true, 130.0);  // kicked off 30 s into the horn
  EXPECT_DOUBLE_EQ(s.CachedPosition(true, "h_goal.mp3"), 30.0);
  auto second = s.OnGoal(true, "Nobody", 40, 900.0);
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(second->file, "h_goal.mp3");
  EXPECT_DOUBLE_EQ(second->seekSeconds, 30.0);  // resumed, not restarted
}

TEST(RigdioSession, ResumePositionWrapsAtDuration) {
  MatchSession s = sesshelp::Make();
  s.SetDuration(true, "h_goal.mp3", 60.0);
  s.OnGoal(true, "Nobody", 5, 0.0);
  s.OnHornPaused(true, 130.0);  // 130 s into a 60 s loop -> 10 s
  EXPECT_DOUBLE_EQ(s.CachedPosition(true, "h_goal.mp3"), 10.0);
}

TEST(RigdioSession, SyncNoStillResumesTheSameEntry) {
  // Every entry owns its player for the whole match, so the SAME entry
  // resumes even with sync;no. What sync adds is sharing across different
  // entries of the same file (legacy.py _position_cache comment).
  std::string homeNoSync =
      "name;hteam\nsync;no\ngoal;h_goal.mp3\n";
  MatchSession s = sesshelp::Make(homeNoSync);
  s.SetDuration(true, "h_goal.mp3", 300.0);
  s.OnGoal(true, "Nobody", 5, 100.0);
  s.OnHornPaused(true, 130.0);
  auto second = s.OnGoal(true, "Nobody", 40, 900.0);
  ASSERT_TRUE(second.has_value());
  EXPECT_DOUBLE_EQ(second->seekSeconds, 30.0);  // its own position
}

TEST(RigdioSession, SyncNoDoesNotShareAcrossEntries) {
  std::string homeNoSync =
      "name;hteam\nsync;no\n"
      "goal;same.mp3;goals == 1\n"
      "goal;same.mp3;goals >= 2\n";
  MatchSession s = sesshelp::Make(homeNoSync);
  s.SetDuration(true, "same.mp3", 300.0);
  s.OnGoal(true, "Nobody", 5, 0.0);
  s.OnHornPaused(true, 25.0);
  auto second = s.OnGoal(true, "Nobody", 30, 100.0);
  ASSERT_TRUE(second.has_value());
  EXPECT_DOUBLE_EQ(second->seekSeconds, 0.0);  // a different entry: fresh
}

TEST(RigdioSession, StartInstructionSeeksOnlyFirstPlay) {
  std::string home =
      "name;hteam\ngoal;h_goal.mp3;start 0:30\n";
  MatchSession s = sesshelp::Make(home);
  s.SetDuration(true, "h_goal.mp3", 300.0);
  auto first = s.OnGoal(true, "Nobody", 5, 0.0);
  ASSERT_TRUE(first.has_value());
  EXPECT_DOUBLE_EQ(first->seekSeconds, 30.0);  // the start seek
  s.OnHornPaused(true, 20.0);                  // played 30..50
  auto second = s.OnGoal(true, "Nobody", 20, 100.0);
  ASSERT_TRUE(second.has_value());
  EXPECT_DOUBLE_EQ(second->seekSeconds, 50.0);  // resumed, not re-seeked
}

TEST(RigdioSession, DifferentEntrySameFileSharesPosition) {
  // The cache is keyed by file, not entry (rigdio keys on abspath).
  std::string home =
      "name;hteam\n"
      "goal;same.mp3;goals == 1\n"
      "goal;same.mp3;goals >= 2\n";
  MatchSession s = sesshelp::Make(home);
  s.SetDuration(true, "same.mp3", 300.0);
  s.OnGoal(true, "Nobody", 5, 0.0);
  s.OnHornPaused(true, 25.0);
  auto second = s.OnGoal(true, "Nobody", 30, 100.0);
  ASSERT_TRUE(second.has_value());
  EXPECT_DOUBLE_EQ(second->seekSeconds, 25.0);
}

TEST(RigdioSession, SidesDoNotSharePositionsForSameFilename) {
  // rigdio's cache is keyed by ABSOLUTE path; each side has its own export
  // folder, so identical relative names (goalhorn.mp3 is common) must not
  // share a resume position across teams.
  std::string home = "name;hteam\ngoal;goalhorn.mp3\n";
  std::string away = "name;ateam\ngoal;goalhorn.mp3\n";
  MatchSession s = sesshelp::Make(home, away);
  s.SetDuration(true, "goalhorn.mp3", 300.0);
  s.OnGoal(true, "Nobody", 5, 0.0);
  s.OnHornPaused(true, 30.0);
  EXPECT_DOUBLE_EQ(s.CachedPosition(true, "goalhorn.mp3"), 30.0);
  EXPECT_DOUBLE_EQ(s.CachedPosition(false, "goalhorn.mp3"), 0.0);
  auto awayGoal = s.OnGoal(false, "Nobody", 10, 50.0);
  ASSERT_TRUE(awayGoal.has_value());
  EXPECT_DOUBLE_EQ(awayGoal->seekSeconds, 0.0);  // not the home position
}

TEST(RigdioSession, EndStopClearsThePositionCache) {
  std::string home =
      "name;hteam\ngoal;h_goal.mp3;end stop;start 0:10\n";
  MatchSession s = sesshelp::Make(home);
  s.SetDuration(true, "h_goal.mp3", 300.0);
  auto first = s.OnGoal(true, "Nobody", 5, 0.0);
  EXPECT_DOUBLE_EQ(first->seekSeconds, 10.0);
  EXPECT_FALSE(first->loop);
  // Natural EOF with end stop: reloadSong semantics - cache cleared,
  // first-play seek re-armed, nothing new starts.
  auto after = s.OnHornEnded(true, 250.0);
  EXPECT_FALSE(after.has_value());
  EXPECT_DOUBLE_EQ(s.CachedPosition(true, "h_goal.mp3"), 0.0);
  auto second = s.OnGoal(true, "Nobody", 40, 500.0);
  ASSERT_TRUE(second.has_value());
  EXPECT_DOUBLE_EQ(second->seekSeconds, 10.0);  // start seek runs again
}

TEST(RigdioSession, VictoryWarcryChainsIntoAnthem) {
  std::string home =
      "name;hteam\n"
      "victory;cry.ogg;warcry\n"
      "victory;anthem.mp3\n";
  MatchSession s = sesshelp::Make(home);
  auto cry = s.Victory(true, 0.0);
  ASSERT_TRUE(cry.has_value());
  EXPECT_EQ(cry->file, "cry.ogg");
  EXPECT_TRUE(cry->isWarcry);
  EXPECT_FALSE(cry->loop);
  // The warcry hit EOF: the chain hands over to the anthem.
  auto chained = s.OnHornEnded(true, 8.0);
  ASSERT_TRUE(chained.has_value());
  EXPECT_EQ(chained->file, "anthem.mp3");
}

TEST(RigdioSession, AdvanceEntryChainsOnEnd) {
  std::string home =
      "name;hteam\n"
      "goal;a.mp3;advance\n"
      "goal;b.mp3\n";
  MatchSession s = sesshelp::Make(home);
  auto a = s.OnGoal(true, "Nobody", 5, 0.0);
  ASSERT_TRUE(a.has_value());
  EXPECT_EQ(a->file, "a.mp3");
  EXPECT_TRUE(a->advance);
  auto b = s.OnHornEnded(true, 60.0);
  ASSERT_TRUE(b.has_value());
  EXPECT_EQ(b->file, "b.mp3");
}

TEST(RigdioSession, AnthemAndVictoryPickPerSide) {
  MatchSession s = sesshelp::Make();
  auto away = s.Anthem(false, 0.0);
  ASSERT_TRUE(away.has_value());
  EXPECT_EQ(away->file, "a_anthem.mp3");
  EXPECT_TRUE(away->loop);  // anthems loop in rigdio
  auto home = s.Anthem(true, 30.0);
  ASSERT_TRUE(home.has_value());
  EXPECT_EQ(home->file, "h_anthem.mp3");
  auto vic = s.Victory(true, 0.0);
  ASSERT_TRUE(vic.has_value());
  EXPECT_EQ(vic->file, "h_victory.mp3");
  EXPECT_FALSE(vic->loop);  // victory never loops
}

TEST(RigdioSession, ChantsFireOneAtATimeAndHonourUnrandom) {
  MatchSession s = sesshelp::Make();
  auto c1 = s.Chant(true);
  ASSERT_TRUE(c1.has_value());
  // h_chant2 is unrandom: never in the random pool.
  EXPECT_EQ(c1->file, "h_chant1.mp3");
  EXPECT_FALSE(c1->loop);
  // Denied while one is active.
  EXPECT_FALSE(s.Chant(true).has_value());
  EXPECT_FALSE(s.Chant(false).has_value());
  s.ChantEnded();
  EXPECT_TRUE(s.Chant(true).has_value());
}

TEST(RigdioSession, TeamWithoutChantsFiresNothing) {
  MatchSession s = sesshelp::Make();
  EXPECT_FALSE(s.Chant(false).has_value());  // away has no chants
  // ...and that did not lock the chant slot.
  EXPECT_TRUE(s.Chant(true).has_value());
}

TEST(RigdioSession, EventsFireOncePerMinutePerType) {
  std::string home =
      "name;hteam\n"
      "goal;g.mp3\n"
      "P;oops.mp3;event owngoal\n";
  MatchSession s = sesshelp::Make(home);
  auto e1 = s.OnEvent(true, "owngoal", "P", 10);
  ASSERT_TRUE(e1.has_value());
  EXPECT_EQ(e1->file, "oops.mp3");
  EXPECT_FALSE(e1->loop);
  // Same minute: suppressed (event.py checkAndPlay: etime > last).
  EXPECT_FALSE(s.OnEvent(true, "owngoal", "P", 10).has_value());
  EXPECT_TRUE(s.OnEvent(true, "owngoal", "P", 11).has_value());
  // Unknown player or type: nothing.
  EXPECT_FALSE(s.OnEvent(true, "owngoal", "Q", 20).has_value());
  EXPECT_FALSE(s.OnEvent(true, "red", "P", 20).has_value());
  // Player matching is upper-cased in event.py.
  EXPECT_TRUE(s.OnEvent(true, "owngoal", "p", 30).has_value());
}

TEST(RigdioSession, PauseRestartOverriddenBySyncCache) {
  // Faithful quirk: with sync on, the cache is saved BEFORE the restart
  // seek, so pause;restart has no audible effect (docs/RIGDIO.md section 4).
  std::string home =
      "name;hteam\ngoal;h_goal.mp3;pause restart\n";
  MatchSession s = sesshelp::Make(home);
  s.SetDuration(true, "h_goal.mp3", 300.0);
  s.OnGoal(true, "Nobody", 5, 0.0);
  s.OnHornPaused(true, 40.0);
  auto second = s.OnGoal(true, "Nobody", 30, 100.0);
  ASSERT_TRUE(second.has_value());
  EXPECT_DOUBLE_EQ(second->seekSeconds, 40.0);
}

TEST(RigdioSession, PauseRestartWorksWithSyncNo) {
  std::string home =
      "name;hteam\nsync;no\ngoal;h_goal.mp3;pause restart;start 0:05\n";
  MatchSession s = sesshelp::Make(home);
  s.SetDuration(true, "h_goal.mp3", 300.0);
  auto first = s.OnGoal(true, "Nobody", 5, 0.0);
  EXPECT_DOUBLE_EQ(first->seekSeconds, 5.0);
  s.OnHornPaused(true, 40.0);
  auto second = s.OnGoal(true, "Nobody", 30, 100.0);
  ASSERT_TRUE(second.has_value());
  EXPECT_DOUBLE_EQ(second->seekSeconds, 5.0);  // restarted at start time
}

TEST(RigdioSession, SpeedRidesOnTheAction) {
  std::string home = "name;hteam\ngoal;h_goal.mp3;speed 1.5\n";
  MatchSession s = sesshelp::Make(home);
  s.SetDuration(true, "h_goal.mp3", 300.0);
  auto act = s.OnGoal(true, "Nobody", 5, 0.0);
  ASSERT_TRUE(act.has_value());
  EXPECT_DOUBLE_EQ(act->speed, 1.5);
  // Position advances at playback speed.
  s.OnHornPaused(true, 20.0);
  EXPECT_DOUBLE_EQ(s.CachedPosition(true, "h_goal.mp3"), 30.0);
}

TEST(RigdioCheck, NotPropagatesUnload) {
  GameState gs;
  Entry e = evalhelp::ParseEntry("not once");
  // First check: once yields True -> not -> No.
  EXPECT_EQ(CheckEntry(e, gs, true), CheckResult::No);
  // Second: once raises UnloadSong; it propagates through not.
  EXPECT_EQ(CheckEntry(e, gs, true), CheckResult::Unload);
}


// --- fidelity against the real exports ---------------------------------------
//
// Reads the community exports linked from exports.txt when present (they are
// never committed; set GF_RIGDIO_EXPORTS or keep them in .exports/). Every
// expectation here states what rigdio v2.2.0 itself would do with the file.

namespace fidelity {

namespace fs = std::filesystem;

fs::path ExportRoot() {
  const char* env = std::getenv("GF_RIGDIO_EXPORTS");
  if (env && *env) return fs::path(env);
  return fs::path(TESTDATA_EXPORTS);
}

// The folder holding the given team's .4ccm, or empty.
fs::path FindFourccm(const std::string& team, const std::string& name) {
  fs::path root = ExportRoot() / team;
  std::error_code ec;
  for (fs::recursive_directory_iterator it(root, ec), end; it != end;
       it.increment(ec)) {
    if (ec) break;
    if (it->path().filename() == name) return it->path();
  }
  return {};
}

struct Loaded {
  ParseResult parsed;
  std::vector<std::string> listing;   // folder's direct children
  std::vector<std::string> missing;   // unresolvable files, rigdio-style
};

Loaded Load(const std::string& team, const std::string& name) {
  Loaded out;
  fs::path file = FindFourccm(team, name);
  if (file.empty()) return out;
  std::ifstream in(file);
  std::string text((std::istreambuf_iterator<char>(in)),
                   std::istreambuf_iterator<char>());
  out.parsed = Parse(text, file.stem().string());
  for (const auto& e : fs::directory_iterator(file.parent_path()))
    out.listing.push_back(e.path().filename().string());
  if (out.parsed.ok) {
    auto resolve = [&](const Entry& e) {
      const bool exact = fs::exists(file.parent_path() / e.file);
      const std::string got =
          SongCheck(e.file, exact, out.listing, out.parsed.team.normalize);
      if (!fs::exists(file.parent_path() / got)) out.missing.push_back(e.file);
    };
    // Only the entries rigdio itself would load (players + events), before
    // the goal-fallback duplication skews the count.
    for (const auto& kv : out.parsed.team.players)
      for (const Entry& e : kv.second)
        if (e.pname == kv.first) resolve(e);
    for (const auto& kv : out.parsed.team.events)
      for (const Entry& e : kv.second) resolve(e);
  }
  return out;
}

#define REQUIRE_EXPORT(loaded, team)                                       \
  if ((loaded).listing.empty())                                            \
    GTEST_SKIP() << "export for " << (team) << " not present";

}  // namespace fidelity

TEST(RigdioFidelity, HdgParsesCleanAndComplete) {
  fidelity::Loaded l = fidelity::Load("hdg", "hdg.4ccm");
  REQUIRE_EXPORT(l, "hdg");
  ASSERT_TRUE(l.parsed.ok) << l.parsed.error;
  const TeamMusic& t = l.parsed.team;
  EXPECT_EQ(t.tname, "hdg");
  EXPECT_TRUE(t.sync);
  EXPECT_TRUE(t.normalize);
  EXPECT_EQ(t.players.at("anthem").size(), 1u);
  EXPECT_EQ(t.players.at("victory").size(), 4u);
  EXPECT_EQ(t.players.at("goal").size(), 1u);
  EXPECT_EQ(t.players.at("chant").size(), 7u);
  // 5 named players, each with the goal entry appended.
  EXPECT_EQ(t.players.size(), 4u + 5u);
  EXPECT_EQ(t.players.at("John Helldiver").size(), 2u);
  // start 0:30.5 on the arrowhead horn.
  EXPECT_DOUBLE_EQ(t.players.at("Chief Vagueposting Officer")[0].startSeconds,
                   30.5);
  // Every referenced file resolves (the .mp4 chant EXISTS - decoding it is
  // the engine's documented divergence, not a missing file).
  EXPECT_TRUE(l.missing.empty());
}

TEST(RigdioFidelity, HdgVictoryLadder) {
  fidelity::Loaded l = fidelity::Load("hdg", "hdg.4ccm");
  REQUIRE_EXPORT(l, "hdg");
  ASSERT_TRUE(l.parsed.ok);
  GameState gs;
  Picker p(l.parsed.team.players.at("victory"));
  // Warcry mode: warcry1 wins; specials are false; the anthem waits.
  Entry* cry = p.Pick(gs, true, [](int n) { return 0; });
  ASSERT_NE(cry, nullptr);
  EXPECT_EQ(cry->file, "victory_warcry1.ogg");
  // The warcry ends: the chain must land on victory_anthem.mp3, skipping
  // the two special warcries.
  p.warcryArmed = false;
  Entry* anthem = p.Pick(gs, true, [](int n) { return 0; });
  ASSERT_NE(anthem, nullptr);
  EXPECT_EQ(anthem->file, "victory_anthem.mp3");
}

TEST(RigdioFidelity, TwoHugParsesCleanAndLaddersByGoals) {
  fidelity::Loaded l = fidelity::Load("2hug", "2hug.4ccm");
  REQUIRE_EXPORT(l, "2hug");
  ASSERT_TRUE(l.parsed.ok) << l.parsed.error;
  const TeamMusic& t = l.parsed.team;
  EXPECT_EQ(t.tname, "2hug");
  EXPECT_EQ(t.players.at("goal").size(), 3u);
  EXPECT_EQ(t.players.at("chant").size(), 6u);
  EXPECT_TRUE(l.missing.empty()) << l.missing[0];

  // Pls Rember's ladder: goals == 1 / == 2 / >= 3, then the goal fallback.
  GameState gs;
  Picker p(t.players.at("Pls Rember"));
  auto rng = [](int n) { return 0; };
  gs.Score("Pls Rember", true);
  EXPECT_EQ(p.Pick(gs, true, rng)->file, "pls rember Goalhorn.mp3");
  gs.Score("Pls Rember", true);
  EXPECT_EQ(p.Pick(gs, true, rng)->file, "Rember1.mp3");
  gs.Score("Pls Rember", true);
  EXPECT_EQ(p.Pick(gs, true, rng)->file, "Rember2.mp3");
  gs.Score("Pls Rember", true);
  EXPECT_EQ(p.Pick(gs, true, rng)->file, "Rember2.mp3");  // >= 3 still true
}

TEST(RigdioFidelity, TwoHugTenguPartialRandomiseIsPriority) {
  // Only 2 of Tengu Thursday's 4 entries carry randomise: the all-randomise
  // rule must NOT engage; file order decides.
  fidelity::Loaded l = fidelity::Load("2hug", "2hug.4ccm");
  REQUIRE_EXPORT(l, "2hug");
  ASSERT_TRUE(l.parsed.ok);
  GameState gs;
  Picker p(l.parsed.team.players.at("Tengu Thursday"));
  gs.Score("Tengu Thursday", true);
  EXPECT_EQ(p.Pick(gs, true, [](int n) { return 1; })->file, "tengu 1.mp3");
}

TEST(RigdioFidelity, DbgVgl26MissingFilesMatchRigdio) {
  fidelity::Loaded l = fidelity::Load("dbg", "dbgvgl26.4ccm");
  REQUIRE_EXPORT(l, "dbg");
  ASSERT_TRUE(l.parsed.ok) << l.parsed.error;
  const TeamMusic& t = l.parsed.team;
  EXPECT_EQ(t.tname, "dbg");
  EXPECT_EQ(t.players.at("anthem").size(), 5u);
  EXPECT_EQ(t.players.at("victory").size(), 6u);
  EXPECT_EQ(t.players.at("chant").size(), 26u);
  // rigdio would REFUSE this export: exactly these three files are
  // unresolvable (docs/RIGDIO.md divergence #1 logs-and-continues).
  EXPECT_EQ(l.missing,
            (std::vector<std::string>{"gogetagoalhorn.mp3",
                                      "Gogeta_Da_Brace_Horn_2021.mp3",
                                      "GogetaDaHattrickHorn.mp3",
                                      "gogetagoalhorn.mp3"}));
}

TEST(RigdioFidelity, DbgAnthemsAllRandomise) {
  fidelity::Loaded l = fidelity::Load("dbg", "dbgvgl26.4ccm");
  REQUIRE_EXPORT(l, "dbg");
  ASSERT_TRUE(l.parsed.ok);
  GameState gs;
  Picker p(l.parsed.team.players.at("anthem"));
  // All five anthems carry randomise: any index is reachable.
  EXPECT_EQ(p.Pick(gs, true, [](int n) { return 4; })->file,
            "Anthem - Tenkaichi 3.mp3");
  EXPECT_EQ(p.Pick(gs, true, [](int n) { return 0; })->file,
            "Anthem - Budokai 3 (Main).mp3");
}

TEST(RigdioFidelity, DbgSpecialVictoryAnthemsNeverAutoPlay) {
  fidelity::Loaded l = fidelity::Load("dbg", "dbgvgl26.4ccm");
  REQUIRE_EXPORT(l, "dbg");
  ASSERT_TRUE(l.parsed.ok);
  GameState gs;
  Picker p(l.parsed.team.players.at("victory"));
  // The default VA is first in file order; the five MVP specials are false.
  Entry* e = p.Pick(gs, true, [](int n) { return 0; });
  ASSERT_NE(e, nullptr);
  EXPECT_EQ(e->file, "VA - Dan Dan Kokoro Hikareteku.mp3");
}
}  // namespace
