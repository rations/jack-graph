// The metrics that belong to the CONTROLS rather than to either panel's layout.
//
// This src/gfx/ is vendored from jack-bridge, which builds its mixer and its own copy of jack-graph
// from it; keep the two close, so a fix in one can be carried to the other as a diff.
//
// The split is: WHAT A CONTROL IS is here, WHERE A CONTROL GOES is in src/graphgeometry.h. A combo
// is 28 units tall and its arrow gutter is 22 wide wherever it is; which combo sits at which y is
// the window's business, never this file's. graphgeometry.h opens the same `geo` namespace and
// adds to it, so call sites say geo::kComboH and there is exactly one definition of every name.
//
// THE VALUES ARE NOT NEW. Every one is carried across from the sibling that already had it --
// Audio-Gui for the slider, indicator, group box and combo, simple-login-gui for the field and
// the menu -- so a control drawn here comes out the same size as the same control drawn there.
// The pill and chip metrics are the exception and are noted where they appear.

#pragma once

namespace jackgraph
{
namespace geo
{

//--- nominal font metrics, for the compile-time asserts only -----------------
//
// Not measurements of any particular string: the font's own box, which is the conservative
// bound. Roboto's ascender is 0.927em and its descender 0.244em (its OS/2 table); these are
// rounded outward so an assert can never pass on a rounding error. Real per-string ink extents
// come from Canvas::stringAscent/stringDescent and are what uirender checks.
constexpr float nominalAscent(float size)
{
    return size * 0.95f;
}
constexpr float nominalDescent(float size)
{
    return size * 0.26f;
}

//--- the window scale -------------------------------------------------------
// What --scale and $JACK_GRAPH_SCALE are clamped to, and the three scales tools/uirender audits
// every page at. Shared rather than per-binary: one environment variable sets both windows, so one
// pair of bounds has to answer for both.
constexpr float kScaleMin = 0.75f;
constexpr float kScaleMax = 2.0f;

//--- text -------------------------------------------------------------------
constexpr float kBodySize = 13.0f;
constexpr float kGroupTitleSize = 13.0f;
constexpr float kSmallSize = 12.0f;

// A label's baseline sits at r.centerY() + fontSize * this. Measured off CPU-Power and shared by
// every project in the family; ink.h states it as part of the idiom.
constexpr float kLabelBaselineBias = 0.36f;

//--- group box --------------------------------------------------------------
constexpr float kGroupRadius = 10.0f;
constexpr float kGroupTitleX = 12.0f;

//--- slider -----------------------------------------------------------------
constexpr float kGrooveH = 6.0f;
constexpr float kGrooveRadius = 3.0f;
constexpr float kThumbR = 8.0f;

//--- checkbox and radio indicator -------------------------------------------
constexpr float kIndicatorSize = 16.0f;
constexpr float kIndicatorGap = 8.0f;
constexpr float kCheckRadius = 4.0f;
constexpr float kRadioRadius = kIndicatorSize / 2.0f;
static_assert(kRadioRadius * 2.0f == kIndicatorSize,
              "a radio indicator is not the same size as a checkbox indicator");

//--- pill (a tab, a toolbar button, a push button) --------------------------
// From rations-amp: drawPill in products/rack/src/rack/rackview.cpp and drawPillToggle in
// core/gfx/widgets.h. THE SIZES are this project's, because that one draws its pills into a
// rack strip and these are the primary buttons of two windows: 24 units tall is a target a
// pointer hits, and the padding is what keeps "Zoom 1:1" and "Apply Live" off their own outline.
constexpr float kPillH = 24.0f;
constexpr float kPillPadX = 14.0f;
constexpr float kPillGap = 6.0f;
constexpr float kPillTextSize = 12.0f;
static_assert(kPillH > nominalAscent(kPillTextSize) + nominalDescent(kPillTextSize),
              "a pill is shorter than the label in it");

// The sliding-knob form, for a setting that is on or off rather than an action: the Steam bridge
// and Discoverable. Wider than tall by enough that the travel reads as travel.
constexpr float kPillToggleW = 40.0f;
constexpr float kPillToggleH = 20.0f;
static_assert(kPillToggleW > kPillToggleH * 1.6f,
              "a pill toggle this square would not read as a switch");

//--- chip (a small non-interactive state badge) ------------------------------
// New in this project: the Bluetooth list's Paired / Trusted / Connected badges. The GTK build
// appended " [Paired]" to the device's own name, which is how a device called "My [Paired]
// Speaker" corrupts its own row -- see the note in btmodel.h. A chip is a separate surface, so
// the name is only ever the name.
constexpr float kChipH = 15.0f;
constexpr float kChipPadX = 6.0f;
constexpr float kChipGap = 4.0f;
constexpr float kChipTextSize = 10.0f;
constexpr float kChipRadius = kChipH / 2.0f;
static_assert(kChipH > nominalAscent(kChipTextSize) + nominalDescent(kChipTextSize),
              "a chip is shorter than the label in it");

//--- combo box and its popup ------------------------------------------------
constexpr float kComboH = 28.0f;
constexpr float kComboRadius = 8.0f;
constexpr float kComboPadX = 10.0f;
constexpr float kComboArrowW = 22.0f;

constexpr float kPopupRowH = 26.0f;
constexpr float kPopupPadY = 5.0f;
constexpr float kPopupRadius = 6.0f;
constexpr float kPopupGap = 4.0f;
constexpr float kPopupTextSize = 12.0f;
constexpr float kPopupTickW = 16.0f; // the gutter holding the current-item tick

constexpr float popupH(int rows)
{
    return 2.0f * kPopupPadY + static_cast<float>(rows) * kPopupRowH;
}

//--- text field -------------------------------------------------------------
constexpr float kFieldH = 30.0f;
constexpr float kFieldRadius = 3.0f;
constexpr float kFieldPadX = 10.0f;
constexpr float kFieldTextSize = 12.0f;
constexpr float kCaretW = 1.0f;
// The password-dot metrics, part of the shared metrics set; jack-graph has no text field.
constexpr float kDotRadius = 2.2f;
constexpr float kDotPitch = 9.0f;
static_assert(kFieldPadX > kCaretW, "the caret would be drawn on top of the field outline");
static_assert(kDotPitch > 2.0f * kDotRadius, "password dots would touch each other");

//--- popup menu -------------------------------------------------------------
constexpr float kMenuW = 230.0f; // the default; Menu::setWidth overrides it per menu
constexpr float kMenuRowH = 28.0f;
constexpr float kMenuPadY = 6.0f;
constexpr float kMenuPadX = 12.0f;
constexpr float kMenuRadius = 4.0f;
constexpr float kMenuTextSize = 12.0f;
constexpr float kMenuGap = 8.0f;
constexpr float kMenuSeparatorInset = 8.0f;

constexpr float menuHeight(int rows)
{
    return 2.0f * kMenuPadY + static_cast<float>(rows) * kMenuRowH;
}

static_assert(kMenuPadX > kFieldPadX * 0.5f, "menu labels would sit too close to the edge");
static_assert(kMenuRowH > nominalAscent(kMenuTextSize) + nominalDescent(kMenuTextSize),
              "a menu row is shorter than the text in it");

//--- list view --------------------------------------------------------------
// New in this project: the Bluetooth device list. A row holds a name, up to three chips and
// nothing else, so its height is set by the chips rather than by the text.
constexpr float kListRowH = 26.0f;
constexpr float kListPadX = 8.0f;
constexpr float kListRadius = 4.0f;
constexpr float kListTextSize = 12.0f;
constexpr float kScrollbarW = 6.0f;
constexpr float kScrollbarInset = 2.0f;
static_assert(kListRowH > kChipH + 4.0f, "a list row cannot hold its state chips");
static_assert(kListRowH > nominalAscent(kListTextSize) + nominalDescent(kListTextSize),
              "a list row is shorter than the text in it");

//--- rules and hairlines ----------------------------------------------------
constexpr int kRuleAlpha = 90;

} // namespace geo
} // namespace jackgraph
