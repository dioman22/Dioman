// Reader interface: one per host registry format, versioned, fixture-tested.
//
// A reader is only allowed to read the registry the host wrote and, for resolution,
// walk the plugin folders it was given to a bounded depth. It never executes plugin
// code and never writes to anything the host owns.
#pragma once

#include "entry.h"

#include "../util/fs.h"

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

/** How deep a plugin folder is walked when looking for a bundle. VST3 installs one
 *  or two levels down (Common Files\VST3\Vendor\Thing.vst3), so three levels cover
 *  every real layout while keeping one walk in the hundreds of milliseconds. */
constexpr int kPluginScanDepth = 3;

/** Resolve a display name to one bundle path. */
struct BundleResolution
{
	std::string path;
	Resolution resolution = Resolution::Unresolved;
	std::vector<std::string> candidates; ///< for "ambiguous" explanations
};

/** One walk of the plugin folders, shared by every lookup of a read pass.
 *
 *  Calling resolveBundleBy*() once per registry entry would re-walk the folders
 *  each time — a cache with a thousand rows would stat the whole library a
 *  thousand times. A host reader builds one index and resolves every entry
 *  against it instead. */
struct BundleIndex
{
	static BundleIndex build (const std::vector<std::string>& folders);

	BundleResolution resolveByName (const std::string& displayName) const;
	BundleResolution resolveByFileName (const std::string& fileName) const;

	size_t size () const { return items_.size (); }

private:
	std::vector<fs::DirEntry> items_;
};

BundleResolution resolveBundleByName (const std::string& displayName,
                                      const std::vector<std::string>& folders);
BundleResolution resolveBundleByFileName (const std::string& fileName,
                                          const std::vector<std::string>& folders);

/** Normalised comparison key: lower case, no spaces/underscores/dashes, no extension,
 *  no trailing "vst3"/"vst" token, no parenthesised suffix, no architecture token. */
std::string bundleMatchKey (const std::string& text);

} // namespace crate::registry
