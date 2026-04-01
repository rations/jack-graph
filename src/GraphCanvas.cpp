#include "GraphCanvas.hpp"
#include <algorithm>
#include <map>
#include <vector>

GraphCanvas::GraphCanvas()
    : m_zoom(1.0), m_offset_x(20), m_offset_y(20),
      m_is_dragging(false), m_is_panning(false),
      m_drag_source(nullptr),
      m_drag_start_x(0), m_drag_start_y(0),
      m_drag_current_x(0), m_drag_current_y(0),
      m_pan_start_x(0), m_pan_start_y(0) {
    add_events(Gdk::BUTTON_PRESS_MASK | Gdk::BUTTON_RELEASE_MASK |
               Gdk::POINTER_MOTION_MASK | Gdk::SCROLL_MASK);
}

GraphCanvas::~GraphCanvas() = default;

void GraphCanvas::clear() {
    m_nodes.clear();
    m_connections.clear();
    queue_draw();
}

void GraphCanvas::add_node(std::shared_ptr<Node> node) {
    m_nodes.push_back(std::move(node));
}

void GraphCanvas::add_connection(std::shared_ptr<Connection> conn) {
    m_connections.push_back(std::move(conn));
}

void GraphCanvas::remove_all() {
    m_nodes.clear();
    m_connections.clear();
    queue_draw();
}

void GraphCanvas::set_zoom(double zoom) {
    m_zoom = std::max(0.25, std::min(zoom, 4.0));
    queue_draw();
}

void GraphCanvas::layout() {
    struct ClientGroup {
        std::vector<std::shared_ptr<Node>> outputs;
        std::vector<std::shared_ptr<Node>> inputs;
    };

    std::map<std::string, ClientGroup> clients;

    for (auto& node : m_nodes) {
        if (!node->client_name.empty()) {
            clients[node->client_name];
            if (node->direction == PortDirection::OUTPUT) {
                clients[node->client_name].outputs.push_back(node);
            } else {
                clients[node->client_name].inputs.push_back(node);
            }
        }
    }

    double left_x = m_offset_x;
    double right_x = m_offset_x + COLUMN_GAP;
    double y = m_offset_y;
    double max_y = y;

    for (auto& [name, group] : clients) {
        double client_y = y;

        for (auto& node : group.outputs) {
            node->x = left_x;
            node->y = client_y;
            node->width = NODE_WIDTH;
            node->height = NODE_HEIGHT;
            client_y += ROW_GAP;
        }

        double input_y = y;
        for (auto& node : group.inputs) {
            node->x = right_x;
            node->y = input_y;
            node->width = NODE_WIDTH;
            node->height = NODE_HEIGHT;
            input_y += ROW_GAP;
        }

        y = std::max(client_y, input_y) + ROW_GAP;
        max_y = std::max(max_y, y);
    }

    double canvas_width = right_x + NODE_WIDTH + m_offset_x * 2;
    double canvas_height = max_y + m_offset_x * 2;

    auto parent = get_parent();
    if (parent) {
        parent->set_size_request(
            static_cast<int>(canvas_width),
            static_cast<int>(canvas_height));
    }

    queue_draw();
}

bool GraphCanvas::on_draw(const Cairo::RefPtr<Cairo::Context>& cr) {
    cr->set_source_rgb(0.92, 0.93, 0.95);
    cr->paint();

    cr->save();
    cr->scale(m_zoom, m_zoom);

    for (auto& conn : m_connections) {
        draw_connection_line(cr, *conn);
    }

    for (auto& node : m_nodes) {
        draw_node(cr, *node);
    }

    if (m_is_dragging && m_drag_source) {
        draw_drag_preview(cr);
    }

    cr->restore();
    return true;
}

void GraphCanvas::draw_node(const Cairo::RefPtr<Cairo::Context>& cr, const Node& node) {
    double x = node.x;
    double y = node.y;
    double w = node.width;
    double h = node.height;

    double radius = 4;

    double r, g, b;
    if (node.type == PortType::AUDIO) {
        r = 0.25; g = 0.45; b = 0.85;
    } else {
        r = 0.20; g = 0.70; b = 0.35;
    }

    cr->set_source_rgba(r, g, b, 0.10);
    cr->move_to(x + radius, y);
    cr->line_to(x + w - radius, y);
    cr->arc(x + w - radius, y + radius, radius, -M_PI / 2, 0);
    cr->line_to(x + w, y + h - radius);
    cr->arc(x + w - radius, y + h - radius, radius, 0, M_PI / 2);
    cr->line_to(x + radius, y + h);
    cr->arc(x + radius, y + h - radius, radius, M_PI / 2, M_PI);
    cr->line_to(x, y + radius);
    cr->arc(x + radius, y + radius, radius, M_PI, 3 * M_PI / 2);
    cr->close_path();
    cr->fill();

    cr->set_source_rgba(r, g, b, 0.85);
    cr->set_line_width(1.5);
    cr->move_to(x + radius, y);
    cr->line_to(x + w - radius, y);
    cr->arc(x + w - radius, y + radius, radius, -M_PI / 2, 0);
    cr->line_to(x + w, y + h - radius);
    cr->arc(x + w - radius, y + h - radius, radius, 0, M_PI / 2);
    cr->line_to(x + radius, y + h);
    cr->arc(x + radius, y + h - radius, radius, M_PI / 2, M_PI);
    cr->line_to(x, y + radius);
    cr->arc(x + radius, y + radius, radius, M_PI, 3 * M_PI / 2);
    cr->close_path();
    cr->stroke();

    double port_x = (node.direction == PortDirection::OUTPUT) ? x + w : x;
    double port_y = y + h / 2;
    cr->arc(port_x, port_y, PORT_RADIUS, 0, 2 * M_PI);
    cr->set_source_rgba(r, g, b, 0.95);
    cr->fill();

    cr->set_source_rgba(0.15, 0.15, 0.20, 1.0);
    Pango::FontDescription font;
    font.set_size(10 * Pango::SCALE);
    auto layout = create_pango_layout(node.display_name());
    layout->set_font_description(font);

    int text_w, text_h;
    layout->get_pixel_size(text_w, text_h);

    double text_x = x + (w - text_w) / 2;
    double text_y = y + (h - text_h) / 2;
    cr->move_to(text_x, text_y);
    layout->show_in_cairo_context(cr);
}

void GraphCanvas::draw_connection_line(const Cairo::RefPtr<Cairo::Context>& cr, const Connection& conn) {
    double x1 = conn.source->x + conn.source->width;
    double y1 = conn.source->y + conn.source->height / 2;
    double x2 = conn.destination->x;
    double y2 = conn.destination->y + conn.destination->height / 2;

    double dx = std::abs(x2 - x1) * 0.5;
    double cp1x = x1 + dx;
    double cp1y = y1;
    double cp2x = x2 - dx;
    double cp2y = y2;

    cr->move_to(x1, y1);
    cr->curve_to(cp1x, cp1y, cp2x, cp2y, x2, y2);

    cr->set_source_rgba(0.3, 0.3, 0.35, 0.6);
    cr->set_line_width(2.0);
    cr->stroke();
}

void GraphCanvas::draw_drag_preview(const Cairo::RefPtr<Cairo::Context>& cr) {
    double x1 = m_drag_source->x + m_drag_source->width;
    double y1 = m_drag_source->y + m_drag_source->height / 2;
    double x2 = m_drag_current_x;
    double y2 = m_drag_current_y;

    double dx = std::abs(x2 - x1) * 0.5;
    double cp1x = x1 + dx;
    double cp1y = y1;
    double cp2x = x2 - dx;
    double cp2y = y2;

    cr->move_to(x1, y1);
    cr->curve_to(cp1x, cp1y, cp2x, cp2y, x2, y2);

    cr->set_source_rgba(0.9, 0.3, 0.2, 0.7);
    cr->set_line_width(2.0);
    std::vector<double> dashes = {5, 3};
    cr->set_dash(dashes, 0);
    cr->stroke();
    cr->unset_dash();
}

std::shared_ptr<Node> GraphCanvas::find_node_at(double x, double y) {
    for (auto it = m_nodes.rbegin(); it != m_nodes.rend(); ++it) {
        auto& node = *it;
        if (x >= node->x && x <= node->x + node->width &&
            y >= node->y && y <= node->y + node->height) {
            return node;
        }
    }
    return nullptr;
}

bool GraphCanvas::is_on_output_port(const Node& node, double x, double y) {
    if (node.direction != PortDirection::OUTPUT) return false;
    double px = node.x + node.width;
    double py = node.y + node.height / 2;
    double dx = x - px;
    double dy = y - py;
    return (dx * dx + dy * dy) <= (PORT_RADIUS + 4) * (PORT_RADIUS + 4);
}

bool GraphCanvas::is_on_input_port(const Node& node, double x, double y) {
    if (node.direction != PortDirection::INPUT) return false;
    double px = node.x;
    double py = node.y + node.height / 2;
    double dx = x - px;
    double dy = y - py;
    return (dx * dx + dy * dy) <= (PORT_RADIUS + 4) * (PORT_RADIUS + 4);
}

bool GraphCanvas::on_button_press_event(GdkEventButton* event) {
    if (event->button == 1) {
        double x = event->x / m_zoom;
        double y = event->y / m_zoom;

        for (auto& node : m_nodes) {
            if (is_on_output_port(*node, x, y)) {
                m_drag_source = node;
                m_drag_start_x = x;
                m_drag_start_y = y;
                m_drag_current_x = x;
                m_drag_current_y = y;
                m_is_dragging = true;
                return true;
            }
        }

        m_is_panning = true;
        m_pan_start_x = event->x;
        m_pan_start_y = event->y;
        return true;
    }

    if (event->button == 3) {
        double x = event->x / m_zoom;
        double y = event->y / m_zoom;

        for (auto it = m_connections.begin(); it != m_connections.end(); ++it) {
            auto& conn = *it;
            double x1 = conn->source->x + conn->source->width;
            double y1 = conn->source->y + conn->source->height / 2;
            double x2 = conn->destination->x;
            double y2 = conn->destination->y + conn->destination->height / 2;

            double num = std::abs((y2 - y1) * x - (x2 - x1) * y + x2 * y1 - y2 * x1);
            double den = std::sqrt((y2 - y1) * (y2 - y1) + (x2 - x1) * (x2 - x1));
            double dist = (den > 0) ? num / den : num;

            if (dist < 10 && x >= std::min(x1, x2) - 10 && x <= std::max(x1, x2) + 10) {
                if (m_disconnect_cb) {
                    m_disconnect_cb(conn->source->full_name(), conn->destination->full_name());
                }
                m_connections.erase(it);
                queue_draw();
                return true;
            }
        }
    }

    return Gtk::DrawingArea::on_button_press_event(event);
}

bool GraphCanvas::on_button_release_event(GdkEventButton* event) {
    if (m_is_dragging && m_drag_source && event->button == 1) {
        double x = event->x / m_zoom;
        double y = event->y / m_zoom;

        for (auto& node : m_nodes) {
            if (node == m_drag_source) continue;
            if (is_on_input_port(*node, x, y)) {
                if (m_connect_cb) {
                    m_connect_cb(m_drag_source->full_name(), node->full_name());
                }

                auto conn = std::make_shared<Connection>(m_drag_source, node, m_drag_source->type);
                m_connections.push_back(conn);
                break;
            }
        }

        m_is_dragging = false;
        m_drag_source = nullptr;
        queue_draw();
        return true;
    }

    if (m_is_panning) {
        m_is_panning = false;
        return true;
    }

    return Gtk::DrawingArea::on_button_release_event(event);
}

bool GraphCanvas::on_motion_notify_event(GdkEventMotion* event) {
    if (m_is_dragging) {
        m_drag_current_x = event->x / m_zoom;
        m_drag_current_y = event->y / m_zoom;
        queue_draw();
        return true;
    }

    if (m_is_panning) {
        double dx = event->x - m_pan_start_x;
        double dy = event->y - m_pan_start_y;
        m_offset_x += dx / m_zoom;
        m_offset_y += dy / m_zoom;
        m_pan_start_x = event->x;
        m_pan_start_y = event->y;
        queue_draw();
        return true;
    }

    return Gtk::DrawingArea::on_motion_notify_event(event);
}

bool GraphCanvas::on_scroll_event(GdkEventScroll* event) {
    if (event->direction == GDK_SCROLL_UP) {
        set_zoom(m_zoom * 1.1);
    } else if (event->direction == GDK_SCROLL_DOWN) {
        set_zoom(m_zoom / 1.1);
    }
    return true;
}
