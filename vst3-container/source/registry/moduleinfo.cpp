#include "moduleinfo.h"

#include "../json/json.h"
#include "../util/fs.h"
#include "../util/stringutil.h"

#include <cstring>

namespace crate::registry {

bool ModuleClassInfo::isInstrument () const
{
	for (const auto& sub : subCategories)
	{
		if (sub == "Instrument" || startsWith (sub, "Instrument|"))
			return true;
	}
	return category == "Instrument";
}

const ModuleClassInfo* ModuleInfo::audioModuleClass () const
{
	for (const auto& cls : classes)
	{
		if (cls.isAudioModuleClass ())
			return &cls;
	}
	return nullptr;
}

const ModuleClassInfo* ModuleInfo::classById (const std::string& cid) const
{
	for (const auto& cls : classes)
	{
		if (iequals (cls.cid, cid))
			return &cls;
	}
	return nullptr;
}

std::string ModuleInfo::remapCid (const std::string& oldCid) const
{
	for (const auto& compat : compatibility)
	{
		for (const auto& old : compat.oldCids)
		{
			if (iequals (old, oldCid))
				return compat.newCid;
		}
	}
	return {};
}

std::string moduleInfoPath (const std::string& bundlePath)
{
	if (bundlePath.empty ())
		return {};
	std::vector<std::string> candidates;
	candidates.push_back (fs::join (bundlePath, "Contents/Resources/moduleinfo.json"));
	candidates.push_back (fs::join (bundlePath, "Contents/moduleinfo.json"));
	candidates.push_back (bundlePath + ".moduleinfo.json");
	for (const auto& candidate : candidates)
	{
		if (fs::fileExists (candidate))
			return candidate;
	}
	return {};
}

namespace {

std::vector<std::string> stringArray (const json::Value* value)
{
	std::vector<std::string> out;
	if (!value || !value->isArray ())
		return out;
	for (const auto& item : value->array ())
	{
		if (item.isString ())
			out.push_back (item.string ());
	}
	return out;
}

} // namespace

bool parseModuleInfo (const std::string& text, ModuleInfo& info, std::string& error)
{
	error.clear ();
	auto root = json::parse (text, error);
	if (!error.empty ())
		return false;
	if (!root.isObject ())
	{
		error = "moduleinfo root is not an object";
		return false;
	}
	info = ModuleInfo ();
	info.name = root.getString ("Name");
	info.version = root.getString ("Version");
	if (auto factory = root.find ("Factory Info"))
		info.factoryVendor = factory->getString ("Vendor");

	if (auto classes = root.getArray ("Classes"))
	{
		for (const auto& item : classes->array ())
		{
			if (!item.isObject ())
				continue;
			ModuleClassInfo cls;
			cls.cid = item.getString ("CID");
			cls.category = item.getString ("Category");
			cls.name = item.getString ("Name");
			cls.vendor = item.getString ("Vendor");
			cls.version = item.getString ("Version");
			cls.sdkVersion = item.getString ("SDKVersion");
			cls.subCategories = stringArray (item.getArray ("Sub Categories"));
			cls.cardinality = static_cast<int64_t> (item.getNumber ("Cardinality", 0x7FFFFFFF));
			cls.flags = static_cast<uint32_t> (item.getNumber ("Class Flags", 0));
			if (auto snapshots = item.getArray ("Snapshots"))
			{
				for (const auto& snap : snapshots->array ())
				{
					Snapshot image;
					image.scaleFactor = snap.getNumber ("Scale Factor", 1.0);
					image.path = snap.getString ("Path");
					image.base64 = snap.getString ("Base64");
					if (!image.path.empty () || !image.base64.empty ())
						cls.snapshots.push_back (std::move (image));
				}
			}
			if (!cls.cid.empty ())
				info.classes.push_back (std::move (cls));
		}
	}

	if (auto compat = root.getArray ("Compatibility"))
	{
		for (const auto& item : compat->array ())
		{
			Compatibility entry;
			entry.newCid = item.getString ("New");
			entry.oldCids = stringArray (item.getArray ("Old"));
			if (!entry.newCid.empty () && !entry.oldCids.empty ())
				info.compatibility.push_back (std::move (entry));
		}
	}

	if (info.classes.empty ())
	{
		error = "moduleinfo contains no classes";
		return false;
	}
	return true;
}

bool readModuleInfo (const std::string& bundlePath, ModuleInfo& info, std::string& error)
{
	auto path = moduleInfoPath (bundlePath);
	if (path.empty ())
	{
		error = "no moduleinfo.json in bundle";
		return false;
	}
	std::string text;
	if (!fs::readFile (path, text))
	{
		error = "moduleinfo.json is unreadable";
		return false;
	}
	return parseModuleInfo (text, info, error);
}

std::string snapshotPath (const std::string& bundlePath, const Snapshot& snapshot)
{
	if (snapshot.path.empty ())
		return {};
	std::string relative = snapshot.path;
	if (relative.find ('\\') != std::string::npos)
		relative = replaceAll (relative, "\\", "/");
	if (startsWith (relative, "/") || (relative.size () > 1 && relative[1] == ':'))
	{
		// Absolute path as written by some third-party moduleinfo generators.
		if (fs::fileExists (relative))
			return relative;
		relative = fs::fileName (relative);
	}
	const std::vector<std::string> bases = {fs::join (bundlePath, "Contents/Resources"),
	                                        fs::join (bundlePath, "Contents"),
	                                        bundlePath};
	for (const auto& base : bases)
	{
		auto candidate = fs::join (base, relative);
		if (fs::fileExists (candidate))
			return candidate;
		// Snapshots live either under Resources/<path> or Resources/Snapshots/<file>.
		candidate = fs::join (base, "Snapshots/" + fs::fileName (relative));
		if (fs::fileExists (candidate))
			return candidate;
	}
	return {};
}

} // namespace crate::registry
