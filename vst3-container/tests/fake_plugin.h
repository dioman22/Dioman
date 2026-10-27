// Fake plugins and test buffers shared by the engine tests. They implement the same
// interface the VST3 layer implements, so a test failure here is an engine bug and not
// a question about the SDK.
#pragma once

#include "../source/host/audioops.h"
#include "../source/host/plugininstance.h"

#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>

// Test-only header: the fakes spell the engine types unqualified for readability.
using namespace crate::host;

namespace crate::testing {

// Internal linkage on purpose: every test binary gets its own copy of the fakes, and
// the helpers stay out of the way of anything else that includes this header.
namespace {

constexpr int32_t kBlock = 64;

/** De-interleaved test buffer with a stable pointer table. */
class Buffer
{
public:
	Buffer (int32_t channels, int32_t samples) : channels_ (channels), samples_ (samples)
	{
		storage_.assign (static_cast<size_t> (channels_) * static_cast<size_t> (samples_), 0.0f);
		pointers_.assign (static_cast<size_t> (channels_), nullptr);
		for (int32_t c = 0; c < channels_; ++c)
			pointers_[static_cast<size_t> (c)] =
			    storage_.data () + static_cast<size_t> (c) * static_cast<size_t> (samples_);
		block_ = makeBlock (pointers_.data (), channels_, samples_);
	}

	float* channel (int32_t index) { return pointers_[static_cast<size_t> (index)]; }
	const float* channel (int32_t index) const
	{
		return pointers_[static_cast<size_t> (index)];
	}
	AudioBlock& block () { return block_; }
	int32_t samples () const { return samples_; }
	int32_t channels () const { return channels_; }

	void fill (float left, float right = 0.0f)
	{
		for (int32_t n = 0; n < samples_; ++n)
		{
			channel (0)[n] = left;
			if (channels_ > 1)
				channel (1)[n] = right;
		}
	}

	void ramp ()
	{
		for (int32_t n = 0; n < samples_; ++n)
		{
			channel (0)[n] = static_cast<float> (n) * 0.01f;
			if (channels_ > 1)
				channel (1)[n] = -static_cast<float> (n) * 0.01f;
		}
	}

private:
	int32_t channels_;
	int32_t samples_;
	std::vector<float> storage_;
	std::vector<float*> pointers_;
	AudioBlock block_;
};

/** A plugin that does exactly what it says: gain, an optional true delay equal to its
 *  reported latency, optional tail, optional failure and optional NaN injection. */
class FakePlugin : public PluginInstance
{
public:
	FakePlugin (int32_t in, int32_t out, float gain, int32_t latency = 0, int32_t tail = 0,
	            bool failStart = false, bool injectNan = false)
	    : in_ (in), out_ (out), gain_ (gain), latency_ (latency), tail_ (tail),
	      failStart_ (failStart), injectNan_ (injectNan)
	{
	}

	const char* displayName () const override { return "Fake"; }
	int32_t numInputChannels () const override { return in_; }
	int32_t numOutputChannels () const override { return out_; }
	int32_t latencySamples () const override { return latency_; }
	int32_t tailSamples () const override { return tail_; }

	bool startProcessing (const ProcessSetup& setup, std::string& error) override
	{
		if (failStart_)
		{
			error = "fake plugin refuses to start";
			return false;
		}
		setup_ = setup;
		active_ = true;
		if (latency_ > 0)
			return delay_.resize (out_, latency_ + setup.maxBlockSize + 1);
		return true;
	}

	void stopProcessing () override { active_ = false; }

	void process (const AudioBlock& in, AudioBlock& out) noexcept override
	{
		++processCalls;
		if (latency_ > 0)
		{
			delay_.write (in);
			delay_.readDelayed (out, latency_);
		}
		else
		{
			for (int32_t c = 0; c < out.numChannels; ++c)
			{
				const int32_t source = in.numChannels > 0 ? (c % in.numChannels) : -1;
				for (int32_t n = 0; n < out.numSamples; ++n)
					out.channels[c][n] = source >= 0 ? in.channels[source][n] * gain_ : 0.0f;
			}
		}
		if (latency_ > 0)
		{
			for (int32_t c = 0; c < out.numChannels; ++c)
			{
				for (int32_t n = 0; n < out.numSamples; ++n)
					out.channels[c][n] *= gain_;
			}
		}
		if (injectNan_ && out.numChannels > 0 && out.numSamples > 0)
			out.channels[0][0] = std::numeric_limits<float>::quiet_NaN ();
		out.setSilent (in.silent ());
	}

	bool getComponentState (std::vector<uint8_t>& out) override
	{
		out.assign ({1, 2, 3});
		return true;
	}
	bool setComponentState (const std::vector<uint8_t>&) override { return true; }
	bool getControllerState (std::vector<uint8_t>& out) override
	{
		out.assign ({4, 5});
		return true;
	}
	bool setControllerState (const std::vector<uint8_t>&) override { return true; }

	int processCalls = 0;

private:
	int32_t in_;
	int32_t out_;
	float gain_;
	int32_t latency_;
	int32_t tail_;
	bool failStart_;
	bool injectNan_;
	bool active_ = false;
	ProcessSetup setup_;
	DelayLine delay_;
};

std::unique_ptr<PluginInstance> makeFake (int32_t in, int32_t out, float gain,
                                          int32_t latency = 0, int32_t tail = 0,
                                          bool failStart = false, bool injectNan = false)
{
	return std::unique_ptr<PluginInstance> (
	    new FakePlugin (in, out, gain, latency, tail, failStart, injectNan));
}

SlotIdentity fakeIdentity (const char* name = "Fake")
{
	SlotIdentity identity;
	identity.uid = "00112233445566778899AABBCCDDEEFF";
	identity.name = name;
	identity.vendor = "Test";
	identity.version = "1.0.0";
	identity.bundlePath = "/tmp/Fake.vst3";
	return identity;
}

ProcessSetup stereoSetup (double sampleRate = 48000.0, int32_t block = kBlock)
{
	ProcessSetup setup;
	setup.sampleRate = sampleRate;
	setup.maxBlockSize = block;
	setup.numChannels = 2;
	return setup;
}

} // namespace

} // namespace crate::testing
