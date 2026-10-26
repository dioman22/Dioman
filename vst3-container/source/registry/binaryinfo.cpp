#include "moduleinfo.h"

#include "../util/fs.h"
#include "../util/stringutil.h"

#include <cstdint>
#include <fstream>
#include <vector>

namespace crate::registry {

namespace {

std::vector<uint8_t> readPrefix (const std::string& path, size_t count)
{
	std::vector<uint8_t> data;
	std::ifstream in (path, std::ios::binary);
	if (!in)
		return data;
	data.resize (count);
	in.read (reinterpret_cast<char*> (data.data ()), static_cast<std::streamsize> (count));
	data.resize (static_cast<size_t> (in.gcount ()));
	return data;
}

uint32_t readLE32 (const std::vector<uint8_t>& d, size_t off)
{
	if (off + 4 > d.size ())
		return 0;
	return static_cast<uint32_t> (d[off]) | (static_cast<uint32_t> (d[off + 1]) << 8) |
	       (static_cast<uint32_t> (d[off + 2]) << 16) |
	       (static_cast<uint32_t> (d[off + 3]) << 24);
}

uint32_t readBE32 (const std::vector<uint8_t>& d, size_t off)
{
	if (off + 4 > d.size ())
		return 0;
	return (static_cast<uint32_t> (d[off]) << 24) | (static_cast<uint32_t> (d[off + 1]) << 16) |
	       (static_cast<uint32_t> (d[off + 2]) << 8) | static_cast<uint32_t> (d[off + 3]);
}

uint16_t readLE16 (const std::vector<uint8_t>& d, size_t off)
{
	if (off + 2 > d.size ())
		return 0;
	return static_cast<uint16_t> (d[off] | (d[off + 1] << 8));
}

std::string binaryInBundle (const std::string& bundlePath)
{
	if (fs::fileExists (bundlePath))
		return bundlePath;
	if (!fs::directoryExists (bundlePath))
		return {};
	auto name = fs::stem (bundlePath); // "Pro-Q 4" from "Pro-Q 4.vst3"
	const std::vector<std::string> candidates = {
	    fs::join (bundlePath, "Contents/x86_64-win/" + name + ".vst3"),
	    fs::join (bundlePath, "Contents/x86_64-linux/" + name + ".so"),
	    fs::join (bundlePath, "Contents/aarch64-linux/" + name + ".so"),
	    fs::join (bundlePath, "Contents/MacOS/" + name),
	    fs::join (bundlePath, "Contents/x86_64-linux/" + name + ".vst3"),
	};
	for (const auto& candidate : candidates)
	{
		if (fs::fileExists (candidate))
			return candidate;
	}
	return {};
}

std::string machoCpu (uint32_t cputype)
{
	switch (cputype)
	{
		case 0x01000007:
			return "x86_64-mac";
		case 0x0100000C:
			return "arm64-mac";
		case 7: // CPU_TYPE_I386, 32 bit: not hostable by a 64 bit container
			return "x86-mac";
		case 12:
			return "arm-mac";
		default:
			return {};
	}
}

} // namespace

std::vector<std::string> detectArchitectures (const std::string& bundlePath)
{
	std::vector<std::string> result;
	auto binary = binaryInBundle (bundlePath);
	if (binary.empty ())
		return result;
	auto data = readPrefix (binary, 4096);
	if (data.size () < 20)
		return result;

	if (data[0] == 0x7F && data[1] == 'E' && data[2] == 'L' && data[3] == 'F')
	{
		uint16_t machine = readLE16 (data, 18);
		if (machine == 0x3E)
			result.push_back ("x86_64-linux");
		else if (machine == 0xB7)
			result.push_back ("aarch64-linux");
		else if (machine == 3)
			result.push_back ("x86-linux");
		return result;
	}

	if (data[0] == 'M' && data[1] == 'Z')
	{
		uint32_t peOffset = readLE32 (data, 0x3C);
		if (peOffset + 6 <= data.size () && data[peOffset] == 'P' && data[peOffset + 1] == 'E')
		{
			uint16_t machine = readLE16 (data, peOffset + 4);
			if (machine == 0x8664)
				result.push_back ("x86_64-win");
			else if (machine == 0xAA64)
				result.push_back ("arm64-win");
			else if (machine == 0x14C)
				result.push_back ("x86-win");
		}
		return result;
	}

	uint32_t magic = readBE32 (data, 0);
	if (magic == 0xCAFEBABE || magic == 0xCAFEBABF)
	{
		uint32_t count = readBE32 (data, 4);
		for (uint32_t i = 0; i < count && i < 16; ++i)
		{
			uint32_t cputype = readBE32 (data, 8 + static_cast<size_t> (i) * 20);
			auto name = machoCpu (cputype);
			if (!name.empty ())
				result.push_back (name);
		}
		return result;
	}

	uint32_t mle = readLE32 (data, 0);
	if (mle == 0xFEEDFACF || mle == 0xFEEDFACE)
	{
		auto name = machoCpu (readLE32 (data, 4));
		if (!name.empty ())
			result.push_back (name);
	}
	return result;
}

std::string hostArchitecture ()
{
#if defined(_WIN64) && defined(_M_ARM64)
	return "arm64-win";
#elif defined(_WIN64)
	return "x86_64-win";
#elif defined(_WIN32)
	return "x86-win";
#elif defined(__APPLE__) && defined(__aarch64__)
	return "arm64-mac";
#elif defined(__APPLE__)
	return "x86_64-mac";
#elif defined(__aarch64__)
	return "aarch64-linux";
#elif defined(__x86_64__)
	return "x86_64-linux";
#else
	return {};
#endif
}

bool isIncompatibleArchitecture (const std::vector<std::string>& architectures)
{
	if (architectures.empty ())
		return false; // unknown is not incompatible: say so instead of hiding the card
	auto host = hostArchitecture ();
	if (host.empty ())
		return false;
	for (const auto& arch : architectures)
	{
		if (iequals (arch, host))
			return false;
	}
	return true;
}

} // namespace crate::registry
