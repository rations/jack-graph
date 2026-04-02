#pragma once

#include <jack/jack.h>
#include <string>
#include <vector>
#include <functional>
#include <mutex>

class JackClient {
public:
    using PortCallback = std::function<void()>;

    JackClient();
    ~JackClient();

    bool connect(const std::string& client_name = "jack-graph");
    void disconnect();
    bool is_connected() const { return m_client != nullptr; }
    std::string get_actual_client_name() const;

    struct PortInfo {
        std::string name;
        std::string client;
        bool is_input;
        bool is_output;
        bool is_audio;
        bool is_midi;
    };

    struct ConnectionInfo {
        std::string source;
        std::string destination;
    };

    std::vector<PortInfo> get_ports() const;
    std::vector<ConnectionInfo> get_connections() const;

    bool connect_ports(const std::string& source, const std::string& dest);
    bool disconnect_ports(const std::string& source, const std::string& dest);

    jack_nframes_t get_buffer_size() const;
    jack_nframes_t get_sample_rate() const;

    std::string get_client_name() const;

    void set_port_callback(PortCallback cb) { m_port_callback = std::move(cb); }

private:
    void scan_ports();
    static void port_registration_callback(jack_port_id_t port_id, int reg, void* arg);
    static void port_connect_callback(jack_port_id_t a, jack_port_id_t b, int connect, void* arg);
    static int sample_rate_callback(jack_nframes_t nframes, void* arg);
    static int buffer_size_callback(jack_nframes_t nframes, void* arg);

    jack_client_t* m_client;
    std::vector<PortInfo> m_ports;
    PortCallback m_port_callback;
    mutable std::mutex m_mutex;
};
