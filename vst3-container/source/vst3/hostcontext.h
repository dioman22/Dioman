// The container is a host for the plugins it hosts. When a hosted plugin asks for its
// host, it must be told "Crate", not the DAW's name — the DAW is *our* host.
//
// These are the three objects a hosted plugin needs:
//  * IHostApplication: name + createInstance(IMessage / IAttributeList),
//  * IPlugFrame:     resizeView() permission for the hosted editor,
//  * IComponentHandler(+2): begin/perform/endEdit, setDirty, restartComponent.
//
// restartComponent is the one that matters: a hosted plugin that changes its latency or
// bus layout tells us here. Flags are recorded lock-free and picked up by the processor
// on the message thread; nothing here blocks or allocates on the audio path.
#pragma once

#include "base/source/fobject.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

#include <atomic>

namespace crate::vst3 {

/** Our name towards hosted plugins. "Crate", never "Ableton Live". */
class CrateHostApplication : public Steinberg::Vst::HostApplication
{
public:
	Steinberg::tresult PLUGIN_API getName (Steinberg::Vst::String128 name) SMTG_OVERRIDE;
};

/** Satisfies hosted `resizeView()` calls. A hosted editor asking to grow gets a no for
 *  now: the container owns its own window size, and resizing it from inside a hosted
 *  plugin is how you end up with a view larger than the DAW's plugin window. */
class HostedPlugFrame : public Steinberg::FObject, public Steinberg::IPlugFrame
{
public:
	Steinberg::tresult PLUGIN_API resizeView (Steinberg::IPlugView* view,
	                                          Steinberg::ViewRect* newSize) SMTG_OVERRIDE;

	DELEGATE_REFCOUNT (Steinberg::FObject)
	Steinberg::tresult PLUGIN_API queryInterface (const Steinberg::TUID _iid,
	                                              void** obj) SMTG_OVERRIDE;
};

/** Component handler for hosted plugins. Edits are not mirrored to the container's own
 *  parameter list (that is a separate feature with its own parameter-id design), but the
 *  lifecycle calls are all accepted so a plugin cannot conclude it is unhosted. */
class HostedComponentHandler : public Steinberg::FObject,
                               public Steinberg::Vst::IComponentHandler,
                               public Steinberg::Vst::IComponentHandler2
{
public:
	Steinberg::tresult PLUGIN_API beginEdit (Steinberg::Vst::ParamID id) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API performEdit (Steinberg::Vst::ParamID id,
	                                          Steinberg::Vst::ParamValue valueNormalized) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API endEdit (Steinberg::Vst::ParamID id) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API restartComponent (Steinberg::int32 flags) SMTG_OVERRIDE;

	Steinberg::tresult PLUGIN_API setDirty (Steinberg::TBool state) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API requestOpenEditor (Steinberg::FIDString name) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API startGroupEdit () SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API finishGroupEdit () SMTG_OVERRIDE;

	/** Flags accumulated by hosted plugins since the last call. Message thread. */
	Steinberg::int32 takeRestartFlags () { return restartFlags_.exchange (0); }
	bool latencyChanged () const { return latencyDirty_; }
	void clearLatencyChanged () { latencyDirty_ = false; }

	DELEGATE_REFCOUNT (Steinberg::FObject)
	Steinberg::tresult PLUGIN_API queryInterface (const Steinberg::TUID _iid,
	                                              void** obj) SMTG_OVERRIDE;

private:
	std::atomic<Steinberg::int32> restartFlags_{0};
	std::atomic<bool> latencyDirty_{false};
};

} // namespace crate::vst3
