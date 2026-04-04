#pragma once

#include <string>
#include <vector>
#include <memory>
#include "Node.hpp"

struct ClientBox {
    std::string client_name;
    double x, y;
    double width, height;
    std::vector<std::shared_ptr<Node>> outputs;
    std::vector<std::shared_ptr<Node>> inputs;
    bool is_alsa;

    ClientBox(const std::string& name, bool alsa = false);

    void add_port(std::shared_ptr<Node> node);
    void calculate_dimensions();
    std::shared_ptr<Node> find_output_at(double local_x, double local_y) const;
    std::shared_ptr<Node> find_input_at(double local_x, double local_y) const;
    bool contains(double px, double py) const;

    static constexpr double HEADER_HEIGHT = 28;
    static constexpr double PORT_HEIGHT = 24;
    static constexpr double PORT_PAD = 4;
    static constexpr double SIDE_PAD = 12;
    static constexpr double COL_WIDTH = 140;
    static constexpr double PORT_BG_WIDTH = 180;
    static constexpr double MIN_WIDTH = 180;
};
