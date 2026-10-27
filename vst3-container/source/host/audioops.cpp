#include "audioops.h"

#include <algorithm>

namespace crate::host {

namespace {

int32_t samples (const AudioBlock& a, const AudioBlock& b) noexcept
{
	return std::min (a.numSamples, b.numSamples);
}

} // namespace

void clearBlock (const AudioBlock& block) noexcept
{
	if (!block.channels)
		return;
	for (int32_t c = 0; c < block.numChannels; ++c)
	{
		float* channel = block.channels[c];
		if (!channel)
			continue;
		std::fill (channel, channel + block.numSamples, 0.0f);
	}
}

void copyBlock (const AudioBlock& src, const AudioBlock& dst) noexcept
{
	if (!dst.channels)
		return;
	const int32_t count = samples (src, dst);
	const int32_t copied = std::min (src.numChannels, dst.numChannels);
	for (int32_t c = 0; c < dst.numChannels; ++c)
	{
		float* out = dst.channels[c];
		if (!out)
			continue;
		if (c < copied && src.channels && src.channels[c])
			std::copy (src.channels[c], src.channels[c] + count, out);
		else
			std::fill (out, out + dst.numSamples, 0.0f);
	}
}

void mapToPluginInput (const AudioBlock& src, const AudioBlock& dst) noexcept
{
	if (!dst.channels || dst.numChannels <= 0)
		return;
	const int32_t count = samples (src, dst);
	if (count <= 0)
		return;
	if (!src.channels || src.numChannels <= 0)
	{
		clearBlock (dst);
		return;
	}
	if (dst.numChannels == 1 && src.numChannels >= 2 && src.channels[0] && src.channels[1])
	{
		const float* left = src.channels[0];
		const float* right = src.channels[1];
		float* out = dst.channels[0];
		for (int32_t n = 0; n < count; ++n)
			out[n] = 0.5f * (left[n] + right[n]);
		if (count < dst.numSamples)
			std::fill (out + count, out + dst.numSamples, 0.0f);
		return;
	}
	for (int32_t c = 0; c < dst.numChannels; ++c)
	{
		float* out = dst.channels[c];
		if (!out)
			continue;
		const int32_t source = std::min (c, src.numChannels - 1);
		const float* in = src.channels[source];
		if (in)
			std::copy (in, in + count, out);
		if (count < dst.numSamples)
			std::fill (out + count, out + dst.numSamples, 0.0f);
	}
}

void mapFromPluginOutput (const AudioBlock& src, const AudioBlock& dst) noexcept
{
	if (!dst.channels)
		return;
	const int32_t count = samples (src, dst);
	if (!src.channels || src.numChannels <= 0)
	{
		clearBlock (dst);
		return;
	}
	for (int32_t c = 0; c < dst.numChannels; ++c)
	{
		float* out = dst.channels[c];
		if (!out)
			continue;
		const int32_t source = std::min (c, src.numChannels - 1);
		const float* in = src.channels[source];
		if (in && count > 0)
			std::copy (in, in + count, out);
		if (count < dst.numSamples)
			std::fill (out + count, out + dst.numSamples, 0.0f);
	}
}

bool scrubBlock (const AudioBlock& block) noexcept
{
	bool fixed = false;
	if (!block.channels)
		return false;
	for (int32_t c = 0; c < block.numChannels; ++c)
	{
		float* channel = block.channels[c];
		if (!channel)
			continue;
		for (int32_t n = 0; n < block.numSamples; ++n)
		{
			const float value = channel[n];
			// NaN fails every comparison, including the self-comparison.
			if (value != value || value > 3.402823466e+38f || value < -3.402823466e+38f)
			{
				channel[n] = 0.0f;
				fixed = true;
			}
		}
	}
	return fixed;
}

bool blockIsZero (const AudioBlock& block) noexcept
{
	if (!block.channels)
		return true;
	for (int32_t c = 0; c < block.numChannels; ++c)
	{
		const float* channel = block.channels[c];
		if (!channel)
			continue;
		for (int32_t n = 0; n < block.numSamples; ++n)
		{
			if (channel[n] != 0.0f)
				return false;
		}
	}
	return true;
}

bool DelayLine::resize (int32_t channels, int32_t capacitySamples)
{
	if (channels <= 0 || capacitySamples <= 0)
	{
		buffer_.clear ();
		channels_ = 0;
		capacity_ = 0;
		writePos_ = 0;
		return false;
	}
	channels_ = channels;
	capacity_ = capacitySamples;
	buffer_.assign (static_cast<size_t> (channels_) * static_cast<size_t> (capacity_), 0.0f);
	writePos_ = 0;
	return true;
}

void DelayLine::clear () noexcept
{
	std::fill (buffer_.begin (), buffer_.end (), 0.0f);
	writePos_ = 0;
}

float DelayLine::at (int32_t channel, int32_t index) const noexcept
{
	return buffer_[static_cast<size_t> (channel) * static_cast<size_t> (capacity_) +
	               static_cast<size_t> (index)];
}

void DelayLine::put (int32_t channel, int32_t index, float value) noexcept
{
	buffer_[static_cast<size_t> (channel) * static_cast<size_t> (capacity_) +
	        static_cast<size_t> (index)] = value;
}

void DelayLine::write (const AudioBlock& src) noexcept
{
	if (capacity_ <= 0 || channels_ <= 0)
		return;
	const int32_t count = std::min (src.numSamples, capacity_);
	const int32_t copied = std::min (src.numChannels, channels_);
	for (int32_t c = 0; c < channels_; ++c)
	{
		const float* in = (c < copied && src.channels) ? src.channels[c] : nullptr;
		int32_t index = writePos_;
		for (int32_t n = 0; n < count; ++n)
		{
			put (c, index, in ? in[n] : 0.0f);
			if (++index == capacity_)
				index = 0;
		}
	}
	writePos_ += count;
	while (writePos_ >= capacity_)
		writePos_ -= capacity_;
}

void DelayLine::readDelayed (const AudioBlock& dst, int32_t delaySamples) const noexcept
{
	if (capacity_ <= 0 || channels_ <= 0 || !dst.channels)
	{
		clearBlock (dst);
		return;
	}
	// Reading is relative to the end of the history: delay 0 returns the block that was
	// written last. The block being read plus the delay has to fit in the ring.
	const int32_t count = std::min (dst.numSamples, capacity_);
	int32_t delay = std::min (std::max (delaySamples, 0), capacity_ - count);
	if (delay < 0)
		delay = 0;
	const int32_t copied = std::min (dst.numChannels, channels_);

	int32_t base = writePos_ - count - delay;
	while (base < 0)
		base += capacity_;
	while (base >= capacity_)
		base -= capacity_;

	for (int32_t c = 0; c < dst.numChannels; ++c)
	{
		float* out = dst.channels[c];
		if (!out)
			continue;
		if (c >= copied)
		{
			std::fill (out, out + dst.numSamples, 0.0f);
			continue;
		}
		int32_t index = base;
		for (int32_t n = 0; n < count; ++n)
		{
			out[n] = at (c, index);
			if (++index == capacity_)
				index = 0;
		}
		if (count < dst.numSamples)
			std::fill (out + count, out + dst.numSamples, 0.0f);
	}
}

} // namespace crate::host
