#pragma once

#include "../source/util/fs.h"
#include "../source/util/stringutil.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#ifndef CRATE_FIXTURE_DIR
// When no build-system override is given, resolve the fixture directory relative to
// this header. Works both for in-tree compiles and absolute paths from CMake.
#define CRATE_FIXTURE_DIR crate::fs::parentPath (__FILE__) + "/fixtures"
#endif

namespace crate::test {

inline std::string fixture (const std::string& relative)
{
	return fs::join (CRATE_FIXTURE_DIR, relative);
}

class TempDir
{
public:
	explicit TempDir (const char* prefix = "crate-test-")
	{
		static std::atomic<int> counter{0};
		auto unique = std::to_string (++counter) + "-" +
		              std::to_string (std::chrono::steady_clock::now ().time_since_epoch ().count ());
		path_ = fs::join (fs::temporaryDirectory (), std::string (prefix) + unique);
		std::filesystem::remove_all (path_);
		fs::createDirectories (path_);
	}
	~TempDir ()
	{
		std::error_code ec;
		std::filesystem::remove_all (path_, ec);
	}
	TempDir (const TempDir&) = delete;
	TempDir& operator= (const TempDir&) = delete;

	const std::string& path () const { return path_; }

	std::string write (const std::string& relative, const std::string& content) const
	{
		auto full = fs::join (path_, relative);
		fs::writeFile (full, content);
		return full;
	}

	std::string mkdir (const std::string& relative) const
	{
		auto full = fs::join (path_, relative);
		fs::createDirectories (full);
		return full;
	}

private:
	std::string path_;
};

/** Create a minimal .vst3 bundle with moduleinfo.json and an optional snapshot. */
std::string makeBundle (const TempDir& dir, const std::string& bundleName, const std::string& cid,
                        const std::string& name, const std::string& vendor,
                        const std::vector<std::string>& subCategories, bool snapshot = false);

} // namespace crate::test
