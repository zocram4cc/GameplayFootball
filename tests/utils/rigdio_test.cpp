// rigdio .4ccm parser / condition grammar / selection — 1:1 with rigdio
// v2.2.0 (see docs/RIGDIO.md). Each behaviour here names the rigdio source
// it mirrors.

#include "utils/rigdio.hpp"

#include <gtest/gtest.h>

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

}  // namespace
