#include "livereader.h"

#include "../util/fs.h"
#include "../util/stringutil.h"
#include "sqlite3.h"

#include <chrono>
#include <set>
#include <thread>

namespace crate::registry {

namespace {

const char* kRequiredTable = "plugins";

bool tableNames (sqlite3* db, std::vector<std::string>& names, bool& busy)
{
	names.clear ();
	busy = false;
	sqlite3_stmt* stmt = nullptr;
	int rc = sqlite3_prepare_v2 (db, "SELECT name FROM sqlite_master WHERE type='table'", -1, &stmt,
	                             nullptr);
	if (rc != SQLITE_OK)
	{
		busy = (rc == SQLITE_BUSY || rc == SQLITE_LOCKED);
		return false;
	}
	while ((rc = sqlite3_step (stmt)) == SQLITE_ROW)
	{
		auto text = reinterpret_cast<const char*> (sqlite3_column_text (stmt, 0));
		if (text)
			names.push_back (text);
	}
	sqlite3_finalize (stmt);
	if (rc != SQLITE_DONE)
	{
		busy = (rc == SQLITE_BUSY || rc == SQLITE_LOCKED);
		return false;
	}
	return true;
}

std::set<std::string> columnNames (sqlite3* db, const std::string& table)
{
	std::set<std::string> names;
	std::string sql = "PRAGMA table_info(\"" + table + "\")";
	sqlite3_stmt* stmt = nullptr;
	if (sqlite3_prepare_v2 (db, sql.c_str (), -1, &stmt, nullptr) != SQLITE_OK)
		return names;
	while (sqlite3_step (stmt) == SQLITE_ROW)
	{
		auto text = reinterpret_cast<const char*> (sqlite3_column_text (stmt, 1));
		if (text)
			names.insert (toLower (text));
	}
	sqlite3_finalize (stmt);
	return names;
}

std::string columnText (sqlite3_stmt* stmt, int index)
{
	auto text = reinterpret_cast<const char*> (sqlite3_column_text (stmt, index));
	return text ? std::string (text) : std::string ();
}

bool isBusy (int code)
{
	return code == SQLITE_BUSY || code == SQLITE_LOCKED;
}

/** One read attempt. Returns false and sets busy when the host holds a lock. */
bool readRows (sqlite3* db, LiveDbResult& result, bool& busy)
{
	busy = false;
	auto columns = columnNames (db, kRequiredTable);
	if (columns.count ("name") == 0)
	{
		result.state = ReaderState::UnsupportedVersion;
		result.detail = "The Live plugins table has no 'name' column. This Live version's "
		                "schema is not one Crate has verified, so the grid stays empty instead "
		                "of guessing.";
		return false;
	}

	auto has = [&columns] (const char* name) { return columns.count (name) != 0; };
	std::vector<std::string> select;
	std::vector<std::string> aliases;
	auto add = [&select, &aliases] (const char* column) {
		select.push_back (std::string ("\"") + column + "\"");
		aliases.push_back (column);
	};
	add ("name");
	if (has ("vendor"))
		add ("vendor");
	if (has ("version"))
		add ("version");
	if (has ("sdk_version"))
		add ("sdk_version");
	if (has ("flags"))
		add ("flags");
	if (has ("scanstate"))
		add ("scanstate");
	if (has ("enabled"))
		add ("enabled");
	if (has ("dev_identifier"))
		add ("dev_identifier");
	if (has ("path"))
		add ("path");

	std::string sql = "SELECT ";
	for (size_t i = 0; i < select.size (); ++i)
	{
		if (i)
			sql += ", ";
		sql += select[i];
	}
	sql += " FROM \"" + std::string (kRequiredTable) + "\" ORDER BY \"name\" COLLATE NOCASE";

	sqlite3_stmt* stmt = nullptr;
	int rc = sqlite3_prepare_v2 (db, sql.c_str (), -1, &stmt, nullptr);
	if (rc != SQLITE_OK)
	{
		if (isBusy (rc))
		{
			busy = true;
			return false;
		}
		result.state = ReaderState::Unreadable;
		result.detail = std::string ("Live database query failed: ") + sqlite3_errmsg (db);
		return false;
	}

	while ((rc = sqlite3_step (stmt)) == SQLITE_ROW)
	{
		Entry entry;
		entry.source = "live-db";
		entry.format = "VST3"; // verified only for entries with no contradictory dev_identifier
		int index = 0;
		entry.name = columnText (stmt, index++);
		if (has ("vendor"))
			entry.vendor = columnText (stmt, index++);
		if (has ("version"))
			entry.version = columnText (stmt, index++);
		if (has ("sdk_version"))
			entry.sdkVersion = columnText (stmt, index++);
		if (has ("flags"))
			entry.hostScanState = sqlite3_column_int64 (stmt, index++);
		if (has ("scanstate"))
			entry.hostScanState = sqlite3_column_int64 (stmt, index++);
		if (has ("enabled"))
			entry.hostEnabled = sqlite3_column_int (stmt, index++) != 0;
		if (has ("dev_identifier"))
		{
			auto dev = toLower (columnText (stmt, index++));
			if (dev.find ("vst3") != std::string::npos)
				entry.format = "VST3";
			else if (dev.find ("vst2") != std::string::npos || dev.find ("vst") == 0)
				entry.format = "VST";
			else if (dev.find ("au") == 0 || dev.find ("aax") != std::string::npos)
				entry.format = dev.find ("aax") != std::string::npos ? "AAX" : "AU";
			else if (!dev.empty ())
				entry.format.clear (); // unknown: do not pretend it is VST3
		}
		if (has ("path"))
			entry.bundle = columnText (stmt, index++);
		if (entry.name.empty ())
			continue;
		if (!entry.hostEnabled)
		{
			entry.status = Status::Disabled;
			entry.statusDetail = defaultStatusDetail (Status::Disabled);
		}
		result.entries.push_back (std::move (entry));
	}
	sqlite3_finalize (stmt);
	if (rc != SQLITE_DONE)
	{
		if (isBusy (rc))
		{
			busy = true;
			return false;
		}
		result.state = ReaderState::Unreadable;
		result.detail = std::string ("Reading the Live plugins table failed: ") +
		                sqlite3_errmsg (db);
		return false;
	}
	return true;
}

} // namespace


namespace {

bool openReadOnly (const std::string& path, sqlite3** db, std::string& error)
{
	int rc = sqlite3_open_v2 (path.c_str (), db, SQLITE_OPEN_READONLY, nullptr);
	if (rc != SQLITE_OK)
	{
		error = *db ? sqlite3_errmsg (*db) : "cannot open database";
		if (*db)
			sqlite3_close (*db);
		*db = nullptr;
		return false;
	}
	sqlite3_busy_timeout (*db, 120);
	// Connection-local: it does not write to the file. Together with SQLITE_OPEN_READONLY
	// this makes an accidental UPDATE impossible.
	sqlite3_exec (*db, "PRAGMA query_only=1", nullptr, nullptr, nullptr);
	return true;
}

void closeDb (sqlite3* db)
{
	if (db)
		sqlite3_close (db);
}

} // namespace

std::string copyDatabaseForReading (const std::string& dbPath)
{
	auto stamp = std::chrono::duration_cast<std::chrono::microseconds> (
	                 std::chrono::steady_clock::now ().time_since_epoch ())
	                 .count ();
	auto folder = fs::join (fs::temporaryDirectory (), "crate-live-" + std::to_string (stamp));
	if (!fs::createDirectories (folder))
		return {};
	auto destination = fs::join (folder, fs::fileName (dbPath));
	std::vector<uint8_t> bytes;
	if (!fs::readFileBytes (dbPath, bytes) ||
	    !fs::writeFile (destination, bytes.data (), bytes.size ()))
		return {};
	for (const char* suffix : {"-wal", "-shm"})
	{
		auto source = dbPath + suffix;
		if (!fs::fileExists (source))
			continue;
		std::vector<uint8_t> sidecar;
		if (fs::readFileBytes (source, sidecar))
			fs::writeFile (destination + suffix, sidecar.data (), sidecar.size ());
	}
	return destination;
}

bool readDatabase (sqlite3* db, LiveDbResult& out, bool& busy)
{
	std::vector<std::string> tables;
	if (!tableNames (db, tables, busy))
		return false;
	bool hasPlugins = false;
	for (const auto& table : tables)
	{
		if (iequals (table, kRequiredTable))
			hasPlugins = true;
	}
	if (!hasPlugins)
	{
		out.state = ReaderState::UnsupportedVersion;
		out.detail = "This database has no 'plugins' table (tables: " + join (tables, ", ") +
		             "). It is not the Live plugin database, or it is a version Crate does not "
		             "know.";
		return false;
	}
	return readRows (db, out, busy);
}

LiveDbResult readLiveDb (const std::string& dbPath)
{
	LiveDbResult result;
	result.source = dbPath;
	result.writtenAt = fs::modifiedTime (dbPath);
	if (!fs::fileExists (dbPath))
	{
		result.state = ReaderState::NotFound;
		result.detail = "Live's plugin database is not there: " + dbPath;
		return result;
	}

	std::string error;
	sqlite3* db = nullptr;
	if (!openReadOnly (dbPath, &db, error))
	{
		result.state = ReaderState::Unreadable;
		result.detail = "Live's plugin database could not be opened read-only: " + error;
		return result;
	}

	bool busy = false;
	auto attemptRead = [&] (LiveDbResult& out) { return readDatabase (db, out, busy); };
	for (int attempt = 0; attempt < 3; ++attempt)
	{
		if (attemptRead (result))
		{
			result.state = ReaderState::Ok;
			closeDb (db);
			return result;
		}
		if (!busy)
		{
			closeDb (db);
			return result;
		}
		std::this_thread::sleep_for (std::chrono::milliseconds (25));
	}
	closeDb (db);

	// The host holds the database open in a way that blocks even a read-only reader.
	// Copy the database (and its WAL) to a private folder and read the copy.
	auto copied = copyDatabaseForReading (dbPath);
	if (!copied.empty ())
	{
		sqlite3* copyDb = nullptr;
		if (openReadOnly (copied, &copyDb, error))
		{
			LiveDbResult copyResult = result;
			copyResult.entries.clear ();
			bool copyBusy = false;
			if (readDatabase (copyDb, copyResult, copyBusy))
			{
				copyResult.state = ReaderState::Ok;
				copyResult.readFromCopy = true;
				copyResult.source = dbPath;
				copyResult.detail = "Live has the database locked; Crate read a copy.";
				closeDb (copyDb);
				fs::removeFile (copied);
				fs::removeFile (copied + "-wal");
				fs::removeFile (copied + "-shm");
				return copyResult;
			}
			closeDb (copyDb);
		}
	}

	result.state = ReaderState::Locked;
	result.detail = "Live is holding its plugin database with an exclusive lock and even a copy "
	                "could not be read. Reopen the editor once Live has finished writing.";
	return result;
}


std::string findLiveDatabase (const ReaderContext& context)
{
	if (!context.registryOverride.empty ())
		return context.registryOverride;
	auto env = fs::environmentVariable ("CRATE_LIVE_DB");
	if (!env.empty ())
		return env;
	std::vector<std::string> dirs;
#if defined(_WIN32)
	auto local = fs::environmentVariable ("LOCALAPPDATA");
	if (!local.empty ())
		dirs.push_back (fs::join (local, "Ableton/Live Database"));
#elif defined(__APPLE__)
	dirs.push_back (fs::join (fs::homeDirectory (),
	                          "Library/Application Support/Ableton/Live Database"));
#else
	dirs.push_back (fs::join (fs::homeDirectory (), ".local/share/Ableton/Live Database"));
#endif
	std::string best;
	int64_t bestMtime = 0;
	for (const auto& dir : dirs)
	{
		for (const auto& item : fs::listDirectoryIfExists (dir))
		{
			if (item.isDirectory)
				continue;
			auto lower = toLower (item.name);
			if (!startsWith (lower, "live-plugins") || !endsWith (lower, ".db"))
				continue;
			auto mtime = fs::modifiedTime (item.path);
			if (best.empty () || mtime > bestMtime)
			{
				best = item.path;
				bestMtime = mtime;
			}
		}
	}
	return best;
}

RegistrySnapshot LiveReader::read (const ReaderContext& context) const
{
	RegistrySnapshot snapshot;
	snapshot.host = context.hostName.empty () ? "Ableton Live" : context.hostName;
	snapshot.hostVersion = context.hostVersion;
	snapshot.reader = id ();

	auto dbPath = findLiveDatabase (context);
	if (dbPath.empty ())
	{
		snapshot.state = ReaderState::NotFound;
		snapshot.stateDetail =
		    "No Live-plugins-*.db found. Live creates it after it scans plugins; open Live's "
		    "Preferences > Plug-Ins and rescan once, then reload this list.";
		return snapshot;
	}

	auto result = readLiveDb (dbPath);
	snapshot.entries = std::move (result.entries);
	snapshot.state = result.state;
	snapshot.stateDetail = result.detail;
	snapshot.source = dbPath;
	snapshot.writtenAt = result.writtenAt;
	snapshot.readFromCopy = result.readFromCopy;
	if (snapshot.state != ReaderState::Ok)
		return snapshot;

	for (auto& entry : snapshot.entries)
	{
		if (!entry.bundle.empty () &&
		    (fs::fileExists (entry.bundle) || fs::directoryExists (entry.bundle)))
		{
			entry.resolution = Resolution::Exact;
			continue;
		}
		auto resolved = resolveBundleByName (entry.name, context.pluginFolders);
		entry.resolution = resolved.resolution;
		entry.bundle = resolved.path;
		if (resolved.resolution == Resolution::Unresolved)
			entry.statusDetail = "Live knows this plugin, but no matching .vst3 bundle was found "
			                     "in the folders Crate can see.";
		else if (resolved.resolution == Resolution::Ambiguous)
			entry.statusDetail = "More than one bundle matches this name: " +
			                     join (resolved.candidates, ", ");
	}
	return snapshot;
}

} // namespace crate::registry
