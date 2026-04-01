#pragma once

#include <string>
#include <vector>
#include <memory>

enum class PortType { AUDIO, MIDI };
enum class PortDirection { INPUT, OUTPUT };

struct Node {
    std::string name;
    std::string client_name;
    PortType type;
    PortDirection direction;
    bool is_alsa;
    double x, y;
    double width, height;
    
    Node(const std::string& name, PortType type, PortDirection direction, bool is_alsa = false);
    std::string full_name() const;
    std::string display_name() const;
};
