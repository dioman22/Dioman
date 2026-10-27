// The container as the DAW sees it.
//
// Responsibilities, in order of how badly they break a session:
//  1. process() never allocates, locks, logs or throws; an empty or failed chain is an
//     exact pass-through,
//  2. latency and tail are the sum of what the hosted plugins report,
//  3. state round-trips through the versioned codec in state/, not through ad-hoc blobs,
//  4. loading happens only on the message thread, with the journal open around it so a
//     crash names the plugin that caused it next time.
//
// Everything SDK-specific about hosting lives in vst3instance.cpp; this file only wires
// it to the DAW's ProcessData and to the parameter map.
#pragma once

#include "../app/journal.h"
#include "../host/chain.h"
#include "../host/params.h"
#include "../state/containerstate.h"
#include "hostcontext.h"
#include "vst3loader.h"

#include "public.sdk/source/vst/vstaudioeffect.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace crate::vst3 {

class CrateProcessor : public Steinberg::Vst::AudioEffect
{
public:
	static Steinberg::FUnknown* createInstance (void* context);

	CrateProcessor ();

	Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API terminate () SMTG_OVERRIDE;

	Steinberg::tresult PLUGIN_API setBusArrangements (Steinberg::Vst::SpeakerArrangement* inputs,
	                                                  Steinberg::int32 numIns,
	                                                  Steinberg::Vst::SpeakerArrangement* outputs,
	                                                  Steinberg::int32 numOuts) SMTG_OVERRIDE;

	Steinberg::tresult PLUGIN_API setupProcessing (Steinberg::Vst::ProcessSetup& setup) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setActive (Steinberg::TBool state) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setProcessing (Steinberg::TBool state) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API process (Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;

	Steinberg::uint32 PLUGIN_API getLatencySamples () SMTG_OVERRIDE;
	Steinberg::uint32 PLUGIN_API getTailSamples () SMTG_OVERRIDE;

	Steinberg::tresult PLUGIN_API getState (Steinberg::IBStream* state) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setState (Steinberg::IBStream* state) SMTG_OVERRIDE;

	OBJ_METHODS (CrateProcessor, Steinberg::Vst::AudioEffect)

	/** Called by the controller when the user picks a plugin for a slot. Message thread.
	 *  Returns false and fills `error` when the plugin cannot be hosted. */
	bool loadSlot (int32_t slot, const host::SlotIdentity& identity, std::string& error);

private:
	void applyParameterChanges (Steinberg::Vst::IParameterChanges* changes) noexcept;
	bool prepareChain (std::string& error);
	void releaseChain ();
	void unloadSlots ();
	bool readContainerState (Steinberg::IBStream* stream, state::ContainerState& out);
	void storeSlotValues (state::ContainerState& out);

	CrateHostApplication hostApplication_;
	Steinberg::IPtr<HostedPlugFrame> plugFrame_;
	Steinberg::IPtr<HostedComponentHandler> componentHandler_;
	std::unique_ptr<Loader> loader_;
	Steinberg::FUnknown* hostContext_ = nullptr;
	std::string hostName_;

	host::Chain chain_;
	state::ContainerState state_;
	std::unique_ptr<app::Journal> journal_;
	app::DirtySuspect crashSuspect_;

	host::ProcessSetup hostSetup_;
	bool prepared_ = false;
	bool active_ = false;

	/** Last values seen on the audio thread, so getState() does not have to race the UI. */
	std::atomic<float> mixValues_[host::kMaxSlots];
	std::atomic<bool> bypassValues_[host::kMaxSlots];

	/** Cached latency/tail, so the DAW can ask from any thread without touching the chain. */
	std::atomic<Steinberg::int32> reportedLatency_{0};
	std::atomic<Steinberg::int32> reportedTail_{0};

	// Used when the host gives us an inactive or empty input bus.
	std::vector<float> silentInputStorage_;
	std::vector<float*> silentInputPointers_;
	host::AudioBlock silentInput_;
};

} // namespace crate::vst3
