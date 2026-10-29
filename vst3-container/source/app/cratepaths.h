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

/** %APPDATA%\Crate\folders.txt — the plugin folders the user added themselves, one
 *  absolute path per line, '#' starting a comment.
 *
 *  Plain text is deliberate. Standard VST3 folders cover a stock install, but VST2
 *  has no standard location at all and plenty of people keep a library somewhere of
 *  their own; when a plugin does not show up, this is the file they need to look at
 *  and fix. Kept next to settings.txt, never inside a host's own directory. */
std::string pluginFoldersPath ();

/** Folder lines in file order, comments and blanks removed. An absent file simply
 *  means the user has added nothing. */
std::vector<std::string> userPluginFolders ();

bool writeUserPluginFolders (const std::vector<std::string>& folders);

/** Append one folder unless it is already listed (compared case-insensitively on a
 *  normalised path). Returns true when the list changed. */
bool addUserPluginFolder (const std::string& folder);

/** Drop every line matching `folder`. Returns true when the list changed. */
bool removeUserPluginFolder (const std::string& folder);

/** Ensure the data directory and snapshot cache exist. Message thread only. */
bool ensureDataDirectories ();

} // namespace crate::app
