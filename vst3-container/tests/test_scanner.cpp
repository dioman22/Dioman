#include "../source/registry/scannerreader.h"
#include "../source/util/fs.h"
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

CRATE_TEST (scanner_reads_a_folder_without_any_host)
{
	test::TempDir dir;
	auto plugins = dir.mkdir ("plugins");
	test::makeBundle (dir, "plugins/Crate.vst3", "00112233445566778899AABBCCDDEEFF", "Crate",
	                  "Crate Audio", {"Fx"});
	ReaderContext context;
	context.hostName = "Cubase 14";
	context.pluginFolders = {plugins};
	ScannerReader reader;
	auto snapshot = reader.read (context);
	CHECK (snapshot.state == ReaderState::Ok);
	CHECK_EQ (std::string (snapshot.reader), std::string ("vst3-fs@1"));
	CHECK_EQ (snapshot.host, std::string ("Cubase 14"));
	CHECK_EQ (snapshot.entries.size (), size_t (1));
	if (!snapshot.entries.empty ())
	{
		CHECK_EQ (snapshot.entries[0].name, std::string ("Crate"));
		CHECK_EQ (snapshot.entries[0].source, std::string ("vst3-fs"));
		CHECK (snapshot.entries[0].resolution == Resolution::Exact);
	}
}

CRATE_TEST (scanner_finds_bundles_in_vendor_subfolders)
{
	test::TempDir dir;
	auto plugins = dir.mkdir ("plugins");
	test::makeBundle (dir, "plugins/Crate.vst3", "00112233445566778899AABBCCDDEEFF", "Crate",
	                  "Crate Audio", {"Fx"});
	test::makeBundle (dir, "plugins/FabFilter/Pro-Q 4.vst3",
	                  "BD58B550F9E5634E9D2EFF39EA0927B1", "Pro-Q 4", "FabFilter", {"Fx", "Fx|EQ"});

	auto entries = scanVST3Folders ({plugins}, kPluginScanDepth);
	CHECK_EQ (entries.size (), size_t (2));
	auto* proq = findByName (entries, "Pro-Q 4");
	CHECK (proq != nullptr);
	if (proq)
	{
		CHECK_EQ (proq->vendor, std::string ("FabFilter"));
		CHECK_EQ (proq->category, std::string ("EQ"));
		CHECK_EQ (proq->format, std::string ("VST3"));
		CHECK_EQ (proq->uid, std::string ("BD58B550F9E5634E9D2EFF39EA0927B1"));
		CHECK (proq->resolution == Resolution::Exact);
		CHECK (fs::directoryExists (proq->bundle));
		CHECK (proq->status == Status::Ready);
	}
}

CRATE_TEST (scanner_makes_one_card_per_audio_module_class)
{
	test::TempDir dir;
	auto plugins = dir.mkdir ("plugins");
	dir.mkdir ("plugins/Multi.vst3/Contents/Resources");
	dir.write ("plugins/Multi.vst3/Contents/Resources/moduleinfo.json", R"({
		"Name": "Multi",
		"Version": "1.0.0",
		"Factory Info": { "Vendor": "Vendor" },
		"Classes": [
			{ "CID": "00112233445566778899AABBCCDDEEFF", "Category": "Audio Module Class",
			  "Name": "Multi One", "Vendor": "Vendor", "Sub Categories": [ "Fx" ] },
			{ "CID": "FFEEDDCCBBAA99887766554433221100", "Category": "Audio Module Class",
			  "Name": "Multi Two", "Vendor": "Vendor",
			  "Sub Categories": [ "Instrument", "Instrument|Synth" ] },
			{ "CID": "11112222333344445555666677778888", "Category": "Audio Controller Class",
			  "Name": "Multi Controller", "Vendor": "Vendor" }
		]
	})");

	auto entries = scanVST3Folders ({plugins}, kPluginScanDepth);
	// One card per loadable class. A controller class is not something a host puts on a
	// track, so it must not become one.
	CHECK_EQ (entries.size (), size_t (2));
	auto* two = findByName (entries, "Multi Two");
	CHECK (two != nullptr);
	if (two)
	{
		CHECK (two->isInstrument);
		CHECK_EQ (two->uid, std::string ("FFEEDDCCBBAA99887766554433221100"));
	}
	CHECK (findByName (entries, "Multi Controller") == nullptr);
}

CRATE_TEST (scanner_reports_a_bundle_it_cannot_describe)
{
	test::TempDir dir;
	auto plugins = dir.mkdir ("plugins");
	// No moduleinfo.json: the card still exists, named after the file, carrying the real
	// reason instead of an invented vendor.
	dir.mkdir ("plugins/Mystery.vst3");
	auto entries = scanVST3Folders ({plugins}, kPluginScanDepth);
	CHECK_EQ (entries.size (), size_t (1));
	if (!entries.empty ())
	{
		CHECK_EQ (entries[0].name, std::string ("Mystery"));
		CHECK_EQ (entries[0].format, std::string ("VST3"));
		CHECK (entries[0].resolution == Resolution::Exact);
		CHECK (!entries[0].statusDetail.empty ());
		CHECK (entries[0].status == Status::Unknown ||
		       entries[0].status == Status::Incompatible);
	}
}

CRATE_TEST (scanner_does_not_walk_into_a_bundle)
{
	test::TempDir dir;
	auto plugins = dir.mkdir ("plugins");
	test::makeBundle (dir, "plugins/Thing.vst3", "00112233445566778899AABBCCDDEEFF", "Thing",
	                  "Vendor", {"Fx"});
	// The Contents tree of a bundle holds resources and binaries. A .vst3 that happens
	// to sit in there is not an installed plugin, and the walk must not descend to
	// reach it: doing so would index every plugin's private files as plugins.
	dir.mkdir ("plugins/Thing.vst3/Contents/x86_64-linux/Nested.vst3");
	auto entries = scanVST3Folders ({plugins}, kPluginScanDepth);
	CHECK_EQ (entries.size (), size_t (1));
	CHECK (findByName (entries, "Nested") == nullptr);
}

CRATE_TEST (scanner_respects_the_depth_cap)
{
	test::TempDir dir;
	auto plugins = dir.mkdir ("plugins");
	test::makeBundle (dir, "plugins/a/b/c/deep.vst3", "00112233445566778899AABBCCDDEEFF", "Deep",
	                  "Vendor", {"Fx"});
	// "a/b/c" is three levels below the folder, so the bundle sits at depth 3.
	CHECK_EQ (scanVST3Folders ({plugins}, 2).size (), size_t (0));
	CHECK_EQ (scanVST3Folders ({plugins}, 3).size (), size_t (1));
}

CRATE_TEST (scanner_lists_a_bundle_once_when_two_folders_overlap)
{
	test::TempDir dir;
	auto outer = dir.mkdir ("outer");
	test::makeBundle (dir, "outer/Thing.vst3", "00112233445566778899AABBCCDDEEFF", "Thing",
	                  "Vendor", {"Fx"});
	// A standard folder and a user folder can point at the same bundle.
	auto entries = scanVST3Folders ({outer, outer}, kPluginScanDepth);
	CHECK_EQ (entries.size (), size_t (1));
}
