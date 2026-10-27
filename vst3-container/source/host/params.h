// The container's own parameter map.
//
// Kept SDK-free for one reason: the processor, the controller and the grid UI must agree
// on parameter ids, ranges and formatting, and "the UI sends 0.5 to the wrong parameter"
// is not a bug you want to find in a DAW. Tests pin the mapping.
//
// Layout:
//   0                      selected slot (0 .. kMaxSlots-1), a UI-only value
//   100 + slot*10 + 0      slot mix     0 .. 1
//   100 + slot*10 + 1      slot bypass  0 .. 1 (0 = active)
// Ids below 100 are reserved for container-wide parameters.
#pragma once

#include "audiotypes.h"

#include <cstdint>
#include <string>

namespace crate::host::params {

constexpr uint32_t kSelectedSlotId = 0;
constexpr uint32_t kSlotBaseId = 100;
constexpr uint32_t kSlotStride = 10;
constexpr uint32_t kMaxParameterId = kSlotBaseId + kSlotStride * static_cast<uint32_t> (kMaxSlots);

/** Slot parameters, as an enum so a switch in the UI cannot silently reorder them. */
enum class SlotParam : uint32_t
{
	Mix = 0,
	Bypass = 1
};

constexpr uint32_t slotId (int32_t slot, SlotParam param)
{
	return kSlotBaseId + kSlotStride * static_cast<uint32_t> (slot) +
	       static_cast<uint32_t> (param);
}

/** True and fills `slot` / `param` when `id` addresses a slot parameter. */
bool parseSlotId (uint32_t id, int32_t& slot, SlotParam& param);

inline bool isSelectedSlot (uint32_t id) { return id == kSelectedSlotId; }

/** Human title: "Slot 2 Bypass". Empty for an unknown id. */
std::string title (uint32_t id);

/** Display value: "On" / "Off", "75.0 %", "Slot 3". Empty for an unknown id. */
std::string format (uint32_t id, double plain);

/** Plain value for a normalised 0..1 value. Clamps. */
double toPlain (uint32_t id, double normalised);

/** Normalised 0..1 value for a plain value. Clamps. */
double toNormalised (uint32_t id, double plain);

/** Value a freshly created slot starts with: mix 1, bypass 0, selected slot 0. */
double defaultPlain (uint32_t id);

/** Bypass is a switch: hosts should show it as stepped, not as a continuous knob. */
bool isStepped (uint32_t id);

} // namespace crate::host::params
