#include "json.h"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <sstream>

namespace crate::json {

const Value* Value::find (std::string_view key) const
{
	if (type_ != Type::Object)
		return nullptr;
	for (const auto& kv : object_)
	{
		if (kv.first == key)
			return &kv.second;
	}
	return nullptr;
}

std::string Value::getString (std::string_view key, std::string defaultValue) const
{
	auto v = find (key);
	return v && v->isString () ? v->string_ : std::move (defaultValue);
}

double Value::getNumber (std::string_view key, double defaultValue) const
{
	auto v = find (key);
	return v && v->isNumber () ? v->number_ : defaultValue;
}

bool Value::getBool (std::string_view key, bool defaultValue) const
{
	auto v = find (key);
	return v && v->isBool () ? v->bool_ : defaultValue;
}

const Value* Value::getArray (std::string_view key) const
{
	auto v = find (key);
	return v && v->isArray () ? v : nullptr;
}

namespace {
void dump (const Value& v, std::string& out)
{
	switch (v.type ())
	{
		case Value::Type::Null:
			out += "null";
			break;
		case Value::Type::Bool:
			out += v.boolean () ? "true" : "false";
			break;
		case Value::Type::Number:
		{
			std::ostringstream ss;
			ss << v.number ();
			out += ss.str ();
			break;
		}
		case Value::Type::String:
			out += '"';
			for (char c : v.string ())
			{
				if (c == '"' || c == '\\')
				{
					out += '\\';
					out += c;
				}
				else if (c == '\n')
					out += "\\n";
				else
					out += c;
			}
			out += '"';
			break;
		case Value::Type::Array:
		{
			out += '[';
			bool first = true;
			for (const auto& item : v.array ())
			{
				if (!first)
					out += ',';
				first = false;
				dump (item, out);
			}
			out += ']';
			break;
		}
		case Value::Type::Object:
		{
			// Value has no public object accessor; toJson is only used for debugging,
			// so reconstruct via the known keys is not possible. Emit the type only.
			out += "{...}";
			break;
		}
	}
}
} // namespace

std::string Value::toJson () const
{
	std::string out;
	dump (*this, out);
	return out;
}

class Parser
{
public:
	Parser (std::string_view text, std::string& error) : text_ (text), error_ (error) {}

	Value parseDocument ()
	{
		Value v = parseValue ();
		skipSpace ();
		if (!error_.empty ())
			return {};
		if (pos_ != text_.size ())
			fail ("unexpected trailing characters");
		return v;
	}

private:
	void fail (const std::string& message)
	{
		if (!error_.empty ())
			return;
		size_t line = 1;
		size_t column = 1;
		for (size_t i = 0; i < pos_ && i < text_.size (); ++i)
		{
			if (text_[i] == '\n')
			{
				++line;
				column = 1;
			}
			else
				++column;
		}
		std::ostringstream ss;
		ss << "line " << line << ", column " << column << ": " << message;
		error_ = ss.str ();
	}

	bool eof () const { return pos_ >= text_.size (); }
	char peek () const { return eof () ? '\0' : text_[pos_]; }
	char take () { return eof () ? '\0' : text_[pos_++]; }

	void skipSpace ()
	{
		while (!eof ())
		{
			char c = peek ();
			if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f')
			{
				++pos_;
				continue;
			}
			if (c == '/' && pos_ + 1 < text_.size () && text_[pos_ + 1] == '/')
			{
				pos_ += 2;
				while (!eof () && peek () != '\n')
					++pos_;
				continue;
			}
			if (c == '/' && pos_ + 1 < text_.size () && text_[pos_ + 1] == '*')
			{
				pos_ += 2;
				while (pos_ + 1 < text_.size () && !(text_[pos_] == '*' && text_[pos_ + 1] == '/'))
					++pos_;
				if (pos_ + 1 < text_.size ())
					pos_ += 2;
				else
				{
					pos_ = text_.size ();
					fail ("unterminated block comment");
				}
				continue;
			}
			break;
		}
	}

	Value parseValue ()
	{
		skipSpace ();
		if (eof ())
		{
			fail ("unexpected end of input");
			return {};
		}
		char c = peek ();
		switch (c)
		{
			case '{':
				return parseObject ();
			case '[':
				return parseArray ();
			case '"':
			case '\'':
				return parseString ();
			default:
				if (c == 't' || c == 'f')
					return parseBool ();
				if (c == 'n')
					return parseNull ();
				return parseNumber ();
		}
	}

	Value parseObject ()
	{
		Value v;
		v.type_ = Value::Type::Object;
		take (); // {
		skipSpace ();
		if (peek () == '}')
		{
			take ();
			return v;
		}
		while (!eof ())
		{
			skipSpace ();
			std::string key;
			if (peek () == '"' || peek () == '\'')
			{
				Value k = parseString ();
				key = k.string ();
			}
			else
			{
				while (!eof ())
				{
					char kc = peek ();
					if ((kc >= 'a' && kc <= 'z') || (kc >= 'A' && kc <= 'Z') ||
					    (kc >= '0' && kc <= '9') || kc == '_' || kc == '$' || kc == '-' ||
					    kc == '.')
					{
						key += kc;
						++pos_;
					}
					else
						break;
				}
				if (key.empty ())
				{
					fail ("expected an object key");
					return {};
				}
			}
			skipSpace ();
			if (take () != ':')
			{
				fail ("expected ':' after object key");
				return {};
			}
			Value item = parseValue ();
			if (!error_.empty ())
				return {};
			v.object_.emplace_back (std::move (key), std::move (item));
			skipSpace ();
			char sep = peek ();
			if (sep == ',')
			{
				take ();
				skipSpace ();
				if (peek () == '}')
				{
					take ();
					return v;
				}
				continue;
			}
			if (sep == '}')
			{
				take ();
				return v;
			}
			fail ("expected ',' or '}' in object");
			return {};
		}
		fail ("unterminated object");
		return {};
	}

	Value parseArray ()
	{
		Value v;
		v.type_ = Value::Type::Array;
		take (); // [
		skipSpace ();
		if (peek () == ']')
		{
			take ();
			return v;
		}
		while (!eof ())
		{
			Value item = parseValue ();
			if (!error_.empty ())
				return {};
			v.array_.push_back (std::move (item));
			skipSpace ();
			char sep = peek ();
			if (sep == ',')
			{
				take ();
				skipSpace ();
				if (peek () == ']')
				{
					take ();
					return v;
				}
				continue;
			}
			if (sep == ']')
			{
				take ();
				return v;
			}
			fail ("expected ',' or ']' in array");
			return {};
		}
		fail ("unterminated array");
		return {};
	}

	Value parseString ()
	{
		Value v;
		v.type_ = Value::Type::String;
		char quote = take ();
		std::string out;
		while (!eof ())
		{
			char c = take ();
			if (c == quote)
			{
				v.string_ = std::move (out);
				return v;
			}
			if (c == '\\')
			{
				if (eof ())
					break;
				char e = take ();
				switch (e)
				{
					case '"':
					case '\'':
					case '\\':
					case '/':
						out += e;
						break;
					case 'b':
						out += '\b';
						break;
					case 'f':
						out += '\f';
						break;
					case 'n':
						out += '\n';
						break;
					case 'r':
						out += '\r';
						break;
					case 't':
						out += '\t';
						break;
					case 'u':
					{
						uint32_t cp = 0;
						if (!readHex4 (cp))
						{
							fail ("invalid \\u escape");
							return {};
						}
						if (cp >= 0xD800 && cp <= 0xDBFF && pos_ + 6 <= text_.size () &&
						    text_[pos_] == '\\' && text_[pos_ + 1] == 'u')
						{
							size_t save = pos_;
							pos_ += 2;
							uint32_t low = 0;
							if (readHex4 (low) && low >= 0xDC00 && low <= 0xDFFF)
								cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
							else
								pos_ = save;
						}
						appendUtf8 (out, cp);
						break;
					}
					default:
						fail ("unknown escape sequence");
						return {};
				}
				continue;
			}
			out += c;
		}
		fail ("unterminated string");
		return {};
	}

	bool readHex4 (uint32_t& cp)
	{
		if (pos_ + 4 > text_.size ())
			return false;
		cp = 0;
		for (int i = 0; i < 4; ++i)
		{
			char c = text_[pos_++];
			int digit;
			if (c >= '0' && c <= '9')
				digit = c - '0';
			else if (c >= 'a' && c <= 'f')
				digit = c - 'a' + 10;
			else if (c >= 'A' && c <= 'F')
				digit = c - 'A' + 10;
			else
				return false;
			cp = (cp << 4) | static_cast<uint32_t> (digit);
		}
		return true;
	}

	static void appendUtf8 (std::string& out, uint32_t cp)
	{
		if (cp <= 0x7F)
			out += static_cast<char> (cp);
		else if (cp <= 0x7FF)
		{
			out += static_cast<char> (0xC0 | (cp >> 6));
			out += static_cast<char> (0x80 | (cp & 0x3F));
		}
		else if (cp <= 0xFFFF)
		{
			out += static_cast<char> (0xE0 | (cp >> 12));
			out += static_cast<char> (0x80 | ((cp >> 6) & 0x3F));
			out += static_cast<char> (0x80 | (cp & 0x3F));
		}
		else
		{
			out += static_cast<char> (0xF0 | (cp >> 18));
			out += static_cast<char> (0x80 | ((cp >> 12) & 0x3F));
			out += static_cast<char> (0x80 | ((cp >> 6) & 0x3F));
			out += static_cast<char> (0x80 | (cp & 0x3F));
		}
	}

	Value parseNumber ()
	{
		size_t start = pos_;
		if (peek () == '+' || peek () == '-')
			++pos_;
		if (text_.substr (pos_, 2) == "0x" || text_.substr (pos_, 2) == "0X")
		{
			bool negative = start < pos_ && text_[start] == '-';
			pos_ += 2;
			size_t hexStart = pos_;
			while (!eof () && std::isxdigit (static_cast<unsigned char> (peek ())))
				++pos_;
			unsigned long long raw = std::strtoull (
			    std::string (text_.substr (hexStart, pos_ - hexStart)).c_str (), nullptr, 16);
			return makeNumber (negative ? -static_cast<double> (raw) : static_cast<double> (raw));
		}
		else
		{
			while (!eof ())
			{
				char c = peek ();
				if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' ||
				    c == '-')
					++pos_;
				else
					break;
			}
		}
		std::string token (text_.substr (start, pos_ - start));
		if (token == "Infinity" || token == "+Infinity")
			return makeNumber (std::numeric_limits<double>::infinity ());
		if (token == "-Infinity")
			return makeNumber (-std::numeric_limits<double>::infinity ());
		if (token == "NaN" || token == "+NaN" || token == "-NaN")
			return makeNumber (std::numeric_limits<double>::quiet_NaN ());
		char* end = nullptr;
		double value = std::strtod (token.c_str (), &end);
		if (end == token.c_str () || token.empty ())
		{
			fail ("expected a value");
			return {};
		}
		return makeNumber (value);
	}

	static Value makeNumber (double value)
	{
		Value v;
		v.type_ = Value::Type::Number;
		v.number_ = value;
		return v;
	}

	Value parseBool ()
	{
		if (text_.substr (pos_, 4) == "true")
		{
			pos_ += 4;
			Value v;
			v.type_ = Value::Type::Bool;
			v.bool_ = true;
			return v;
		}
		if (text_.substr (pos_, 5) == "false")
		{
			pos_ += 5;
			Value v;
			v.type_ = Value::Type::Bool;
			v.bool_ = false;
			return v;
		}
		fail ("invalid literal");
		return {};
	}

	Value parseNull ()
	{
		if (text_.substr (pos_, 4) == "null")
		{
			pos_ += 4;
			return Value ();
		}
		fail ("invalid literal");
		return {};
	}

	std::string_view text_;
	size_t pos_ = 0;
	std::string& error_;
};

Value parse (std::string_view text, std::string& error)
{
	error.clear ();
	Parser parser (text, error);
	Value v = parser.parseDocument ();
	if (!error.empty ())
		return {};
	return v;
}

Value parse (std::string_view text)
{
	std::string ignored;
	return parse (text, ignored);
}

std::string stripJson5 (std::string_view text)
{
	std::string out;
	out.reserve (text.size ());
	bool inString = false;
	char quote = 0;
	for (size_t i = 0; i < text.size (); ++i)
	{
		char c = text[i];
		if (inString)
		{
			out += c;
			if (c == '\\' && i + 1 < text.size ())
			{
				out += text[++i];
			}
			else if (c == quote)
				inString = false;
			continue;
		}
		if (c == '"' || c == '\'')
		{
			inString = true;
			quote = c;
			out += c;
			continue;
		}
		if (c == '/' && i + 1 < text.size () && text[i + 1] == '/')
		{
			while (i < text.size () && text[i] != '\n')
				++i;
			if (i < text.size ())
				out += '\n';
			continue;
		}
		if (c == '/' && i + 1 < text.size () && text[i + 1] == '*')
		{
			i += 2;
			while (i + 1 < text.size () && !(text[i] == '*' && text[i + 1] == '/'))
				++i;
			++i;
			continue;
		}
		out += c;
	}
	return out;
}

} // namespace crate::json
