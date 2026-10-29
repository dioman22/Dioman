// The filesystem index: Crate's own view of the library, built by reading the
// plugin folders instead of a host's private cache.
//
// This is what makes Crate work in a host whose registry we have not reverse
// engineered — including hosts nobody has written a reader for yet. A host cache is
// an *optional overlay* on top of this ("what that host will actually load"), never a
// prerequisite for showing a library at all.
//
// It reads files only: bundle names, moduleinfo.json and the header of a binary. It
// never loads or executes a plugin, so it is safe on the message thread — and it is
// why one process covers every architecture.
#pragma once

#include "reader.h"

namespace crate::registry {

class ScannerReader final : public RegistryReader
{
public:
	const char* id () const override { return "vst3-fs@1"; }
	const char* hostKey () const override { return "fs"; }
	RegistrySnapshot read (const ReaderContext& context) const override;
};

/** Exposed for fixture tests: index folder trees without a host around them. */
std::vector<Entry> scanVST3Folders (const std::vector<std::string>& folders, int maxDepth);

} // namespace crate::registry
