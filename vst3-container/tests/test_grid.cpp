// Geometry and filtering for the card grid. These run without a window system, which is
// the point: a hit-test that is off by one card is a bug you otherwise only see in a DAW.
#include "../source/ui/gridmodel.h"
#include "test_framework.h"

using namespace crate;
using namespace crate::ui;

namespace {

registry::Entry makeEntry (const std::string& name, const std::string& vendor,
                           const std::string& category, bool instrument = false)
{
	registry::Entry entry;
	entry.name = name;
	entry.vendor = vendor;
	entry.category = category;
	entry.isInstrument = instrument;
	entry.format = "VST3";
	entry.uid = "UID" + name;
	entry.bundle = "C:/plugins/" + name + ".vst3";
	entry.resolution = registry::Resolution::Exact;
	entry.status = registry::Status::Ready;
	return entry;
}

std::vector<registry::Entry> sampleCatalogue ()
{
	return {
	    makeEntry ("Pro-Q 4", "FabFilter", "EQ"),
	    makeEntry ("Pro-C 2", "FabFilter", "Dynamics"),
	    makeEntry ("Serum", "Xfer Records", "Synth", true),
	    makeEntry ("Diva", "u-he", "Synth", true),
	    makeEntry ("ValhallaVintageVerb", "Valhalla DSP", "Reverb"),
	    makeEntry ("MT-PowerDrumKit", "Manda Audio", "Drums", true),
	};
}

} // namespace

CRATE_TEST (grid_filters_and_sorts_by_search)
{
	GridModel model;
	model.setEntries (sampleCatalogue ());
	CHECK_EQ (model.visibleCount (), 6);

	model.setQuery ("fabfilter");
	CHECK_EQ (model.visibleCount (), 2);
	CHECK_EQ (std::string (model.visible ()[0]->vendor), std::string ("FabFilter"));

	model.setQuery ("zzz-nothing");
	CHECK_EQ (model.visibleCount (), 0);
	CHECK_EQ (model.selected (), -1);
}

CRATE_TEST (grid_layout_rectangles_do_not_overlap_and_hit_test)
{
	GridModel model;
	model.setEntries (sampleCatalogue ());
	GridMetrics metrics;
	model.layout (620.0f, metrics);

	// (620 - 28 + 12) / 300 = 2.01 -> two columns, three rows for six cards.
	CHECK_EQ (model.columns (), 2);
	CHECK_EQ (static_cast<int> (model.cards ().size ()), 6);

	const Rect& first = model.cards ()[0].rect;
	CHECK_EQ (model.cardAt (first.x + 5.0f, first.y + 5.0f), 0);
	const Rect& second = model.cards ()[1].rect;
	CHECK (second.x > first.x);
	CHECK_EQ (model.cardAt (second.x + 5.0f, second.y + 5.0f), 1);
	// A gap is not a card.
	CHECK_EQ (model.cardAt (first.x + first.w + metrics.gap * 0.5f, first.y + 5.0f), -1);
	CHECK_EQ (model.cardAt (first.x + 5.0f, first.y + first.h + metrics.gap * 0.5f), -1);
}

CRATE_TEST (grid_content_height_covers_every_row)
{
	GridModel model;
	model.setEntries (sampleCatalogue ());
	GridMetrics metrics;
	model.layout (620.0f, metrics); // 2 columns -> 3 rows

	const float expected =
	    2.0f * metrics.padding + 3.0f * metrics.cardHeight + 2.0f * metrics.gap;
	CHECK (model.contentHeight () >= expected - 0.5f);
	CHECK (model.contentHeight () <= expected + 0.5f);
}

CRATE_TEST (grid_keeps_the_selected_plugin_across_a_refresh)
{
	GridModel model;
	model.setEntries (sampleCatalogue ());
	model.setQuery ("serum");
	CHECK_EQ (model.visibleCount (), 1);
	model.setSelected (0);
	CHECK (model.selectedEntry () != nullptr);
	CHECK_EQ (std::string (model.selectedEntry ()->name), std::string ("Serum"));

	// A new catalogue with an extra entry: the selection is matched by uid, not index.
	auto catalogue = sampleCatalogue ();
	catalogue.insert (catalogue.begin (), makeEntry ("Aaa First", "Vendor", "EQ"));
	model.setEntries (catalogue);
	CHECK (model.selectedEntry () != nullptr);
	CHECK_EQ (std::string (model.selectedEntry ()->name), std::string ("Serum"));
}

CRATE_TEST (grid_keyboard_selection_stays_inside_the_list)
{
	GridModel model;
	model.setEntries (sampleCatalogue ());
	model.setSelected (0);
	model.moveSelection (-1);
	CHECK_EQ (model.selected (), 0);
	model.moveSelection (100);
	CHECK_EQ (model.selected (), model.visibleCount () - 1);
	model.moveSelection (1);
	CHECK_EQ (model.selected (), model.visibleCount () - 1);
}

CRATE_TEST (grid_filters_hide_instruments_and_unresolved)
{
	GridModel model;
	auto catalogue = sampleCatalogue ();
	catalogue[0].resolution = registry::Resolution::Unresolved;
	model.setEntries (catalogue);

	util::Filters effectsOnly;
	effectsOnly.effectsOnly = true;
	effectsOnly.vst3Only = false;
	model.setFilters (effectsOnly);
	CHECK_EQ (model.visibleCount (), 3); // Pro-Q 4, Pro-C 2, Valhalla; unresolved still shown

	util::Filters hideUnresolved = effectsOnly;
	hideUnresolved.hideUnresolved = true;
	model.setFilters (hideUnresolved);
	CHECK_EQ (model.visibleCount (), 2);
}
