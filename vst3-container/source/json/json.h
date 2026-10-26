// Minimal JSON / JSON5 reader for VST3 moduleinfo.json.
//
// Why not a dependency: moduleinfo.json is JSON5 (comments, trailing commas) and the
// plugin must parse it on the message thread only, with no exceptions crossing any
// boundary. This parser accepts the subset the SDK emits plus the usual JSON5
// conveniences, rejects malformed input with a line/column message, and never throws.
#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace crate::json {

class Value
{
public:
	enum class Type
	{
		Null,
		Bool,
		Number,
		String,
		Array,
		Object
	};

	Value () = default;

	Type type () const { return type_; }
	bool isNull () const { return type_ == Type::Null; }
	bool isObject () const { return type_ == Type::Object; }
	bool isArray () const { return type_ == Type::Array; }
	bool isString () const { return type_ == Type::String; }
	bool isNumber () const { return type_ == Type::Number; }
	bool isBool () const { return type_ == Type::Bool; }

	const std::vector<Value>& array () const { return array_; }
	const std::string& string () const { return string_; }
	double number () const { return number_; }
	bool boolean () const { return bool_; }

	/** Object lookup by exact key. Returns nullptr when absent or not an object. */
	const Value* find (std::string_view key) const;

	/** Convenience accessors with defaults; never throw. */
	std::string getString (std::string_view key, std::string defaultValue = {}) const;
	double getNumber (std::string_view key, double defaultValue = 0.0) const;
	bool getBool (std::string_view key, bool defaultValue = false) const;
	const Value* getArray (std::string_view key) const;

	/** A JSON-ish dump used in errors and tests (numbers are not round-trip precise). */
	std::string toJson () const;

private:
	friend class Parser;
	Type type_ = Type::Null;
	bool bool_ = false;
	double number_ = 0.0;
	std::string string_;
	std::vector<Value> array_;
	std::vector<std::pair<std::string, Value>> object_;
};

/** Parse UTF-8 text. On failure returns a Null value and fills `error`. */
Value parse (std::string_view text, std::string& error);

/** Parse and ignore the error text. */
Value parse (std::string_view text);

/** Strip // and slash-star comments and trailing commas from JSON5-ish text. */
std::string stripJson5 (std::string_view text);

} // namespace crate::json
