#include "../source/app/blacklist.h"
#include "../source/app/journal.h"
#include "../source/util/search.h"
#include "test_framework.h"
#include "test_util.h"

using namespace crate;

CRATE_TEST (journal_detects_unfinished_load)
{
	test::TempDir dir;
	auto path = dir.path () + "/journal.log";
	app::Journal journal (path);
	auto suspect = journal.startSession ();
	CHECK (!suspect.found);

	journal.begin ("load_begin", "BD58B550F9E5634E9D2EFF39EA0927B1", "Pro-Q 4",
	               "/plugins/Pro-Q 4.vst3");
	std::string error;
	auto dirty = app::inspectJournal (path, error);
	CHECK_MSG (error.empty (), error);
	CHECK (dirty.found);
	CHECK_EQ (dirty.record.step, std::string ("load_begin"));
	CHECK_EQ (dirty.record.name, std::string ("Pro-Q 4"));
	CHECK_EQ (dirty.record.uid, std::string ("BD58B550F9E5634E9D2EFF39EA0927B1"));

	journal.end ("load_end");
	auto clean = app::inspectJournal (path, error);
	CHECK (!clean.found);
}

CRATE_TEST (journal_escapes_separators)
{
	test::TempDir dir;
	auto path = dir.path () + "/journal.log";
	app::Journal journal (path);
	journal.begin ("editor_begin", "uid", "Name | with\nnewline", "C:\\path|odd");
	std::string error;
	auto dirty = app::inspectJournal (path, error);
	CHECK (dirty.found);
	CHECK_EQ (dirty.record.name, std::string ("Name | with\nnewline"));
	CHECK_EQ (dirty.record.path, std::string ("C:\\path|odd"));
}

CRATE_TEST (blacklist_roundtrip_and_reasons)
{
	test::TempDir dir;
	auto path = dir.path () + "/blacklist.txt";
	app::Blacklist list (path);
	list.load ();
	CHECK (list.entries ().empty ());

	list.add ("AAAA", "Bad Plugin", app::BlacklistReason::CrashedOnLoad);
	list.add ("BBBB", "Hang Plugin", app::BlacklistReason::HangOnLoad);
	std::string error;
	CHECK (list.save (error));
	CHECK_MSG (error.empty (), error);

	app::Blacklist reloaded (path);
	reloaded.load ();
	CHECK_EQ (reloaded.entries ().size (), size_t (2));
	CHECK (reloaded.contains ("aaaa"));
	CHECK (reloaded.contains ("BBBB"));
	auto* entry = reloaded.find ("BBBB");
	CHECK (entry && entry->reason == app::BlacklistReason::HangOnLoad);
	CHECK (entry && entry->name == "Hang Plugin");

	reloaded.add ("AAAA", "Bad Plugin", app::BlacklistReason::CrashedOnEdit);
	CHECK (reloaded.find ("AAAA")->reason == app::BlacklistReason::CrashedOnEdit);
	CHECK (reloaded.remove ("AAAA"));
	CHECK (!reloaded.contains ("AAAA"));
	CHECK (!reloaded.remove ("AAAA"));
}

CRATE_TEST (search_scores_names_and_filters)
{
	std::vector<registry::Entry> entries (3);
	entries[0].name = "Pro-Q 4";
	entries[0].vendor = "FabFilter";
	entries[0].category = "EQ";
	entries[0].format = "VST3";
	entries[0].status = registry::Status::Ready;
	entries[1].name = "Pro-C 2";
	entries[1].vendor = "FabFilter";
	entries[1].category = "Dynamics";
	entries[1].format = "VST3";
	entries[1].status = registry::Status::Ready;
	entries[2].name = "LegacyComp";
	entries[2].vendor = "Old Audio";
	entries[2].category = "Dynamics";
	entries[2].format = "VST";

	util::Filters all;
	all.vst3Only = false;
	auto results = util::search (entries, "", all);
	CHECK_EQ (results.size (), size_t (3));

	auto proq = util::search (entries, "pro q", all);
	CHECK_EQ (proq.size (), size_t (1));
	if (proq.size () == 1)
		CHECK_EQ (proq[0]->name, std::string ("Pro-Q 4"));

	auto byVendor = util::search (entries, "fabfilter", all);
	CHECK_EQ (byVendor.size (), size_t (2));

	auto vst3Only = util::search (entries, "", util::Filters ());
	CHECK_EQ (vst3Only.size (), size_t (2));

	util::Filters eq;
	eq.category = "EQ";
	auto eqResults = util::search (entries, "", eq);
	CHECK_EQ (eqResults.size (), size_t (1));
	CHECK_EQ (eqResults[0]->name, std::string ("Pro-Q 4"));

	auto none = util::search (entries, "zzzz", all);
	CHECK (none.empty ());

	CHECK_EQ (util::categories (entries).size (), size_t (2));
	CHECK_EQ (util::vendors (entries).size (), size_t (2));
}
