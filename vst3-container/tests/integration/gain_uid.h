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

/** The same id as 32 uppercase hex characters: the form the container stores. */
inline std::string gainUidHex ()
{
	const uint32_t longs[4] = {kGainUidLong1, kGainUidLong2, kGainUidLong3, kGainUidLong4};
	static const char* digits = "0123456789ABCDEF";
	std::string out;
	out.reserve (32);
	for (int i = 0; i < 4; ++i)
	{
		for (int b = 0; b < 4; ++b)
		{
			const unsigned char byte =
			    static_cast<unsigned char> ((longs[i] >> (8 * b)) & 0xFF);
			out += digits[byte >> 4];
			out += digits[byte & 0x0F];
		}
	}
	return out;
}

/** Fixed plugin behaviour the integration check asserts. */
constexpr float kGain = 2.0f;
constexpr int32_t kLatencySamples = 32;

} // namespace crate::integration
