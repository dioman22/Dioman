// The controller is the parameter list the DAW automates and the parent of the editor.
//
// Until the editor existed this only exposed per-slot mix/bypass. Now it is also the half
// that owns the catalogue: it reads the host's registry on the message thread, enriches
// cards a few at a time, and forwards a click to the processor (which owns the chain) over
// the VST3 connection point. Nothing here runs on the audio thread.
#pragma once

#include "../host/params.h"
#include "../registry/entry.h"
#include "../registry/registryservice.h"

#include "public.sdk/source/vst/vsteditcontroller.h"

#include <memory>
#include <string>
#include <vector>

namespace crate::vst3 {

class CrateEditor;

class CrateController : public Steinberg::Vst::EditController
{
public:
	static Steinberg::FUnknown* createInstance (void* context);

	CrateController ();

	Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API terminate () SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setComponentState (Steinberg::IBStream* state) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setState (Steinberg::IBStream* state) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API getState (Steinberg::IBStream* state) SMTG_OVERRIDE;

	Steinberg::IPlugView* PLUGIN_API createView (Steinberg::FIDString name) SMTG_OVERRIDE;

	/** Load result from the processor. */
	Steinberg::tresult PLUGIN_API notify (Steinberg::Vst::IMessage* message) SMTG_OVERRIDE;

	OBJ_METHODS (CrateController, Steinberg::Vst::EditController)

	/** Which slot the grid would load into. UI state, saved with the controller. */
	int32_t selectedSlot () const;
	void setSelectedSlot (int32_t slot);

	// --- editor API. Message thread only; the editor runs on the same thread. ---

	/** Reads the host's registry once, on the first open. A failure becomes a sentence
	 *  in the footer, never an empty grid with no explanation. */
	void ensureCatalogue ();
	const std::vector<registry::Entry>& catalogue () const { return entries_; }
	const registry::RegistrySnapshot& registrySnapshot () const { return snapshot_; }
	int catalogueRevision () const { return catalogueRevision_; }
	/** "reaper-vstplugins64 · 214 plugins", for the header. */
	std::string sourceLine () const;

	/** Resolves a few more cards. Called from the editor's idle timer so opening never
	 *  walks hundreds of bundles at once. */
	void pumpEnrichment (int budget);

	/** Forwards a card click to the processor and reports what could not be sent. */
	bool loadEntry (const registry::Entry& entry, int slot, std::string& error);

	const std::string& statusMessage () const { return statusMessage_; }
	void setStatusMessage (std::string message) { statusMessage_ = std::move (message); }

	void editorClosed (CrateEditor* editor);

private:
	void addParameters ();

	int32_t selectedSlot_ = 0;

	std::string hostName_;
	std::string statusMessage_;
	void refreshEditor ();

	std::unique_ptr<registry::RegistryService> registry_;
	registry::RegistrySnapshot snapshot_;
	std::vector<registry::Entry> entries_;
	std::vector<char> enriched_;
	size_t enrichCursor_ = 0;
	bool catalogueLoaded_ = false;
	int catalogueRevision_ = 0;

	CrateEditor* editor_ = nullptr;
};

} // namespace crate::vst3
