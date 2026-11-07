// Karta — Id generation (implementation).
//
// C++17 port of pi's uuidv7() from @earendil-works/pi-ai.
// Time-ordered UUID version 7: 48-bit millisecond timestamp, 41-bit clock
// sequence, version nibble 0100b (7), variant bits 100000b.
//
// Produces strings like "01923f4e-8b12-7c3a-8000-123456789abc".
//
// session_id() returns a shorter url-safe v7-derived id (dashes removed) for
// --session-id/--fork and CLI-visible session identifiers.

#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <random>
#include <string>

#include "karta/id.h"

namespace karta {
namespace Id {

namespace {

long long last_ordinary_timestamp = -1;
uint64_t sequence = 0;
bool sequence_initialized = false;

std::string hex_byte(uint8_t b) {
  static const char* digits = "0123456789abcdef";
  std::string out(2, '0');
  out[0] = digits[(b >> 4) & 0x0f];
  out[1] = digits[b & 0x0f];
  return out;
}

}  // namespace

std::string uuidv7() {
  using namespace std::chrono;
  long long now_ms =
      duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
  if (now_ms < 0) now_ms = 0;
  if (now_ms > 0x0000FFFFFFFFFFFFLL) now_ms = 0x0000FFFFFFFFFFFFLL;

  long long effective = now_ms;
  if (effective < last_ordinary_timestamp) effective = last_ordinary_timestamp;
  last_ordinary_timestamp = effective;

  std::random_device rd;
  std::array<uint8_t, 16> bytes{};
  for (auto& b : bytes) b = static_cast<uint8_t>(rd());

  if (!sequence_initialized) {
    sequence = (static_cast<uint64_t>(bytes[1]) << 32) |
               (static_cast<uint64_t>(bytes[2]) << 24) |
               (static_cast<uint64_t>(bytes[3]) << 16) |
               (static_cast<uint64_t>(bytes[4]) << 8) |
               static_cast<uint64_t>(bytes[5]);
    sequence_initialized = true;
  } else {
    const uint64_t kMaxSequence = (1ULL << 41) - 1ULL;
    if (sequence == kMaxSequence) {
      sequence = static_cast<uint64_t>(rd()) & kMaxSequence;
    } else {
      sequence++;
    }
  }

  uint64_t timestamp = static_cast<uint64_t>(effective);
  for (int index = 5; index >= 0; --index) {
    bytes[index] = static_cast<uint8_t>((timestamp >> ((5 - index) * 8)) & 0xff);
  }

  bytes[6] = static_cast<uint8_t>(0x70 | ((sequence >> 37) & 0x0f));
  bytes[7] = static_cast<uint8_t>((sequence >> 29) & 0xff);
  bytes[8] = static_cast<uint8_t>(0x80 | ((sequence >> 23) & 0x3f));
  bytes[9] = static_cast<uint8_t>((sequence >> 15) & 0xff);
  bytes[10] = static_cast<uint8_t>((sequence >> 7) & 0xff);
  bytes[11] = static_cast<uint8_t>(((sequence & 0x7f) << 1) | (bytes[11] & 0x01));

  std::string out;
  out.reserve(36);
  out += hex_byte(bytes[0]);
  out += hex_byte(bytes[1]);
  out += hex_byte(bytes[2]);
  out += hex_byte(bytes[3]);
  out += '-';
  out += hex_byte(bytes[4]);
  out += hex_byte(bytes[5]);
  out += '-';
  out += hex_byte(bytes[6]);
  out += hex_byte(bytes[7]);
  out += '-';
  out += hex_byte(bytes[8]);
  out += hex_byte(bytes[9]);
  out += '-';
  out += hex_byte(bytes[10]);
  out += hex_byte(bytes[11]);
  out += hex_byte(bytes[12]);
  out += hex_byte(bytes[13]);
  out += hex_byte(bytes[14]);
  out += hex_byte(bytes[15]);
  return out;
}

std::string session_id() {
  std::string full = uuidv7();
  full.erase(std::remove(full.begin(), full.end(), '-'), full.end());
  return full;
}

}  // namespace Id
}  // namespace karta
