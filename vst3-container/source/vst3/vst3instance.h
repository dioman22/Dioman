// One hosted VST3 plugin, wrapped so the engine can run it like any other plugin.
//
// This is the only file that touches IComponent/IAudioProcessor/IEditController for the
// *hosted* side. Loading happens on the message thread; process() does nothing but fill
// a ProcessData that points straight at our buffers, so there is no allocation, no
// string copy and no lock on the audio path.
//
// Limitation, stated up front: only the main audio bus pair and no events. Instruments
// therefore load and report their state but do not receive MIDI yet — MIDI routing is
// the next piece of the hosting work, not a hidden bug.
#pragma once

#include "../host/plugininstance.h"

#include "pluginterfaces/base/smartpointer.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "public.sdk/source/vst/hosting/module.h"

#include <string>
#include <vector>

namespace crate::vst3 {

class Instance : public host::PluginInstance
{
public:
	Instance () = default;
	~Instance () override;

	/** Message thread. Resolves the class by UID (or by being the only audio module
	 *  class when the state has no UID), then creates component, processor and
	 *  controller and activates the main buses. */
	bool load (const std::string& bundlePath, const std::string& uid, const std::string& name,
	           Steinberg::FUnknown* hostContext, Steinberg::Vst::IComponentHandler* handler,
	           std::string& error);

	const char* displayName () const override { return name_.c_str (); }
	int32_t numInputChannels () const override { return inputChannels_; }
	int32_t numOutputChannels () const override { return outputChannels_; }
	int32_t latencySamples () const override;
	int32_t tailSamples () const override;

	bool startProcessing (const host::ProcessSetup& setup, std::string& error) override;
	void stopProcessing () override;
	void process (const host::AudioBlock& in, host::AudioBlock& out) noexcept override;

	bool getComponentState (std::vector<uint8_t>& out) override;
	bool setComponentState (const std::vector<uint8_t>& in) override;
	bool getControllerState (std::vector<uint8_t>& out) override;
	bool setControllerState (const std::vector<uint8_t>& in) override;

private:
	void close ();

	VST3::Hosting::Module::Ptr module_;
	Steinberg::IPtr<Steinberg::Vst::IComponent> component_;
	Steinberg::IPtr<Steinberg::Vst::IAudioProcessor> processor_;
	Steinberg::IPtr<Steinberg::Vst::IEditController> controller_;

	std::string name_;
	std::string bundlePath_;
	Steinberg::Vst::ProcessContext processContext_ {}; ///< minimal context: sample rate, no transport yet
	int32_t inputChannels_ = 0;
	int32_t outputChannels_ = 0;
	bool active_ = false;
};

} // namespace crate::vst3
