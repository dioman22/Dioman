#include "entry.h"

#include <algorithm>

namespace crate::registry {

const char* toString (Resolution r)
{
	switch (r)
	{
		case Resolution::Exact:
			return "exact";
		case Resolution::ByName:
			return "by_name";
		case Resolution::Ambiguous:
			return "ambiguous";
		case Resolution::Unresolved:
			return "unresolved";
	}
	return "unresolved";
}

const char* toString (Status s)
{
	switch (s)
	{
		case Status::Ok:
			return "ok";
		case Status::Unknown:
			return "unknown";
		case Status::Unreadable:
			return "unreadable";
		case Status::Incompatible:
			return "incompatible";
		case Status::LicenseRequired:
			return "license_required";
		case Status::Blacklisted:
			return "blacklisted";
		case Status::Crashed:
			return "crashed";
		case Status::Missing:
			return "missing";
		case Status::Disabled:
			return "disabled";
	}
	return "unknown";
}

const char* defaultStatusDetail (Status s)
{
	switch (s)
	{
		case Status::Ok:
			return "";
		case Status::Unknown:
			return "Not probed yet. The card is readable from the host's registry.";
		case Status::Unreadable:
			return "The bundle could not be read. It may be damaged or inaccessible.";
		case Status::Incompatible:
			return "This bundle does not contain a binary for this host's architecture.";
		case Status::LicenseRequired:
			return "The plugin needs activation before it can load. Activate it in its own installer.";
		case Status::Blacklisted:
			return "Crate recorded a crash or hang for this plugin. Retry from the card menu.";
		case Status::Crashed:
			return "This plugin was loading when the host last exited unexpectedly.";
		case Status::Missing:
			return "The project references this plugin but its bundle is not installed.";
		case Status::Disabled:
			return "The host has this plugin disabled. Enable it in the host's own preferences.";
	}
	return "";
}

const char* toString (ReaderState s)
{
	switch (s)
	{
		case ReaderState::Ok:
			return "ok";
		case ReaderState::Unreadable:
			return "unreadable";
		case ReaderState::Locked:
			return "locked";
		case ReaderState::NotFound:
			return "not_found";
		case ReaderState::UnsupportedVersion:
			return "unsupported_version";
		case ReaderState::NoHost:
			return "no_host";
	}
	return "no_host";
}

size_t RegistrySnapshot::vst3Count () const
{
	return static_cast<size_t> (std::count_if (entries.begin (), entries.end (), [] (const Entry& e) {
		return e.format.empty () || e.format == "VST3";
	}));
}

std::string canonicalCategory (const std::vector<std::string>& subCategories, bool isInstrument)
{
	// VST3 sub-categories are hierarchical strings such as "Fx|EQ" or
	// "Instrument|Synth". The last token is the most specific, the first is the
	// type. Map a few common ones to the labels producers look for.
	if (isInstrument)
	{
		for (const auto& sub : subCategories)
		{
			if (sub == "Instrument|Synth" || sub == "Synth")
				return "Instrument";
		}
		return "Instrument";
	}
	std::string first;
	for (const auto& sub : subCategories)
	{
		auto parts = sub;
		auto bar = parts.find ('|');
		if (bar != std::string::npos)
			parts = parts.substr (bar + 1);
		if (parts.empty ())
			continue;
		if (parts == "EQ")
			return "EQ";
		if (parts == "Dynamics" || parts == "Compressor" || parts == "Limiter")
			return "Dynamics";
		if (parts == "Reverb" || parts == "Delay" || parts == "Chorus" || parts == "Flanger")
			return "Space";
		if (parts == "Distortion" || parts == "Saturation" || parts == "Amp")
			return "Drive";
		if (parts == "Filter" || parts == "Analyzer" || parts == "Mastering")
			return parts;
		if (first.empty ())
			first = parts;
	}
	if (first.empty ())
		return "Effect";
	return first;
}

} // namespace crate::registry
