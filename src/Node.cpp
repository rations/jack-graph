#include "Node.hpp"

Node::Node(const std::string& name, PortType type, PortDirection direction, bool is_alsa)
    : name(name), client_name(), type(type), direction(direction), is_alsa(is_alsa),
      x(0), y(0), width(180), height(24) {
    auto colon = name.find(':');
    if (colon != std::string::npos) {
        std::string client = name.substr(0, colon);
        std::string port_part = name.substr(colon + 1);
        if (client == "system") {
            auto underscore = port_part.find('_');
            if (underscore != std::string::npos) {
                client_name = "system:" + port_part.substr(0, underscore);
            } else {
                client_name = "system:" + port_part;
            }
        } else {
            client_name = client;
        }
    } else {
        client_name = name;
    }
}

std::string Node::full_name() const {
    return name;
}

std::string Node::display_name() const {
    auto colon = name.find(':');
    if (colon != std::string::npos) {
        return name.substr(colon + 1);
    }
    return name;
}
