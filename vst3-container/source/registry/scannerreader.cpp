#include "scannerreader.h"

#include "moduleinfo.h"

#include "../util/fs.h"
#include "../util/stringutil.h"

#include <algorithm>
#include <set>

namespace crate::registry {

namespace {

/** Upper bound on how many cards one scan may produce. A damaged or absurd folder
 *  layout must not turn into an unbounded allocation on the message thread. */
constexpr size_t kMaxScannedClasses = 20000;

bool isVST3Bundle (const fs::DirEntry& item)
{
	return item.isDirectory && endsWith (toLower (item.name), ".vst3");
}

Entry baseEntry (const std::string& bundle)
{
	Entry entry;
	entry.source = "vst3-fs";
	entry.format = "VST3";
	entry.bundle = bundle;
	entry.resolution = Resolution::Exact;
	entry.registryName = fs::fileName (bundle);
	return entry;
}

/** One card per audio module class: a single bundle may expose several plugins, and
 *  each of them is separately loadable. */
Entry entryFromClass (const std::string& bundle, const ModuleInfo& info,
                      const ModuleClassInfo& cls)
{
	auto entry = baseEntry (bundle);
	entry.uid = cls.cid;
	entry.name = !cls.name.empty () ? cls.name : info.name;
	entry.vendor = !cls.vendor.empty () ? cls.vendor : info.factoryVendor;
	entry.version = !cls.version.empty () ? cls.version : info.version;
	entry.sdkVersion = cls.sdkVersion;
	entry.subCategories = cls.subCategories;
	entry.isInstrument = cls.isInstrument ();
	entry.category = canonicalCategory (entry.subCategories, entry.isInstrument);
	entry.status = Status::Ready;
	entry.statusDetail = "Read from this bundle's moduleinfo.json.";
	return entry;
}

/** A bundle we can see but cannot describe without loading it — and loading it is
 *  exactly what this layer refuses to do. The card is still shown, with the real
 *  reason, rather than being hidden or given an invented vendor. Architecture comes
 *  from the binary header, which is a few bytes at the start of the file. */
Entry entryWithoutMetadata (const std::string& bundle, const std::string& reason)
{
	auto entry = baseEntry (bundle);
	entry.name = fs::stem (bundle);
	entry.arch = detectArchitectures (bundle);
	if (isIncompatibleArchitecture (entry.arch))
	{
		entry.status = Status::Incompatible;
		entry.statusDetail = "This bundle contains " + join (entry.arch, ", ") +
		                     " but this host process is " + hostArchitecture () + ".";
	}
	else
	{
		entry.status = Status::Unknown;
		entry.statusDetail = reason;
	}
	return entry;
}

} // namespace

std::vector<Entry> scanVST3Folders (const std::vector<std::string>& folders, int maxDepth)
{
	std::vector<Entry> entries;
	std::set<std::string> seen;

	for (const auto& folder : folders)
	{
		if (entries.size () >= kMaxScannedClasses)
			break;
		for (const auto& item : fs::listDirectoryRecursive (folder, maxDepth))
		{
			if (entries.size () >= kMaxScannedClasses)
				break;
			if (!isVST3Bundle (item))
				continue;
			// Two configured folders can point at the same plugin (a standard folder
			// plus a user folder that contains it). The card must appear once.
			if (!seen.insert (toLower (fs::normalized (item.path))).second)
				continue;

			ModuleInfo info;
			std::string error;
			if (!readModuleInfo (item.path, info, error))
			{
				entries.push_back (entryWithoutMetadata (
				    item.path,
				    "This bundle has no readable moduleinfo.json, so its name comes from the "
				    "file name and its vendor and category stay unknown until it is loaded."));
				continue;
			}

			bool foundAudioModule = false;
			for (const auto& cls : info.classes)
			{
				// A bundle also carries controller and service classes; only an audio
				// module class is something a host can put on a track.
				if (!cls.isAudioModuleClass ())
					continue;
				entries.push_back (entryFromClass (item.path, info, cls));
				foundAudioModule = true;
			}
			if (!foundAudioModule)
				entries.push_back (entryWithoutMetadata (
				    item.path,
				    "This bundle's moduleinfo.json lists no \"Audio Module Class\", so it may "
				    "contain only a controller or an unsupported class."));
		}
	}

	std::sort (entries.begin (), entries.end (), [] (const Entry& a, const Entry& b) {
		if (a.name != b.name)
			return a.name < b.name;
		return a.bundle < b.bundle;
	});
	return entries;
}

RegistrySnapshot ScannerReader::read (const ReaderContext& context) const
{
	RegistrySnapshot snapshot;
	snapshot.host = context.hostName;
	snapshot.hostVersion = context.hostVersion;
	snapshot.reader = id ();

	auto folders = context.pluginFolders;
	for (const auto& extra : context.extraSearchPaths)
		folders.push_back (extra);

	std::vector<std::string> present;
	for (const auto& folder : folders)
	{
		if (fs::directoryExists (folder))
			present.push_back (folder);
	}

	snapshot.entries = scanVST3Folders (present, kPluginScanDepth);
	snapshot.state = ReaderState::Ok;
	snapshot.source = join (present, ", ");
	if (present.empty ())
		snapshot.stateDetail = "No plugin folder exists yet. Add one to Crate's folder list.";
	else if (snapshot.entries.empty ())
		snapshot.stateDetail = "Scanned " + join (present, ", ") + " and found no .vst3 bundle.";
	else
		snapshot.stateDetail = "Read " + std::to_string (snapshot.entries.size ()) +
		                       " plugin classes from " + std::to_string (present.size ()) +
		                       " folder(s).";
	return snapshot;
}

} // namespace crate::registry
