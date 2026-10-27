// Shared identity between the fixture plugin and the integration check.
//
// The UID belongs to a *test fixture* plugin built only when the VST3 SDK is available.
// It must never collide with the container's own UIDs (see source/vst3/pluginids.h).
#pragma once

#include <cstdint>

namespace crate::integration {

/** 11223344-5567-7888-99AA-BBCCDDEEFF01, as FUID longs in gainplugin.cpp. */
constexpr const char* kGainUid = "112233445566778899AABBCCDDEEFF01";

/** Fixed plugin behaviour the integration check asserts. */
constexpr float kGain = 2.0f;
constexpr int32_t kLatencySamples = 32;

} // namespace crate::integration
