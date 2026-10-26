#include "blacklist.h"

#include "journal.h" // escapeField / unescapeField
#include "../util/fs.h"
#include "../util/stringutil.h"

#include <algorithm>
#include <chrono>

namespace crate::app {

const char* toString (BlacklistReason reason)
{
	switch (reason)
	{
		case BlacklistReason::CrashedOnLoad:
			return "crashed_on_load";
		case BlacklistReason::CrashedOnEdit:
			return "crashed_on_edit";
		case BlacklistReason::HangOnLoad:
			return "hang_on_load";
	}
	return "crashed_on_load";
}

bool parseReason (const std::string& text, BlacklistReason& out)
{
	if (text == "crashed_on_load")
	{
		out = BlacklistReason::CrashedOnLoad;
		return true;
	}
	if (text == "crashed_on_edit")
	{
		out = BlacklistReason::CrashedOnEdit;
		return true;
	}
	if (text == "hang_on_load")
	{
		out = BlacklistReason::HangOnLoad;
		return true;
	}
	return false;
}

Blacklist::Blacklist (std::string path) : path_ (std::move (path)) {}

void Blacklist::load ()
{
	entries_.clear ();
	std::string text;
	if (!fs::readFile (path_, text))
		return;
	for (const auto& raw : split (text, '\n'))
	{
		auto line = std::string (trimView (raw));
		if (line.empty () || line[0] == '#')
			continue;
		auto fields = split (line, '|');
		if (fields.size () < 3)
			continue;
		BlacklistEntry entry;
		entry.uid = unescapeField (fields[0]);
		if (!parseReason (unescapeField (fields[1]), entry.reason))
			continue;
		entry.addedAt = std::strtoll (fields[2].c_str (), nullptr, 10);
		if (fields.size () >= 4)
			entry.name = unescapeField (fields[3]);
		if (!entry.uid.empty ())
			entries_.push_back (std::move (entry));
	}
}

bool Blacklist::save (std::string& error) const
{
	error.clear ();
	std::string text = "# Crate blacklist: uid|reason|addedAt|name\n";
	for (const auto& entry : entries_)
	{
		text += escapeField (entry.uid) + "|" + toString (entry.reason) + "|" +
		        std::to_string (entry.addedAt) + "|" + escapeField (entry.name) + "\n";
	}
	if (!fs::writeFile (path_, text))
	{
		error = "could not write " + path_;
		return false;
	}
	return true;
}

bool Blacklist::contains (const std::string& uid) const
{
	return find (uid) != nullptr;
}

const BlacklistEntry* Blacklist::find (const std::string& uid) const
{
	for (const auto& entry : entries_)
	{
		if (iequals (entry.uid, uid))
			return &entry;
	}
	return nullptr;
}

void Blacklist::add (const std::string& uid, const std::string& name, BlacklistReason reason)
{
	auto existing = std::find_if (entries_.begin (), entries_.end (), [&uid] (const auto& entry) {
		return iequals (entry.uid, uid);
	});
	auto now = std::chrono::duration_cast<std::chrono::seconds> (
	               std::chrono::system_clock::now ().time_since_epoch ())
	               .count ();
	if (existing != entries_.end ())
	{
		existing->reason = reason;
		existing->addedAt = now;
		if (!name.empty ())
			existing->name = name;
		return;
	}
	BlacklistEntry entry;
	entry.uid = uid;
	entry.name = name;
	entry.reason = reason;
	entry.addedAt = now;
	entries_.push_back (std::move (entry));
}

bool Blacklist::remove (const std::string& uid)
{
	auto before = entries_.size ();
	entries_.erase (std::remove_if (entries_.begin (), entries_.end (), [&uid] (const auto& entry) {
		                return iequals (entry.uid, uid);
	                }),
	                entries_.end ());
	return entries_.size () != before;
}

} // namespace crate::app
