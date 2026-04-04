#include "GraphCanvas.hpp"
#include <algorithm>
#include <map>
#include <vector>

GraphCanvas::GraphCanvas()
    : m_zoom(1.0), m_offset_x(20), m_offset_y(20),
      m_is_dragging(false), m_is_panning(false), m_is_moving_box(false),
      m_drag_source(nullptr), m_moving_box(nullptr),
      m_drag_start_x(0), m_drag_start_y(0),
      m_drag_current_x(0), m_drag_current_y(0),
      m_pan_start_x(0), m_pan_start_y(0),
      m_box_move_offset_x(0), m_box_move_offset_y(0) {
    add_events(Gdk::BUTTON_PRESS_MASK | Gdk::BUTTON_RELEASE_MASK |
               Gdk::POINTER_MOTION_MASK | Gdk::SCROLL_MASK);
}

GraphCanvas::~GraphCanvas() = default;

void GraphCanvas::clear() {
    m_nodes.clear();
    m_connections.clear();
    m_client_boxes.clear();
    queue_draw();
}

void GraphCanvas::add_node(std::shared_ptr<Node> node) {
    m_nodes.push_back(std::move(node));
}

void GraphCanvas::add_connection(std::shared_ptr<Connection> conn) {
    m_connections.push_back(std::move(conn));
}

void GraphCanvas::remove_all() {
    for (const auto& box : m_client_boxes) {
        m_saved_positions[box.client_name] = {box.x, box.y};
    }
    m_nodes.clear();
    m_connections.clear();
    m_client_boxes.clear();
    queue_draw();
}

void GraphCanvas::set_zoom(double zoom) {
    m_zoom = std::max(0.25, std::min(zoom, 4.0));
    queue_draw();
}

void GraphCanvas::build_client_boxes() {
    m_client_boxes.clear();

    std::map<std::string, std::unique_ptr<ClientBox>> boxes;

    for (auto& node : m_nodes) {
        std::string cname = node->client_name;
        if (cname.empty()) continue;

        if (boxes.find(cname) == boxes.end()) {
            boxes[cname] = std::make_unique<ClientBox>(cname, node->is_alsa);
        }
        boxes[cname]->add_port(node);
    }

    for (auto& [name, box] : boxes) {
        m_client_boxes.push_back(std::move(*box));
    }
}

void GraphCanvas::layout(bool preserve_positions) {
    if (preserve_positions) {
        for (const auto& box : m_client_boxes) {
            m_saved_positions[box.client_name] = {box.x, box.y};
        }
    }

    build_client_boxes();

    double x = m_offset_x;
    double y = m_offset_y;
    double max_y = y;

    for (auto& box : m_client_boxes) {
        auto it = m_saved_positions.find(box.client_name);
        if (it != m_saved_positions.end()) {
            box.x = it->second.x;
            box.y = it->second.y;
        } else {
            box.x = x;
            box.y = y;
        }

        for (size_t i = 0; i < box.inputs.size(); ++i) {
            box.inputs[i]->x = box.x + ClientBox::SIDE_PAD;
            box.inputs[i]->y = box.y + ClientBox::HEADER_HEIGHT + ClientBox::PORT_PAD +
                                i * (ClientBox::PORT_HEIGHT + ClientBox::PORT_PAD);
            box.inputs[i]->width = ClientBox::COL_WIDTH;
            box.inputs[i]->height = ClientBox::PORT_HEIGHT;
        }

        for (size_t i = 0; i < box.outputs.size(); ++i) {
            box.outputs[i]->x = box.x + ClientBox::SIDE_PAD + ClientBox::COL_WIDTH + ClientBox::SIDE_PAD;
            box.outputs[i]->y = box.y + ClientBox::HEADER_HEIGHT + ClientBox::PORT_PAD +
                               i * (ClientBox::PORT_HEIGHT + ClientBox::PORT_PAD);
            box.outputs[i]->width = ClientBox::COL_WIDTH;
            box.outputs[i]->height = ClientBox::PORT_HEIGHT;
        }

        y += box.height + ROW_GAP;
        max_y = std::max(max_y, y);
    }

    double max_box_width = 0;
    for (const auto& box : m_client_boxes) {
        max_box_width = std::max(max_box_width, box.width);
    }

    double canvas_width = x + max_box_width + m_offset_x * 2 + BOX_GAP;
    double canvas_height = max_y + m_offset_y * 2;

    auto parent = get_parent();
    if (parent) {
        parent->set_size_request(
            static_cast<int>(canvas_width),
            static_cast<int>(canvas_height));
    }

    queue_draw();
}

bool GraphCanvas::on_draw(const Cairo::RefPtr<Cairo::Context>& cr) {
    cr->set_source_rgb(0.15, 0.15, 0.18);
    cr->paint();

    cr->save();
    cr->scale(m_zoom, m_zoom);

    for (auto& conn : m_connections) {
        draw_connection_line(cr, *conn);
    }

    for (const auto& box : m_client_boxes) {
        draw_client_box(cr, box);
    }

    if (m_is_dragging && m_drag_source) {
        draw_drag_preview(cr);
    }

    cr->restore();
    return true;
}

void GraphCanvas::draw_client_box(const Cairo::RefPtr<Cairo::Context>& cr, const ClientBox& box) {
    double x = box.x;
    double y = box.y;
    double w = box.width;
    double h = box.height;
    double radius = 6;

    cr->set_source_rgba(0.22, 0.22, 0.26, 0.95);
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

    cr->set_source_rgba(0.35, 0.35, 0.40, 0.8);
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

    cr->move_to(x, y + ClientBox::HEADER_HEIGHT);
    cr->line_to(x + w, y + ClientBox::HEADER_HEIGHT);
    cr->set_source_rgba(0.35, 0.35, 0.40, 0.5);
    cr->set_line_width(1.0);
    cr->stroke();

    cr->set_source_rgba(0.90, 0.90, 0.92, 1.0);
    Pango::FontDescription header_font;
    header_font.set_size(11 * Pango::SCALE);
    header_font.set_weight(Pango::WEIGHT_BOLD);
    auto header_layout = create_pango_layout(box.client_name);
    header_layout->set_font_description(header_font);

    int hw, hh;
    header_layout->get_pixel_size(hw, hh);
    cr->move_to(x + (w - hw) / 2, y + (ClientBox::HEADER_HEIGHT - hh) / 2);
    header_layout->show_in_cairo_context(cr);

    double in_x = x + ClientBox::SIDE_PAD;
    double out_x = x + ClientBox::SIDE_PAD + ClientBox::COL_WIDTH + ClientBox::SIDE_PAD;
    double port_y = y + ClientBox::HEADER_HEIGHT + ClientBox::PORT_PAD;

    for (const auto& node : box.inputs) {
        draw_port(cr, *node, in_x, port_y, false);
        port_y += ClientBox::PORT_HEIGHT + ClientBox::PORT_PAD;
    }

    port_y = y + ClientBox::HEADER_HEIGHT + ClientBox::PORT_PAD;
    for (const auto& node : box.outputs) {
        draw_port(cr, *node, out_x, port_y, true);
        port_y += ClientBox::PORT_HEIGHT + ClientBox::PORT_PAD;
    }
}

void GraphCanvas::draw_port(const Cairo::RefPtr<Cairo::Context>& cr, const Node& node, double x, double y, bool is_output) {
    double w = ClientBox::PORT_BG_WIDTH;
    double h = ClientBox::PORT_HEIGHT;
    double radius = 3;

    double r, g, b;
    if (node.type == PortType::AUDIO) {
        r = 0.25; g = 0.45; b = 0.85;
    } else {
        r = 0.20; g = 0.70; b = 0.35;
    }

    double bg_x = is_output ? x - (ClientBox::PORT_BG_WIDTH - ClientBox::COL_WIDTH) : x;
    cr->set_source_rgba(r, g, b, 0.12);
    cr->move_to(bg_x + radius, y);
    cr->line_to(bg_x + w - radius, y);
    cr->arc(bg_x + w - radius, y + radius, radius, -M_PI / 2, 0);
    cr->line_to(bg_x + w, y + h - radius);
    cr->arc(bg_x + w - radius, y + h - radius, radius, 0, M_PI / 2);
    cr->line_to(bg_x + radius, y + h);
    cr->arc(bg_x + radius, y + h - radius, radius, M_PI / 2, M_PI);
    cr->line_to(bg_x, y + radius);
    cr->arc(bg_x + radius, y + radius, radius, M_PI, 3 * M_PI / 2);
    cr->close_path();
    cr->fill();

    double port_dot_x = is_output ? bg_x + w : bg_x;
    double port_dot_y = y + h / 2;
    cr->arc(port_dot_x, port_dot_y, PORT_RADIUS, 0, 2 * M_PI);
    cr->set_source_rgba(r, g, b, 0.95);
    cr->fill();

    cr->set_source_rgba(0.80, 0.80, 0.85, 1.0);
    Pango::FontDescription font;
    font.set_size(9 * Pango::SCALE);
    auto layout = create_pango_layout(node.display_name());
    layout->set_font_description(font);

    int tw, th;
    layout->get_pixel_size(tw, th);

    double max_text_width = w - PORT_RADIUS * 2 - 12;
    if (tw > max_text_width) {
        std::string truncated = node.display_name();
        while (tw > max_text_width && !truncated.empty()) {
            truncated = truncated.substr(0, truncated.size() - 1);
            layout->set_text(truncated + "...");
            layout->get_pixel_size(tw, th);
        }
    }

    double text_x;
    if (is_output) {
        text_x = bg_x + w - PORT_RADIUS - 4 - tw;
    } else {
        text_x = bg_x + PORT_RADIUS + 4;
    }
    double text_y = y + (h - th) / 2;
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

    cr->set_source_rgba(0.55, 0.55, 0.60, 0.5);
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

    cr->set_source_rgba(0.9, 0.5, 0.2, 0.7);
    cr->set_line_width(2.0);
    std::vector<double> dashes = {5, 3};
    cr->set_dash(dashes, 0);
    cr->stroke();
    cr->unset_dash();
}

std::shared_ptr<Node> GraphCanvas::find_output_port_at(double x, double y) {
    for (auto& box : m_client_boxes) {
        if (x >= box.x && x <= box.x + box.width &&
            y >= box.y && y <= box.y + box.height) {
            double local_x = x - box.x;
            double local_y = y - box.y;
            return box.find_output_at(local_x, local_y);
        }
    }
    return nullptr;
}

std::shared_ptr<Node> GraphCanvas::find_input_port_at(double x, double y) {
    for (auto& box : m_client_boxes) {
        if (x >= box.x && x <= box.x + box.width &&
            y >= box.y && y <= box.y + box.height) {
            double local_x = x - box.x;
            double local_y = y - box.y;
            return box.find_input_at(local_x, local_y);
        }
    }
    return nullptr;
}

ClientBox* GraphCanvas::find_box_at(double x, double y) {
    for (auto& box : m_client_boxes) {
        if (box.contains(x, y)) {
            return &box;
        }
    }
    return nullptr;
}

bool GraphCanvas::on_button_press_event(GdkEventButton* event) {
    if (event->button == 1) {
        double x = event->x / m_zoom;
        double y = event->y / m_zoom;

        auto output = find_output_port_at(x, y);
        if (output) {
            m_drag_source = output;
            m_drag_start_x = x;
            m_drag_start_y = y;
            m_drag_current_x = x;
            m_drag_current_y = y;
            m_is_dragging = true;
            return true;
        }

        auto* box = find_box_at(x, y);
        if (box) {
            m_is_moving_box = true;
            m_moving_box = box;
            m_box_move_offset_x = x - box->x;
            m_box_move_offset_y = y - box->y;
            return true;
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

        auto target = find_input_port_at(x, y);
        if (target && target != m_drag_source) {
            if (m_connect_cb) {
                m_connect_cb(m_drag_source->full_name(), target->full_name());
            }

            auto conn = std::make_shared<Connection>(m_drag_source, target, m_drag_source->type);
            m_connections.push_back(conn);
        }

        m_is_dragging = false;
        m_drag_source = nullptr;
        queue_draw();
        return true;
    }

    if (m_is_moving_box) {
        m_is_moving_box = false;
        m_moving_box = nullptr;
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

    if (m_is_moving_box && m_moving_box) {
        double x = event->x / m_zoom;
        double y = event->y / m_zoom;
        m_moving_box->x = x - m_box_move_offset_x;
        m_moving_box->y = y - m_box_move_offset_y;

        for (size_t i = 0; i < m_moving_box->inputs.size(); ++i) {
            m_moving_box->inputs[i]->x = m_moving_box->x + ClientBox::SIDE_PAD;
            m_moving_box->inputs[i]->y = m_moving_box->y + ClientBox::HEADER_HEIGHT +
                                          ClientBox::PORT_PAD + i * (ClientBox::PORT_HEIGHT + ClientBox::PORT_PAD);
        }
        for (size_t i = 0; i < m_moving_box->outputs.size(); ++i) {
            m_moving_box->outputs[i]->x = m_moving_box->x + ClientBox::SIDE_PAD +
                                         ClientBox::COL_WIDTH + ClientBox::SIDE_PAD;
            m_moving_box->outputs[i]->y = m_moving_box->y + ClientBox::HEADER_HEIGHT +
                                         ClientBox::PORT_PAD + i * (ClientBox::PORT_HEIGHT + ClientBox::PORT_PAD);
        }
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
