#include "../source/registry/livereader.h"
#include "../source/util/fs.h"
#include "test_framework.h"
#include "test_util.h"
#include "../third_party/sqlite/sqlite3.h"

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

bool execSql (const std::string& path, const char* sql)
{
	sqlite3* db = nullptr;
	if (sqlite3_open (path.c_str (), &db) != SQLITE_OK)
		return false;
	char* error = nullptr;
	bool ok = sqlite3_exec (db, sql, nullptr, nullptr, &error) == SQLITE_OK;
	if (error)
		sqlite3_free (error);
	sqlite3_close (db);
	return ok;
}
} // namespace

CRATE_TEST (live_reads_fixture_database)
{
	auto path = test::fixture ("live/Live-plugins-1.db");
	std::vector<uint8_t> before;
	CHECK (fs::readFileBytes (path, before));
	auto beforeMtime = fs::modifiedTime (path);

	auto result = readLiveDb (path);
	CHECK (result.state == ReaderState::Ok);
	CHECK (!result.readFromCopy);
	CHECK_EQ (result.entries.size (), size_t (6));
	CHECK (result.detail.empty ());

	auto* proq = findByName (result.entries, "Pro-Q 4");
	CHECK (proq != nullptr);
	if (proq)
	{
		CHECK_EQ (proq->vendor, std::string ("FabFilter"));
		CHECK_EQ (proq->version, std::string ("4.01"));
		CHECK_EQ (proq->sdkVersion, std::string ("VST 3.7.5"));
		CHECK_EQ (proq->format, std::string ("VST3"));
		CHECK (proq->hostEnabled);
	}

	auto* serum = findByName (result.entries, "Serum");
	CHECK (serum != nullptr);
	if (serum)
	{
		CHECK (!serum->hostEnabled);
		CHECK (serum->status == Status::Disabled);
	}

	auto* legacy = findByName (result.entries, "LegacyComp");
	CHECK (legacy != nullptr);
	if (legacy)
		CHECK_EQ (legacy->format, std::string ("VST"));

	// The registry must be byte-identical after a full read (read-only guarantee).
	std::vector<uint8_t> after;
	CHECK (fs::readFileBytes (path, after));
	CHECK (before == after);
	CHECK_EQ (beforeMtime, fs::modifiedTime (path));
}

CRATE_TEST (live_reports_unsupported_schema)
{
	test::TempDir dir;
	auto path = dir.path () + "/NotLive.db";
	CHECK (execSql (path, "CREATE TABLE plugins_but_wrong (id INTEGER);"));
	auto result = readLiveDb (path);
	CHECK (result.state == ReaderState::UnsupportedVersion);
	CHECK (result.detail.find ("plugins") != std::string::npos);

	test::TempDir dir2;
	auto path2 = dir2.path () + "/Empty.db";
	CHECK (execSql (path2, "CREATE TABLE modules (id INTEGER);"));
	auto result2 = readLiveDb (path2);
	CHECK (result2.state == ReaderState::UnsupportedVersion);
}

CRATE_TEST (live_missing_database_is_not_a_crash)
{
	auto result = readLiveDb ("/nonexistent/Live-plugins-1.db");
	CHECK (result.state == ReaderState::NotFound);
	CHECK (result.entries.empty ());
}

CRATE_TEST (live_reads_from_copy_when_host_holds_exclusive_lock)
{
	// Work on a copy of the fixture so the committed fixture is never touched.
	test::TempDir dir;
	auto source = test::fixture ("live/Live-plugins-1.db");
	std::vector<uint8_t> bytes;
	CHECK (fs::readFileBytes (source, bytes));
	auto dbPath = dir.path () + "/Live-plugins-1.db";
	CHECK (fs::writeFile (dbPath, bytes.data (), bytes.size ()));

	sqlite3* locker = nullptr;
	CHECK_EQ (sqlite3_open (dbPath.c_str (), &locker), SQLITE_OK);
	CHECK (execSql (dbPath, "CREATE TABLE IF NOT EXISTS lock_probe (x INTEGER);"));
	CHECK_EQ (sqlite3_exec (locker, "BEGIN EXCLUSIVE", nullptr, nullptr, nullptr), SQLITE_OK);

	auto result = readLiveDb (dbPath);
	CHECK (result.state == ReaderState::Ok);
	CHECK_EQ (result.entries.size (), size_t (6));
	// The reader either read directly (if SQLite allowed it) or used the copy. If it used
	// the copy it must say so; it must never claim a clean read of a locked file.
	if (result.readFromCopy)
		CHECK (!result.detail.empty ());

	sqlite3_exec (locker, "ROLLBACK", nullptr, nullptr, nullptr);
	sqlite3_close (locker);
}
