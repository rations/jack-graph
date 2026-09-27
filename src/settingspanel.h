// The JACK Settings window: start and stop the server, and choose what it starts with.
//
// This is SettingsDialog with Gtk::Dialog taken out from under it. The fields are the same --
// interface, sample rate, frames/period, periods/buffer, MIDI driver, realtime and 16-bit samples
// -- with the same value tokens, and one rule from it survives unchanged:
//
//   * populateDevices() RUNS BEFORE loadCurrentSettings(). The other order silently no-ops,
//     because selecting a value in an empty list does nothing, and jackd then starts on whatever
//     the list happened to default to.
//
// WHAT CHANGED:
//
//   * IT IS NOT MODAL. That was Gtk::Dialog::run()'s nested main loop, and there is none here.
//   * START AND STOP NO LONGER FREEZE THE WINDOW. The panel asks and App does the work
//     asynchronously -- spawn, then poll for the server; SIGTERM, then watch for the exit -- and
//     reports back through setServerState() and setMessage(). While either is in flight every
//     control is disabled and the status says what is happening.
//   * jackd's error output is shown IN THE WINDOW, wrapped, instead of in a Gtk::MessageDialog.
//   * Apply Live changes frames/period on the running server, which is the one setting JACK can
//     change without a restart.
//
// NO JackServerControl HERE, deliberately. The panel talks to the server only through the hooks
// below, so it links against nothing but cairo and the config -- which is what lets tools/uirender
// compose and audit it with no JACK library, no sound card and no X server.

#pragma once

#include "Config.hpp"
#include "JackServerControl.hpp"
#include "gfx/canvas.h"
#include "gfx/combo.h"
#include "gfx/keys.h"
#include "gfx/widgets.h"

#include <functional>
#include <string>

namespace jackgraph
{

class SettingsPanel
{
public:
    enum class ServerState { Stopped, Starting, Running, Stopping };

    explicit SettingsPanel(Config &config);

    //--- what the graph window does for us ------------------------------
    // Playback devices as "id|label" lines -- JackServerControl::list_audio_devices().
    std::function<std::string()> listDevices;
    // Start pressed, with the settings already saved to the config.
    std::function<void(const JackSettings &)> onStart;
    // Stop pressed.
    std::function<void()> onStop;
    // Apply Live. False means the running server refused the size.
    std::function<bool(unsigned int nframes)> onBufferSize;
    // What the running server says its frames/period is, or 0 with no server. After an Apply Live
    // only the server knows this; the config may say something else.
    std::function<unsigned int()> bufferSizeQuery;
    // Close was pressed, or Escape.
    std::function<void()> onClose;

    std::function<void()> onNeedsRepaint;

    // Read the device list and the config again. Called once the hooks above are wired: the
    // constructor runs before they exist, so it has no devices to list.
    void reload();

    // The server's state, pushed by App whenever it changes. `ours` is whether jack-graph started
    // it -- a server started any other way is not ours to stop, and Stop says so by being disabled.
    void setServerState(ServerState s, bool ours);
    // A sentence for the message area, or empty to clear it. Errors are drawn in the warning
    // colour.
    void setMessage(const std::string &text, bool error);

    //--- layout, paint, input -------------------------------------------
    float layout();
    float height() const
    {
        return mHeight;
    }
    void draw(Canvas &c) const;

    void motion(float x, float y);
    void press(float x, float y, int button);
    void release(float x, float y, int button);
    bool key(Key k, const char *text, int len, unsigned state);

    ServerState serverState() const
    {
        return mState;
    }
    const std::string &message() const
    {
        return mMessage;
    }

    // For tools/uirender: every fixed string the panel draws, with the rect it must fit.
    const Pill &startPill() const
    {
        return mStart;
    }
    const Pill &stopPill() const
    {
        return mStop;
    }
    const Pill &applyLivePill() const
    {
        return mApplyLive;
    }
    const Pill &closePill() const
    {
        return mClose;
    }
    const Toggle &realtimeToggle() const
    {
        return mRealtime;
    }
    const Toggle &shortsToggle() const
    {
        return mShorts;
    }
    const Combo &midiCombo() const
    {
        return mMidi;
    }

private:
    enum class Target { Nothing, Start, Stop, ApplyLive, CloseButton, Interface, Rate, Frames,
                        Periods, Midi, Realtime, Shorts };
    Target targetAt(float x, float y) const;
    void clearHover();
    void applyEnables();
    void repaint() const
    {
        if (onNeedsRepaint)
            onNeedsRepaint();
    }

    void populateDevices();
    void loadCurrentSettings();
    void doStart();
    void doApplyLive();

    Config &mConfig;

    Combo mInterface;
    Combo mRate;
    Combo mFrames;
    Combo mPeriods;
    Combo mMidi;

    Toggle mRealtime;
    Toggle mShorts;

    Pill mStart;
    Pill mStop;
    Pill mApplyLive;
    Pill mClose;

    ServerState mState = ServerState::Stopped;
    bool mOurs = false;
    std::string mMessage;
    bool mMessageIsError = false;

    float mHeight = 0.0f;
    Target mPressTarget = Target::Nothing;
    Target mHoverTarget = Target::Nothing;
};

} // namespace jackgraph
