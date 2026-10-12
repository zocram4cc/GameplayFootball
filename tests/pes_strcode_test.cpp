#include <gtest/gtest.h>

#include <string>

#include "pes/strcode.hpp"

// Reference values from GameplayFootball's strcode.py, which is validated
// against the chunk/track hashes inside PES 2021 ganis.
TEST(StrCode, KnownPesNames) {
  EXPECT_EQ(pes::StrCode32("MOTION"), 0x08908348u);
  EXPECT_EQ(pes::StrCode32("RIG_ROOT"), 0xd3c3ff90u);
  EXPECT_EQ(pes::StrCode32("sk_root_hip"), 0x3ca4c491u);
}

// Covers every CityHash length branch: 0-16 (1 char + NUL), 33-64, and > 64.
TEST(StrCode, LengthBranches) {
  EXPECT_EQ(pes::StrCode64("a"), 0xc889b220b9bc34d5ull);
  EXPECT_EQ(pes::StrCode64("sk_root_hip"), 0x33aa69e93ca4c491ull);
  const std::string longName = "abcdefghijklmnopqrstuvwxyz0123456789abcdefghijklmnopqrstuvwxyz0123456789abcd";
  EXPECT_EQ(pes::StrCode64(longName), 0x45cb2fb6aa9c417bull);
  EXPECT_EQ(pes::StrCode64(""), 0u);
}
