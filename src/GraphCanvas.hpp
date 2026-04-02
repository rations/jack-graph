#pragma once

#include <gtkmm.h>
#include <vector>
#include <memory>
#include <string>
#include <map>
#include "Node.hpp"
#include "Connection.hpp"
#include "ClientBox.hpp"

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

    void layout(bool preserve_positions = false);

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
    void build_client_boxes();
    void draw_client_box(const Cairo::RefPtr<Cairo::Context>& cr, const ClientBox& box);
    void draw_port(const Cairo::RefPtr<Cairo::Context>& cr, const Node& node, double x, double y, bool is_output);
    void draw_connection_line(const Cairo::RefPtr<Cairo::Context>& cr, const Connection& conn);
    void draw_drag_preview(const Cairo::RefPtr<Cairo::Context>& cr);

    std::shared_ptr<Node> find_output_port_at(double x, double y);
    std::shared_ptr<Node> find_input_port_at(double x, double y);
    ClientBox* find_box_at(double x, double y);

    std::vector<std::shared_ptr<Node>> m_nodes;
    std::vector<std::shared_ptr<Connection>> m_connections;
    std::vector<ClientBox> m_client_boxes;

    struct SavedPosition {
        double x, y;
    };
    std::map<std::string, SavedPosition> m_saved_positions;

    double m_zoom;
    double m_offset_x;
    double m_offset_y;
    bool m_is_dragging;
    bool m_is_panning;
    bool m_is_moving_box;
    std::shared_ptr<Node> m_drag_source;
    ClientBox* m_moving_box;
    double m_drag_start_x;
    double m_drag_start_y;
    double m_drag_current_x;
    double m_drag_current_y;
    double m_pan_start_x;
    double m_pan_start_y;
    double m_box_move_offset_x;
    double m_box_move_offset_y;

    ConnectCallback m_connect_cb;
    DisconnectCallback m_disconnect_cb;

    static constexpr double BOX_PAD = 8;
    static constexpr double PORT_RADIUS = 6;
    static constexpr double BOX_GAP = 80;
    static constexpr double ROW_GAP = 10;
};
