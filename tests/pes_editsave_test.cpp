#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "pes/editsave.hpp"

namespace {

std::string Hex(const std::array<uint8_t, 64>& d) {
  static const char* digits = "0123456789abcdef";
  std::string s;
  for (uint8_t b : d) s += digits[b >> 4], s += digits[b & 15];
  return s;
}

}  // namespace

// FIPS 180-2 test vectors.
TEST(Sha512, MatchesTheStandardVectors) {
  const std::string abc = "abc";
  EXPECT_EQ(Hex(pes::Sha512(std::span(reinterpret_cast<const uint8_t*>(abc.data()), abc.size()))),
            "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
            "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");
  EXPECT_EQ(Hex(pes::Sha512({})),
            "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce"
            "47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e");
  // Two blocks: 112 bytes forces the length into a second block.
  const std::string two =
      "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";
  EXPECT_EQ(Hex(pes::Sha512(std::span(reinterpret_cast<const uint8_t*>(two.data()), two.size()))),
            "8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299aeadb6889018"
            "501d289e4900f7e4331b99dec4b5433ac7d329eeb6dd26545e96e55b874be909");
}

// Matsumoto & Nishimura's mt19937ar.c reference output for
// init_by_array({0x123, 0x234, 0x345, 0x456}).
TEST(Mt19937, InitByArrayMatchesTheReference) {
  pes::Mt19937 mt(std::vector<uint32_t>{0x123, 0x234, 0x345, 0x456});
  EXPECT_EQ(mt.Next(), 1067595299u);
  EXPECT_EQ(mt.Next(), 955945823u);
  EXPECT_EQ(mt.Next(), 477289528u);
}

// The cipher is its own inverse (FUN_14140e0f0 serves both directions), and a
// 1-3 byte tail is covered.
TEST(CryptStream, IsAnInvolutionIncludingTheTail) {
  std::array<uint8_t, 64> key{};
  for (size_t i = 0; i < key.size(); ++i) key[i] = uint8_t(i * 7 + 3);
  std::vector<uint8_t> plain(4 * 9 + 3);
  for (size_t i = 0; i < plain.size(); ++i) plain[i] = uint8_t(i);
  auto once = pes::CryptStream(key, plain);
  EXPECT_NE(once, plain);
  EXPECT_EQ(pes::CryptStream(key, once), plain);
}

// Body +0x08: sleeves bits 6-7 (0 = default short, else value - 1), socks
// bits 10-11, shirt tail bit 14; body +0x20 bits 8-10 skin; X bits 4-17 boots,
// 18-31 glove (pes-kit-assembly.md 8.1).
TEST(Appearance, DecodesTheLookFields) {
  std::array<uint8_t, pes::kAppearanceRecordBytes> rec{};
  const uint32_t id = 76711, x = (1386u << 4) | (12u << 18);
  const uint32_t body08 = (2u << 6) | (2u << 10) | (1u << 14), body20 = 5u << 8;
  std::memcpy(&rec[0], &id, 4);
  std::memcpy(&rec[4], &x, 4);
  std::memcpy(&rec[0xC + 0x08], &body08, 4);
  std::memcpy(&rec[0xC + 0x20], &body20, 4);
  const pes::PlayerLook look = pes::DecodeLook(rec);
  EXPECT_TRUE(look.longSleeves);
  EXPECT_EQ(look.socks, 2);
  EXPECT_TRUE(look.shirtTailOut);
  EXPECT_EQ(look.skin, 5);
  EXPECT_EQ(look.bootsId, 1386);
  EXPECT_EQ(look.gloveId, 12);
}

// The user's own save, given at run time only. The game checks a SHA-512 of
// every block against the decrypted header; so does the reader.
TEST(EditSaveInstall, DecryptsAndMatchesTheRunningGame) {
  const char* path = std::getenv("PES21_EDIT_SAVE");
  if (!path) GTEST_SKIP() << "PES21_EDIT_SAVE not set";
  std::ifstream f(path, std::ios::binary);
  ASSERT_TRUE(f) << "cannot open PES21_EDIT_SAVE";
  const std::vector<uint8_t> file((std::istreambuf_iterator<char>(f)), {});
  std::string err;
  auto save = pes::ReadEditSave(file, &err);
  ASSERT_TRUE(save) << err;
  EXPECT_EQ(save->appearances.size(), 4830u);
  // Read live from the game (pes-kit-assembly.md 7.4): short sleeves, socks 0,
  // shirt out, no glove, boots id 0 (-> BootsList default).
  auto it = save->appearances.find(76711);
  ASSERT_NE(it, save->appearances.end());
  const pes::PlayerLook look = pes::DecodeLook(it->second);
  EXPECT_FALSE(look.longSleeves);
  EXPECT_EQ(look.socks, 0);
  EXPECT_TRUE(look.shirtTailOut);
  EXPECT_EQ(look.gloveId, 0);
  EXPECT_EQ(look.bootsId, 0);
  // Seen in a live match: 75403 in long sleeves; 75206 with glove 12, boots 38.
  ASSERT_TRUE(save->appearances.count(75403) && save->appearances.count(75206));
  EXPECT_TRUE(pes::DecodeLook(save->appearances.at(75403)).longSleeves);
  EXPECT_EQ(pes::DecodeLook(save->appearances.at(75206)).gloveId, 12);
  EXPECT_EQ(pes::DecodeLook(save->appearances.at(75206)).bootsId, 38);
}

// PlayerAppearance.bin: 60-byte records (u32 id + the 56-byte body). The game's
// live table holds them as 72-byte records with X and the alternate id zero.
TEST(PlayerAppearanceFile, BecomesRecordsWithNoBootsOrGlove) {
  std::vector<uint8_t> blob(2 * pes::kAppearanceFileRecordBytes, 0);
  const uint32_t id0 = 70101, id1 = 70102, look1 = (2u << 6) | (1u << 14);
  std::memcpy(&blob[0], &id0, 4);
  std::memcpy(&blob[pes::kAppearanceFileRecordBytes], &id1, 4);
  std::memcpy(&blob[pes::kAppearanceFileRecordBytes + 4 + 0x08], &look1, 4);  // body +0x08
  std::string err;
  auto table = pes::ParsePlayerAppearanceFile(blob, &err);
  ASSERT_TRUE(table) << err;
  ASSERT_EQ(table->size(), 2u);
  const pes::PlayerLook look = pes::DecodeLook(table->at(id1));
  EXPECT_TRUE(look.longSleeves);
  EXPECT_TRUE(look.shirtTailOut);
  EXPECT_EQ(look.bootsId, 0);
}

TEST(PlayerAppearanceFile, RejectsPartialRecords) {
  std::vector<uint8_t> blob(pes::kAppearanceFileRecordBytes + 1, 0);
  std::string err;
  EXPECT_FALSE(pes::ParsePlayerAppearanceFile(blob, &err));
}
