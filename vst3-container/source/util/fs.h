// Crate — filesystem helpers. Everything here runs on the message thread or the
// worker thread; nothing in this file may be called from process().
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace crate::fs {

struct DirEntry
{
	std::string name; // file name only, UTF-8
	std::string path; // full path, UTF-8
	bool isDirectory = false;
};

bool fileExists (const std::string& path);
bool directoryExists (const std::string& path);
bool createDirectories (const std::string& path);
bool writeFile (const std::string& path, const void* data, size_t size);
bool writeFile (const std::string& path, const std::string& data);
bool readFile (const std::string& path, std::string& out);
bool readFileBytes (const std::string& path, std::vector<uint8_t>& out);
bool removeFile (const std::string& path);

/** Seconds since the unix epoch, 0 when the file does not exist. */
int64_t modifiedTime (const std::string& path);

/** Immediate children only. Sorted by name, directories and files mixed. */
std::vector<DirEntry> listDirectory (const std::string& path);

/** Depth-limited recursive walk, used by the filesystem index and by bundle
 *  resolution. `maxDepth` counts levels below `path`, so 0 is exactly
 *  listDirectory(). Three rules keep the walk cheap and safe:
 *   - it never descends into a plugin bundle (`*.vst3`, `*.clap`): the Contents
 *     tree of a bundle holds resources and binaries, never more plugins;
 *   - it never follows directory symlinks or junctions, so cycles are impossible;
 *   - an unreadable subdirectory is skipped, not an error.
 *  Results are sorted by full path. */
std::vector<DirEntry> listDirectoryRecursive (const std::string& path, int maxDepth);

std::string parentPath (const std::string& path);
std::string fileName (const std::string& path);
std::string stem (const std::string& path);
std::string extension (const std::string& path);
std::string join (const std::string& a, const std::string& b);
std::string absolute (const std::string& path);
std::string normalized (const std::string& path);

std::string environmentVariable (const char* name);
std::string homeDirectory ();
std::string temporaryDirectory ();
std::string currentExecutablePath ();

/** %APPDATA%\Crate, ~/Library/Application Support/Crate, ~/.config/Crate */
std::string applicationDataDirectory ();
std::string displayPath (const std::string& path);

/** Standard VST3 folders of this platform, whether they exist or not. */
std::vector<std::string> standardVST3Folders ();

/** Join-only helper used by readers: list a folder when it exists, else empty. */
std::vector<DirEntry> listDirectoryIfExists (const std::string& path);

} // namespace crate::fs
