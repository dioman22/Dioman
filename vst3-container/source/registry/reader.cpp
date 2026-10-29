#include "reader.h"
#include "flreader.h"
#include "livereader.h"
#include "reaperreader.h"

#include "../util/fs.h"
#include "../util/stringutil.h"

#include <algorithm>
#include <cctype>

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
	// Anything that is not a letter or a digit is noise for comparison purposes:
	// "Pro-Q 4", "Pro_Q_4" and "Pro Q 4" name the same plugin, and so do
	// "Blue Cat's" and "Blue Cats". Bytes of a multi-byte UTF-8 sequence are kept
	// untouched, so a name in a non-Latin script is not erased into an empty key.
	s.erase (std::remove_if (s.begin (), s.end (),
	                         [] (char ch) {
		                         auto c = static_cast<unsigned char> (ch);
		                         if (c >= 0x80)
			                         return false;
		                         return std::isalnum (c) == 0;
	                         }),
	         s.end ());
	if (endsWith (s, "vst3"))
		s.erase (s.size () - 4);
	else if (endsWith (s, "vst"))
		s.erase (s.size () - 3);
	// The module on disk and the name a host recorded for it often differ only by
	// an architecture token: REAPER writes "Filterscape_x64_.vst3" for the bundle
	// installed as "Filterscape(x64).vst3", and "Diva_x64_.vst3" for "Diva.vst3".
	// Without dropping the token every such entry stays unresolved forever and the
	// card can never load.
	for (const char* token : {"amd64", "win64", "win32", "x64", "x86"})
		s = replaceAll (s, token, "");
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

std::vector<Candidate> collectCandidates (const std::vector<fs::DirEntry>& items)
{
	std::vector<Candidate> candidates;
	for (const auto& item : items)
	{
		if (item.isDirectory && endsWith (toLower (item.name), ".vst3"))
			candidates.push_back ({item.path, fs::stem (item.name)});
		else if (!item.isDirectory && looksLikePluginFile (item.name))
			candidates.push_back ({item.path, fs::stem (item.name)});
	}
	return candidates;
}
} // namespace

BundleIndex BundleIndex::build (const std::vector<std::string>& folders)
{
	BundleIndex index;
	for (const auto& folder : folders)
	{
		auto items = fs::listDirectoryRecursive (folder, kPluginScanDepth);
		index.items_.insert (index.items_.end (), items.begin (), items.end ());
	}
	return index;
}

BundleResolution BundleIndex::resolveByName (const std::string& displayName) const
{
	BundleResolution result;
	auto wanted = bundleMatchKey (displayName);
	if (wanted.empty ())
		return result;

	std::vector<std::string> exact;
	std::vector<std::string> normalized;
	for (const auto& candidate : collectCandidates (items_))
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

BundleResolution BundleIndex::resolveByFileName (const std::string& fileName) const
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
	for (const auto& item : items_)
	{
		if (iequals (item.name, fileName))
		{
			addMatch (item.path);
			exactName = true;
		}
		else if (bundleMatchKey (item.name) == wanted)
			addMatch (item.path);
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

BundleResolution resolveBundleByName (const std::string& displayName,
                                      const std::vector<std::string>& folders)
{
	return BundleIndex::build (folders).resolveByName (displayName);
}

BundleResolution resolveBundleByFileName (const std::string& fileName,
                                          const std::vector<std::string>& folders)
{
	return BundleIndex::build (folders).resolveByFileName (fileName);
}

} // namespace crate::registry
