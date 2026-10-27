#include "hostcontext.h"

#include "pluginids.h"
#include "public.sdk/source/vst/utility/stringconvert.h"

namespace crate::vst3 {

using namespace Steinberg;
using namespace Steinberg::Vst;

tresult PLUGIN_API CrateHostApplication::getName (String128 name)
{
	return StringConvert::convert (kPluginName, name) ? kResultTrue : kInternalError;
}

//------------------------------------------------------------------------
tresult PLUGIN_API HostedPlugFrame::resizeView (IPlugView* view, ViewRect* newSize)
{
	(void)view;
	(void)newSize;
	return kResultFalse;
}

tresult PLUGIN_API HostedPlugFrame::queryInterface (const TUID _iid, void** obj)
{
	QUERY_INTERFACE (_iid, obj, FUnknown::iid, IPlugFrame)
	QUERY_INTERFACE (_iid, obj, IPlugFrame::iid, IPlugFrame)
	*obj = nullptr;
	return kNoInterface;
}

//------------------------------------------------------------------------
tresult PLUGIN_API HostedComponentHandler::beginEdit (ParamID id)
{
	(void)id;
	return kResultOk;
}

tresult PLUGIN_API HostedComponentHandler::performEdit (ParamID id, ParamValue valueNormalized)
{
	(void)id;
	(void)valueNormalized;
	return kResultOk;
}

tresult PLUGIN_API HostedComponentHandler::endEdit (ParamID id)
{
	(void)id;
	return kResultOk;
}

tresult PLUGIN_API HostedComponentHandler::restartComponent (int32 flags)
{
	restartFlags_.fetch_or (flags);
	if ((flags & kLatencyChanged) != 0)
		latencyDirty_ = true;
	return kResultOk;
}

tresult PLUGIN_API HostedComponentHandler::setDirty (TBool state)
{
	(void)state;
	return kResultOk;
}

tresult PLUGIN_API HostedComponentHandler::requestOpenEditor (FIDString name)
{
	(void)name;
	return kResultFalse;
}

tresult PLUGIN_API HostedComponentHandler::startGroupEdit ()
{
	return kResultOk;
}

tresult PLUGIN_API HostedComponentHandler::finishGroupEdit ()
{
	return kResultOk;
}

tresult PLUGIN_API HostedComponentHandler::queryInterface (const TUID _iid, void** obj)
{
	QUERY_INTERFACE (_iid, obj, FUnknown::iid, IComponentHandler)
	QUERY_INTERFACE (_iid, obj, IComponentHandler::iid, IComponentHandler)
	QUERY_INTERFACE (_iid, obj, IComponentHandler2::iid, IComponentHandler2)
	*obj = nullptr;
	return kNoInterface;
}

} // namespace crate::vst3
