#include "cratepaths.h"

#include "../util/fs.h"
#include "../util/stringutil.h"

namespace crate::app {

namespace {

/** Comparison key for a folder path: case folded and lexically normalised, so
 *  "C:\Vst\" and "c:/vst" are the same folder. */
std::string folderKey (const std::string& folder)
{
	return toLower (fs::normalized (folder));
}

std::vector<std::string> readFolderLines (const std::string& path)
{
	std::vector<std::string> lines;
	std::string text;
	if (!fs::readFile (path, text))
		return lines;
	for (auto& raw : split (text, '\n'))
	{
		auto line = std::string (trimView (raw));
		if (line.empty () || line[0] == '#')
			continue;
		lines.push_back (line);
	}
	return lines;
}

} // namespace

std::string dataDirectory ()
{
	return fs::applicationDataDirectory ();
}

std::string journalPath ()
{
	return fs::join (dataDirectory (), "journal.log");
}

std::string blacklistPath ()
{
	return fs::join (dataDirectory (), "blacklist.txt");
}

std::string settingsPath ()
{
	return fs::join (dataDirectory (), "settings.txt");
}

std::string snapshotCacheDirectory ()
{
	return fs::join (dataDirectory (), "snapshots");
}

std::string fallbackCachePath ()
{
	return fs::join (dataDirectory (), "fallback-index.json");
}

std::string pluginFoldersPath ()
{
	return fs::join (dataDirectory (), "folders.txt");
}

std::vector<std::string> userPluginFolders ()
{
	return readFolderLines (pluginFoldersPath ());
}

bool writeUserPluginFolders (const std::vector<std::string>& folders)
{
	std::vector<std::string> lines;
	lines.push_back ("# Crate plugin folders, one absolute path per line.");
	lines.push_back ("# The standard VST3 folders are always scanned; this file is for the rest.");
	lines.push_back ("# Add or remove folders here by hand, or from Crate's own folder panel.");
	for (const auto& folder : folders)
		lines.push_back (folder);
	return fs::writeFile (pluginFoldersPath (), join (lines, "\n") + "\n");
}

bool addUserPluginFolder (const std::string& folder)
{
	auto trimmed = std::string (trimView (folder));
	if (trimmed.empty ())
		return false;
	auto key = folderKey (trimmed);
	auto current = userPluginFolders ();
	for (const auto& existing : current)
	{
		if (folderKey (existing) == key)
			return false;
	}
	current.push_back (trimmed);
	return writeUserPluginFolders (current);
}

bool removeUserPluginFolder (const std::string& folder)
{
	auto key = folderKey (folder);
	auto current = userPluginFolders ();
	std::vector<std::string> kept;
	for (const auto& existing : current)
	{
		if (folderKey (existing) != key)
			kept.push_back (existing);
	}
	if (kept.size () == current.size ())
		return false;
	return writeUserPluginFolders (kept);
}

bool ensureDataDirectories ()
{
	return fs::createDirectories (dataDirectory ()) &&
	       fs::createDirectories (snapshotCacheDirectory ());
}

} // namespace crate::app
