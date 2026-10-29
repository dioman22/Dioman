#include "reaperreader.h"

#include "../util/fs.h"
#include "../util/stringutil.h"

#include <cctype>
#include <cstdlib>

namespace crate::registry {

namespace {

bool isChannelGroup (const std::string& group)
{
	auto lower = toLower (group);
	if (lower == "mono" || lower == "stereo")
		return true;
	bool hasDigit = false;
	for (char c : lower)
	{
		if (std::isdigit (static_cast<unsigned char> (c)))
			hasDigit = true;
	}
	if (!hasDigit)
		return false;
	return lower.find ("ch") != std::string::npos || lower.find ("out") != std::string::npos ||
	       lower.find ("in") != std::string::npos;
}

std::vector<std::string> resourceCandidates (const ReaderContext& context)
{
	std::vector<std::string> dirs;
	if (!context.registryOverride.empty ())
	{
		if (fs::directoryExists (context.registryOverride))
			dirs.push_back (context.registryOverride);
		else
			dirs.push_back (fs::parentPath (context.registryOverride));
	}
	auto env = fs::environmentVariable ("CRATE_REAPER_DIR");
	if (!env.empty ())
		dirs.push_back (env);
	auto exe = fs::currentExecutablePath ();
	if (!exe.empty ())
		dirs.push_back (fs::parentPath (exe)); // portable install
#if defined(_WIN32)
	auto appData = fs::environmentVariable ("APPDATA");
	if (!appData.empty ())
		dirs.push_back (fs::join (appData, "REAPER"));
#elif defined(__APPLE__)
	dirs.push_back (fs::join (fs::homeDirectory (), "Library/Application Support/REAPER"));
#else
	dirs.push_back (fs::join (fs::homeDirectory (), ".config/REAPER"));
#endif
	return dirs;
}

std::vector<std::string> reaperVstPaths (const std::string& resourceDir)
{
	std::vector<std::string> paths;
	std::string ini;
	if (!fs::readFile (fs::join (resourceDir, "reaper.ini"), ini))
		return paths;
	for (auto& line : split (ini, '\n'))
	{
		line = std::string (trimView (line));
		auto equals = line.find ('=');
		if (equals == std::string::npos)
			continue;
		auto key = toLower (line.substr (0, equals));
		if (key != "vstpath" && key != "vstpath64")
			continue;
		auto value = line.substr (equals + 1);
		for (auto& part : split (value, ';'))
		{
			auto p = std::string (trimView (part));
			if (!p.empty ())
				paths.push_back (p);
		}
	}
	return paths;
}

/** Everything from the first '<' on is a REAPER-local class identifier, not part
 *  of the file name: "Thing.vst3<123456" is the bundle "Thing.vst3". */
std::string stripClassSuffix (const std::string& key)
{
	auto suffix = key.find ('<');
	if (suffix == std::string::npos)
		return key;
	return std::string (trimView (key.substr (0, suffix)));
}

/** REAPER writes "<SHELL>" in the display field of a module row that carries no
 *  class of its own; the classes are the separate "Thing.vst3<id" rows. Such a row
 *  has no name, no vendor and nothing that can be loaded, so it must not become a
 *  card — treating it as one produced a grid full of "<SHELL> / unknown vendor". */
bool isPlaceholderName (const std::string& name)
{
	return name.size () >= 2 && name.front () == '<' && name.back () == '>';
}

} // namespace

void parseReaperDisplayName (const std::string& raw, std::string& name, std::string& vendor,
                             bool& isInstrument)
{
	std::string s = std::string (trimView (raw));
	isInstrument = false;
	// Trailing "!!!VSTi" / "!!!VST" markers.
	while (true)
	{
		auto marker = s.find ("!!!");
		if (marker == std::string::npos)
			break;
		auto flags = toLower (s.substr (marker + 3));
		if (flags.find ("vsti") != std::string::npos)
			isInstrument = true;
		s = std::string (trimView (s.substr (0, marker)));
	}
	// Channel suffixes such as "(16 out)", "(8ch)", "(mono)".
	while (true)
	{
		auto t = trimView (s);
		if (t.empty () || t.back () != ')')
			break;
		auto open = t.rfind ('(');
		if (open == std::string_view::npos)
			break;
		auto group = std::string (trimView (t.substr (open + 1, t.size () - open - 2)));
		if (!isChannelGroup (group))
			break;
		s = std::string (trimView (t.substr (0, open)));
	}
	// The remaining trailing group is the vendor, as REAPER's own browser shows it.
	auto t = trimView (s);
	if (!t.empty () && t.back () == ')')
	{
		auto open = t.rfind ('(');
		if (open != std::string_view::npos && open > 0)
		{
			vendor = std::string (trimView (t.substr (open + 1, t.size () - open - 2)));
			s = std::string (trimView (t.substr (0, open)));
		}
	}
	name = s;
	if (name.empty ())
		name = raw;
}

std::vector<Entry> parseReaperCache (const std::string& text)
{
	std::vector<Entry> entries;
	bool inCache = false;
	for (auto& rawLine : split (text, '\n'))
	{
		auto line = std::string (trimView (rawLine));
		if (line.empty () || line[0] == ';' || line[0] == '#')
			continue;
		if (line.front () == '[')
		{
			inCache = iequals (line, "[vstcache]");
			continue;
		}
		if (!inCache)
			continue;
		auto equals = line.find ('=');
		if (equals == std::string::npos)
			continue;
		auto key = std::string (trimView (line.substr (0, equals)));
		auto value = std::string (trimView (line.substr (equals + 1)));
		if (key.empty () || value.empty ())
			continue;
		auto fields = split (value, ',');
		if (fields.size () < 2)
			continue;

		// Real [vstcache] rows look like this:
		//
		//   Thing.vst3=0032BE7E7159D801,1628748483{56535441627974616D706C6520626173,Thing (Vendor)
		//   Shell.vst3=0030CE162AE0D901,0,<SHELL>
		//   Shell.vst3<86606732=0030CE162AE0D901,86606732{D39D5B69D6AF42FA12345678414D4551,One (u-he)
		//
		// so the class id and the class UID live in the field before the display
		// name, and a vendor name may itself contain a comma. Only the first two
		// fields are positional, which is why the name is everything from index 2 on.
		std::string display;
		if (fields.size () >= 3)
		{
			std::vector<std::string> tail (fields.begin () + 2, fields.end ());
			display = join (tail, ",");
		}
		else
			display = fields.back ();

		auto fileKey = stripClassSuffix (key);
		if (fileKey.empty ())
			continue;

		Entry entry;
		entry.source = "reaper-ini";
		parseReaperDisplayName (display, entry.name, entry.vendor, entry.isInstrument);
		if (entry.name.empty () || isPlaceholderName (entry.name))
			continue;
		// The extension has to come from the stripped key: on a shell row the raw
		// key ends in ".vst3<id", which is not an extension, so the format stayed
		// empty and the card never got its VST3 badge.
		auto ext = toLower (fs::extension (fileKey));
		if (ext == ".vst3")
			entry.format = "VST3";
		else if (ext == ".dll" || ext == ".so" || ext == ".dylib")
			entry.format = "VST";
		entry.hostScanState = std::strtoll (fields[1].c_str (), nullptr, 10);
		entry.registryName = fileKey;
		entries.push_back (std::move (entry));
	}
	return entries;
}

RegistrySnapshot ReaperReader::read (const ReaderContext& context) const
{
	RegistrySnapshot snapshot;
	snapshot.host = context.hostName.empty () ? "REAPER" : context.hostName;
	snapshot.hostVersion = context.hostVersion;
	snapshot.reader = id ();

	std::vector<std::string> tried;
	std::string chosenDir;
	std::string chosenFile;
	const std::vector<std::string> fileNames = {"reaper-vstplugins64.ini",
	                                            "reaper-vstplugins.ini"};
	auto dirs = resourceCandidates (context);
	if (!context.registryOverride.empty () && fs::fileExists (context.registryOverride))
	{
		chosenFile = context.registryOverride;
		chosenDir = fs::parentPath (chosenFile);
	}
	else
	{
		for (const auto& dir : dirs)
		{
			for (const auto& name : fileNames)
			{
				auto candidate = fs::join (dir, name);
				tried.push_back (candidate);
				if (chosenFile.empty () && fs::fileExists (candidate))
				{
					chosenFile = candidate;
					chosenDir = dir;
				}
			}
		}
	}

	if (chosenFile.empty ())
	{
		snapshot.state = ReaderState::NotFound;
		snapshot.stateDetail = "No REAPER plugin cache found. Looked in: " + join (tried, ", ");
		return snapshot;
	}

	snapshot.source = chosenFile;
	snapshot.writtenAt = fs::modifiedTime (chosenFile);
	std::string text;
	if (!fs::readFile (chosenFile, text))
	{
		snapshot.state = ReaderState::Unreadable;
		snapshot.stateDetail = "The REAPER cache exists but could not be read (permissions?).";
		return snapshot;
	}

	snapshot.entries = parseReaperCache (text);
	auto searchFolders = context.pluginFolders;
	for (auto& path : reaperVstPaths (chosenDir))
		searchFolders.push_back (path);
	for (auto& path : context.extraSearchPaths)
		searchFolders.push_back (path);

	// One walk of the plugin folders for the whole cache: a real [vstcache] holds
	// well over a thousand rows, and resolving each of them on its own would walk
	// the library once per row.
	auto index = BundleIndex::build (searchFolders);
	for (auto& entry : snapshot.entries)
	{
		auto resolved = index.resolveByFileName (entry.registryName);
		entry.resolution = resolved.resolution;
		entry.bundle = resolved.path;
		if (resolved.resolution == Resolution::Unresolved)
			entry.statusDetail = "REAPER listed this file but Crate cannot find it in a plugin "
			                     "folder. Check the folder list in REAPER's settings.";
		else if (resolved.resolution == Resolution::Ambiguous)
			entry.statusDetail = "More than one bundle matches this name: " +
			                     join (resolved.candidates, ", ");
	}
	snapshot.state = ReaderState::Ok;
	if (snapshot.entries.empty ())
		snapshot.stateDetail = "The REAPER cache is readable but has no entries in [vstcache].";
	return snapshot;
}

} // namespace crate::registry
