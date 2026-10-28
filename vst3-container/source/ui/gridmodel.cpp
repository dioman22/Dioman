#include "gridmodel.h"

#include <algorithm>
#include <cmath>

namespace crate::ui {

void GridModel::setEntries (const std::vector<registry::Entry>& entries)
{
	// entries_ is replaced, so the old pointer cannot be compared afterwards. Match the
	// selection by uid (or name when the registry gave no uid) instead.
	std::string previousKey;
	if (const registry::Entry* previous = selectedEntry ())
		previousKey = previous->uid.empty () ? previous->name : previous->uid;

	entries_ = entries;
	rebuild ();

	selected_ = -1;
	if (!previousKey.empty ())
	{
		for (size_t i = 0; i < visible_.size (); ++i)
		{
			const registry::Entry* entry = visible_[i];
			const std::string& key = entry->uid.empty () ? entry->name : entry->uid;
			if (key == previousKey)
			{
				selected_ = static_cast<int> (i);
				break;
			}
		}
	}
	if (selected_ < 0 && !visible_.empty ())
		selected_ = 0;
	layout (layoutWidth_, metrics_);
}

void GridModel::setQuery (const std::string& query)
{
	if (query_ == query)
		return;
	query_ = query;
	const registry::Entry* previous = selectedEntry ();
	rebuild ();
	selected_ = previous ? indexOf (previous) : (visible_.empty () ? -1 : 0);
	if (selected_ < 0 && !visible_.empty ())
		selected_ = 0;
	layout (layoutWidth_, metrics_);
}

void GridModel::setFilters (const util::Filters& filters)
{
	filters_ = filters;
	const registry::Entry* previous = selectedEntry ();
	rebuild ();
	selected_ = previous ? indexOf (previous) : (visible_.empty () ? -1 : 0);
	if (selected_ < 0 && !visible_.empty ())
		selected_ = 0;
	layout (layoutWidth_, metrics_);
}

void GridModel::rebuild ()
{
	visible_ = util::search (entries_, query_, filters_);
}

int GridModel::indexOf (const registry::Entry* entry) const
{
	if (!entry)
		return -1;
	for (size_t i = 0; i < visible_.size (); ++i)
	{
		if (visible_[i] == entry)
			return static_cast<int> (i);
	}
	return -1;
}

void GridModel::layout (float viewWidth, const GridMetrics& metrics)
{
	metrics_ = metrics;
	layoutWidth_ = viewWidth;
	cards_.clear ();

	const float usable = std::max (0.0f, viewWidth - 2.0f * metrics_.padding);
	const float step = metrics_.cardWidth + metrics_.gap;
	columns_ = std::max (1, static_cast<int> (std::floor ((usable + metrics_.gap) / step)));

	for (size_t i = 0; i < visible_.size (); ++i)
	{
		const int col = static_cast<int> (i) % columns_;
		const int row = static_cast<int> (i) / columns_;
		CardSlot slot;
		slot.index = static_cast<int> (i);
		slot.rect.x = metrics_.padding + static_cast<float> (col) * step;
		slot.rect.y = metrics_.padding + static_cast<float> (row) * (metrics_.cardHeight + metrics_.gap);
		slot.rect.w = metrics_.cardWidth;
		slot.rect.h = metrics_.cardHeight;
		cards_.push_back (slot);
	}

	if (cards_.empty ())
	{
		contentHeight_ = 2.0f * metrics_.padding;
		return;
	}
	const int rows = (static_cast<int> (cards_.size ()) + columns_ - 1) / columns_;
	contentHeight_ = 2.0f * metrics_.padding + static_cast<float> (rows) * metrics_.cardHeight +
	                 static_cast<float> (rows - 1) * metrics_.gap;

	if (selected_ >= static_cast<int> (visible_.size ()))
		selected_ = visible_.empty () ? -1 : static_cast<int> (visible_.size ()) - 1;
}

int GridModel::cardAt (float x, float y) const
{
	if (cards_.empty () || x < metrics_.padding || y < metrics_.padding)
		return -1;
	const float stepX = metrics_.cardWidth + metrics_.gap;
	const float stepY = metrics_.cardHeight + metrics_.gap;
	const int col = static_cast<int> ((x - metrics_.padding) / stepX);
	const int row = static_cast<int> ((y - metrics_.padding) / stepY);
	if (col < 0 || col >= columns_ || row < 0)
		return -1;
	const int index = row * columns_ + col;
	if (index < 0 || index >= static_cast<int> (cards_.size ()))
		return -1;
	return cards_[static_cast<size_t> (index)].rect.contains (x, y) ? index : -1;
}

void GridModel::setSelected (int index)
{
	if (index < -1 || index >= static_cast<int> (visible_.size ()))
		return;
	selected_ = index;
}

const registry::Entry* GridModel::selectedEntry () const
{
	if (selected_ < 0 || selected_ >= static_cast<int> (visible_.size ()))
		return nullptr;
	return visible_[static_cast<size_t> (selected_)];
}

void GridModel::moveSelection (int delta)
{
	if (visible_.empty ())
	{
		selected_ = -1;
		return;
	}
	if (selected_ < 0)
		selected_ = delta >= 0 ? 0 : static_cast<int> (visible_.size ()) - 1;
	else
		selected_ += delta;
	selected_ = std::max (0, std::min (selected_, static_cast<int> (visible_.size ()) - 1));
}

} // namespace crate::ui
