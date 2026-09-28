// uirender -- compose the real panels offline and AUDIT EVERY STRING AGAINST THE REAL SLOT IT
// LANDS IN.
//
// graphgeometry.h's static_asserts can hold a clearance between two rectangles, because both are
// compile-time numbers. They cannot hold a LABEL inside its slot: how wide "Apply Live" renders
// depends on the face, the size and the rasteriser, none of which exist at compile time. So the
// layout is checked in two halves -- the header asserts the rectangles, and this measures the text
// that goes in them, with the bundled faces, against the rects the panels actually assigned.
//
// A STRING WIDER THAN ITS SLOT IS NOT A CRASH, which is the whole reason this tool exists.
// Canvas::clipToWidth truncates it with an ellipsis and the window still draws, so the failure is
// silent, it is invisible on a machine whose fonts happen to be narrow enough, and the first person
// to see "Apply Li..." is a user on a different machine.
//
// IT COMPOSES ALL THREE WINDOWS: the graph with its toolbar and status line, the About card, and
// the JACK Settings window -- which the jack-bridge copy of this tool could not, because its panel
// takes a JackServerControl&. This one talks to the server only through std::function hooks, so it
// is composed here with fabricated devices and fabricated server states, and every state it can be
// in is drawn.
//
// NO X SERVER, NO SOUND CARD, NO JACK. It links src/gfx, the three cairo-only panels, the
// pure-data model types they lay out, the config and src/platform/{respath,fs} -- and nothing else,
// which is what the Makefile's rule about X11 is for. If this tool ever needs libX11, libasound or
// libjack to build, something has reached across that line. That is also why CI can run it on an
// aarch64 runner with no display.
//
// Text metrics are scale-invariant in logical units -- Canvas sets CAIRO_HINT_METRICS_OFF, so a
// string's width at scale s is exactly s times its width at scale 1 -- so the AUDIT RUNS ONCE while
// the PNGs are written at each scale, because rasterisation at 0.75x genuinely is not rasterisation
// at 2x and the pictures are what a human looks at.
//
// Exit status is 0 only if every string fits and every codepoint has a glyph.
//
// Usage: uirender [--out <dir>]

#include "Config.hpp"
#include "chrome.h"
#include "gfx/fontstack.h"
#include "gfx/palette.h"
#include "graphgeometry.h"
#include "graphpanel.h"
#include "platform/respath.h"
#include "settingspanel.h"

#include <cairo/cairo.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

using namespace jackgraph;

namespace
{

int gFailures = 0;

//------------------------------------------------------------------------
// EVERY CHARACTER THE PANEL DRAWS MUST EXIST IN THE FACE THAT DRAWS IT.
//
// This is a separate question from whether the text fits, and checking only the latter is how a
// hole in a label reaches a user. cairo's toy text API -- cairo_show_text on one cairo_ft face --
// has NO FONT FALLBACK: a character the face lacks renders as glyph 0, which in Roboto is a
// zero-width nothing. It therefore measures as fitting, draws as a gap, and looks on the
// developer's machine exactly like it looks in the screenshot they are about to ship. GTK hid this
// completely, because Pango falls back per character across the whole installed font set; that is
// why the GTK build could put any codepoint in a label and this one cannot.
void checkGlyphs(const FontStack &fonts, const char *what, const std::string &text, Font font)
{
    FT_Face face =
        static_cast<FT_Face>(font == Font::Title ? fonts.titleFtFace() : fonts.bodyFtFace());
    if (!face)
        return; // a toy fallback; load() already refused to proceed on that

    // Minimal UTF-8 decode. The strings reaching here are literals in this repository, so it only
    // has to handle well-formed input.
    const unsigned char *p = reinterpret_cast<const unsigned char *>(text.c_str());
    while (*p) {
        unsigned cp = *p;
        int len = 1;
        if ((*p & 0xE0) == 0xC0) {
            cp = *p & 0x1Fu;
            len = 2;
        } else if ((*p & 0xF0) == 0xE0) {
            cp = *p & 0x0Fu;
            len = 3;
        } else if ((*p & 0xF8) == 0xF0) {
            cp = *p & 0x07u;
            len = 4;
        }
        for (int i = 1; i < len; ++i) {
            if ((p[i] & 0xC0) != 0x80) {
                len = i;
                break;
            }
            cp = (cp << 6) | (p[i] & 0x3Fu);
        }
        if (cp >= 0x20 && FT_Get_Char_Index(face, cp) == 0) {
            fprintf(stderr,
                    "uirender: %s uses U+%04X, which %s has no glyph for -- it will draw as a "
                    "gap: \"%s\"\n",
                    what, cp, face->family_name ? face->family_name : "the bundled face",
                    text.c_str());
            ++gFailures;
        }
        p += len;
    }
}

void checkFits(Canvas &c, const char *what, const std::string &text, float slot, Font font,
               float size)
{
    c.setFont(font);
    c.setFontSize(size);
    const float w = c.stringWidth(text.c_str());
    if (w > slot) {
        fprintf(stderr, "uirender: %s does not fit: \"%s\" needs %.1f, slot is %.1f\n", what,
                text.c_str(), static_cast<double>(w), static_cast<double>(slot));
        ++gFailures;
    }
}

// A checkbox or radio: the label starts after the indicator and its gap, and ends at the rect's
// right edge. Measured off the widget's OWN rect rather than off a constant, so a layout change
// that narrows a cell is caught here instead of at the next release.
void checkToggle(Canvas &c, const char *what, const Toggle &t)
{
    checkFits(c, what, t.label, t.textMaxW(), Font::Body, geo::kBodySize);
}

void checkPill(Canvas &c, const char *what, const Pill &p)
{
    const float need = Pill::widthFor(c, p.label.c_str());
    if (need > p.rect.w) {
        fprintf(stderr,
                "uirender: %s does not fit: \"%s\" needs %.1f including its padding, the pill is "
                "%.1f wide\n",
                what, p.label.c_str(), static_cast<double>(need), static_cast<double>(p.rect.w));
        ++gFailures;
    }
}

// A combo, closed and open. Both slots are audited because they are different widths: the closed
// control loses the arrow gutter and the popup loses the tick gutter, and an item can fit one and
// not the other.
void checkCombo(Canvas &c, const char *what, const Combo &combo)
{
    const Rect r = combo.rect();
    const float closedSlot = std::max(0.0f, r.w - geo::kComboArrowW - geo::kComboPadX);
    const float popupW = std::max(r.w, geo::kPopupTickW + 40.0f);
    const float popupSlot = std::max(0.0f, popupW - geo::kPopupTickW - geo::kComboPadX);
    for (const ComboItem &it : combo.items()) {
        checkFits(c, what, it.label, closedSlot, Font::Body, geo::kBodySize);
        checkFits(c, what, it.label, popupSlot, Font::Body, geo::kPopupTextSize);
    }
}

//------------------------------------------------------------------------
// THE GRAPH'S OWN CONTENT IS NOT AUDITED FOR WIDTH, and that is deliberate. A client box's header
// is a client name and a port row is a port name; both are whatever the running programs call
// themselves, both are clipped with an ellipsis by graphpanel.cpp on purpose, and the GTK build's
// Pango layout ellipsised them too. What IS audited is everything this repository chose the wording
// of: the toolbar pills, the status line, the About card and the settings window.

// The status line at its longest: updateStatus() joins its facts with " | " and the numbers are the
// widest they plausibly get -- a 192 kHz server with a six-figure xrun count.
const char *const kWidestStatus =
    "JACK: connected | Buffer: 8192 frames | Rate: 192000 Hz | Xruns: 999999 | Server: Starting";
const char *const kWidestStatusStopped =
    "JACK: not connected | Server: Stopping | ALSA MIDI: connected";

// Every message this program can put in the settings window's message area, at its longest. jackd's
// own lines are appended to some of these; those are jackd's words, not ours, and are wrapped and
// then clipped like any name off the hardware -- but the part we wrote has to fit.
const char *const kSetMessages[] = {
    "This server was started outside Jack Graph, so it cannot be stopped here.",
    "The running server refused frames/period 2048.",
    "Frames/period is now 2048.",
    "Could not run jackd. Is the jackd2 package installed?",
    "jackd (PID 4194304) would not exit, even after SIGKILL.",
    "Cannot create /run/user/4294967294/jack-graph: Permission denied",
    "Cannot open /run/user/4294967294/jack-graph/jackd.log: Permission denied",
    "fork() failed: Resource temporarily unavailable",
};

// A real jackd failure, in the shape App shows it: our first line, then the lines log_tail() picks
// -- here, what jackd 1.9.22 prints when another process holds the card, spaces collapsed.
const char *const kJackdFailure =
    "jackd exited with status 255:\n"
    "ATTENTION: The playback device \"hw:CARD=PCH\" is already in use. The following "
    "applications are using your soundcard(s) so you should check them and stop them as "
    "necessary before trying to start JACK again:\n"
    "jackd (process ID 1988)\n"
    "Failed to open server";

// A graph with something in every shape the panel can draw: two clients with audio ports, one with
// MIDI, a stereo pair to exercise pairStereoPorts, and a name longer than a box.
void sceneGraph(GraphPanel &g)
{
    struct PortSpec {
        const char *name;
        PortType type;
        PortDirection dir;
    };
    static const PortSpec kPorts[] = {
        {"system:capture_1", PortType::AUDIO, PortDirection::OUTPUT},
        {"system:capture_2", PortType::AUDIO, PortDirection::OUTPUT},
        {"system:playback_1", PortType::AUDIO, PortDirection::INPUT},
        {"system:playback_2", PortType::AUDIO, PortDirection::INPUT},
        {"system:midi_capture_1", PortType::MIDI, PortDirection::OUTPUT},
        {"system:midi_playback_1", PortType::MIDI, PortDirection::INPUT},
        {"ardour:Audio 1/audio_out 1", PortType::AUDIO, PortDirection::OUTPUT},
        {"ardour:Audio 1/audio_out 2", PortType::AUDIO, PortDirection::OUTPUT},
        {"ardour:Audio 1/audio_in 1", PortType::AUDIO, PortDirection::INPUT},
        {"ardour:MIDI 1/midi_in 1", PortType::MIDI, PortDirection::INPUT},
        {"qsynth:left", PortType::AUDIO, PortDirection::OUTPUT},
        {"qsynth:right", PortType::AUDIO, PortDirection::OUTPUT},
        // A stereo pair in the "<base>" / "<base>R" spelling pairStereoPorts exists for, and a
        // client name wider than a box, which is what clipToWidth is there to survive.
        {"REAPER (A Very Long Session Name):out", PortType::AUDIO, PortDirection::OUTPUT},
        {"REAPER (A Very Long Session Name):outR", PortType::AUDIO, PortDirection::OUTPUT},
    };

    std::vector<std::shared_ptr<Node>> made;
    for (const PortSpec &p : kPorts) {
        auto n = std::make_shared<Node>(p.name, p.type, p.dir);
        made.push_back(n);
        g.addNode(n);
    }
    // Cables, so the connector curve is in the picture as well as the boxes: two audio, one MIDI,
    // and one from a mixed box across to a sink.
    g.addConnection(std::make_shared<Connection>(made[6], made[2], PortType::AUDIO));
    g.addConnection(std::make_shared<Connection>(made[7], made[3], PortType::AUDIO));
    g.addConnection(std::make_shared<Connection>(made[4], made[9], PortType::MIDI));
    g.addConnection(std::make_shared<Connection>(made[0], made[8], PortType::AUDIO));
    g.layout(false);
}

// JACK stopped: the ALSA sequencer's view. A duplex port on both sides of its box, which is what
// Midi Through is, and a subscription between two clients.
void sceneAlsa(GraphPanel &g)
{
    struct PortSpec {
        const char *client;
        const char *port;
        int cid, pid;
        PortDirection dir;
    };
    static const PortSpec kPorts[] = {
        // Labels as App::refreshPorts gives them: the client's name taken off the front.
        {"Midi Through", "Port-0", 14, 0, PortDirection::OUTPUT},
        {"Midi Through", "Port-0", 14, 0, PortDirection::INPUT},
        {"Arturia MiniLab mkII", "MIDI 1", 24, 0, PortDirection::OUTPUT},
        {"Arturia MiniLab mkII", "MIDI 1", 24, 0, PortDirection::INPUT},
        {"FLUID Synth (4242)", "Synth input port (4242:0)", 128, 0, PortDirection::INPUT},
    };
    std::vector<std::shared_ptr<Node>> made;
    for (const PortSpec &p : kPorts) {
        auto n = std::make_shared<Node>(std::string(p.client) + ":" + p.port, PortType::MIDI,
                                        p.dir, true);
        n->client_name = p.client;
        n->label = p.port;
        n->alsa_client = p.cid;
        n->alsa_port = p.pid;
        made.push_back(n);
        g.addNode(n);
    }
    g.addConnection(std::make_shared<Connection>(made[2], made[4], PortType::MIDI, true));
    g.addConnection(std::make_shared<Connection>(made[2], made[1], PortType::MIDI, true));
    g.layout(false);
}

// Compose the graph window the way jack-graph's App::draw does -- ground, then the canvas, then the
// chrome over it. Four lines, repeated here rather than reached for, because App links libjack.
void drawGraphWindow(Canvas &c, Chrome &chrome, const GraphPanel &graph)
{
    c.setColor(pal::kBgColor);
    c.fillRect(c.bounds());
    chrome.layout(c);
    graph.draw(c);
    chrome.draw(c);
}

void auditGraph(Canvas &c, Chrome &chrome)
{
    static const Tool kTools[] = {Tool::Refresh,  Tool::ZoomOut,  Tool::ZoomIn, Tool::ZoomNormal,
                                  Tool::Fit,      Tool::Settings, Tool::About};

    float total = geo::kGraphMargin;
    for (Tool t : kTools) {
        const Pill *p = chrome.pill(t);
        if (!p) {
            fprintf(stderr, "uirender: the toolbar has no pill for tool %d\n", static_cast<int>(t));
            ++gFailures;
            continue;
        }
        checkPill(c, "a toolbar pill", *p);
        total += p->rect.w + geo::kToolbarGap;
    }
    total += geo::kGraphMargin - geo::kToolbarGap;

    // THE TOOLBAR AT THE NARROWEST THE WINDOW GETS. Every other rectangle in this window grows with
    // it, so this is the only width that can run out -- and it runs out silently, by drawing the
    // last pill off the edge.
    if (total > geo::kGraphMinW) {
        fprintf(stderr,
                "uirender: the toolbar needs %.1f but the smallest window is %.1f wide\n",
                static_cast<double>(total), static_cast<double>(geo::kGraphMinW));
        ++gFailures;
    }

    // The status line, at the narrowest window, in the row Chrome::draw puts it in.
    checkFits(c, "the status line", kWidestStatus, geo::kGraphMinW - 2.0f * geo::kGraphMargin,
              Font::Body, geo::kStatusSize);
}

void auditAbout(Canvas &c)
{
    const float w = geo::kAboutW - 2.0f * geo::kSetMargin;
    checkFits(c, "the About title", about::kName, w, Font::Title, geo::kAboutTitleSize);
    for (const char *t : {about::kVersion, about::kComment, about::kLicence})
        checkFits(c, "an About line", t, w, Font::Body, geo::kAboutTextSize);

    Pill close;
    close.label = "Close";
    close.rect = Rect(0.0f, 0.0f, geo::kSetButtonW, geo::kPillH);
    checkPill(c, "the About Close pill", close);
}

//------------------------------------------------------------------------
// The JACK Settings window, COMPOSED -- the real panel, laid out by its own layout(), with a device
// list fabricated to be as wide as a real one gets. Its fixed strings are measured against the rects
// its widgets were actually given.

const char *const kDevices =
    "hw:CARD=PCH|HDA Intel PCH - ALC269VB Analog\n"
    "hw:CARD=PCH,DEV=3|HDA Intel PCH - HDMI 0\n"
    "hw:CARD=USB|Scarlett 2i2 USB - USB Audio\n"
    "hw:CARD=sofhdadsp|sof-hda-dsp - HDA Analog (*)\n";

void wireSettings(SettingsPanel &p)
{
    p.listDevices = [] { return std::string(kDevices); };
    p.bufferSizeQuery = [] { return 0u; };
    p.reload();
    p.layout();
}

void auditSettings(Canvas &c, SettingsPanel &p)
{
    for (const char *t : {"JACK Server", "Audio", "MIDI", "Options"})
        checkFits(c, "a settings group title", t, geo::kSetContentW - 2.0f * geo::kGroupTitleX,
                  Font::Body, geo::kGroupTitleSize);

    for (const char *t : {"Interface:", "Sample Rate:", "Frames/Period:", "Periods/Buffer:",
                          "MIDI Driver:"})
        checkFits(c, "a settings field label", t, geo::kSetLabelW, Font::Body, geo::kBodySize);

    // The server status shares its row with Start and Stop: what is left of the group's inner width
    // once both pills and the gap before them are taken off.
    const float statusSlot = p.startPill().rect.x - geo::kSetMargin - geo::kSetGroupPad -
                             geo::kSetLabelGap;
    for (const char *t : {"Status: Running", "Status: Stopped", "Status: Starting...",
                          "Status: Stopping..."})
        checkFits(c, "the server status", t, statusSlot, Font::Body, geo::kBodySize);

    checkPill(c, "the Start pill", p.startPill());
    checkPill(c, "the Stop pill", p.stopPill());
    checkPill(c, "the Apply Live pill", p.applyLivePill());
    checkPill(c, "the Close pill", p.closePill());
    checkToggle(c, "the Realtime toggle", p.realtimeToggle());
    checkToggle(c, "the 16-bit toggle", p.shortsToggle());
    checkCombo(c, "a MIDI driver item", p.midiCombo());

    for (const char *t : {"Apply Live changes frames/period on the running server.",
                          "Everything else needs Stop then Start."})
        checkFits(c, "a settings note line", t, geo::kSetContentW - geo::kSetGroupPad, Font::Body,
                  geo::kSetNoteSize);

    // The message area, wrapped over kSetStatusLines. 90% of the last line, because word wrapping
    // cannot use a line's last few units.
    const float slot = geo::kSetContentW * (static_cast<float>(geo::kSetStatusLines) - 0.10f);
    for (const char *t : kSetMessages)
        checkFits(c, "a settings message", t, slot, Font::Body, geo::kSetStatusSize);
}

bool renderSettings(SettingsPanel &p, float scale, const std::string &outPath)
{
    const float h = p.layout();
    const int pw = static_cast<int>(geo::kSetW * scale + 0.5f);
    const int ph = static_cast<int>(h * scale + 0.5f);
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pw, ph);
    if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(s);
        return false;
    }
    cairo_t *cr = cairo_create(s);
    cairo_scale(cr, scale, scale);
    {
        FontStack fonts;
        fonts.load(resourceDir());
        Canvas c(cr, &fonts, geo::kSetW, h);
        p.draw(c);
    }
    cairo_destroy(cr);
    const bool ok = cairo_surface_write_to_png(s, outPath.c_str()) == CAIRO_STATUS_SUCCESS;
    cairo_surface_destroy(s);
    return ok;
}

void writeOrFail(bool ok, const std::string &path)
{
    if (!ok) {
        fprintf(stderr, "uirender: could not write %s\n", path.c_str());
        ++gFailures;
    }
}

// The graph window at one size and one scale. Composed into an image surface exactly as
// X11Window::paint would, which is what makes the picture worth looking at.
bool renderGraph(Chrome &chrome, GraphPanel &graph, float w, float h, float scale,
                 const std::string &outPath)
{
    chrome.setWindow(Rect(0.0f, 0.0f, w, h));
    graph.setRect(chrome.canvasRect());
    // AFTER the rect, not before: fitToWindow() measures against the viewport it was last given, so
    // fitting at the old size and then shrinking the window is how the picture ends up clipped.
    graph.fitToWindow();

    const int pw = static_cast<int>(w * scale + 0.5f);
    const int ph = static_cast<int>(h * scale + 0.5f);
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pw, ph);
    if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(s);
        return false;
    }
    cairo_t *cr = cairo_create(s);
    cairo_scale(cr, scale, scale);
    {
        FontStack fonts;
        fonts.load(resourceDir());
        Canvas c(cr, &fonts, w, h);
        drawGraphWindow(c, chrome, graph);
    }
    cairo_destroy(cr);
    const bool ok = cairo_surface_write_to_png(s, outPath.c_str()) == CAIRO_STATUS_SUCCESS;
    cairo_surface_destroy(s);
    return ok;
}

bool renderAbout(AboutCard &about, float scale, const std::string &outPath)
{
    const float h = about.layout();
    const int pw = static_cast<int>(geo::kAboutW * scale + 0.5f);
    const int ph = static_cast<int>(h * scale + 0.5f);
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pw, ph);
    if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(s);
        return false;
    }
    cairo_t *cr = cairo_create(s);
    cairo_scale(cr, scale, scale);
    {
        FontStack fonts;
        fonts.load(resourceDir());
        Canvas c(cr, &fonts, geo::kAboutW, h);
        about.draw(c);
    }
    cairo_destroy(cr);
    const bool ok = cairo_surface_write_to_png(s, outPath.c_str()) == CAIRO_STATUS_SUCCESS;
    cairo_surface_destroy(s);
    return ok;
}

} // namespace

int main(int argc, char **argv)
{
    std::string out = ".";
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--out") == 0 && i + 1 < argc)
            out = argv[++i];
    }

    // Create the output directory rather than failing on every write: this is a developer tool and
    // "mkdir first" is not a useful thing to have to remember.
    mkdir(out.c_str(), 0755);

    FontStack fonts;
    if (!fonts.load(resourceDir())) {
        // Measuring text against a substituted system face proves nothing about what a user with
        // the bundled fonts will see, so this is fatal here even though the window survives it.
        fprintf(stderr, "uirender: the bundled fonts are missing from %s; the audit would be "
                        "meaningless\n",
                resourceDir().c_str());
        return 1;
    }

    const float kScales[] = {geo::kScaleMin, 1.0f, geo::kScaleMax};

    // --- glyph coverage: a string with a hole in it is wrong whatever its width ---
    for (const char *t : {"Refresh", "Zoom -", "Zoom +", "Zoom 1:1", "Fit", "JACK Settings",
                          "About"})
        checkGlyphs(fonts, "a toolbar pill", t, Font::Body);
    checkGlyphs(fonts, "the status line", kWidestStatus, Font::Body);
    checkGlyphs(fonts, "the status line", kWidestStatusStopped, Font::Body);
    checkGlyphs(fonts, "the About title", about::kName, Font::Title);
    for (const char *t : {about::kVersion, about::kComment, about::kLicence})
        checkGlyphs(fonts, "an About line", t, Font::Body);
    for (const char *t : {"JACK Server", "Audio", "MIDI", "Options", "Interface:", "Sample Rate:",
                          "Frames/Period:", "Periods/Buffer:", "MIDI Driver:", "Status: Running",
                          "Status: Stopped", "Status: Starting...", "Status: Stopping...", "Start",
                          "Stop", "Apply Live", "Close", "Realtime", "16-bit samples (shorts)",
                          "None", "ALSA sequencer (seq)", "ALSA raw MIDI (raw)",
                          "Apply Live changes frames/period on the running server.",
                          "Everything else needs Stop then Start."})
        checkGlyphs(fonts, "a settings string", t, Font::Body);
    for (const char *t : kSetMessages)
        checkGlyphs(fonts, "a settings message", t, Font::Body);
    // clipToWidth appends U+2026 whenever anything is truncated, so the ellipsis itself has to
    // exist or an elided string ends in a gap.
    checkGlyphs(fonts, "the truncation ellipsis", "\xE2\x80\xA6", Font::Body);

    //--------------------------------------------------------------------
    // The graph window: the toolbar and status line audited against the rects Chrome::layout()
    // assigned, then the whole window drawn at three scales and at the smallest size it can be.
    {
        Chrome chrome;
        GraphPanel graph;
        sceneGraph(graph);

        cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 8, 8);
        cairo_t *cr = cairo_create(s);
        {
            Canvas c(cr, &fonts, geo::kGraphW, geo::kGraphH);
            chrome.setWindow(Rect(0.0f, 0.0f, geo::kGraphW, geo::kGraphH));
            chrome.layout(c);
            chrome.setStatus(kWidestStatus);
            auditGraph(c, chrome);
            checkFits(c, "the status line", kWidestStatusStopped,
                      geo::kGraphMinW - 2.0f * geo::kGraphMargin, Font::Body, geo::kStatusSize);
            auditAbout(c);
        }
        cairo_destroy(cr);
        cairo_surface_destroy(s);

        for (float scale : kScales) {
            char buf[512];
            snprintf(buf, sizeof(buf), "%s/graph@%.2fx.png", out.c_str(),
                     static_cast<double>(scale));
            writeOrFail(renderGraph(chrome, graph, geo::kGraphW, geo::kGraphH, scale, buf), buf);
        }
        // THE SMALLEST WINDOW, which is the one the toolbar can run out of room in.
        writeOrFail(renderGraph(chrome, graph, geo::kGraphMinW, geo::kGraphMinH, 1.0f,
                                out + "/graph-min@1.00x.png"),
                    out + "/graph-min@1.00x.png");
        // JACK stopped: the ALSA sequencer's ports and subscriptions instead.
        {
            Chrome bare;
            GraphPanel alsa;
            sceneAlsa(alsa);
            bare.setStatus("JACK: not connected | Server: Stopped | ALSA MIDI: connected");
            writeOrFail(renderGraph(bare, alsa, geo::kGraphW, geo::kGraphH, 1.0f,
                                    out + "/graph-alsa@1.00x.png"),
                        out + "/graph-alsa@1.00x.png");
        }
        // And an empty one: nothing to draw but the chrome saying so.
        {
            Chrome bare;
            GraphPanel none;
            bare.setStatus("JACK: not connected | Server: Stopped");
            none.layout(false);
            writeOrFail(renderGraph(bare, none, geo::kGraphW, geo::kGraphH, 1.0f,
                                    out + "/graph-empty@1.00x.png"),
                        out + "/graph-empty@1.00x.png");
        }
    }

    // The About card, which is its own dialog window.
    {
        AboutCard about;
        for (float scale : kScales) {
            char buf[512];
            snprintf(buf, sizeof(buf), "%s/about@%.2fx.png", out.c_str(),
                     static_cast<double>(scale));
            writeOrFail(renderAbout(about, scale, buf), buf);
        }
    }

    //--------------------------------------------------------------------
    // The JACK Settings window, audited, then drawn in each state it can be in. A default Config:
    // the audit must not depend on whatever the machine running it has saved.
    {
        Config config;
        SettingsPanel p(config);
        wireSettings(p);

        cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 8, 8);
        cairo_t *cr = cairo_create(s);
        {
            Canvas c(cr, &fonts, geo::kSetW, p.height());
            auditSettings(c, p);
        }
        cairo_destroy(cr);
        cairo_surface_destroy(s);

        for (float scale : kScales) {
            char buf[512];
            snprintf(buf, sizeof(buf), "%s/settings-stopped@%.2fx.png", out.c_str(),
                     static_cast<double>(scale));
            writeOrFail(renderSettings(p, scale, buf), buf);
        }

        p.setServerState(SettingsPanel::ServerState::Starting, true);
        writeOrFail(renderSettings(p, 1.0f, out + "/settings-starting@1.00x.png"),
                    out + "/settings-starting@1.00x.png");

        p.setServerState(SettingsPanel::ServerState::Stopped, false);
        p.setMessage(kJackdFailure, true);
        writeOrFail(renderSettings(p, 1.0f, out + "/settings-failed@1.00x.png"),
                    out + "/settings-failed@1.00x.png");

        p.setMessage(std::string(), false);
        p.setServerState(SettingsPanel::ServerState::Running, true);
        p.setMessage("Frames/period is now 256.", false);
        writeOrFail(renderSettings(p, 1.0f, out + "/settings-running@1.00x.png"),
                    out + "/settings-running@1.00x.png");

        p.setMessage(std::string(), false);
        p.setServerState(SettingsPanel::ServerState::Running, false);
        writeOrFail(renderSettings(p, 1.0f, out + "/settings-external@1.00x.png"),
                    out + "/settings-external@1.00x.png");

        // An open combo, because the popup draws over what is below it. The MIDI one, which is the
        // nearest the foot of the window and so the one most likely to run off it.
        p.setServerState(SettingsPanel::ServerState::Stopped, false);
        const Rect midi = p.midiCombo().rect();
        p.press(midi.centerX(), midi.centerY(), 1);
        p.release(midi.centerX(), midi.centerY(), 1);
        writeOrFail(renderSettings(p, 1.0f, out + "/settings-combo-open@1.00x.png"),
                    out + "/settings-combo-open@1.00x.png");
    }

    if (gFailures > 0) {
        fprintf(stderr, "uirender: %d layout problem(s)\n", gFailures);
        return 1;
    }
    printf("uirender: layout audit passed; images written to %s\n", out.c_str());
    return 0;
}
