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

    /* ALSA sequencer ports are addressed by number, never by name: ALSA names
     * are not unique and may contain ':'. -1 on a JACK port. */
    int alsa_client = -1;
    int alsa_port = -1;
    /* What the port is called on screen, when that is not simply the part of
     * `name` after the first ':' -- which is the case for ALSA ports. */
    std::string label;

    Node(const std::string& name, PortType type, PortDirection direction, bool is_alsa = false);
    std::string full_name() const;
    std::string display_name() const;
};
