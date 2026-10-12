// PES 2021 EDIT save reader. Cipher, keys and layout: docs/re/pes-kit-assembly.md
// 8.1 (FUN_14140e0f0 cryptStream, FUN_14140ea40 keys, FUN_14140f470 load).
#include "pes/editsave.hpp"

#include <bit>
#include <cstring>

namespace pes {
namespace {

template <typename T>
T Load(const uint8_t* p) { T v; std::memcpy(&v, p, sizeof v); return v; }

// --- SHA-512 (FIPS 180-4) ---
constexpr uint64_t SHA512_K[80] = {
    0x428a2f98d728ae22, 0x7137449123ef65cd, 0xb5c0fbcfec4d3b2f, 0xe9b5dba58189dbbc, 0x3956c25bf348b538,
    0x59f111f1b605d019, 0x923f82a4af194f9b, 0xab1c5ed5da6d8118, 0xd807aa98a3030242, 0x12835b0145706fbe,
    0x243185be4ee4b28c, 0x550c7dc3d5ffb4e2, 0x72be5d74f27b896f, 0x80deb1fe3b1696b1, 0x9bdc06a725c71235,
    0xc19bf174cf692694, 0xe49b69c19ef14ad2, 0xefbe4786384f25e3, 0x0fc19dc68b8cd5b5, 0x240ca1cc77ac9c65,
    0x2de92c6f592b0275, 0x4a7484aa6ea6e483, 0x5cb0a9dcbd41fbd4, 0x76f988da831153b5, 0x983e5152ee66dfab,
    0xa831c66d2db43210, 0xb00327c898fb213f, 0xbf597fc7beef0ee4, 0xc6e00bf33da88fc2, 0xd5a79147930aa725,
    0x06ca6351e003826f, 0x142929670a0e6e70, 0x27b70a8546d22ffc, 0x2e1b21385c26c926, 0x4d2c6dfc5ac42aed,
    0x53380d139d95b3df, 0x650a73548baf63de, 0x766a0abb3c77b2a8, 0x81c2c92e47edaee6, 0x92722c851482353b,
    0xa2bfe8a14cf10364, 0xa81a664bbc423001, 0xc24b8b70d0f89791, 0xc76c51a30654be30, 0xd192e819d6ef5218,
    0xd69906245565a910, 0xf40e35855771202a, 0x106aa07032bbd1b8, 0x19a4c116b8d2d0c8, 0x1e376c085141ab53,
    0x2748774cdf8eeb99, 0x34b0bcb5e19b48a8, 0x391c0cb3c5c95a63, 0x4ed8aa4ae3418acb, 0x5b9cca4f7763e373,
    0x682e6ff3d6b2b8a3, 0x748f82ee5defb2fc, 0x78a5636f43172f60, 0x84c87814a1f0ab72, 0x8cc702081a6439ec,
    0x90befffa23631e28, 0xa4506cebde82bde9, 0xbef9a3f7b2c67915, 0xc67178f2e372532b, 0xca273eceea26619c,
    0xd186b8c721c0c207, 0xeada7dd6cde0eb1e, 0xf57d4f7fee6ed178, 0x06f067aa72176fba, 0x0a637dc5a2c898a6,
    0x113f9804bef90dae, 0x1b710b35131c471b, 0x28db77f523047d84, 0x32caab7b40c72493, 0x3c9ebe0a15c9bebc,
    0x431d67c49c100d4c, 0x4cc5d4becb3e42b6, 0x597f299cfc657e2a, 0x5fcb6fab3ad6faec, 0x6c44198c4a475817};
constexpr uint64_t SHA512_IV[8] = {0x6a09e667f3bcc908, 0xbb67ae8584caa73b, 0x3c6ef372fe94f82b,
                                   0xa54ff53a5f1d36f1, 0x510e527fade682d1, 0x9b05688c2b3e6c1f,
                                   0x1f83d9abfb41bd6b, 0x5be0cd19137e2179};
constexpr size_t SHA512_BLOCK = 128;

uint64_t LoadBe64(const uint8_t* p) {
  uint64_t v = 0;
  for (int i = 0; i < 8; ++i) v = v << 8 | p[i];
  return v;
}

void Compress(uint64_t h[8], const uint8_t* block) {
  uint64_t w[80];
  for (int t = 0; t < 16; ++t) w[t] = LoadBe64(block + 8 * t);
  for (int t = 16; t < 80; ++t) {
    const uint64_t s0 = std::rotr(w[t - 15], 1) ^ std::rotr(w[t - 15], 8) ^ (w[t - 15] >> 7);
    const uint64_t s1 = std::rotr(w[t - 2], 19) ^ std::rotr(w[t - 2], 61) ^ (w[t - 2] >> 6);
    w[t] = w[t - 16] + s0 + w[t - 7] + s1;
  }
  uint64_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7];
  for (int t = 0; t < 80; ++t) {
    const uint64_t t1 = k + (std::rotr(e, 14) ^ std::rotr(e, 18) ^ std::rotr(e, 41)) + ((e & f) ^ (~e & g)) +
                        SHA512_K[t] + w[t];
    const uint64_t t2 = (std::rotr(a, 28) ^ std::rotr(a, 34) ^ std::rotr(a, 39)) + ((a & b) ^ (a & c) ^ (b & c));
    k = g, g = f, f = e, e = d + t1, d = c, c = b, b = a, a = t1 + t2;
  }
  h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e, h[5] += f, h[6] += g, h[7] += k;
}

// --- the save cipher ---
constexpr size_t KEY_BYTES = 64;
constexpr size_t ENCRYPTION_HEADER = 0x100, SALT = 0x40, FILE_HEADER = 0xD0;
constexpr size_t FILE_HEADER_AT = ENCRYPTION_HEADER + SALT;  // 0x140
constexpr size_t BLOCKS_AT = FILE_HEADER_AT + FILE_HEADER;   // 0x210
constexpr size_t DATA_SIZE = 0x40, LOGO_SIZE = 0x44, DESC_SIZE = 0x48, SERIAL_UNITS = 0x4C;
constexpr uint64_t FILE_HEADER_KEY_SALT = 0xD0;  // R xor 0xD0
enum Block { DESCRIPTION = 0, LOGO = 1, DATA = 2, SERIAL = 3, BLOCK_COUNT = 4 };
// FUN_1414a18c0: the title whose SHA-512 keys the encryption header.
constexpr char16_t SAVE_TITLE[] = u"eFootball PES 2021 SEASON UPDATE";

// data.dat: player count at +0x60, records from +0x7C, 0x138 bytes each, the
// appearance record at +0xF0 of each.
constexpr size_t PLAYER_COUNT = 0x60, PLAYERS_AT = 0x7C, PLAYER_STRIDE = 0x138, APPEARANCE_AT = 0xF0;

std::array<uint8_t, KEY_BYTES> XorWord(std::array<uint8_t, KEY_BYTES> key, uint64_t n) {
  for (size_t i = 0; i < KEY_BYTES; i += 8) {
    uint64_t w = Load<uint64_t>(&key[i]) ^ n;
    std::memcpy(&key[i], &w, 8);
  }
  return key;
}

}  // namespace

std::array<uint8_t, 64> Sha512(std::span<const uint8_t> data) {
  uint64_t h[8];
  std::memcpy(h, SHA512_IV, sizeof h);
  size_t whole = data.size() / SHA512_BLOCK * SHA512_BLOCK;
  for (size_t i = 0; i < whole; i += SHA512_BLOCK) Compress(h, data.data() + i);
  uint8_t tail[2 * SHA512_BLOCK] = {};
  const size_t rest = data.size() - whole;
  if (rest) std::memcpy(tail, data.data() + whole, rest);
  tail[rest] = 0x80;
  const size_t tailBytes = rest + 1 + 16 <= SHA512_BLOCK ? SHA512_BLOCK : 2 * SHA512_BLOCK;
  const uint64_t bits = uint64_t(data.size()) * 8;  // high 64 bits of the length stay 0
  for (int i = 0; i < 8; ++i) tail[tailBytes - 1 - i] = uint8_t(bits >> (8 * i));
  for (size_t i = 0; i < tailBytes; i += SHA512_BLOCK) Compress(h, tail + i);
  std::array<uint8_t, 64> out;
  for (int i = 0; i < 8; ++i)
    for (int j = 0; j < 8; ++j) out[8 * i + j] = uint8_t(h[i] >> (56 - 8 * j));
  return out;
}

// mt19937ar.c init_genrand / init_by_array / genrand_int32.
Mt19937::Mt19937(const std::vector<uint32_t>& key) {
  constexpr uint32_t INIT_SEED = 19650218u;
  state_[0] = INIT_SEED;
  for (size_t i = 1; i < 624; ++i) state_[i] = 1812433253u * (state_[i - 1] ^ (state_[i - 1] >> 30)) + uint32_t(i);
  size_t i = 1, j = 0;
  for (size_t k = std::max<size_t>(624, key.size()); k; --k) {
    state_[i] = (state_[i] ^ ((state_[i - 1] ^ (state_[i - 1] >> 30)) * 1664525u)) + key[j] + uint32_t(j);
    if (++i >= 624) state_[0] = state_[623], i = 1;
    if (++j >= key.size()) j = 0;
  }
  for (size_t k = 623; k; --k) {
    state_[i] = (state_[i] ^ ((state_[i - 1] ^ (state_[i - 1] >> 30)) * 1566083941u)) - uint32_t(i);
    if (++i >= 624) state_[0] = state_[623], i = 1;
  }
  state_[0] = 0x80000000u;
}

uint32_t Mt19937::Next() {
  if (index_ >= 624) {
    for (size_t k = 0; k < 624; ++k) {
      const uint32_t y = (state_[k] & 0x80000000u) | (state_[(k + 1) % 624] & 0x7fffffffu);
      state_[k] = state_[(k + 397) % 624] ^ (y >> 1) ^ ((y & 1) ? 0x9908b0dfu : 0);
    }
    index_ = 0;
  }
  uint32_t y = state_[index_++];
  y ^= y >> 11;
  y ^= (y << 7) & 0x9d2c5680u;
  y ^= (y << 15) & 0xefc60000u;
  return y ^ (y >> 18);
}

std::vector<uint8_t> CryptStream(const std::array<uint8_t, 64>& key, std::span<const uint8_t> in) {
  std::vector<uint32_t> seed(KEY_BYTES / 4);
  for (size_t i = 0; i < seed.size(); ++i) seed[i] = Load<uint32_t>(&key[4 * i]);
  Mt19937 mt(seed);
  uint32_t c0 = mt.Next(), c1 = mt.Next(), c2 = mt.Next(), c3 = mt.Next();
  auto mix = [&] {
    const uint32_t c4 = mt.Next(), k = c0 ^ c1 ^ c2 ^ c3 ^ c4;
    c0 = std::rotr(c1, 15), c1 = std::rotl(c2, 11), c2 = std::rotl(c3, 7), c3 = std::rotr(c4, 13);
    return k;
  };
  std::vector<uint8_t> out(in.begin(), in.end());
  const size_t words = in.size() / 4;
  for (size_t i = 0; i < words; ++i) {
    const uint32_t w = Load<uint32_t>(&in[4 * i]) ^ mix();
    std::memcpy(&out[4 * i], &w, 4);
  }
  if (in.size() % 4) {
    const uint32_t k = mix();
    for (size_t i = 4 * words; i < in.size(); ++i) out[i] ^= uint8_t(k >> (8 * (i - 4 * words)));
  }
  return out;
}

std::optional<EditSave> ReadEditSave(std::span<const uint8_t> file, std::string* error) {
  if (file.size() < BLOCKS_AT) {
    *error = "EDIT save: shorter than its headers";
    return std::nullopt;
  }
  // K_hdr = salt xor SHA-512(UTF-16LE title).
  std::vector<uint8_t> title;
  for (char16_t c : std::u16string_view(SAVE_TITLE)) title.push_back(uint8_t(c)), title.push_back(uint8_t(c >> 8));
  std::array<uint8_t, KEY_BYTES> key = Sha512(title);
  for (size_t i = 0; i < KEY_BYTES; ++i) key[i] ^= file[ENCRYPTION_HEADER + i];
  std::vector<uint8_t> head = CryptStream(key, file.subspan(0, FILE_HEADER_AT));
  std::memcpy(&head[ENCRYPTION_HEADER], &file[ENCRYPTION_HEADER], SALT);  // the salt stays plain
  // R = the 0x140 header bytes folded into 64 by XOR.
  std::array<uint8_t, KEY_BYTES> root{};
  for (size_t i = 0; i < head.size(); ++i) root[i % KEY_BYTES] ^= head[i];

  const std::vector<uint8_t> fileHeader =
      CryptStream(XorWord(root, FILE_HEADER_KEY_SALT), file.subspan(FILE_HEADER_AT, FILE_HEADER));
  const size_t sizes[BLOCK_COUNT] = {Load<uint32_t>(&fileHeader[DESC_SIZE]), Load<uint32_t>(&fileHeader[LOGO_SIZE]),
                                     Load<uint32_t>(&fileHeader[DATA_SIZE]),
                                     size_t(Load<uint32_t>(&fileHeader[SERIAL_UNITS])) * 2};
  size_t at = BLOCKS_AT;
  std::vector<uint8_t> data;
  for (int b = 0; b < BLOCK_COUNT; ++b) {
    if (sizes[b] > file.size() - at) {
      *error = "EDIT save: block " + std::to_string(b) + " runs past the end (wrong key or not a PES 2021 save)";
      return std::nullopt;
    }
    std::vector<uint8_t> plain = CryptStream(XorWord(root, uint64_t(b)), file.subspan(at, sizes[b]));
    // The game checks each block against the SHA-512 stored in the header.
    if (Sha512(plain) != *reinterpret_cast<const std::array<uint8_t, 64>*>(&head[KEY_BYTES * b])) {
      *error = "EDIT save: block " + std::to_string(b) + " fails its SHA-512 check";
      return std::nullopt;
    }
    if (b == DATA) data = std::move(plain);
    at += sizes[b];
  }

  if (data.size() < PLAYERS_AT) {
    *error = "EDIT save: data block too short";
    return std::nullopt;
  }
  const uint32_t players = Load<uint32_t>(&data[PLAYER_COUNT]);
  if (players > (data.size() - PLAYERS_AT) / PLAYER_STRIDE) {
    *error = "EDIT save: player table runs past the data block";
    return std::nullopt;
  }
  EditSave save;
  for (uint32_t i = 0; i < players; ++i) {
    const uint8_t* rec = &data[PLAYERS_AT + PLAYER_STRIDE * i];
    AppearanceRecord appearance;
    std::memcpy(appearance.data(), rec + APPEARANCE_AT, kAppearanceRecordBytes);
    save.appearances.emplace(Load<uint32_t>(rec), appearance);
  }
  return save;
}

std::optional<AppearanceTable> ParsePlayerAppearanceFile(std::span<const uint8_t> blob, std::string* error) {
  constexpr size_t ID_BYTES = 4, BODY_AT = 0xC;  // record: id, X = 0, alternate id = 0, body
  if (blob.size() % kAppearanceFileRecordBytes != 0) {
    *error = "PlayerAppearance.bin: size is not whole 60-byte records";
    return std::nullopt;
  }
  AppearanceTable table;
  for (size_t at = 0; at < blob.size(); at += kAppearanceFileRecordBytes) {
    AppearanceRecord r{};
    std::memcpy(&r[0], &blob[at], ID_BYTES);
    std::memcpy(&r[BODY_AT], &blob[at + ID_BYTES], kAppearanceFileRecordBytes - ID_BYTES);
    table.emplace(Load<uint32_t>(&blob[at]), r);
  }
  return table;
}

// FUN_141ec4560 / FUN_141ad9220. Body = record + 0xC.
PlayerLook DecodeLook(const AppearanceRecord& r) {
  constexpr size_t X = 4, BODY = 0xC, LOOK_WORD = BODY + 0x08, SKIN_WORD = BODY + 0x20;
  const uint32_t x = Load<uint32_t>(&r[X]), look = Load<uint32_t>(&r[LOOK_WORD]), skin = Load<uint32_t>(&r[SKIN_WORD]);
  PlayerLook out;
  const uint32_t sleeves = (look >> 6) & 3;  // 0 = the caller's default (short), else value - 1
  out.longSleeves = sleeves == 2;
  out.socks = int((look >> 10) & 3);
  out.shirtTailOut = (look >> 14) & 1;
  out.skin = int((skin >> 8) & 7);
  out.bootsId = int((x >> 4) & 0x3fff);
  out.gloveId = int(x >> 18);
  return out;
}

}  // namespace pes
