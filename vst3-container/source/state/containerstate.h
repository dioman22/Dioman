// Versioned container state. Deliberately independent of the VST3 SDK so the format
// can be unit-tested (and migrated) without loading a plugin, and so a session saved on
// one OS can be read on another.
//
// schema v1: magic, version, slotCount, slots{uid,path,name,vendor,version,bypass,mix,componentState}
// schema v2: adds flags, indexSchema, UI layout and controllerState per slot.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace crate::state {

constexpr uint32_t kCurrentSchemaVersion = 2;
constexpr size_t kMaxStateBytes = 128u * 1024u * 1024u; // 128 MB hard cap
constexpr size_t kMaxBlobBytes = 64u * 1024u * 1024u;

struct SlotState
{
	std::string uid;        ///< 32 hex characters, empty for an empty slot
	std::string bundlePath; ///< absolute path the plugin was loaded from
	std::string name;
	std::string vendor;
	std::string version;
	bool bypass = false;
	float mix = 1.0f;
	std::vector<uint8_t> componentState;
	std::vector<uint8_t> controllerState;

	bool isEmpty () const { return uid.empty () && bundlePath.empty (); }
};

struct UiState
{
	int32_t gridEditorSplit = -1; ///< pixels, -1 = use default
	int32_t windowWidth = 0;
	int32_t windowHeight = 0;
};

struct ContainerState
{
	uint32_t schemaVersion = kCurrentSchemaVersion;
	uint32_t indexSchemaVersion = 1;
	uint32_t slotCount = 4;
	bool safeMode = false; ///< instantiated with every hosted plugin disabled
	UiState ui;
	std::vector<SlotState> slots;

	ContainerState ();
	void normalize (); ///< pads/truncates slots to slotCount and clamps values
	bool empty () const;
};

enum class DecodeStatus
{
	Ok,
	Empty,         ///< no state at all (fresh instance)
	Corrupt,       ///< magic/length/checksum failure; refuse to guess
	FutureVersion  ///< saved by a newer build; refuse to misread
};

const char* toString (DecodeStatus status);

/** Serialise. Returns false only when a limit is exceeded. */
bool encode (const ContainerState& state, std::vector<uint8_t>& out, std::string& error);

DecodeStatus decode (const uint8_t* data, size_t size, ContainerState& out, std::string& error);

} // namespace crate::state
