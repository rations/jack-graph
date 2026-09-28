// See x11window.h.

#include "x11window.h"

#include "keymap.h"
#include "respath.h"
#include "xerror.h"

#include <cairo/cairo-xlib.h>

#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <sys/select.h>
#include <sys/time.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <ctime>
#include <vector>

namespace jackgraph
{

namespace
{

//------------------------------------------------------------------------
// Publish the application icon on the window itself, as _NET_WM_ICON.
//
// THIS IS NOT THE SAME MECHANISM as the installed icon theme, and it is the one that matters for
// this program's audience. A desktop environment finds an icon through the desktop entry's Icon=
// key and the icon theme; a PLAIN WINDOW MANAGER never reads desktop entries at all, and takes a
// live window's icon from this property. jack-graph is aimed at people running a window manager
// on a machine they have set up by hand, so shipping only the theme copy would leave the titlebar
// and task list blank on exactly the systems it was written for.
//
// NEITHER BINARY SHIPS EMBEDDED ARTWORK TODAY, so setIcon() is simply never called and nothing is
// published; the desktop entries' Icon= keys are all there is. The mechanism is kept whole rather
// than deleted because adding artwork later should be one call at start-up and not a
// re-derivation of what follows.
//
// The caller hands over the property's own layout, which EWMH 1.5 section 5.12 gives as an array
// of 32-bit packed ARGB cardinals -- high byte alpha, low byte blue -- with each image preceded
// by its width and height, rows left to right and top to bottom, and several images concatenated.
// The pixels are NOT premultiplied, which the specification does not say and which had to be
// measured.
//
// The one conversion that is unavoidable is the width of the array. A property declared /32 goes
// over the wire as 32 bits per value, but Xlib takes it from the caller as an array of LONG,
// which is 64 bits here, and narrows it itself. Handing XChangeProperty the packed uint32_t
// array directly would read the right number of bytes and encode the wrong thing entirely --
// every second pixel becoming the top half of the pair before it -- so it is widened here.
void publishIcon(Display *dpy, Window win, const uint32_t *icon, size_t words)
{
    if (!icon || words == 0)
        return;

    // One XChangeProperty is one X request, and a request that exceeds the server's limit is an
    // error rather than a silent truncation. XMaxRequestSize is in 4-byte units and this payload
    // is fixed at compile time, so the check is cheap and the failure says what happened instead
    // of surfacing later as a BadLength against an unrelated call.
    const long need = 6 + static_cast<long>(words); // request header + data, in words
    if (need > XMaxRequestSize(dpy)) {
        fprintf(stderr, "jack-graph: the window icon is too large for this X server, skipping\n");
        return;
    }

    std::vector<unsigned long> prop(icon, icon + words);

    // Not error-checked here on purpose: XChangeProperty is asynchronous and its return value
    // carries no status. The round trip in open() -- sample errorCount(), XSync, sample again --
    // is what actually catches a rejected request, and it already covers this one.
    XChangeProperty(dpy, win, XInternAtom(dpy, "_NET_WM_ICON", False), XA_CARDINAL, 32,
                    PropModeReplace, reinterpret_cast<const unsigned char *>(prop.data()),
                    static_cast<int>(prop.size()));
}

long nowNs()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<long>(ts.tv_sec) * 1000000000L + ts.tv_nsec;
}

int pixels(float logical, float scale)
{
    return static_cast<int>(logical * scale + 0.5f);
}

} // namespace

//------------------------------------------------------------------------
X11Window::~X11Window()
{
    close();
}

//------------------------------------------------------------------------
bool X11Window::open(const std::string &title, const std::string &wmClass, const Geometry &g)
{
    mDpy = XOpenDisplay(nullptr);
    if (!mDpy) {
        fprintf(stderr, "jack-graph: cannot open the X display ($DISPLAY)\n");
        return false;
    }
    mOwnsDisplay = true;

    // BEFORE any window exists: Xlib's default handler calls exit(), and the failure this function
    // goes on to detect would otherwise be detected by dying.
    registerDisplay(mDpy);

    mFonts = &mOwnFonts;

    if (!createWindow(title, wmClass, g, nullptr))
        return false;

    mFontsLoaded = mOwnFonts.load(resourceDir());
    if (!mFontsLoaded) {
        // Not fatal, but worth saying: the layout tools/uirender passed was measured against the
        // bundled faces, and a system fallback has different advance widths, so a label that fit
        // in the audit may not fit on screen.
        fprintf(stderr, "jack-graph: drawing with a system font; labels may not fit their slots\n");
    }
    return true;
}

//------------------------------------------------------------------------
bool X11Window::openDialog(X11Window &parent, const std::string &title, const std::string &wmClass,
                           const Geometry &g)
{
    if (!parent.mDpy || !parent.mWin) {
        fprintf(stderr, "jack-graph: cannot open a dialog before its parent window\n");
        return false;
    }

    // Everything shared is shared deliberately. The display, because two connections would mean
    // two select()s; the error handler registration, because it is per display; the fonts,
    // because loading both faces again for a window that is open for ten seconds is waste with no
    // upside.
    mDpy = parent.mDpy;
    mOwnsDisplay = false;
    mFonts = parent.mFonts;
    mFontsLoaded = parent.mFontsLoaded;
    mParent = &parent;

    if (!createWindow(title, wmClass, g, &parent))
        return false;

    parent.mDialogs.push_back(this);
    parent.mDirty = true;
    return true;
}

//------------------------------------------------------------------------
bool X11Window::createWindow(const std::string &title, const std::string &wmClass,
                             const Geometry &g, X11Window *parent)
{
    mLogicalW = g.logicalW;
    mLogicalH = g.logicalH;
    mScale = g.scale > 0.0f ? g.scale : 1.0f;

    // An axis whose bounds were left at zero is PINNED to its logical size. That is the degenerate
    // case of a bounded axis, not a special case in the code, which is what lets the dialogs stay
    // fixed while the graph window resizes freely.
    mMinW = g.minW > 0.0f ? g.minW : mLogicalW;
    mMaxW = g.maxW > 0.0f ? g.maxW : mLogicalW;
    mMinH = g.minH > 0.0f ? g.minH : mLogicalH;
    mMaxH = g.maxH > 0.0f ? g.maxH : mLogicalH;
    if (mMaxW < mMinW)
        mMaxW = mMinW;
    if (mMaxH < mMinH)
        mMaxH = mMinH;

    const int screen = DefaultScreen(mDpy);
    const int w = pixels(mLogicalW, mScale);
    const int h = pixels(mLogicalH, mScale);

    const unsigned long before = errorCount();
    mWin = XCreateSimpleWindow(mDpy, RootWindow(mDpy, screen), 0, 0, static_cast<unsigned>(w),
                               static_cast<unsigned>(h), 0, BlackPixel(mDpy, screen),
                               BlackPixel(mDpy, screen));

    XStoreName(mDpy, mWin, title.c_str());

    // So a desktop entry's StartupWMClass can match this window. That is the DESKTOP ENVIRONMENT
    // half of being identifiable -- it is what lets a dock or a taskbar tie a running window back
    // to the installed .desktop file and its themed icon. The window manager half is _NET_WM_ICON,
    // published below, which needs no desktop entry at all. BOTH halves are the class token, not
    // the human title. ICCCM gives res_name as the name the program was invoked with and res_class
    // as the general class of application, and a desktop entry's StartupWMClass is matched against
    // one or the other depending on whose implementation is reading it -- so making them the same
    // string removes the question.
    std::vector<char> resName(wmClass.begin(), wmClass.end());
    resName.push_back('\0');
    std::vector<char> resClass(wmClass.begin(), wmClass.end());
    resClass.push_back('\0');
    XClassHint classHint = {};
    classHint.res_name = resName.data();
    classHint.res_class = resClass.data();
    XSetClassHint(mDpy, mWin, &classHint);

    applySizeHints();

    if (parent) {
        // The two properties that make a window manager treat this as a dialog belonging to the
        // graph rather than as a second application: WM_TRANSIENT_FOR is ICCCM and is what keeps
        // it stacked above its parent and off the task list, and _NET_WM_WINDOW_TYPE_DIALOG is
        // EWMH and is what gets it centred on the parent and given a dialog's decorations.
        // Setting only one of them works on some window managers and not others.
        XSetTransientForHint(mDpy, mWin, parent->mWin);

        const Atom type = XInternAtom(mDpy, "_NET_WM_WINDOW_TYPE", False);
        const Atom dialog = XInternAtom(mDpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
        XChangeProperty(mDpy, mWin, type, XA_ATOM, 32, PropModeReplace,
                        reinterpret_cast<const unsigned char *>(&dialog), 1);
    }

    // NOTHING IS ADDED HERE FOR THE WHEEL, and that is deliberate. ButtonPressMask already
    // delivers a wheel notch: X reports one as a press and a release of button 4 or 5, and
    // handleEvent() turns the press into a scroll callback.
    //
    // The obvious-looking addition is Button4Mask | Button5Mask, and it is a mistake. Those are
    // MODIFIER STATE bits, not event mask bits -- cites: X11/X.h:249-250, Button4Mask (1<<11) and
    // Button5Mask (1<<12) sit in the same block as Button1Mask and ShiftMask. In the event mask
    // those same values are Button4MotionMask and Button5MotionMask (cites: X11/X.h:162-163), so
    // asking for them requests motion reporting while the wheel "button" is held, which is not a
    // thing that happens. It compiles, it changes no behaviour, and it reads as though the wheel
    // needed enabling.
    XSelectInput(mDpy, mWin,
                 ExposureMask | StructureNotifyMask | ButtonPressMask | ButtonReleaseMask |
                     PointerMotionMask | KeyPressMask | LeaveWindowMask | FocusChangeMask);

    // Before the map, because a window manager reads a new window's properties when it is mapped
    // and is not obliged to notice one that turns up afterwards.
    publishIcon(mDpy, mWin, mIconData, mIconWords);

    mWmDelete = XInternAtom(mDpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(mDpy, mWin, &mWmDelete, 1);
    XMapWindow(mDpy, mWin);

    // THE ROUND TRIP. XCreateWindow is asynchronous: if it was rejected, nothing above has failed
    // yet and the first symptom would be an unrelated error against a window id that never
    // existed. Sample, sync, sample.
    XSync(mDpy, False);
    if (errorCount() != before) {
        fprintf(stderr, "jack-graph: the X server rejected the window (see the error above)\n");
        close();
        return false;
    }

    mTarget = cairo_xlib_surface_create(mDpy, mWin, DefaultVisual(mDpy, screen), w, h);
    if (cairo_surface_status(mTarget) != CAIRO_STATUS_SUCCESS) {
        fprintf(stderr, "jack-graph: could not create the drawing surface\n");
        close();
        return false;
    }

    openInputMethod();
    return true;
}

//------------------------------------------------------------------------
// The input method. Both windows need it for one field -- the recorder's filename -- and it is
// opened unconditionally because a per-window switch would be a switch somebody has to remember
// to set on the window that grew a field later.
//
// jack-graph sets no XMODIFIERS of its own, so XOpenIM falls back to Xlib's built-in local input
// method: it reads the X keyboard mapping and the Compose file and needs no daemon.
//
// The caller must have done setlocale() and XSetLocaleModifiers() before open() -- main.cpp does.
// If either the locale is unsupported or the IM will not open, mXic stays null and the key path
// falls back to XLookupString, which is Latin-1 and has no dead keys. That is a degradation, not a
// failure: a filename in ASCII still works.
void X11Window::openInputMethod()
{
    if (!XSupportsLocale()) {
        fprintf(stderr, "jack-graph: X does not support the current locale; "
                        "dead keys and Compose will not work\n");
        return;
    }

    mXim = XOpenIM(mDpy, nullptr, nullptr, nullptr);
    if (!mXim) {
        fprintf(stderr, "jack-graph: no X input method; dead keys and Compose will not work\n");
        return;
    }

    // XIMPreeditNothing | XIMStatusNothing is the style that needs no preedit or status window of
    // its own, which is the only style we can honour: there is nowhere to put one.
    mXic = XCreateIC(mXim, XNInputStyle, XIMPreeditNothing | XIMStatusNothing, XNClientWindow, mWin,
                     XNFocusWindow, mWin, nullptr);
    if (!mXic) {
        fprintf(stderr, "jack-graph: could not create an input context; "
                        "dead keys and Compose will not work\n");
        XCloseIM(mXim);
        mXim = nullptr;
        return;
    }
}

void X11Window::closeInputMethod()
{
    if (mXic) {
        XDestroyIC(mXic);
        mXic = nullptr;
    }
    if (mXim) {
        XCloseIM(mXim);
        mXim = nullptr;
    }
}

//------------------------------------------------------------------------
void X11Window::releaseBuffer()
{
    if (mBuffer) {
        cairo_surface_destroy(mBuffer);
        mBuffer = nullptr;
    }
    mBufferW = mBufferH = 0;
}

void X11Window::close()
{
    // Close our own dialogs first: they hold our Window as their WM_TRANSIENT_FOR and would
    // otherwise be destroyed after it.
    const std::vector<X11Window *> dialogs = mDialogs;
    mDialogs.clear();
    for (X11Window *d : dialogs) {
        d->mParent = nullptr;
        d->close();
    }

    // Unregister from a parent that is still alive, so its loop does not dispatch to a window
    // that is gone.
    if (mParent) {
        std::vector<X11Window *> &siblings = mParent->mDialogs;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), this), siblings.end());
        mParent->mDirty = true;
        mParent = nullptr;
    }

    closeInputMethod();
    releaseBuffer();
    if (mTarget) {
        cairo_surface_destroy(mTarget);
        mTarget = nullptr;
    }
    if (mDpy) {
        if (mWin) {
            XDestroyWindow(mDpy, mWin);
            mWin = 0;
        }
        // A DIALOG SHARES THE PARENT'S CONNECTION. Unregistering the error handler or closing the
        // display here would take the main window down with the dialog.
        if (mOwnsDisplay) {
            unregisterDisplay(mDpy);
            XCloseDisplay(mDpy);
        } else {
            XFlush(mDpy);
        }
        mDpy = nullptr;
    }
    mActive = nullptr;
    mFonts = nullptr;
}

//------------------------------------------------------------------------
void X11Window::paint(const Callbacks &cb)
{
    if (!cb.draw || !mTarget || !mFonts)
        return;

    const int pw = pixels(mLogicalW, mScale);
    const int ph = pixels(mLogicalH, mScale);
    if (pw <= 0 || ph <= 0)
        return;

    // Compose offscreen. ARGB32 is premultiplied -- the one Cairo convention not pinned in Canvas,
    // because it belongs to whoever creates the surface, which is here.
    //
    // The buffer is KEPT between frames and reallocated only when the size changes. The window
    // this was ported from allocated one per frame, which is invisible on a panel that repaints
    // when something is clicked and is thirty allocations a second behind a dragged graph box.
    if (!mBuffer || mBufferW != pw || mBufferH != ph) {
        releaseBuffer();
        mBuffer = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pw, ph);
        if (cairo_surface_status(mBuffer) != CAIRO_STATUS_SUCCESS) {
            releaseBuffer();
            return;
        }
        mBufferW = pw;
        mBufferH = ph;
    }

    cairo_t *cr = cairo_create(mBuffer);
    // Reused surfaces hold the last frame, so clear before composing. CAIRO_OPERATOR_CLEAR rather
    // than painting the background colour: the panel paints its own ground and this only has to
    // guarantee nothing survives from the frame before.
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_restore(cr);
    // ONE scale, here. Nothing downstream of this knows the scale exists.
    cairo_scale(cr, mScale, mScale);
    {
        Canvas canvas(cr, mFonts, mLogicalW, mLogicalH);
        cb.draw(canvas);
    }
    cairo_destroy(cr);

    // Blit once, SOURCE not OVER: the buffer is the frame, not a layer on top of the last one.
    cairo_t *out = cairo_create(mTarget);
    cairo_set_source_surface(out, mBuffer, 0, 0);
    cairo_set_operator(out, CAIRO_OPERATOR_SOURCE);
    cairo_paint(out);
    cairo_destroy(out);

    cairo_surface_flush(mTarget);
    XFlush(mDpy);
}

//------------------------------------------------------------------------
// Telling the window manager the real range on both axes, rather than pinning both, is what lets
// resize() below be honoured instead of fought. An axis whose min equals its max is pinned, and
// that is exactly how a fixed-width window is expressed.
void X11Window::applySizeHints()
{
    if (!mDpy || !mWin)
        return;
    XSizeHints *hints = XAllocSizeHints();
    if (!hints)
        return;
    hints->flags = PMinSize | PMaxSize;
    hints->min_width = pixels(mMinW, mScale);
    hints->max_width = pixels(mMaxW, mScale);
    hints->min_height = pixels(mMinH, mScale);
    hints->max_height = pixels(mMaxH, mScale);
    XSetWMNormalHints(mDpy, mWin, hints);
    XFree(hints);
}

//------------------------------------------------------------------------
void X11Window::resize(float logicalW, float logicalH)
{
    if (!mDpy || !mWin)
        return;

    const float w = std::clamp(logicalW, mMinW, mMaxW);
    const float h = std::clamp(logicalH, mMinH, mMaxH);

    const int oldW = pixels(mLogicalW, mScale);
    const int oldH = pixels(mLogicalH, mScale);
    const int newW = pixels(w, mScale);
    const int newH = pixels(h, mScale);

    mLogicalW = w;
    mLogicalH = h;
    if (newW == oldW && newH == oldH)
        return;

    // The hints go FIRST. A window manager that is still holding the old max may refuse the resize
    // request, and then the surface and the window would disagree about the size.
    applySizeHints();
    XResizeWindow(mDpy, mWin, static_cast<unsigned>(newW), static_cast<unsigned>(newH));

    // cites: cairo-xlib.h -- the surface must be told, it does not track the drawable.
    if (mTarget)
        cairo_xlib_surface_set_size(mTarget, newW, newH);

    mDirty = true;
}

//------------------------------------------------------------------------
int X11Window::addFd(int fd, std::function<void()> onReady, bool wantWrite)
{
    if (fd < 0)
        return -1;
    const int token = mNextToken++;
    mFds.push_back({token, fd, wantWrite, std::move(onReady)});
    return token;
}

void X11Window::removeFd(int token)
{
    for (size_t i = 0; i < mFds.size(); ++i) {
        if (mFds[i].token == token) {
            mFds.erase(mFds.begin() + static_cast<long>(i));
            return;
        }
    }
}

int X11Window::addTimer(int intervalMs, std::function<void()> fn)
{
    if (intervalMs <= 0)
        return -1;
    const int token = mNextToken++;
    const long interval = static_cast<long>(intervalMs) * 1000000L;
    mTimers.push_back({token, interval, nowNs() + interval, std::move(fn)});
    return token;
}

void X11Window::removeTimer(int token)
{
    for (size_t i = 0; i < mTimers.size(); ++i) {
        if (mTimers[i].token == token) {
            mTimers.erase(mTimers.begin() + static_cast<long>(i));
            return;
        }
    }
}

//------------------------------------------------------------------------
// One event, for THIS window. Returns false when the event was not ours, which is how run()
// hands it on to a dialog.
//
// SETS STATE, NEVER PAINTS. A drag generates a MotionNotify per pixel and each one would
// otherwise be a full recompose.
bool X11Window::handleEvent(XEvent &ev)
{
    if (ev.xany.window != mWin)
        return false;

    const Callbacks *cb = mActive;

    switch (ev.type) {
        case Expose:
            mDirty = true;
            break;

        case ClientMessage:
            if (static_cast<Atom>(ev.xclient.data.l[0]) == mWmDelete) {
                if (cb && cb->closed)
                    cb->closed();
                if (mOwnsDisplay)
                    mRunning = false;
                else
                    mCloseRequested = true;
            }
            break;

        case ConfigureNotify: {
            // The surface does not track the drawable on its own, so a size it was not told about
            // is drawn at the old size and clipped.
            //
            // Acted on whatever the source: this arrives as confirmation of our own XResizeWindow,
            // and also when a window manager that does not honour the size hints gives us
            // something else. BELIEVING THE SERVER OVER OUR OWN INTENT is what keeps the two from
            // disagreeing -- and it is why a tiling window manager, which ignores size hints as a
            // matter of policy, gets a correctly drawn window rather than a clipped one.
            const int pw = pixels(mLogicalW, mScale);
            const int ph = pixels(mLogicalH, mScale);
            if (ev.xconfigure.width != pw || ev.xconfigure.height != ph) {
                if (mTarget)
                    cairo_xlib_surface_set_size(mTarget, ev.xconfigure.width,
                                                ev.xconfigure.height);
                mLogicalW = static_cast<float>(ev.xconfigure.width) / mScale;
                mLogicalH = static_cast<float>(ev.xconfigure.height) / mScale;
                mDirty = true;
                if (cb && cb->resized)
                    cb->resized(mLogicalW, mLogicalH);
            }
            break;
        }

        case ButtonPress:
        case ButtonRelease: {
            const unsigned b = ev.xbutton.button;
            const float x = static_cast<float>(ev.xbutton.x) / mScale;
            const float y = static_cast<float>(ev.xbutton.y) / mScale;

            // THE WHEEL IS NOT A BUTTON, whatever X thinks. X reports a notch as a press and a
            // release of button 4 or 5; a panel that saw those as buttons would get a phantom
            // click at the pointer every time somebody scrolled. Only the press is forwarded, as
            // one scroll event.
            if (b == Button4 || b == Button5) {
                if (ev.type == ButtonPress && cb && cb->scroll)
                    cb->scroll(x, y, b == Button4 ? -1 : 1);
                break;
            }
            if (cb && cb->button)
                cb->button(x, y, static_cast<int>(b), ev.type == ButtonPress);
            break;
        }

        case MotionNotify:
            if (cb && cb->motion) {
                cb->motion(static_cast<float>(ev.xmotion.x) / mScale,
                           static_cast<float>(ev.xmotion.y) / mScale);
            }
            break;

        case LeaveNotify:
            // A pointer that left without a ButtonRelease would otherwise leave a control latched
            // in its hover state.
            if (cb && cb->motion)
                cb->motion(-1.0f, -1.0f);
            break;

        case FocusIn:
            // The input context follows the focus. Without this a dialog that has been clicked
            // into still routes its keystrokes through the parent's context.
            if (mXic)
                XSetICFocus(mXic);
            break;

        case FocusOut:
            if (mXic)
                XUnsetICFocus(mXic);
            break;

        case KeyPress: {
            KeySym sym = NoSymbol;
            char buf[64];
            int len = 0;

            if (mXic) {
                Status status = XLookupNone;
                len = Xutf8LookupString(mXic, &ev.xkey, buf, sizeof(buf) - 1, &sym, &status);
                // XLookupBoth and XLookupChars carry text; XLookupKeySym carries only the symbol.
                // Anything else produced neither.
                if (status != XLookupChars && status != XLookupBoth)
                    len = 0;
                if (status != XLookupKeySym && status != XLookupBoth)
                    sym = NoSymbol;
            } else {
                // No input method: Latin-1 only, no dead keys. Warned about at open().
                len = XLookupString(&ev.xkey, buf, sizeof(buf) - 1, &sym, nullptr);
            }

            if (len < 0 || len > static_cast<int>(sizeof(buf) - 1))
                len = 0;
            buf[len] = '\0';

            const Key k = keyFromSym(sym);
            const bool handled = (cb && cb->key) ? cb->key(k, buf, len, ev.xkey.state) : false;
            if (!handled && k == Key::Escape) {
                if (cb && cb->closed)
                    cb->closed();
                if (mOwnsDisplay)
                    mRunning = false;
                else
                    mCloseRequested = true;
            }
            break;
        }

        default:
            break;
    }
    return true;
}

//------------------------------------------------------------------------
// Dialogs that asked to close, closed at a point where nothing is iterating them. A dialog that
// destroyed its own window from inside its own button handler would pull the ground out from under
// the dispatch loop, so requestClose() only sets a flag and this is where it is honoured.
void X11Window::reapDialogs()
{
    for (size_t i = 0; i < mDialogs.size();) {
        X11Window *d = mDialogs[i];
        if (!d->mCloseRequested) {
            ++i;
            continue;
        }
        // close() removes it from mDialogs itself, via mParent, so the index is not advanced.
        d->close();
        mDirty = true;
        if (i < mDialogs.size() && mDialogs[i] == d)
            ++i; // defensive: it did not remove itself after all
    }
}

//------------------------------------------------------------------------
void X11Window::run(const Callbacks &cb)
{
    if (!mDpy || !mWin)
        return;

    mRunning = true;
    mDirty = true;
    mActive = &cb;

    const int xfd = ConnectionNumber(mDpy);

    while (mRunning) {
        // Drain everything the server has for us first.
        while (XPending(mDpy)) {
            XEvent ev;
            XNextEvent(mDpy, &ev);

            // The input method gets first refusal. A dead-key press, and every key that makes up a
            // Compose sequence, is consumed here and reappears later as the composed character on
            // the key that completes it. Skipping this is why a naive X client cannot type an
            // umlaut.
            if (XFilterEvent(&ev, None))
                continue;

            // DISPATCH BY WINDOW. One display and one loop serve the main window and any dialog,
            // which is the whole point of the shared connection: gtk_dialog_run's nested main loop
            // is what this replaces.
            if (handleEvent(ev))
                continue;
            for (X11Window *d : mDialogs) {
                if (d->handleEvent(ev))
                    break;
            }
        }

        if (!mRunning)
            break;

        reapDialogs();

        if (mDirty) {
            mDirty = false;
            paint(cb);
        }
        for (X11Window *d : mDialogs) {
            if (d->mDirty && d->mActive) {
                d->mDirty = false;
                d->paint(*d->mActive);
            }
        }

        // Fire every timer that is due. THIS HAPPENS BEFORE THE WAIT AND UNCONDITIONALLY, which is
        // the difference from the window Audio-Gui was ported from: that one fired its single tick
        // only when select() returned 0, so any continuous stream of events -- a drag, in practice
        // -- reset the timeout every pass and the tick never ran at all. Here the deadline is
        // absolute and checked on its own terms, so dragging a graph box cannot stop the device
        // poll.
        long now = nowNs();
        {
            // A copy, because a timer's handler may add or remove timers.
            const std::vector<TimerEntry> due = mTimers;
            for (const TimerEntry &t : due) {
                if (t.dueNs > now)
                    continue;
                bool live = false;
                for (TimerEntry &cur : mTimers) {
                    if (cur.token != t.token)
                        continue;
                    // Re-arm from NOW rather than from the old deadline. Adding the interval to a
                    // deadline already in the past makes a timer that fell behind -- because a
                    // paint ran long, or the machine suspended -- try to catch up by firing back
                    // to back.
                    cur.dueNs = now + cur.intervalNs;
                    live = true;
                    break;
                }
                if (live && t.fn)
                    t.fn();
            }
        }

        if (!mRunning)
            break;

        reapDialogs();

        // A timer handler may have dirtied a window or pushed more X requests out. Paint before
        // sleeping rather than after waking.
        if (mDirty) {
            mDirty = false;
            paint(cb);
        }
        for (X11Window *d : mDialogs) {
            if (d->mDirty && d->mActive) {
                d->mDirty = false;
                d->paint(*d->mActive);
            }
        }

        // Wait for the next event, descriptor or deadline, whichever comes first. XPending above
        // may have left events buffered inside Xlib that never reach the fd, so it is checked
        // again rather than slept through.
        if (XPending(mDpy))
            continue;

        fd_set r;
        fd_set wr;
        FD_ZERO(&r);
        FD_ZERO(&wr);
        FD_SET(xfd, &r);
        int maxFd = xfd;
        for (const FdWatch &w : mFds) {
            if (w.fd < 0 || w.fd >= FD_SETSIZE)
                continue;
            FD_SET(w.fd, w.write ? &wr : &r);
            if (w.fd > maxFd)
                maxFd = w.fd;
        }

        // Sleep only until the earliest deadline. With no timers at all, block indefinitely rather
        // than spinning on a zero timeout.
        now = nowNs();
        bool haveTimer = false;
        long waitNs = 0;
        for (const TimerEntry &t : mTimers) {
            const long left = t.dueNs - now;
            if (!haveTimer || left < waitNs) {
                haveTimer = true;
                waitNs = left;
            }
        }

        struct timeval tv;
        struct timeval *timeout = nullptr;
        if (haveTimer) {
            // A deadline can already be in the past -- a paint that ran long, a handler that
            // blocked. That is a zero timeout, not a negative one, and certainly not the "block
            // forever" a null timeout would mean.
            if (waitNs < 0)
                waitNs = 0;
            tv.tv_sec = waitNs / 1000000000L;
            tv.tv_usec = (waitNs % 1000000000L) / 1000L;
            timeout = &tv;
        }

        const int n = select(maxFd + 1, &r, &wr, nullptr, timeout);
        if (n < 0 && errno != EINTR) {
            fprintf(stderr, "jack-graph: select on the X connection failed; closing\n");
            mRunning = false;
            break;
        }
        if (n <= 0)
            continue; // timed out, or interrupted: back round to the deadline check

        // Dispatch from a COPY: a handler may add or remove descriptors. The mixer's does, every
        // time the output device changes and the mixer is reopened onto another card, and
        // libdbus's watch functions do during authentication -- which would otherwise invalidate
        // the iterator underneath this loop.
        const std::vector<FdWatch> ready = mFds;
        for (const FdWatch &w : ready) {
            if (w.fd < 0 || w.fd >= FD_SETSIZE)
                continue;
            if (!FD_ISSET(w.fd, w.write ? &wr : &r))
                continue;
            // Still registered? A previous handler in this same pass may have removed it.
            bool live = false;
            for (const FdWatch &cur : mFds) {
                if (cur.token == w.token) {
                    live = true;
                    break;
                }
            }
            if (live && w.onReady)
                w.onReady();
        }
    }

    mActive = nullptr;
}

} // namespace jackgraph
