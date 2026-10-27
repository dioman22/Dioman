#include "params.h"

#include <algorithm>
#include <cstdio>

namespace crate::host::params {

namespace {

double clamp01 (double value)
{
	if (value < 0.0)
		return 0.0;
	if (value > 1.0)
		return 1.0;
	return value;
}

int32_t slotOf (uint32_t id)
{
	if (id < kSlotBaseId)
		return -1;
	const uint32_t offset = id - kSlotBaseId;
	const int32_t slot = static_cast<int32_t> (offset / kSlotStride);
	if (slot < 0 || slot >= kMaxSlots)
		return -1;
	return slot;
}

} // namespace

bool parseSlotId (uint32_t id, int32_t& slot, SlotParam& param)
{
	const int32_t found = slotOf (id);
	if (found < 0)
		return false;
	const uint32_t kind = (id - kSlotBaseId) % kSlotStride;
	if (kind > static_cast<uint32_t> (SlotParam::Bypass))
		return false;
	slot = found;
	param = static_cast<SlotParam> (kind);
	return true;
}

std::string title (uint32_t id)
{
	if (isSelectedSlot (id))
		return "Selected Slot";
	int32_t slot = 0;
	SlotParam param = SlotParam::Mix;
	if (!parseSlotId (id, slot, param))
		return std::string ();
	const char* name = param == SlotParam::Bypass ? "Bypass" : "Mix";
	return "Slot " + std::to_string (slot + 1) + " " + name;
}

std::string format (uint32_t id, double plain)
{
	if (isSelectedSlot (id))
	{
		const int32_t slot = static_cast<int32_t> (plain + 0.5);
		return "Slot " + std::to_string (slot + 1);
	}
	int32_t slot = 0;
	SlotParam param = SlotParam::Mix;
	if (!parseSlotId (id, slot, param))
		return std::string ();
	if (param == SlotParam::Bypass)
		return plain >= 0.5 ? "On" : "Off";
	char text[32] = {};
	std::snprintf (text, sizeof (text), "%.1f %%", clamp01 (plain) * 100.0);
	return text;
}

double toPlain (uint32_t id, double normalised)
{
	if (isSelectedSlot (id))
	{
		const double span = static_cast<double> (kMaxSlots - 1);
		return std::min (static_cast<double> (kMaxSlots - 1), clamp01 (normalised) * span);
	}
	return clamp01 (normalised); // both mix and bypass are 0..1 in plain units
}

double toNormalised (uint32_t id, double plain)
{
	if (isSelectedSlot (id))
	{
		const double span = static_cast<double> (kMaxSlots - 1);
		return clamp01 (plain / span);
	}
	return clamp01 (plain);
}

double defaultPlain (uint32_t id)
{
	int32_t slot = 0;
	SlotParam param = SlotParam::Mix;
	if (parseSlotId (id, slot, param))
		return param == SlotParam::Bypass ? 0.0 : 1.0;
	return 0.0;
}

bool isStepped (uint32_t id)
{
	if (isSelectedSlot (id))
		return true;
	int32_t slot = 0;
	SlotParam param = SlotParam::Mix;
	return parseSlotId (id, slot, param) && param == SlotParam::Bypass;
}

} // namespace crate::host::params
