// Shared identity between the fixture plugin and the integration check.
//
// The UID belongs to a *test fixture* plugin built only when the VST3 SDK is available.
// It must never collide with the container's own UIDs (see source/vst3/pluginids.h).
#pragma once

#include <cstdint>
#include <string>

namespace crate::integration {

/** The fixture's class id, as the four longs gainplugin.cpp hands to FUID. This is the
 *  one place it is written down: FUID keeps each long little-endian, so the hex form a
 *  host sees in PClassInfo::cid is not the longs in reading order. Deriving one from the
 *  other below is what keeps the check and the fixture from drifting apart. */
constexpr uint32_t kGainUidLong1 = 0x11223344u;
constexpr uint32_t kGainUidLong2 = 0x55667788u;
constexpr uint32_t kGainUidLong3 = 0x99AABBCCu;
constexpr uint32_t kGainUidLong4 = 0xDDEEFF01u;

/** The hex form the container compares against is read from the loaded bundle by the
 *  integration check. FUID's byte layout is its own business, and guessing it here is
 *  how the check and the fixture drifted apart in the first place. */

/** Fixed plugin behaviour the integration check asserts. */
constexpr float kGain = 2.0f;
constexpr int32_t kLatencySamples = 32;

} // namespace crate::integration
