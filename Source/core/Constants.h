#pragma once

#include <cstddef>
#include <cstdint>

namespace cuemix
{
// Layout of the shared bus. Changing any of these changes the shared-memory layout, so bump
// kBusLayoutVersion (and the bus name) together with them.
inline constexpr int kMaxChannels = 32;        // CueMix Send instances that can be live at once
inline constexpr int kNumMixes = 8;            // stereo headphone mixes per CueMix instance
inline constexpr int kRingFrames = 16384;      // per channel, power of two
inline constexpr int kNameWords = 8;           // channel name: 8 x 8 bytes = 63 chars + NUL
inline constexpr uint32_t kBusMagic = 0x43554D58; // 'CUMX'
inline constexpr uint32_t kBusLayoutVersion = 1;

// The mixer reads a channel at most this many frames behind its writer before it resyncs.
inline constexpr int kMaxReadLagFrames = kRingFrames / 2;

static_assert((kRingFrames & (kRingFrames - 1)) == 0, "ring size must be a power of two");
} // namespace cuemix
