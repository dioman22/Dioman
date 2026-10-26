#include "stringutil.h"

#include <algorithm>
#include <cctype>

namespace crate {

namespace {
bool isSpace (char c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}
} // namespace

std::string_view trimView (std::string_view s)
{
	size_t begin = 0;
	while (begin < s.size () && isSpace (s[begin]))
		++begin;
	size_t end = s.size ();
	while (end > begin && isSpace (s[end - 1]))
		--end;
	return s.substr (begin, end - begin);
}

std::string trim (std::string s)
{
	auto v = trimView (s);
	return std::string (v);
}

std::string toLower (std::string s)
{
	std::transform (s.begin (), s.end (), s.begin (), [] (unsigned char c) {
		return static_cast<char> (std::tolower (c));
	});
	return s;
}

bool iequals (std::string_view a, std::string_view b)
{
	if (a.size () != b.size ())
		return false;
	for (size_t i = 0; i < a.size (); ++i)
	{
		if (std::tolower (static_cast<unsigned char> (a[i])) !=
		    std::tolower (static_cast<unsigned char> (b[i])))
			return false;
	}
	return true;
}

bool startsWith (std::string_view s, std::string_view prefix)
{
	return s.size () >= prefix.size () && s.compare (0, prefix.size (), prefix) == 0;
}

bool endsWith (std::string_view s, std::string_view suffix)
{
	return s.size () >= suffix.size () &&
	       s.compare (s.size () - suffix.size (), suffix.size (), suffix) == 0;
}

bool containsNoCase (std::string_view haystack, std::string_view needle)
{
	return toLower (std::string (haystack)).find (toLower (std::string (needle))) !=
	       std::string::npos;
}

std::vector<std::string> split (std::string_view s, char separator)
{
	std::vector<std::string> result;
	size_t start = 0;
	while (true)
	{
		size_t pos = s.find (separator, start);
		if (pos == std::string_view::npos)
		{
			result.emplace_back (s.substr (start));
			break;
		}
		result.emplace_back (s.substr (start, pos - start));
		start = pos + 1;
	}
	return result;
}

std::string join (const std::vector<std::string>& parts, std::string_view separator)
{
	std::string out;
	for (size_t i = 0; i < parts.size (); ++i)
	{
		if (i)
			out.append (separator);
		out.append (parts[i]);
	}
	return out;
}

std::string replaceAll (std::string s, std::string_view from, std::string_view to)
{
	if (from.empty ())
		return s;
	size_t pos = 0;
	while ((pos = s.find (from, pos)) != std::string::npos)
	{
		s.replace (pos, from.size (), to);
		pos += to.size ();
	}
	return s;
}

uint64_t fnv1a64 (std::string_view s)
{
	uint64_t hash = 1469598103934665603ull;
	for (unsigned char c : s)
	{
		hash ^= c;
		hash *= 1099511628211ull;
	}
	return hash;
}

bool base64Decode (std::string_view in, std::vector<uint8_t>& out)
{
	out.clear ();
	auto comma = in.find (',');
	if (in.substr (0, 5) == "data:" && comma != std::string_view::npos)
		in = in.substr (comma + 1);

	int value = 0;
	int bits = -8;
	for (char c : in)
	{
		if (isSpace (c) || c == '=')
			continue;
		int digit;
		if (c >= 'A' && c <= 'Z')
			digit = c - 'A';
		else if (c >= 'a' && c <= 'z')
			digit = c - 'a' + 26;
		else if (c >= '0' && c <= '9')
			digit = c - '0' + 52;
		else if (c == '+' || c == '-')
			digit = 62;
		else if (c == '/' || c == '_')
			digit = 63;
		else
			return false;

		value = (value << 6) | digit;
		bits += 6;
		if (bits >= 0)
		{
			out.push_back (static_cast<uint8_t> ((value >> bits) & 0xFF));
			bits -= 8;
		}
	}
	return true;
}

std::string imageExtensionFromBytes (const std::vector<uint8_t>& bytes)
{
	if (bytes.size () >= 8 && bytes[0] == 0x89 && bytes[1] == 'P' && bytes[2] == 'N' &&
	    bytes[3] == 'G')
		return "png";
	if (bytes.size () >= 3 && bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF)
		return "jpg";
	if (bytes.size () >= 6 && bytes[0] == 'G' && bytes[1] == 'I' && bytes[2] == 'F')
		return "gif";
	if (bytes.size () >= 12 && bytes[0] == 'R' && bytes[1] == 'I' && bytes[2] == 'F' &&
	    bytes[3] == 'F' && bytes[8] == 'W' && bytes[9] == 'E' && bytes[10] == 'B' &&
	    bytes[11] == 'P')
		return "webp";
	return {};
}

namespace {
void appendUtf8 (std::string& out, uint32_t cp)
{
	if (cp <= 0x7F)
		out.push_back (static_cast<char> (cp));
	else if (cp <= 0x7FF)
	{
		out.push_back (static_cast<char> (0xC0 | (cp >> 6)));
		out.push_back (static_cast<char> (0x80 | (cp & 0x3F)));
	}
	else if (cp <= 0xFFFF)
	{
		out.push_back (static_cast<char> (0xE0 | (cp >> 12)));
		out.push_back (static_cast<char> (0x80 | ((cp >> 6) & 0x3F)));
		out.push_back (static_cast<char> (0x80 | (cp & 0x3F)));
	}
	else
	{
		out.push_back (static_cast<char> (0xF0 | (cp >> 18)));
		out.push_back (static_cast<char> (0x80 | ((cp >> 12) & 0x3F)));
		out.push_back (static_cast<char> (0x80 | ((cp >> 6) & 0x3F)));
		out.push_back (static_cast<char> (0x80 | (cp & 0x3F)));
	}
}
} // namespace

std::u16string utf8ToUtf16 (std::string_view utf8)
{
	std::u16string out;
	out.reserve (utf8.size ());
	for (size_t i = 0; i < utf8.size ();)
	{
		unsigned char c = static_cast<unsigned char> (utf8[i]);
		uint32_t cp = 0xFFFD;
		size_t extra = 0;
		if (c < 0x80)
			cp = c;
		else if ((c & 0xE0) == 0xC0)
		{
			cp = c & 0x1F;
			extra = 1;
		}
		else if ((c & 0xF0) == 0xE0)
		{
			cp = c & 0x0F;
			extra = 2;
		}
		else if ((c & 0xF8) == 0xF0)
		{
			cp = c & 0x07;
			extra = 3;
		}
		else
		{
			out.push_back (static_cast<char16_t> (0xFFFD));
			++i;
			continue;
		}
		if (i + extra >= utf8.size ())
		{
			out.push_back (static_cast<char16_t> (0xFFFD));
			break;
		}
		bool valid = true;
		for (size_t k = 1; k <= extra; ++k)
		{
			unsigned char cc = static_cast<unsigned char> (utf8[i + k]);
			if ((cc & 0xC0) != 0x80)
			{
				valid = false;
				break;
			}
			cp = (cp << 6) | (cc & 0x3F);
		}
		i += extra + 1;
		if (!valid || cp > 0x10FFFF)
		{
			out.push_back (static_cast<char16_t> (0xFFFD));
			continue;
		}
		if (cp <= 0xFFFF)
			out.push_back (static_cast<char16_t> (cp));
		else
		{
			cp -= 0x10000;
			out.push_back (static_cast<char16_t> (0xD800 + (cp >> 10)));
			out.push_back (static_cast<char16_t> (0xDC00 + (cp & 0x3FF)));
		}
	}
	return out;
}

std::string utf16ToUtf8 (std::u16string_view utf16)
{
	std::string out;
	for (size_t i = 0; i < utf16.size (); ++i)
	{
		uint32_t cp = utf16[i];
		if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < utf16.size () && utf16[i + 1] >= 0xDC00 &&
		    utf16[i + 1] <= 0xDFFF)
		{
			cp = 0x10000 + ((cp - 0xD800) << 10) + (utf16[i + 1] - 0xDC00);
			++i;
		}
		else if (cp >= 0xD800 && cp <= 0xDFFF)
			cp = 0xFFFD;
		appendUtf8 (out, cp);
	}
	return out;
}

} // namespace crate
