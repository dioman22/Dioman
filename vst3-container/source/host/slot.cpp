#include "slot.h"

#include <algorithm>
#include <string>

namespace crate::host {

namespace {

int32_t clampInt (int32_t value, int32_t low, int32_t high)
{
	if (value < low)
		return low;
	if (value > high)
		return high;
	return value;
}

} // namespace

Slot::Slot () = default;

Slot::~Slot ()
{
	release ();
}

void Slot::setStatus (registry::Status status, std::string detail)
{
	status_.store (status, std::memory_order_relaxed);
	statusDetail_ = std::move (detail);
}

void Slot::setMix (float mix) noexcept
{
	if (mix < 0.0f)
		mix = 0.0f;
	if (mix > 1.0f)
		mix = 1.0f;
	mix_.store (mix, std::memory_order_relaxed);
}

float Slot::targetWetGain () const noexcept
{
	if (bypass_.load (std::memory_order_relaxed))
		return 0.0f;
	const float value = mix_.load (std::memory_order_relaxed);
	if (value < 0.0f)
		return 0.0f;
	if (value > 1.0f)
		return 1.0f;
	return value;
}

bool Slot::load (std::unique_ptr<PluginInstance> instance, const SlotIdentity& identity,
                 registry::Status status, std::string& error)
{
	error.clear ();
	if (prepared_)
	{
		error = "the slot is still prepared; release it before loading another plugin";
		return false;
	}
	instance_ = std::move (instance);
	identity_ = identity;
	latency_ = 0;
	tail_ = 0;
	status_.store (status, std::memory_order_relaxed);
	return true;
}

void Slot::clear ()
{
	instance_.reset ();
	identity_ = SlotIdentity ();
	latency_ = 0;
	tail_ = 0;
	status_.store (registry::Status::Unknown, std::memory_order_relaxed);
}

void Slot::releaseBuffers ()
{
	std::vector<float> ().swap (pluginInStorage_);
	std::vector<float> ().swap (pluginOutStorage_);
	std::vector<float> ().swap (wetStorage_);
	std::vector<float> ().swap (dryStorage_);
	std::vector<float> ().swap (gainRamp_);
	std::vector<float*> ().swap (pluginInPtrs_);
	std::vector<float*> ().swap (pluginOutPtrs_);
	std::vector<float*> ().swap (wetPtrs_);
	std::vector<float*> ().swap (dryPtrs_);
	pluginIn_ = AudioBlock ();
	pluginOut_ = AudioBlock ();
	wet_ = AudioBlock ();
	dry_ = AudioBlock ();
}

void Slot::release ()
{
	if (prepared_ && instance_)
		instance_->stopProcessing ();
	prepared_ = false;
	releaseBuffers ();
	dryDelay_.resize (0, 0);
	latency_ = 0;
	tail_ = 0;
	pluginInChannels_ = 0;
	pluginOutChannels_ = 0;
	drySilent_ = true;
	silentRun_ = 0;
	lastOutputSilent_ = true;
}

bool Slot::prepare (const ProcessSetup& setup, int32_t containerChannels, std::string& error)
{
	error.clear ();
	release ();
	if (!setup.isValid ())
	{
		error = "invalid process setup";
		return false;
	}
	setup_ = setup;
	containerChannels_ = clampInt (containerChannels, 1, kMaxChannels);
	rampSamples_ = std::max (1, static_cast<int32_t> (setup_.sampleRate * kRampSeconds + 0.5));
	wetGain_ = targetWetGain (); // resume at the saved value: no ramp on project load
	drySilent_ = true;
	silentRun_ = 0;
	lastOutputSilent_ = true;

	if (!instance_)
	{
		prepared_ = true; // an empty slot is a valid, prepared pass-through
		return true;
	}

	if (!instance_->startProcessing (setup_, error))
	{
		if (error.empty ())
			error = "the plugin refused to start processing";
		status_.store (registry::Status::Incompatible, std::memory_order_relaxed);
		return false;
	}

	latency_ = std::max (0, instance_->latencySamples ());
	tail_ = std::max (0, instance_->tailSamples ());
	if (static_cast<double> (latency_) > setup_.sampleRate * kMaxLatencySeconds)
	{
		error = "the plugin reports " + std::to_string (latency_) +
		        " samples of latency, more than the 2 s budget";
		instance_->stopProcessing ();
		latency_ = 0;
		status_.store (registry::Status::Incompatible, std::memory_order_relaxed);
		return false;
	}

	if (instance_->numOutputChannels () <= 0)
	{
		error = "the plugin has no audio output";
		instance_->stopProcessing ();
		status_.store (registry::Status::Incompatible, std::memory_order_relaxed);
		return false;
	}

	pluginInChannels_ = clampInt (instance_->numInputChannels (), 0, kMaxChannels);
	pluginOutChannels_ = clampInt (instance_->numOutputChannels (), 1, kMaxChannels);

	const int32_t maxBlock = setup_.maxBlockSize;
	auto allocate = [maxBlock] (std::vector<float>& storage, std::vector<float*>& pointers,
	                            AudioBlock& block, int32_t channels) {
		storage.assign (static_cast<size_t> (channels) * static_cast<size_t> (maxBlock), 0.0f);
		pointers.assign (static_cast<size_t> (channels), nullptr);
		for (int32_t c = 0; c < channels; ++c)
			pointers[static_cast<size_t> (c)] =
			    storage.data () + static_cast<size_t> (c) * static_cast<size_t> (maxBlock);
		block = makeBlock (pointers.data (), channels, 0);
	};

	allocate (pluginInStorage_, pluginInPtrs_, pluginIn_, pluginInChannels_);
	allocate (pluginOutStorage_, pluginOutPtrs_, pluginOut_, pluginOutChannels_);
	allocate (wetStorage_, wetPtrs_, wet_, containerChannels_);
	allocate (dryStorage_, dryPtrs_, dry_, containerChannels_);
	gainRamp_.assign (static_cast<size_t> (maxBlock) + 1, 0.0f);

	if (latency_ > 0 &&
	    !dryDelay_.resize (containerChannels_, latency_ + maxBlock + 1))
	{
		error = "could not allocate the latency-compensation delay line";
		instance_->stopProcessing ();
		releaseBuffers ();
		status_.store (registry::Status::Unreadable, std::memory_order_relaxed);
		return false;
	}

	prepared_ = true;
	status_.store (registry::Status::Ok, std::memory_order_relaxed);
	return true;
}


void Slot::process (const AudioBlock& in, AudioBlock& out) noexcept
{
	if (!out.channels || out.numChannels <= 0 || out.numSamples <= 0)
		return;

	// Empty, unloaded or unprepared slot: exact pass-through, no plugin code runs.
	if (!instance_ || !prepared_)
	{
		copyBlock (in, out);
		out.silenceFlags = in.silenceFlags;
		lastOutputSilent_ = in.silent ();
		return;
	}

	const int32_t count = std::min (out.numSamples, setup_.maxBlockSize);
	if (count <= 0)
	{
		clearBlock (out);
		out.setSilent (true);
		lastOutputSilent_ = true;
		return;
	}

	pluginIn_.numSamples = count;
	pluginOut_.numSamples = count;
	wet_.numSamples = count;
	dry_.numSamples = count;

	// 1. Feed the plugin. The host's silence flag is forwarded, not invented: a
	//    plugin that honours it saves real CPU, and one that ignores it still works.
	mapToPluginInput (in, pluginIn_);
	pluginIn_.silenceFlags = in.silenceFlags;
	pluginOut_.silenceFlags = 0;

	// 2. Run foreign code. Nothing below may throw; if the plugin returns garbage we
	//    scrub it here rather than let a NaN reach the DAW's master bus.
	instance_->process (pluginIn_, pluginOut_);
	scrubBlock (pluginOut_);
	mapFromPluginOutput (pluginOut_, wet_);

	// 3. Dry path, delayed by exactly the plugin's latency so blending stays in phase.
	if (latency_ > 0)
	{
		dryDelay_.write (in);
		dryDelay_.readDelayed (dry_, latency_);
	}
	else
	{
		copyBlock (in, dry_);
	}

	// 4. Blend with a per-sample ramp: bypass and mix changes never click.
	const float target = targetWetGain ();
	const float step = 1.0f / static_cast<float> (rampSamples_);
	float gain = wetGain_;
	for (int32_t n = 0; n < count; ++n)
	{
		gainRamp_[static_cast<size_t> (n)] = gain;
		if (gain < target)
			gain = std::min (target, gain + step);
		else if (gain > target)
			gain = std::max (target, gain - step);
	}
	wetGain_ = gain;

	const int32_t channels = std::min (out.numChannels, containerChannels_);
	for (int32_t c = 0; c < channels; ++c)
	{
		float* dst = out.channels[c];
		if (!dst)
			continue;
		const float* wet = c < wet_.numChannels ? wet_.channels[c] : nullptr;
		const float* dry = c < dry_.numChannels ? dry_.channels[c] : nullptr;
		for (int32_t n = 0; n < count; ++n)
		{
			const float d = dry ? dry[n] : 0.0f;
			const float w = wet ? wet[n] : d;
			const float g = gainRamp_[static_cast<size_t> (n)];
			dst[n] = d + g * (w - d);
		}
		if (count < out.numSamples)
			std::fill (dst + count, dst + out.numSamples, 0.0f);
	}
	// A host that hands us more channels than we negotiated gets silence, not stale
	// memory and not an out-of-bounds read.
	for (int32_t c = channels; c < out.numChannels; ++c)
	{
		if (out.channels[c])
			std::fill (out.channels[c], out.channels[c] + out.numSamples, 0.0f);
	}

	// 5. Silence accounting. The dry path keeps ringing for `latency_` samples after a
	//    silent block, so "output is silent" is only true once the delay line drained.
	if (!in.silent ())
	{
		drySilent_ = false;
		silentRun_ = 0;
	}
	else if (!drySilent_)
	{
		silentRun_ += count;
		if (silentRun_ > latency_)
			drySilent_ = true;
	}
	const bool silent = pluginOut_.silent () && drySilent_;
	out.setSilent (silent);
	lastOutputSilent_ = silent;
}

} // namespace crate::host
