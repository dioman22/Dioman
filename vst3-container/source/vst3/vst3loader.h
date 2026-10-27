// The loader the container uses in production: resolves a bundle on disk and hands back
// a host::PluginInstance. The cheap checks (does the file still exist, is the UID still
// there) happen before any foreign code is executed, so a plugin that was moved or
// updated produces a sentence instead of a crash or a silent wrong plugin.
#pragma once

#include "../host/plugininstance.h"

#include "pluginterfaces/vst/ivsteditcontroller.h"

namespace crate::vst3 {

class Loader : public host::PluginLoader
{
public:
	Loader (Steinberg::FUnknown* hostContext, Steinberg::Vst::IComponentHandler* handler)
	    : hostContext_ (hostContext), handler_ (handler)
	{
	}

	host::LoadResult load (const host::LoadRequest& request) override;

private:
	Steinberg::FUnknown* hostContext_ = nullptr;
	Steinberg::Vst::IComponentHandler* handler_ = nullptr;
};

} // namespace crate::vst3
