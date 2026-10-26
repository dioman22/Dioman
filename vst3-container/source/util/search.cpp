#include "search.h"
#include "stringutil.h"

#include <algorithm>
#include <cctype>
#include <set>

namespace crate::util {

namespace {

std::vector<std::string> tokenize (const std::string& query)
{
	std::vector<std::string> tokens;
	std::string current;
	for (char c : query)
	{
		if (std::isspace (static_cast<unsigned char> (c)))
		{
			if (!current.empty ())
				tokens.push_back (current);
			current.clear ();
		}
		else
			current += static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
	}
	if (!current.empty ())
		tokens.push_back (current);
	return tokens;
}

std::string haystack (const registry::Entry& entry)
{
	std::string text = entry.name + " " + entry.vendor + " " + entry.category + " " +
	                   entry.sdkVersion;
	for (const auto& sub : entry.subCategories)
		text += " " + sub;
	return toLower (text);
}

int scoreToken (const std::string& token, const registry::Entry& entry, const std::string& all)
{
	auto name = toLower (entry.name);
	auto vendor = toLower (entry.vendor);
	auto category = toLower (entry.category);

	int score = 0;
	if (name == token)
		score += 120;
	else if (startsWith (name, token))
		score += 90;
	else if (name.find (token) != std::string::npos)
		score += 70;
	if (startsWith (vendor, token))
		score += 40;
	else if (vendor.find (token) != std::string::npos)
		score += 25;
	if (category == token)
		score += 35;
	else if (category.find (token) != std::string::npos)
		score += 20;
	for (const auto& sub : entry.subCategories)
	{
		auto lower = toLower (sub);
		if (lower.find (token) != std::string::npos)
		{
			score += 15;
			break;
		}
	}
	if (score == 0 && all.find (token) != std::string::npos)
		score += 10;
	return score;
}

} // namespace

int matchScore (const registry::Entry& entry, const std::string& query)
{
	auto tokens = tokenize (query);
	if (tokens.empty ())
		return 1;
	auto all = haystack (entry);
	int total = 0;
	for (const auto& token : tokens)
	{
		int score = scoreToken (token, entry, all);
		if (score == 0)
			return 0; // every token must match somewhere
		total += score;
	}
	return total;
}

bool matchesFilters (const registry::Entry& entry, const Filters& filters)
{
	if (filters.vst3Only && !entry.format.empty () && entry.format != "VST3")
		return false;
	if (!filters.category.empty () && !iequals (entry.category, filters.category))
		return false;
	if (!filters.vendor.empty () && !iequals (entry.vendor, filters.vendor))
		return false;
	if (filters.instrumentsOnly && !entry.isInstrument)
		return false;
	if (filters.effectsOnly && entry.isInstrument)
		return false;
	if (filters.hideUnresolved && entry.resolution == registry::Resolution::Unresolved)
		return false;
	return true;
}

std::vector<const registry::Entry*> search (const std::vector<registry::Entry>& entries,
                                            const std::string& query, const Filters& filters)
{
	std::vector<std::pair<int, const registry::Entry*>> scored;
	for (const auto& entry : entries)
	{
		if (!matchesFilters (entry, filters))
			continue;
		int score = matchScore (entry, query);
		if (score == 0)
			continue;
		scored.emplace_back (score, &entry);
	}
	std::stable_sort (scored.begin (), scored.end (),
	                  [] (const auto& a, const auto& b) {
		                  if (a.first != b.first)
			                  return a.first > b.first;
		                  return toLower (a.second->name) < toLower (b.second->name);
	                  });
	std::vector<const registry::Entry*> result;
	result.reserve (scored.size ());
	for (const auto& item : scored)
		result.push_back (item.second);
	return result;
}

std::vector<std::string> categories (const std::vector<registry::Entry>& entries)
{
	std::set<std::string> unique;
	for (const auto& entry : entries)
	{
		if (!entry.category.empty ())
			unique.insert (entry.category);
	}
	return std::vector<std::string> (unique.begin (), unique.end ());
}

std::vector<std::string> vendors (const std::vector<registry::Entry>& entries)
{
	std::set<std::string> unique;
	for (const auto& entry : entries)
	{
		if (!entry.vendor.empty ())
			unique.insert (entry.vendor);
	}
	return std::vector<std::string> (unique.begin (), unique.end ());
}

} // namespace crate::util
