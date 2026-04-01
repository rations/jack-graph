#pragma once

#include <memory>
#include "Node.hpp"

struct Connection {
    std::shared_ptr<Node> source;
    std::shared_ptr<Node> destination;
    PortType type;
    bool is_alsa;
    
    Connection(std::shared_ptr<Node> src, std::shared_ptr<Node> dst, PortType type, bool is_alsa = false);
};
