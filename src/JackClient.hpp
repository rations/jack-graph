#pragma once

#include <jack/jack.h>
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <atomic>

class JackClient {
public:
    using PortCallback = std::function<void()>;
    using XRunCallback = std::function<void()>;
    using ShutdownCallback = std::function<void()>;

    JackClient();
    ~JackClient();

    bool connect(const std::string& client_name = "jack-graph");
    void disconnect();
    bool is_connected() const { return m_client != nullptr; }
    /* True once this client has been told the server went away. The handle is
     * still allocated -- it must be, so disconnect() can free it -- but calling
     * into JACK with it dereferences shared memory that has been unmapped. */
    bool server_gone() const { return m_server_gone.load(); }
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
    bool set_buffer_size(jack_nframes_t nframes);
    jack_nframes_t get_sample_rate() const;
    uint64_t get_xrun_count() const { return m_xrun_count.load(); }

    std::string get_client_name() const;

    void set_port_callback(PortCallback cb) { m_port_callback = std::move(cb); }
    void set_xrun_callback(XRunCallback cb) { m_xrun_callback = std::move(cb); }
    /* Fires on JACK's own thread when the server goes away -- a stop, a crash, or
     * the restart Settings -> Start performs. The handler
     * must not touch the client or any widget; marshal to the main loop first. */
    void set_shutdown_callback(ShutdownCallback cb) { m_shutdown_callback = std::move(cb); }

    void scan_ports();

private:
    /* A handle that is safe to call into: present, and not yet orphaned. */
    bool usable() const { return m_client != nullptr && !m_server_gone.load(); }

    static void client_registration_callback(const char* name, int reg, void* arg);
    static void port_registration_callback(jack_port_id_t port_id, int reg, void* arg);
    static void port_connect_callback(jack_port_id_t a, jack_port_id_t b, int connect, void* arg);
    static int sample_rate_callback(jack_nframes_t nframes, void* arg);
    static int buffer_size_callback(jack_nframes_t nframes, void* arg);
    static int xrun_callback(void* arg);
    static void shutdown_callback(void* arg);

    jack_client_t* m_client;
    std::vector<PortInfo> m_ports;
    PortCallback m_port_callback;
    XRunCallback m_xrun_callback;
    ShutdownCallback m_shutdown_callback;
    /* Both set from a JACK thread and read from the main loop. */
    std::atomic<bool> m_server_gone;
    std::atomic<uint64_t> m_xrun_count;
    mutable std::mutex m_mutex;
};
