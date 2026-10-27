// The controller is the parameter list the DAW automates and the parent of the editor.
//
// It owns exactly one job right now: expose per-slot mix/bypass (and the selected slot)
// with real titles and units, and persist them. It deliberately does not create a view
// yet — the grid editor is the next milestone; until then hosts show their generic
// parameter list, which is honest rather than a broken window.
#pragma once

#include "../host/params.h"

#include "public.sdk/source/vst/vsteditcontroller.h"

namespace crate::vst3 {

class CrateController : public Steinberg::Vst::EditController
{
public:
	static Steinberg::FUnknown* createInstance (void* context);

	CrateController ();

	Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setComponentState (Steinberg::IBStream* state) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setState (Steinberg::IBStream* state) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API getState (Steinberg::IBStream* state) SMTG_OVERRIDE;

	OBJ_METHODS (CrateController, Steinberg::Vst::EditController)

	/** Which slot the grid would load into. UI state, saved with the controller. */
	int32_t selectedSlot () const;
	void setSelectedSlot (int32_t slot);

private:
	void addParameters ();

	int32_t selectedSlot_ = 0;
};

} // namespace crate::vst3
