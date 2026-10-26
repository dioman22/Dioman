#include "cratepaths.h"

#include "../util/fs.h"

namespace crate::app {

std::string dataDirectory ()
{
	return fs::applicationDataDirectory ();
}

std::string journalPath ()
{
	return fs::join (dataDirectory (), "journal.log");
}

std::string blacklistPath ()
{
	return fs::join (dataDirectory (), "blacklist.txt");
}

std::string settingsPath ()
{
	return fs::join (dataDirectory (), "settings.txt");
}

std::string snapshotCacheDirectory ()
{
	return fs::join (dataDirectory (), "snapshots");
}

std::string fallbackCachePath ()
{
	return fs::join (dataDirectory (), "fallback-index.json");
}

bool ensureDataDirectories ()
{
	return fs::createDirectories (dataDirectory ()) &&
	       fs::createDirectories (snapshotCacheDirectory ());
}

} // namespace crate::app
