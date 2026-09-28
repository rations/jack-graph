// The JACK graph itself: client boxes, ports, connections, and the drags that make and break them.
//
// This is GraphCanvas with Gtk::DrawingArea taken out from under it. The LAYOUT, the hit testing,
// pair_stereo_ports(), build_client_boxes(), fit_to_window() and the saved-position logic are moved
// ACROSS UNCHANGED -- they were always pure geometry over Node and ClientBox, which is the single
// largest piece of luck in this port. What changed is the surface: it draws through gfx::Canvas
// instead of a Cairo::Context, it carries its own rect instead of asking a parent widget for an
// allocation, and it returns whether it consumed an event instead of chaining to a base class.
//
// CAIRO ONLY. No Xlib, no libjack, no gtkmm. The Makefile's rule about src/gfx not linking X11
// covers this file too, and for the same reason: it is what lets the layout be composed and
// audited with no X server.
//
// THE PAN AND ZOOM ARE THE PANEL'S OWN and always were -- m_pan_x/m_pan_y predate the port. The
// Gtk::ScrolledWindow that wrapped this was therefore redundant scrolling on top of scrolling, and
// it is deleted rather than replaced.

#pragma once

#include "ClientBox.hpp"
#include "Connection.hpp"
#include "Node.hpp"
#include "gfx/canvas.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace jackgraph
{

class GraphPanel
{
public:
    // A drag from an output port to an input port completed, or a right-click landed on a
    // connection. They carry the NODES, not names: a JACK port is addressed by its full name, an
    // ALSA sequencer port by its client and port numbers, and only the node has both.
    //
    // onConnect RETURNS WHETHER IT WORKED, and the cable is drawn only if it did. The GTK build
    // drew it unconditionally, so a connection the server refused -- or one to an ALSA port, which
    // went nowhere at all -- sat on screen looking made until the next refresh.
    std::function<bool(const Node &source, const Node &dest)> onConnect;
    std::function<void(const Node &source, const Node &dest)> onDisconnect;

    // Something moved. The window turns this into one invalidate() per pass round its loop.
    std::function<void()> onNeedsRepaint;

    //--- where it is ----------------------------------------------------
    // The viewport, in window coordinates. Everything below is in CANVAS coordinates, which are
    // related by: window = rect.origin + pan + canvas * zoom.
    void setRect(const Rect &r);
    Rect rect() const
    {
        return mRect;
    }

    //--- content --------------------------------------------------------
    void clear();
    void addNode(std::shared_ptr<Node> node);
    void addConnection(std::shared_ptr<Connection> conn);
    // Drops everything, KEEPING each box's position under its client name, so a refresh does not
    // undo a layout the user arranged by hand.
    void removeAll();

    // FORGET WHERE THE USER PUT THINGS, so the next layout() places every box by the automatic
    // rule again. Only the toolbar's Refresh asks for this: a refresh JACK asked for -- a client
    // appearing, a port going away, a reconnect -- must leave an arrangement alone, or any
    // application opening a port would scatter the boxes the user just tidied.
    void forgetSavedPositions();

    const std::vector<std::shared_ptr<Node>> &nodes() const
    {
        return mNodes;
    }

    //--- view -----------------------------------------------------------
    void setZoom(double zoom);
    // Zoom keeping the canvas point under window point (wx, wy) where it is.
    void setZoomAround(double zoom, float wx, float wy);
    double zoom() const
    {
        return mZoom;
    }
    void layout(bool preservePositions = false);
    void fitToWindow();

    //--- paint and input ------------------------------------------------
    void draw(Canvas &c) const;

    // Each returns true if it consumed the event. Coordinates are in WINDOW space; the panel maps
    // them itself, because the mapping is its own pan and zoom.
    bool press(float x, float y, int button);
    bool release(float x, float y, int button);
    bool motion(float x, float y);
    bool scroll(float x, float y, int dir);

private:
    void buildClientBoxes();
    void drawClientBox(Canvas &c, const ClientBox &box) const;
    void drawPort(Canvas &c, const Node &node, float x, float y, bool isOutput,
                  bool twoSided) const;
    void drawConnection(Canvas &c, const Connection &conn) const;
    void drawDragPreview(Canvas &c) const;
    void positionPorts(ClientBox *box) const;

    // Whether a drag from `src` may end on `dst`: same kind of signal, same server. An audio port
    // on a MIDI port is refused by JACK anyway, and a JACK port cannot reach an ALSA one at all.
    static bool compatible(const Node &src, const Node &dst);
    // The cable under canvas point (x, y), measured against the curve actually drawn.
    int connectionAt(double x, double y) const;

    std::shared_ptr<Node> outputPortAt(double x, double y);
    std::shared_ptr<Node> inputPortAt(double x, double y);
    ClientBox *boxAt(double x, double y);

    // The box with this client name, or nullptr if the last refresh did not bring it back.
    ClientBox *boxFor(const std::string &client);

    // Window space to canvas space, and the inverse of what draw() sets up.
    double canvasX(float windowX) const
    {
        return (windowX - mRect.x - mPanX) / mZoom;
    }
    double canvasY(float windowY) const
    {
        return (windowY - mRect.y - mPanY) / mZoom;
    }
    void repaint() const
    {
        if (onNeedsRepaint)
            onNeedsRepaint();
    }

    Rect mRect;

    std::vector<std::shared_ptr<Node>> mNodes;
    std::vector<std::shared_ptr<Connection>> mConnections;
    std::vector<ClientBox> mClientBoxes;

    struct SavedPosition {
        double x, y;
    };
    // KEYED BY CLIENT NAME, and kept for the life of the window: a client that goes away and comes
    // back -- which is every JACK restart -- returns to where the user put it.
    std::map<std::string, SavedPosition> mSavedPositions;

    double mZoom = 1.0;
    double mPanX = 0.0;
    double mPanY = 0.0;

    bool mDragging = false;
    bool mPanning = false;
    bool mMovingBox = false;
    std::shared_ptr<Node> mDragSource;
    // THE BOX BEING MOVED IS HELD BY NAME, NOT BY POINTER. A port refresh can land in the middle of
    // a drag -- JACK decides when, not the user -- and it rebuilds mClientBoxes from scratch, so any
    // pointer into that vector is stale the moment it does. Holding the name means a refresh mid-drag
    // is invisible to the user: removeAll() saves the box's CURRENT position, layout(true) puts the
    // rebuilt box back there, and the next motion resolves the name to the new box and carries on.
    // The gtkmm build held a pointer and wrote through it after the vector was cleared.
    std::string mMovingBoxClient;
    double mDragCurrentX = 0.0;
    double mDragCurrentY = 0.0;
    float mPanStartX = 0.0f;
    float mPanStartY = 0.0f;
    double mBoxOffsetX = 0.0;
    double mBoxOffsetY = 0.0;
};

} // namespace jackgraph
