// jack-graph -- the JACK connection graph, its toolbar, and its two dialog windows.
//
// THE ONLY PLACE THE WINDOWS AND THE APPLICATION MEET. app.h includes no X11 header and the window
// knows nothing about JACK, ALSA or the config; everything below is the wiring between them.
// Handlers never paint: they set a dirty flag, and one invalidate() per handler turns into one
// repaint per pass round the loop.
//
// THREE WINDOWS, ONE LOOP. The graph is the main window; JACK Settings and About are dialogs opened
// with X11Window::openDialog, which share the display, the font stack and this one select(). That
// is the whole reason openDialog exists: Gtk::Dialog::run() spun a NESTED main loop for each of
// them, and a nested loop is precisely what this port is getting rid of. Nothing here is modal.
//
// A dialog is asked to close, never closed: X11Window::requestClose() sets a flag that the loop
// honours between passes, because a window that destroyed itself from inside its own button handler
// would pull the ground out from under the dispatch it is in the middle of.

#include "app.h"
#include "chrome.h"
#include "graphgeometry.h"
#include "platform/wakepipe.h"
#include "platform/x11window.h"

#include <X11/Xlib.h>

#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

using namespace jackgraph;

namespace
{

float scaleFromArgs(int argc, char **argv)
{
    // --scale, then $JACK_GRAPH_SCALE, then 1. There is no automatic DPI detection: Xft.dpi,
    // RandR's physical size and the toolkit-specific overrides disagree often enough that guessing
    // produces a window the user cannot correct, and one flag is easy to put in a desktop entry.
    const char *v = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--scale") == 0 && i + 1 < argc)
            v = argv[i + 1];
    }
    if (!v)
        v = getenv("JACK_GRAPH_SCALE");
    if (!v || !*v)
        return 1.0f;

    char *end = nullptr;
    const double d = strtod(v, &end);
    if (end == v || d <= 0.0)
        return 1.0f;
    if (d < geo::kScaleMin)
        return geo::kScaleMin;
    if (d > geo::kScaleMax)
        return geo::kScaleMax;
    return static_cast<float>(d);
}

} // namespace

int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            printf("usage: jack-graph [--scale N] [--version]\n"
                   "  --scale N   render at N times the logical size (%.2f..%.2f);\n"
                   "              also read from $JACK_GRAPH_SCALE\n"
                   "  --version   print the version and exit\n",
                   static_cast<double>(geo::kScaleMin), static_cast<double>(geo::kScaleMax));
            return 0;
        }
        if (std::strcmp(argv[i], "--version") == 0) {
            printf("jack-graph %s\n", JACKGRAPH_VERSION);
            return 0;
        }
    }

    // Before any window opens its input method. No field in this window takes text, but X11Window
    // opens an XIC unconditionally, and XOpenIM reads the locale.
    setlocale(LC_ALL, "");
    if (!XSetLocaleModifiers(""))
        fprintf(stderr, "jack-graph: could not set the X locale modifiers\n");

    // THE WAKE PIPE, BEFORE ANY JACK CLIENT EXISTS AND BEFORE jackd IS EVER FORKED. It is the hop
    // that carries JACK's port, xrun and shutdown callbacks off JACK's own threads and onto this
    // loop -- the mechanism Glib::Dispatcher provided in the GTK build -- and its SIGCHLD handler
    // is what reaps the jackd that Settings -> Start launches. app.h explains both.
    wakepipe::install();

    App app;
    // Before the window: its size is in here.
    app.loadConfig();

    X11Window win;

    // FREELY RESIZABLE, on both axes: bounds on both, so ConfigureNotify updates both logical
    // extents and the canvas grows with the window. It opens at the size it was closed at.
    X11Window::Geometry g;
    const auto clampTo = [](int v, float lo, float hi) {
        const float f = static_cast<float>(v);
        return f < lo ? lo : (f > hi ? hi : f);
    };
    g.logicalW = clampTo(app.config().get_window_width(), geo::kGraphMinW, geo::kGraphMaxW);
    g.logicalH = clampTo(app.config().get_window_height(), geo::kGraphMinH, geo::kGraphMaxH);
    g.minW = geo::kGraphMinW;
    g.maxW = geo::kGraphMaxW;
    g.minH = geo::kGraphMinH;
    g.maxH = geo::kGraphMaxH;
    g.scale = scaleFromArgs(argc, argv);

    if (!win.open("Jack Graph", "jack-graph", g))
        return 1;

    app.onNeedsRepaint = [&win] { win.invalidate(); };
    app.addTimer = [&win](int ms, std::function<void()> fn) {
        return win.addTimer(ms, std::move(fn));
    };
    app.removeTimer = [&win](int token) { win.removeTimer(token); };

    // The JACK threads' one byte, arriving on the same select() as everything else.
    wakepipe::setPostHandler([&app] { app.onPostFromJackThread(); });
    win.addFd(wakepipe::readFd(), [] { wakepipe::drain(); });

    //--------------------------------------------------------------------
    // The JACK Settings window.
    //
    // DECLARED BEFORE the window that uses them and destroyed after it: X11Window::setCallbacks
    // keeps a POINTER to the Callbacks struct, so it has to outlive the window, and the window has
    // to outlive nothing at all.
    SettingsPanel *setPanel = nullptr;
    X11Window::Callbacks setCb;
    std::unique_ptr<X11Window> setWin;

    setCb.draw = [&setPanel](Canvas &c) {
        if (setPanel)
            setPanel->draw(c);
    };
    setCb.button = [&setPanel](float x, float y, int button, bool pressed) {
        if (!setPanel)
            return;
        if (pressed)
            setPanel->press(x, y, button);
        else
            setPanel->release(x, y, button);
    };
    setCb.motion = [&setPanel](float x, float y) {
        if (setPanel)
            setPanel->motion(x, y);
    };
    setCb.key = [&setPanel](Key k, const char *text, int len, unsigned state) {
        return setPanel ? setPanel->key(k, text, len, state) : false;
    };
    // The window manager closed it, or an unhandled Escape did. App tears down its side and asks
    // this window to close; the request is honoured between passes round the loop.
    setCb.closed = [&app] { app.closeSettings(); };

    app.onOpenSettings = [&] {
        SettingsPanel *p = app.openSettings();
        if (!p)
            return; // already open

        // Drops the previous, ALREADY-CLOSED window. Never the open one: app.openSettings() only
        // hands back a panel when there is no window up.
        setWin.reset(new X11Window());

        X11Window::Geometry dg;
        dg.logicalW = geo::kSetW;
        dg.logicalH = p->layout(); // pinned on both axes: no bounds given
        dg.scale = win.scale();
        if (!setWin->openDialog(win, "JACK Settings", "jack-graph", dg)) {
            setWin.reset();
            app.closeSettings();
            return;
        }

        setPanel = p;
        p->onNeedsRepaint = [&setWin] {
            if (setWin)
                setWin->invalidate();
        };
        setWin->setCallbacks(setCb);
    };

    app.onCloseSettings = [&] {
        setPanel = nullptr;
        if (setWin)
            setWin->requestClose();
    };

    //--------------------------------------------------------------------
    // The About card. Four strings and a Close pill, which is what Gtk::AboutDialog was given.
    AboutCard about;
    bool aboutOpen = false;
    X11Window::Callbacks aboutCb;
    std::unique_ptr<X11Window> aboutWin;

    aboutCb.draw = [&about](Canvas &c) { about.draw(c); };
    aboutCb.button = [&about](float x, float y, int button, bool pressed) {
        if (pressed)
            about.press(x, y, button);
        else
            about.release(x, y, button);
    };
    aboutCb.motion = [&about](float x, float y) { about.motion(x, y); };
    aboutCb.closed = [&aboutOpen] { aboutOpen = false; };

    about.onNeedsRepaint = [&aboutWin] {
        if (aboutWin)
            aboutWin->invalidate();
    };
    about.onClose = [&] {
        aboutOpen = false;
        if (aboutWin)
            aboutWin->requestClose();
    };

    app.onOpenAbout = [&] {
        if (aboutOpen)
            return;

        const float h = about.layout();
        aboutWin.reset(new X11Window());

        X11Window::Geometry dg;
        dg.logicalW = geo::kAboutW;
        dg.logicalH = h;
        dg.scale = win.scale();
        if (!aboutWin->openDialog(win, "About Jack Graph", "jack-graph", dg)) {
            aboutWin.reset();
            return;
        }
        aboutOpen = true;
        aboutWin->setCallbacks(aboutCb);
    };

    //--------------------------------------------------------------------
    // The panel needs its rect before start(), because start() fits the graph to the window and
    // fitToWindow() measures against that rect. The GTK build deferred the fit to a Glib::signal_idle
    // to wait for GTK's allocation; here the size is known the moment the window is open.
    app.setSize(win.logicalWidth(), win.logicalHeight());

    if (!app.start())
        return 1;

    // The ALSA sequencer's announce events -- a MIDI device plugged in, a port subscribed from a
    // shell with aconnect -- on the same select(). Registered after start(), which opens it.
    if (app.alsaFd() >= 0)
        win.addFd(app.alsaFd(), [&app] { app.onAlsaReadable(); });

    X11Window::Callbacks cb;

    cb.draw = [&app](Canvas &c) { app.draw(c); };

    cb.button = [&app](float x, float y, int button, bool pressed) {
        app.button(x, y, button, pressed);
    };

    // Motion does NOT invalidate unconditionally: the graph panel and the toolbar call
    // onNeedsRepaint themselves, and only when a drag moved something or a hover changed. Marking
    // every pixel dirty would recompose the whole window for a pointer crossing it.
    cb.motion = [&app](float x, float y) { app.motion(x, y); };

    cb.scroll = [&app](float x, float y, int dir) { app.scroll(x, y, dir); };

    cb.resized = [&app](float w, float h) { app.setSize(w, h); };

    win.run(cb);

    // BEFORE ANYTHING IN THIS FUNCTION IS DESTROYED: the callbacks wired above point at `win` and
    // at the two dialog windows, all of which are destroyed before `app` is -- a use-after-free
    // AddressSanitizer reports on every exit if this is left to the destructors. This severs them first, then drops the JACK client and saves the config --
    // which is what ~JackGraph and on_delete_event did between them.
    app.shutdown();
    return 0;
}
