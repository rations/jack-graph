#pragma once

#include <alsa/asoundlib.h>
#include <string>
#include <vector>

/* The ALSA sequencer, for the MIDI ports the graph shows while JACK is not
 * running. Everything here runs on the main loop: the sequencer is opened
 * non-blocking and its announce events arrive through poll_fd(), which main.cpp
 * adds to the same select() as the X connection. */
class AlsaClient {
public:
    AlsaClient();
    ~AlsaClient();

    bool connect(const std::string& client_name = "jack-graph");
    void disconnect();
    bool is_connected() const { return m_seq != nullptr; }

    /* The descriptor that becomes readable when the sequencer has announce
     * events for us, or -1. */
    int poll_fd() const;

    /* Read every pending event. True if any of them changed what the graph
     * shows -- a client or port came or went, or a subscription changed. */
    bool drain_events();

    struct PortInfo {
        std::string name;
        std::string client;
        int client_id;
        int port_id;
        /* A duplex port is both, and the graph shows it on both sides. */
        bool is_input;
        bool is_output;
    };

    struct ConnectionInfo {
        int src_client;
        int src_port;
        int dst_client;
        int dst_port;
    };

    std::vector<PortInfo> get_ports() const;
    std::vector<ConnectionInfo> get_connections() const;

    bool connect_ports(int src_client, int src_port, int dst_client, int dst_port);
    bool disconnect_ports(int src_client, int src_port, int dst_client, int dst_port);

private:
    snd_seq_t* m_seq;
    /* Our own client and the one port we create to receive announce events.
     * These are two different numbers: the client id comes from
     * snd_seq_client_id(), the port from snd_seq_create_simple_port(). */
    int m_client_id;
    int m_port_id;
};
