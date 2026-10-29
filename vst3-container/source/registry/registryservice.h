// Chooses the reader for the host we are running inside, reads its registry and
// enriches only the cards that are about to be shown. This is the only place that
// knows about per-host formats; the plugin and the UI see RegistrySnapshot/Entry.
//
// The filesystem index (ScannerReader) is the *base* for every host, because it is
// the only source that exists in a DAW whose cache we have not deciphered. A host
// registry, when we have a verified reader for it, is an overlay on top: it answers
// "what will this host actually load", and it never gates whether a library is shown.
#pragma once

#include "entry.h"
#include "moduleinfo.h"
#include "reader.h"
#include "scannerreader.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace crate::registry {

class RegistryService
{
public:
	RegistryService (std::string hostName, std::string hostVersion, std::string dataDir,
	                 std::vector<std::string> extraFolders = {});

	RegistrySnapshot load (bool useFallbackCache = false);

	/** Resolve path, read that one bundle's moduleinfo.json, detect arch and cache a
	 *  snapshot. Message thread only (it touches the filesystem). */
	void enrich (Entry& entry);

	const ModuleInfo* moduleInfo (const std::string& bundlePath, std::string& error);

	HostKind hostKind () const { return hostKind_; }
	const std::string& hostName () const { return hostName_; }
	const std::vector<std::string>& pluginFolders () const { return pluginFolders_; }
	const std::string& dataDir () const { return dataDir_; }
	std::string snapshotCacheDir () const;

	/** Mark entries that are on the user's blacklist (never hides them). */
	void applyBlacklist (RegistrySnapshot& snapshot,
	                     const std::vector<std::string>& blacklistedUids,
	                     const std::vector<std::string>& crashedUids) const;

private:
	struct CachedInfo
	{
		int64_t mtime = 0;
		std::string error;
		ModuleInfo info;
		bool ok = false;
	};

	void cacheSnapshot (Entry& entry, const ModuleInfo& info, const ModuleClassInfo& cls);
	bool loadFallbackCache (RegistrySnapshot& snapshot, const std::string& reason);

	/** One walk of the plugin folders, built on first use and reused. */
	const BundleIndex& pluginIndex ();

	/** Host rows first, then everything the index found that the host has not
	 *  catalogued yet, so nothing installed is ever invisible. */
	RegistrySnapshot mergeHostOverIndex (RegistrySnapshot host,
	                                     const RegistrySnapshot& index) const;

	std::string hostName_;
	std::string hostVersion_;
	std::string dataDir_;
	HostKind hostKind_ = HostKind::Unknown;
	std::vector<std::string> pluginFolders_;
	std::unordered_map<std::string, CachedInfo> infoCache_;
	std::unordered_map<std::string, std::vector<std::string>> archCache_;
	BundleIndex bundleIndex_;
	bool bundleIndexBuilt_ = false;
};

} // namespace crate::registry
