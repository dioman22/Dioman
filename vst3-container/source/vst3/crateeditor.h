// The editor: the actual product. A searchable grid of the host's own plugin registry,
// and a click loads that plugin into the selected slot of the container.
//
// VSTGUI comes with the SDK, so this adds no third-party dependency and no installer. The
// grid geometry lives in source/ui/gridmodel.cpp and is unit-tested without a window system;
// this file is only the drawing and the input.
#pragma once

#include "public.sdk/source/vst/vstguieditor.h"

#include <string>

namespace VSTGUI {
class CFrame;
class CView;
class CTextEdit;
} // namespace VSTGUI

namespace crate::vst3 {

class CrateController;

class CrateEditor : public Steinberg::Vst::VSTGUIEditor
{
public:
	explicit CrateEditor (CrateController* controller);
	~CrateEditor () override;

	bool PLUGIN_API open (void* parent, const VSTGUI::PlatformType& platformType) SMTG_OVERRIDE;
	void PLUGIN_API close () SMTG_OVERRIDE;

	Steinberg::tresult PLUGIN_API onSize (Steinberg::ViewRect* newSize) SMTG_OVERRIDE;
	VSTGUI::CMessageResult notify (VSTGUI::CBaseObject* sender,
	                               const char* message) SMTG_OVERRIDE;

	/** Called by the controller when a load finished or a catalogue batch arrived. */
	void refreshFromController ();

	/** Search box text went through the model; the editor re-lays out the grid. */
	void setSearchQuery (const std::string& query);

private:
	void relayout ();

	CrateController* controller_ = nullptr;
	VSTGUI::CFrame* frame_ = nullptr;
	VSTGUI::CView* header_ = nullptr;
	VSTGUI::CView* footer_ = nullptr;
	VSTGUI::CView* scroll_ = nullptr;
	VSTGUI::CView* grid_ = nullptr;
	VSTGUI::CTextEdit* search_ = nullptr;
	int lastRevision_ = -1;
};

} // namespace crate::vst3
