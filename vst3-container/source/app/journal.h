// Crash journal: a tiny append-only log written *before* and *after* every call that
// executes foreign plugin code. If the host dies between the two, the next instance
// can name the suspect instead of silently loading it again.
#pragma once

#include <cstdint>
#include <string>

namespace crate::app {

struct JournalRecord
{
	int64_t timestamp = 0;
	std::string step; ///< load_begin / load_end / editor_begin / editor_end ...
	std::string uid;
	std::string name;
	std::string path;
};

struct DirtySuspect
{
	bool found = false;
	JournalRecord record;
};

/** Parse the journal and report an unfinished step, if any. */
DirtySuspect inspectJournal (const std::string& path, std::string& error);

class Journal
{
public:
	explicit Journal (std::string path);

	/** Inspect once at instance start, then mark the session as started. */
	DirtySuspect startSession ();

	void begin (const char* step, const std::string& uid, const std::string& name,
	            const std::string& path);
	void end (const char* step);

	const std::string& path () const { return path_; }

private:
	void append (const JournalRecord& record);
	std::string path_;
};

/** Escape a field for the pipe-separated journal format (also used by the blacklist). */
std::string escapeField (const std::string& value);
std::string unescapeField (const std::string& value);

} // namespace crate::app
