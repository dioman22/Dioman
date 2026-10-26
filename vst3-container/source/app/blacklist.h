// Per-user blacklist of plugins that crashed or hung while Crate was loading or
// editing them. Never silent, never permanent without a visible reason.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace crate::app {

enum class BlacklistReason
{
	CrashedOnLoad,
	CrashedOnEdit,
	HangOnLoad
};

const char* toString (BlacklistReason reason);
bool parseReason (const std::string& text, BlacklistReason& out);

struct BlacklistEntry
{
	std::string uid;
	std::string name;
	BlacklistReason reason = BlacklistReason::CrashedOnLoad;
	int64_t addedAt = 0;
};

class Blacklist
{
public:
	explicit Blacklist (std::string path);

	void load ();
	bool save (std::string& error) const;

	bool contains (const std::string& uid) const;
	const BlacklistEntry* find (const std::string& uid) const;
	const std::vector<BlacklistEntry>& entries () const { return entries_; }

	/** Adds or updates the entry; the caller still has to save(). */
	void add (const std::string& uid, const std::string& name, BlacklistReason reason);
	bool remove (const std::string& uid);

private:
	std::string path_;
	std::vector<BlacklistEntry> entries_;
};

} // namespace crate::app
