// Layout and filtering for the card grid, without a GUI toolkit.
//
// The editor draws what this returns and asks it what was clicked. Keeping the geometry
// here means the hit-testing, the search and the scroll height are unit-tested on a
// machine with no window system — the part most likely to break when the grid changes.
#pragma once

#include "../registry/entry.h"
#include "../util/search.h"

#include <string>
#include <vector>

namespace crate::ui {

struct Rect
{
	float x = 0;
	float y = 0;
	float w = 0;
	float h = 0;

	bool contains (float px, float py) const
	{
		return px >= x && py >= y && px < x + w && py < y + h;
	}
	bool intersects (const Rect& other) const
	{
		return !(other.x >= x + w || other.x + other.w <= x || other.y >= y + h ||
		         other.y + other.h <= y);
	}
};

struct GridMetrics
{
	float cardWidth = 288.0f;
	float cardHeight = 96.0f;
	float gap = 12.0f;
	float padding = 14.0f;
};

struct CardSlot
{
	int index = -1; ///< index into visible()
	Rect rect;
};

class GridModel
{
public:
	/** Copies the catalogue. Called when the controller reports a new revision. */
	void setEntries (const std::vector<registry::Entry>& entries);

	void setQuery (const std::string& query);
	void setFilters (const util::Filters& filters);

	const std::string& query () const { return query_; }
	const util::Filters& filters () const { return filters_; }
	const std::vector<registry::Entry>& entries () const { return entries_; }

	/** Recomputes columns and every card rectangle for a viewport of this width. */
	void layout (float viewWidth, const GridMetrics& metrics = GridMetrics ());

	const std::vector<const registry::Entry*>& visible () const { return visible_; }
	const std::vector<CardSlot>& cards () const { return cards_; }
	int visibleCount () const { return static_cast<int> (visible_.size ()); }
	float contentHeight () const { return contentHeight_; }
	int columns () const { return columns_; }

	/** Index into visible() under the point, or -1. */
	int cardAt (float x, float y) const;

	int selected () const { return selected_; }
	void setSelected (int index);
	const registry::Entry* selectedEntry () const;

	/** Moves the selection by whole cards; keeps it inside the visible list. */
	void moveSelection (int delta);

private:
	void rebuild ();
	int indexOf (const registry::Entry* entry) const;

	std::vector<registry::Entry> entries_;
	util::Filters filters_;
	std::string query_;

	std::vector<const registry::Entry*> visible_;
	std::vector<CardSlot> cards_;

	GridMetrics metrics_;
	float layoutWidth_ = -1.0f;
	int columns_ = 1;
	float contentHeight_ = 0.0f;
	int selected_ = -1;
};

} // namespace crate::ui
