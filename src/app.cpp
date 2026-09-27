// See app.h.

#include "app.h"

#include "graphgeometry.h"
#include "gfx/palette.h"
#include "platform/wakepipe.h"

#include <cstdio>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>

namespace jackgraph
{

namespace
{
// Readiness and exit polls, in 250 ms ticks.
constexpr int kServerPollMs = 250;
constexpr int kStartTicks = 40; // 10 s for jackd to answer
constexpr int kKillTicks = 20;  // 5 s after SIGTERM, SIGKILL
constexpr int kGiveUpTicks = 40; // 10 s after SIGTERM, stop waiting
} // namespace

App::App() = default;

//------------------------------------------------------------------------
void App::loadConfig()
{
    mConfig.load();

    // jack-graph 1.x saved interfaces as hw:N,M, and card numbers move when a USB interface is
    // plugged in. Converted once, here, to the stable hw:CARD= form the device list now uses, so
    // the saved choice is still selected in the Interface field.
    const std::string iface = mConfig.get_interface();
    const std::string migrated = JackServerControl::migrate_device_id(iface);
    if (migrated != iface) {
        fprintf(stderr, "jack-graph: saved interface %s is now %s\n", iface.c_str(),
                migrated.c_str());
        mConfig.set_interface(migrated);
    }
}

bool App::start()
{
    mChrome.onNeedsRepaint = [this] { repaint(); };
    mChrome.onTool = [this](Tool t) { onTool(t); };
    mGraph.onNeedsRepaint = [this] { repaint(); };

    mGraph.onConnect = [this](const Node &src, const Node &dst) { return connectNodes(src, dst); };
    mGraph.onDisconnect = [this](const Node &src, const Node &dst) { disconnectNodes(src, dst); };

    // Connect to a JACK server that is already running. One that is not is not an error: that is
    // what Settings -> Start is for.
    mJackConnected = mJack.connect("jack-graph");
    if (mJackConnected) {
        fprintf(stderr, "jack-graph: connected to the running JACK server\n");
        attachJackCallbacks();
    } else {
        fprintf(stderr, "jack-graph: JACK is not running; use JACK Settings to start it\n");
    }

    mAlsaConnected = mAlsa.connect("jack-graph");

    refreshPorts();

    // FIT ON FIRST SHOW. The GTK build deferred this to a Glib::signal_idle because it had to wait
    // for GTK to allocate the widget; here setSize() has already given the panel its rect.
    mGraph.fitToWindow();
    return true;
}

void App::shutdown()
{
    // Sever everything pointing outward FIRST -- see the header.
    onNeedsRepaint = nullptr;
    onOpenSettings = nullptr;
    onOpenAbout = nullptr;
    onCloseSettings = nullptr;
    mChrome.onNeedsRepaint = nullptr;
    mChrome.onTool = nullptr;
    mGraph.onNeedsRepaint = nullptr;
    if (mSettings)
        mSettings->onNeedsRepaint = nullptr;

    // Then the timers, before addTimer/removeTimer stop being callable.
    if (removeTimer) {
        for (int token : {mRefreshToken, mReconnectToken, mServerOpToken}) {
            if (token >= 0)
                removeTimer(token);
        }
    }
    mRefreshToken = mReconnectToken = mServerOpToken = -1;
    addTimer = nullptr;
    removeTimer = nullptr;

    // jackd keeps running; this window just stops listening for it to exit.
    if (mServer.owned_is_child())
        wakepipe::forget(mServer.owned_pid());

    // And only then the client, whose shutdown callback would otherwise be able to post one more
    // byte into a handler that no longer has anything to do.
    wakepipe::setPostHandler(nullptr);
    mJack.disconnect();
    mJackConnected = false;
    mAlsa.disconnect();
    mAlsaConnected = false;
    mSettingsOpen = false;
    mSettings.reset();

    mConfig.save();
}

//------------------------------------------------------------------------
void App::attachJackCallbacks()
{
    // ONE PLACE THAT WIRES THESE, called from every path that opens a client -- the first
    // connect, the connect after Start, and the reconnect after the server went away -- so every
    // one of them registers the same set.
    //
    // ALL THREE RUN ON A JACK THREAD. They set a flag and write one byte; see the header.
    mJack.set_port_callback([this] {
        mPortsPosted.store(true);
        wakepipe::post();
    });
    mJack.set_xrun_callback([this] {
        mXrunPosted.store(true);
        wakepipe::post();
    });
    mJack.set_shutdown_callback([this] {
        mShutdownPosted.store(true);
        wakepipe::post();
    });
}

bool App::connectJack()
{
    mJackConnected = mJack.connect("jack-graph");
    if (mJackConnected) {
        attachJackCallbacks();
        refreshPorts();
    } else {
        updateStatus();
    }
    return mJackConnected;
}

void App::disconnectJack()
{
    if (mReconnectToken >= 0 && removeTimer)
        removeTimer(mReconnectToken);
    mReconnectToken = -1;
    if (mJack.is_connected())
        mJack.disconnect();
    mJackConnected = false;
}

void App::onPostFromJackThread()
{
    // Shutdown first: there is no point refreshing a graph off a client that has been orphaned, and
    // handleServerGone() refreshes it itself once the client is gone.
    if (mShutdownPosted.exchange(false))
        handleServerGone();
    if (mPortsPosted.exchange(false))
        schedulePortRefresh();
    if (mXrunPosted.exchange(false))
        updateStatus();
}

void App::onAlsaReadable()
{
    // Always drained, or the descriptor stays readable and the loop spins. Only acted on while the
    // ALSA ports are on screen, which is only while JACK is not.
    if (mAlsa.drain_events() && !mJackConnected)
        schedulePortRefresh();
}

// A burst of port registrations -- which is what a JACK restart is -- collapses into one refresh
// 100 ms later. The flag is what makes it one timer rather than one per port.
void App::schedulePortRefresh()
{
    if (mRefreshPending)
        return;
    if (!addTimer) {
        refreshPorts();
        return;
    }
    mRefreshPending = true;
    mRefreshToken = addTimer(100, [this] {
        // ONE SHOT: the timer removes itself. X11Window dispatches timers from a copy of its list
        // and re-checks that a timer is still live before calling it, so removing this one from
        // inside its own handler is safe.
        const int token = mRefreshToken;
        mRefreshToken = -1;
        mRefreshPending = false;
        if (removeTimer && token >= 0)
            removeTimer(token);
        refreshPorts();
    });
}

//------------------------------------------------------------------------
// The server went away without this window asking it to: it crashed, somebody killed it, or
// whatever started it stopped it. Before this handler existed the canvas froze on a dead client
// until the user noticed and pressed Refresh.
void App::handleServerGone()
{
    if (!mJack.is_connected())
        return; // already torn down

    // The hop means this can arrive LATE, after something else has already opened a fresh client
    // on a new server. Tearing down a client that is alive and well would turn that into a dropout.
    if (!mJack.server_gone())
        return;

    fprintf(stderr, "jack-graph: the JACK server went away; waiting for it to return\n");
    mJack.disconnect();
    mJackConnected = false;
    refreshPorts();

    mReconnectAttempts = 0;
    startReconnectPoll();
}

void App::startReconnectPoll()
{
    if (!addTimer)
        return;
    if (mReconnectToken >= 0 && removeTimer)
        removeTimer(mReconnectToken);
    mReconnectToken = addTimer(500, [this] {
        if (tryReconnectJack())
            return;
        const int token = mReconnectToken;
        mReconnectToken = -1;
        if (removeTimer && token >= 0)
            removeTimer(token);
    });
}

// True to keep polling.
bool App::tryReconnectJack()
{
    if (mJackConnected)
        return false;

    if (mJack.connect("jack-graph")) {
        mJackConnected = true;
        attachJackCallbacks();
        refreshPorts();
        fprintf(stderr, "jack-graph: reconnected to JACK\n");
        return false;
    }

    // 60 * 500ms = 30s, which covers a server being restarted by hand or by a script. A server
    // still absent after that was stopped deliberately, so stop polling and leave it to Start.
    if (++mReconnectAttempts >= 60) {
        fprintf(stderr, "jack-graph: JACK did not return within 30s; no longer polling\n");
        updateStatus();
        return false;
    }
    return true;
}

//------------------------------------------------------------------------
void App::refreshPorts(bool forgetPositions)
{
    mJack.scan_ports();
    mGraph.removeAll();

    // AFTER removeAll(), NOT BEFORE. removeAll() saves every box's current position on its way out
    // -- that is what carries an arrangement across a rebuild -- so clearing the saved positions
    // first would simply see them written again.
    if (forgetPositions)
        mGraph.forgetSavedPositions();

    if (mJackConnected) {
        // One map instead of a scan of every node for both ends of every connection, which is
        // what JackGraph::refresh_ports did: ports times connections, twice, on every refresh.
        std::unordered_map<std::string, std::shared_ptr<Node>> byName;
        const std::string ourClient = mJack.get_actual_client_name();
        for (const JackClient::PortInfo &p : mJack.get_ports()) {
            if (p.client == ourClient)
                continue;
            auto node = std::make_shared<Node>(
                p.name, p.is_audio ? PortType::AUDIO : PortType::MIDI,
                p.is_output ? PortDirection::OUTPUT : PortDirection::INPUT);
            byName[p.name] = node;
            mGraph.addNode(std::move(node));
        }

        for (const JackClient::ConnectionInfo &c : mJack.get_connections()) {
            auto src = byName.find(c.source);
            auto dst = byName.find(c.destination);
            if (src != byName.end() && dst != byName.end())
                mGraph.addConnection(
                    std::make_shared<Connection>(src->second, dst->second, src->second->type));
        }
    }

    // ALSA MIDI ports ONLY WHEN JACK IS NOT CONNECTED. With JACK running, jack_get_ports above
    // already reports every ALSA MIDI device the server bridged; adding them again from the
    // sequencer produces phantom duplicates (Midi-Through shows three ports instead of two).
    if (mAlsaConnected && !mJackConnected) {
        const std::vector<AlsaClient::PortInfo> ports = mAlsa.get_ports();

        // A box per client, keyed by name -- which is what keeps a box where the user put it
        // across a replug, when the client comes back with a new number. Two clients that share a
        // name at the same time get their number appended so they are two boxes, not one.
        std::map<std::string, std::set<int>> idsByName;
        for (const AlsaClient::PortInfo &p : ports)
            idsByName[p.client].insert(p.client_id);

        std::map<std::pair<int, int>, std::shared_ptr<Node>> outs, ins;
        for (const AlsaClient::PortInfo &p : ports) {
            const std::string box = idsByName[p.client].size() > 1
                                        ? p.client + " [" + std::to_string(p.client_id) + "]"
                                        : p.client;
            auto make = [&](PortDirection dir) {
                auto node = std::make_shared<Node>(box + ":" + p.name, PortType::MIDI, dir, true);
                // Set rather than parsed back out of the name: an ALSA name may contain ':'.
                node->client_name = box;
                // Most ALSA ports repeat their client's name ("Arturia MiniLab mkII MIDI 1" in
                // the "Arturia MiniLab mkII" box), which leaves no room for the part that differs.
                // The box header already says it, so the row says only the rest.
                node->label = p.name;
                if (p.name.size() > p.client.size() + 1 &&
                    p.name.compare(0, p.client.size(), p.client) == 0 &&
                    p.name[p.client.size()] == ' ')
                    node->label = p.name.substr(p.client.size() + 1);
                node->alsa_client = p.client_id;
                node->alsa_port = p.port_id;
                mGraph.addNode(node);
                return node;
            };
            // A duplex port is BOTH: it can be subscribed from and to, and forcing it to one side
            // -- as this used to, always output -- left nothing able to connect into it.
            if (p.is_output)
                outs[{p.client_id, p.port_id}] = make(PortDirection::OUTPUT);
            if (p.is_input)
                ins[{p.client_id, p.port_id}] = make(PortDirection::INPUT);
        }

        for (const AlsaClient::ConnectionInfo &c : mAlsa.get_connections()) {
            auto src = outs.find({c.src_client, c.src_port});
            auto dst = ins.find({c.dst_client, c.dst_port});
            if (src != outs.end() && dst != ins.end())
                mGraph.addConnection(std::make_shared<Connection>(src->second, dst->second,
                                                                  PortType::MIDI, true));
        }
    }

    mGraph.layout(true);
    updateStatus();
}

bool App::connectNodes(const Node &src, const Node &dst)
{
    if (src.is_alsa)
        return mAlsaConnected &&
               mAlsa.connect_ports(src.alsa_client, src.alsa_port, dst.alsa_client, dst.alsa_port);
    return mJackConnected && mJack.connect_ports(src.full_name(), dst.full_name());
}

void App::disconnectNodes(const Node &src, const Node &dst)
{
    if (src.is_alsa) {
        if (mAlsaConnected)
            mAlsa.disconnect_ports(src.alsa_client, src.alsa_port, dst.alsa_client, dst.alsa_port);
        return;
    }
    if (mJackConnected)
        mJack.disconnect_ports(src.full_name(), dst.full_name());
}

void App::updateStatus()
{
    std::string status;

    if (mJackConnected) {
        status += "JACK: connected";
        status += " | Buffer: " + std::to_string(mJack.get_buffer_size()) + " frames";
        status += " | Rate: " + std::to_string(mJack.get_sample_rate()) + " Hz";
        status += " | Xruns: " + std::to_string(mJack.get_xrun_count());
    } else {
        status += "JACK: not connected";
    }

    // THE SERVER'S STATE IS OUR OWN CONNECTION WHENEVER WE HAVE ONE, and asking JACK instead is a
    // feedback loop. JackServerControl::is_running() answers by opening a client called
    // "status_check" and closing it again -- and this window watches client registrations, so that
    // open-and-close fires our own callback, which schedules a port refresh, which ends by calling
    // this function, which asks again. The GTK build did exactly that and rebuilt the graph about
    // nine times a second on an idle server. With no client of our own there is nothing for the
    // probe's registration to fire, so asking then cannot loop.
    status += " | Server: ";
    if (mServerOp == ServerOp::Starting)
        status += "Starting";
    else if (mServerOp == ServerOp::Stopping)
        status += "Stopping";
    else
        status += mJackConnected ? "Running" : mServer.get_status();

    if (mAlsaConnected && !mJackConnected)
        status += " | ALSA MIDI: connected";

    mChrome.setStatus(status);
    pushServerState();
}

//------------------------------------------------------------------------
void App::startServer(const JackSettings &settings)
{
    if (mServerOp != ServerOp::None)
        return;

    setServerMessage(std::string(), false);

    // Somebody else's server is already up: there is nothing to spawn, only a client to open.
    if (!mJackConnected && mServer.is_running()) {
        connectJack();
        mGraph.fitToWindow();
        return;
    }
    if (mJackConnected) {
        pushServerState();
        return;
    }

    const pid_t pid = mServer.spawn(settings);
    if (pid < 0) {
        setServerMessage(mServer.last_error(), true);
        return;
    }
    wakepipe::watch(pid, [this](int status) { onJackdExited(status); });

    mServerOp = ServerOp::Starting;
    mServerOpTicks = 0;
    if (addTimer)
        mServerOpToken = addTimer(kServerPollMs, [this] { pollServerOp(); });
    updateStatus();
}

void App::stopServer()
{
    if (mServerOp != ServerOp::None)
        return;
    if (mServer.owned_pid() <= 0) {
        setServerMessage("This server was started outside Jack Graph, so it cannot be stopped here.",
                         false);
        return;
    }

    setServerMessage(std::string(), false);

    // DROP OUR CLIENT FIRST, so nothing of ours is inside the server while it shuts down -- and so
    // its shutdown callback does not start a reconnect poll for a server we are stopping on purpose.
    disconnectJack();
    refreshPorts();

    mServer.request_stop();
    mServerOp = ServerOp::Stopping;
    mServerOpTicks = 0;
    if (addTimer)
        mServerOpToken = addTimer(kServerPollMs, [this] { pollServerOp(); });
    updateStatus();
}

void App::pollServerOp()
{
    ++mServerOpTicks;

    if (mServerOp == ServerOp::Starting) {
        // Safe to probe: there is no client of ours yet, so the probe cannot loop (see
        // updateStatus). jackd answers within a second or two on most hardware.
        if (mServer.is_running()) {
            endServerOp();
            if (connectJack())
                mGraph.fitToWindow();
            updateStatus();
            return;
        }
        if (mServerOpTicks >= kStartTicks) {
            // Alive but not answering. Report it, then stop it the ordinary way, so a jackd stuck
            // on a device does not linger holding it.
            std::string msg = "jackd did not come up within 10 seconds.";
            const std::string tail = mServer.log_tail();
            if (!tail.empty())
                msg += "\n" + tail;
            endServerOp();
            setServerMessage(msg, true);
            mServer.request_stop();
            mServerOp = ServerOp::Stopping;
            mServerOpTicks = 0;
            if (addTimer)
                mServerOpToken = addTimer(kServerPollMs, [this] { pollServerOp(); });
            updateStatus();
        }
        return;
    }

    if (mServerOp == ServerOp::Stopping) {
        // Our own child is reaped by the wake pipe, which calls onJackdExited(). One adopted from an
        // earlier session is init's child, so the only way to see it go is to ask.
        if (!mServer.owned_alive()) {
            mServer.forget_owned();
            endServerOp();
            updateStatus();
            return;
        }
        if (mServerOpTicks == kKillTicks)
            mServer.force_kill();
        if (mServerOpTicks >= kGiveUpTicks) {
            setServerMessage("jackd (PID " + std::to_string(mServer.owned_pid()) +
                                 ") would not exit, even after SIGKILL.",
                             true);
            endServerOp();
            updateStatus();
        }
    }
}

void App::endServerOp()
{
    if (mServerOpToken >= 0 && removeTimer)
        removeTimer(mServerOpToken);
    mServerOpToken = -1;
    mServerOp = ServerOp::None;
}

// Our jackd exited, delivered on the main loop by the wake pipe.
void App::onJackdExited(int status)
{
    const ServerOp op = mServerOp;
    mServer.child_exited(status);

    if (op == ServerOp::Starting) {
        // The usual case for a failed Start: the device is busy or does not take this rate, and
        // jackd said so and exited. last_error() carries its own words.
        endServerOp();
        setServerMessage(mServer.last_error(), true);
    } else if (op == ServerOp::Stopping) {
        endServerOp();
    } else {
        // Nobody asked. The JACK client's own shutdown callback deals with the graph; this is only
        // the explanation, for the settings window.
        setServerMessage("jackd stopped unexpectedly. " + mServer.last_error(), true);
    }
    updateStatus();
}

void App::pushServerState()
{
    if (!mSettingsOpen || !mSettings)
        return;

    SettingsPanel::ServerState s = SettingsPanel::ServerState::Stopped;
    if (mServerOp == ServerOp::Starting)
        s = SettingsPanel::ServerState::Starting;
    else if (mServerOp == ServerOp::Stopping)
        s = SettingsPanel::ServerState::Stopping;
    else if (mJackConnected || mServer.is_running())
        s = SettingsPanel::ServerState::Running;

    mSettings->setServerState(s, mServer.owned_pid() > 0);
}

void App::setServerMessage(const std::string &text, bool error)
{
    mServerMessage = text;
    mServerMessageIsError = error;
    if (mSettingsOpen && mSettings)
        mSettings->setMessage(text, error);
}

//------------------------------------------------------------------------
void App::onTool(Tool t)
{
    const Rect r = mGraph.rect();
    switch (t) {
    case Tool::Refresh:
        // THE TOOLBAR'S REFRESH IS THE ONE THAT FORGETS. Pressing it means "lay this out again",
        // which is the only way back to the automatic layout once boxes have been dragged. Every
        // other path into refreshPorts() is the server's doing, not the user's, and keeps both.
        refreshPorts(true);
        mGraph.fitToWindow();
        break;
    case Tool::ZoomOut:
        mGraph.setZoomAround(mGraph.zoom() / geo::kZoomStepButton, r.centerX(), r.centerY());
        break;
    case Tool::ZoomIn:
        mGraph.setZoomAround(mGraph.zoom() * geo::kZoomStepButton, r.centerX(), r.centerY());
        break;
    case Tool::ZoomNormal:
        mGraph.setZoomAround(1.0, r.centerX(), r.centerY());
        break;
    case Tool::Fit:
        mGraph.fitToWindow();
        break;
    case Tool::Settings:
        if (onOpenSettings)
            onOpenSettings();
        break;
    case Tool::About:
        if (onOpenAbout)
            onOpenAbout();
        break;
    case Tool::None:
        break;
    }
}

//------------------------------------------------------------------------
SettingsPanel *App::openSettings()
{
    if (mSettingsOpen)
        return nullptr;

    // A panel left over from a window that was closed earlier. Dropped HERE rather than at close
    // time, because closing is asked for from inside the panel's own release handler.
    mSettings.reset(new SettingsPanel(mConfig));

    mSettings->listDevices = [this] { return mServer.list_audio_devices(); };
    mSettings->onStart = [this](const JackSettings &s) { startServer(s); };
    mSettings->onStop = [this] { stopServer(); };

    // A live frames/period change, with no server restart. False means the running server refused
    // it, which the panel reports rather than pretending it worked.
    mSettings->onBufferSize = [this](unsigned int nframes) -> bool {
        if (!mJack.is_connected())
            return false;
        if (!mJack.set_buffer_size(static_cast<jack_nframes_t>(nframes)))
            return false;
        updateStatus();
        return true;
    };
    mSettings->bufferSizeQuery = [this]() -> unsigned int {
        return mJack.is_connected() ? static_cast<unsigned int>(mJack.get_buffer_size()) : 0u;
    };

    mSettings->onClose = [this] { closeSettings(); };

    // onNeedsRepaint is NOT set here: it has to invalidate the settings WINDOW, and main.cpp is
    // what owns that. It sets it as soon as the window is open.

    // The hooks exist now, so the device list can be read -- before the config is applied to it.
    mSettings->reload();

    mSettingsOpen = true;
    mChrome.setActive(Tool::Settings, true);

    pushServerState();
    mSettings->setMessage(mServerMessage, mServerMessageIsError);
    return mSettings.get();
}

void App::closeSettings()
{
    if (!mSettingsOpen)
        return;
    mSettingsOpen = false;
    mChrome.setActive(Tool::Settings, false);

    // DEFERRED. This is reached from inside SettingsPanel::release() and SettingsPanel::key(), so
    // the panel and its window are only asked to go away; the panel object itself is dropped at the
    // next openSettings() or at shutdown().
    if (onCloseSettings)
        onCloseSettings();
}

//------------------------------------------------------------------------
void App::setSize(float w, float h)
{
    mChrome.setWindow(Rect(0.0f, 0.0f, w, h));
    mGraph.setRect(mChrome.canvasRect());
    // Remembered for the next launch; the Config fields existed in 1.x but nothing wrote them.
    mConfig.set_window_size(static_cast<int>(w), static_cast<int>(h));
    repaint();
}

void App::draw(Canvas &c)
{
    c.setColor(pal::kBgColor);
    c.fillRect(c.bounds());

    // Measured once, from the labels themselves; only a Canvas can measure a string.
    mChrome.layout(c);

    mGraph.draw(c);
    mChrome.draw(c);
}

void App::button(float x, float y, int button, bool pressed)
{
    if (pressed) {
        if (mChrome.press(x, y, button))
            return;
        mGraph.press(x, y, button);
        return;
    }
    if (mChrome.release(x, y, button))
        return;
    mGraph.release(x, y, button);
}

void App::motion(float x, float y)
{
    // The graph first, and only when it is mid-drag: a cable being dragged to a port near the top
    // of the canvas passes under the toolbar, and losing the drag there would be maddening.
    if (mGraph.motion(x, y))
        return;
    mChrome.motion(x, y);
}

void App::scroll(float x, float y, int dir)
{
    mGraph.scroll(x, y, dir);
}

} // namespace jackgraph
