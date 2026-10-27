// One slot of the container: a hosted plugin plus the plumbing that makes it safe to
// load, bypass and blend while the DAW is running audio.
//
// Why the dry path is delayed: a hosted plugin with latency L returns its output L
// samples late. Blending that against an undelayed dry signal combs the low end, so
// the dry path runs through a delay line of exactly L. That is also why a bypassed
// slot still costs CPU and still needs the plugin to stay active — dropping it would
// move the dry signal in time and change what the host compensates.
//
// Threading contract:
//  * load / clear / prepare / release: message thread only, never while processing,
//  * setBypass / setMix: any thread, lock-free (smoothed on the audio thread),
//  * process: audio thread only, allocation-free and noexcept.
#pragma once

#include "../registry/entry.h" // registry::Status is the shared card/slot vocabulary
#include "audioops.h"
#include "plugininstance.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace crate::host {

struct SlotIdentity
{
	std::string uid;        ///< 32 hex chars, empty when the registry had none
	std::string name;       ///< the host's display name, for the slot header
	std::string vendor;
	std::string version;
	std::string bundlePath; ///< absolute path it was loaded from

	bool empty () const { return uid.empty () && bundlePath.empty (); }
};

class Slot
{
public:
	/** 20 ms of ramp. Long enough to hide a click, short enough to feel instant. */
	static constexpr double kRampSeconds = 0.020;

	Slot ();
	~Slot ();

	Slot (const Slot&) = delete;
	Slot& operator= (const Slot&) = delete;

	// ---- message thread -------------------------------------------------------

	/** Takes ownership. Fails when the slot is still prepared: release it first. */
	bool load (std::unique_ptr<PluginInstance> instance, const SlotIdentity& identity,
	           registry::Status status, std::string& error);

	/** Drop the plugin. Message thread, after release() and with audio stopped. */
	void clear ();

	/** setupProcessing + setActive + allocate every buffer process() needs. */
	bool prepare (const ProcessSetup& setup, int32_t containerChannels, std::string& error);

	/** setActive(false) and free the buffers. Idempotent. */
	void release ();

	void setIdentity (const SlotIdentity& identity) { identity_ = identity; }
	const SlotIdentity& identity () const { return identity_; }

	void setStatus (registry::Status status, std::string detail = {});
	registry::Status status () const { return status_.load (std::memory_order_relaxed); }
	/** Sentence shown on the card, empty when the default explanation is enough. */
	const std::string& statusDetail () const { return statusDetail_; }

	bool isPrepared () const { return prepared_; }

	// ---- any thread -----------------------------------------------------------

	void setBypass (bool bypass) noexcept { bypass_.store (bypass, std::memory_order_relaxed); }
	bool bypass () const noexcept { return bypass_.load (std::memory_order_relaxed); }

	/** 0 = dry, 1 = fully wet. Bypass overrides mix: it forces the wet gain to 0
	 *  without touching the stored mix value the user chose. */
	void setMix (float mix) noexcept;
	float mix () const noexcept { return mix_.load (std::memory_order_relaxed); }

	// ---- audio thread ---------------------------------------------------------

	void process (const AudioBlock& in, AudioBlock& out) noexcept;

	// ---- reporting ------------------------------------------------------------

	bool hasInstance () const noexcept { return instance_ != nullptr; }
	PluginInstance* instance () noexcept { return instance_.get (); }

	int32_t latencySamples () const noexcept { return instance_ ? latency_ : 0; }
	int32_t tailSamples () const noexcept { return instance_ ? tail_ : 0; }

	/** True when the last block produced nothing at all (input silent, plugin silent,
	 *  delay line drained). Used by Chain to keep silence flags honest. */
	bool lastOutputSilent () const noexcept { return lastOutputSilent_; }

private:
	float targetWetGain () const noexcept;
	void releaseBuffers ();

	std::unique_ptr<PluginInstance> instance_;
	SlotIdentity identity_;
	std::string statusDetail_;
	std::atomic<registry::Status> status_{registry::Status::Unknown};
	std::atomic<bool> bypass_{false};
	std::atomic<float> mix_{1.0f};

	ProcessSetup setup_;
	int32_t containerChannels_ = 2;
	int32_t pluginInChannels_ = 0;
	int32_t pluginOutChannels_ = 0;
	int32_t latency_ = 0;
	int32_t tail_ = 0;
	int32_t rampSamples_ = 1;
	float wetGain_ = 1.0f;
	bool prepared_ = false;

	// Audio-thread state for silence accounting.
	bool drySilent_ = true;
	int32_t silentRun_ = 0;
	bool lastOutputSilent_ = true;

	// Buffers, all sized in prepare().
	std::vector<float> pluginInStorage_;
	std::vector<float> pluginOutStorage_;
	std::vector<float> wetStorage_;
	std::vector<float> dryStorage_;
	std::vector<float> gainRamp_;
	std::vector<float*> pluginInPtrs_;
	std::vector<float*> pluginOutPtrs_;
	std::vector<float*> wetPtrs_;
	std::vector<float*> dryPtrs_;
	AudioBlock pluginIn_;
	AudioBlock pluginOut_;
	AudioBlock wet_;
	AudioBlock dry_;
	DelayLine dryDelay_;
};

} // namespace crate::host
