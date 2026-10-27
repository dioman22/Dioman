#include "vst3instance.h"

#include "../host/audioops.h"
#include "../util/stringutil.h"

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/vstspeaker.h"
#include "public.sdk/source/common/memorystream.h"

#include <cstring>
#include <limits>

namespace crate::vst3 {

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

std::string hexFromTuid (const TUID tuid)
{
	static const char* digits = "0123456789ABCDEF";
	std::string out;
	out.reserve (32);
	for (int i = 0; i < 16; ++i)
	{
		const unsigned char byte = static_cast<unsigned char> (tuid[i]);
		out += digits[byte >> 4];
		out += digits[byte & 0x0F];
	}
	return out;
}

int32_t channelCount (IAudioProcessor* processor, BusDirection direction)
{
	if (!processor)
		return 0;
	SpeakerArrangement arrangement = 0;
	if (processor->getBusArrangement (direction, 0, arrangement) != kResultOk)
		return 0;
	return static_cast<int32_t> (SpeakerArr::getChannelCount (arrangement));
}

bool readAll (IBStream* stream, std::vector<uint8_t>& out)
{
	out.clear ();
	if (!stream)
		return false;
	int64 size = 0;
	if (stream->seek (0, IBStream::kIBSeekEnd, &size) != kResultOk || size < 0)
		return false;
	if (stream->seek (0, IBStream::kIBSeekSet, nullptr) != kResultOk)
		return false;
	out.resize (static_cast<size_t> (size));
	if (size == 0)
		return true;
	int32 read = 0;
	if (stream->read (out.data (), static_cast<int32> (size), &read) != kResultOk)
		return false;
	out.resize (static_cast<size_t> (read));
	return true;
}

} // namespace

Instance::~Instance ()
{
	close ();
}

void Instance::close ()
{
	if (active_)
		stopProcessing ();
	controller_ = nullptr;
	processor_ = nullptr;
	component_ = nullptr;
	module_.reset ();
	inputChannels_ = 0;
	outputChannels_ = 0;
}

bool Instance::load (const std::string& bundlePath, const std::string& uid,
                     const std::string& name, FUnknown* hostContext,
                     IComponentHandler* handler, std::string& error)
{
	close ();
	error.clear ();
	name_ = name;
	bundlePath_ = bundlePath;

	std::string moduleError;
	module_ = VST3::Hosting::Module::create (bundlePath, moduleError);
	if (!module_)
	{
		error = moduleError.empty () ? "the bundle could not be opened" : moduleError;
		return false;
	}

	IPluginFactory* factory = module_->getFactory ().get ().get ();
	if (!factory)
	{
		error = "the bundle has no plugin factory";
		return false;
	}

	// Find the audio module class. Never by index: a bundle can export several classes.
	PClassInfo classInfo {};
	TUID classId {};
	bool found = false;
	for (int32 i = 0; i < factory->countClasses (); ++i)
	{
		classInfo = PClassInfo {};
		if (factory->getClassInfo (i, &classInfo) != kResultOk)
			continue;
		if (std::strcmp (classInfo.category, kVstAudioEffectClass) != 0)
			continue;
		if (!uid.empty () && !iequals (hexFromTuid (classInfo.cid), uid))
			continue;
		std::memcpy (classId, classInfo.cid, sizeof (TUID));
		found = true;
		break;
	}
	if (!found)
	{
		error = uid.empty () ? "the bundle exports no audio module class"
		                     : "the bundle no longer contains class " + uid +
		                           " (the plugin was updated or replaced)";
		return false;
	}

	IComponent* component = nullptr;
	if (factory->createInstance (classId, IComponent::iid, reinterpret_cast<void**> (&component)) !=
	        kResultTrue ||
	    !component)
	{
		error = "the plugin factory refused to create the component";
		return false;
	}
	component_ = owned (component);

	if (component_->initialize (hostContext) != kResultOk)
	{
		error = "the plugin could not be initialised";
		close ();
		return false;
	}

	IAudioProcessor* processor = nullptr;
	if (component_->queryInterface (IAudioProcessor::iid, reinterpret_cast<void**> (&processor)) !=
	        kResultOk ||
	    !processor)
	{
		error = "the component does not expose IAudioProcessor";
		close ();
		return false;
	}
	processor_ = owned (processor);

	// A single-component plugin implements the controller itself; a split plugin creates
	// a second object, and that object has its *own* class ID — the processor CID will not
	// find it in the factory.
	IEditController* controller = nullptr;
	if (component_->queryInterface (IEditController::iid, reinterpret_cast<void**> (&controller)) !=
	        kResultOk ||
	    !controller)
	{
		controller = nullptr;
		TUID controllerCid {};
		if (component_->getControllerClassId (controllerCid) == kResultOk &&
		    factory->createInstance (controllerCid, IEditController::iid,
		                             reinterpret_cast<void**> (&controller)) == kResultTrue &&
		    controller)
		{
			controller_ = owned (controller);
			controller_->initialize (hostContext);
		}
	}
	else
	{
		controller_ = owned (controller);
	}
	if (controller_ && handler)
		controller_->setComponentHandler (handler);

	inputChannels_ = component_->getBusCount (kAudio, kInput) > 0 ? channelCount (processor_, kInput) : 0;
	outputChannels_ = channelCount (processor_, kOutput);

	// Hosted plugins that refuse activation are a real failure; kNotImplemented from
	// setState/getState only means "this plugin keeps no state".
	if (component_->getBusCount (kAudio, kInput) > 0)
		component_->activateBus (kAudio, kInput, 0, true);
	if (component_->getBusCount (kAudio, kOutput) > 0)
		component_->activateBus (kAudio, kOutput, 0, true);

	return true;
}


int32_t Instance::latencySamples () const
{
	if (!processor_)
		return 0;
	const uint32 latency = processor_->getLatencySamples ();
	if (latency > static_cast<uint32> (std::numeric_limits<int32_t>::max ()))
		return std::numeric_limits<int32_t>::max ();
	return static_cast<int32_t> (latency);
}

int32_t Instance::tailSamples () const
{
	if (!processor_)
		return 0;
	const uint32 tail = processor_->getTailSamples ();
	if (tail == kInfiniteTail)
		return std::numeric_limits<int32_t>::max ();
	if (tail > static_cast<uint32> (std::numeric_limits<int32_t>::max ()))
		return std::numeric_limits<int32_t>::max ();
	return static_cast<int32_t> (tail);
}

bool Instance::startProcessing (const host::ProcessSetup& setup, std::string& error)
{
	error.clear ();
	if (!component_ || !processor_)
	{
		error = "the plugin is not loaded";
		return false;
	}
	if (active_)
		return true;

	ProcessSetup vstSetup {};
	vstSetup.processMode = kRealtime;
	vstSetup.symbolicSampleSize = kSample32;
	vstSetup.maxSamplesPerBlock = setup.maxBlockSize;
	vstSetup.sampleRate = setup.sampleRate;

	if (processor_->setupProcessing (vstSetup) != kResultOk)
	{
		error = "the plugin rejected the sample rate / block size";
		return false;
	}
	processContext_.sampleRate = setup.sampleRate;
	if (component_->setActive (true) != kResultOk)
	{
		error = "the plugin could not be activated";
		return false;
	}
	// kNotImplemented is what every plugin derived from the SDK's AudioEffect returns
	// here, so it means "nothing to do", not "failed".
	const tresult processing = processor_->setProcessing (true);
	if (processing != kResultOk && processing != kNotImplemented)
	{
		component_->setActive (false);
		error = "the plugin could not start processing";
		return false;
	}
	active_ = true;
	return true;
}

void Instance::stopProcessing ()
{
	if (processor_)
		processor_->setProcessing (false);
	if (component_)
		component_->setActive (false);
	active_ = false;
}

void Instance::process (const host::AudioBlock& in, host::AudioBlock& out) noexcept
{
	if (!processor_ || !active_)
	{
		host::copyBlock (in, out);
		return;
	}

	AudioBusBuffers inputBus {};
	AudioBusBuffers outputBus {};
	ProcessData data {};
	data.processMode = kRealtime;
	data.symbolicSampleSize = kSample32;
	data.numSamples = out.numSamples;

	if (in.numChannels > 0 && in.channels)
	{
		inputBus.numChannels = in.numChannels;
		inputBus.channelBuffers32 = const_cast<float**> (in.channels);
		inputBus.silenceFlags = in.silenceFlags;
		data.numInputs = 1;
		data.inputs = &inputBus;
	}

	outputBus.numChannels = out.numChannels;
	outputBus.channelBuffers32 = const_cast<float**> (out.channels);
	outputBus.silenceFlags = 0;
	data.numOutputs = out.numChannels > 0 ? 1 : 0;
	data.outputs = out.numChannels > 0 ? &outputBus : nullptr;

	// No events and no parameter changes: the container exposes its own parameters, and
	// MIDI routing is not wired yet (see the header).
	data.inputEvents = nullptr;
	data.outputEvents = nullptr;
	data.inputParameterChanges = nullptr;
	data.outputParameterChanges = nullptr;
	data.processContext = &processContext_;

	processor_->process (data);

	out.silenceFlags = data.numOutputs > 0 ? outputBus.silenceFlags : 0;
}

bool Instance::getComponentState (std::vector<uint8_t>& out)
{
	out.clear ();
	if (!component_)
		return false;
	MemoryStream stream;
	if (component_->getState (&stream) != kResultOk)
		return false;
	return readAll (&stream, out);
}

bool Instance::setComponentState (const std::vector<uint8_t>& in)
{
	if (!component_)
		return false;
	MemoryStream componentStream (const_cast<uint8_t*> (in.data ()),
	                              static_cast<TSize> (in.size ()));
	const tresult componentResult = component_->setState (&componentStream);
	if (controller_)
	{
		// The same bytes are what most plugins expect on the controller side, and it is
		// also the only way controller-only state (program selection) comes back.
		MemoryStream controllerStream (const_cast<uint8_t*> (in.data ()),
		                               static_cast<TSize> (in.size ()));
		controller_->setComponentState (&controllerStream);
	}
	return componentResult == kResultOk;
}

bool Instance::getControllerState (std::vector<uint8_t>& out)
{
	out.clear ();
	if (!controller_)
		return false;
	MemoryStream stream;
	if (controller_->getState (&stream) != kResultOk)
		return false;
	return readAll (&stream, out);
}

bool Instance::setControllerState (const std::vector<uint8_t>& in)
{
	if (!controller_)
		return false;
	MemoryStream stream (const_cast<uint8_t*> (in.data ()), static_cast<TSize> (in.size ()));
	return controller_->setState (&stream) == kResultOk;
}

} // namespace crate::vst3
