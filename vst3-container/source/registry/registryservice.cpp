#include "registryservice.h"

#include "../app/cratepaths.h"
#include "../json/json.h"
#include "../util/fs.h"
#include "../util/stringutil.h"

#include <algorithm>
#include <set>

namespace crate::registry {

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
	for (const auto& folder : fs::standardVST3Folders ())
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

RegistrySnapshot RegistryService::load (bool useFallbackCache)
{
	RegistrySnapshot snapshot;
	snapshot.host = hostName_;
	snapshot.hostVersion = hostVersion_;
	snapshot.state = ReaderState::NoHost;

	if (hostKind_ == HostKind::Unknown)
	{
		snapshot.stateDetail =
		    "\"" + hostName_ + "\" is not a host whose plugin cache Crate has verified. "
		    "Crate does not scan the filesystem to guess: use the explicit fallback scan "
		    "if you want a file list that may include plugins the host rejects.";
		return snapshot;
	}

	auto* reader = createReader (hostKind_);
	if (!reader)
	{
		snapshot.stateDetail = std::string ("Crate has no verified reader for ") +
		                       hostKindDisplayName (hostKind_) +
		                       " yet. Adding one requires a sanitised fixture of its "
		                       "registry from a real install; guessing is not allowed.";
		return snapshot;
	}

	ReaderContext context;
	context.hostName = hostName_;
	context.hostVersion = hostVersion_;
	context.dataDir = dataDir_;
	context.pluginFolders = pluginFolders_;
	auto overridePath = fs::environmentVariable ("CRATE_REGISTRY");
	if (!overridePath.empty ())
		context.registryOverride = overridePath;

	snapshot = reader->read (context);
	snapshot.host = hostName_.empty () ? hostKindDisplayName (hostKind_) : hostName_;
	delete reader;

	if (snapshot.state != ReaderState::Ok && useFallbackCache)
	{
		auto reason = snapshot.stateDetail;
		loadFallbackCache (snapshot, reason);
	}
	return snapshot;
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
		auto resolved = resolveBundleByName (entry.name, pluginFolders_);
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
	                       " Showing the opt-in fallback scan instead: these entries are files "
	                       "Crate found, not necessarily what the host will load.";
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
