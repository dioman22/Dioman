#include "crateeditor.h"

#include "cratecontroller.h"
#include "../ui/gridmodel.h"

#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/cscrollbar.h"
#include "vstgui/lib/controls/ctextedit.h"
#include "vstgui/lib/cscrollview.h"
#include "vstgui/lib/cview.h"
#include "vstgui/lib/events.h"
#include "vstgui/lib/vstguibase.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace crate::vst3 {

using namespace Steinberg;
using namespace VSTGUI;

namespace {

constexpr CCoord kDefaultWidth = 980;
constexpr CCoord kDefaultHeight = 640;
constexpr CCoord kHeaderHeight = 92;
constexpr CCoord kFooterHeight = 30;
constexpr CCoord kSlotChipWidth = 34;
constexpr CCoord kSlotChipHeight = 22;

const CColor kBackground (20, 22, 27, 255);
const CColor kPanel (26, 29, 36, 255);
const CColor kCard (33, 37, 46, 255);
const CColor kCardHover (40, 45, 56, 255);
const CColor kBorder (54, 61, 76, 255);
const CColor kText (234, 238, 246, 255);
const CColor kMuted (144, 153, 170, 255);
const CColor kWhite (255, 255, 255, 255);
const CColor kAccent (86, 150, 255, 255);
const CColor kError (231, 101, 101, 255);
const CColor kWarn (233, 179, 82, 255);

CFontRef fontTitle ()
{
	static CFontRef font = new CFontDesc ("", 22, kBoldFace);
	return font;
}
CFontRef fontName ()
{
	static CFontRef font = new CFontDesc ("", 14, kBoldFace);
	return font;
}
CFontRef fontBody ()
{
	static CFontRef font = new CFontDesc ("", 12, kNormalFace);
	return font;
}
CFontRef fontSmall ()
{
	static CFontRef font = new CFontDesc ("", 11, kNormalFace);
	return font;
}

/** A stable colour per vendor, so a wall of cards is readable before it has images. */
CColor accentFor (const std::string& text)
{
	uint32_t hash = 2166136261u;
	for (unsigned char c : text)
	{
		hash ^= c;
		hash *= 16777619u;
	}
	static const CColor palette[] = {
	    CColor (86, 150, 255, 255),  CColor (78, 199, 143, 255),
	    CColor (233, 179, 82, 255),  CColor (199, 120, 255, 255),
	    CColor (255, 130, 110, 255), CColor (86, 205, 220, 255),
	};
	return palette[hash % (sizeof (palette) / sizeof (palette[0]))];
}

void drawText (CDrawContext* context, const std::string& value, const CRect& rect,
               CFontRef font, const CColor& color, CHoriTxtAlign align = kLeftText)
{
	context->setFont (font);
	context->setFontColor (color);
	context->drawString (value.c_str (), rect, align);
}

/** Truncates on a UTF-8 boundary. drawString does not clip to the rectangle. */
std::string fitText (CDrawContext* context, CFontRef font, const std::string& value,
                     CCoord maxWidth)
{
	context->setFont (font);
	if (maxWidth <= 0)
		return {};
	if (context->getStringWidth (value.c_str ()) <= maxWidth)
		return value;

	std::string cut = value;
	while (!cut.empty ())
	{
		size_t keep = cut.size () - 1;
		while (keep > 0 && (static_cast<unsigned char> (cut[keep]) & 0xC0) == 0x80)
			--keep;
		cut.resize (keep);
		const std::string candidate = cut + "...";
		if (context->getStringWidth (candidate.c_str ()) <= maxWidth)
			return candidate;
	}
	return {};
}

struct Badge
{
	const char* label;
	CColor color;
};

Badge badgeFor (const registry::Entry& entry)
{
	if (entry.resolution == registry::Resolution::Unresolved)
		return {"MISSING", kError};
	if (entry.status == registry::Status::Incompatible)
		return {"INCOMPATIBLE", kWarn};
	if (entry.status == registry::Status::Blacklisted ||
	    entry.status == registry::Status::Crashed)
		return {"BLOCKED", kError};
	if (entry.status == registry::Status::Unreadable)
		return {"UNREADABLE", kWarn};
	if (entry.isInstrument)
		return {"INSTRUMENT", accentFor (entry.vendor)};
	return {"VST3", kAccent};
}

} // namespace

namespace {

/** The grid itself: one child view of the scroll view that draws every visible card and
 *  turns a click into "load this into the selected slot". */
class CardGridView : public CView
{
public:
	CardGridView (CrateController* controller, CrateEditor* editor, const CRect& size)
	    : CView (size), controller_ (controller), editor_ (editor)
	{
		setWantsFocus (true);
	}

	void setCatalogue (const std::vector<registry::Entry>& entries)
	{
		model_.setEntries (entries);
	}
	void setQuery (const std::string& query) { model_.setQuery (query); }
	void relayout (CCoord width)
	{
		model_.layout (static_cast<float> (width));
		setViewSize (CRect (0, 0, width, model_.contentHeight ()));
		invalid ();
	}
	const ui::GridModel& model () const { return model_; }

	void draw (CDrawContext* context) override
	{
		context->setDrawMode (kAntiAliasing);
		context->setFillColor (kBackground);
		context->drawRect (getViewSize (), kDrawFilled);

		CRect clip;
		context->getClipRect (clip);

		if (model_.visibleCount () == 0)
		{
			const CCoord width = getViewSize ().getWidth ();
			drawText (context, "nothing matches", CRect (18, 26, width - 18, 54), fontTitle (),
			          kMuted);
			drawText (context, "clear the search, or check the registry line at the top",
			          CRect (18, 58, width - 18, 78), fontBody (), kMuted);
			return;
		}

		for (const ui::CardSlot& card : model_.cards ())
		{
			if (card.index < 0 || card.index >= model_.visibleCount ())
				continue;
			CRect rect (card.rect.x, card.rect.y, card.rect.x + card.rect.w,
			            card.rect.y + card.rect.h);
			// With a 900-entry registry the offscreen cards must not cost a text draw.
			if (!rect.rectOverlap (clip))
				continue;
			drawCard (context, rect, card.index,
			          *model_.visible ()[static_cast<size_t> (card.index)]);
		}
	}

	CMouseEventResult onMouseDown (CPoint& where, const CButtonState& buttons) override
	{
		if (!buttons.isLeftButton ())
			return kMouseEventNotImplemented;
		const int index =
		    model_.cardAt (static_cast<float> (where.x), static_cast<float> (where.y));
		if (index < 0)
			return kMouseEventNotImplemented;

		model_.setSelected (index);
		invalidateCard (hover_);
		hover_ = index;
		invalid ();
		if (CFrame* frame = getFrame ())
			frame->setFocusView (this);
		if (const registry::Entry* entry = model_.selectedEntry ())
			load (*entry);
		return kMouseEventHandled;
	}

	CMouseEventResult onMouseMoved (CPoint& where, const CButtonState& buttons) override
	{
		(void) buttons;
		const int index =
		    model_.cardAt (static_cast<float> (where.x), static_cast<float> (where.y));
		if (index != hover_)
		{
			invalidateCard (hover_);
			invalidateCard (index);
			hover_ = index;
		}
		return kMouseEventHandled;
	}

	CMouseEventResult onMouseExited (CPoint& where, const CButtonState& buttons) override
	{
		(void) where;
		(void) buttons;
		invalidateCard (hover_);
		hover_ = -1;
		return kMouseEventHandled;
	}

	void onKeyboardEvent (KeyboardEvent& event) override
	{
		if (event.type != EventType::KeyDown)
			return;

		int delta = 0;
		switch (event.virt)
		{
		case VirtualKey::Left: delta = -1; break;
		case VirtualKey::Right: delta = 1; break;
		case VirtualKey::Up: delta = -model_.columns (); break;
		case VirtualKey::Down: delta = model_.columns (); break;
		case VirtualKey::PageUp: delta = -model_.columns () * 3; break;
		case VirtualKey::PageDown: delta = model_.columns () * 3; break;
		case VirtualKey::Return:
		case VirtualKey::Enter:
			if (const registry::Entry* entry = model_.selectedEntry ())
				load (*entry);
			return;
		default: return;
		}

		invalidateCard (model_.selected ());
		model_.moveSelection (delta);
		invalidateCard (model_.selected ());
		scrollToSelection ();
	}


private:
	void drawCard (CDrawContext* context, const CRect& rect, int index,
	               const registry::Entry& entry)
	{
		const bool selected = index == model_.selected ();
		const bool hovered = index == hover_;

		context->setFillColor (hovered ? kCardHover : kCard);
		context->drawRect (rect, kDrawFilled);

		context->setFillColor (accentFor (entry.vendor.empty () ? entry.name : entry.vendor));
		context->drawRect (CRect (rect.left, rect.top, rect.left + 4, rect.bottom), kDrawFilled);

		context->setLineWidth (selected ? 2.0 : 1.0);
		context->setFrameColor (selected ? kAccent : (hovered ? kMuted : kBorder));
		context->drawRect (rect, kDrawStroked);

		const CCoord left = rect.left + 16;
		const CCoord right = rect.right - 12;
		const CCoord textRight = right - 100;

		drawText (context, fitText (context, fontName (), entry.name, textRight - left),
		          CRect (left, rect.top + 10, textRight, rect.top + 30), fontName (), kText);

		const std::string vendor = entry.vendor.empty () ? "unknown vendor" : entry.vendor;
		drawText (context, fitText (context, fontBody (), vendor, textRight - left),
		          CRect (left, rect.top + 34, textRight, rect.top + 50), fontBody (), kMuted);

		std::string category = entry.category;
		if (category.empty () && !entry.subCategories.empty ())
			category = entry.subCategories.front ();
		if (category.empty ())
			category = entry.isInstrument ? "Instrument" : "plugin";
		drawText (context, fitText (context, fontSmall (), category, textRight - left),
		          CRect (left, rect.top + 56, textRight, rect.top + 72), fontSmall (), kMuted);

		const Badge badge = badgeFor (entry);
		CRect badgeRect (right - 92, rect.top + 10, right, rect.top + 28);
		context->setFillColor (CColor (badge.color.red, badge.color.green, badge.color.blue, 36));
		context->drawRect (badgeRect, kDrawFilled);
		drawText (context, badge.label, badgeRect, fontSmall (), badge.color, kCenterText);
	}

	void load (const registry::Entry& entry)
	{
		std::string error;
		controller_->loadEntry (entry, controller_->selectedSlot (), error);
		editor_->refreshFromController ();
	}

	void invalidateCard (int index)
	{
		if (index < 0 || index >= static_cast<int> (model_.cards ().size ()))
			return;
		const ui::Rect& rect = model_.cards ()[static_cast<size_t> (index)].rect;
		invalidRect (CRect (rect.x - 2, rect.y - 2, rect.x + rect.w + 2, rect.y + rect.h + 2));
	}

	void scrollToSelection ()
	{
		const int selected = model_.selected ();
		if (selected < 0 || selected >= static_cast<int> (model_.cards ().size ()))
			return;
		auto* scrollView = dynamic_cast<CScrollView*> (getParentView ());
		if (!scrollView)
			return;
		CScrollbar* bar = scrollView->getVerticalScrollbar ();
		if (!bar)
			return;

		const ui::Rect& card = model_.cards ()[static_cast<size_t> (selected)].rect;
		const CCoord viewport = scrollView->getViewSize ().getHeight ();
		const CCoord content = model_.contentHeight ();
		const CCoord maxScroll = std::max<CCoord> (1.0, content - viewport);
		const CCoord target =
		    static_cast<CCoord> (card.y) + static_cast<CCoord> (card.h) * 0.5 - viewport * 0.5;
		const CCoord clamped = std::max<CCoord> (0.0, std::min (target, maxScroll));
		bar->setValue (static_cast<float> (clamped / maxScroll));
	}


	CrateController* controller_;
	CrateEditor* editor_;
	ui::GridModel model_;
	int hover_ = -1;
};

} // namespace


namespace {

/** Title, registry line and the slot chips the next click loads into. */
class HeaderView : public CView
{
public:
	HeaderView (CrateController* controller, CrateEditor* editor, const CRect& size)
	    : CView (size), controller_ (controller), editor_ (editor)
	{
	}

	void draw (CDrawContext* context) override
	{
		const CRect bounds = getViewSize ();
		context->setFillColor (kPanel);
		context->drawRect (bounds, kDrawFilled);
		context->setFrameColor (kBorder);
		context->setLineWidth (1);
		context->drawLine (CPoint (bounds.left, bounds.bottom - 1),
		                   CPoint (bounds.right, bounds.bottom - 1));

		drawText (context, "CRATE", CRect (bounds.left + 18, bounds.top + 8, bounds.right - 360,
		                                   bounds.top + 40),
		          fontTitle (), kText);
		drawText (context, controller_->sourceLine (),
		          CRect (bounds.left + 18, bounds.top + 38, bounds.right - 360,
		                 bounds.top + 56),
		          fontSmall (), kMuted);

		const CCoord y = bounds.top + 62;
		CCoord x = bounds.left + 18;
		for (int slot = 0; slot < host::kMaxSlots; ++slot)
		{
			const bool active = slot == controller_->selectedSlot ();
			const CRect chip (x, y, x + kSlotChipWidth, y + kSlotChipHeight);
			context->setFillColor (active ? kAccent : kCard);
			context->drawRect (chip, kDrawFilled);
			if (!active)
			{
				context->setFrameColor (kBorder);
				context->setLineWidth (1);
				context->drawRect (chip, kDrawStroked);
			}
			drawText (context, std::to_string (slot + 1), chip, fontBody (),
			          active ? kWhite : kMuted, kCenterText);
			x += kSlotChipWidth + 6;
		}
		drawText (context, "load into slot", CRect (x + 6, y, x + 130, y + kSlotChipHeight),
		          fontSmall (), kMuted);
	}

	CMouseEventResult onMouseDown (CPoint& where, const CButtonState& buttons) override
	{
		if (!buttons.isLeftButton ())
			return kMouseEventNotImplemented;

		const CCoord y = getViewSize ().top + 62;
		CCoord x = getViewSize ().left + 18;
		for (int slot = 0; slot < host::kMaxSlots; ++slot)
		{
			const CRect chip (x, y, x + kSlotChipWidth, y + kSlotChipHeight);
			if (where.x >= chip.left && where.x < chip.right && where.y >= chip.top &&
			    where.y < chip.bottom)
			{
				controller_->setSelectedSlot (slot);
				controller_->setStatusMessage ("click a card to load it into slot " +
				                               std::to_string (slot + 1));
				editor_->refreshFromController ();
				invalid ();
				return kMouseEventHandled;
			}
			x += kSlotChipWidth + 6;
		}
		return kMouseEventNotImplemented;
	}

private:
	CrateController* controller_;
	CrateEditor* editor_;
};

/** One line of truth: what just happened, and how much of the catalogue is showing. */
class FooterView : public CView
{
public:
	FooterView (CrateController* controller, const CRect& size)
	    : CView (size), controller_ (controller)
	{
	}

	void setVisibleCount (int count)
	{
		if (visibleCount_ == count)
			return;
		visibleCount_ = count;
		invalid ();
	}

	void draw (CDrawContext* context) override
	{
		const CRect bounds = getViewSize ();
		context->setFillColor (kPanel);
		context->drawRect (bounds, kDrawFilled);
		context->setFrameColor (kBorder);
		context->setLineWidth (1);
		context->drawLine (CPoint (bounds.left, bounds.top), CPoint (bounds.right, bounds.top));

		const std::string status = controller_->statusMessage ();
		drawText (context, fitText (context, fontSmall (), status, bounds.getWidth () - 220),
		          CRect (bounds.left + 18, bounds.top + 6, bounds.right - 200, bounds.bottom - 3),
		          fontSmall (), kText);

		const std::string count = std::to_string (visibleCount_) + " of " +
		                          std::to_string (controller_->catalogue ().size ()) +
		                          " shown";
		drawText (context, count,
		          CRect (bounds.right - 190, bounds.top + 6, bounds.right - 18, bounds.bottom - 3),
		          fontSmall (), kMuted, kRightText);
	}

private:
	CrateController* controller_;
	int visibleCount_ = 0;
};

} // namespace


CrateEditor::CrateEditor (CrateController* controller)
    : VSTGUIEditor (controller, nullptr), controller_ (controller)
{
	setIdleRate (120);
}

CrateEditor::~CrateEditor ()
{
	if (controller_)
		controller_->editorClosed (this);
}

bool PLUGIN_API CrateEditor::open (void* parent, const PlatformType& platformType)
{
	if (!controller_)
		return false;
	controller_->ensureCatalogue ();

	frame_ = new CFrame (CRect (0, 0, kDefaultWidth, kDefaultHeight), this);
	frame_->setBackgroundColor (kBackground);
	frame = frame_; // the base class updates its size, idle and events through this

	header_ = new HeaderView (controller_, this, CRect (0, 0, kDefaultWidth, kHeaderHeight));
	frame_->addView (header_);

	footer_ = new FooterView (
	    controller_, CRect (0, kDefaultHeight - kFooterHeight, kDefaultWidth, kDefaultHeight));
	frame_->addView (footer_);

	auto* grid = new CardGridView (controller_, this, CRect (0, 0, kDefaultWidth, kDefaultHeight));
	grid->setCatalogue (controller_->catalogue ());
	grid_ = grid;

	auto* scroll = new CScrollView (
	    CRect (8, kHeaderHeight + 6, kDefaultWidth - 8, kDefaultHeight - kFooterHeight - 6),
	    CRect (0, 0, kDefaultWidth - 16, kDefaultHeight),
	    CScrollView::kVerticalScrollbar | CScrollView::kDontDrawFrame, 12);
	scroll->setBackgroundColor (kBackground);
	scroll->addView (grid_);
	scroll_ = scroll;
	frame_->addView (scroll_);

	search_ = new CTextEdit (CRect (kDefaultWidth - 350, 16, kDefaultWidth - 18, 46), nullptr,
	                         0, "", nullptr);
	search_->setPlaceholderString ("search name, vendor, category");
	search_->setImmediateTextChange (true);
	search_->setStringToValueFunction ([this] (UTF8StringPtr value, float&, CTextEdit*) {
		this->setSearchQuery (value ? std::string (value) : std::string ());
		return false; // this is a filter, not a numeric parameter
	});
	search_->setBackColor (CColor (16, 18, 22, 255));
	search_->setFrameColor (kBorder);
	search_->setFontColor (kText);
	search_->setFont (fontBody ());
	frame_->addView (search_);

	if (!frame_->open (parent, platformType))
	{
		frame_->forget ();
		frame_ = nullptr;
		frame = nullptr;
		return false;
	}
	frame_->setVisible (true);
	relayout ();
	lastRevision_ = controller_->catalogueRevision ();
	return true;
}

void PLUGIN_API CrateEditor::close ()
{
	if (frame_)
	{
		frame_->close ();
		frame_ = nullptr;
		frame = nullptr;
	}
	header_ = nullptr;
	footer_ = nullptr;
	scroll_ = nullptr;
	grid_ = nullptr;
	search_ = nullptr;
	if (controller_)
		controller_->editorClosed (this);
}

tresult PLUGIN_API CrateEditor::onSize (ViewRect* newSize)
{
	const tresult result = VSTGUIEditor::onSize (newSize);
	relayout ();
	return result;
}

CMessageResult CrateEditor::notify (CBaseObject* sender, const char* message)
{
	if (message == CVSTGUITimer::kMsgTimer && controller_ && frame_)
	{
		controller_->pumpEnrichment (2);
		if (controller_->catalogueRevision () != lastRevision_)
			refreshFromController ();
	}
	return VSTGUIEditor::notify (sender, message);
}

void CrateEditor::refreshFromController ()
{
	if (!frame_ || !grid_ || !controller_)
		return;

	auto* grid = static_cast<CardGridView*> (grid_);
	grid->setCatalogue (controller_->catalogue ());
	relayout ();
	if (header_)
		header_->invalid ();
	if (footer_)
	{
		static_cast<FooterView*> (footer_)->setVisibleCount (grid->model ().visibleCount ());
		footer_->invalid ();
	}
	frame_->invalid ();
}

void CrateEditor::setSearchQuery (const std::string& query)
{
	if (!grid_)
		return;
	static_cast<CardGridView*> (grid_)->setQuery (query);
	relayout ();
}

void CrateEditor::relayout ()
{
	if (!frame_)
		return;

	CRect frameSize;
	frame_->getSize (frameSize);
	const CCoord width = frameSize.getWidth ();
	const CCoord height = frameSize.getHeight ();

	if (header_)
		header_->setViewSize (CRect (0, 0, width, kHeaderHeight));
	if (footer_)
		footer_->setViewSize (CRect (0, height - kFooterHeight, width, height));
	if (search_)
		search_->setViewSize (CRect (width - 350, 16, width - 18, 46));
	if (scroll_)
		scroll_->setViewSize (
		    CRect (8, kHeaderHeight + 6, width - 8, height - kFooterHeight - 6));

	if (grid_)
	{
		auto* grid = static_cast<CardGridView*> (grid_);
		grid->relayout (width - 16 - 12); // scroll view insets plus its scrollbar
		if (auto* scroll = dynamic_cast<CScrollView*> (scroll_))
			scroll->setContainerSize (CRect (0, 0, grid->getViewSize ().getWidth (),
			                                 grid->getViewSize ().getHeight ()));
	}

	if (footer_)
		static_cast<FooterView*> (footer_)->setVisibleCount (
		    grid_ ? static_cast<CardGridView*> (grid_)->model ().visibleCount () : 0);
	frame_->invalid ();
}

} // namespace crate::vst3

