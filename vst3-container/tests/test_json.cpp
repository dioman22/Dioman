#include "../source/json/json.h"
#include "test_framework.h"

using namespace crate;

CRATE_TEST (json_parses_moduleinfo_like_text)
{
	std::string error;
	auto v = json::parse (R"({
		// a comment the SDK emits
		"Name": "Pro-Q 4",
		"Classes": [
			{ "CID": "BD58B550F9E5634E9D2EFF39EA0927B1", "Category": "Audio Module Class", },
		],
	})",
	                       error);
	CHECK_MSG (error.empty (), "parse error: " + error);
	CHECK_EQ (v.getString ("Name"), std::string ("Pro-Q 4"));
	auto classes = v.getArray ("Classes");
	CHECK (classes != nullptr);
	if (classes)
	{
		CHECK_EQ (classes->array ().size (), size_t (1));
		CHECK_EQ (classes->array ()[0].getString ("Category"),
		          std::string ("Audio Module Class"));
	}
}

CRATE_TEST (json_accepts_single_quotes_and_escapes)
{
	auto v = json::parse (R"({ 'a': 'line\none', "b": "\u00e9\u4e2d", "c": true, "d": null })");
	CHECK_EQ (v.getString ("a"), std::string ("line\none"));
	CHECK_EQ (v.getString ("b"), std::string ("\xc3\xa9\xe4\xb8\xad"));
	CHECK (v.getBool ("c"));
	auto d = v.find ("d");
	CHECK (d && d->isNull ());
}

CRATE_TEST (json_handles_numbers_and_nesting)
{
	auto v = json::parse ("{\"n\": -12.5e2, \"hex\": 0x10, \"arr\": [[1,2],[3]]}");
	CHECK_EQ (v.getNumber ("n"), -1250.0);
	CHECK_EQ (v.getNumber ("hex"), 16.0);
	auto arr = v.getArray ("arr");
	CHECK (arr && arr->array ().size () == 2);
	if (arr)
		CHECK_EQ (arr->array ()[1].array ()[0].number (), 3.0);
}

CRATE_TEST (json_reports_errors_with_position)
{
	std::string error;
	auto v = json::parse ("{\n  \"a\" 1\n}", error);
	CHECK (!error.empty ());
	CHECK (v.isNull ());
	CHECK (error.find ("line 2") != std::string::npos);
}

CRATE_TEST (json_strip_json5_keeps_strings_intact)
{
	auto stripped = json::stripJson5 ("a // not a comment\n\"http://x\" /* gone */ b");
	CHECK (stripped.find ("not a comment") == std::string::npos);
	CHECK (stripped.find ("http://x") != std::string::npos);
	CHECK (stripped.find ("gone") == std::string::npos);
}

CRATE_TEST (json_rejects_trailing_garbage)
{
	std::string error;
	json::parse ("{} extra", error);
	CHECK (!error.empty ());
}
