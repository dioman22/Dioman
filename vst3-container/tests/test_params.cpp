// The parameter map is the contract between the processor, the controller and the grid
// UI. These tests exist so a renumbering mistake fails here instead of in a DAW.
#include "../source/host/params.h"
#include "test_framework.h"

using namespace crate;
using namespace crate::host;

CRATE_TEST (params_slot_ids_round_trip)
{
	for (int32_t slot = 0; slot < kMaxSlots; ++slot)
	{
		const uint32_t mixId = params::slotId (slot, params::SlotParam::Mix);
		const uint32_t bypassId = params::slotId (slot, params::SlotParam::Bypass);
		CHECK (mixId >= params::kSlotBaseId);
		CHECK (mixId < params::kMaxParameterId);
		CHECK (bypassId < params::kMaxParameterId);
		CHECK (mixId != bypassId);

		int32_t foundSlot = -1;
		params::SlotParam foundParam = params::SlotParam::Mix;
		CHECK (params::parseSlotId (mixId, foundSlot, foundParam));
		CHECK_EQ (foundSlot, slot);
		CHECK_EQ (static_cast<int> (foundParam), static_cast<int> (params::SlotParam::Mix));

		CHECK (params::parseSlotId (bypassId, foundSlot, foundParam));
		CHECK_EQ (foundSlot, slot);
		CHECK_EQ (static_cast<int> (foundParam), static_cast<int> (params::SlotParam::Bypass));
	}
}

CRATE_TEST (params_reject_ids_outside_the_map)
{
	int32_t slot = 0;
	params::SlotParam param = params::SlotParam::Mix;
	CHECK (!params::parseSlotId (params::kSelectedSlotId, slot, param));
	CHECK (!params::parseSlotId (params::kMaxParameterId, slot, param));
	CHECK (!params::parseSlotId (params::kSlotBaseId + 2, slot, param)); // no third kind
	CHECK (params::parseSlotId (params::kSlotBaseId + 1, slot, param));  // that one is slot 0 bypass
	CHECK_EQ (slot, 0);
}

CRATE_TEST (params_titles_and_formatting)
{
	CHECK_EQ (params::title (params::slotId (0, params::SlotParam::Mix)), std::string ("Slot 1 Mix"));
	CHECK_EQ (params::title (params::slotId (2, params::SlotParam::Bypass)),
	          std::string ("Slot 3 Bypass"));
	CHECK_EQ (params::title (params::kSelectedSlotId), std::string ("Selected Slot"));
	CHECK_EQ (params::title (params::kMaxParameterId), std::string ());

	CHECK_EQ (params::format (params::slotId (0, params::SlotParam::Bypass), 0.0),
	          std::string ("Off"));
	CHECK_EQ (params::format (params::slotId (0, params::SlotParam::Bypass), 1.0),
	          std::string ("On"));
	CHECK_EQ (params::format (params::slotId (0, params::SlotParam::Mix), 0.5),
	          std::string ("50.0 %"));
	CHECK_EQ (params::format (params::slotId (0, params::SlotParam::Mix), 2.0),
	          std::string ("100.0 %"));
	CHECK_EQ (params::format (params::kSelectedSlotId, 3.0), std::string ("Slot 4"));
}

CRATE_TEST (params_normalisation_is_lossless_enough)
{
	const uint32_t selected = params::kSelectedSlotId;
	CHECK_EQ (params::toPlain (selected, 0.0), 0.0);
	CHECK_EQ (params::toPlain (selected, 1.0), static_cast<double> (kMaxSlots - 1));
	CHECK_EQ (params::toNormalised (selected, static_cast<double> (kMaxSlots - 1)), 1.0);
	CHECK_EQ (params::toPlain (selected, 2.0), static_cast<double> (kMaxSlots - 1)); // clamps

	const uint32_t mix = params::slotId (1, params::SlotParam::Mix);
	CHECK_EQ (params::toPlain (mix, 0.25), 0.25);
	CHECK_EQ (params::toNormalised (mix, 0.25), 0.25);
	CHECK_EQ (params::toPlain (mix, -1.0), 0.0);
}

CRATE_TEST (params_defaults_and_stepping)
{
	CHECK_EQ (params::defaultPlain (params::slotId (0, params::SlotParam::Mix)), 1.0);
	CHECK_EQ (params::defaultPlain (params::slotId (0, params::SlotParam::Bypass)), 0.0);

	CHECK (params::isStepped (params::slotId (0, params::SlotParam::Bypass)));
	CHECK (!params::isStepped (params::slotId (0, params::SlotParam::Mix)));
	CHECK (params::isStepped (params::kSelectedSlotId));
}
