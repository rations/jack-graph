#include "ClientBox.hpp"
#include <algorithm>

ClientBox::ClientBox(const std::string& name, bool alsa)
    : client_name(name), x(0), y(0), width(MIN_WIDTH), height(HEADER_HEIGHT), is_alsa(alsa) {
}

void ClientBox::add_port(std::shared_ptr<Node> node) {
    if (node->direction == PortDirection::OUTPUT) {
        outputs.push_back(std::move(node));
    } else {
        inputs.push_back(std::move(node));
    }
    calculate_dimensions();
}

void ClientBox::calculate_dimensions() {
    size_t max_ports = std::max(outputs.size(), inputs.size());
    double ports_height = max_ports * (PORT_HEIGHT + PORT_PAD);
    if (max_ports > 0) {
        ports_height -= PORT_PAD;
    }
    height = HEADER_HEIGHT + ports_height + PORT_PAD * 2;

    double needed_width = SIDE_PAD + COL_WIDTH + SIDE_PAD + COL_WIDTH + SIDE_PAD;
    width = std::max(MIN_WIDTH, needed_width);
}

std::shared_ptr<Node> ClientBox::find_output_at(double local_x, double local_y) const {
    double out_x = SIDE_PAD + COL_WIDTH + SIDE_PAD;
    double port_y = HEADER_HEIGHT + PORT_PAD;

    for (size_t i = 0; i < outputs.size(); ++i) {
        double px = out_x + COL_WIDTH;
        double py = port_y + PORT_HEIGHT / 2;
        double dx = local_x - px;
        double dy = local_y - py;
        if (dx * dx + dy * dy <= 100) {
            return outputs[i];
        }
        port_y += PORT_HEIGHT + PORT_PAD;
    }
    return nullptr;
}

std::shared_ptr<Node> ClientBox::find_input_at(double local_x, double local_y) const {
    double in_x = SIDE_PAD;
    double port_y = HEADER_HEIGHT + PORT_PAD;

    for (size_t i = 0; i < inputs.size(); ++i) {
        double px = in_x;
        double py = port_y + PORT_HEIGHT / 2;
        double dx = local_x - px;
        double dy = local_y - py;
        if (dx * dx + dy * dy <= 100) {
            return inputs[i];
        }
        port_y += PORT_HEIGHT + PORT_PAD;
    }
    return nullptr;
}

bool ClientBox::contains(double px, double py) const {
    return px >= x && px <= x + width && py >= y && py <= y + height;
}
