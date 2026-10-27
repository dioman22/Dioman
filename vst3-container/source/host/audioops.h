// Realtime-safe block operations and the dry-path delay line.
//
// Everything in this file is called from process(). No allocation, no locks, no
// exceptions, no I/O. The functions tolerate mismatched channel counts because a
// hosted mono plugin must still work on a stereo track.
#pragma once

#include "audiotypes.h"

#include <cstdint>
#include <vector>

namespace crate::host {

/** Copy min(src,dst) channels and min(samples); extra dst channels are cleared. */
void copyBlock (const AudioBlock& src, const AudioBlock& dst) noexcept;

void clearBlock (const AudioBlock& block) noexcept;

/** Container channels -> plugin input channels.
 *  mono plugin on a stereo track: 0.5 * (L + R). mono source on a stereo plugin:
 *  the mono channel is duplicated. Extra plugin channels get the last source channel. */
void mapToPluginInput (const AudioBlock& src, const AudioBlock& dst) noexcept;

/** Plugin output channels -> container channels. mono plugin: both sides get the
 *  mono signal. Extra plugin channels are dropped. */
void mapFromPluginOutput (const AudioBlock& src, const AudioBlock& dst) noexcept;

/** Replace NaN/Inf with 0. Returns true when something had to be fixed. */
bool scrubBlock (const AudioBlock& block) noexcept;

/** True when every sample of every channel is exactly zero. */
bool blockIsZero (const AudioBlock& block) noexcept;

/**
 * Fixed-size circular delay for the latency-compensated dry path.
 *
 * A hosted plugin with latency L returns its output L samples late. Blending that
 * with an undelayed dry signal cancels low frequencies, so the dry path is delayed
 * by exactly L as well. Capacity is allocated once in prepare(); write()/read() are
 * audio-thread safe and never touch the allocator.
 */
class DelayLine
{
public:
	/** Message thread only. `channels` * `capacitySamples` floats are allocated. */
	bool resize (int32_t channels, int32_t capacitySamples);

	/** Audio thread. Drop every sample but keep the allocation. */
	void clear () noexcept;

	/** Audio thread. Append one block (min channels/samples, extra channels cleared). */
	void write (const AudioBlock& src) noexcept;

	/** Audio thread. Read the block written `delaySamples` ago into `dst`.
	 *  `delaySamples` is clamped to the allocated capacity. Does not advance. */
	void readDelayed (const AudioBlock& dst, int32_t delaySamples) const noexcept;

	int32_t channels () const noexcept { return channels_; }
	int32_t capacity () const noexcept { return capacity_; }

private:
	float at (int32_t channel, int32_t index) const noexcept;
	void put (int32_t channel, int32_t index, float value) noexcept;

	std::vector<float> buffer_;
	int32_t channels_ = 0;
	int32_t capacity_ = 0;
	int32_t writePos_ = 0;
};

} // namespace crate::host
