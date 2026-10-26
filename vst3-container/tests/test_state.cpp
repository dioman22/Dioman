#include "../source/state/containerstate.h"
#include "../source/util/stringutil.h"
#include "test_framework.h"

#include <cstring>

using namespace crate;

namespace {
void putU32 (std::vector<uint8_t>& data, uint32_t v)
{
	for (int i = 0; i < 4; ++i)
		data.push_back (static_cast<uint8_t> ((v >> (8 * i)) & 0xFF));
}
void putString (std::vector<uint8_t>& data, const std::string& s)
{
	putU32 (data, static_cast<uint32_t> (s.size ()));
	data.insert (data.end (), s.begin (), s.end ());
}
void putBlob (std::vector<uint8_t>& data, const std::vector<uint8_t>& blob)
{
	putU32 (data, static_cast<uint32_t> (blob.size ()));
	data.insert (data.end (), blob.begin (), blob.end ());
}
void putFloat (std::vector<uint8_t>& data, float f)
{
	uint32_t bits = 0;
	std::memcpy (&bits, &f, sizeof (bits));
	putU32 (data, bits);
}

/** Build a schema-v1 blob exactly as the first released build wrote it. */
std::vector<uint8_t> makeV1State ()
{
	std::vector<uint8_t> data;
	const char magic[8] = {'C', 'R', 'A', 'T', 'E', 'S', 'T', '1'};
	data.insert (data.end (), magic, magic + 8);
	putU32 (data, 1); // schema version
	putU32 (data, 2); // two slots, v1 had no flags/ui block
	putString (data, "00112233445566778899AABBCCDDEEFF");
	putString (data, "/Library/Audio/Plug-Ins/VST3/Old.vst3");
	putString (data, "Old Plugin");
	putString (data, "Old Vendor");
	putString (data, "1.2.3");
	data.push_back (1); // bypass
	putFloat (data, 0.25f);
	putBlob (data, {1, 2, 3, 4});
	putString (data, "");
	putString (data, "");
	putString (data, "");
	putString (data, "");
	putString (data, "");
	data.push_back (0);
	putFloat (data, 1.0f);
	putBlob (data, {});
	putU32 (data, static_cast<uint32_t> (fnv1a64 (
	                  std::string_view (reinterpret_cast<const char*> (data.data ()), data.size ()))));
	return data;
}
} // namespace

CRATE_TEST (state_roundtrips_with_hosted_blobs)
{
	state::ContainerState in;
	in.slotCount = 3;
	in.safeMode = true;
	in.indexSchemaVersion = 2;
	in.ui.gridEditorSplit = 420;
	in.ui.windowWidth = 980;
	in.ui.windowHeight = 620;
	in.normalize ();
	in.slots[0].uid = "BD58B550F9E5634E9D2EFF39EA0927B1";
	in.slots[0].bundlePath = "/Library/Audio/Plug-Ins/VST3/Pro-Q 4.vst3";
	in.slots[0].name = "Pro-Q 4";
	in.slots[0].vendor = "FabFilter";
	in.slots[0].version = "4.01";
	in.slots[0].mix = 0.5f;
	in.slots[0].componentState = {9, 8, 7};
	in.slots[0].controllerState = {6, 5};
	in.slots[1].bypass = true;
	in.slots[2].uid = "00112233445566778899AABBCCDDEEFF";

	std::vector<uint8_t> bytes;
	std::string error;
	CHECK (state::encode (in, bytes, error));
	CHECK_MSG (error.empty (), error);
	CHECK (bytes.size () > 20);

	state::ContainerState out;
	CHECK (state::decode (bytes.data (), bytes.size (), out, error) == state::DecodeStatus::Ok);
	CHECK_MSG (error.empty (), error);
	CHECK_EQ (out.schemaVersion, state::kCurrentSchemaVersion);
	CHECK_EQ (out.slotCount, uint32_t (3));
	CHECK (out.safeMode);
	CHECK_EQ (out.ui.gridEditorSplit, int32_t (420));
	CHECK_EQ (out.ui.windowWidth, int32_t (980));
	CHECK_EQ (out.slots[0].name, std::string ("Pro-Q 4"));
	CHECK_EQ (out.slots[0].mix, 0.5f);
	CHECK (out.slots[0].componentState == std::vector<uint8_t> ({9, 8, 7}));
	CHECK (out.slots[0].controllerState == std::vector<uint8_t> ({6, 5}));
	CHECK (out.slots[1].bypass);
	CHECK (out.slots[2].uid == "00112233445566778899AABBCCDDEEFF");
}

CRATE_TEST (state_reads_v1_and_migrates_defaults)
{
	auto v1 = makeV1State ();
	state::ContainerState out;
	std::string error;
	CHECK (state::decode (v1.data (), v1.size (), out, error) == state::DecodeStatus::Ok);
	CHECK_MSG (error.empty (), error);
	CHECK_EQ (out.schemaVersion, uint32_t (1));
	CHECK_EQ (out.slotCount, uint32_t (2));
	CHECK_EQ (out.slots[0].name, std::string ("Old Plugin"));
	CHECK (out.slots[0].bypass);
	CHECK (out.slots[0].componentState.size () == 4);
	CHECK (out.slots[0].controllerState.empty ());
	CHECK (!out.safeMode);
	out.normalize ();
	CHECK_EQ (out.slots.size (), size_t (2));
}

CRATE_TEST (state_rejects_corruption_and_future_versions)
{
	state::ContainerState in;
	in.slots[0].name = "X";
	std::vector<uint8_t> bytes;
	std::string error;
	CHECK (state::encode (in, bytes, error));

	auto truncated = bytes;
	truncated.resize (bytes.size () - 3);
	state::ContainerState out;
	CHECK (state::decode (truncated.data (), truncated.size (), out, error) ==
	       state::DecodeStatus::Corrupt);

	auto modified = bytes;
	modified[20] ^= 0xFF;
	CHECK (state::decode (modified.data (), modified.size (), out, error) ==
	       state::DecodeStatus::Corrupt);

	auto future = bytes;
	future[8] = 99; // schema version byte
	uint32_t sum = static_cast<uint32_t> (fnv1a64 (std::string_view (
	    reinterpret_cast<const char*> (future.data ()), future.size () - 4)));
	future[future.size () - 4] = static_cast<uint8_t> (sum & 0xFF);
	future[future.size () - 3] = static_cast<uint8_t> ((sum >> 8) & 0xFF);
	future[future.size () - 2] = static_cast<uint8_t> ((sum >> 16) & 0xFF);
	future[future.size () - 1] = static_cast<uint8_t> ((sum >> 24) & 0xFF);
	CHECK (state::decode (future.data (), future.size (), out, error) ==
	       state::DecodeStatus::FutureVersion);

	CHECK (state::decode (nullptr, 0, out, error) == state::DecodeStatus::Empty);
	std::string junk = "not a crate state";
	CHECK (state::decode (reinterpret_cast<const uint8_t*> (junk.data ()), junk.size (), out,
	                      error) == state::DecodeStatus::Corrupt);
}

CRATE_TEST (state_clamps_out_of_range_values)
{
	state::ContainerState in;
	in.slotCount = 0; // normalize to 1
	in.slots[0].mix = 5.0f;
	std::vector<uint8_t> bytes;
	std::string error;
	CHECK (state::encode (in, bytes, error));
	state::ContainerState out;
	CHECK (state::decode (bytes.data (), bytes.size (), out, error) == state::DecodeStatus::Ok);
	CHECK_EQ (out.slots.size (), size_t (1));
	CHECK_EQ (out.slots[0].mix, 1.0f);
}
