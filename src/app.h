// Everything jack-graph is, minus the windows.
//
// NO X11 HEADER HERE, and none in app.cpp. This is JackGraph.cpp with Gtk::Window taken out from
// under it: it owns the JACK client, the ALSA sequencer client, the config, the server control,
// the graph panel, the window chrome and the settings panel, and it turns the chrome's semantic
// events into model calls and the models' changes back into panel content.
//
// IT OWNS NO DESCRIPTOR AND NO LOOP. It owns timers, but only through the addTimer / removeTimer
// functions main.cpp gives it, and the ALSA sequencer's descriptor is handed to main.cpp to watch.
//
//------------------------------------------------------------------------------------------------
// THE THREE JACK CALLBACKS ARRIVE ON JACK'S OWN THREADS AND NONE OF THEM MAY TOUCH ANYTHING HERE.
//
// JackClient's header is explicit about the shutdown one: "the handler must not touch the client or
// any widget; marshal to the main loop first". platform/wakepipe.h is a self-pipe already in the
// main loop's select(), so the hop costs no second descriptor and no second mechanism -- it is what
// Glib::Dispatcher was.
//
// The GTK build did not marshal the port and xrun callbacks at all: they called
// Glib::signal_timeout().connect() and Glib::signal_idle().connect_once() straight from JACK's
// notification thread, which GLib does not promise to survive. All three go through the pipe now.
// Each handler sets an atomic flag and writes one byte, and onPostFromJackThread() -- which runs on
// the main loop -- is the only place that acts.
//
// The 100 ms coalescer is unchanged: a JACK restart registers dozens of ports in a burst, and the
// flag plus a one-shot timer collapses the burst into one refresh.
//
//------------------------------------------------------------------------------------------------
// STARTING AND STOPPING jackd NEVER BLOCKS.
//
// SettingsDialog::on_start() slept in half-second steps for up to five seconds waiting for the
// server, and on_stop() for up to ten waiting for it to exit, with both windows frozen. Now:
//
//   Start: JackServerControl::spawn() forks jackd and returns at once. A 250 ms timer asks whether
//          the server answers yet, for up to ten seconds; the wake pipe reports an early exit --
//          a busy device, a bad rate -- the moment it happens, with jackd's own words.
//   Stop:  our client is dropped first, then SIGTERM; the wake pipe (our child) or the timer (a
//          jackd adopted from an earlier session) sees it go, and after five seconds the timer
//          escalates to SIGKILL.
//
// mServerOp says which is in flight, and the settings panel disables everything while one is.

#pragma once

#include "AlsaClient.hpp"
#include "Config.hpp"
#include "JackClient.hpp"
#include "JackServerControl.hpp"
#include "chrome.h"
#include "gfx/canvas.h"
#include "graphpanel.h"
#include "settingspanel.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace jackgraph
{

class App
{
public:
    App();

    // Read the config -- before the window opens, because the window's size comes from it.
    void loadConfig();
    const Config &config() const
    {
        return mConfig;
    }

    // Connect to JACK and to the ALSA sequencer, read the graph and fit it to the window. A
    // stopped JACK server is not a failure: that is what Settings -> Start is for.
    bool start();

    // The other half of start(), and not optional tidying: the callbacks below point into main()'s
    // locals, which are destroyed before this object is. Severs the outward callbacks, drops the
    // JACK client and saves the config -- what ~JackGraph and on_delete_event did between them.
    //
    // It does NOT stop jackd. The server outlives the window on purpose, and its PID file lets the
    // next session adopt it.
    void shutdown();

    //--- what main.cpp gives us -----------------------------------------
    std::function<int(int intervalMs, std::function<void()> fn)> addTimer;
    std::function<void(int token)> removeTimer;

    std::function<void()> onNeedsRepaint;

    // The Settings and About pills. main.cpp owns the two dialog windows, because opening one is
    // the one thing in this file that needs X11.
    std::function<void()> onOpenSettings;
    std::function<void()> onOpenAbout;
    // The settings window should go away. Deferred, always: this fires from inside the panel's own
    // release handler, so the window may only be asked to close, never closed here.
    std::function<void()> onCloseSettings;

    //--- the settings window --------------------------------------------
    // Builds the panel, wires its hooks and reloads it. Returns null if one is already open, which
    // is what stops the pill opening a second window.
    SettingsPanel *openSettings();
    void closeSettings();
    SettingsPanel *settings()
    {
        return mSettingsOpen ? mSettings.get() : nullptr;
    }

    //--- the ALSA sequencer ---------------------------------------------
    // The descriptor main.cpp watches, or -1 with no sequencer.
    int alsaFd() const
    {
        return mAlsa.poll_fd();
    }
    void onAlsaReadable();

    // The status line's text, as drawn.
    const std::string &statusText() const
    {
        return mChrome.status();
    }

    //--- layout, paint, input -------------------------------------------
    // The window's logical size, at start-up and on every resize.
    void setSize(float w, float h);

    void draw(Canvas &c);
    void button(float x, float y, int button, bool pressed);
    void motion(float x, float y);
    void scroll(float x, float y, int dir);

    //--- the hop off JACK's threads -------------------------------------
    // Registered with wakepipe::setPostHandler. Runs on the main loop; the three flags are the
    // only thing JACK's threads touched.
    void onPostFromJackThread();

private:
    enum class ServerOp { None, Starting, Stopping };

    void attachJackCallbacks();
    bool connectJack();
    void disconnectJack();
    // forgetPositions drops the saved box positions first, so the rebuild lays the graph out by
    // the automatic rule instead of restoring the user's arrangement. Only Tool::Refresh passes
    // true; every automatic caller leaves the arrangement alone.
    void refreshPorts(bool forgetPositions = false);
    void schedulePortRefresh();
    void startReconnectPoll();
    bool tryReconnectJack();
    void handleServerGone();
    void updateStatus();
    void onTool(Tool t);
    void repaint() const
    {
        if (onNeedsRepaint)
            onNeedsRepaint();
    }

    bool connectNodes(const Node &src, const Node &dst);
    void disconnectNodes(const Node &src, const Node &dst);

    // The server, asynchronously -- see the header comment.
    void startServer(const JackSettings &settings);
    void stopServer();
    void pollServerOp();
    void endServerOp();
    void onJackdExited(int status);
    void pushServerState();
    void setServerMessage(const std::string &text, bool error);

    JackClient mJack;
    AlsaClient mAlsa;
    Config mConfig;
    JackServerControl mServer;

    GraphPanel mGraph;
    Chrome mChrome;
    std::unique_ptr<SettingsPanel> mSettings;

    bool mJackConnected = false;
    bool mAlsaConnected = false;

    // Written on JACK's notification threads, read and cleared on the main loop. Nothing else
    // crosses the boundary.
    std::atomic<bool> mShutdownPosted{false};
    std::atomic<bool> mPortsPosted{false};
    std::atomic<bool> mXrunPosted{false};

    // Main loop only, from here down.
    bool mRefreshPending = false;
    int mRefreshToken = -1;

    int mReconnectToken = -1;
    int mReconnectAttempts = 0;

    ServerOp mServerOp = ServerOp::None;
    int mServerOpToken = -1;
    int mServerOpTicks = 0;
    // The last thing worth telling the user about the server -- why a start failed, or that jackd
    // died -- kept here so a settings window opened afterwards can still show it.
    std::string mServerMessage;
    bool mServerMessageIsError = false;

    bool mSettingsOpen = false;
};

} // namespace jackgraph
