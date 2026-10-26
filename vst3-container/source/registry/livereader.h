#pragma once

#include "reader.h"

namespace crate::registry {

/** Ableton Live's Live-plugins-*.db (SQLite). Read-only, copy-on-lock. */
class LiveReader final : public RegistryReader
{
public:
	const char* id () const override { return "live-db@1"; }
	const char* hostKey () const override { return "live"; }
	RegistrySnapshot read (const ReaderContext& context) const override;
};

struct LiveDbResult
{
	std::vector<Entry> entries;
	ReaderState state = ReaderState::NotFound;
	std::string detail;
	std::string source;
	int64_t writtenAt = 0;
	bool readFromCopy = false;
};

/** Read one Live database file. Never writes to it. */
LiveDbResult readLiveDb (const std::string& dbPath);

/** Locate Live-plugins-*.db (newest mtime) in the platform's Live Database folder. */
std::string findLiveDatabase (const ReaderContext& context);

/** Copy a SQLite database plus its -wal/-shm siblings into a private temp folder.
 *  Returns the copied main database path, empty on failure. */
std::string copyDatabaseForReading (const std::string& dbPath);

} // namespace crate::registry
