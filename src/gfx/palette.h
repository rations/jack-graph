// The palette, in one place.
//
// ONE FIXED DARK LOOK. The sibling projects split two ways on this: CPU-Power and
// simple-login-gui pin a constexpr `pal::` namespace, and Audio-Gui carries a runtime `Palette`
// struct because it offers the user dark/light and four accents. This follows the former: there is
// no preferences UI to hang a theme control off, and more to the point, a constexpr palette is
// what lets `inkFor()` stay constexpr and lets tools/uirender audit the real colours without
// constructing an app.
//
// 0xRRGGBB throughout, matching Canvas::setColor. ALPHA IS NEVER BAKED IN -- it is a separate
// argument at every call site, and the named alphas live in ink.h.
//
// WHERE THESE NUMBERS CAME FROM, because inventing a palette for a program that already has one
// is how a port turns into a redesign nobody asked for:
//
//   * The panel colours are Audio-Gui's dark scheme (palette.cpp, the isDark() branch), which is
//     the audio sibling of this program and the closest thing to a house style for a mixer.
//   * The `graph` group below is lifted from GraphCanvas::on_draw's existing literals, converted
//     from Cairo's 0..1 doubles to bytes, so jack-graph after the port looks like jack-graph
//     before it. The graph canvas is the one surface in this project a user already knows by
//     sight, and restyling it in the same commit that replaces its toolkit would make any
//     "it looks wrong" report impossible to attribute.
//   * kGold is the one colour every project in this family shares.

#pragma once

#include <cstdint>

namespace jackgraph
{
namespace pal
{

//--- the panel ---------------------------------------------------------
constexpr uint32_t kBgColor = 0x1E2228;   // the window ground, behind everything
constexpr uint32_t kFaceColor = 0x262B33; // cards and group boxes sitting on it
constexpr uint32_t kWellColor = 0x161A1F; // sunk areas: grooves, fields, list viewports
constexpr uint32_t kBorderColor = 0x333A44;
constexpr uint32_t kGold = 0xB88B4C; // piping / hairlines

constexpr uint32_t kTextColor = 0xE6E9EE; // labels and values
constexpr uint32_t kDimColor = 0x9AA3AD;  // secondary text, and a control's idle outline

// The accent marks what is on: a ticked box, a filled groove, the selected tab, a connected
// device. Green rather than the blue a desktop theme would have given us, because this is the
// family's audio accent (Audio-Gui's default) and because blue is what the graph canvas's
// neutral greys already lean towards.
constexpr uint32_t kAccent = 0x2ECC71;
constexpr uint32_t kAccentBright = 0x6FE3A0;

// What is drawn ON TOP of an accent fill: a checkmark inside a ticked box, the label of a
// selected tab pill. It is a separate constant and not kTextColor because the two answer
// different questions -- kTextColor has to read against kFaceColor, this has to read against
// kAccent -- and an accent light enough to need dark ink here would leave the other unchanged.
constexpr uint32_t kOnAccent = 0xFFFFFF;

// A control that cannot be used right now -- Scan while the adapter is off, Set as Output with
// nothing selected, the USB tab with no interface plugged in -- is drawn in this and LEFT
// VISIBLE, never hidden. It has to read as "not available", not as "absent". This is the rule
// the GTK build followed with gtk_widget_set_sensitive and it survives the port.
constexpr uint32_t kDisabledColor = 0x5A626C;

// kWarnColor is a state that is not an error but wants reading: Discoverable left on, a bridge
// still shutting down. kErrorColor is the message strip after a failed pair, a missing helper,
// or jackd not running.
constexpr uint32_t kWarnColor = 0xE8A33F;
constexpr uint32_t kErrorColor = 0xFF5A4D;

//--- the graph canvas --------------------------------------------------
// Lifted from GraphCanvas::on_draw. Each comment records the call it came from, so the next
// person to touch one of these can check it against the pre-port screenshot rather than taste.
namespace graph
{

constexpr uint32_t kGround = 0x26262E;    // set_source_rgb(0.15, 0.15, 0.18)
constexpr uint32_t kBoxFill = 0x38383F;   // 0.22, 0.22, 0.26 at alpha 0.95
constexpr uint32_t kBoxBorder = 0x59595E; // 0.35, 0.35, 0.40 at alpha 0.8, pen 1.5
constexpr uint32_t kHeaderText = 0xE6E6EB;
constexpr uint32_t kPortText = 0xCCCCD9;
constexpr uint32_t kConnector = 0x8C8C99; // 0.55, 0.55, 0.60 at alpha 0.5, pen 2.0
constexpr uint32_t kPreview = 0xE68033;   // 0.9, 0.5, 0.2 at alpha 0.7, dashed

// THE TWO PORT COLOURS CARRY MEANING AND ARE NOT DECORATION: blue is an audio port, green is a
// MIDI port, and that is the only thing on screen distinguishing them -- a port's name does not
// say which it is. GraphCanvas::draw_port picks between them from Node::type.
constexpr uint32_t kPortAudio = 0x4073D9; // 0.25, 0.45, 0.85
constexpr uint32_t kPortMidi = 0x33B359;  // 0.20, 0.70, 0.35

} // namespace graph

} // namespace pal
} // namespace jackgraph
