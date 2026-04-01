#pragma once

#include <gtkmm.h>
#include <vector>
#include <memory>
#include <string>
#include "Node.hpp"
#include "Connection.hpp"

class GraphCanvas : public Gtk::DrawingArea {
public:
    GraphCanvas();
    ~GraphCanvas() override;

    void clear();
    void add_node(std::shared_ptr<Node> node);
    void add_connection(std::shared_ptr<Connection> conn);
    void remove_all();

    void set_zoom(double zoom);
    double get_zoom() const { return m_zoom; }

    void layout();

    const std::vector<std::shared_ptr<Node>>& get_nodes() const { return m_nodes; }
    const std::vector<std::shared_ptr<Connection>>& get_connections() const { return m_connections; }

    using ConnectCallback = std::function<void(const std::string& source, const std::string& dest)>;
    void set_connect_callback(ConnectCallback cb) { m_connect_cb = std::move(cb); }

    using DisconnectCallback = std::function<void(const std::string& source, const std::string& dest)>;
    void set_disconnect_callback(DisconnectCallback cb) { m_disconnect_cb = std::move(cb); }

protected:
    bool on_draw(const Cairo::RefPtr<Cairo::Context>& cr) override;
    bool on_button_press_event(GdkEventButton* event) override;
    bool on_button_release_event(GdkEventButton* event) override;
    bool on_motion_notify_event(GdkEventMotion* event) override;
    bool on_scroll_event(GdkEventScroll* event) override;

private:
    void draw_node(const Cairo::RefPtr<Cairo::Context>& cr, const Node& node);
    void draw_connection_line(const Cairo::RefPtr<Cairo::Context>& cr, const Connection& conn);
    void draw_drag_preview(const Cairo::RefPtr<Cairo::Context>& cr);

    std::shared_ptr<Node> find_node_at(double x, double y);
    bool is_on_output_port(const Node& node, double x, double y);
    bool is_on_input_port(const Node& node, double x, double y);

    std::vector<std::shared_ptr<Node>> m_nodes;
    std::vector<std::shared_ptr<Connection>> m_connections;

    double m_zoom;
    double m_offset_x;
    double m_offset_y;
    bool m_is_dragging;
    bool m_is_panning;
    std::shared_ptr<Node> m_drag_source;
    double m_drag_start_x;
    double m_drag_start_y;
    double m_drag_current_x;
    double m_drag_current_y;
    double m_pan_start_x;
    double m_pan_start_y;

    ConnectCallback m_connect_cb;
    DisconnectCallback m_disconnect_cb;

    static constexpr double NODE_WIDTH = 200;
    static constexpr double NODE_HEIGHT = 28;
    static constexpr double PORT_RADIUS = 6;
    static constexpr double COLUMN_GAP = 400;
    static constexpr double ROW_GAP = 36;
};
