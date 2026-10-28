// Test fixture: a minimal VST3 effect that reports 32 samples of latency and applies a
// gain of 2.0. It exists so the integration check can prove that the container really
// loads a bundle, reads its latency and processes through it — with a plugin that has
// no UI, no parameters and no state, which is the easiest shape to reason about.
//
// Built only when the VST3 SDK is available. It is a test artefact: never ship it.
#include "gain_uid.h"

#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioeffect.h"

#include <algorithm>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

const FUID kGainUid (crate::integration::kGainUidLong1, crate::integration::kGainUidLong2,
                     crate::integration::kGainUidLong3, crate::integration::kGainUidLong4);

class GainProcessor : public AudioEffect
{
public:
	static FUnknown* createInstance (void*)
	{
		return static_cast<IAudioProcessor*> (new GainProcessor ());
	}

	GainProcessor ()
	{
		addAudioInput (STR16 ("In"), SpeakerArr::kStereo);
		addAudioOutput (STR16 ("Out"), SpeakerArr::kStereo);
	}

	uint32 PLUGIN_API getLatencySamples () SMTG_OVERRIDE
	{
		return static_cast<uint32> (crate::integration::kLatencySamples);
	}

	tresult PLUGIN_API process (ProcessData& data) SMTG_OVERRIDE
	{
		for (int32 bus = 0; bus < data.numOutputs; ++bus)
		{
			const bool haveInput =
			    data.numInputs > 0 && data.inputs[bus].channelBuffers32 != nullptr;
			for (int32 c = 0; c < data.outputs[bus].numChannels; ++c)
			{
				const float* in = nullptr;
				if (haveInput)
				{
					const int32 source = std::min (c, data.inputs[bus].numChannels - 1);
					in = data.inputs[bus].channelBuffers32[source];
				}
				for (int32 n = 0; n < data.numSamples; ++n)
					data.outputs[bus].channelBuffers32[c][n] =
					    in ? in[n] * crate::integration::kGain : 0.0f;
			}
		}
		return kResultOk;
	}
};

} // namespace

BEGIN_FACTORY ("Crate Tests", "https://example.invalid", "mailto:tests@example.invalid",
               PFactoryInfo::kNoFlags)
	DEF_CLASS2 (INLINE_UID_FROM_FUID (kGainUid), PClassInfo::kManyInstances, kVstAudioEffectClass,
	            "Crate Gain Fixture", Vst::kDistributable, "Fx", "1.0.0", kVstVersionString,
	            GainProcessor::createInstance)
END_FACTORY
