// See settingspanel.h.

#include "settingspanel.h"

#include "graphgeometry.h"
#include "gfx/ink.h"
#include "gfx/palette.h"

#include <cstdlib>
#include <sstream>
#include <vector>

namespace jackgraph
{

namespace
{

// Select the item whose stable VALUE token matches, never its visible label. combo.h records why:
// matching on the visible text is how "48000" and "48000 Hz" become the same bug twice. This is
// Gtk::ComboBoxText::set_active_id, and like it, it reports whether anything matched.
bool selectByValue(Combo &combo, const std::string &value)
{
    if (value.empty())
        return false;
    const std::vector<ComboItem> &items = combo.items();
    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i].value == value) {
            combo.setIndex(static_cast<int>(i));
            return true;
        }
    }
    return false;
}

int valueAsInt(const Combo &combo, int fallback)
{
    const std::string &v = combo.value();
    if (v.empty())
        return fallback;
    char *end = nullptr;
    const long n = std::strtol(v.c_str(), &end, 10);
    return (end && *end == '\0' && n > 0) ? static_cast<int>(n) : fallback;
}

void label(Canvas &c, float x, float cy, const char *text, float maxW)
{
    c.setFont(Font::Body);
    c.setFontSize(geo::kBodySize);
    c.setColor(pal::kDimColor);
    const std::string t = c.clipToWidth(text, maxW);
    c.drawString(t.c_str(), x, cy + geo::kBodySize * geo::kLabelBaselineBias);
}

} // namespace

//------------------------------------------------------------------------
SettingsPanel::SettingsPanel(Config &config) : mConfig(config)
{
    mStart.label = "Start";
    mStop.label = "Stop";
    mApplyLive.label = "Apply Live";
    mClose.label = "Close";
    mRealtime.label = "Realtime";
    mShorts.label = "16-bit samples (shorts)";

    // The same item lists the Gtk::ComboBoxTexts held, with the same value tokens.
    mRate.setItems({{"44100", "44100"},
                    {"48000", "48000"},
                    {"88200", "88200"},
                    {"96000", "96000"},
                    {"192000", "192000"}});
    mFrames.setItems({{"64", "64"},
                      {"128", "128"},
                      {"256", "256"},
                      {"512", "512"},
                      {"1024", "1024"},
                      {"2048", "2048"}});
    mPeriods.setItems(
        {{"2", "2"}, {"3", "3"}, {"4", "4"}, {"5", "5"}, {"6", "6"}, {"7", "7"}, {"8", "8"}});
    // The LABEL is what the user reads and the VALUE is what jackd is started with: "-X seq",
    // "-X raw", or the flag omitted entirely for none.
    mMidi.setItems({{"None", "none"}, {"ALSA sequencer (seq)", "seq"}, {"ALSA raw MIDI (raw)", "raw"}});

    // An empty interface list until reload(): listDevices is not wired yet. The one item is what
    // Start falls back to if it were somehow pressed before then.
    mInterface.setItems({{"default", "default"}});
    loadCurrentSettings();
    applyEnables();
}

//------------------------------------------------------------------------
void SettingsPanel::reload()
{
    // DEVICES FIRST. loadCurrentSettings() selects by value, which silently does nothing on an
    // empty list -- see the header.
    populateDevices();
    loadCurrentSettings();
    applyEnables();
    repaint();
}

void SettingsPanel::populateDevices()
{
    std::vector<ComboItem> items;
    items.push_back({"default", "default"});

    const std::string devices = listDevices ? listDevices() : std::string();
    std::istringstream stream(devices);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty())
            continue;
        // "hw:CARD=id,DEV=n|Card Name - Device". The id is what jackd is started with and what is
        // saved; the label is the only half the user should have to read.
        const size_t pipe = line.find('|');
        if (pipe == std::string::npos)
            items.push_back({line, line});
        else
            items.push_back({line.substr(pipe + 1), line.substr(0, pipe)});
    }
    mInterface.setItems(std::move(items));
}

void SettingsPanel::loadCurrentSettings()
{
    // A saved device that is not plugged in now falls back to "default" rather than leaving the
    // field blank, which Start would have passed to jackd as no device at all.
    if (!selectByValue(mInterface, mConfig.get_interface()))
        mInterface.setIndex(0);

    // Sample rate -- default 48000 on first run.
    const int sr = mConfig.get_sample_rate();
    if (!selectByValue(mRate, sr > 0 ? std::to_string(sr) : "48000"))
        selectByValue(mRate, "48000");

    // Frames/period: the RUNNING server's, if there is one -- after an Apply Live that and the
    // config can differ, and the field should show what is actually in effect. Else the config,
    // else 1024.
    unsigned int live = bufferSizeQuery ? bufferSizeQuery() : 0u;
    const int fpp = mConfig.get_frames_per_period();
    if (!(live > 0 && selectByValue(mFrames, std::to_string(live))) &&
        !selectByValue(mFrames, fpp > 0 ? std::to_string(fpp) : "1024"))
        selectByValue(mFrames, "1024");

    // Periods/buffer -- default 2.
    const int ppb = mConfig.get_periods_per_buffer();
    if (!selectByValue(mPeriods, ppb > 0 ? std::to_string(ppb) : "2"))
        selectByValue(mPeriods, "2");

    // MIDI driver -- default none.
    if (!selectByValue(mMidi, mConfig.get_midi_driver()))
        selectByValue(mMidi, "none");

    mRealtime.on = mConfig.get_realtime();
    mShorts.on = mConfig.get_synchronous();
}

//------------------------------------------------------------------------
void SettingsPanel::setServerState(ServerState s, bool ours)
{
    if (s == mState && ours == mOurs)
        return;
    const bool becameRunning = s == ServerState::Running && mState != ServerState::Running;
    mState = s;
    mOurs = ours;
    // A server that has just come up may be running a different frames/period from the one the
    // field shows -- someone else started it, or an earlier session changed it live.
    if (becameRunning && bufferSizeQuery) {
        const unsigned int live = bufferSizeQuery();
        if (live > 0)
            selectByValue(mFrames, std::to_string(live));
    }
    applyEnables();
    repaint();
}

void SettingsPanel::setMessage(const std::string &text, bool error)
{
    mMessage = text;
    mMessageIsError = error;
    repaint();
}

// What may be pressed follows from the server's state alone. Everything is disabled while a start
// or stop is in flight: pressing Start twice would spawn two jackds, and changing a field mid-start
// would save settings the running server does not have.
void SettingsPanel::applyEnables()
{
    const bool idle = mState == ServerState::Stopped || mState == ServerState::Running;
    const bool fields = mState == ServerState::Stopped;

    mStart.enabled = mState == ServerState::Stopped;
    mStop.enabled = mState == ServerState::Running && mOurs;
    mApplyLive.enabled = mState == ServerState::Running;

    // The fields stay editable only while stopped: everything but frames/period is fixed for the
    // life of a server, so editing them while one runs would only save settings that are not in
    // effect. Frames/period stays live for Apply Live.
    mInterface.setEnabled(fields);
    mRate.setEnabled(fields);
    mPeriods.setEnabled(fields);
    mMidi.setEnabled(fields);
    mFrames.setEnabled(idle);
    mRealtime.enabled = fields;
    mShorts.enabled = fields;
}

//------------------------------------------------------------------------
void SettingsPanel::doStart()
{
    JackSettings settings;
    settings.interface = mInterface.value().empty() ? "default" : mInterface.value();
    settings.sample_rate = valueAsInt(mRate, 48000);
    settings.frames_per_period = valueAsInt(mFrames, 1024);
    settings.periods_per_buffer = valueAsInt(mPeriods, 2);
    settings.realtime = mRealtime.on;
    settings.synchronous = mShorts.on;
    settings.midi_driver = mMidi.value().empty() ? "none" : mMidi.value();

    mConfig.set_interface(settings.interface);
    mConfig.set_sample_rate(settings.sample_rate);
    mConfig.set_frames_per_period(settings.frames_per_period);
    mConfig.set_periods_per_buffer(settings.periods_per_buffer);
    mConfig.set_midi_driver(settings.midi_driver);
    mConfig.set_realtime(settings.realtime);
    mConfig.set_synchronous(settings.synchronous);
    mConfig.save();

    mMessage.clear();
    if (onStart)
        onStart(settings);
}

void SettingsPanel::doApplyLive()
{
    const int frames = valueAsInt(mFrames, 0);
    if (frames <= 0)
        return;
    if (onBufferSize && onBufferSize(static_cast<unsigned int>(frames))) {
        // Saved as well, so the next Start uses what the user just chose.
        mConfig.set_frames_per_period(frames);
        mConfig.save();
        setMessage("Frames/period is now " + std::to_string(frames) + ".", false);
    } else {
        setMessage("The running server refused frames/period " + std::to_string(frames) + ".",
                   true);
    }
}

//------------------------------------------------------------------------
float SettingsPanel::layout()
{
    const float x = geo::kSetMargin;
    const float controlX = x + geo::kSetGroupPad + geo::kSetLabelW + geo::kSetLabelGap;
    const float fullW = geo::kSetComboW + geo::kSetRowGap + geo::kSetApplyW;

    float y = geo::kSetMargin + geo::kSetGroupTitleBand;

    // --- JACK Server ---
    {
        const float rowY = y + geo::kSetGroupPad;
        mStop.rect = Rect(x + geo::kSetContentW - geo::kSetGroupPad - geo::kSetButtonW, rowY,
                          geo::kSetButtonW, geo::kPillH);
        mStart.rect = Rect(mStop.rect.x - geo::kPillGap - geo::kSetButtonW, rowY,
                           geo::kSetButtonW, geo::kPillH);
        y += geo::setServerH() + geo::kSetGroupGap + geo::kSetGroupTitleBand;
    }

    // --- Audio ---
    {
        float rowY = y + geo::kSetGroupPad;
        mInterface.setRect(Rect(controlX, rowY, fullW, geo::kComboH));
        rowY += geo::kSetRowH + geo::kSetRowGap;
        mRate.setRect(Rect(controlX, rowY, geo::kSetComboW, geo::kComboH));
        rowY += geo::kSetRowH + geo::kSetRowGap;
        mFrames.setRect(Rect(controlX, rowY, geo::kSetComboW, geo::kComboH));
        mApplyLive.rect = Rect(controlX + geo::kSetComboW + geo::kSetRowGap,
                               rowY + (geo::kComboH - geo::kPillH) * 0.5f, geo::kSetApplyW,
                               geo::kPillH);
        rowY += geo::kSetRowH + geo::kSetRowGap;
        mPeriods.setRect(Rect(controlX, rowY, geo::kSetComboW, geo::kComboH));

        y += geo::setAudioH() + geo::kSetGroupGap + geo::kSetGroupTitleBand;
    }

    // --- MIDI ---
    {
        mMidi.setRect(Rect(controlX, y + geo::kSetGroupPad, fullW, geo::kComboH));
        y += geo::setMidiH() + geo::kSetGroupGap + geo::kSetGroupTitleBand;
    }

    // --- Options ---
    {
        const float rowX = x + geo::kSetGroupPad;
        const float rowW = geo::kSetContentW - 2.0f * geo::kSetGroupPad;
        float rowY = y + geo::kSetGroupPad;
        mRealtime.rect = Rect(rowX, rowY, rowW, geo::kSetToggleH);
        rowY += geo::kSetToggleH + geo::kSetRowGap;
        mShorts.rect = Rect(rowX, rowY, rowW, geo::kSetToggleH);
        y += geo::setOptionsH() + geo::kSetGroupGap;
    }

    // --- the message area, outside any box ---
    y += geo::setStatusH() + geo::kSetGroupGap;

    mClose.rect = Rect(x + geo::kSetContentW - geo::kSetButtonW, y, geo::kSetButtonW,
                       geo::kPillH);
    y += geo::kPillH + geo::kSetMargin;

    mHeight = y;
    return mHeight;
}

//------------------------------------------------------------------------
void SettingsPanel::draw(Canvas &c) const
{
    c.setColor(pal::kBgColor);
    c.fillRect(c.bounds());

    const float x = geo::kSetMargin;
    const float labelX = x + geo::kSetGroupPad;
    const float labelW = geo::kSetLabelW;

    // --- JACK Server ---
    {
        const Rect frame(x, geo::kSetMargin + geo::kSetGroupTitleBand, geo::kSetContentW,
                         geo::setServerH());
        drawGroupBox(c, frame, "JACK Server");
        const char *status = "Status: Stopped";
        switch (mState) {
        case ServerState::Stopped:
            break;
        case ServerState::Starting:
            status = "Status: Starting...";
            break;
        case ServerState::Running:
            status = "Status: Running";
            break;
        case ServerState::Stopping:
            status = "Status: Stopping...";
            break;
        }
        label(c, labelX, mStart.rect.centerY(), status,
              mStart.rect.x - labelX - geo::kSetLabelGap);
        mStart.draw(c);
        mStop.draw(c);
    }

    // --- Audio ---
    {
        const Rect frame(x, mInterface.rect().y - geo::kSetGroupPad, geo::kSetContentW,
                         geo::setAudioH());
        drawGroupBox(c, frame, "Audio");

        label(c, labelX, mInterface.rect().centerY(), "Interface:", labelW);
        label(c, labelX, mRate.rect().centerY(), "Sample Rate:", labelW);
        label(c, labelX, mFrames.rect().centerY(), "Frames/Period:", labelW);
        label(c, labelX, mPeriods.rect().centerY(), "Periods/Buffer:", labelW);

        mInterface.drawClosed(c);
        mRate.drawClosed(c);
        mFrames.drawClosed(c);
        mPeriods.drawClosed(c);
        mApplyLive.draw(c);

        const float noteY = mPeriods.rect().bottom() + geo::kSetRowGap;
        c.setFont(Font::Body);
        c.setFontSize(geo::kSetNoteSize);
        c.setColor(pal::kDisabledColor);
        c.drawString("Apply Live changes frames/period on the running server.",
                     labelX, noteY + geo::kSetNoteLineH * 0.5f +
                                 geo::kSetNoteSize * geo::kLabelBaselineBias);
        c.drawString("Everything else needs Stop then Start.", labelX,
                     noteY + geo::kSetNoteLineH * 1.5f +
                         geo::kSetNoteSize * geo::kLabelBaselineBias);
    }

    // --- MIDI ---
    {
        const Rect frame(x, mMidi.rect().y - geo::kSetGroupPad, geo::kSetContentW,
                         geo::setMidiH());
        drawGroupBox(c, frame, "MIDI");
        label(c, labelX, mMidi.rect().centerY(), "MIDI Driver:", labelW);
        mMidi.drawClosed(c);
    }

    // --- Options ---
    {
        const Rect frame(x, mRealtime.rect.y - geo::kSetGroupPad, geo::kSetContentW,
                         geo::setOptionsH());
        drawGroupBox(c, frame, "Options");
        mRealtime.draw(c);
        mShorts.draw(c);
    }

    // --- the message area ---
    //
    // What jackd said when it failed, or the result of an Apply Live. With nothing to report and
    // a server this program did not start, it says why Stop is disabled -- a greyed button with
    // no reason given is a button the user assumes is broken.
    {
        const Rect area(x, mShorts.rect.bottom() + geo::kSetGroupPad + geo::kSetGroupGap,
                        geo::kSetContentW, geo::setStatusH());
        std::string text = mMessage;
        uint32_t rgb = mMessageIsError ? pal::kWarnColor : pal::kDimColor;
        if (text.empty() && mState == ServerState::Running && !mOurs) {
            text = "This server was started outside Jack Graph, so it cannot be stopped here.";
            rgb = pal::kDimColor;
        }
        if (!text.empty())
            drawWrappedText(c, area, text, rgb, geo::kSetStatusSize, geo::kSetStatusLineH,
                            geo::kSetStatusLines);
    }

    mClose.draw(c);

    // The popups last, over everything. Only one can be open at a time.
    for (const Combo *combo : {&mInterface, &mRate, &mFrames, &mPeriods, &mMidi}) {
        if (combo->isOpen())
            combo->drawPopup(c);
    }
}

//------------------------------------------------------------------------
SettingsPanel::Target SettingsPanel::targetAt(float px, float py) const
{
    // An open popup takes everything, including a click outside it -- which closes it and is
    // SWALLOWED, so the click does not also press whatever is under it.
    if (mInterface.isOpen())
        return Target::Interface;
    if (mRate.isOpen())
        return Target::Rate;
    if (mFrames.isOpen())
        return Target::Frames;
    if (mPeriods.isOpen())
        return Target::Periods;
    if (mMidi.isOpen())
        return Target::Midi;

    if (mStart.hit(px, py))
        return Target::Start;
    if (mStop.hit(px, py))
        return Target::Stop;
    if (mApplyLive.hit(px, py))
        return Target::ApplyLive;
    if (mClose.hit(px, py))
        return Target::CloseButton;
    if (mInterface.hitClosed(px, py))
        return Target::Interface;
    if (mRate.hitClosed(px, py))
        return Target::Rate;
    if (mFrames.hitClosed(px, py))
        return Target::Frames;
    if (mPeriods.hitClosed(px, py))
        return Target::Periods;
    if (mMidi.hitClosed(px, py))
        return Target::Midi;
    if (mRealtime.hit(px, py))
        return Target::Realtime;
    if (mShorts.hit(px, py))
        return Target::Shorts;
    return Target::Nothing;
}

void SettingsPanel::clearHover()
{
    mStart.hovered = mStop.hovered = mApplyLive.hovered = mClose.hovered = false;
    mInterface.hovered = mRate.hovered = mFrames.hovered = mPeriods.hovered = mMidi.hovered =
        false;
    mRealtime.hovered = mShorts.hovered = false;
}

void SettingsPanel::motion(float px, float py)
{
    for (Combo *combo : {&mInterface, &mRate, &mFrames, &mPeriods, &mMidi}) {
        if (combo->isOpen()) {
            combo->motion(px, py);
            repaint();
            return;
        }
    }

    const Target t = targetAt(px, py);
    if (t == mHoverTarget)
        return;
    mHoverTarget = t;
    clearHover();
    switch (t) {
    case Target::Start:
        mStart.hovered = true;
        break;
    case Target::Stop:
        mStop.hovered = true;
        break;
    case Target::ApplyLive:
        mApplyLive.hovered = true;
        break;
    case Target::CloseButton:
        mClose.hovered = true;
        break;
    case Target::Interface:
        mInterface.hovered = true;
        break;
    case Target::Rate:
        mRate.hovered = true;
        break;
    case Target::Frames:
        mFrames.hovered = true;
        break;
    case Target::Periods:
        mPeriods.hovered = true;
        break;
    case Target::Midi:
        mMidi.hovered = true;
        break;
    case Target::Realtime:
        mRealtime.hovered = true;
        break;
    case Target::Shorts:
        mShorts.hovered = true;
        break;
    case Target::Nothing:
        break;
    }
    repaint();
}

void SettingsPanel::press(float px, float py, int button)
{
    if (button != 1)
        return;
    mPressTarget = targetAt(px, py);
}

void SettingsPanel::release(float px, float py, int button)
{
    if (button != 1)
        return;

    const Target pressed = mPressTarget;
    mPressTarget = Target::Nothing;

    // An open popup: hand the click to it and stop, whether it landed inside or outside.
    for (Combo *combo : {&mInterface, &mRate, &mFrames, &mPeriods, &mMidi}) {
        if (combo->isOpen()) {
            combo->click(px, py);
            repaint();
            return;
        }
    }

    // PAIRED: act only if the release is on the same thing the press armed.
    const Target t = targetAt(px, py);
    if (t != pressed)
        return;

    const Rect screen(0, 0, geo::kSetW, mHeight);
    switch (t) {
    case Target::Start:
        if (mStart.enabled)
            doStart();
        break;
    case Target::Stop:
        if (mStop.enabled) {
            mMessage.clear();
            if (onStop)
                onStop();
        }
        break;
    case Target::ApplyLive:
        if (mApplyLive.enabled)
            doApplyLive();
        break;
    case Target::CloseButton:
        if (onClose)
            onClose();
        break;
    case Target::Interface:
        // Plugged in since the window opened? Read the list again before showing it.
        if (mInterface.enabled()) {
            const std::string keep = mInterface.value();
            populateDevices();
            if (!selectByValue(mInterface, keep))
                mInterface.setIndex(0);
            mInterface.open(screen);
        }
        repaint();
        break;
    case Target::Rate:
        if (mRate.enabled())
            mRate.open(screen);
        repaint();
        break;
    case Target::Frames:
        if (mFrames.enabled())
            mFrames.open(screen);
        repaint();
        break;
    case Target::Periods:
        if (mPeriods.enabled())
            mPeriods.open(screen);
        repaint();
        break;
    case Target::Midi:
        if (mMidi.enabled())
            mMidi.open(screen);
        repaint();
        break;
    case Target::Realtime:
        mRealtime.on = !mRealtime.on;
        repaint();
        break;
    case Target::Shorts:
        mShorts.on = !mShorts.on;
        repaint();
        break;
    case Target::Nothing:
        break;
    }
}

bool SettingsPanel::key(Key k, const char *, int, unsigned)
{
    for (Combo *combo : {&mInterface, &mRate, &mFrames, &mPeriods, &mMidi}) {
        if (combo->isOpen()) {
            const bool used = combo->key(k);
            repaint();
            return used;
        }
    }
    if (k == Key::Escape) {
        if (onClose)
            onClose();
        return true;
    }
    return false;
}

} // namespace jackgraph
