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

		Entry entry;
		entry.source = "reaper-ini";
		auto display = fields.back ();
		parseReaperDisplayName (display, entry.name, entry.vendor, entry.isInstrument);
		if (entry.name.empty ())
			continue;
		auto ext = toLower (fs::extension (key));
		if (ext == ".vst3")
			entry.format = "VST3";
		else if (ext == ".dll" || ext == ".so" || ext == ".dylib")
			entry.format = "VST";
		entry.hostScanState = std::strtoll (fields[1].c_str (), nullptr, 10);
		entry.registryName = key;
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

	for (auto& entry : snapshot.entries)
	{
		auto resolved = resolveBundleByFileName (entry.registryName, searchFolders);
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
