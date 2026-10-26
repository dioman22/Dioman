#include "../source/registry/reaperreader.h"
#include "test_framework.h"
#include "test_util.h"

using namespace crate;
using namespace crate::registry;

namespace {
const Entry* findByName (const std::vector<Entry>& entries, const std::string& name)
{
	for (const auto& entry : entries)
	{
		if (entry.name == name)
			return &entry;
	}
	return nullptr;
}
} // namespace

CRATE_TEST (reaper_parses_fixture_cache)
{
	std::string text;
	CHECK (fs::readFile (test::fixture ("reaper-vstplugins64.ini"), text));
	auto entries = parseReaperCache (text);
	CHECK_EQ (entries.size (), size_t (7));

	auto* reacomp = findByName (entries, "ReaComp");
	CHECK (reacomp != nullptr);
	if (reacomp)
	{
		CHECK_EQ (reacomp->vendor, std::string ("Cockos"));
		CHECK_EQ (reacomp->format, std::string ("VST"));
		CHECK (!reacomp->isInstrument);
	}

	auto* drums = findByName (entries, "MT-PowerDrumKit");
	CHECK (drums != nullptr);
	if (drums)
	{
		CHECK_EQ (drums->vendor, std::string ("MANDA AUDIO"));
		CHECK (drums->isInstrument);
		CHECK_EQ (drums->format, std::string ("VST"));
	}

	auto* proq = findByName (entries, "Pro-Q 4");
	CHECK (proq != nullptr);
	if (proq)
	{
		CHECK_EQ (proq->vendor, std::string ("FabFilter"));
		CHECK_EQ (proq->format, std::string ("VST3"));
		CHECK_EQ (proq->registryName, std::string ("FabFilter Pro-Q 4.vst3"));
	}

	auto* diva = findByName (entries, "Diva");
	CHECK (diva != nullptr);
	if (diva)
	{
		CHECK_EQ (diva->vendor, std::string ("u-he"));
		CHECK (diva->isInstrument);
	}

	// "Half Written.vst3=ABCDEF" has no complete value and must not become a card.
	CHECK (findByName (entries, "Half Written") == nullptr);
	// Another INI section is not the cache.
	CHECK (findByName (entries, "Should Not Appear") == nullptr);
}

CRATE_TEST (reaper_reader_reports_missing_cache)
{
	ReaderContext context;
	context.registryOverride = "/nonexistent/crate/reaper-vstplugins64.ini";
	context.hostName = "REAPER";
	ReaperReader reader;
	auto snapshot = reader.read (context);
	CHECK (snapshot.state == ReaderState::NotFound);
	CHECK (!snapshot.stateDetail.empty ());
	CHECK (snapshot.entries.empty ());
	CHECK_EQ (std::string (snapshot.reader), std::string ("reaper-ini@1"));
}

CRATE_TEST (reaper_reader_resolves_and_reports_unresolved)
{
	test::TempDir dir;
	auto plugins = dir.mkdir ("plugins");
	test::makeBundle (dir, "plugins/FabFilter Pro-Q 4.vst3", "BD58B550F9E5634E9D2EFF39EA0927B1",
	                  "Pro-Q 4", "FabFilter", {"Fx", "Fx|EQ"}, true);
	test::makeBundle (dir, "plugins/Diva.vst3", "6E5A776FC9A34D9E9B1D8C2A1F0E4D77", "Diva",
	                  "u-he", {"Instrument", "Instrument|Synth"});

	auto ini = dir.write ("reaper/reaper-vstplugins64.ini",
	                      "[vstcache]\n"
	                      "FabFilter Pro-Q 4.vst3=BD58B550F9E5634E9D2EFF39EA0927B1,1,Pro-Q 4 (FabFilter)\n"
	                      "Diva.vst3=6E5A776FC9A34D9E9B1D8C2A1F0E4D77,2,Diva (u-he)!!!VSTi\n"
	                      "Nowhere.vst3=00112233445566778899AABBCCDDEEFF,3,Nowhere (Nobody)\n");

	ReaderContext context;
	context.registryOverride = ini;
	context.hostName = "REAPER";
	context.pluginFolders = {plugins};
	ReaperReader reader;
	auto snapshot = reader.read (context);
	CHECK (snapshot.state == ReaderState::Ok);
	CHECK_EQ (snapshot.entries.size (), size_t (3));
	CHECK (snapshot.writtenAt > 0);

	auto* proq = findByName (snapshot.entries, "Pro-Q 4");
	CHECK (proq && proq->resolution == Resolution::Exact);
	if (proq)
		CHECK (fs::directoryExists (proq->bundle));

	auto* diva = findByName (snapshot.entries, "Diva");
	CHECK (diva && diva->resolution == Resolution::Exact);

	auto* nowhere = findByName (snapshot.entries, "Nowhere");
	CHECK (nowhere && nowhere->resolution == Resolution::Unresolved);
	if (nowhere)
	{
		CHECK (nowhere->bundle.empty ());
		CHECK (!nowhere->statusDetail.empty ());
	}
}

CRATE_TEST (reaper_reader_marks_ambiguous_names)
{
	test::TempDir dir;
	auto a = dir.mkdir ("a");
	auto b = dir.mkdir ("b");
	test::makeBundle (dir, "a/Twin.vst3", "00112233445566778899AABBCCDDEEFF", "Twin", "A", {"Fx"});
	test::makeBundle (dir, "b/Twin.vst3", "FFEEDDCCBBAA99887766554433221100", "Twin", "B", {"Fx"});
	auto ini = dir.write ("cache.ini", "[vstcache]\nTwin.vst3=AABBCCDDEEFF00112233445566778899,1,Twin (A)\n");
	ReaderContext context;
	context.registryOverride = ini;
	context.pluginFolders = {a, b};
	ReaperReader reader;
	auto snapshot = reader.read (context);
	CHECK_EQ (snapshot.entries.size (), size_t (1));
	CHECK (snapshot.entries[0].resolution == Resolution::Ambiguous);
	CHECK_EQ (snapshot.entries[0].bundle, std::string (""));
}

CRATE_TEST (host_names_map_to_readers)
{
	CHECK (hostKindFromName ("REAPER") == HostKind::Reaper);
	CHECK (hostKindFromName ("Ableton Live 12") == HostKind::Live);
	CHECK (hostKindFromName ("FL Studio 2024") == HostKind::FLStudio);
	CHECK (hostKindFromName ("Bitwig Studio") == HostKind::Bitwig);
	CHECK (hostKindFromName ("Cubase 14") == HostKind::Cubase);
	CHECK (hostKindFromName ("Studio One 7") == HostKind::StudioOne);
	CHECK (hostKindFromName ("Some Unknown Host") == HostKind::Unknown);
	CHECK (createReader (HostKind::Bitwig) == nullptr);
}
