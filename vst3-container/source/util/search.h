// Search and filtering over the registry snapshot. Runs on the message thread only;
// the grid asks for a filtered index, never for filesystem access.
#pragma once

#include "../registry/entry.h"

#include <string>
#include <vector>

namespace crate::util {

struct Filters
{
	std::string category;  ///< exact match on Entry::category; empty = any
	std::string vendor;    ///< exact match; empty = any
	bool instrumentsOnly = false;
	bool effectsOnly = false;
	bool vst3Only = true;   ///< default: only what the container can host
	bool hideUnresolved = false;
};

/** 0 = no match. Higher is better. Tokenised, case-insensitive, prefix-weighted. */
int matchScore (const registry::Entry& entry, const std::string& query);

bool matchesFilters (const registry::Entry& entry, const Filters& filters);

/** Filtered + scored + sorted (score desc, name asc). Pointers refer to `entries`. */
std::vector<const registry::Entry*> search (const std::vector<registry::Entry>& entries,
                                            const std::string& query, const Filters& filters);

/** Distinct categories and vendors for the filter row, sorted. */
std::vector<std::string> categories (const std::vector<registry::Entry>& entries);
std::vector<std::string> vendors (const std::vector<registry::Entry>& entries);

} // namespace crate::util
