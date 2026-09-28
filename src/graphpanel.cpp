// See graphpanel.h.

#include "graphpanel.h"

#include "graphgeometry.h"
#include "gfx/ink.h"
#include "gfx/palette.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>

namespace jackgraph
{

namespace
{

// Move each "<base>R" port to sit immediately after its "<base>" port within the same column. Pure
// relocation -- all other ports keep their order, so MIDI stays on top and unrelated clients are
// unaffected. O(n^2) but n is tiny.
//
// MOVED ACROSS VERBATIM from GraphCanvas.cpp. It is what puts a stereo pair next to each other
// instead of at opposite ends of a box, and the reason it is a relocation rather than a sort is
// that a sort would reorder everything else too.
void pairStereoPorts(std::vector<std::shared_ptr<Node>> &ports)
{
    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t i = 0; i < ports.size(); ++i) {
            const std::string &nm = ports[i]->name; // full "client:port"
            if (nm.empty() || nm.back() != 'R')
                continue;
            std::string base = nm.substr(0, nm.size() - 1); // drop trailing 'R'

            size_t b = SIZE_MAX;
            for (size_t j = 0; j < ports.size(); ++j) {
                if (ports[j]->name == base) {
                    b = j;
                    break;
                }
            }
            if (b == SIZE_MAX)
                continue; // no matching base -- leave it alone
            if (i == b + 1)
                continue; // already right after its base

            auto node = ports[i];
            ports.erase(ports.begin() + static_cast<long>(i));
            if (b > i)
                --b; // base shifted left by the erase
            ports.insert(ports.begin() + static_cast<long>(b) + 1, node);
            changed = true; // indices moved -- rescan
            break;
        }
    }
}

constexpr float kBoxRadius = 6.0f;
constexpr float kPortRadius = 6.0f;
constexpr float kPortBgRadius = 3.0f;
// TEXT SIZES, AND THE ONE REASON THEY ARE ABOVE THE FAMILY'S BODY SIZE. Everywhere else in this
// project a logical unit IS a pixel, so geo::kBodySize lands on screen at the size it names. Not
// here: this canvas is drawn through a zoom, and the zoom a freshly opened window picks is
// fitToWindow's, which is below 1 for any graph wider than the viewport -- a typical desktop
// graph fits at about 0.9, and a busy one a good deal less. A 13-unit name drawn at 0.76 is 9
// pixels tall, which is what "the font is too small in the boxes" was: the number was fine and
// the zoom ate it.
//
// So these are sized to be right AFTER that scaling rather than before it, and kBoxColGap below is
// part of the same fix -- gap is pure whitespace, and whitespace in the content bounds comes
// straight off the fit zoom, which comes straight off every glyph on screen.
constexpr float kHeaderTextSize = 16.0f;
constexpr float kPortTextSize = 14.0f;
constexpr float kLayoutMargin = 20.0f;
constexpr float kBoxRowGap = 10.0f;
// THE GAP BETWEEN THE THREE COLUMNS, and it is not free. It was 250, which put 500 units of empty
// canvas into content bounds only 948 units of which were boxes -- a third of the window's width
// spent on nothing, and since fitToWindow divides the viewport by those bounds, it shrank every
// box and every label by a quarter to make room for it. 120 still reads as three distinct columns
// and still leaves a cable room to curve.
constexpr float kBoxColGap = 120.0f;

} // namespace

//------------------------------------------------------------------------
void GraphPanel::setRect(const Rect &r)
{
    mRect = r;
}

void GraphPanel::clear()
{
    mNodes.clear();
    mConnections.clear();
    mClientBoxes.clear();
    repaint();
}

void GraphPanel::addNode(std::shared_ptr<Node> node)
{
    mNodes.push_back(std::move(node));
}

void GraphPanel::addConnection(std::shared_ptr<Connection> conn)
{
    mConnections.push_back(std::move(conn));
}

void GraphPanel::removeAll()
{
    for (const ClientBox &box : mClientBoxes)
        mSavedPositions[box.client_name] = {box.x, box.y};
    mNodes.clear();
    mConnections.clear();
    mClientBoxes.clear();

    // A DRAG IN FLIGHT SURVIVES THIS, and is meant to. The loop above has just saved the dragged
    // box's CURRENT position, layout(true) puts the rebuilt box back on it, and the next motion
    // resolves mMovingBoxClient to that box -- so a refresh arriving mid-drag is invisible to the
    // hand holding the mouse. This used to cancel the drag instead, to stop a ClientBox* dangling
    // past the clear() above; holding the name removes the pointer and with it the reason to
    // cancel. Cancelling made a box unmovable on any server that refreshes often, which was every
    // server for as long as App::updateStatus() caused a refresh of its own.

    repaint();
}

void GraphPanel::setZoom(double zoom)
{
    mZoom = std::max(geo::kZoomMin, std::min(zoom, geo::kZoomMax));
    repaint();
}

// ZOOM ABOUT A POINT: the canvas point under (wx, wy) stays under it. The wheel passes the pointer,
// so zooming in on a port keeps that port in view; the toolbar passes the viewport's centre.
// GraphCanvas always scaled about the canvas origin, which slid whatever you were looking at off
// towards the top-left corner with every step.
void GraphPanel::setZoomAround(double zoom, float wx, float wy)
{
    const double cx = canvasX(wx);
    const double cy = canvasY(wy);
    mZoom = std::max(geo::kZoomMin, std::min(zoom, geo::kZoomMax));
    // window = rect.origin + pan + canvas * zoom, solved for pan with the canvas point held.
    mPanX = (wx - mRect.x) - cx * mZoom;
    mPanY = (wy - mRect.y) - cy * mZoom;
    repaint();
}

//------------------------------------------------------------------------
void GraphPanel::fitToWindow()
{
    if (mClientBoxes.empty())
        return;

    double minX = mClientBoxes[0].x;
    double minY = mClientBoxes[0].y;
    double maxX = mClientBoxes[0].x + mClientBoxes[0].width;
    double maxY = mClientBoxes[0].y + mClientBoxes[0].height;

    for (const ClientBox &box : mClientBoxes) {
        minX = std::min(minX, box.x);
        minY = std::min(minY, box.y);
        maxX = std::max(maxX, box.x + box.width);
        maxY = std::max(maxY, box.y + box.height);
    }

    const double contentW = maxX - minX;
    const double contentH = maxY - minY;
    if (contentW <= 0.0 || contentH <= 0.0)
        return;

    // THE VIEWPORT IS THIS PANEL'S OWN RECT. GraphCanvas asked get_parent()->get_allocation() for
    // it, which is the one line of that function that could not come across: there is no parent
    // widget, and the window tells the panel its rect on every ConfigureNotify instead.
    const double viewW = mRect.w;
    const double viewH = mRect.h;
    if (viewW <= 1.0 || viewH <= 1.0)
        return;

    const double newZoom = std::min((viewW - geo::kFitMargin * 2.0) / contentW,
                                    (viewH - geo::kFitMargin * 2.0) / contentH);
    mZoom = std::max(geo::kZoomMin, std::min(newZoom, geo::kFitZoomMax));

    // screen = canvas * zoom + pan  =>  pan = screen_centre - canvas_centre * zoom
    mPanX = viewW / 2.0 - ((minX + maxX) / 2.0) * mZoom;
    mPanY = viewH / 2.0 - ((minY + maxY) / 2.0) * mZoom;

    repaint();
}

//------------------------------------------------------------------------
void GraphPanel::buildClientBoxes()
{
    mClientBoxes.clear();

    std::map<std::string, std::unique_ptr<ClientBox>> boxes;

    for (auto &node : mNodes) {
        const std::string cname = node->client_name;
        if (cname.empty())
            continue;
        if (boxes.find(cname) == boxes.end())
            boxes[cname] = std::make_unique<ClientBox>(cname, node->is_alsa);
        boxes[cname]->add_port(node);
    }

    for (auto &entry : boxes) {
        pairStereoPorts(entry.second->inputs);
        pairStereoPorts(entry.second->outputs);
        mClientBoxes.push_back(std::move(*entry.second));
    }
}

void GraphPanel::positionPorts(ClientBox *box) const
{
    for (size_t i = 0; i < box->inputs.size(); ++i) {
        box->inputs[i]->x = box->x + ClientBox::SIDE_PAD;
        box->inputs[i]->y = box->y + ClientBox::HEADER_HEIGHT + ClientBox::PORT_PAD +
                            static_cast<double>(i) *
                                (ClientBox::PORT_HEIGHT + ClientBox::PORT_PAD);
        box->inputs[i]->width = ClientBox::COL_WIDTH;
        box->inputs[i]->height = ClientBox::PORT_HEIGHT;
    }
    for (size_t i = 0; i < box->outputs.size(); ++i) {
        box->outputs[i]->x =
            box->x + ClientBox::SIDE_PAD + ClientBox::COL_WIDTH + ClientBox::SIDE_PAD;
        box->outputs[i]->y = box->y + ClientBox::HEADER_HEIGHT + ClientBox::PORT_PAD +
                             static_cast<double>(i) *
                                 (ClientBox::PORT_HEIGHT + ClientBox::PORT_PAD);
        box->outputs[i]->width = ClientBox::COL_WIDTH;
        box->outputs[i]->height = ClientBox::PORT_HEIGHT;
    }
}

void GraphPanel::layout(bool preservePositions)
{
    if (preservePositions) {
        for (const ClientBox &box : mClientBoxes)
            mSavedPositions[box.client_name] = {box.x, box.y};
    }

    buildClientBoxes();

    // Split boxes by signal role:
    //   sources -- OUTPUT ports only  -> left column  (system:capture, apps)
    //   sinks   -- INPUT ports only   -> right column (system:playback)
    //   mixed   -- both directions    -> middle column
    std::vector<ClientBox *> sources, sinks, mixed;
    for (ClientBox &box : mClientBoxes) {
        if (!box.outputs.empty() && box.inputs.empty())
            sources.push_back(&box);
        else if (box.outputs.empty() && !box.inputs.empty())
            sinks.push_back(&box);
        else
            mixed.push_back(&box);
    }

    // Sort each column so MIDI clients appear above audio clients.
    auto isMidi = [](const ClientBox *b) {
        for (const auto &p : b->inputs) {
            if (p->type == PortType::MIDI)
                return true;
        }
        for (const auto &p : b->outputs) {
            if (p->type == PortType::MIDI)
                return true;
        }
        return false;
    };
    auto midiFirst = [&](const ClientBox *a, const ClientBox *b) { return isMidi(a) > isMidi(b); };
    std::stable_sort(sources.begin(), sources.end(), midiFirst);
    std::stable_sort(sinks.begin(), sinks.end(), midiFirst);
    std::stable_sort(mixed.begin(), mixed.end(), midiFirst);

    double maxBoxWidth = 0.0;
    for (const ClientBox &box : mClientBoxes)
        maxBoxWidth = std::max(maxBoxWidth, box.width);

    const double leftX = kLayoutMargin;
    const double midX = kLayoutMargin + maxBoxWidth + kBoxColGap;
    const double rightX = kLayoutMargin + maxBoxWidth * 2.0 + kBoxColGap * 2.0;

    double leftY = kLayoutMargin;
    double midY = kLayoutMargin;
    double rightY = kLayoutMargin;

    // Pass 1: restore saved boxes at their saved positions and find the lowest occupied Y in each
    // column so new boxes never overlap them.
    auto placeSaved = [&](ClientBox *box, double &colFloor) {
        auto it = mSavedPositions.find(box->client_name);
        if (it == mSavedPositions.end())
            return;
        box->x = it->second.x;
        box->y = it->second.y;
        positionPorts(box);
        colFloor = std::max(colFloor, box->y + box->height + kBoxRowGap);
    };

    for (ClientBox *box : sources)
        placeSaved(box, leftY);
    for (ClientBox *box : mixed)
        placeSaved(box, midY);
    for (ClientBox *box : sinks)
        placeSaved(box, rightY);

    // Pass 2: place new (unsaved) boxes below all saved content in their column.
    auto placeNew = [&](ClientBox *box, double colX, double &colY) {
        if (mSavedPositions.count(box->client_name))
            return;
        box->x = colX;
        box->y = colY;
        positionPorts(box);
        colY += box->height + kBoxRowGap;
    };

    for (ClientBox *box : sources)
        placeNew(box, leftX, leftY);
    for (ClientBox *box : mixed)
        placeNew(box, midX, midY);
    for (ClientBox *box : sinks)
        placeNew(box, rightX, rightY);

    // GraphCanvas ended here by calling get_parent()->set_size_request() with the content bounds,
    // which grew the Gtk::ScrolledWindow's scrollable area. There is no scrolled window: this panel
    // pans itself and always did, so the request has nothing to ask and nothing to ask it of.

    repaint();
}

//------------------------------------------------------------------------
void GraphPanel::draw(Canvas &c) const
{
    c.setColor(pal::graph::kGround);
    c.fillRect(mRect);

    // CLIPPED TO THE VIEWPORT, then panned and zoomed. Both have to be inside the same save/restore
    // as the drawing, and the clip has to come first: a box dragged off the top of the canvas would
    // otherwise be drawn over the toolbar.
    c.pushClip(mRect);
    cairo_save(c.cr());
    cairo_translate(c.cr(), mRect.x + mPanX, mRect.y + mPanY);
    cairo_scale(c.cr(), mZoom, mZoom);

    // Connections UNDER the boxes, so a cable passing behind a client does not draw over its ports.
    for (const auto &conn : mConnections)
        drawConnection(c, *conn);

    for (const ClientBox &box : mClientBoxes)
        drawClientBox(c, box);

    if (mDragging && mDragSource)
        drawDragPreview(c);

    cairo_restore(c.cr());
    c.popClip();
}

void GraphPanel::drawClientBox(Canvas &c, const ClientBox &box) const
{
    const Rect r(static_cast<float>(box.x), static_cast<float>(box.y),
                 static_cast<float>(box.width), static_cast<float>(box.height));

    c.setColor(pal::graph::kBoxFill, 242); // 0.95
    c.fillRoundRect(r, kBoxRadius);
    c.setColor(pal::graph::kBoxBorder, 204); // 0.8
    c.setPenSize(1.5f);
    c.strokeRoundRect(r, kBoxRadius);

    // The rule under the header.
    c.setColor(pal::graph::kBoxBorder, 128); // 0.5
    c.setPenSize(1.0f);
    c.strokeLine(r.x, r.y + ClientBox::HEADER_HEIGHT, r.right(),
                 r.y + ClientBox::HEADER_HEIGHT);

    // The client name, centred in the header.
    //
    // NOT BOLD, and not in the display face. The old header was Pango WEIGHT_BOLD, and the bundled
    // stack has one weight of Roboto and Michroma -- which is a display face far too wide for a
    // 140-unit column. Drawing it in the brighter kHeaderText against the ports' dimmer kPortText
    // is what separates the two here, which is the distinction the weight was carrying.
    c.setFont(Font::Body);
    c.setFontSize(kHeaderTextSize);
    c.setColor(pal::graph::kHeaderText);
    {
        const std::string name = c.clipToWidth(box.client_name, r.w - 8.0f);
        const float w = c.stringWidth(name.c_str());
        c.drawString(name.c_str(), r.centerX() - w * 0.5f,
                     r.y + ClientBox::HEADER_HEIGHT * 0.5f +
                         kHeaderTextSize * geo::kLabelBaselineBias);
    }

    const float inX = r.x + ClientBox::SIDE_PAD;
    const float outX = r.x + ClientBox::SIDE_PAD + ClientBox::COL_WIDTH + ClientBox::SIDE_PAD;

    // A box with ports on BOTH sides gives each side only its own column. The wider background
    // is for a one-sided box, whose other column is empty; drawn in a two-sided one, the two sides'
    // backgrounds and names overlapped in the middle -- which every duplex ALSA MIDI device, and any
    // JACK client with inputs and outputs, showed as "Arturia MiniLab mkIArturia...".
    const bool twoSided = !box.inputs.empty() && !box.outputs.empty();

    float portY = r.y + ClientBox::HEADER_HEIGHT + ClientBox::PORT_PAD;
    for (const auto &node : box.inputs) {
        drawPort(c, *node, inX, portY, false, twoSided);
        portY += ClientBox::PORT_HEIGHT + ClientBox::PORT_PAD;
    }

    portY = r.y + ClientBox::HEADER_HEIGHT + ClientBox::PORT_PAD;
    for (const auto &node : box.outputs) {
        drawPort(c, *node, outX, portY, true, twoSided);
        portY += ClientBox::PORT_HEIGHT + ClientBox::PORT_PAD;
    }
}

void GraphPanel::drawPort(Canvas &c, const Node &node, float x, float y, bool isOutput,
                          bool twoSided) const
{
    const float w = twoSided ? ClientBox::COL_WIDTH : ClientBox::PORT_BG_WIDTH;
    const float h = ClientBox::PORT_HEIGHT;

    // BLUE IS AUDIO, GREEN IS MIDI, and it is the only thing on screen saying which a port is.
    const uint32_t rgb =
        node.type == PortType::AUDIO ? pal::graph::kPortAudio : pal::graph::kPortMidi;

    // An output port's background extends LEFT of its column, so the two columns' backgrounds meet
    // their own edge of the box and the dots sit on the outside.
    // Either way the dot lands where ClientBox's hit test looks for it: an input's at x, an
    // output's at x + COL_WIDTH.
    const float bgX = isOutput ? x + ClientBox::COL_WIDTH - w : x;
    const Rect bg(bgX, y, w, h);

    c.setColor(rgb, 31); // 0.12
    c.fillRoundRect(bg, kPortBgRadius);

    const float dotX = isOutput ? bg.right() : bg.x;
    c.setColor(rgb, 242); // 0.95
    c.fillEllipse(dotX, y + h * 0.5f, kPortRadius, kPortRadius);

    c.setFont(Font::Body);
    c.setFontSize(kPortTextSize);
    c.setColor(pal::graph::kPortText);

    // clipToWidth replaces the hand-written truncation loop, which re-measured the string once per
    // character removed and appended "..." rather than an ellipsis. Same result, one measurement,
    // and it cuts on whole UTF-8 characters -- a JACK client may be named in any encoding it likes.
    const float maxTextW = w - kPortRadius * 2.0f - 12.0f;
    const std::string text = c.clipToWidth(node.display_name(), maxTextW);
    const float tw = c.stringWidth(text.c_str());
    const float textX = isOutput ? bg.right() - kPortRadius - 4.0f - tw
                                 : bg.x + kPortRadius + 4.0f;
    c.drawString(text.c_str(), textX, y + h * 0.5f + kPortTextSize * geo::kLabelBaselineBias);
}

void GraphPanel::drawConnection(Canvas &c, const Connection &conn) const
{
    const float x1 = static_cast<float>(conn.source->x + conn.source->width);
    const float y1 = static_cast<float>(conn.source->y + conn.source->height / 2.0);
    const float x2 = static_cast<float>(conn.destination->x);
    const float y2 = static_cast<float>(conn.destination->y + conn.destination->height / 2.0);

    c.setColor(pal::graph::kConnector, 128); // 0.5
    c.setPenSize(2.0f);
    // The slack the old curve_to used: half the horizontal distance, on both control points.
    c.strokeConnector(x1, y1, x2, y2, std::fabs(x2 - x1) * 0.5f);
}

void GraphPanel::drawDragPreview(Canvas &c) const
{
    const float x1 = static_cast<float>(mDragSource->x + mDragSource->width);
    const float y1 = static_cast<float>(mDragSource->y + mDragSource->height / 2.0);
    const float x2 = static_cast<float>(mDragCurrentX);
    const float y2 = static_cast<float>(mDragCurrentY);

    // DASHED, and that is the whole point of the stroke: it is what says this cable is proposed
    // rather than made. Drawing it solid would make an in-flight drag indistinguishable from a
    // connection that already exists.
    static const float kDashes[] = {5.0f, 3.0f};
    c.setColor(pal::graph::kPreview, 178); // 0.7
    c.setPenSize(2.0f);
    c.setDash(kDashes, 2);
    c.strokeConnector(x1, y1, x2, y2, std::fabs(x2 - x1) * 0.5f);
    c.clearDash();
}

//------------------------------------------------------------------------
// ALL THREE HIT TESTS WALK THE BOXES LAST TO FIRST. draw() paints them first to last, so the last
// one is on top, and where two boxes overlap a click belongs to the one the user can see. Walking
// first to last, as GraphCanvas did, grabbed the box underneath.
//
// A port dot sits on the box's edge, half outside it, so the port tests widen each box by the dot's
// hit radius before asking it: a press on the outer half of a dot is still a press on the port.
std::shared_ptr<Node> GraphPanel::outputPortAt(double x, double y)
{
    constexpr double kReach = 10.0; // ClientBox::find_output_at's own radius
    for (auto it = mClientBoxes.rbegin(); it != mClientBoxes.rend(); ++it) {
        ClientBox &box = *it;
        if (x >= box.x - kReach && x <= box.x + box.width + kReach && y >= box.y &&
            y <= box.y + box.height) {
            if (auto node = box.find_output_at(x - box.x, y - box.y))
                return node;
        }
    }
    return nullptr;
}

std::shared_ptr<Node> GraphPanel::inputPortAt(double x, double y)
{
    constexpr double kReach = 10.0;
    for (auto it = mClientBoxes.rbegin(); it != mClientBoxes.rend(); ++it) {
        ClientBox &box = *it;
        if (x >= box.x - kReach && x <= box.x + box.width + kReach && y >= box.y &&
            y <= box.y + box.height) {
            if (auto node = box.find_input_at(x - box.x, y - box.y))
                return node;
        }
    }
    return nullptr;
}

ClientBox *GraphPanel::boxAt(double x, double y)
{
    for (auto it = mClientBoxes.rbegin(); it != mClientBoxes.rend(); ++it) {
        if (it->contains(x, y))
            return &*it;
    }
    return nullptr;
}

bool GraphPanel::compatible(const Node &src, const Node &dst)
{
    return src.type == dst.type && src.is_alsa == dst.is_alsa;
}

// Distance to the CURVE, not to the straight line between its ends. The chord test this replaces
// missed any cable that bowed far from its chord -- which is every long cable between two boxes at
// different heights, since the control points pull the curve horizontal at both ends -- and could
// catch a click on empty canvas beside the chord. Sampling the same cubic drawConnection() strokes
// is cheap at these counts and is exactly what the user is aiming at.
int GraphPanel::connectionAt(double x, double y) const
{
    constexpr int kSteps = 32;
    // Ten canvas units, as before, but never less than about four pixels on screen, so a cable is
    // still a target when the graph is zoomed out.
    const double tol = std::max(10.0, 4.0 / mZoom);

    int best = -1;
    double bestD2 = tol * tol;
    for (size_t i = 0; i < mConnections.size(); ++i) {
        const Connection &conn = *mConnections[i];
        const double x0 = conn.source->x + conn.source->width;
        const double y0 = conn.source->y + conn.source->height / 2.0;
        const double x3 = conn.destination->x;
        const double y3 = conn.destination->y + conn.destination->height / 2.0;
        // strokeConnector's control points: `slack` out horizontally from each end.
        const double slack = std::fabs(x3 - x0) * 0.5;
        const double x1 = x0 + slack, y1 = y0;
        const double x2 = x3 - slack, y2 = y3;

        // Cheap reject: the curve lies inside the hull of its four points.
        if (x < std::min({x0, x1, x2, x3}) - tol || x > std::max({x0, x1, x2, x3}) + tol ||
            y < std::min(y0, y3) - tol || y > std::max(y0, y3) + tol)
            continue;

        double px = x0, py = y0;
        for (int k = 1; k <= kSteps; ++k) {
            const double t = static_cast<double>(k) / kSteps;
            const double u = 1.0 - t;
            const double qx = u * u * u * x0 + 3 * u * u * t * x1 + 3 * u * t * t * x2 + t * t * t * x3;
            const double qy = u * u * u * y0 + 3 * u * u * t * y1 + 3 * u * t * t * y2 + t * t * t * y3;
            // Distance from (x, y) to the segment p..q.
            const double dx = qx - px, dy = qy - py;
            const double len2 = dx * dx + dy * dy;
            double s = len2 > 0.0 ? ((x - px) * dx + (y - py) * dy) / len2 : 0.0;
            s = std::max(0.0, std::min(1.0, s));
            const double ex = px + s * dx - x, ey = py + s * dy - y;
            const double d2 = ex * ex + ey * ey;
            if (d2 < bestD2) {
                bestD2 = d2;
                best = static_cast<int>(i);
            }
            px = qx;
            py = qy;
        }
    }
    return best;
}

void GraphPanel::forgetSavedPositions()
{
    mSavedPositions.clear();
}

ClientBox *GraphPanel::boxFor(const std::string &client)
{
    for (ClientBox &box : mClientBoxes) {
        if (box.client_name == client)
            return &box;
    }
    return nullptr;
}

//------------------------------------------------------------------------
bool GraphPanel::press(float wx, float wy, int button)
{
    if (!mRect.contains(wx, wy))
        return false;

    const double x = canvasX(wx);
    const double y = canvasY(wy);

    if (button == 1) {
        // A PORT FIRST, THEN A BOX, THEN THE GROUND. The order is the whole interaction: a port dot
        // sits inside its box, so testing the box first would make every port drag a box move.
        if (auto output = outputPortAt(x, y)) {
            mDragSource = output;
            mDragCurrentX = x;
            mDragCurrentY = y;
            mDragging = true;
            repaint();
            return true;
        }

        if (ClientBox *box = boxAt(x, y)) {
            mMovingBox = true;
            mMovingBoxClient = box->client_name;
            mBoxOffsetX = x - box->x;
            mBoxOffsetY = y - box->y;
            return true;
        }

        mPanning = true;
        mPanStartX = wx;
        mPanStartY = wy;
        return true;
    }

    if (button == 3) {
        // RIGHT-CLICK ON A CABLE DISCONNECTS IT.
        const int hit = connectionAt(x, y);
        if (hit >= 0) {
            const std::shared_ptr<Connection> conn = mConnections[static_cast<size_t>(hit)];
            if (onDisconnect)
                onDisconnect(*conn->source, *conn->destination);
            mConnections.erase(mConnections.begin() + hit);
            repaint();
            return true;
        }
    }

    return false;
}

bool GraphPanel::release(float wx, float wy, int button)
{
    if (mDragging && mDragSource && button == 1) {
        const double x = canvasX(wx);
        const double y = canvasY(wy);

        auto target = inputPortAt(x, y);
        if (target && target != mDragSource && compatible(*mDragSource, *target) && onConnect &&
            onConnect(*mDragSource, *target)) {
            // Drawn immediately rather than waiting for the server's callback to come back round:
            // the cable appears under the pointer that made it. Only once the server has said yes,
            // though -- see onConnect in the header.
            mConnections.push_back(std::make_shared<Connection>(mDragSource, target,
                                                                mDragSource->type,
                                                                mDragSource->is_alsa));
        }

        mDragging = false;
        mDragSource = nullptr;
        repaint();
        return true;
    }

    if (mMovingBox) {
        mMovingBox = false;
        mMovingBoxClient.clear();
        return true;
    }

    if (mPanning) {
        mPanning = false;
        return true;
    }

    return false;
}

bool GraphPanel::motion(float wx, float wy)
{
    if (mDragging) {
        mDragCurrentX = canvasX(wx);
        mDragCurrentY = canvasY(wy);
        repaint();
        return true;
    }

    if (mMovingBox) {
        // RESOLVED ON EVERY MOTION, not once at press: see mMovingBoxClient in the header. A client
        // that left the graph during its own drag has nothing left to move, so the drag ends quietly
        // rather than inventing a box to put somewhere.
        ClientBox *box = boxFor(mMovingBoxClient);
        if (!box) {
            mMovingBox = false;
            mMovingBoxClient.clear();
            return false;
        }
        box->x = canvasX(wx) - mBoxOffsetX;
        box->y = canvasY(wy) - mBoxOffsetY;
        // The ports move with the box, and so do the cables, because a Connection reads its
        // endpoints out of the Nodes rather than caching them.
        positionPorts(box);
        repaint();
        return true;
    }

    if (mPanning) {
        mPanX += wx - mPanStartX;
        mPanY += wy - mPanStartY;
        mPanStartX = wx;
        mPanStartY = wy;
        repaint();
        return true;
    }

    return false;
}

bool GraphPanel::scroll(float wx, float wy, int dir)
{
    if (!mRect.contains(wx, wy))
        return false;
    // dir is X11Window's convention: -1 is button 4, the wheel pushed AWAY from the user, which
    // zooms IN. GraphCanvas read GDK_SCROLL_UP for the same notch.
    setZoomAround(dir < 0 ? mZoom * geo::kZoomStepWheel : mZoom / geo::kZoomStepWheel, wx, wy);
    return true;
}

} // namespace jackgraph
