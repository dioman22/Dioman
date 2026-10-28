// Integration check for the hosting path. It runs *inside the test process* (no DAW, no
// audio device) and asserts the promises the container makes to a host:
//
//  1. a real .vst3 bundle named in the container state is loaded and processed,
//  2. the container reports the hosted plugin's latency,
//  3. wet/dry at 50 % stays in phase because the dry path is delayed by that latency,
//  4. bypass passes the dry signal (still latency aligned),
//  5. a missing bundle degrades to pass-through instead of crashing or going silent,
//  6. state round-trips: the slot keeps uid, path and name.
//
// Usage: crate_hosting_check <path-to-fixture-bundle>
// Built only when the VST3 SDK is available (see CMakeLists.txt).
#include "gain_uid.h"

#include "pluginterfaces/base/smartpointer.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/module.h"

#include "../../source/state/containerstate.h"
#include "../../source/util/fs.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

// The container is loaded the way a host loads it, not by calling GetPluginFactory:
// that symbol lives inside the .vst3 bundle and there is nothing to link against.
std::string containerBundlePathFrom (const std::string& fixturePath)
{
    const size_t slash = fixturePath.find_last_of ("/\\");
    const std::string folder =
        slash == std::string::npos ? std::string () : fixturePath.substr (0, slash + 1);
    return folder + "Crate.vst3";
}

namespace {

class HostContext : public FUnknown
{
public:
	tresult PLUGIN_API queryInterface (const TUID, void** obj) override
	{
		if (obj)
			*obj = nullptr;
		return kNoInterface;
	}
	uint32 PLUGIN_API addRef () override { return 1000; }
	uint32 PLUGIN_API release () override { return 1000; }
};

/** The fixture's class id, taken from the bundle that is about to be hosted: the
 *  factory hands out exactly the bytes the container compares against, so no byte-order
 *  assumption can be wrong here. */
std::string fixtureUidHex (IPluginFactory* factory)
{
	if (!factory)
		return std::string ();
	static const char* digits = "0123456789ABCDEF";
	for (int32 i = 0; i < factory->countClasses (); ++i)
	{
		PClassInfo info {};
		if (factory->getClassInfo (i, &info) != kResultOk)
			continue;
		if (std::strcmp (info.category, kVstAudioEffectClass) != 0)
			continue;
		std::string out;
		out.reserve (32);
		for (int b = 0; b < 16; ++b)
		{
			const unsigned char byte = static_cast<unsigned char> (info.cid[b]);
			out += digits[byte >> 4];
			out += digits[byte & 0x0F];
		}
		return out;
	}
	return std::string ();
}

std::string gFixtureUid;

int failures = 0;

void check (bool ok, const char* what)
{
	if (!ok)
	{
		std::printf ("FAIL %s\n", what);
		++failures;
	}
}

std::vector<uint8_t> makeState (const std::string& bundlePath, float mix, bool bypass)
{
	crate::state::ContainerState s;
	s.slotCount = 1;
	s.normalize ();
	s.slots[0].uid = gFixtureUid;
	s.slots[0].bundlePath = bundlePath;
	s.slots[0].name = "Crate Gain Fixture";
	s.slots[0].vendor = "Crate Tests";
	s.slots[0].version = "1.0.0";
	s.slots[0].mix = mix;
	s.slots[0].bypass = bypass;

	std::vector<uint8_t> blob;
	std::string error;
	if (!crate::state::encode (s, blob, error))
		std::printf ("encode failed: %s\n", error.c_str ());
	return blob;
}

struct Rig
{
	IPtr<IComponent> component;
	IPtr<IAudioProcessor> processor;
};

bool buildRig (IPluginFactory* factory, HostContext& context, Rig& rig)
{
	for (int32 i = 0; i < factory->countClasses (); ++i)
	{
		PClassInfo info {};
		if (factory->getClassInfo (i, &info) != kResultOk)
			continue;
		if (std::strcmp (info.category, kVstAudioEffectClass) != 0)
			continue;
		IComponent* raw = nullptr;
		if (factory->createInstance (info.cid, IComponent::iid, (void**)&raw) != kResultTrue)
			return false;
		rig.component = owned (raw);
		break;
	}
	if (!rig.component)
		return false;
	if (rig.component->initialize (&context) != kResultOk)
		return false;

	IAudioProcessor* rawProcessor = nullptr;
	if (rig.component->queryInterface (IAudioProcessor::iid, (void**)&rawProcessor) != kResultOk)
		return false;
	rig.processor = owned (rawProcessor);

	ProcessSetup setup {};
	setup.processMode = kRealtime;
	setup.symbolicSampleSize = kSample32;
	setup.maxSamplesPerBlock = 64;
	setup.sampleRate = 48000.0;
	return rig.processor->setupProcessing (setup) == kResultOk;
}

/** Runs one block of constant input and returns its last output sample. */
float runBlock (Rig& rig, float input, int32 samples = 64)
{
	std::vector<float> inL (samples, input), inR (samples, input);
	std::vector<float> outL (samples, -99.0f), outR (samples, -99.0f);
	float* inPtrs[2] = {inL.data (), inR.data ()};
	float* outPtrs[2] = {outL.data (), outR.data ()};

	AudioBusBuffers inBus {}, outBus {};
	inBus.numChannels = 2;
	inBus.channelBuffers32 = inPtrs;
	outBus.numChannels = 2;
	outBus.channelBuffers32 = outPtrs;

	ProcessData data {};
	data.processMode = kRealtime;
	data.symbolicSampleSize = kSample32;
	data.numSamples = samples;
	data.numInputs = 1;
	data.inputs = &inBus;
	data.numOutputs = 1;
	data.outputs = &outBus;
	rig.processor->process (data);
	return outL[samples - 1];
}

} // namespace

int main (int argc, char** argv)
{
	if (argc < 2)
	{
		std::printf ("usage: %s <path-to-fixture-bundle>\n", argv[0]);
		return 2;
	}
	const std::string bundlePath = argv[1];
	std::printf ("fixture: %s\n", bundlePath.c_str ());
std::printf ("fixture exists: %d\n", crate::fs::directoryExists (bundlePath) ? 1 : 0);
check (crate::fs::directoryExists (bundlePath), "fixture bundle path resolves");
std::string fixtureModuleError;
auto fixtureModule = VST3::Hosting::Module::create (bundlePath, fixtureModuleError);
std::printf ("fixture module: %s (%s)\n", fixtureModule ? "loaded" : "FAILED",
             fixtureModuleError.c_str ());
check (fixtureModule != nullptr, "fixture bundle can be loaded");
gFixtureUid = fixtureUidHex (fixtureModule ? fixtureModule->getFactory ().get () : nullptr);
std::printf ("fixture uid: %s\n", gFixtureUid.c_str ());
check (!gFixtureUid.empty (), "fixture uid read from the bundle");
if (fixtureModule)
{
    auto ff = fixtureModule->getFactory ().get ();
    std::printf ("fixture classes: %d\n", (int)ff->countClasses ());
    for (int32 i = 0; i < ff->countClasses (); ++i)
    {
        PClassInfo info {};
        const tresult r1 = ff->getClassInfo (i, &info);
        std::printf ("  [%d] getClassInfo=%d category=[%s] name=[%s] cid=", (int)i,
                     (int)r1, info.category, info.name);
        for (int b = 0; b < 16; ++b)
            std::printf ("%02X", (unsigned char)info.cid[b]);
        std::printf ("\n");
        FUnknownPtr<IPluginFactory2> f2 (ff);
        if (f2)
        {
            PClassInfo2 info2 {};
            const tresult r2 = f2->getClassInfo2 (i, &info2);
            std::printf ("  [%d] getClassInfo2=%d category=[%s] name=[%s] sub=[%s]\n",
                         (int)i, (int)r2, info2.category, info2.name, info2.subCategories);
        }
        else
        {
            std::printf ("  [%d] no IPluginFactory2\n", (int)i);
        }
    }
}

	std::string containerPath = containerBundlePathFrom (bundlePath);
    std::printf ("container: %s\n", containerPath.c_str ());
    std::string moduleError;
    auto module = VST3::Hosting::Module::create (containerPath, moduleError);
    if (!module)
    {
        std::printf ("cannot load the container bundle: %s\n", moduleError.c_str ());
        return 1;
    }
    IPluginFactory* factory = module->getFactory ().get ();
	check (factory != nullptr, "container factory");
	if (!factory)
		return 1;
	HostContext context;

	// 0. The container's own class metadata. Crate is a split plug-in, so the component
	// has to name its controller class; without that a host and the SDK validator treat
	// it as having no controller at all (no parameters, no editor) even though the class
	// is in the factory. Cheap to assert, and it is exactly the defect that shipped in
	// the first bundle.
	{
		IPtr<IComponent> component;
		for (int32 i = 0; i < factory->countClasses (); ++i)
		{
			PClassInfo info {};
			if (factory->getClassInfo (i, &info) != kResultOk)
				continue;
			if (std::strcmp (info.category, kVstAudioEffectClass) != 0)
				continue;
			IComponent* raw = nullptr;
			if (factory->createInstance (info.cid, IComponent::iid,
			                             reinterpret_cast<void**> (&raw)) == kResultOk &&
			    raw)
				component = owned (raw);
			break;
		}
		check (component != nullptr, "the container factory creates its audio component");

		TUID controllerCid {};
		const tresult hasController =
		    component ? component->getControllerClassId (controllerCid) : kResultFalse;
		check (hasController == kResultTrue, "the container component names its controller class");

		IEditController* rawController = nullptr;
		if (hasController == kResultTrue)
			factory->createInstance (controllerCid, IEditController::iid,
			                         reinterpret_cast<void**> (&rawController));
		IPtr<IEditController> controller = owned (rawController);
		check (controller != nullptr, "the controller class can be created from the factory");
		if (controller)
		{
			FUnknownPtr<IPluginBase> base (controller);
			check (base && base->initialize (&context) == kResultOk,
			       "the controller initializes");
			check (controller->getParameterCount () > 0,
			       "the controller exposes the slot parameters");
			if (base)
				base->terminate ();
		}
	}

	// 1-3. A real bundle, fully wet, then half wet, plus the state round trip.
	{
		Rig rig;
		check (buildRig (factory, context, rig), "container rig built");
		const auto state = makeState (bundlePath, 1.0f, false);
		MemoryStream stream (const_cast<uint8_t*> (state.data ()),
		                     static_cast<TSize> (state.size ()));
		check (rig.component->setState (&stream) == kResultOk, "setState");
		check (rig.component->setActive (true) == kResultOk, "setActive");
		check (rig.processor->setProcessing (true) == kResultOk, "setProcessing");

		const uint32 latency = rig.processor->getLatencySamples ();
		std::printf ("latency: %u\n", latency);
		check (latency == static_cast<uint32> (crate::integration::kLatencySamples),
		       "container reports the hosted plugin's latency");

		const float wet = runBlock (rig, 0.25f);
		std::printf ("wet: %.4f\n", wet);
		check (std::fabs (wet - 0.5f) < 1e-5f, "hosted gain applied");

		MemoryStream saved;
		check (rig.component->getState (&saved) == kResultOk, "getState");
		crate::state::ContainerState round;
		std::string error;
		check (crate::state::decode (reinterpret_cast<const uint8_t*> (saved.getData ()),
		                             static_cast<size_t> (saved.getSize ()), round, error) ==
		           crate::state::DecodeStatus::Ok,
		       "saved state decodes with the container's own codec");
		check (round.slots.size () == 1, "one slot saved");
		if (!round.slots.empty ())
		{
			check (round.slots[0].uid == gFixtureUid, "uid saved");
			check (round.slots[0].bundlePath == bundlePath, "bundle path saved");
		}
	}

	{
		Rig rig;
		buildRig (factory, context, rig);
		const auto state = makeState (bundlePath, 0.5f, false);
		MemoryStream stream (const_cast<uint8_t*> (state.data ()),
		                     static_cast<TSize> (state.size ()));
		check (rig.component->setState (&stream) == kResultOk, "setState mix 0.5");
		check (rig.component->setActive (true) == kResultOk, "setActive mix 0.5");
		const float half = runBlock (rig, 0.25f);
		std::printf ("50%%: %.4f\n", half);
		check (std::fabs (half - 0.375f) < 1e-5f, "wet/dry blend stays in phase");
	}

	// 4. Bypass.
	{
		Rig rig;
		buildRig (factory, context, rig);
		const auto state = makeState (bundlePath, 1.0f, true);
		MemoryStream stream (const_cast<uint8_t*> (state.data ()),
		                     static_cast<TSize> (state.size ()));
		check (rig.component->setState (&stream) == kResultOk, "setState bypassed");
		check (rig.component->setActive (true) == kResultOk, "setActive bypassed");
		const float dry = runBlock (rig, 0.25f);
		std::printf ("bypass: %.4f\n", dry);
		check (std::fabs (dry - 0.25f) < 1e-5f, "bypass passes the dry signal");
	}

	// 5. Missing bundle: a sentence, not a crash and not silence.
	{
		Rig rig;
		buildRig (factory, context, rig);
		auto state = makeState (bundlePath, 1.0f, false);
		crate::state::ContainerState parsed;
		std::string error;
		crate::state::decode (state.data (), state.size (), parsed, error);
		parsed.slots[0].bundlePath = "/nonexistent/CrateFixtureMissing.vst3";
		check (crate::state::encode (parsed, state, error), "re-encode missing bundle state");
		MemoryStream stream (const_cast<uint8_t*> (state.data ()),
		                     static_cast<TSize> (state.size ()));
		check (rig.component->setState (&stream) == kResultOk, "setState missing bundle");
		check (rig.component->setActive (true) == kResultOk, "setActive missing bundle");
		const float passthrough = runBlock (rig, 0.25f);
		std::printf ("missing: %.4f\n", passthrough);
		check (std::fabs (passthrough - 0.25f) < 1e-5f,
		       "a missing plugin degrades to pass-through");
	}

	std::printf ("%s\n", failures == 0 ? "HOSTING OK" : "HOSTING FAILED");
	return failures == 0 ? 0 : 1;
}
