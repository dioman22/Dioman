#include "containerstate.h"

#include "../util/stringutil.h"

#include <cstring>

namespace crate::state {

namespace {

constexpr char kMagic[8] = {'C', 'R', 'A', 'T', 'E', 'S', 'T', '2'};
constexpr char kMagicV1[8] = {'C', 'R', 'A', 'T', 'E', 'S', 'T', '1'};

class Writer
{
public:
	void u8 (uint8_t v) { data_.push_back (v); }
	void u32 (uint32_t v)
	{
		for (int i = 0; i < 4; ++i)
			data_.push_back (static_cast<uint8_t> ((v >> (8 * i)) & 0xFF));
	}
	void i32 (int32_t v) { u32 (static_cast<uint32_t> (v)); }
	void f32 (float v)
	{
		uint32_t bits = 0;
		std::memcpy (&bits, &v, sizeof (bits));
		u32 (bits);
	}
	void bytes (const void* data, size_t size)
	{
		auto p = static_cast<const uint8_t*> (data);
		data_.insert (data_.end (), p, p + size);
	}
	void string (const std::string& s)
	{
		u32 (static_cast<uint32_t> (s.size ()));
		bytes (s.data (), s.size ());
	}
	void blob (const std::vector<uint8_t>& b)
	{
		u32 (static_cast<uint32_t> (b.size ()));
		bytes (b.data (), b.size ());
	}
	std::vector<uint8_t>& data () { return data_; }

private:
	std::vector<uint8_t> data_;
};

class Reader
{
public:
	Reader (const uint8_t* data, size_t size) : data_ (data), size_ (size) {}

	bool u8 (uint8_t& v)
	{
		if (!need (1))
			return false;
		v = data_[pos_++];
		return true;
	}
	bool u32 (uint32_t& v)
	{
		if (!need (4))
			return false;
		v = static_cast<uint32_t> (data_[pos_]) | (static_cast<uint32_t> (data_[pos_ + 1]) << 8) |
		    (static_cast<uint32_t> (data_[pos_ + 2]) << 16) |
		    (static_cast<uint32_t> (data_[pos_ + 3]) << 24);
		pos_ += 4;
		return true;
	}
	bool i32 (int32_t& v)
	{
		uint32_t raw = 0;
		if (!u32 (raw))
			return false;
		v = static_cast<int32_t> (raw);
		return true;
	}
	bool f32 (float& v)
	{
		uint32_t raw = 0;
		if (!u32 (raw))
			return false;
		std::memcpy (&v, &raw, sizeof (v));
		return true;
	}
	bool string (std::string& s, size_t maxLength = 1024 * 1024)
	{
		uint32_t length = 0;
		if (!u32 (length) || length > maxLength || !need (length))
			return false;
		s.assign (reinterpret_cast<const char*> (data_ + pos_), length);
		pos_ += length;
		return true;
	}
	bool blob (std::vector<uint8_t>& b, size_t maxLength = kMaxBlobBytes)
	{
		uint32_t length = 0;
		if (!u32 (length) || length > maxLength || !need (length))
			return false;
		b.assign (data_ + pos_, data_ + pos_ + length);
		pos_ += length;
		return true;
	}
	bool skip (size_t count)
	{
		if (!need (count))
			return false;
		pos_ += count;
		return true;
	}
	size_t remaining () const { return size_ - pos_; }
	size_t position () const { return pos_; }

private:
	bool need (size_t count) const { return pos_ + count <= size_; }
	const uint8_t* data_;
	size_t size_;
	size_t pos_ = 0;
};

uint32_t checksum (const std::vector<uint8_t>& data)
{
	return static_cast<uint32_t> (fnv1a64 (
	    std::string_view (reinterpret_cast<const char*> (data.data ()), data.size ())));
}

void clampSlotValues (SlotState& slot)
{
	if (slot.mix < 0.0f)
		slot.mix = 0.0f;
	if (slot.mix > 1.0f)
		slot.mix = 1.0f;
	if (slot.uid.size () > 64)
		slot.uid.resize (64);
	if (slot.bundlePath.size () > 32768)
		slot.bundlePath.resize (32768);
}

} // namespace


ContainerState::ContainerState ()
{
	slots.resize (slotCount);
}

void ContainerState::normalize ()
{
	if (slotCount > 64)
		slotCount = 64;
	if (slotCount < 1)
		slotCount = 1;
	slots.resize (slotCount);
	for (auto& slot : slots)
		clampSlotValues (slot);
	if (ui.gridEditorSplit < -1)
		ui.gridEditorSplit = -1;
	if (ui.windowWidth < 0)
		ui.windowWidth = 0;
	if (ui.windowHeight < 0)
		ui.windowHeight = 0;
}

bool ContainerState::empty () const
{
	for (const auto& slot : slots)
	{
		if (!slot.isEmpty ())
			return false;
	}
	return true;
}

const char* toString (DecodeStatus status)
{
	switch (status)
	{
		case DecodeStatus::Ok:
			return "ok";
		case DecodeStatus::Empty:
			return "empty";
		case DecodeStatus::Corrupt:
			return "corrupt";
		case DecodeStatus::FutureVersion:
			return "future_version";
	}
	return "corrupt";
}

bool encode (const ContainerState& state, std::vector<uint8_t>& out, std::string& error)
{
	error.clear ();
	ContainerState copy = state;
	copy.schemaVersion = kCurrentSchemaVersion;
	copy.normalize ();

	Writer w;
	w.bytes (kMagic, sizeof (kMagic));
	w.u32 (copy.schemaVersion);
	w.u32 (copy.slotCount);
	w.u32 (copy.indexSchemaVersion);
	w.u8 (copy.safeMode ? 1 : 0);
	w.i32 (copy.ui.gridEditorSplit);
	w.i32 (copy.ui.windowWidth);
	w.i32 (copy.ui.windowHeight);
	for (const auto& slot : copy.slots)
	{
		w.string (slot.uid);
		w.string (slot.bundlePath);
		w.string (slot.name);
		w.string (slot.vendor);
		w.string (slot.version);
		w.u8 (slot.bypass ? 1 : 0);
		w.f32 (slot.mix);
		w.blob (slot.componentState);
		w.blob (slot.controllerState);
	}
	auto sum = checksum (w.data ());
	w.u32 (sum);
	if (w.data ().size () > kMaxStateBytes)
	{
		error = "container state exceeds the 128 MB cap";
		return false;
	}
	out = std::move (w.data ());
	return true;
}

DecodeStatus decode (const uint8_t* data, size_t size, ContainerState& out, std::string& error)
{
	error.clear ();
	out = ContainerState ();
	if (!data || size == 0)
		return DecodeStatus::Empty;
	if (size < 12)
	{
		error = "state is shorter than its header";
		return DecodeStatus::Corrupt;
	}
	bool v1 = std::memcmp (data, kMagicV1, sizeof (kMagicV1)) == 0;
	bool v2 = std::memcmp (data, kMagic, sizeof (kMagic)) == 0;
	if (!v1 && !v2)
	{
		error = "state does not start with the CrateST magic";
		return DecodeStatus::Corrupt;
	}

	// Verify the trailing checksum before trusting any length field.
	std::vector<uint8_t> payload (data, data + size - 4);
	uint32_t storedSum = static_cast<uint32_t> (data[size - 4]) |
	                     (static_cast<uint32_t> (data[size - 3]) << 8) |
	                     (static_cast<uint32_t> (data[size - 2]) << 16) |
	                     (static_cast<uint32_t> (data[size - 1]) << 24);
	if (checksum (payload) != storedSum)
	{
		error = "state checksum mismatch (truncated or modified)";
		return DecodeStatus::Corrupt;
	}

	Reader r (data, size - 4);
	uint8_t magic[8] = {};
	for (auto& byte : magic)
	{
		uint8_t b = 0;
		if (!r.u8 (b))
		{
			error = "state header is truncated";
			return DecodeStatus::Corrupt;
		}
		byte = b;
	}
	uint32_t version = 0;
	if (!r.u32 (version))
	{
		error = "state has no schema version";
		return DecodeStatus::Corrupt;
	}
	if (version > kCurrentSchemaVersion)
	{
		error = "state was saved by a newer Crate (schema " + std::to_string (version) +
		        "); refusing to misread it";
		return DecodeStatus::FutureVersion;
	}
	if (version < 1)
	{
		error = "state schema version 0 is not valid";
		return DecodeStatus::Corrupt;
	}

	uint32_t slotCount = 0;
	if (!r.u32 (slotCount) || slotCount == 0 || slotCount > 64)
	{
		error = "state has an invalid slot count";
		return DecodeStatus::Corrupt;
	}
	out.slotCount = slotCount;
	out.schemaVersion = version;
	out.slots.clear ();
	out.slots.reserve (slotCount);

	if (version >= 2)
	{
		uint8_t flags = 0;
		if (!r.u32 (out.indexSchemaVersion) || !r.u8 (flags) ||
		    !r.i32 (out.ui.gridEditorSplit) || !r.i32 (out.ui.windowWidth) ||
		    !r.i32 (out.ui.windowHeight))
		{
			error = "state header is truncated";
			return DecodeStatus::Corrupt;
		}
		out.safeMode = (flags & 1) != 0;
	}

	for (uint32_t i = 0; i < slotCount; ++i)
	{
		SlotState slot;
		uint8_t bypass = 0;
		if (!r.string (slot.uid) || !r.string (slot.bundlePath) || !r.string (slot.name) ||
		    !r.string (slot.vendor) || !r.string (slot.version) || !r.u8 (bypass) ||
		    !r.f32 (slot.mix) || !r.blob (slot.componentState))
		{
			error = "state slot " + std::to_string (i + 1) + " is truncated";
			return DecodeStatus::Corrupt;
		}
		slot.bypass = bypass != 0;
		if (version >= 2)
		{
			if (!r.blob (slot.controllerState))
			{
				error = "state slot " + std::to_string (i + 1) + " is truncated";
				return DecodeStatus::Corrupt;
			}
		}
		clampSlotValues (slot);
		out.slots.push_back (std::move (slot));
	}
	if (r.remaining () != 0)
	{
		error = "state has " + std::to_string (r.remaining ()) + " unexpected trailing bytes";
		return DecodeStatus::Corrupt;
	}
	out.normalize ();
	return DecodeStatus::Ok;
}

} // namespace crate::state
