#include "journal.h"

#include "../util/fs.h"
#include "../util/stringutil.h"

#include <chrono>

namespace crate::app {

namespace {
int64_t nowSeconds ()
{
	return std::chrono::duration_cast<std::chrono::seconds> (
	           std::chrono::system_clock::now ().time_since_epoch ())
	    .count ();
}

bool endsWithBegin (const std::string& step)
{
	return endsWith (step, "_begin");
}
} // namespace

std::string escapeField (const std::string& value)
{
	std::string out;
	out.reserve (value.size ());
	for (char c : value)
	{
		switch (c)
		{
			case '|':
				out += "%7C";
				break;
			case '\\':
				out += "\\\\";
				break;
			case '\n':
				out += "\\n";
				break;
			case '\r':
				out += "\\r";
				break;
			case '%':
				out += "%25";
				break;
			default:
				out += c;
		}
	}
	return out;
}

std::string unescapeField (const std::string& value)
{
	std::string out;
	for (size_t i = 0; i < value.size (); ++i)
	{
		char c = value[i];
		if (c == '%' && i + 2 < value.size () && value.compare (i, 3, "%7C") == 0)
		{
			out += '|';
			i += 2;
		}
		else if (c == '%' && i + 2 < value.size () && value.compare (i, 3, "%25") == 0)
		{
			out += '%';
			i += 2;
		}
		else if (c == '\\' && i + 1 < value.size ())
		{
			char n = value[++i];
			if (n == 'n')
				out += '\n';
			else if (n == 'r')
				out += '\r';
			else
				out += n;
		}
		else
			out += c;
	}
	return out;
}

namespace {
JournalRecord parseLine (const std::string& line)
{
	JournalRecord record;
	auto fields = split (line, '|');
	if (fields.size () >= 1)
		record.timestamp = std::strtoll (fields[0].c_str (), nullptr, 10);
	if (fields.size () >= 2)
		record.step = unescapeField (fields[1]);
	if (fields.size () >= 3)
		record.uid = unescapeField (fields[2]);
	if (fields.size () >= 4)
		record.name = unescapeField (fields[3]);
	if (fields.size () >= 5)
		record.path = unescapeField (fields[4]);
	return record;
}
} // namespace

DirtySuspect inspectJournal (const std::string& path, std::string& error)
{
	error.clear ();
	DirtySuspect suspect;
	if (!fs::fileExists (path))
		return suspect;
	std::string text;
	if (!fs::readFile (path, text))
	{
		error = "journal exists but could not be read";
		return suspect;
	}
	// The last complete line wins. A journal caught mid-write is normal.
	JournalRecord last;
	bool haveLast = false;
	for (const auto& raw : split (text, '\n'))
	{
		auto line = std::string (trimView (raw));
		if (line.empty ())
			continue;
		last = parseLine (line);
		haveLast = true;
	}
	if (haveLast && endsWithBegin (last.step))
	{
		suspect.found = true;
		suspect.record = last;
	}
	return suspect;
}

Journal::Journal (std::string path) : path_ (std::move (path)) {}

void Journal::append (const JournalRecord& record)
{
	std::string existing;
	std::string text;
	fs::readFile (path_, existing);
	if (existing.size () > 256 * 1024)
		existing.clear (); // bounded: the journal is a tripwire, not a history
	text = std::to_string (record.timestamp) + "|" + escapeField (record.step) + "|" +
	       escapeField (record.uid) + "|" + escapeField (record.name) + "|" +
	       escapeField (record.path) + "\n";
	fs::writeFile (path_, existing + text);
}

DirtySuspect Journal::startSession ()
{
	std::string error;
	auto suspect = inspectJournal (path_, error);
	JournalRecord record;
	record.timestamp = nowSeconds ();
	record.step = "session_start";
	append (record);
	return suspect;
}

void Journal::begin (const char* step, const std::string& uid, const std::string& name,
                     const std::string& path)
{
	JournalRecord record;
	record.timestamp = nowSeconds ();
	record.step = step;
	record.uid = uid;
	record.name = name;
	record.path = path;
	append (record);
}

void Journal::end (const char* step)
{
	JournalRecord record;
	record.timestamp = nowSeconds ();
	record.step = step;
	append (record);
}

} // namespace crate::app
