#include "cratecontroller.h"

#include "../state/containerstate.h"
#include "../util/stringutil.h"

#include "pluginterfaces/base/ibstream.h"
#include "public.sdk/source/vst/utility/stringconvert.h"

#include <cstring>
#include <vector>

namespace crate::vst3 {

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace VST3;

namespace {

/** A parameter that formats itself from the shared parameter map, so the DAW shows
 *  "Slot 2 Bypass: On" instead of "Bypass: 1.0000". */
class CrateParameter : public Parameter
{
public:
	CrateParameter (const TChar* title, ParamID id, double defaultPlain, int32 stepCount,
	                int32 flags)
	    : Parameter (title, id, nullptr, host::params::toNormalised (id, defaultPlain), stepCount,
	                 flags)
	{
	}

	void toString (ParamValue valueNormalized, String128 string) const SMTG_OVERRIDE
	{
		const std::string text =
		    host::params::format (static_cast<uint32_t> (info.id),
		                          host::params::toPlain (static_cast<uint32_t> (info.id),
		                                                 valueNormalized));
		if (!StringConvert::convert (text, string))
			string[0] = 0;
	}

	bool fromString (const TChar* string, ParamValue& valueNormalized) const SMTG_OVERRIDE
	{
		const std::u16string wide (reinterpret_cast<const char16_t*> (string));
		const std::string text = toLower (trim (utf16ToUtf8 (wide)));
		if (text.empty ())
			return false;
		if (text == "on" || text == "off")
		{
			valueNormalized = text == "on" ? 1.0 : 0.0;
			return true;
		}
		char* end = nullptr;
		double plain = std::strtod (text.c_str (), &end);
		if (end == text.c_str ())
			return false;
		if (text.find ('%') != std::string::npos)
			plain /= 100.0;
		valueNormalized =
		    host::params::toNormalised (static_cast<uint32_t> (info.id), plain);
		return true;
	}

	ParamValue toPlain (ParamValue valueNormalized) const SMTG_OVERRIDE
	{
		return host::params::toPlain (static_cast<uint32_t> (info.id), valueNormalized);
	}

	ParamValue toNormalized (ParamValue plainValue) const SMTG_OVERRIDE
	{
		return host::params::toNormalised (static_cast<uint32_t> (info.id), plainValue);
	}

	OBJ_METHODS (CrateParameter, Parameter)
};

/** Controller state: the selected slot plus every mix/bypass value. Small, versioned and
 *  checksummed, because a truncated controller state is as bad as a truncated session. */
constexpr char kMagic[8] = {'C', 'R', 'A', 'T', 'E', 'C', 'T', '1'};

void putU32 (std::vector<uint8_t>& out, uint32_t value)
{
	for (int i = 0; i < 4; ++i)
		out.push_back (static_cast<uint8_t> ((value >> (8 * i)) & 0xFF));
}

void putF32 (std::vector<uint8_t>& out, float value)
{
	uint32_t bits = 0;
	std::memcpy (&bits, &value, sizeof (bits));
	putU32 (out, bits);
}

uint32_t readU32 (const std::vector<uint8_t>& data, size_t offset)
{
	return static_cast<uint32_t> (data[offset]) |
	       (static_cast<uint32_t> (data[offset + 1]) << 8) |
	       (static_cast<uint32_t> (data[offset + 2]) << 16) |
	       (static_cast<uint32_t> (data[offset + 3]) << 24);
}

float readF32 (const std::vector<uint8_t>& data, size_t offset)
{
	const uint32_t bits = readU32 (data, offset);
	float value = 0.0f;
	std::memcpy (&value, &bits, sizeof (value));
	return value;
}

bool readStream (IBStream* stream, std::vector<uint8_t>& out)
{
	out.clear ();
	if (!stream)
		return false;
	int64 size = 0;
	if (stream->seek (0, IBStream::kIBSeekEnd, &size) != kResultOk || size <= 0 ||
	    size > 1024 * 1024)
		return false;
	if (stream->seek (0, IBStream::kIBSeekSet, nullptr) != kResultOk)
		return false;
	out.resize (static_cast<size_t> (size));
	int32 read = 0;
	if (stream->read (out.data (), static_cast<int32> (size), &read) != kResultOk || read <= 0)
		return false;
	out.resize (static_cast<size_t> (read));
	return true;
}

} // namespace

CrateController::CrateController ()
{
	addParameters ();
}

void CrateController::addParameters ()
{
	String128 title;
	{
		const std::string text = host::params::title (host::params::kSelectedSlotId);
		StringConvert::convert (text, title);
		parameters.addParameter (new CrateParameter (title, host::params::kSelectedSlotId, 0.0,
		                                             host::kMaxSlots - 1,
		                                             ParameterInfo::kNoFlags));
	}
	for (int32_t slot = 0; slot < host::kMaxSlots; ++slot)
	{
		for (auto kind : {host::params::SlotParam::Mix, host::params::SlotParam::Bypass})
		{
			const uint32_t id = host::params::slotId (slot, kind);
			const std::string text = host::params::title (id);
			StringConvert::convert (text, title);
			const int32 stepCount =
			    kind == host::params::SlotParam::Bypass ? 1 : 0;
			parameters.addParameter (new CrateParameter (title, id, host::params::defaultPlain (id),
			                                             stepCount,
			                                             ParameterInfo::kCanAutomate));
		}
	}
}

FUnknown* CrateController::createInstance (void*)
{
	return static_cast<IEditController*> (new CrateController ());
}

tresult PLUGIN_API CrateController::initialize (FUnknown* context)
{
	return EditController::initialize (context);
}

int32_t CrateController::selectedSlot () const
{
	return selectedSlot_;
}

void CrateController::setSelectedSlot (int32_t slot)
{
	if (slot < 0)
		slot = 0;
	if (slot >= host::kMaxSlots)
		slot = host::kMaxSlots - 1;
	selectedSlot_ = slot;
	const ParamValue value =
	    host::params::toNormalised (host::params::kSelectedSlotId, static_cast<double> (slot));
	setParamNormalized (host::params::kSelectedSlotId, value);
}

tresult PLUGIN_API CrateController::setComponentState (IBStream* stream)
{
	std::vector<uint8_t> data;
	if (!readStream (stream, data))
		return kResultFalse;

	state::ContainerState container;
	std::string error;
	if (state::decode (data.data (), data.size (), container, error) != state::DecodeStatus::Ok)
		return kResultFalse;

	const size_t slots = std::min<size_t> (container.slots.size (),
	                                       static_cast<size_t> (host::kMaxSlots));
	for (size_t i = 0; i < slots; ++i)
	{
		const state::SlotState& slot = container.slots[i];
		const int32_t index = static_cast<int32_t> (i);
		const uint32_t mixId = host::params::slotId (index, host::params::SlotParam::Mix);
		const uint32_t bypassId = host::params::slotId (index, host::params::SlotParam::Bypass);
		setParamNormalized (mixId, host::params::toNormalised (mixId, slot.mix));
		setParamNormalized (bypassId, slot.bypass ? 1.0 : 0.0);
	}
	return kResultOk;
}

tresult PLUGIN_API CrateController::getState (IBStream* stream)
{
	if (!stream)
		return kResultFalse;

	std::vector<uint8_t> payload;
	payload.insert (payload.end (), kMagic, kMagic + 8);
	putU32 (payload, 1); // version
	putU32 (payload, static_cast<uint32_t> (selectedSlot_));

	for (int32_t slot = 0; slot < host::kMaxSlots; ++slot)
	{
		const uint32_t mixId = host::params::slotId (slot, host::params::SlotParam::Mix);
		const uint32_t bypassId =
		    host::params::slotId (slot, host::params::SlotParam::Bypass);
		const double mix = host::params::toPlain (mixId, getParamNormalized (mixId));
		const double bypass = host::params::toPlain (bypassId, getParamNormalized (bypassId));
		putF32 (payload, static_cast<float> (mix));
		payload.push_back (bypass >= 0.5 ? 1 : 0);
	}

	const uint64_t sum = fnv1a64 (std::string_view (
	    reinterpret_cast<const char*> (payload.data ()), payload.size ()));
	for (int i = 0; i < 8; ++i)
		payload.push_back (static_cast<uint8_t> ((sum >> (8 * i)) & 0xFF));

	int32 written = 0;
	if (stream->write (payload.data (), static_cast<int32> (payload.size ()), &written) !=
	    kResultOk)
		return kResultFalse;
	return kResultOk;
}

tresult PLUGIN_API CrateController::setState (IBStream* stream)
{
	std::vector<uint8_t> data;
	if (!readStream (stream, data))
		return kResultFalse;
	if (data.size () < 8 + 4 + 4 + 8)
		return kResultFalse;
	if (std::memcmp (data.data (), kMagic, 8) != 0)
		return kResultFalse;

	const size_t payloadSize = data.size () - 8;
	uint64_t stored = 0;
	for (int i = 0; i < 8; ++i)
		stored |= static_cast<uint64_t> (data[payloadSize + static_cast<size_t> (i)]) << (8 * i);
	const uint64_t sum = fnv1a64 (std::string_view (
	    reinterpret_cast<const char*> (data.data ()), payloadSize));
	if (sum != stored)
		return kResultFalse;
	if (readU32 (data, 8) != 1)
		return kResultFalse; // a newer controller wrote this: refuse rather than misread

	const int32_t selected = static_cast<int32_t> (readU32 (data, 12));
	const size_t expected =
	    16 + static_cast<size_t> (host::kMaxSlots) * 5;
	if (payloadSize < expected)
		return kResultFalse;

	for (int32_t slot = 0; slot < host::kMaxSlots; ++slot)
	{
		const size_t offset = 16 + static_cast<size_t> (slot) * 5;
		const float mix = readF32 (data, offset);
		const bool bypass = data[offset + 4] != 0;
		const uint32_t mixId = host::params::slotId (slot, host::params::SlotParam::Mix);
		const uint32_t bypassId =
		    host::params::slotId (slot, host::params::SlotParam::Bypass);
		setParamNormalized (mixId, host::params::toNormalised (mixId, mix));
		setParamNormalized (bypassId, bypass ? 1.0 : 0.0);
	}

	setSelectedSlot (selected);
	return kResultOk;
}

} // namespace crate::vst3
