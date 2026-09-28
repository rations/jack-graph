// X11Window -- a top-level window, painted by hand through Canvas, plus the loop that drives it.
//
// THE ONLY PLACE X11 LIVES. src/gfx/ links cairo, cairo-ft and freetype2 and nothing else, which
// is what lets tools/uirender compose and audit both real panels with no X server running. That
// split is stated as a rule in every sibling project's build file and it is kept here.
//
// Merged from the two siblings rather than written: Audio-Gui's window is the base (a managed
// window with WM protocols, a class hint, size hints, an icon, extra descriptors, several timers
// on absolute deadlines, a resizable height and a persistent back buffer), and the text-input
// path is simple-login-gui's (XIM/XIC and Xutf8LookupString rather than XLookupString).
//
// Four rules carry over unchanged, because they are the ones that bite:
//
//   * DOUBLE BUFFER, ALWAYS. Compose into an ARGB32 image surface, blit once with
//     CAIRO_OPERATOR_SOURCE. A partially drawn frame is never visible.
//   * NEVER PAINT SYNCHRONOUSLY FROM AN EVENT HANDLER. Handlers set a dirty flag; the paint
//     happens once per pass round the loop, so a burst of motion events costs one repaint.
//   * INSTALL THE NON-FATAL X ERROR HANDLER (xerror.h) before creating anything.
//   * DETECT ASYNCHRONOUS XCreateWindow FAILURE by counting X errors across an XSync. X requests
//     do not fail in place, so a window that was never created otherwise shows up much later as
//     an unrelated BadDrawable.
//
// LAYOUT IS IN LOGICAL UNITS. The window is created at logical size x scale, one cairo_scale is
// applied at compose time, and pointer coordinates are divided by the scale before they reach the
// callbacks. No geometry constant anywhere has a scale factor baked into it.
//
//------------------------------------------------------------------------------------------------
// SIX THINGS HERE ARE IN NEITHER SIBLING. Each is a real gap and not a preference; each is
// something this repository's two windows need and no sibling's one window did.
//
//   1. THE WHEEL (Callbacks::scroll). No sibling project handles the wheel at all -- checked
//      across all six. jack-graph zooms with it, and that is not optional: it is how you navigate
//      a graph with twenty clients in it. It needs no addition to the event mask, for the reason
//      spelled out at the XSelectInput call.
//
//   2. WHICH BUTTON (the `button` index). Audio-Gui forwards Button1 only. jack-graph's
//      right-click on a cable disconnects it (GraphCanvas.cpp:510 in the GTK build), so the
//      button number has to reach the panel. Button4/5 never arrive here -- they are the wheel,
//      and they go to scroll().
//
//   3. TWO-AXIS RESIZE. Audio-Gui pins the width and resizes only the height, and its size hints
//      say so. jack-graph is a freely resizable 1200x800 window. So Geometry carries min/max on
//      both axes and ConfigureNotify updates both logical extents. LEAVING AN AXIS'S BOUNDS AT
//      ZERO PINS IT to the logical size, which is the degenerate case and is what the two dialogs
//      pass.
//
//   4. A SECOND TOP-LEVEL WINDOW (openDialog). jack-graph's JACK Settings is its own window with
//      WM_TRANSIENT_FOR and _NET_WM_WINDOW_TYPE_DIALOG. It has its OWN X window, back buffer and
//      Callbacks, but it is DRIVEN BY THE PARENT'S run() LOOP, which dispatches each XEvent by
//      ev.xany.window. One display, one select(), no second loop and no nested modality --
//      which matters because gtk_dialog_run's nested loop is precisely what the port is getting
//      rid of.
//
//   5. TEXT INPUT THROUGH XIM. The recorder's filename field can contain an accented character
//      and XLookupString cannot type one. Every event is offered to XFilterEvent first, which is
//      how the input method gets to consume the keystrokes making up a dead-key or Compose
//      sequence. Straight from simple-login-gui, which needed it so a password with an accent in
//      it could be typed.
//
//   6. KEYS ARRIVE TRANSLATED. Both siblings hand a raw KeySym to the callback, which leaks an X
//      type into panel code that is supposed to be X-free. platform/keymap.h already exists to
//      translate; this window calls it, so the panels see gfx/keys.h values and uirender can
//      drive them with Key directly.

#pragma once

#include "gfx/canvas.h"
#include "gfx/fontstack.h"
#include "gfx/keys.h"

#include <X11/Xlib.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace jackgraph
{

class X11Window
{
public:
    X11Window() = default;
    ~X11Window();

    X11Window(const X11Window &) = delete;
    X11Window &operator=(const X11Window &) = delete;

    // All coordinates handed to these are LOGICAL units, already divided by the scale.
    struct Callbacks {
        std::function<void(Canvas &)> draw;

        // `button` is 1, 2 or 3. `pressed` is true on ButtonPress and false on ButtonRelease.
        // The wheel is NOT reported here -- see scroll.
        std::function<void(float x, float y, int button, bool pressed)> button;

        std::function<void(float x, float y)> motion;

        // `dir` is -1 for a notch up (Button4) and +1 for a notch down (Button5).
        std::function<void(float x, float y, int dir)> scroll;

        // One key press, already translated out of X's world by platform/keymap.h. `text` is the
        // UTF-8 the input method produced, which is empty for a key that produces none and can be
        // more than one byte -- or, after a dead-key sequence, more than one character. `state` is
        // the raw X modifier mask, which is the one X type that has to come through: a widget
        // cannot tell Ctrl+A from A without it.
        //
        // Return true if the key was handled. An unhandled Escape closes the window -- or, for a
        // dialog, closes the dialog.
        std::function<bool(Key k, const char *text, int len, unsigned state)> key;

        // The window is going away, by WM_DELETE_WINDOW or by an unhandled Escape. For a dialog
        // this is where the owner drops its reference; for a main window it is informational,
        // since run() is about to return.
        std::function<void()> closed;

        // The window changed size. Logical units. Only fires for an axis that is not pinned, so a
        // window with both axes pinned never sees it.
        std::function<void(float w, float h)> resized;
    };

    // What size the window is and what sizes it may become. LEAVING AN AXIS'S min/max AT ZERO
    // PINS THAT AXIS to its logical size and tells the window manager so.
    struct Geometry {
        float logicalW = 0.0f;
        float logicalH = 0.0f;
        float minW = 0.0f;
        float maxW = 0.0f;
        float minH = 0.0f;
        float maxH = 0.0f;
        float scale = 1.0f;
    };

    // Returns false having already warned: no display, no window, or no drawing surface.
    bool open(const std::string &title, const std::string &wmClass, const Geometry &g);

    // A second top-level window, transient for `parent` and typed as a dialog, sharing the
    // parent's display connection and font stack. It does NOT get its own loop: give it callbacks
    // with setCallbacks() and the PARENT'S run() will draw it and deliver its events.
    //
    // The shared display is why this is not just another open(): the dialog must not
    // XCloseDisplay on the way out, and it must not unregister the X error handler either.
    bool openDialog(X11Window &parent, const std::string &title, const std::string &wmClass,
                    const Geometry &g);

    // For a dialog: the callbacks the parent's loop will use. A main window passes its callbacks
    // to run() instead.
    void setCallbacks(const Callbacks &cb)
    {
        mActive = &cb;
        mDirty = true;
    }

    // Ask to be closed at the end of this pass round the parent's loop. NOT a synchronous close:
    // a dialog closing itself from inside its own button handler would otherwise destroy the
    // window the loop is in the middle of dispatching to.
    void requestClose()
    {
        mCloseRequested = true;
    }

    // Publish an _NET_WM_ICON before open(). The payload is EWMH 1.5 section 5.12 packed ARGB
    // cardinals; see the note on publishIcon in the .cpp. Never calling this simply publishes no
    // icon, and the desktop entry's Icon= key is then all a desktop environment has to go on.
    void setIcon(const uint32_t *data, size_t words)
    {
        mIconData = data;
        mIconWords = words;
    }

    // Change the logical size, each axis clamped to the bounds given to open(). Cheap and
    // idempotent: a call that changes neither dimension does nothing at all. A pinned axis
    // ignores what it is passed, so a caller need not know which axes are pinned.
    void resize(float logicalW, float logicalH);

    float logicalWidth() const
    {
        return mLogicalW;
    }
    float logicalHeight() const
    {
        return mLogicalH;
    }
    float scale() const
    {
        return mScale;
    }
    Rect bounds() const
    {
        return Rect(0, 0, mLogicalW, mLogicalH);
    }

    // Wait on `fd` as well as the X connection, calling `onReady` when it is ready. The token
    // returned removes it again.
    //
    // `wantWrite` puts the descriptor in select()'s WRITE set instead of the read set, and exists
    // for exactly one caller: libdbus asks for a write watch when its outgoing queue will not
    // drain in one go. A write watch is ready almost all the time, so registering one that nobody
    // disables spins this loop at 100% CPU -- which is safe here ONLY BECAUSE libdbus disables the
    // watch as soon as the queue empties and the client removes it again on that toggle. Do not
    // add a write watch for anything that does not disable itself.
    //
    // Handlers are dispatched from a COPY of the list, because a handler may add or remove
    // descriptors -- which the mixer's does, every time the output device changes and the mixer
    // is reopened onto a different card, and which libdbus's watch functions do during
    // authentication.
    int addFd(int fd, std::function<void()> onReady, bool wantWrite = false);
    void removeFd(int token);

    // Call `fn` every intervalMs. The token returned removes it again.
    //
    // The deadline is absolute, so a slow frame does not make the next tick late as well, and a
    // busy pointer cannot starve it. Audio-Gui's note on this is worth keeping: the window it was
    // ported FROM fired its one tick only when select() timed out, so a stream of motion events
    // reset the timeout every pass and the tick never ran. Here a device poll cannot be starved
    // by somebody dragging a volume slider.
    int addTimer(int intervalMs, std::function<void()> fn);
    void removeTimer(int token);

    void run(const Callbacks &cb);
    void stop()
    {
        mRunning = false;
    }
    // Ask for a repaint on the next pass. Cheap and idempotent -- call it from any handler.
    void invalidate()
    {
        mDirty = true;
    }

    // Repaint NOW, from inside a handler, and only for the one case that needs it: a handler that
    // is about to block for a noticeable time. Switching the output device SIGTERMs the running
    // bridge and waits for it to exit, and starting jackd takes a second or two; leaving a stale
    // frame up for that long, with the old device still shown as selected, reads as a hang. Every
    // other repaint goes through invalidate() and the loop's one paint per pass, which is the rule
    // this is the deliberate exception to.
    void paintNow()
    {
        if (mActive) {
            mDirty = false;
            paint(*mActive);
        }
    }

    bool fontsAreBundled() const
    {
        return mFonts ? mFontsLoaded : true;
    }

private:
    void paint(const Callbacks &cb);
    void close();
    void applySizeHints();
    void releaseBuffer();
    void openInputMethod();
    void closeInputMethod();
    // True when the event was for this window and has been handled.
    bool handleEvent(XEvent &ev);
    void reapDialogs();
    // The half of open() and openDialog() that is the same: create the X window, set its
    // properties and hints, map it, check the round trip, make the surface. `parent` non-null
    // makes it a dialog.
    bool createWindow(const std::string &title, const std::string &wmClass, const Geometry &g,
                      X11Window *parent);

    struct FdWatch {
        int token;
        int fd;
        bool write; // in select()'s write set rather than its read set
        std::function<void()> onReady;
    };
    struct TimerEntry {
        int token;
        long intervalNs;
        long dueNs; // absolute CLOCK_MONOTONIC
        std::function<void()> fn;
    };

    ::Display *mDpy = nullptr;
    ::Window mWin = 0;
    Atom mWmDelete = 0;
    cairo_surface_t *mTarget = nullptr;

    XIM mXim = nullptr;
    XIC mXic = nullptr;

    // Owned by a main window, borrowed by a dialog. A dialog with its own FontStack would load
    // both faces a second time for a window that is open for ten seconds.
    FontStack *mFonts = nullptr;
    FontStack mOwnFonts;
    bool mFontsLoaded = false;

    // False for a dialog: the display, the error-handler registration and the font stack all
    // belong to the parent, and closing them here would take the main window down with the
    // dialog.
    bool mOwnsDisplay = true;

    float mLogicalW = 0, mLogicalH = 0, mScale = 1.0f;
    float mMinW = 0, mMaxW = 0, mMinH = 0, mMaxH = 0;

    // Reallocated only when the pixel size changes, not once per frame.
    cairo_surface_t *mBuffer = nullptr;
    int mBufferW = 0, mBufferH = 0;

    const uint32_t *mIconData = nullptr;
    size_t mIconWords = 0;

    std::vector<FdWatch> mFds;
    std::vector<TimerEntry> mTimers;
    int mNextToken = 1;

    // Dialogs this window is driving. Not owned: the caller owns the X11Window object and this is
    // only how run() finds it to dispatch and to paint.
    std::vector<X11Window *> mDialogs;
    X11Window *mParent = nullptr;

    // The callbacks in force. For a main window, the ones run() was given, so paintNow() can
    // compose the same frame the loop would; for a dialog, the ones setCallbacks() was given.
    const Callbacks *mActive = nullptr;

    bool mRunning = false;
    bool mDirty = true;
    bool mCloseRequested = false;
};

} // namespace jackgraph
