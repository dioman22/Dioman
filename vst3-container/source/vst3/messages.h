// Messages between the controller (UI/registry side) and the processor (audio/chain side).
//
// The two halves of a split VST3 plugin only share an IConnectionPoint. The editor asks the
// controller to load a card; the controller forwards it to the processor, which owns the
// chain and the state, and the processor answers with what happened so the footer can say it.
#pragma once

#include "pluginterfaces/vst/ivstattributes.h"
#include "public.sdk/source/vst/utility/stringconvert.h"

#include <string>

namespace crate::vst3::messages {

inline constexpr const char* kLoadSlot = "crate.load_slot";
inline constexpr const char* kLoadResult = "crate.load_result";

inline constexpr const char* kSlot = "slot";
inline constexpr const char* kUid = "uid";
inline constexpr const char* kPath = "path";
inline constexpr const char* kName = "name";
inline constexpr const char* kVendor = "vendor";
inline constexpr const char* kVersion = "version";
inline constexpr const char* kOk = "ok";
inline constexpr const char* kError = "error";

inline void setString (Steinberg::Vst::IAttributeList* list, const char* id,
                       const std::string& text)
{
	if (!list)
		return;
	Steinberg::Vst::String128 buffer {};
	if (VST3::StringConvert::convert (text, buffer))
		list->setString (id, buffer);
}

inline std::string getString (Steinberg::Vst::IAttributeList* list, const char* id)
{
	if (!list)
		return {};
	Steinberg::Vst::String128 buffer {};
	buffer[0] = 0;
	if (list->getString (id, buffer, sizeof (buffer)) != Steinberg::kResultOk)
		return {};
	return VST3::StringConvert::convert (buffer);
}

} // namespace crate::vst3::messages
