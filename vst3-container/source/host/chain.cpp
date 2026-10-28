#include "chain.h"

#include <algorithm>
#include <cstdio>
#include <limits>

namespace crate::host {

Chain::Chain () = default;

void Chain::setSlotCount (int32_t count)
{
	if (count < 1)
		count = 1;
	if (count > kMaxSlots)
		count = kMaxSlots;
	slotCount_ = count;
}

Slot& Chain::slot (int32_t index)
{
	if (index < 0)
		index = 0;
	if (index >= kMaxSlots)
		index = kMaxSlots - 1;
	return slots_[static_cast<size_t> (index)];
}

const Slot& Chain::slot (int32_t index) const
{
	if (index < 0)
		index = 0;
	if (index >= kMaxSlots)
		index = kMaxSlots - 1;
	return slots_[static_cast<size_t> (index)];
}

bool Chain::prepare (const ProcessSetup& setup, std::string& error)
{
	error.clear ();
	release ();
	if (!setup.isValid ())
	{
		error = "invalid process setup";
		return false;
	}
	setup_ = setup;

	const size_t frames = static_cast<size_t> (setup.numChannels) * setup.maxBlockSize;
	bufferA_.assign (frames, 0.0f);
	bufferB_.assign (frames, 0.0f);
	if (bufferA_.size () != frames || bufferB_.size () != frames)
	{
		error = "could not allocate the chain buffers";
		release ();
		return false;
	}
	pointersA_.assign (static_cast<size_t> (setup.numChannels), nullptr);
	pointersB_.assign (static_cast<size_t> (setup.numChannels), nullptr);
	for (int32_t c = 0; c < setup.numChannels; ++c)
	{
		pointersA_[static_cast<size_t> (c)] =
		    bufferA_.data () + static_cast<size_t> (c) * static_cast<size_t> (setup.maxBlockSize);
		pointersB_[static_cast<size_t> (c)] =
		    bufferB_.data () + static_cast<size_t> (c) * static_cast<size_t> (setup.maxBlockSize);
	}
	blockA_ = makeBlock (pointersA_.data (), setup.numChannels, 0);
	blockB_ = makeBlock (pointersB_.data (), setup.numChannels, 0);
	prepared_ = true;

	// Prepare every slot. Failures are per-slot and non-fatal: a plugin that refuses to
	// start must not take the whole container (and the DAW project) down with it.
	for (int32_t i = 0; i < slotCount_; ++i)
	{
		std::string slotError;
		slots_[static_cast<size_t> (i)].prepare (setup, setup.numChannels, slotError);
		if (!slotError.empty ())
			std::fprintf (stderr, "crate: slot %d prepare said: %s\n", i, slotError.c_str ());
	}
	return true;
}

void Chain::release ()
{
	for (auto& slot : slots_)
		slot.release ();
	std::vector<float> ().swap (bufferA_);
	std::vector<float> ().swap (bufferB_);
	std::vector<float*> ().swap (pointersA_);
	std::vector<float*> ().swap (pointersB_);
	blockA_ = AudioBlock ();
	blockB_ = AudioBlock ();
	prepared_ = false;
}

void Chain::process (const AudioBlock& in, AudioBlock& out) noexcept
{
	if (!prepared_ || !blockA_.channels || !blockB_.channels)
	{
		copyBlock (in, out);
		out.silenceFlags = in.silenceFlags;
		return;
	}

	const int32_t count = std::min (std::min (in.numSamples, out.numSamples), setup_.maxBlockSize);
	if (count <= 0)
	{
		clearBlock (out);
		out.setSilent (true);
		return;
	}

	blockA_.numSamples = count;
	blockB_.numSamples = count;
	copyBlock (in, blockA_);

	AudioBlock* current = &blockA_;
	AudioBlock* next = &blockB_;
	bool anyProcessed = false;
	for (int32_t i = 0; i < slotCount_; ++i)
	{
		Slot& s = slots_[static_cast<size_t> (i)];
		if (!s.hasInstance ())
			continue; // nothing loaded: the signal is already in `current`
		next->numSamples = count;
		s.process (*current, *next);
		std::swap (current, next);
		anyProcessed = true;
	}

	copyBlock (*current, out);
	if (count < out.numSamples)
	{
		// The host asked for more samples than we prepared for; never return stale data.
		for (int32_t c = 0; c < out.numChannels; ++c)
		{
			if (out.channels[c])
				std::fill (out.channels[c] + count, out.channels[c] + out.numSamples, 0.0f);
		}
	}
	if (!anyProcessed)
		out.silenceFlags = in.silenceFlags;
	else
		out.setSilent (current->silent ());
}

int32_t Chain::latencySamples () const noexcept
{
	int64_t total = 0;
	for (int32_t i = 0; i < slotCount_; ++i)
		total += slots_[static_cast<size_t> (i)].latencySamples ();
	if (total > std::numeric_limits<int32_t>::max ())
		return std::numeric_limits<int32_t>::max ();
	return static_cast<int32_t> (total);
}

int32_t Chain::tailSamples () const noexcept
{
	int64_t total = 0;
	for (int32_t i = 0; i < slotCount_; ++i)
		total += slots_[static_cast<size_t> (i)].tailSamples ();
	if (total > std::numeric_limits<int32_t>::max ())
		return std::numeric_limits<int32_t>::max ();
	return static_cast<int32_t> (total);
}

int32_t Chain::loadedSlots () const noexcept
{
	int32_t count = 0;
	for (int32_t i = 0; i < slotCount_; ++i)
	{
		if (slots_[static_cast<size_t> (i)].hasInstance ())
			++count;
	}
	return count;
}

} // namespace crate::host
