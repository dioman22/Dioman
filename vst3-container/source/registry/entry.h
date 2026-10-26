// The Entry model: one row of whatever plugin registry the running host built.
//
// There is deliberately no authoritative index of our own. Readers return entries;
// resolution (bundle path, UID, vendor, snapshot) happens per visible card and is
// recorded on the entry, so the UI can always say where a fact came from.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace crate::registry {

/** How the entry's bundle was found. */
enum class Resolution
{
	Exact,     ///< registry gave a path, or exactly one bundle matched
	ByName,    ///< matched a bundle file name (case-insensitive)
	Ambiguous, ///< more than one bundle matched; refuse to load
	Unresolved ///< no bundle found; card is still shown
};

const char* toString (Resolution r);

/** The health/state badge on a card. Never inferred from a grey colour: every
 *  status has a sentence in `statusDetail` or a default explanation. */
enum class Status
{
	Ok,
	Ready,           ///< metadata resolved, ready to load (not loaded yet)
	Unknown,         ///< not probed yet
	Unreadable,      ///< bundle or moduleinfo could not be read
	Incompatible,    ///< architecture/channel layout cannot be hosted
	LicenseRequired, ///< the plugin refused to run without activation
	Blacklisted,     ///< crashed or hung before, user can retry
	Crashed,         ///< journal says it died in this process
	Missing,         ///< project references it, plugin is absent
	Disabled         ///< the host itself disabled the entry
};

const char* toString (Status s);
const char* defaultStatusDetail (Status s);

/** One card. `name` is always the host's own display name when the registry has one. */
struct Entry
{
	std::string uid; ///< 32 hex chars, empty until resolved from moduleinfo.json
	std::string name;
	std::string vendor;
	std::string version;
	std::string sdkVersion;
	std::string category; ///< canonical, mapped from sub-categories
	std::vector<std::string> subCategories;
	bool isInstrument = false;
	std::string format; ///< "VST3" when known, empty when the registry does not say
	std::string capabilities; ///< e.g. "stereo+sidechain", from PlugType flags if probed
	std::string bundle;       ///< absolute path, empty until resolved
	std::vector<std::string> arch;
	std::string image;        ///< absolute path to a cached snapshot, may be empty
	std::string imageSource;  ///< "moduleinfo" | "probed" | "generated"
	Resolution resolution = Resolution::Unresolved;
	Status status = Status::Unknown;
	std::string statusDetail;
	bool hostEnabled = true;
	int64_t hostScanState = -1; ///< passed through verbatim; never reinterpreted
	std::string source;         ///< reader id that produced the row
	std::string registryName;   ///< display name the host used, when it differs from `name`
	bool loaded = false;        ///< true for the slot that currently hosts this entry
};

/** Reader state, shown in the header so an empty grid is never unexplained. */
enum class ReaderState
{
	Ok,
	Unreadable,
	Locked,
	NotFound,
	UnsupportedVersion,
	NoHost
};

const char* toString (ReaderState s);

struct RegistrySnapshot
{
	std::string host;        ///< from IHostApplication::getName()
	std::string hostVersion; ///< best effort, "" if unknown
	std::string reader;      ///< "live-db@1"
	std::string source;      ///< file path or folder the rows came from
	ReaderState state = ReaderState::NoHost;
	std::string stateDetail; ///< sentence shown under the header
	int64_t writtenAt = 0;   ///< mtime of the registry file, seconds since epoch
	bool readFromCopy = false;
	std::vector<Entry> entries;

	size_t count () const { return entries.size (); }
	size_t vst3Count () const;
};

/** Canonical category from VST3 sub-categories (the first "Fx|EQ" style token). */
std::string canonicalCategory (const std::vector<std::string>& subCategories,
                               bool isInstrument);

} // namespace crate::registry
