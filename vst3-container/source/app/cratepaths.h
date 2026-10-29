// Per-user files owned by Crate. Nothing here is shared with a DAW, and nothing
// here is ever written by the audio thread.
#pragma once

#include <string>
#include <vector>

namespace crate::app {

/** %APPDATA%\Crate, ~/Library/Application Support/Crate, ~/.config/Crate */
std::string dataDirectory ();
std::string journalPath ();
std::string blacklistPath ();
std::string settingsPath ();
std::string snapshotCacheDirectory ();
std::string fallbackCachePath ();

/** <dataDir>\folders.txt — the plugin folders the user added themselves, one
 *  absolute path per line, '#' starting a comment.
 *
 *  Plain text is deliberate. Standard VST3 folders cover a stock install, but VST2
 *  has no standard location at all and plenty of people keep a library somewhere of
 *  their own; when a plugin does not show up, this is the file they need to look at
 *  and fix. Kept next to settings.txt, never inside a host's own directory.
 *
 *  `dataDir` empty means the real per-user directory. Callers that carry their own
 *  data directory — the test suite, a support run with an override — must pass it,
 *  or they would silently read the machine's own folder list. */
std::string pluginFoldersPath (const std::string& dataDir = std::string ());

/** Folder lines in file order, comments and blanks removed. An absent file simply
 *  means the user has added nothing. */
std::vector<std::string> userPluginFolders (const std::string& dataDir = std::string ());

bool writeUserPluginFolders (const std::vector<std::string>& folders,
                             const std::string& dataDir = std::string ());

/** Append one folder unless it is already listed (compared case-insensitively on a
 *  normalised path). Returns true when the list changed. */
bool addUserPluginFolder (const std::string& folder, const std::string& dataDir = std::string ());

/** Drop every line matching `folder`. Returns true when the list changed. */
bool removeUserPluginFolder (const std::string& folder,
                             const std::string& dataDir = std::string ());

/** Ensure the data directory and snapshot cache exist. Message thread only. */
bool ensureDataDirectories ();

} // namespace crate::app
