// The controls both windows are built from.
//
// Ported from Audio-Gui, which wrote them because CPU-Power's could not be copied:
// simple-login-gui's ink.h records why -- none of CPU-Power's controls carries a rect, and every
// draw() and hit() reads absolute constants out of that project's geometry.h. That works for a
// panel with one fixed arrangement, and neither panel here has one.
//
// SO EVERY CONTROL CARRIES ITS RECT, and hit-tests against the same rect it draws, which is what
// stops the painter and the hit test drifting apart when the layout is computed at run time. That
// is not a nicety here: jack-graph builds everything it draws from whatever JACK reports this
// second, and nothing in any of its windows knows where it is until the panel has told it.
//
// What carries over from CPU-Power is the drawing idiom in ink.h -- a well fill, an optional
// accent wash, and a 1px inkFor() outline whose alpha is the entire hover treatment -- so a
// control drawn here reads as a sibling of one drawn there.
//
// WHY THERE IS NO ICON SET, having been offered one. Audio-Gui ends each mixer strip with a
// wordless IconButton drawing a speaker or a dot, and that came across in the first pass. It is
// deliberately NOT used here and the icons are gone with it, because of the four roles a
// checkbox plays in this mixer -- Mute, Enable, the internal card's `Capture`, and any
// switch-only control such as `IEC958` -- three carry an ALSA element name that is the only
// thing on screen telling you what the box does. An icon cannot say `IEC958`. Every control in
// this file that has a state therefore has a LABEL, and the one wordless thing left is the state
// chip, which is never interactive.
//
// The rule ink.h states still holds for what IS drawn: a checkmark is two strokeLine() calls and
// never a glyph, because a glyph depends on the bundled face having one.

#pragma once

#include "canvas.h"
#include "palette.h"

#include <string>

namespace jackgraph
{

// A group box: the @SURFACE card, its 1px @BORDER frame at radius 10, and the @SUBTEXT title
// sitting in the band above it. `frame` is the card, NOT including the title band.
void drawGroupBox(Canvas &c, const Rect &frame, const char *title);

// One line of text, baseline-positioned within `row` by ink.h's kLabelBaselineBias, clipped to
// the row's width with an ellipsis if it does not fit.
void drawRowText(Canvas &c, const Rect &row, const char *text, uint32_t rgb,
                 float size);

// `text` broken over at most `maxLines` lines that each fit `area.w`, on WORD BOUNDARIES, drawn as
// a block centred vertically in `area`.
//
// For a sentence whose length is not the author's to choose: in the JACK Settings window, the
// message area, which reports back whatever jackd said when it failed. '\n' in `text` is a hard
// break, and each paragraph is wrapped on its own.
//
// A word too long for a line on its own is CLIPPED rather than broken mid-character: that word is
// a path, and half a path invites the reader to type it. Anything past maxLines joins the last
// line and is clipped with it, so an over-long message ends in an ellipsis rather than losing its
// tail without saying so.
void drawWrappedText(Canvas &c, const Rect &area, const std::string &text, uint32_t rgb,
                     float size, float lineH, int maxLines);

// --- The volume slider ------------------------------------------------------
// 0..100, matching the range AlsaMixer already converts the element's raw range into.
struct Slider {
    Rect rect; // the groove's full travel, thumb centres at [rect.left(), rect.right()]
    int value = 0;
    bool enabled = true;
    bool hovered = false;
    bool dragging = false;

    void draw(Canvas &c) const;

    // The hit rect is the groove grown vertically to the thumb's height, because a 6-unit groove
    // is not a target. Grown horizontally too, by the thumb radius, so the ends are reachable.
    bool hit(float x, float y) const;

    // The value the thumb CENTRE lands on for a pointer at x, clamped. Mapping the pointer to
    // the centre rather than the left edge is what stops the thumb jumping half its width on
    // the first press of a drag.
    int valueAt(float x) const;
};

// --- Checkbox and radio -----------------------------------------------------
// One type, two indicator shapes: a 16x16 box at radius 4, or a circle of the same size. They
// differ in nothing else, and splitting them into two structs would duplicate the label
// handling and the hit rect for the sake of one branch.
struct Toggle {
    enum class Shape { Check, Radio };

    Rect rect; // the whole clickable row, indicator and label together
    std::string label;
    Shape shape = Shape::Check;
    bool on = false;
    bool enabled = true;
    bool hovered = false;

    void draw(Canvas &c) const;
    bool hit(float x, float y) const;

    // Where the text starts, so the layout audit can measure the label against the room it has.
    float textX() const;
    float textMaxW() const;
};

// --- Pill: a tab, a toolbar button, a push button ---------------------------
// From rations-amp (drawPill in products/rack/src/rack/rackview.cpp). This is the only button
// shape there is: the toolbar row, Start/Stop/Apply Live/Close. `active` is a state that is on --
// the Settings pill while its window is open; `hovered` is the pointer being on it.
//
// A DISABLED PILL IS STILL DRAWN, in kDisabledColor -- see palette.h. The GTK build greyed these
// out with gtk_widget_set_sensitive rather than hiding them, and a Scan button that vanishes when
// the adapter powers off is a Scan button the user goes looking for.
struct Pill {
    Rect rect;
    std::string label;
    bool active = false;
    bool enabled = true;
    bool hovered = false;

    void draw(Canvas &c) const;
    bool hit(float x, float y) const;

    // The width this label needs, so a row of pills can be laid out from its contents rather
    // than from a guess. Takes the canvas because only it can measure a string, and takes it
    // non-const because measuring means setting the font first.
    static float widthFor(Canvas &c, const char *label);
};

// --- PillToggle: a setting that is on or off -------------------------------
// From rations-amp's drawPillToggle (core/gfx/widgets.h), which draws the knob travelling from
// one end to the other. Used where the GTK build used a GtkSwitch -- the Steam bridge and
// Bluetooth Discoverable -- and NOT used for anything that is really a checkbox. The distinction
// is the one GTK itself draws: a switch is a setting that takes effect the moment it moves, a
// checkbox is a state.
//
// The label is drawn to the LEFT of the knob and is part of the hit rect, because a 40x20 knob
// on its own is a small target and the words beside it are what people click at.
struct PillToggle {
    Rect rect; // the whole clickable row, label and knob together
    std::string label;
    bool on = false;
    bool enabled = true;
    bool hovered = false;

    void draw(Canvas &c) const;
    bool hit(float x, float y) const;

    // Where the knob sits inside `rect`: right-aligned, vertically centred.
    Rect knobRect() const;
};

// --- Chip: a small state badge, never interactive ---------------------------
// The Bluetooth list's Paired / Trusted / Connected badges, and anything else that is a fact
// about a row rather than a control on it. Returns the width it drew, so a caller laying out
// several in a row does not have to measure them twice.
//
// It exists because the GTK build encoded these three states by appending " [Paired]" to the
// device's own name inside a GtkListStore that could only hold strings, and then parsed them
// back off again -- which is how a device that calls itself "My [Paired] Speaker" corrupts its
// own row. See btmodel.h.
float drawChip(Canvas &c, float x, float cy, const char *label, uint32_t rgb);
float chipWidth(Canvas &c, const char *label);

} // namespace jackgraph
