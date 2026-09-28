// See widgets.h.

#include "widgets.h"

#include "metrics.h"
#include "ink.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace jackgraph
{
namespace
{

// The well every control is drawn on: the Qt stylesheet used @INPUT for a slider groove, an
// unchecked indicator and a tool button alike, and keeping that one token here is what makes
// them read as the same material.
void fillWell(Canvas &c, const Rect &r, float radius, bool enabled)
{
    c.setColor(enabled ? pal::kWellColor : pal::kBgColor);
    c.fillRoundRect(r, radius);
}

// The 1px outline whose ALPHA is the entire hover treatment. There is no hover fill and no
// pressed state anywhere in this file -- see ink.h.
void strokeInk(Canvas &c, const Rect &r, float radius, bool enabled, bool on,
               bool hovered)
{
    c.setColor(inkFor(enabled, on), hovered ? kOutlineAlphaHover : kOutlineAlphaIdle);
    c.setPenSize(1.0f);
    c.strokeRoundRect(r, radius);
}

} // namespace

void drawGroupBox(Canvas &c, const Rect &frame, const char *title)
{
    c.setColor(pal::kFaceColor);
    c.fillRoundRect(frame, geo::kGroupRadius);

    // THE FRAME IS THE ACCENT, not the neutral border token. Drawn at an alpha rather than full
    // strength: at 255 three stacked boxes outlined in a saturated colour fight the controls
    // inside them for attention, which is the opposite of what a frame is for.
    c.setColor(pal::kAccent, kFrameAlpha);
    c.setPenSize(1.0f);
    c.strokeRoundRect(frame, geo::kGroupRadius);

    if (title && *title) {
        c.setFont(Font::Body);
        c.setFontSize(geo::kGroupTitleSize);
        c.setColor(pal::kDimColor);
        const float maxW = frame.w - 2.0f * geo::kGroupTitleX;
        const std::string s = c.clipToWidth(title, maxW);
        // The title sits in the band ABOVE the frame, baselined so its descenders clear the
        // frame's top edge rather than touching it.
        c.drawString(s.c_str(), frame.x + geo::kGroupTitleX, frame.y - 5.0f);

        // A gold hairline from the end of the title to the frame's right edge: the family's
        // piping, in the one place this window has room for it. CPU-Power rules off its title
        // the same way, at the same sort of alpha.
        const float titleEnd = frame.x + geo::kGroupTitleX + c.stringWidth(s.c_str()) + 8.0f;
        const float ruleY = frame.y - 9.0f;
        if (titleEnd < frame.right() - 4.0f) {
            c.setColor(pal::kGold, kHairlineAlpha);
            c.setPenSize(1.0f);
            c.strokeLine(titleEnd, ruleY, frame.right(), ruleY);
        }
    }
}

void drawRowText(Canvas &c, const Rect &row, const char *text, uint32_t rgb,
                 float size)
{
    if (!text || !*text)
        return;
    c.setFont(Font::Body);
    c.setFontSize(size);
    c.setColor(rgb);
    const std::string s = c.clipToWidth(text, row.w);
    c.drawString(s.c_str(), row.x, row.centerY() + size * geo::kLabelBaselineBias);
}

// --- Slider -----------------------------------------------------------------

namespace
{
// The thumb travels between these two x positions; everything about the slider derives from it,
// so draw() and valueAt() cannot disagree about where a value sits.
float thumbTravelX0(const Rect &r)
{
    return r.x + geo::kThumbR;
}
float thumbTravelW(const Rect &r)
{
    return std::max(1.0f, r.w - 2.0f * geo::kThumbR);
}
} // namespace

void drawWrappedText(Canvas &c, const Rect &area, const std::string &text, uint32_t rgb,
                     float size, float lineH, int maxLines)
{
    c.setFont(Font::Body);
    c.setFontSize(size);
    c.setColor(rgb);

    // '\n' IS A HARD BREAK. jackd's error output arrives as lines, and running them together would
    // make "exited with status 1:" and the reason one sentence. Each paragraph is wrapped on its
    // own; the font has no glyph for '\n', so leaving it in would draw a gap.
    std::vector<std::string> lines;
    size_t pos = 0;
    while (pos < text.size() && static_cast<int>(lines.size()) < maxLines) {
        size_t paraEnd = text.find('\n', pos);
        if (paraEnd == std::string::npos)
            paraEnd = text.size();

        // The longest prefix of the rest of this paragraph that fits, ending at a space.
        size_t take = paraEnd - pos;
        if (c.stringWidth(text.substr(pos, take).c_str()) > area.w) {
            size_t fit = pos;
            size_t probe = pos;
            while (probe < paraEnd) {
                const size_t sp = text.find(' ', probe);
                const size_t end = (sp == std::string::npos || sp > paraEnd) ? paraEnd : sp;
                if (c.stringWidth(text.substr(pos, end - pos).c_str()) > area.w)
                    break;
                fit = end;
                if (end == paraEnd)
                    break;
                probe = sp + 1;
            }
            // Nothing fit: one word is wider than the line. Take it whole and let the clip below
            // shorten it.
            if (fit > pos) {
                take = fit - pos;
            } else {
                const size_t sp = text.find(' ', pos);
                take = (sp == std::string::npos || sp > paraEnd) ? paraEnd - pos : sp - pos;
            }
        }
        lines.push_back(text.substr(pos, take));
        pos += take;
        while (pos < text.size() && text[pos] == ' ')
            ++pos;
        if (pos < text.size() && text[pos] == '\n')
            ++pos;
    }
    // Anything that did not fit in maxLines lines joins the last one, which is then clipped -- so an
    // over-long message ends in an ellipsis instead of losing its tail without saying so.
    if (pos < text.size() && !lines.empty()) {
        std::string rest = text.substr(pos);
        for (char &ch : rest) {
            if (ch == '\n')
                ch = ' ';
        }
        lines.back() += " " + rest;
    }

    // Vertically centred as a block, so a one-line message sits in the middle of the strip rather
    // than at the top of it.
    const float blockH = static_cast<float>(lines.size()) * lineH;
    float y = area.centerY() - blockH * 0.5f + lineH * 0.5f;
    for (const std::string &line : lines) {
        const std::string t = c.clipToWidth(line, area.w);
        c.drawString(t.c_str(), area.x, y + size * geo::kLabelBaselineBias);
        y += lineH;
    }
}
//------------------------------------------------------------------------
void Slider::draw(Canvas &c) const
{
    const float grooveY = rect.centerY() - geo::kGrooveH / 2.0f;
    const Rect groove(rect.x, grooveY, rect.w, geo::kGrooveH);

    c.setColor(enabled ? pal::kWellColor : pal::kBorderColor);
    c.fillRoundRect(groove, geo::kGrooveRadius);

    const float t = std::clamp(value, 0, 100) / 100.0f;
    const float cx = thumbTravelX0(rect) + thumbTravelW(rect) * t;

    // The filled portion. The Qt stylesheet ran a light-to-dark accent gradient across it; the
    // house idiom is a flat accent, and Canvas has no gradient API in any project in this family.
    if (cx > groove.x) {
        c.setColor(enabled ? pal::kAccent : pal::kBorderColor);
        c.fillRoundRect(Rect(groove.x, groove.y, cx - groove.x, groove.h), geo::kGrooveRadius);
    }

    // The thumb: @TEXT filled, accent-ringed when the pointer is on it. 16x16 at radius 8.
    c.setColor(enabled ? pal::kTextColor : pal::kDisabledColor);
    c.fillEllipse(cx, rect.centerY(), geo::kThumbR, geo::kThumbR);
    if (enabled && (hovered || dragging)) {
        c.setColor(pal::kAccent, kOutlineAlphaHover);
        c.strokeEllipse(cx, rect.centerY(), geo::kThumbR, geo::kThumbR);
    }
}

bool Slider::hit(float x, float y) const
{
    if (!enabled)
        return false;
    // Grown to the thumb's size in both axes: the groove itself is 6 units tall and would be a
    // target nobody can hit.
    const Rect target(rect.x - geo::kThumbR, rect.centerY() - geo::kThumbR,
                      rect.w + 2.0f * geo::kThumbR, 2.0f * geo::kThumbR);
    return target.contains(x, y);
}

int Slider::valueAt(float x) const
{
    const float t = (x - thumbTravelX0(rect)) / thumbTravelW(rect);
    return std::clamp(static_cast<int>(std::lround(t * 100.0f)), 0, 100);
}

// --- Toggle (checkbox / radio) ----------------------------------------------

namespace
{
Rect indicatorRect(const Rect &row)
{
    return Rect(row.x, row.centerY() - geo::kIndicatorSize / 2.0f, geo::kIndicatorSize,
                geo::kIndicatorSize);
}
} // namespace

float Toggle::textX() const
{
    return rect.x + geo::kIndicatorSize + geo::kIndicatorGap;
}

float Toggle::textMaxW() const
{
    return std::max(0.0f, rect.right() - textX());
}

void Toggle::draw(Canvas &c) const
{
    const Rect box = indicatorRect(rect);
    const bool circle = shape == Shape::Radio;
    const float radius = circle ? geo::kRadioRadius : geo::kCheckRadius;

    if (circle) {
        c.setColor(enabled ? pal::kWellColor : pal::kBgColor);
        c.fillEllipse(box.centerX(), box.centerY(), geo::kRadioRadius, geo::kRadioRadius);
    } else {
        fillWell(c, box, radius, enabled);
    }

    if (on && enabled) {
        c.setColor(pal::kAccent, kOnFillAlpha);
        if (circle)
            c.fillEllipse(box.centerX(), box.centerY(), geo::kRadioRadius, geo::kRadioRadius);
        else
            c.fillRoundRect(box, radius);
    }

    c.setColor(inkFor(enabled, on), hovered ? kOutlineAlphaHover : kOutlineAlphaIdle);
    c.setPenSize(1.0f);
    if (circle)
        c.strokeEllipse(box.centerX(), box.centerY(), geo::kRadioRadius, geo::kRadioRadius);
    else
        c.strokeRoundRect(box, radius);

    // The mark. A radio gets a dot; a checkbox gets two strokeLines, never a glyph -- a
    // checkmark taken from the body font depends on that font having one.
    if (on) {
        c.setColor(enabled ? pal::kOnAccent : pal::kDisabledColor);
        if (circle) {
            c.fillEllipse(box.centerX(), box.centerY(), geo::kRadioRadius * 0.4f,
                          geo::kRadioRadius * 0.4f);
        } else {
            c.setPenSize(2.0f);
            const float x0 = box.x + box.w * 0.24f;
            const float x1 = box.x + box.w * 0.44f;
            const float x2 = box.x + box.w * 0.78f;
            c.strokeLine(x0, box.centerY(), x1, box.y + box.h * 0.72f);
            c.strokeLine(x1, box.y + box.h * 0.72f, x2, box.y + box.h * 0.28f);
        }
    }

    if (!label.empty()) {
        c.setFont(Font::Body);
        c.setFontSize(geo::kBodySize);
        c.setColor(enabled ? pal::kTextColor : pal::kDisabledColor);
        const std::string s = c.clipToWidth(label, textMaxW());
        c.drawString(s.c_str(), textX(), rect.centerY() + geo::kBodySize * geo::kLabelBaselineBias);
    }
}

bool Toggle::hit(float x, float y) const
{
    return enabled && rect.contains(x, y);
}

// --- Pill -------------------------------------------------------------------

float Pill::widthFor(Canvas &c, const char *label)
{
    // Measured at the pill's own text size, not whatever the canvas was last set to: a row of
    // pills laid out against the wrong size is a row that overflows at one scale and not another,
    // which is precisely what uirender exists to catch and what it must not have to catch here.
    c.setFont(Font::Body);
    c.setFontSize(geo::kPillTextSize);
    return c.stringWidth(label ? label : "") + 2.0f * geo::kPillPadX;
}

void Pill::draw(Canvas &c) const
{
    const float radius = rect.h * 0.5f;

    // An active pill is an accent WASH, not a solid accent fill: rations-amp draws it at alpha 55
    // because a solid green pill beside six dark ones reads as an alert rather than as a state.
    if (active && enabled) {
        c.setColor(pal::kAccent, 55);
        c.fillRoundRect(rect, radius);
    } else {
        c.setColor(pal::kWellColor);
        c.fillRoundRect(rect, radius);
    }

    strokeInk(c, rect, radius, enabled, active, hovered);

    if (!label.empty()) {
        c.setFont(Font::Body);
        c.setFontSize(geo::kPillTextSize);
        c.setColor(!enabled ? pal::kDisabledColor
                            : (active ? pal::kTextColor : (hovered ? pal::kTextColor : pal::kDimColor)));
        // Clipped to the room between the paddings, so a label that outgrew its pill truncates
        // instead of running over the outline.
        const std::string t = c.clipToWidth(label, std::max(0.0f, rect.w - 2.0f * geo::kPillPadX));
        c.drawString(t.c_str(), rect.centerX() - c.stringWidth(t.c_str()) * 0.5f,
                     rect.centerY() + geo::kPillTextSize * geo::kLabelBaselineBias);
    }
}

bool Pill::hit(float x, float y) const
{
    return enabled && rect.contains(x, y);
}

// --- PillToggle -------------------------------------------------------------

Rect PillToggle::knobRect() const
{
    return Rect(rect.right() - geo::kPillToggleW, rect.centerY() - geo::kPillToggleH / 2.0f,
                geo::kPillToggleW, geo::kPillToggleH);
}

void PillToggle::draw(Canvas &c) const
{
    const Rect pill = knobRect();
    const float radius = pill.h * 0.5f;

    // rations-amp's drawPillToggle, with its two hardcoded greys replaced by the palette's own
    // tokens. It fills the track in the accent when on so the state is readable from across the
    // window and not only from the knob's position.
    c.setColor(on && enabled ? pal::kAccent : pal::kWellColor, on && enabled ? kOnFillAlpha : 255);
    c.fillRoundRect(pill, radius);
    c.setColor(inkFor(enabled, on), (hovered || on) ? kOutlineAlphaHover : kOutlineAlphaIdle);
    c.setPenSize(1.0f);
    c.strokeRoundRect(pill, radius);

    // The knob: inset 2 on every side, so its travel is the track's width less its own.
    const float d = pill.h - 4.0f;
    const float kx = on ? pill.right() - d - 2.0f : pill.left() + 2.0f;
    c.setColor(enabled ? (on ? pal::kOnAccent : pal::kTextColor) : pal::kDisabledColor);
    c.fillEllipse(Rect(kx, pill.y + 2.0f, d, d));

    if (!label.empty()) {
        c.setFont(Font::Body);
        c.setFontSize(geo::kBodySize);
        c.setColor(enabled ? pal::kTextColor : pal::kDisabledColor);
        const float maxW = std::max(0.0f, pill.x - geo::kPillGap - rect.x);
        const std::string t = c.clipToWidth(label, maxW);
        c.drawString(t.c_str(), rect.x, rect.centerY() + geo::kBodySize * geo::kLabelBaselineBias);
    }
}

bool PillToggle::hit(float x, float y) const
{
    return enabled && rect.contains(x, y);
}

// --- Chip -------------------------------------------------------------------

float chipWidth(Canvas &c, const char *label)
{
    c.setFont(Font::Body);
    c.setFontSize(geo::kChipTextSize);
    return c.stringWidth(label ? label : "") + 2.0f * geo::kChipPadX;
}

float drawChip(Canvas &c, float x, float cy, const char *label, uint32_t rgb)
{
    if (!label || !*label)
        return 0.0f;

    const float w = chipWidth(c, label);
    const Rect r(x, cy - geo::kChipH / 2.0f, w, geo::kChipH);

    // A wash of its own colour, outlined in it: enough to read as a badge at 10 units without
    // becoming a second thing competing with the device name beside it.
    c.setColor(rgb, 40);
    c.fillRoundRect(r, geo::kChipRadius);
    c.setColor(rgb, 170);
    c.setPenSize(1.0f);
    c.strokeRoundRect(r, geo::kChipRadius);

    c.setFont(Font::Body);
    c.setFontSize(geo::kChipTextSize);
    c.setColor(rgb);
    c.drawString(label, r.x + geo::kChipPadX,
                 r.centerY() + geo::kChipTextSize * geo::kLabelBaselineBias);
    return w;
}

} // namespace jackgraph
