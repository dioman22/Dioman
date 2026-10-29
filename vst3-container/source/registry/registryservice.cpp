#include "registryservice.h"

#include "../app/cratepaths.h"
#include "../json/json.h"
#include "../util/fs.h"
#include "../util/stringutil.h"

#include <algorithm>
#include <map>
#include <set>

namespace crate::registry {

namespace {

std::string bundleKeyOf (const Entry& entry)
{
	return toLower (fs::normalized (entry.bundle));
}

/** Identity of one card: the bundle plus the class inside it. A bundle can expose
 *  several plugins, and they must stay separate cards. When the UID is still
 *  unknown — a host cache that did not record it — fall back to the name, so a row
 *  and an index entry describing the same plugin still collapse into one. */
std::string identityOf (const Entry& entry)
{
	auto key = bundleKeyOf (entry);
	if (key.empty ())
		return std::string ();
	if (!entry.uid.empty ())
		return key + "|" + toLower (entry.uid);
	return key + "|" + bundleMatchKey (entry.name);
}

/** A host's own name for a plugin wins; everything the host could not tell us is
 *  filled in from what the bundle itself declares. */
void applyIndexMetadata (Entry& entry, const Entry& fromIndex)
{
	if (entry.uid.empty ())
		entry.uid = fromIndex.uid;
	if (entry.vendor.empty ())
		entry.vendor = fromIndex.vendor;
	if (entry.version.empty ())
		entry.version = fromIndex.version;
	if (entry.sdkVersion.empty ())
		entry.sdkVersion = fromIndex.sdkVersion;
	if (entry.subCategories.empty ())
		entry.subCategories = fromIndex.subCategories;
	if (entry.category.empty ())
		entry.category = fromIndex.category;
	if (!entry.isInstrument)
		entry.isInstrument = fromIndex.isInstrument;
	if (entry.registryName.empty ())
		entry.registryName = fromIndex.registryName;
	if (entry.status == Status::Unknown && fromIndex.status == Status::Ready)
	{
		entry.status = Status::Ready;
		entry.statusDetail = fromIndex.statusDetail;
	}
}

/** Unset means "include them": a stock install must work with no configuration at
 *  all. The override exists for support and for the test suite, whose assertions
 *  must not depend on whatever plugins happen to be installed on the machine. */
bool includeStandardFolders ()
{
	auto value = fs::environmentVariable ("CRATE_STANDARD_FOLDERS");
	if (value.empty ())
		return true;
	return !(iequals (value, "0") || iequals (value, "off") || iequals (value, "false") ||
	         iequals (value, "no") || iequals (value, "none"));
}

} // namespace

RegistryService::RegistryService (std::string hostName, std::string hostVersion,
                                  std::string dataDir, std::vector<std::string> extraFolders)
: hostName_ (std::move (hostName)), hostVersion_ (std::move (hostVersion)),
  dataDir_ (std::move (dataDir))
{
	// A support-friendly override: it also lets the test suite pretend to be REAPER
	// without a DAW installed.
	auto envHost = fs::environmentVariable ("CRATE_HOST_NAME");
	if (!envHost.empty ())
		hostName_ = envHost;
	hostKind_ = hostKindFromName (hostName_);

	std::set<std::string> folders;
	if (includeStandardFolders ())
	{
		for (const auto& folder : fs::standardVST3Folders ())
			folders.insert (fs::normalized (folder));
	}
	// The folders the user added themselves. This is the only way VST2 can be listed
	// at all: that format has no standard location to fall back on.
	for (const auto& folder : app::userPluginFolders (dataDir_))
		folders.insert (fs::normalized (folder));
	for (const auto& folder : extraFolders)
	{
		if (!folder.empty ())
			folders.insert (fs::normalized (folder));
	}
	auto envExtra = fs::environmentVariable ("CRATE_VST3_EXTRA");
	if (!envExtra.empty ())
	{
#if defined(_WIN32)
		for (const auto& part : split (envExtra, ';'))
#else
		for (const auto& part : split (envExtra, ':'))
#endif
		{
			auto trimmed = std::string (trimView (part));
			if (!trimmed.empty ())
				folders.insert (fs::normalized (trimmed));
		}
	}
	pluginFolders_.assign (folders.begin (), folders.end ());
}

std::string RegistryService::snapshotCacheDir () const
{
	if (dataDir_.empty ())
		return app::snapshotCacheDirectory ();
	return fs::join (dataDir_, "snapshots");
}

const BundleIndex& RegistryService::pluginIndex ()
{
	if (!bundleIndexBuilt_)
	{
		bundleIndex_ = BundleIndex::build (pluginFolders_);
		bundleIndexBuilt_ = true;
	}
	return bundleIndex_;
}

RegistrySnapshot RegistryService::load (bool useFallbackCache)
{
	ReaderContext context;
	context.hostName = hostName_;
	context.hostVersion = hostVersion_;
	context.dataDir = dataDir_;
	context.pluginFolders = pluginFolders_;
	auto overridePath = fs::environmentVariable ("CRATE_REGISTRY");
	if (!overridePath.empty ())
		context.registryOverride = overridePath;

	// The index comes first: it is the only source that exists in a host nobody has
	// written a reader for, and it is what makes the container usable in Cubase,
	// Bitwig or Studio One instead of showing an empty grid.
	ScannerReader scanner;
	RegistrySnapshot snapshot = scanner.read (context);
	snapshot.host = hostName_;
	snapshot.hostVersion = hostVersion_;

	if (hostKind_ != HostKind::Unknown)
	{
		if (auto* reader = createReader (hostKind_))
		{
			auto host = reader->read (context);
			delete reader;
			if (host.state == ReaderState::Ok && !host.entries.empty ())
				snapshot = mergeHostOverIndex (std::move (host), snapshot);
			else if (!host.stateDetail.empty ())
				snapshot.stateDetail = host.stateDetail + " " + snapshot.stateDetail;
		}
	}

	if (snapshot.entries.empty () && useFallbackCache)
	{
		auto reason = snapshot.stateDetail;
		loadFallbackCache (snapshot, reason);
	}
	return snapshot;
}

RegistrySnapshot RegistryService::mergeHostOverIndex (RegistrySnapshot host,
                                                      const RegistrySnapshot& index) const
{
	std::map<std::string, const Entry*> byIdentity;
	std::map<std::string, const Entry*> byBundle;
	for (const auto& entry : index.entries)
	{
		if (entry.bundle.empty ())
			continue;
		byIdentity.emplace (identityOf (entry), &entry);
		byBundle.emplace (bundleKeyOf (entry), &entry);
	}

	std::set<std::string> claimed;
	for (auto& entry : host.entries)
	{
		if (entry.bundle.empty ())
			continue;
		const Entry* match = nullptr;
		auto identity = identityOf (entry);
		if (!identity.empty ())
		{
			auto it = byIdentity.find (identity);
			if (it != byIdentity.end ())
				match = it->second;
		}
		if (!match)
		{
			auto it = byBundle.find (bundleKeyOf (entry));
			if (it != byBundle.end ())
				match = it->second;
		}
		if (!match)
			continue;
		applyIndexMetadata (entry, *match);
		claimed.insert (identityOf (*match));
	}

	const size_t fromCache = host.entries.size ();
	size_t fromDisk = 0;
	for (const auto& entry : index.entries)
	{
		if (entry.bundle.empty ())
			continue;
		auto identity = identityOf (entry);
		if (identity.empty () || !claimed.insert (identity).second)
			continue;
		auto copy = entry;
		// Never silently pretend the host will load it: it is installed, and the
		// host has not catalogued it yet.
		copy.statusDetail = "Found on disk but not in this host's plugin cache yet. "
		                    "Rescan plugins in the DAW to make the host aware of it, or "
		                    "load it anyway.";
		host.entries.push_back (std::move (copy));
		++fromDisk;
	}

	host.reader = host.reader + " + " + ScannerReader ().id ();
	host.stateDetail = std::to_string (fromCache) + " from this host's own cache, " +
	                   std::to_string (fromDisk) + " found on disk that the host has not "
	                   "scanned yet.";
	return host;
}

const ModuleInfo* RegistryService::moduleInfo (const std::string& bundlePath, std::string& error)
{
	error.clear ();
	if (bundlePath.empty ())
	{
		error = "no bundle path";
		return nullptr;
	}
	auto infoPath = moduleInfoPath (bundlePath);
	auto mtime = infoPath.empty () ? 0 : fs::modifiedTime (infoPath);
	auto it = infoCache_.find (bundlePath);
	if (it != infoCache_.end () && it->second.mtime == mtime)
	{
		error = it->second.error;
		return it->second.ok ? &it->second.info : nullptr;
	}
	CachedInfo cached;
	cached.mtime = mtime;
	cached.ok = readModuleInfo (bundlePath, cached.info, cached.error);
	if (!cached.ok)
	{
		cached.error = infoPath.empty () ? "This bundle has no moduleinfo.json."
		                                 : "moduleinfo.json could not be read: " + cached.error;
	}
	auto& stored = infoCache_[bundlePath] = std::move (cached);
	error = stored.error;
	return stored.ok ? &stored.info : nullptr;
}


void RegistryService::enrich (Entry& entry)
{
	if (entry.bundle.empty () ||
	    (!fs::fileExists (entry.bundle) && !fs::directoryExists (entry.bundle)))
	{
		auto resolved = pluginIndex ().resolveByName (entry.name);
		entry.resolution = resolved.resolution;
		entry.bundle = resolved.path;
		if (resolved.resolution == Resolution::Ambiguous)
			entry.statusDetail = "More than one bundle matches this name: " +
			                     join (resolved.candidates, ", ");
	}
	else if (entry.resolution == Resolution::Unresolved)
		entry.resolution = Resolution::Exact;

	if (entry.bundle.empty ())
	{
		if (entry.statusDetail.empty ())
			entry.statusDetail = "No matching .vst3 bundle was found for this registry entry.";
		return;
	}

	auto archIt = archCache_.find (entry.bundle);
	if (archIt == archCache_.end ())
		archIt = archCache_.emplace (entry.bundle, detectArchitectures (entry.bundle)).first;
	entry.arch = archIt->second;

	std::string error;
	auto* info = moduleInfo (entry.bundle, error);
	if (!info)
	{
		if (entry.status != Status::Disabled)
			entry.status = Status::Unknown;
		if (entry.statusDetail.empty ())
			entry.statusDetail = error;
		return;
	}
	auto* cls = info->audioModuleClass ();
	if (!cls)
	{
		entry.status = Status::Unreadable;
		entry.statusDetail = "moduleinfo.json lists no \"Audio Module Class\"; this bundle may "
		                     "only contain a controller or an unsupported class.";
		return;
	}

	if (entry.uid.empty ())
		entry.uid = cls->cid;
	if (entry.vendor.empty ())
		entry.vendor = !cls->vendor.empty () ? cls->vendor : info->factoryVendor;
	if (entry.version.empty ())
		entry.version = !cls->version.empty () ? cls->version : info->version;
	if (entry.sdkVersion.empty ())
		entry.sdkVersion = cls->sdkVersion;
	if (entry.name.empty ())
		entry.name = cls->name;
	if (entry.subCategories.empty ())
		entry.subCategories = cls->subCategories;
	entry.isInstrument = cls->isInstrument () || entry.isInstrument;
	if (entry.category.empty ())
		entry.category = canonicalCategory (entry.subCategories, entry.isInstrument);
	cacheSnapshot (entry, *info, *cls);

	if (isIncompatibleArchitecture (entry.arch))
	{
		entry.status = Status::Incompatible;
		entry.statusDetail = "This bundle contains " + join (entry.arch, ", ") +
		                     " but the host is running " + hostArchitecture () + ".";
	}
	else if (entry.status == Status::Unknown)
	{
		entry.status = Status::Ready;
		entry.statusDetail = "Ready to load. Metadata read from this bundle's moduleinfo.json.";
	}
}


void RegistryService::cacheSnapshot (Entry& entry, const ModuleInfo& info,
                                     const ModuleClassInfo& cls)
{
	(void) info;
	if (cls.snapshots.empty () || entry.uid.empty ())
		return;
	const Snapshot* chosen = nullptr;
	for (const auto& snap : cls.snapshots)
	{
		if (snap.scaleFactor <= 2.0 + 1e-9 && (!chosen || snap.scaleFactor > chosen->scaleFactor))
			chosen = &snap;
	}
	if (!chosen)
		chosen = &cls.snapshots.front ();

	std::vector<uint8_t> bytes;
	std::string extension;
	if (!chosen->base64.empty ())
	{
		base64Decode (chosen->base64, bytes);
		extension = imageExtensionFromBytes (bytes);
	}
	else
	{
		auto source = snapshotPath (entry.bundle, *chosen);
		if (!source.empty ())
		{
			fs::readFileBytes (source, bytes);
			extension = imageExtensionFromBytes (bytes);
			if (extension.empty ())
				extension = toLower (fs::extension (source));
			if (!extension.empty () && extension[0] == '.')
				extension.erase (0, 1);
		}
	}
	if (bytes.empty ())
		return;
	if (extension.empty ())
		extension = "png";

	auto cacheDir = snapshotCacheDir ();
	fs::createDirectories (cacheDir);
	auto scale = std::to_string (static_cast<int> (chosen->scaleFactor * 10));
	auto destination = fs::join (cacheDir, entry.uid + "@" + scale + "." + extension);
	std::vector<uint8_t> existing;
	if (!fs::readFileBytes (destination, existing) || existing.size () != bytes.size ())
		fs::writeFile (destination, bytes.data (), bytes.size ());
	entry.image = destination;
	entry.imageSource = "moduleinfo";
}

bool RegistryService::loadFallbackCache (RegistrySnapshot& snapshot, const std::string& reason)
{
	auto path = fs::join (dataDir_.empty () ? app::dataDirectory () : dataDir_,
	                      "fallback-index.json");
	std::string text;
	if (!fs::readFile (path, text))
		return false;
	std::string error;
	auto root = json::parse (text, error);
	if (!error.empty () || !root.isObject ())
		return false;
	auto entries = root.getArray ("entries");
	if (!entries)
		return false;

	std::vector<Entry> loaded;
	for (const auto& item : entries->array ())
	{
		if (!item.isObject ())
			continue;
		Entry entry;
		entry.source = "scanner-fallback";
		entry.format = "VST3";
		entry.name = item.getString ("name");
		entry.vendor = item.getString ("vendor");
		entry.category = item.getString ("category");
		entry.uid = item.getString ("uid");
		entry.bundle = item.getString ("bundle");
		entry.isInstrument = item.getBool ("is_instrument");
		entry.resolution = (!entry.bundle.empty () && fs::directoryExists (entry.bundle))
		                       ? Resolution::Exact
		                       : Resolution::Unresolved;
		if (auto arch = item.getArray ("arch"))
		{
			for (const auto& value : arch->array ())
			{
				if (value.isString ())
					entry.arch.push_back (value.string ());
			}
		}
		if (entry.name.empty ())
			entry.name = fs::stem (entry.bundle);
		if (entry.name.empty ())
			continue;
		if (isIncompatibleArchitecture (entry.arch))
		{
			entry.status = Status::Incompatible;
			entry.statusDetail = "Fallback scan found this bundle but it has no binary for " +
			                     hostArchitecture () + ".";
		}
		loaded.push_back (std::move (entry));
	}
	if (loaded.empty ())
		return false;

	snapshot.entries = std::move (loaded);
	snapshot.reader = "scanner-fallback@1";
	snapshot.source = path;
	snapshot.writtenAt = fs::modifiedTime (path);
	snapshot.stateDetail = reason +
	                       " Showing the opt-in fallback index instead: these entries come "
	                       "from a file list, not from a scan of the plugin folders.";
	return true;
}

void RegistryService::applyBlacklist (RegistrySnapshot& snapshot,
                                      const std::vector<std::string>& blacklistedUids,
                                      const std::vector<std::string>& crashedUids) const
{
	auto matches = [] (const std::vector<std::string>& list, const std::string& uid) {
		for (const auto& candidate : list)
		{
			if (iequals (candidate, uid))
				return true;
		}
		return false;
	};
	for (auto& entry : snapshot.entries)
	{
		if (entry.uid.empty ())
			continue;
		if (matches (crashedUids, entry.uid))
		{
			entry.status = Status::Crashed;
			entry.statusDetail = defaultStatusDetail (Status::Crashed);
		}
		else if (matches (blacklistedUids, entry.uid))
		{
			entry.status = Status::Blacklisted;
			entry.statusDetail = defaultStatusDetail (Status::Blacklisted);
		}
	}
}

} // namespace crate::registry
