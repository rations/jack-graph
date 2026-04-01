#pragma once

#include <alsa/asoundlib.h>
#include <string>
#include <vector>
#include <functional>
#include <mutex>

class AlsaClient {
public:
    using PortCallback = std::function<void()>;

    AlsaClient();
    ~AlsaClient();

    bool connect(const std::string& client_name = "jack-graph");
    void disconnect();
    bool is_connected() const { return m_seq != nullptr; }

    struct PortInfo {
        std::string name;
        std::string client;
        int client_id;
        int port_id;
        bool is_input;
        bool is_output;
        bool is_midi;
    };

    struct ConnectionInfo {
        int src_client;
        int src_port;
        int dst_client;
        int dst_port;
        std::string src_name;
        std::string dst_name;
    };

    std::vector<PortInfo> get_ports() const;
    std::vector<ConnectionInfo> get_connections() const;

    bool connect_ports(int src_client, int src_port, int dst_client, int dst_port);
    bool disconnect_ports(int src_client, int src_port, int dst_client, int dst_port);

    void set_port_callback(PortCallback cb) { m_port_callback = std::move(cb); }

private:
    snd_seq_t* m_seq;
    int m_client_id;
    PortCallback m_port_callback;
    mutable std::mutex m_mutex;
};
