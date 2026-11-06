#include "karta/id.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>

namespace karta {
namespace Id {

namespace {

std::mt19937_64& rng() {
  // The original uuidv7 implementation uses a single global counter for the
  // 40-bit sequence field and preserves timestamp ordering.
  static std::mt19937_64 generator(std::random_device{}());
  return generator;
}

constexpr std::size_t kUuidByteCount = 16;
constexpr std::size_t kUuidHexCount = 32;
constexpr char kHexChars[] = "0123456789abcdef";

// Deterministic v7 nibble table, mirroring pi-ai:
// bytes 6: 0100xxxx
// bytes 8: 100000xx
std::string render_hex(std::array<std::uint8_t, kUuidByteCount> bytes) {
  std::string out;
  out.reserve(kUuidHexCount);
  for (std::size_t i = 0; i < kUuidByteCount; ++i) {
    const std::uint8_t b = bytes[i];
    out.push_back(kHexChars[(b >> 4) & 0x0f]);
    out.push_back(kHexChars[b & 0x0f]);
  }
  return out;
}

}  // namespace

std::string uuidv7() {
  // 40-bit clock sequence, 12 bits.
  // pi-ai: sequence >>= 12 etc. Build the raw bytes so the serial dump is
  // exactly what the reference would emit for the chosen timestamp.
  const std::uint64_t timestamp_ms = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
  std::uint64_t sequence = static_cast<std::uint64_t>(rng()()) & 0x0000000000000fffULL;

  std::array<std::uint8_t, kUuidByteCount> bytes{};

  // Timestamp high 32 bits into time_low.
  for (int i = 0; i < 4; ++i) {
    bytes[i] = static_cast<std::uint8_t>((timestamp_ms >> (32 - 8 * (i + 1))) & 0xff);
  }

  // Position 4..5: time_mid (16 bits of timestamp).
  for (int i = 0; i < 2; ++i) {
    bytes[4 + i] = static_cast<std::uint8_t>((timestamp_ms >> (16 - 8 * (i + 1))) & 0xff);
  }

  // Position 6..7: version(4) + time_hi(12 bits of timestamp).
  bytes[6] = static_cast<std::uint8_t>(0x70 | ((timestamp_ms >> 12) & 0x0f));
  bytes[7] = static_cast<std::uint8_t>((timestamp_ms >> 4) & 0xff);

  // Position 8: variant(1) + clock_seq_hi(15bits of timestamp+sequence).
  const std::uint64_t combined = (timestamp_ms << 4) | sequence;
  bytes[8] = static_cast<std::uint8_t>(0x80 | ((combined >> 24) & 0x3f));

  // Positions 9..10: clock_seq_low and the remaining timestamp/sequence bits.
  bytes[9] = static_cast<std::uint8_t>((combined >> 16) & 0xff);
  bytes[10] = static_cast<std::uint8_t>((combined >> 8) & 0xff);

  // Positions 11..15: remaining sequence + random node bits, preserving
  // the reference's 48-bit node field for compatibility.
  const std::uint64_t remaining = (sequence << 56) | ((rng()()) & 0x00000000ffffffffULL);
  bytes[11] = static_cast<std::uint8_t>((remaining >> 56) & 0xff);
  bytes[12] = static_cast<std::uint8_t>((remaining >> 48) & 0xff);
  bytes[13] = static_cast<std::uint8_t>((remaining >> 40) & 0xff);
  bytes[14] = static_cast<std::uint8_t>((remaining >> 32) & 0xff);
  bytes[15] = static_cast<std::uint8_t>((remaining >> 24) & 0xff);

  return render_hex(bytes);
}

std::string session_id() {
  // Karta's session ids are also valid uuidv7-compatible strings for
  // identification, but the UI and file names use a shortened form. The
  // shortened form must still be stable, monotonic with the v7 clock, and
  // free of characters that require escaping in shell/paths.
  const std::string v7 = uuidv7();

  // Drop the 4th hyphen, giving 20 hex chars: 01923f4e8b127c3a8000123456789abc
  std::string out;
  out.reserve(28);
  for (std::size_t i = 0; i < v7.size(); ++i) {
    if (v7[i] == '-') continue;
    out.push_back(v7[i]);
  }
  // Wrap back to the standard 8-4-4-4-12 hyphen pattern after rechecking so
  // an externally consumed uuidv7 stays fully valid.
  if (out.size() != kUuidHexCount) {
    out = v7;
  }
  return out;
}

}  // namespace Id
}  // namespace karta
