#include "reader.h"
#include "flreader.h"
#include "livereader.h"
#include "reaperreader.h"

#include "../util/fs.h"
#include "../util/stringutil.h"

#include <algorithm>

namespace crate::registry {

HostKind hostKindFromName (const std::string& hostName)
{
	auto name = toLower (hostName);
	if (name.find ("reaper") != std::string::npos)
		return HostKind::Reaper;
	if (name.find ("ableton") != std::string::npos || name.find ("live") != std::string::npos)
		return HostKind::Live;
	if (name.find ("fl studio") != std::string::npos || name.find ("fruityloops") != std::string::npos ||
	    name == "fl64")
		return HostKind::FLStudio;
	if (name.find ("bitwig") != std::string::npos)
		return HostKind::Bitwig;
	if (name.find ("cubase") != std::string::npos || name.find ("nuendo") != std::string::npos)
		return HostKind::Cubase;
	if (name.find ("studio one") != std::string::npos || name.find ("studioone") != std::string::npos)
		return HostKind::StudioOne;
	return HostKind::Unknown;
}

const char* hostKindKey (HostKind kind)
{
	switch (kind)
	{
		case HostKind::Reaper:
			return "reaper";
		case HostKind::Live:
			return "live";
		case HostKind::FLStudio:
			return "fl";
		case HostKind::Bitwig:
			return "bitwig";
		case HostKind::Cubase:
			return "cubase";
		case HostKind::StudioOne:
			return "studio-one";
		case HostKind::Unknown:
			return "unknown";
	}
	return "unknown";
}

const char* hostKindDisplayName (HostKind kind)
{
	switch (kind)
	{
		case HostKind::Reaper:
			return "REAPER";
		case HostKind::Live:
			return "Ableton Live";
		case HostKind::FLStudio:
			return "FL Studio";
		case HostKind::Bitwig:
			return "Bitwig Studio";
		case HostKind::Cubase:
			return "Cubase";
		case HostKind::StudioOne:
			return "Studio One";
		case HostKind::Unknown:
			return "Unknown host";
	}
	return "Unknown host";
}

RegistryReader* createReader (HostKind kind)
{
	switch (kind)
	{
		case HostKind::Reaper:
			return new ReaperReader ();
		case HostKind::Live:
			return new LiveReader ();
		case HostKind::FLStudio:
			return new FLReader ();
		default:
			return nullptr; // tiers Bitwig/Cubase/Studio One: discover before shipping
	}
}

std::string bundleMatchKey (const std::string& text)
{
	std::string s = toLower (text);
	// Drop a trailing parenthesised group, channel counts included.
	while (true)
	{
		auto v = trimView (s);
		if (v.empty () || v.back () != ')')
			break;
		auto open = v.rfind ('(');
		if (open == std::string_view::npos)
			break;
		s = std::string (trimView (v.substr (0, open)));
	}
	auto dot = s.rfind ('.');
	if (dot != std::string::npos)
	{
		auto ext = s.substr (dot + 1);
		if (ext == "vst3" || ext == "dll" || ext == "so" || ext == "vst" || ext == "dylib")
			s = s.substr (0, dot);
	}
	auto strip = [] (char c) {
		return c == ' ' || c == '_' || c == '-' || c == '\\' || c == '/';
	};
	s.erase (std::remove_if (s.begin (), s.end (), strip), s.end ());
	if (endsWith (s, "vst3"))
		s.erase (s.size () - 4);
	else if (endsWith (s, "vst"))
		s.erase (s.size () - 3);
	return s;
}

namespace {
bool looksLikePluginFile (const std::string& name)
{
	auto lower = toLower (name);
	return endsWith (lower, ".vst3") || endsWith (lower, ".dll") || endsWith (lower, ".so") ||
	       endsWith (lower, ".dylib");
}

struct Candidate
{
	std::string path;
	std::string stem;
};
} // namespace

BundleResolution resolveBundleByName (const std::string& displayName,
                                      const std::vector<std::string>& folders)
{
	BundleResolution result;
	auto wanted = bundleMatchKey (displayName);
	if (wanted.empty ())
		return result;

	std::vector<Candidate> candidates;
	for (const auto& folder : folders)
	{
		for (const auto& item : fs::listDirectoryIfExists (folder))
		{
			if (item.isDirectory && endsWith (toLower (item.name), ".vst3"))
				candidates.push_back ({item.path, fs::stem (item.name)});
			else if (!item.isDirectory && looksLikePluginFile (item.name))
				candidates.push_back ({item.path, fs::stem (item.name)});
		}
	}

	std::vector<std::string> exact;
	std::vector<std::string> normalized;
	for (const auto& candidate : candidates)
	{
		if (iequals (candidate.stem, displayName) || iequals (candidate.stem, fs::stem (displayName)))
			exact.push_back (candidate.path);
		else if (bundleMatchKey (candidate.stem) == wanted)
			normalized.push_back (candidate.path);
	}
	auto& chosen = !exact.empty () ? exact : normalized;
	result.resolution = !exact.empty () ? Resolution::Exact : Resolution::ByName;
	if (chosen.size () == 1)
		result.path = chosen.front ();
	else if (chosen.size () > 1)
	{
		result.resolution = Resolution::Ambiguous;
		result.candidates = chosen;
	}
	else
		result.resolution = Resolution::Unresolved;
	return result;
}

BundleResolution resolveBundleByFileName (const std::string& fileName,
                                          const std::vector<std::string>& folders)
{
	BundleResolution result;
	if (fileName.empty ())
		return result;
	if (fs::fileExists (fileName) || fs::directoryExists (fileName))
	{
		result.path = fileName;
		result.resolution = Resolution::Exact;
		return result;
	}
	auto wanted = bundleMatchKey (fileName);
	std::vector<std::string> matches;
	auto addMatch = [&matches] (const std::string& path) {
		if (std::find (matches.begin (), matches.end (), path) == matches.end ())
			matches.push_back (path);
	};
	bool exactName = false;
	for (const auto& folder : folders)
	{
		for (const auto& item : fs::listDirectoryIfExists (folder))
		{
			if (iequals (item.name, fileName))
			{
				addMatch (item.path);
				exactName = true;
			}
			else if (bundleMatchKey (item.name) == wanted)
				addMatch (item.path);
		}
	}
	if (matches.size () == 1)
	{
		result.path = matches.front ();
		result.resolution = exactName ? Resolution::Exact : Resolution::ByName;
	}
	else if (matches.size () > 1)
	{
		result.resolution = Resolution::Ambiguous;
		result.candidates = matches;
	}
	return result;
}

} // namespace crate::registry
