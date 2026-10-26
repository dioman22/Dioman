#include "../source/registry/flreader.h"
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

CRATE_TEST (fl_scans_categories_from_fixture_folder)
{
	auto entries = scanFLPluginDatabase (test::fixture ("fl/Plugin database"));
	CHECK_EQ (entries.size (), size_t (6));

	auto* proq = findByName (entries, "Pro-Q 4");
	CHECK (proq != nullptr);
	if (proq)
	{
		CHECK_EQ (proq->category, std::string ("Effects"));
		CHECK (!proq->isInstrument);
		CHECK_EQ (proq->registryName, std::string ("EQ/Pro-Q 4.fst"));
		CHECK_EQ (proq->source, std::string ("fl-db"));
	}

	auto* serum = findByName (entries, "Serum");
	CHECK (serum != nullptr);
	if (serum)
	{
		CHECK_EQ (serum->category, std::string ("Generators"));
		CHECK (serum->isInstrument);
	}
}

CRATE_TEST (fl_reader_resolves_bundles_and_reports_missing_folder)
{
	test::TempDir dir;
	test::makeBundle (dir, "plugins/Serum.vst3", "3C4D5E6F708192A3B4C5D6E7F8091A2B", "Serum",
	                  "Xfer Records", {"Instrument", "Instrument|Synth"});
	ReaderContext context;
	context.hostName = "FL Studio";
	context.registryOverride = test::fixture ("fl/Plugin database");
	context.pluginFolders = {dir.path () + "/plugins"};
	FLReader reader;
	auto snapshot = reader.read (context);
	CHECK (snapshot.state == ReaderState::Ok);
	CHECK_EQ (snapshot.entries.size (), size_t (6));
	auto* serum = findByName (snapshot.entries, "Serum");
	CHECK (serum && serum->resolution == Resolution::Exact);
	auto* proq = findByName (snapshot.entries, "Pro-Q 4");
	CHECK (proq && proq->resolution == Resolution::Unresolved);

	ReaderContext missing;
	missing.registryOverride = "/nonexistent/Plugin database";
	FLReader reader2;
	auto snap2 = reader2.read (missing);
	CHECK (snap2.state == ReaderState::NotFound);
	CHECK (!snap2.stateDetail.empty ());
}

CRATE_TEST (bundle_match_key_is_stable)
{
	CHECK_EQ (bundleMatchKey ("FabFilter Pro-Q 4.vst3"), std::string ("fabfilterproq4"));
	CHECK_EQ (bundleMatchKey ("Pro-Q 4 (FabFilter)"), std::string ("proq4"));
	CHECK_EQ (bundleMatchKey ("MT-PowerDrumKit"), std::string ("mtpowerdrumkit"));
	CHECK_EQ (bundleMatchKey ("Serum (16 out)"), std::string ("serum"));
	CHECK_EQ (bundleMatchKey ("Some Plug-in VST3"), std::string ("someplugin"));
}
