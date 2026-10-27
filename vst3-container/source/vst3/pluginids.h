// Identity of the container plugin itself.
//
// The class UIDs are permanent: changing them makes every existing session lose the
// container (hosts key the plugin by UID, and "restore" would fail silently). Bump
// kVersion for releases, never touch the UIDs.
#pragma once

#include "pluginterfaces/base/funknown.h"

namespace crate::vst3 {

static const Steinberg::FUID kProcessorUID (0x1C7A6E30, 0x4B2D4A55, 0x9A1C5FBE, 0x3E7D2A61);
static const Steinberg::FUID kControllerUID (0x2D8B7F41, 0x5C3E4B66, 0xAB2D60CF, 0x4F8E3B72);

constexpr const char* kVendor = "Crate Audio";
constexpr const char* kVendorUrl = "https://example.invalid/crate";
constexpr const char* kVendorEmail = "mailto:support@example.invalid";
constexpr const char* kPluginName = "Crate";
constexpr const char* kVersion = "0.1.0";
/** Fx + Tools, not Instrument: the container is an effect that hosts other effects. */
constexpr const char* kSubCategories = "Fx|Tools";

} // namespace crate::vst3
