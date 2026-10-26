#pragma once

#include "reader.h"

namespace crate::registry {

/** FL Studio's categorised Plugin database folder: subfolders are categories, each
 *  plugin is an .fst file. The filename is the display name; we never parse .fst. */
class FLReader final : public RegistryReader
{
public:
	const char* id () const override { return "fl-folder@1"; }
	const char* hostKey () const override { return "fl"; }
	RegistrySnapshot read (const ReaderContext& context) const override;
};

/** Exposed for tests: list a Plugin database folder. */
std::vector<Entry> scanFLPluginDatabase (const std::string& folder);

} // namespace crate::registry
