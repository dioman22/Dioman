// What the engine needs from one loaded plugin.
//
// This is the seam between "code we can unit-test" and "foreign code that can crash
// us". The VST3 implementation wraps Module + IComponent + IAudioProcessor +
// IEditController behind this interface; tests use fakes.
//
// Threading contract, mirroring VST3:
//  * every method except process() is message-thread only,
//  * process() is audio-thread only, must not allocate, block or throw,
//  * startProcessing() implies setupProcessing + setActive(true) + setProcessing(true),
//    stopProcessing() implies setActive(false) and must be safe to call twice.
#pragma once

#include "../registry/entry.h"
#include "audiotypes.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace crate::host {

class PluginInstance
{
public:
	virtual ~PluginInstance () = default;

	virtual const char* displayName () const = 0;

	/** Channel counts of the main bus pair, after bus negotiation. 0 in is valid for
	 *  instruments. */
	virtual int32_t numInputChannels () const = 0;
	virtual int32_t numOutputChannels () const = 0;

	/** Values the plugin reports right now; latency may change after startProcessing()
	 *  and is re-read by the engine at that point. */
	virtual int32_t latencySamples () const = 0;
	virtual int32_t tailSamples () const = 0;

	/** Message thread. Returns false and explains itself when the plugin refuses to run
	 *  (activation failure, unsupported layout, licence). */
	virtual bool startProcessing (const ProcessSetup& setup, std::string& error) = 0;

	/** Message thread. Idempotent. */
	virtual void stopProcessing () = 0;

	/** Audio thread. `in` may have numChannels == 0 for instruments. Both blocks are
	 *  pre-sized to the channel counts reported above. Implementation must honour the
	 *  silence flags it is given and set them honestly on `out`. */
	virtual void process (const AudioBlock& in, AudioBlock& out) noexcept = 0;

	/** Message thread. Persisted in the container state next to the class UID. */
	virtual bool getComponentState (std::vector<uint8_t>& out) = 0;
	virtual bool setComponentState (const std::vector<uint8_t>& in) = 0;
	virtual bool getControllerState (std::vector<uint8_t>& out) = 0;
	virtual bool setControllerState (const std::vector<uint8_t>& in) = 0;
};

struct LoadRequest
{
	std::string bundlePath; ///< absolute path of the .vst3 bundle
	std::string uid;        ///< 32 hex chars, may be empty when the registry had none
	std::string name;       ///< registry display name, for error messages
};

struct LoadResult
{
	std::unique_ptr<PluginInstance> instance;
	std::string error;
	registry::Status status = registry::Status::Unknown;
};

/** Implemented by the VST3 layer. Only ever called on the message thread. */
class PluginLoader
{
public:
	virtual ~PluginLoader () = default;
	virtual LoadResult load (const LoadRequest& request) = 0;
};

} // namespace crate::host
