#include "../source/registry/registryservice.h"
#include "../source/util/fs.h"
#include "test_framework.h"
#include "test_util.h"

#include <cstdlib>

using namespace crate;
using namespace crate::registry;

namespace {
Entry* findByName (std::vector<Entry>& entries, const std::string& name)
{
	for (auto& entry : entries)
	{
		if (entry.name == name)
			return &entry;
	}
	return nullptr;
}
// Windows has no setenv/unsetenv. _putenv_s with an empty value removes the
// variable, which is the only thing these tests need.
void setRegistryVar (const char* value)
{
#if defined(_WIN32)
    _putenv_s ("CRATE_REGISTRY", value);
#else
    setenv ("CRATE_REGISTRY", value, 1);
#endif
}

void clearRegistryVar ()
{
#if defined(_WIN32)
    _putenv_s ("CRATE_REGISTRY", "");
#else
    unsetenv ("CRATE_REGISTRY");
#endif
}
} // namespace

CRATE_TEST (registry_service_reads_reader_and_enriches_lazily)
{
	test::TempDir dir;
	auto plugins = dir.mkdir ("plugins");
	test::makeBundle (dir, "plugins/FabFilter Pro-Q 4.vst3", "BD58B550F9E5634E9D2EFF39EA0927B1",
	                  "Pro-Q 4", "FabFilter", {"Fx", "Fx|EQ"}, true);
	auto serum = test::makeBundle (dir, "plugins/Serum.vst3",
	                               "3C4D5E6F708192A3B4C5D6E7F8091A2B", "Serum",
	                               "Xfer Records", {"Instrument", "Instrument|Synth"}, false);
	// A Windows arm64 binary cannot be hosted by this Linux build.
	auto win = dir.mkdir ("plugins/Serum.vst3/Contents/x86_64-win");
	std::vector<uint8_t> pe (0x80, 0);
	pe[0] = 'M';
	pe[1] = 'Z';
	pe[0x3C] = 0x40;
	pe[0x40] = 'P';
	pe[0x41] = 'E';
	pe[0x44] = 0x64;
	pe[0x45] = 0xAA; // IMAGE_FILE_MACHINE_ARM64
	fs::writeFile (win + "/Serum.vst3", pe.data (), pe.size ());

	auto ini = dir.write ("reaper/reaper-vstplugins64.ini",
	                      "[vstcache]\n"
	                      "FabFilter Pro-Q 4.vst3=BD58B550F9E5634E9D2EFF39EA0927B1,1,Pro-Q 4 (FabFilter)\n"
	                      "Serum.vst3=3C4D5E6F708192A3B4C5D6E7F8091A2B,2,Serum (Xfer Records)!!!VSTi\n"
	                      "Nowhere.vst3=00112233445566778899AABBCCDDEEFF,3,Nowhere (Nobody)\n");
	setRegistryVar (ini.c_str ());

	auto dataDir = dir.mkdir ("data");
	RegistryService service ("REAPER", "7.99", dataDir, {plugins});
	auto snapshot = service.load (false);
	CHECK (snapshot.state == ReaderState::Ok);
	CHECK_EQ (snapshot.entries.size (), size_t (3));
	CHECK_EQ(snapshot.source, ini);

	auto* proq = findByName (snapshot.entries, "Pro-Q 4");
	CHECK (proq != nullptr);
	if (proq)
	{
		service.enrich (*proq);
		CHECK_EQ (proq->uid, std::string ("BD58B550F9E5634E9D2EFF39EA0927B1"));
		CHECK_EQ (proq->vendor, std::string ("FabFilter"));
		CHECK_EQ (proq->category, std::string ("EQ"));
		CHECK (proq->resolution == Resolution::Exact);
		CHECK (proq->status == Status::Ready);
		CHECK (!proq->image.empty ());
		CHECK (fs::fileExists (proq->image));
		CHECK_EQ (proq->imageSource, std::string ("moduleinfo"));
	}

	auto* serumEntry = findByName (snapshot.entries, "Serum");
	CHECK (serumEntry != nullptr);
	if (serumEntry)
	{
		service.enrich (*serumEntry);
		CHECK (serumEntry->isInstrument);
		CHECK (serumEntry->status == Status::Incompatible);
		CHECK (serumEntry->statusDetail.find ("arm64-win") != std::string::npos);
	}

	auto* nowhere = findByName (snapshot.entries, "Nowhere");
	CHECK (nowhere != nullptr);
	if (nowhere)
	{
		service.enrich (*nowhere);
		CHECK (nowhere->resolution == Resolution::Unresolved);
		CHECK_MSG (nowhere->status == Status::Unknown, "status=" + std::to_string (static_cast<int> (nowhere->status)) + " detail=" + nowhere->statusDetail + " bundle=" + nowhere->bundle);
		CHECK (!nowhere->statusDetail.empty ());
	}

	// Calling enrich again must reuse the moduleinfo cache (mtime based), not reread.
	service.enrich (*proq);
	CHECK_EQ (proq->status, Status::Ready);
	clearRegistryVar ();
}

CRATE_TEST (registry_service_explains_unknown_host_and_fallback)
{
	test::TempDir dir;
	auto dataDir = dir.mkdir ("data");
	RegistryService unknown ("Some DAW", "1.0", dataDir);
	auto snapshot = unknown.load (false);
	CHECK (snapshot.state == ReaderState::NoHost);
	CHECK (snapshot.stateDetail.find ("Some DAW") != std::string::npos);
	CHECK (snapshot.entries.empty ());

	// Fallback cache: opt-in, explicitly labelled.
	std::string cached = R"({
		"scanner": "crate-scan@1",
		"entries": [
			{ "name": "Fallback Plugin", "vendor": "Fallback Audio",
			  "bundle": "/nowhere/Fallback Plugin.vst3", "uid": "00112233445566778899AABBCCDDEEFF",
			  "is_instrument": false, "arch": [] }
		]
	})";
	dir.write ("data/fallback-index.json", cached);
	setRegistryVar ("/nonexistent/reaper-vstplugins64.ini");
	RegistryService reaper ("REAPER", "7.99", dataDir);
	auto fallback = reaper.load (true);
	CHECK (fallback.state == ReaderState::NotFound);
	CHECK_EQ (fallback.entries.size (), size_t (1));
	if (!fallback.entries.empty ())
	{
		CHECK_EQ (fallback.entries[0].name, std::string ("Fallback Plugin"));
		CHECK_EQ (fallback.entries[0].source, std::string ("scanner-fallback"));
		CHECK (fallback.entries[0].resolution == Resolution::Unresolved);
	}
	CHECK (fallback.stateDetail.find ("fallback") != std::string::npos);

	// Without the opt-in, the fallback cache is never used.
	RegistryService reaper2 ("REAPER", "7.99", dataDir);
	auto noFallback = reaper2.load (false);
	CHECK (noFallback.state == ReaderState::NotFound);
	CHECK (noFallback.entries.empty ());
	clearRegistryVar ();
}

CRATE_TEST (registry_service_marks_blacklist_without_hiding)
{
	RegistrySnapshot snapshot;
	Entry one;
	one.uid = "AAAA";
	one.name = "Bad";
	Entry two;
	two.uid = "BBBB";
	two.name = "Crashed";
	Entry three;
	three.uid = "CCCC";
	three.name = "Fine";
	snapshot.entries = {one, two, three};

	RegistryService service ("REAPER", "7", std::string ());
	service.applyBlacklist (snapshot, {"AAAA"}, {"BBBB"});
	CHECK_EQ (snapshot.entries.size (), size_t (3));
	CHECK (snapshot.entries[0].status == Status::Blacklisted);
	CHECK (snapshot.entries[1].status == Status::Crashed);
	CHECK (snapshot.entries[2].uid == "CCCC");
	CHECK (snapshot.entries[2].status == Status::Unknown);
}
