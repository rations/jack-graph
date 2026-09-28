// See chrome.h.

#include "chrome.h"

#include "graphgeometry.h"
#include "gfx/ink.h"
#include "gfx/palette.h"

namespace jackgraph
{

//------------------------------------------------------------------------
void Chrome::setWindow(const Rect &r)
{
    mWindow = r;
    // The pills are placed from the left margin and keep their measured widths, so a resize does
    // not move them and does not need a re-measure.
}

void Chrome::layout(Canvas &c)
{
    if (mLaidOut)
        return;

    mRefresh.label = "Refresh";
    mZoomOut.label = "Zoom -";
    mZoomIn.label = "Zoom +";
    mZoomNormal.label = "Zoom 1:1";
    mFit.label = "Fit";
    mSettings.label = "JACK Settings";
    mAbout.label = "About";

    float x = geo::kGraphMargin;
    for (Tool t = Tool::Refresh; t <= Tool::About; t = static_cast<Tool>(static_cast<int>(t) + 1)) {
        Pill *p = pillFor(t);
        if (!p)
            continue;
        const float w = Pill::widthFor(c, p->label.c_str());
        p->rect = Rect(x, geo::kToolbarY, w, geo::kToolbarH);
        x += w + geo::kToolbarGap;
    }
    mLaidOut = true;
}

Rect Chrome::canvasRect() const
{
    return Rect(geo::kGraphMargin, geo::kCanvasTop, mWindow.w - 2.0f * geo::kGraphMargin,
                geo::canvasH(mWindow.h));
}

void Chrome::setStatus(const std::string &text)
{
    if (mStatus == text)
        return;
    mStatus = text;
    repaint();
}

void Chrome::setActive(Tool t, bool on)
{
    Pill *p = pillFor(t);
    if (!p || p->active == on)
        return;
    p->active = on;
    repaint();
}

//------------------------------------------------------------------------
void Chrome::draw(Canvas &c) const
{
    // The window ground is App's, filled before the graph; this draws only the chrome on top of it.
    mRefresh.draw(c);
    mZoomOut.draw(c);
    mZoomIn.draw(c);
    mZoomNormal.draw(c);
    mFit.draw(c);
    mSettings.draw(c);
    mAbout.draw(c);

    const Rect row(geo::kGraphMargin, mWindow.h - geo::kGraphMargin - geo::kStatusH,
                   mWindow.w - 2.0f * geo::kGraphMargin, geo::kStatusH);
    // CLIPPED, NOT WRAPPED. The status line is a single line of facts joined with " | " and it is
    // as long as the numbers in it; a second line would move the canvas every time the xrun count
    // gained a digit.
    drawRowText(c, row, mStatus.c_str(), pal::kDimColor, geo::kStatusSize);
}

//------------------------------------------------------------------------
Pill *Chrome::pillFor(Tool t)
{
    switch (t) {
    case Tool::Refresh:
        return &mRefresh;
    case Tool::ZoomOut:
        return &mZoomOut;
    case Tool::ZoomIn:
        return &mZoomIn;
    case Tool::ZoomNormal:
        return &mZoomNormal;
    case Tool::Fit:
        return &mFit;
    case Tool::Settings:
        return &mSettings;
    case Tool::About:
        return &mAbout;
    case Tool::None:
        break;
    }
    return nullptr;
}

const Pill *Chrome::pill(Tool t) const
{
    return const_cast<Chrome *>(this)->pillFor(t);
}

Tool Chrome::targetAt(float x, float y) const
{
    for (Tool t = Tool::Refresh; t <= Tool::About; t = static_cast<Tool>(static_cast<int>(t) + 1)) {
        const Pill *p = pill(t);
        if (p && p->hit(x, y))
            return t;
    }
    return Tool::None;
}

bool Chrome::press(float x, float y, int button)
{
    if (button != 1)
        return false;
    mPressTarget = targetAt(x, y);
    return mPressTarget != Tool::None;
}

bool Chrome::release(float x, float y, int button)
{
    if (button != 1)
        return false;
    const Tool was = mPressTarget;
    mPressTarget = Tool::None;
    if (was == Tool::None)
        return false;
    // Only if the pointer is still on the pill it went down on.
    if (targetAt(x, y) != was)
        return false;
    if (onTool)
        onTool(was);
    return true;
}

bool Chrome::motion(float x, float y)
{
    const Tool t = targetAt(x, y);
    if (t == mHoverTarget)
        return t != Tool::None;
    mHoverTarget = t;
    for (Tool k = Tool::Refresh; k <= Tool::About; k = static_cast<Tool>(static_cast<int>(k) + 1)) {
        Pill *p = pillFor(k);
        if (p)
            p->hovered = (k == t);
    }
    repaint();
    return t != Tool::None;
}

//------------------------------------------------------------------------
float AboutCard::layout()
{
    const float y = geo::kSetMargin + geo::nominalAscent(geo::kAboutTitleSize) +
                    geo::nominalDescent(geo::kAboutTitleSize) + geo::kAboutTitleGap +
                    static_cast<float>(geo::kAboutLines) * geo::kAboutLineH + geo::kSetGroupGap;

    mClose.label = "Close";
    mClose.rect = Rect(geo::kAboutW - geo::kSetMargin - geo::kSetButtonW, y, geo::kSetButtonW,
                       geo::kPillH);

    mHeight = y + geo::kPillH + geo::kSetMargin;
    return mHeight;
}

void AboutCard::draw(Canvas &c) const
{
    c.setColor(pal::kBgColor);
    c.fillRect(c.bounds());

    const float x = geo::kSetMargin;
    const float w = geo::kAboutW - 2.0f * geo::kSetMargin;

    float y = geo::kSetMargin + geo::nominalAscent(geo::kAboutTitleSize);
    c.setFont(Font::Title);
    c.setFontSize(geo::kAboutTitleSize);
    c.setColor(pal::kTextColor);
    c.drawString(c.clipToWidth(about::kName, w).c_str(), x, y);

    y += geo::nominalDescent(geo::kAboutTitleSize) + geo::kAboutTitleGap;

    const char *const lines[geo::kAboutLines] = {about::kVersion, about::kComment, about::kLicence};
    for (int i = 0; i < geo::kAboutLines; ++i) {
        const Rect row(x, y + static_cast<float>(i) * geo::kAboutLineH, w, geo::kAboutLineH);
        drawRowText(c, row, lines[i], i == 0 ? pal::kTextColor : pal::kDimColor,
                    geo::kAboutTextSize);
    }

    mClose.draw(c);
}

void AboutCard::press(float x, float y, int button)
{
    if (button != 1)
        return;
    mPressedClose = mClose.hit(x, y);
}

void AboutCard::release(float x, float y, int button)
{
    if (button != 1)
        return;
    const bool was = mPressedClose;
    mPressedClose = false;
    if (was && mClose.hit(x, y) && onClose)
        onClose();
}

void AboutCard::motion(float x, float y)
{
    const bool hot = mClose.hit(x, y);
    if (hot == mClose.hovered)
        return;
    mClose.hovered = hot;
    if (onNeedsRepaint)
        onNeedsRepaint();
}

} // namespace jackgraph
