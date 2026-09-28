// The graph window's chrome: the toolbar across the top, the status line along the foot, and the
// About card.
//
// THE MENU BAR IS GONE AND THIS IS WHAT REPLACES IT. Gtk::MenuBar held four menus whose every item
// was a single action -- Refresh, three zooms, JACK Settings, About -- which is a toolbar wearing a
// menu's clothes, and no project in this family has a menu bar. The two duplicate buttons that sat
// under the canvas (Refresh and JACK Settings, which existed because the menu was easy to miss) go
// with it: there is one place to press each thing now.
//
// Quit is gone too, and deliberately: it was a menu item doing exactly what the window manager's
// close button already does, and WM_DELETE_WINDOW is handled.
//
// CAIRO ONLY, like graphpanel and settingspanel, so tools/uirender can compose and audit the
// toolbar without an X server -- which matters more here than anywhere else in this window,
// because a pill's width comes from MEASURING its label and a label that does not fit its pill is
// not a crash, it is an ellipsis the author never sees.

#pragma once

#include "gfx/canvas.h"
#include "gfx/widgets.h"

#include <functional>
#include <string>

#ifndef JACKGRAPH_VERSION
#error "JACKGRAPH_VERSION must be defined by the build (the Makefile reads it from ./VERSION)"
#endif

namespace jackgraph
{

// The About card's four strings, which is everything Gtk::AboutDialog was given. Here rather than
// in chrome.cpp so tools/uirender measures the very strings the card draws. The version comes from
// the VERSION file through the Makefile, so it cannot drift from the release the way the
// hard-coded "1.0.5" did.
namespace about
{
constexpr const char *kName = "Jack Graph";
constexpr const char *kVersion = "Version " JACKGRAPH_VERSION;
constexpr const char *kComment = "JACK and ALSA port connection manager";
constexpr const char *kLicence = "GPL-2.0";
} // namespace about

// The seven toolbar actions, in the order they are drawn.
enum class Tool { None, Refresh, ZoomOut, ZoomIn, ZoomNormal, Fit, Settings, About };

class Chrome
{
public:
    // A pill was pressed and released over the same pill.
    std::function<void(Tool)> onTool;
    std::function<void()> onNeedsRepaint;

    // The whole window, in logical units. Called at start-up and on every resize.
    void setWindow(const Rect &r);

    // Measure the labels and place the pills. Needs a Canvas because only a Canvas can measure a
    // string; called from draw(), once, since the labels never change.
    void layout(Canvas &c);

    // What is left for the graph: everything between the toolbar and the status line.
    Rect canvasRect() const;

    // update_status_bar()'s text, drawn rather than pushed into a Gtk::Label.
    void setStatus(const std::string &text);
    const std::string &status() const
    {
        return mStatus;
    }

    // Whether a pill draws as engaged. Only Settings uses it -- its window stays open while the
    // graph carries on, so the pill says so rather than opening a second one.
    void setActive(Tool t, bool on);

    void draw(Canvas &c) const;

    // Each returns true when the event was the toolbar's. PRESS AND RELEASE ARE PAIRED: press
    // records what was under the pointer and release acts only if it is still the same pill.
    bool press(float x, float y, int button);
    bool release(float x, float y, int button);
    bool motion(float x, float y);

    // For tools/uirender: the pill a Tool draws as, or null for Tool::None. The audit measures each
    // label against the rect layout() actually gave it rather than against a constant, which is
    // what stops the painter and the check drifting apart.
    const Pill *pill(Tool t) const;

private:
    Pill *pillFor(Tool t);
    Tool targetAt(float x, float y) const;
    void repaint() const
    {
        if (onNeedsRepaint)
            onNeedsRepaint();
    }

    Rect mWindow;
    std::string mStatus;

    // In drawing order, which is also Tool's order.
    Pill mRefresh;
    Pill mZoomOut;
    Pill mZoomIn;
    Pill mZoomNormal;
    Pill mFit;
    Pill mSettings;
    Pill mAbout;

    bool mLaidOut = false;
    Tool mPressTarget = Tool::None;
    Tool mHoverTarget = Tool::None;
};

//------------------------------------------------------------------------
// The About card, in its own dialog window.
//
// Gtk::AboutDialog's content here was four strings and a logo nobody set, so this is four strings
// and a Close pill. It is a separate window rather than a page in the graph for the same reason
// the settings are: it is not part of the graph, and a window manager already knows how to put a
// small transient window in front of a big one.
class AboutCard
{
public:
    std::function<void()> onClose;
    std::function<void()> onNeedsRepaint;

    // The height the card needs. Its width is geo::kAboutW and fixed.
    float layout();
    float height() const
    {
        return mHeight;
    }

    void draw(Canvas &c) const;

    void press(float x, float y, int button);
    void release(float x, float y, int button);
    void motion(float x, float y);

private:
    Pill mClose;
    float mHeight = 0.0f;
    bool mPressedClose = false;
};

} // namespace jackgraph
