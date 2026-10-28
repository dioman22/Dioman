// Plugin entry point: the factory the DAW calls when it scans the bundle.
//
// Two classes are registered, which is what a split VST3 plugin looks like:
//  * the processor (kVstAudioEffectClass) — audio and state,
//  * the controller (kVstComponentControllerClass) — parameters and (later) the editor.
// The UIDs are in pluginids.h and must never change.
#include "cratecontroller.h"
#include "crateprocessor.h"
#include "pluginids.h"

#include "public.sdk/source/main/pluginfactory.h"

BEGIN_FACTORY (crate::vst3::kVendor, crate::vst3::kVendorUrl, crate::vst3::kVendorEmail,
               PFactoryInfo::kNoFlags)
	DEF_CLASS2 (INLINE_UID_FROM_FUID (crate::vst3::kProcessorUID), PClassInfo::kManyInstances,
	            kVstAudioEffectClass, crate::vst3::kPluginName, Vst::kDistributable,
	            crate::vst3::kSubCategories, crate::vst3::kVersion, kVstVersionString,
	            crate::vst3::CrateProcessor::createInstance)
	DEF_CLASS2 (INLINE_UID_FROM_FUID (crate::vst3::kControllerUID), PClassInfo::kManyInstances,
	            kVstComponentControllerClass, crate::vst3::kPluginName, 0, "",
	            crate::vst3::kVersion, kVstVersionString,
	            crate::vst3::CrateController::createInstance)
END_FACTORY

#if defined(_WIN32)
// A Windows host calls these two right after loading a VST3 module. InitModule and
// DeinitModule live in moduleinit.cpp inside sdk.lib, and referencing them here is also
// what pulls that object into the link.
extern bool InitModule ();
extern bool DeinitModule ();

extern "C" __declspec (dllexport) bool InitDll ()
{
	return InitModule ();
}

extern "C" __declspec (dllexport) bool ExitDll ()
{
	return DeinitModule ();
}
#endif



