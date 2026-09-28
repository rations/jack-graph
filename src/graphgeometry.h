// Where everything in jack-graph's three windows goes.
//
// This opens the same `geo` namespace that src/gfx/metrics.h does, adding jack-graph's own
// constants to it. WHAT A CONTROL IS lives in metrics.h and is shared with the rest of the toolkit;
// WHERE IT GOES lives here.
//
// THE GRAPH WINDOW IS FREELY RESIZABLE. So almost nothing here is a position -- the
// toolbar and the status line take fixed heights off the top and bottom and the canvas is whatever
// is left. What is fixed is the chrome.
//
// The client boxes and ports are NOT in here. Their geometry is ClientBox's own
// (HEADER_HEIGHT, PORT_HEIGHT, COL_WIDTH and the rest), it is in canvas coordinates rather than
// window coordinates, and it is scaled by the zoom -- so it belongs with the model that lays it
// out, which is where it already was.

#pragma once

#include "gfx/metrics.h"

namespace jackgraph
{
namespace geo
{

//--- the window -------------------------------------------------------------
// The size Gtk::Window was given with set_default_size(1200, 800).
constexpr float kGraphW = 1200.0f;
constexpr float kGraphH = 800.0f;
// Small enough to park beside something else, large enough that the toolbar still fits its pills.
constexpr float kGraphMinW = 640.0f;
constexpr float kGraphMinH = 400.0f;
constexpr float kGraphMaxW = 8192.0f;
constexpr float kGraphMaxH = 8192.0f;

constexpr float kGraphMargin = 10.0f;

//--- the toolbar ------------------------------------------------------------
// A ROW OF PILLS, NOT A MENU BAR. No project in this family has a menu bar, and the seven things
// the Gtk::MenuBar held were all single actions -- Refresh, three zooms, Fit, Settings, About --
// which is a toolbar wearing a menu's clothes. The two duplicate buttons below it (Refresh and
// JACK Settings, which existed because the menu was easy to miss) go with it.
constexpr float kToolbarY = kGraphMargin;
constexpr float kToolbarH = kPillH;
constexpr float kToolbarGap = kPillGap;
constexpr int kToolCount = 7; // Refresh, Zoom -, Zoom +, 1:1, Fit, Settings, About

//--- the status line --------------------------------------------------------
// update_status_bar()'s text, drawn rather than put in a Gtk::Label.
constexpr float kStatusH = 20.0f;
constexpr float kStatusSize = 12.0f;
static_assert(kStatusH > nominalAscent(kStatusSize) + nominalDescent(kStatusSize),
              "the status line is shorter than the text in it");

//--- the canvas -------------------------------------------------------------
// Everything between the toolbar and the status line.
constexpr float kCanvasTop = kToolbarY + kToolbarH + 8.0f;
constexpr float kCanvasBottomGap = 6.0f;

constexpr float canvasH(float windowH)
{
    return windowH - kCanvasTop - kStatusH - kCanvasBottomGap - kGraphMargin;
}
static_assert(canvasH(kGraphMinH) > 200.0f,
              "the smallest window leaves too little canvas to see a graph in");

//--- zoom -------------------------------------------------------------------
// GraphCanvas::set_zoom's own clamp, and the steps the menu items used.
constexpr double kZoomMin = 0.25;
constexpr double kZoomMax = 4.0;
constexpr double kZoomStepButton = 1.2; // the Zoom In / Zoom Out menu items
constexpr double kZoomStepWheel = 1.1;  // on_scroll_event
// fit_to_window never zooms IN past 1:1 -- a graph with two boxes in it would otherwise fill the
// window with two enormous boxes.
constexpr double kFitZoomMax = 1.0;
constexpr float kFitMargin = 40.0f;

//--- the JACK Settings window -----------------------------------------------
// ITS OWN TOP-LEVEL WINDOW, opened with X11Window::openDialog: WM_TRANSIENT_FOR the graph and
// _NET_WM_WINDOW_TYPE_DIALOG, so a window manager treats it as a dialog, but driven by the graph's
// own run() loop. It is NOT MODAL, and that is a change worth naming: Gtk::Dialog::run() spun a
// nested main loop, there is no nested loop here, and the graph behind it stays live -- which is
// also what lets Start and Stop run without freezing either window.
constexpr float kSetW = 420.0f; // set_default_size(420, -1)
constexpr float kSetMargin = 14.0f;
constexpr float kSetContentW = kSetW - 2.0f * kSetMargin;

// A group box's title sits in a band ABOVE its frame, so a box needs that band plus its own
// padding before the first row.
constexpr float kSetGroupTitleBand = 20.0f;
constexpr float kSetGroupPad = 10.0f;
constexpr float kSetGroupGap = 12.0f;

constexpr float kSetRowH = kComboH;
constexpr float kSetRowGap = 8.0f;
constexpr float kSetLabelW = 112.0f;
constexpr float kSetLabelGap = 10.0f;

// The Apply Live pill sits BESIDE the frames combo rather than with Start and Stop, because
// frames/period is the only field it touches and putting it there says so without a paragraph.
constexpr float kSetApplyW = 96.0f;
constexpr float kSetComboW = kSetContentW - 2.0f * kSetGroupPad - kSetLabelW - kSetLabelGap -
                             kSetApplyW - kSetRowGap;
static_assert(kSetComboW > 120.0f, "the settings combos are too narrow to read a device name in");

// What Apply Live does, drawn under the field. THERE ARE NO TOOLTIPS HERE, and a sentence saying
// what a button does is better under the button than behind a hover nobody performs.
constexpr float kSetNoteSize = 11.0f;
constexpr float kSetNoteLineH = 14.0f;
constexpr int kSetNoteLines = 2;

// The Realtime and 16-bit rows: a checkbox indicator and its label.
constexpr float kSetToggleH = 22.0f;
static_assert(kSetToggleH >= kIndicatorSize, "a settings toggle row is shorter than its box");

// The message area: whatever jackd said when it failed to start, so its length is not this
// program's to choose. Wrapped rather than clipped, and long enough for the line saying how jackd
// exited plus the three JackServerControl::log_tail() picks -- the first of which, jackd's
// "device is already in use" notice, wraps over three lines on its own.
constexpr float kSetStatusSize = 12.0f;
constexpr float kSetStatusLineH = 15.0f;
constexpr int kSetStatusLines = 6;

constexpr float kSetButtonW = 90.0f;

// The four group boxes' heights, each from its own row count.
constexpr float setServerH()
{
    return 2.0f * kSetGroupPad + kPillH;
}
constexpr float setAudioH()
{
    return 2.0f * kSetGroupPad + 4.0f * kSetRowH + 3.0f * kSetRowGap + kSetRowGap +
           static_cast<float>(kSetNoteLines) * kSetNoteLineH;
}
constexpr float setMidiH()
{
    return 2.0f * kSetGroupPad + kSetRowH;
}
constexpr float setOptionsH()
{
    return 2.0f * kSetGroupPad + 2.0f * kSetToggleH + kSetRowGap;
}
constexpr float setStatusH()
{
    return static_cast<float>(kSetStatusLines) * kSetStatusLineH;
}

constexpr float settingsH()
{
    return kSetMargin + kSetGroupTitleBand + setServerH() + kSetGroupGap + kSetGroupTitleBand +
           setAudioH() + kSetGroupGap + kSetGroupTitleBand + setMidiH() + kSetGroupGap +
           kSetGroupTitleBand + setOptionsH() + kSetGroupGap + setStatusH() + kSetGroupGap +
           kPillH + kSetMargin;
}
static_assert(settingsH() > 300.0f && settingsH() < 700.0f,
              "the settings window is not a plausible height");

//--- the About card ---------------------------------------------------------
// Gtk::AboutDialog held four strings, and this is those four strings: a title, a version, one line
// saying what the program is, and a licence. Wide enough for the longest of them at kAboutTextSize
// with room to spare -- uirender is what checks that claim, since only a Canvas can measure it.
constexpr float kAboutW = 320.0f;
constexpr float kAboutTitleSize = 16.0f;
constexpr float kAboutTextSize = 12.0f;
constexpr float kAboutLineH = 17.0f;
constexpr int kAboutLines = 3; // version, description, licence

constexpr float kAboutTitleGap = 8.0f;

constexpr float aboutH()
{
    return kSetMargin + nominalAscent(kAboutTitleSize) + nominalDescent(kAboutTitleSize) +
           kAboutTitleGap + static_cast<float>(kAboutLines) * kAboutLineH + kSetGroupGap + kPillH +
           kSetMargin;
}
static_assert(aboutH() < kSetW, "the About card is taller than the settings window is wide");

} // namespace geo
} // namespace jackgraph
