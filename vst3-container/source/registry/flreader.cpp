#include "flreader.h"

#include "../util/fs.h"
#include "../util/stringutil.h"

namespace crate::registry {

namespace {

void collectFst (const std::string& folder, const std::string& category, const std::string& sub,
                 std::vector<Entry>& entries, int depth)
{
	if (depth > 2)
		return;
	for (const auto& item : fs::listDirectoryIfExists (folder))
	{
		if (item.isDirectory)
		{
			collectFst (item.path, category, sub.empty () ? item.name : sub + "/" + item.name,
			            entries, depth + 1);
			continue;
		}
		if (!endsWith (toLower (item.name), ".fst"))
			continue;
		Entry entry;
		entry.source = "fl-db";
		entry.name = fs::stem (item.name);
		entry.category = category;
		entry.registryName = sub.empty () ? item.name : sub + "/" + item.name;
		entry.isInstrument = containsNoCase (category, "generator") ||
		                     containsNoCase (sub, "generator");
		entries.push_back (std::move (entry));
	}
}

std::vector<std::string> flDatabaseCandidates (const ReaderContext& context)
{
	std::vector<std::string> dirs;
	if (!context.registryOverride.empty ())
		dirs.push_back (context.registryOverride);
	auto env = fs::environmentVariable ("CRATE_FL_DB");
	if (!env.empty ())
		dirs.push_back (env);
	auto documents = fs::join (fs::homeDirectory (), "Documents");
	dirs.push_back (fs::join (documents, "Image-Line/FL Studio/Presets/Plugin database"));
#if defined(_WIN32)
	auto userProfile = fs::environmentVariable ("USERPROFILE");
	if (!userProfile.empty ())
		dirs.push_back (fs::join (userProfile,
		                          "Documents/Image-Line/FL Studio/Presets/Plugin database"));
#endif
	return dirs;
}

} // namespace

std::vector<Entry> scanFLPluginDatabase (const std::string& folder)
{
	std::vector<Entry> entries;
	if (!fs::directoryExists (folder))
		return entries;
	// The top-level folders are the categories FL shows in its browser.
	for (const auto& item : fs::listDirectoryIfExists (folder))
	{
		if (!item.isDirectory)
			continue;
		collectFst (item.path, item.name, "", entries, 0);
	}
	return entries;
}

RegistrySnapshot FLReader::read (const ReaderContext& context) const
{
	RegistrySnapshot snapshot;
	snapshot.host = context.hostName.empty () ? "FL Studio" : context.hostName;
	snapshot.hostVersion = context.hostVersion;
	snapshot.reader = id ();

	std::string folder;
	std::vector<std::string> tried;
	for (const auto& candidate : flDatabaseCandidates (context))
	{
		tried.push_back (candidate);
		if (folder.empty () && fs::directoryExists (candidate))
			folder = candidate;
	}
	if (folder.empty ())
	{
		snapshot.state = ReaderState::NotFound;
		snapshot.stateDetail = "No FL Studio 'Plugin database' folder found. Looked in: " +
		                       join (tried, ", ");
		return snapshot;
	}

	snapshot.source = folder;
	snapshot.writtenAt = fs::modifiedTime (folder);
	snapshot.entries = scanFLPluginDatabase (folder);
	for (auto& entry : snapshot.entries)
	{
		auto resolved = resolveBundleByName (entry.name, context.pluginFolders);
		entry.resolution = resolved.resolution;
		entry.bundle = resolved.path;
		if (resolved.resolution == Resolution::Ambiguous)
			entry.statusDetail = "More than one .vst3 bundle matches this FL category entry: " +
			                     join (resolved.candidates, ", ");
		else if (resolved.resolution == Resolution::Unresolved)
			entry.statusDetail = "FL has this entry in its plugin database, but Crate could not "
			                     "find a matching .vst3 bundle.";
	}
	snapshot.state = ReaderState::Ok;
	if (snapshot.entries.empty ())
		snapshot.stateDetail = "The FL plugin database folder is empty.";
	return snapshot;
}

} // namespace crate::registry
