#include "crateprocessor.h"

#include "../app/cratepaths.h"
#include "../util/fs.h"
#include "../util/stringutil.h"

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

namespace crate::vst3 {

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

bool readStreamBytes (IBStream* stream, std::vector<uint8_t>& out)
{
	out.clear ();
	if (!stream)
		return false;
	int64 size = 0;
	if (stream->seek (0, IBStream::kIBSeekEnd, &size) != kResultOk || size <= 0)
		return false;
	if (stream->seek (0, IBStream::kIBSeekSet, nullptr) != kResultOk)
		return false;
	out.resize (static_cast<size_t> (size));
	int32 read = 0;
	if (stream->read (out.data (), static_cast<int32> (size), &read) != kResultOk || read <= 0)
		return false;
	out.resize (static_cast<size_t> (read));
	return true;
}

} // namespace

FUnknown* CrateProcessor::createInstance (void*)
{
	return static_cast<IComponent*> (new CrateProcessor ());
}

CrateProcessor::CrateProcessor ()
{
	for (auto& value : mixValues_)
		value.store (1.0f, std::memory_order_relaxed);
	for (auto& value : bypassValues_)
		value.store (false, std::memory_order_relaxed);

	plugFrame_ = owned (new HostedPlugFrame ());
	componentHandler_ = owned (new HostedComponentHandler ());

	addAudioInput (STR16 ("Input"), SpeakerArr::kStereo);
	addAudioOutput (STR16 ("Output"), SpeakerArr::kStereo);
}

tresult PLUGIN_API CrateProcessor::initialize (FUnknown* context)
{
	const tresult result = AudioEffect::initialize (context);
	if (result != kResultOk)
		return result;

	hostContext_ = context;

	// Who is hosting us? The registry readers are keyed on this name, so ask once.
	if (context)
	{
		IHostApplication* raw = nullptr;
		if (context->queryInterface (IHostApplication::iid, reinterpret_cast<void**> (&raw)) ==
		        kResultOk &&
		    raw)
		{
			IPtr<IHostApplication> application = owned (raw);
			String128 name {};
			name[0] = 0;
			if (application->getName (name) == kResultOk)
			{
				const std::u16string wide (reinterpret_cast<const char16_t*> (name));
				hostName_ = utf16ToUtf8 (wide);
			}
		}
	}

	loader_.reset (new Loader (context, componentHandler_));

	app::ensureDataDirectories ();
	journal_.reset (new app::Journal (app::journalPath ()));
	crashSuspect_ = journal_->startSession ();

	return kResultOk;
}

tresult PLUGIN_API CrateProcessor::terminate ()
{
	releaseChain ();
	unloadSlots ();
	loader_.reset ();
	journal_.reset ();
	hostContext_ = nullptr;
	return AudioEffect::terminate ();
}

tresult PLUGIN_API CrateProcessor::setBusArrangements (SpeakerArrangement* inputs, int32 numIns,
                                                       SpeakerArrangement* outputs, int32 numOuts)
{
	// The container is a stereo effect. Anything else would need a channel map that the
	// hosted mono/stereo plugins do not promise.
	if (numIns == 1 && numOuts == 1 && inputs && outputs && inputs[0] == SpeakerArr::kStereo &&
	    outputs[0] == SpeakerArr::kStereo)
		return AudioEffect::setBusArrangements (inputs, numIns, outputs, numOuts);
	return kResultFalse;
}

tresult PLUGIN_API CrateProcessor::setupProcessing (ProcessSetup& setup)
{
	const tresult result = AudioEffect::setupProcessing (setup);
	if (result != kResultOk)
		return result;

	if (setup.symbolicSampleSize != kSample32)
		return kResultFalse; // the engine is 32-bit float only, and says so

	hostSetup_.sampleRate = setup.sampleRate;
	hostSetup_.maxBlockSize = std::min<int32_t> (setup.maxSamplesPerBlock, host::kMaxBlockSize);
	hostSetup_.numChannels = 2;

	// A new sample rate or block size invalidates every hosted plugin's setup and every
	// delay line length, so the chain is rebuilt rather than patched.
	releaseChain ();

	silentInputStorage_.assign (
	    static_cast<size_t> (2) * static_cast<size_t> (hostSetup_.maxBlockSize), 0.0f);
	silentInputPointers_.assign (2, nullptr);
	for (int32_t c = 0; c < 2; ++c)
		silentInputPointers_[static_cast<size_t> (c)] =
		    silentInputStorage_.data () +
		    static_cast<size_t> (c) * static_cast<size_t> (hostSetup_.maxBlockSize);
	silentInput_ = host::makeBlock (silentInputPointers_.data (), 2, 0);
	silentInput_.setSilent (true);

	return kResultOk;
}

tresult PLUGIN_API CrateProcessor::setActive (TBool state)
{
	if (state)
	{
		if (!prepared_)
		{
			std::string error;
			if (!prepareChain (error))
				return kResultFalse;
		}
		active_ = true;
	}
	else
	{
		active_ = false;
		releaseChain ();
	}
	return AudioEffect::setActive (state);
}

tresult PLUGIN_API CrateProcessor::setProcessing (TBool state)
{
	if (!state)
		active_ = false;
	else if (prepared_)
		active_ = true;
	// kResultOk, not the SDK base class' kNotImplemented: we do track this state, and
	// hosts are entitled to know that the call did something.
	return kResultOk;
}


tresult PLUGIN_API CrateProcessor::process (ProcessData& data)
{
	applyParameterChanges (data.inputParameterChanges);

	if (data.numOutputs <= 0 || !data.outputs || !data.outputs[0].channelBuffers32)
		return kResultOk;

	AudioBusBuffers& outBus = data.outputs[0];
	const int32_t channels = std::min<int32_t> (outBus.numChannels, host::kMaxChannels);
	if (channels <= 0 || data.numSamples <= 0)
		return kResultOk;

	AudioBusBuffers* inBus = data.numInputs > 0 ? &data.inputs[0] : nullptr;
	const bool haveInput = inBus && inBus->channelBuffers32 && inBus->numChannels > 0;

	if (!active_ || !prepared_)
	{
		// Not running: muting the track would be wrong, so copy the input through.
		for (int32_t c = 0; c < channels; ++c)
		{
			if (!haveInput)
			{
				std::fill (outBus.channelBuffers32[c],
				           outBus.channelBuffers32[c] + data.numSamples, 0.0f);
				continue;
			}
			const int32_t source = std::min (c, inBus->numChannels - 1);
			std::copy (inBus->channelBuffers32[source],
			           inBus->channelBuffers32[source] + data.numSamples,
			           outBus.channelBuffers32[c]);
		}
		outBus.silenceFlags = haveInput ? (inBus->silenceFlags & 1u) : 1u;
		return kResultOk;
	}

	float* inPointers[host::kMaxChannels] = {};
	float* outPointers[host::kMaxChannels] = {};
	uint64_t silent = 1;

	int32_t offset = 0;
	while (offset < data.numSamples)
	{
		const int32_t count = std::min (data.numSamples - offset, hostSetup_.maxBlockSize);
		for (int32_t c = 0; c < channels; ++c)
		{
			outPointers[c] = outBus.channelBuffers32[c] + offset;
			if (haveInput)
			{
				const int32_t source = std::min (c, inBus->numChannels - 1);
				inPointers[c] = inBus->channelBuffers32[source] + offset;
			}
		}

		host::AudioBlock in = host::makeBlock (inPointers, haveInput ? channels : 0, count);
		host::AudioBlock out = host::makeBlock (outPointers, channels, count);
		in.silenceFlags = haveInput ? (inBus->silenceFlags & 1u) : 1u;
		chain_.process (in, out);
		silent &= (out.silenceFlags & 1u);
		offset += count;
	}

	outBus.silenceFlags = silent;
	return kResultOk;
}

void CrateProcessor::applyParameterChanges (IParameterChanges* changes) noexcept
{
	if (!changes)
		return;
	const int32 count = changes->getParameterCount ();
	for (int32 i = 0; i < count; ++i)
	{
		IParamValueQueue* queue = changes->getParameterData (i);
		if (!queue)
			continue;
		const uint32_t id = static_cast<uint32_t> (queue->getParameterId ());
		int32_t slot = 0;
		host::params::SlotParam kind = host::params::SlotParam::Mix;
		if (!host::params::parseSlotId (id, slot, kind))
			continue;
		const int32 points = queue->getPointCount ();
		if (points <= 0)
			continue;
		// The last point wins for the whole block. Splitting the block at every automation
		// point is a refinement; the ramp in Slot already removes the zipper noise.
		int32 sampleOffset = 0;
		ParamValue value = 0.0;
		if (queue->getPoint (points - 1, sampleOffset, value) != kResultOk)
			continue;
		const double plain = host::params::toPlain (id, value);
		if (kind == host::params::SlotParam::Mix)
		{
			mixValues_[slot].store (static_cast<float> (plain), std::memory_order_relaxed);
			chain_.slot (slot).setMix (static_cast<float> (plain));
		}
		else
		{
			const bool bypassed = plain >= 0.5;
			bypassValues_[slot].store (bypassed, std::memory_order_relaxed);
			chain_.slot (slot).setBypass (bypassed);
		}
	}
}

uint32 PLUGIN_API CrateProcessor::getLatencySamples ()
{
	return static_cast<uint32> (reportedLatency_.load (std::memory_order_relaxed));
}

uint32 PLUGIN_API CrateProcessor::getTailSamples ()
{
	const int32_t tail = reportedTail_.load (std::memory_order_relaxed);
	if (tail >= std::numeric_limits<int32_t>::max ())
		return kInfiniteTail;
	return static_cast<uint32> (tail);
}

tresult PLUGIN_API CrateProcessor::getState (IBStream* stream)
{
	if (!stream)
		return kResultFalse;

	state::ContainerState snapshot = state_;
	snapshot.normalize ();
	storeSlotValues (snapshot);

	std::vector<uint8_t> blob;
	std::string error;
	if (!state::encode (snapshot, blob, error))
		return kResultFalse;

	int32 written = 0;
	if (stream->write (blob.data (), static_cast<int32> (blob.size ()), &written) != kResultOk)
		return kResultFalse;
	return kResultOk;
}

tresult PLUGIN_API CrateProcessor::setState (IBStream* stream)
{
	state::ContainerState incoming;
	if (!readContainerState (stream, incoming))
		return kResultFalse;

	// Message thread, host-inactive by contract. Stop everything before touching it.
	releaseChain ();
	unloadSlots ();

	state_ = incoming;
	state_.normalize ();
	const int32_t slotCount =
	    std::min (static_cast<int32_t> (state_.slots.size ()), host::kMaxSlots);
	chain_.setSlotCount (slotCount);

	for (int32_t i = 0; i < slotCount; ++i)
	{
		const state::SlotState& slotState = state_.slots[static_cast<size_t> (i)];
		mixValues_[i].store (slotState.mix, std::memory_order_relaxed);
		bypassValues_[i].store (slotState.bypass, std::memory_order_relaxed);

		host::Slot& slot = chain_.slot (i);
		slot.setMix (slotState.mix);
		slot.setBypass (slotState.bypass);

		if (slotState.isEmpty ())
		{
			slot.clear ();
			continue;
		}

		host::SlotIdentity identity;
		identity.uid = slotState.uid;
		identity.name = slotState.name;
		identity.vendor = slotState.vendor;
		identity.version = slotState.version;
		identity.bundlePath = slotState.bundlePath;

		// Safe mode: the journal says the host died while loading this plugin. Do not run
		// it again until the user asks for it explicitly; name it on the card instead.
		if (crashSuspect_.found && !identity.uid.empty () &&
		    iequals (crashSuspect_.record.uid, identity.uid))
		{
			slot.setStatus (registry::Status::Crashed,
			                "the host crashed while loading this plugin last time; it was "
			                "left out of this session");
			std::fprintf (stderr, "crate: safe-mode skip for %s\n", identity.name.c_str ());
			continue;
		}

		host::LoadRequest request;
		request.bundlePath = identity.bundlePath;
		request.uid = identity.uid;
		request.name = identity.name;
		if (journal_)
			journal_->begin ("load_begin", identity.uid, identity.name, identity.bundlePath);
		host::LoadResult result = loader_ ? loader_->load (request) : host::LoadResult ();
		if (journal_)
			journal_->end ("load_end");

		if (!result.instance)
		{
			slot.clear ();
			slot.setStatus (result.status, result.error);
			std::fprintf (stderr, "crate: load failed for %s: %s\n",
			              identity.bundlePath.c_str (), result.error.c_str ());
			continue;
		}
		if (!slotState.componentState.empty ())
			result.instance->setComponentState (slotState.componentState);
		if (!slotState.controllerState.empty ())
			result.instance->setControllerState (slotState.controllerState);

		std::string loadError;
		slot.load (std::move (result.instance), identity, registry::Status::Ok, loadError);
	}

	prepared_ = false;
	return kResultOk;
}


bool CrateProcessor::loadSlot (int32_t slotIndex, const host::SlotIdentity& identity,
                               std::string& error)
{
	error.clear ();
	if (slotIndex < 0 || slotIndex >= chain_.slotCount ())
	{
		error = "slot index out of range";
		return false;
	}

	const bool wasActive = active_;
	if (prepared_)
		releaseChain ();

	host::Slot& slot = chain_.slot (slotIndex);
	slot.clear ();

	host::LoadRequest request;
	request.bundlePath = identity.bundlePath;
	request.uid = identity.uid;
	request.name = identity.name;
	if (journal_)
		journal_->begin ("load_begin", identity.uid, identity.name, identity.bundlePath);
	host::LoadResult result = loader_ ? loader_->load (request) : host::LoadResult ();
	if (journal_)
		journal_->end ("load_end");

	if (!result.instance)
	{
		slot.setStatus (result.status, result.error);
		error = result.error;
		if (wasActive)
		{
			std::string ignored;
			prepareChain (ignored);
		}
		return false;
	}

	std::string loadError;
	slot.load (std::move (result.instance), identity, registry::Status::Ok, loadError);

	while (static_cast<int32_t> (state_.slots.size ()) <= slotIndex)
		state_.slots.push_back (state::SlotState ());
	state::SlotState& stored = state_.slots[static_cast<size_t> (slotIndex)];
	stored.uid = identity.uid;
	stored.name = identity.name;
	stored.vendor = identity.vendor;
	stored.version = identity.version;
	stored.bundlePath = identity.bundlePath;
	stored.mix = slot.mix ();
	stored.bypass = slot.bypass ();
	stored.componentState.clear ();
	stored.controllerState.clear ();

	if (wasActive)
	{
		std::string prepareError;
		if (!prepareChain (prepareError))
		{
			error = prepareError;
			return false;
		}
	}
	return true;
}

bool CrateProcessor::prepareChain (std::string& error)
{
	error.clear ();
	releaseChain ();
	chain_.setSlotCount (
	    std::min (static_cast<int32_t> (state_.slots.size ()), host::kMaxSlots));

	if (!hostSetup_.isValid ())
		return true; // no setupProcessing yet: stay unprepared, the DAW will call it

	if (!chain_.prepare (hostSetup_, error))
		return false;

	prepared_ = true;
	reportedLatency_.store (chain_.latencySamples (), std::memory_order_relaxed);
	reportedTail_.store (chain_.tailSamples (), std::memory_order_relaxed);
	return true;
}

void CrateProcessor::releaseChain ()
{
	prepared_ = false;
	chain_.release ();
	reportedLatency_.store (0, std::memory_order_relaxed);
	reportedTail_.store (0, std::memory_order_relaxed);
}

void CrateProcessor::unloadSlots ()
{
	for (int32_t i = 0; i < chain_.slotCount (); ++i)
		chain_.slot (i).clear ();
}

void CrateProcessor::storeSlotValues (state::ContainerState& out)
{
	const int32_t slotCount =
	    std::min (static_cast<int32_t> (out.slots.size ()), chain_.slotCount ());
	for (int32_t i = 0; i < slotCount; ++i)
	{
		state::SlotState& stored = out.slots[static_cast<size_t> (i)];
		stored.mix = mixValues_[i].load (std::memory_order_relaxed);
		stored.bypass = bypassValues_[i].load (std::memory_order_relaxed);

		host::Slot& slot = chain_.slot (i);
		if (!slot.hasInstance ())
			continue;
		std::vector<uint8_t> blob;
		if (slot.instance ()->getComponentState (blob))
			stored.componentState = std::move (blob);
		blob.clear ();
		if (slot.instance ()->getControllerState (blob))
			stored.controllerState = std::move (blob);
	}
}

bool CrateProcessor::readContainerState (IBStream* stream, state::ContainerState& out)
{
	std::vector<uint8_t> data;
	if (!readStreamBytes (stream, data))
		return false;

	std::string error;
	const state::DecodeStatus status = state::decode (data.data (), data.size (), out, error);
	if (status == state::DecodeStatus::Empty)
	{
		out = state::ContainerState ();
		return true;
	}
	return status == state::DecodeStatus::Ok;
}

} // namespace crate::vst3
