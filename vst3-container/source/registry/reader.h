// Reader interface: one per host registry format, versioned, fixture-tested.
//
// A reader is only allowed to read the registry the host wrote and, for resolution,
// list the *immediate children* of the plugin folders it was given. It never
// executes plugin code and never writes to anything the host owns.
#pragma once

#include "entry.h"

#include <string>
#include <vector>

namespace crate::registry {

struct ReaderContext
{
	std::string hostName;    ///< raw IHostApplication::getName()
	std::string hostVersion; ///< best effort
	std::string dataDir;     ///< Crate's own per-user directory (cache, journal)
	std::vector<std::string> pluginFolders; ///< configured + standard VST3 folders
	std::vector<std::string> extraSearchPaths; ///< e.g. REAPER's vstpath entries
	std::string registryOverride; ///< absolute registry path, for tests and support
	int64_t hostStartTime = 0;    ///< for staleness reporting, 0 if unknown
};

class RegistryReader
{
public:
	virtual ~RegistryReader () = default;
	virtual const char* id () const = 0;
	virtual const char* hostKey () const = 0;
	virtual RegistrySnapshot read (const ReaderContext& context) const = 0;
};

enum class HostKind
{
	Reaper,
	Live,
	FLStudio,
	Bitwig,
	Cubase,
	StudioOne,
	Unknown
};

HostKind hostKindFromName (const std::string& hostName);
const char* hostKindKey (HostKind kind);
const char* hostKindDisplayName (HostKind kind);

/** Returns nullptr for hosts whose registry we have not verified (tier C). */
RegistryReader* createReader (HostKind kind);

/** Resolve a display name to one bundle path by looking at one level of `folders`. */
struct BundleResolution
{
	std::string path;
	Resolution resolution = Resolution::Unresolved;
	std::vector<std::string> candidates; ///< for "ambiguous" explanations
};

BundleResolution resolveBundleByName (const std::string& displayName,
                                      const std::vector<std::string>& folders);
BundleResolution resolveBundleByFileName (const std::string& fileName,
                                          const std::vector<std::string>& folders);

/** Normalised comparison key: lower case, no spaces/underscores/dashes, no extension,
 *  no trailing "vst3"/"vst" token, no parenthesised suffix. */
std::string bundleMatchKey (const std::string& text);

} // namespace crate::registry
