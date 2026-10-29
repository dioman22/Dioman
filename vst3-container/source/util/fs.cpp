#include "fs.h"

#include "stringutil.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace fsys = std::filesystem;

namespace crate::fs {

namespace {
fsys::path toPath (const std::string& p)
{
#if defined(_WIN32)
	return fsys::u8path (p);
#else
	return fsys::path (p);
#endif
}

std::string fromPath (const fsys::path& p)
{
#if defined(_WIN32)
	auto u8 = p.u8string ();
	return std::string (u8.begin (), u8.end ());
#else
	return p.string ();
#endif
}

/** A bundle is a plugin, not a folder to walk into. `.clap` is a single file on
 *  Windows and a bundle on other platforms; both are leaves either way. */
bool isPluginBundle (const std::string& name)
{
	auto lower = toLower (name);
	return endsWith (lower, ".vst3") || endsWith (lower, ".clap");
}
} // namespace

bool fileExists (const std::string& path)
{
	std::error_code ec;
	return fsys::is_regular_file (toPath (path), ec);
}

bool directoryExists (const std::string& path)
{
	std::error_code ec;
	return fsys::is_directory (toPath (path), ec);
}

bool createDirectories (const std::string& path)
{
	if (path.empty ())
		return false;
	std::error_code ec;
	fsys::create_directories (toPath (path), ec);
	return !ec;
}

bool writeFile (const std::string& path, const void* data, size_t size)
{
	auto parent = parentPath (path);
	if (!parent.empty ())
		createDirectories (parent);
	std::ofstream out (toPath (path), std::ios::binary | std::ios::trunc);
	if (!out)
		return false;
	out.write (static_cast<const char*> (data), static_cast<std::streamsize> (size));
	return static_cast<bool> (out);
}

bool writeFile (const std::string& path, const std::string& data)
{
	return writeFile (path, data.data (), data.size ());
}

bool readFile (const std::string& path, std::string& out)
{
	std::ifstream in (toPath (path), std::ios::binary);
	if (!in)
		return false;
	out.assign (std::istreambuf_iterator<char> (in), std::istreambuf_iterator<char> ());
	return true;
}

bool readFileBytes (const std::string& path, std::vector<uint8_t>& out)
{
	std::ifstream in (toPath (path), std::ios::binary);
	if (!in)
		return false;
	out.assign (std::istreambuf_iterator<char> (in), std::istreambuf_iterator<char> ());
	return true;
}

bool removeFile (const std::string& path)
{
	std::error_code ec;
	return fsys::remove (toPath (path), ec);
}

int64_t modifiedTime (const std::string& path)
{
	std::error_code ec;
	auto ft = fsys::last_write_time (toPath (path), ec);
	if (ec)
		return 0;
	auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration> (
	    ft - fsys::file_time_type::clock::now () + std::chrono::system_clock::now ());
	return static_cast<int64_t> (
	    std::chrono::duration_cast<std::chrono::seconds> (sys.time_since_epoch ()).count ());
}


std::vector<DirEntry> listDirectory (const std::string& path)
{
	std::vector<DirEntry> result;
	std::error_code ec;
	fsys::directory_iterator it (toPath (path), ec), end;
	if (ec)
		return result;
	for (; it != end; it.increment (ec))
	{
		if (ec)
			break;
		DirEntry entry;
		entry.path = fromPath (it->path ());
		entry.name = fromPath (it->path ().filename ());
		std::error_code typeEc;
		entry.isDirectory = it->is_directory (typeEc);
		result.push_back (std::move (entry));
	}
	std::sort (result.begin (), result.end (), [] (const DirEntry& a, const DirEntry& b) {
		return a.name < b.name;
	});
	return result;
}

std::vector<DirEntry> listDirectoryIfExists (const std::string& path)
{
	if (!directoryExists (path))
		return {};
	return listDirectory (path);
}

std::vector<DirEntry> listDirectoryRecursive (const std::string& path, int maxDepth)
{
	std::vector<DirEntry> result;
	std::error_code ec;
	if (!fsys::is_directory (toPath (path), ec))
		return result;

	// Directories we may not enter are skipped rather than failing the walk: a
	// plugins folder lives under Program Files, and one unreadable vendor folder
	// must not hide the other thousand plugins.
	fsys::recursive_directory_iterator it (toPath (path),
	                                      fsys::directory_options::skip_permission_denied, ec);
	if (ec)
		return result;
	fsys::recursive_directory_iterator end;
	for (; it != end; it.increment (ec))
	{
		if (ec)
			break; // the iterator is not usable after a failed increment

		if (it.depth () > maxDepth)
		{
			it.disable_recursion_pending ();
			continue;
		}

		DirEntry entry;
		entry.path = fromPath (it->path ());
		entry.name = fromPath (it->path ().filename ());
		std::error_code typeEc;
		entry.isDirectory = it->is_directory (typeEc);
		if (entry.isDirectory && isPluginBundle (entry.name))
			it.disable_recursion_pending ();
		result.push_back (std::move (entry));
	}

	std::sort (result.begin (), result.end (), [] (const DirEntry& a, const DirEntry& b) {
		return a.path < b.path;
	});
	return result;
}

std::string parentPath (const std::string& path)
{
	return fromPath (toPath (path).parent_path ());
}

std::string fileName (const std::string& path)
{
	return fromPath (toPath (path).filename ());
}

std::string stem (const std::string& path)
{
	return fromPath (toPath (path).stem ());
}

std::string extension (const std::string& path)
{
	return fromPath (toPath (path).extension ());
}

std::string join (const std::string& a, const std::string& b)
{
	if (a.empty ())
		return b;
	return fromPath (toPath (a) / toPath (b));
}

std::string absolute (const std::string& path)
{
	std::error_code ec;
	auto p = fsys::absolute (toPath (path), ec);
	return ec ? path : fromPath (p);
}

std::string normalized (const std::string& path)
{
	auto p = toPath (path).lexically_normal ();
	auto s = fromPath (p);
	if (s.size () > 1 && (s.back () == '/' || s.back () == '\\'))
		s.pop_back ();
	return s;
}

std::string environmentVariable (const char* name)
{
	const char* v = std::getenv (name);
	return v ? std::string (v) : std::string ();
}

std::string homeDirectory ()
{
#if defined(_WIN32)
	auto profile = environmentVariable ("USERPROFILE");
	if (!profile.empty ())
		return profile;
#else
	auto home = environmentVariable ("HOME");
	if (!home.empty ())
		return home;
#endif
	std::error_code ec;
	return fromPath (fsys::current_path (ec));
}

std::string temporaryDirectory ()
{
	std::error_code ec;
	auto p = fsys::temp_directory_path (ec);
	return ec ? std::string ("/tmp") : fromPath (p);
}

std::string currentExecutablePath ()
{
#if defined(_WIN32)
	return std::string ();
#else
	std::error_code ec;
	auto p = fsys::read_symlink ("/proc/self/exe", ec);
	if (!ec)
		return fromPath (p);
	return std::string ();
#endif
}

std::string applicationDataDirectory ()
{
#if defined(_WIN32)
	auto appData = environmentVariable ("APPDATA");
	if (!appData.empty ())
		return join (appData, "Crate");
	return join (homeDirectory (), "AppData\\Roaming\\Crate");
#elif defined(__APPLE__)
	return join (homeDirectory (), "Library/Application Support/Crate");
#else
	auto xdg = environmentVariable ("XDG_CONFIG_HOME");
	if (!xdg.empty ())
		return join (xdg, "Crate");
	return join (homeDirectory (), ".config/Crate");
#endif
}

std::string displayPath (const std::string& path)
{
	return path;
}

std::vector<std::string> standardVST3Folders ()
{
	std::vector<std::string> folders;
#if defined(_WIN32)
	auto programFiles = environmentVariable ("ProgramFiles");
	if (!programFiles.empty ())
		folders.push_back (join (programFiles, "Common Files\\VST3"));
	// 32 bit VST3 bundles live in the sibling folder. They are listed so that a
	// card can say "this is a 32 bit bundle" instead of vanishing from the grid.
	auto programFilesX86 = environmentVariable ("ProgramFiles(x86)");
	if (!programFilesX86.empty ())
		folders.push_back (join (programFilesX86, "Common Files\\VST3"));
	auto localAppData = environmentVariable ("LOCALAPPDATA");
	if (!localAppData.empty ())
		folders.push_back (join (localAppData, "Programs\\Common\\VST3"));
#elif defined(__APPLE__)
	folders.push_back (join (homeDirectory (), "Library/Audio/Plug-Ins/VST3"));
	folders.push_back ("/Library/Audio/Plug-Ins/VST3");
#else
	folders.push_back (join (homeDirectory (), ".vst3"));
	folders.push_back ("/usr/lib/vst3");
	folders.push_back ("/usr/local/lib/vst3");
#endif
	return folders;
}

} // namespace crate::fs
