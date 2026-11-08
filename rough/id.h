// Karta — Id generation.
//
// Karta must stay compatible with pi's id scheme so sessions, RPC requests,
// extension-ui requests, tool calls, and provider call ids all read the same
// way as the original pi code. pi uses uuidv7() from
// @earendil-works/pi-ai, a time-ordered UUID version 7 with a 40-bit
// millisecond timestamp, a 12-bit clock sequence, and a 6-bit variant nibble.
//
// This header provides a C++ implementation of that scheme:
//   - uuidv7(): a full UUIDv7 string, e.g. "01923f4e-8b12-7c3a-8000-123456789abc".
//   - session_id(): a shortened, url-safe session id suitable for Karta's
//     --session-id/--fork session id and for CLI-visible session identifiers.
//     It keeps the same v7 timestamp/counter ordering so lookups are stable,
//     but it is shorter than a full UUID so terminal/session-window text stays
//     readable.
//
// Both use std::random_device for seed entropy up front and
// crypto-safe global random bytes for the remaining high-order bits.
//
// Implementation: src/core/id.cpp (declaration-only header to avoid ODR violations).

#pragma once

#include <cstdint>
#include <string>

namespace karta {

namespace Id {

// Time-ordered UUIDv7, matching the pi-ai reference impl:
// bytes 0..3  = time_low (32 bits)
// bytes 4..5  = time_mid (16 bits)
// bytes 6..7  = version(4) + time_hi (12 bits)
// bytes 8..9  = variant(1) + clock_seq_hi (15 bits)
// bytes 10..15 = clock_seq_low(8 bits) + node(48 bits)
// The version nibble is 0100b and the variant is 100000b.
std::string uuidv7();

// Session id used by Karta: short enough for UI/file names, but still a
// deterministic v7-derived string with the same ordering as uuidv7.
std::string session_id();

}  // namespace Id

}  // namespace karta
