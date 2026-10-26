// Crate — VST3 plugin browser & container
// Small string helpers shared by the registry layer, the state codec and the UI.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace crate {

std::string_view trimView (std::string_view s);
std::string trim (std::string s);
std::string toLower (std::string s);
bool iequals (std::string_view a, std::string_view b);
bool startsWith (std::string_view s, std::string_view prefix);
bool endsWith (std::string_view s, std::string_view suffix);
bool containsNoCase (std::string_view haystack, std::string_view needle);
std::vector<std::string> split (std::string_view s, char separator);
std::string join (const std::vector<std::string>& parts, std::string_view separator);
std::string replaceAll (std::string s, std::string_view from, std::string_view to);

/** FNV-1a 64 bit, used for stable card colours and cache keys. */
uint64_t fnv1a64 (std::string_view s);

/** Base64 decode. Accepts whitespace, URL-safe alphabet and (optionally) data URIs. */
bool base64Decode (std::string_view in, std::vector<uint8_t>& out);

/** Detect an image container from magic bytes: "png", "jpg", "gif", "webp" or "" */
std::string imageExtensionFromBytes (const std::vector<uint8_t>& bytes);

/** Convert UTF-8 to UTF-16 for VST3 String128. Invalid sequences become U+FFFD. */
std::u16string utf8ToUtf16 (std::string_view utf8);
/** Convert UTF-16 to UTF-8. Unpaired surrogates become U+FFFD. */
std::string utf16ToUtf8 (std::u16string_view utf16);

} // namespace crate
