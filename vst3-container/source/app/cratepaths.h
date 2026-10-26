// Per-user files owned by Crate. Nothing here is shared with a DAW, and nothing
// here is ever written by the audio thread.
#pragma once

#include <string>

namespace crate::app {

/** %APPDATA%\Crate, ~/Library/Application Support/Crate, ~/.config/Crate */
std::string dataDirectory ();
std::string journalPath ();
std::string blacklistPath ();
std::string settingsPath ();
std::string snapshotCacheDirectory ();
std::string fallbackCachePath ();

/** Ensure the data directory and snapshot cache exist. Message thread only. */
bool ensureDataDirectories ();

} // namespace crate::app
