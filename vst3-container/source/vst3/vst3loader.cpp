#include "vst3loader.h"

#include "../util/fs.h"
#include "vst3instance.h"

namespace crate::vst3 {

host::LoadResult Loader::load (const host::LoadRequest& request)
{
	host::LoadResult result;

	if (request.bundlePath.empty ())
	{
		result.error = "the slot has no bundle path";
		result.status = registry::Status::Missing;
		return result;
	}
	if (!fs::directoryExists (request.bundlePath))
	{
		result.error = "the plugin is no longer at " + request.bundlePath;
		result.status = registry::Status::Missing;
		return result;
	}

	auto instance = std::unique_ptr<Instance> (new Instance ());
	std::string error;
	if (!instance->load (request.bundlePath, request.uid, request.name, hostContext_, handler_,
	                     error))
	{
		result.error = error.empty () ? "the plugin could not be loaded" : error;
		// A missing class or a moved bundle is "missing"; anything else is a plugin we
		// cannot read, which is a different sentence on the card.
		result.status = error.find ("no longer contains") != std::string::npos
		                    ? registry::Status::Missing
		                    : registry::Status::Unreadable;
		return result;
	}

	result.instance = std::move (instance);
	result.status = registry::Status::Ok;
	return result;
}

} // namespace crate::vst3
