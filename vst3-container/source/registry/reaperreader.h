#pragma once

#include "reader.h"

namespace crate::registry {

/** REAPER's vstcache INI. One line per scanned plugin, keyed by file name. */
class ReaperReader final : public RegistryReader
{
public:
	const char* id () const override { return "reaper-ini@1"; }
	const char* hostKey () const override { return "reaper"; }
	RegistrySnapshot read (const ReaderContext& context) const override;
};

/** Exposed for fixture tests: parse only the cache text. */
std::vector<Entry> parseReaperCache (const std::string& text);

/** Parse a single display string such as "Pro-Q 4 (FabFilter) (16 out)!!!VSTi". */
void parseReaperDisplayName (const std::string& raw, std::string& name, std::string& vendor,
                             bool& isInstrument);

} // namespace crate::registry
