// Reads the one bundle we were asked about (and only that bundle) for UID, vendor,
// category and a vendor snapshot. No directory walks happen here.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace crate::registry {

struct Snapshot
{
	double scaleFactor = 1.0;
	std::string path;   ///< relative to Contents/Resources, as written by moduleinfotool
	std::string base64; ///< tolerated extension; not emitted by the SDK tool
};

struct ModuleClassInfo
{
	std::string cid;
	std::string category;
	std::string name;
	std::string vendor;
	std::string version;
	std::string sdkVersion;
	std::vector<std::string> subCategories;
	std::vector<Snapshot> snapshots;
	int64_t cardinality = 0x7FFFFFFF;
	uint32_t flags = 0;

	bool isInstrument () const;
	bool isAudioModuleClass () const { return category == "Audio Module Class"; }
};

struct Compatibility
{
	std::string newCid;
	std::vector<std::string> oldCids;
};

struct ModuleInfo
{
	std::string name;
	std::string version;
	std::string factoryVendor;
	std::vector<ModuleClassInfo> classes;
	std::vector<Compatibility> compatibility;

	const ModuleClassInfo* audioModuleClass () const;
	const ModuleClassInfo* classById (const std::string& cid) const;
	/** Returns the new CID when `oldCid` is listed in Compatibility, else "". */
	std::string remapCid (const std::string& oldCid) const;
};

/** Path of moduleinfo.json for a .vst3 bundle, or "" if the bundle has none. */
std::string moduleInfoPath (const std::string& bundlePath);

/** Read + parse. Never throws; on error `error` explains the first failure. */
bool readModuleInfo (const std::string& bundlePath, ModuleInfo& info, std::string& error);
bool parseModuleInfo (const std::string& text, ModuleInfo& info, std::string& error);

/** Absolute path of a snapshot referenced by a class, "" when absent. */
std::string snapshotPath (const std::string& bundlePath, const Snapshot& snapshot);

/** "x86_64-linux", "aarch64-linux", "x86_64-win", "arm64-win", "x86_64-mac", "arm64-mac";
 *  empty when the binary format is not recognised. Reads headers only. */
std::vector<std::string> detectArchitectures (const std::string& bundlePath);

/** Architecture tag of the running process, e.g. "x86_64-linux". */
std::string hostArchitecture ();

/** True when `architectures` is non-empty and contains none compatible with this host. */
bool isIncompatibleArchitecture (const std::vector<std::string>& architectures);

} // namespace crate::registry
