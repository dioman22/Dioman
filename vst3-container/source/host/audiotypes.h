// The audio shapes the container's own signal path speaks. Deliberately independent
// of the VST3 SDK: the engine below (slot, chain) is the code that runs under a
// 64-sample buffer, so it has to be unit-testable with a fake plugin and a plain
// compiler. The VST3 layer only converts between these shapes and ProcessData.
//
// Conventions:
//  * channels are de-interleaved (VST3 style): one float* per channel,
//  * `silenceFlags` uses the VST3 bit layout, bit 0 = main bus,
//  * nothing here owns memory; blocks are views over buffers allocated in prepare().
#pragma once

#include <cstdint>

namespace crate::host {

/** Engine-side ceiling. The container itself negotiates 1..kMaxChannels channels. */
constexpr int32_t kMaxChannels = 8;

/** Slots in the container. Also the ceiling for the parameter map. */
constexpr int32_t kMaxSlots = 8;

/** Block size ceiling we are willing to pre-allocate for. */
constexpr int32_t kMaxBlockSize = 4096;

/** Latency ceiling per hosted plugin: 2 seconds. Beyond that the card is marked
 *  incompatible instead of silently allocating a second of delay line per channel. */
constexpr double kMaxLatencySeconds = 2.0;

struct ProcessSetup
{
	double sampleRate = 48000.0;
	int32_t maxBlockSize = 512;
	int32_t numChannels = 2;

	bool isValid () const
	{
		return sampleRate > 0.0 && maxBlockSize > 0 && maxBlockSize <= kMaxBlockSize &&
		       numChannels > 0 && numChannels <= kMaxChannels;
	}
};

/** A view over one block of de-interleaved audio. */
struct AudioBlock
{
	int32_t numChannels = 0;
	int32_t numSamples = 0;
	float* const* channels = nullptr;
	/** Bit n set = bus n is silent. Bit 0 is the main bus, as in VST3. */
	uint64_t silenceFlags = 0;

	bool silent () const { return (silenceFlags & 1u) != 0; }
	void setSilent (bool value)
	{
		if (value)
			silenceFlags |= 1u;
		else
			silenceFlags &= ~static_cast<uint64_t> (1u);
	}
	bool valid () const { return numChannels >= 0 && numSamples >= 0 && (numChannels == 0 || channels != nullptr); }
};

/** Build a block from a channel-pointer table. */
inline AudioBlock makeBlock (float* const* channels, int32_t numChannels, int32_t numSamples)
{
	AudioBlock block;
	block.numChannels = numChannels;
	block.numSamples = numSamples;
	block.channels = channels;
	return block;
}

} // namespace crate::host
