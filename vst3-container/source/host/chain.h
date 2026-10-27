// The container's own signal path: the loaded slots, in series, in the order the grid
// shows them. One buffer pair ping-pongs between slots so a chain of four plugins
// still allocates nothing per block.
//
// A slot without an instance is skipped outright (the signal is already in the current
// buffer). A slot that is loaded but bypassed is *not* skipped: its dry path is delayed
// by its latency, so skipping it would shift the signal in time.
#pragma once

#include "slot.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace crate::host {

class Chain
{
public:
	static constexpr int32_t kMaxSlots = 8;

	Chain ();

	/** Message thread, before prepare(). Values above kMaxSlots are clamped. */
	void setSlotCount (int32_t count);
	int32_t slotCount () const { return slotCount_; }

	Slot& slot (int32_t index);
	const Slot& slot (int32_t index) const;

	/** Allocates the ping-pong buffers and prepares every slot that has an instance.
	 *  A slot that fails to start is left in pass-through with its status explaining
	 *  why; the rest of the chain still runs. Returns false only when the chain itself
	 *  cannot be prepared. */
	bool prepare (const ProcessSetup& setup, std::string& error);
	void release ();

	bool isPrepared () const { return prepared_; }
	const ProcessSetup& setup () const { return setup_; }

	/** Audio thread. `in` and `out` must have the channel count from the setup. */
	void process (const AudioBlock& in, AudioBlock& out) noexcept;

	/** Sum of the loaded plugins' latency, which is what the container reports. */
	int32_t latencySamples () const noexcept;
	/** Sum of the loaded plugins' tails, saturating at INT32_MAX (VST3 kInfiniteTail). */
	int32_t tailSamples () const noexcept;

	int32_t loadedSlots () const noexcept;

private:
	std::array<Slot, kMaxSlots> slots_;
	int32_t slotCount_ = 4;
	ProcessSetup setup_;
	bool prepared_ = false;

	std::vector<float> bufferA_;
	std::vector<float> bufferB_;
	std::vector<float*> pointersA_;
	std::vector<float*> pointersB_;
	AudioBlock blockA_;
	AudioBlock blockB_;
};

} // namespace crate::host
