#include "JackClient.hpp"
#include <cstring>
#include <algorithm>

JackClient::JackClient() : m_client(nullptr) {
}

JackClient::~JackClient() {
    disconnect();
}

bool JackClient::connect(const std::string& client_name) {
    if (m_client) {
        disconnect();
    }

    jack_status_t status;
    m_client = jack_client_open(client_name.c_str(), JackNullOption, &status);
    if (!m_client) {
        return false;
    }

    jack_set_port_registration_callback(m_client, port_registration_callback, this);
    jack_set_port_connect_callback(m_client, port_connect_callback, this);
    jack_set_sample_rate_callback(m_client, sample_rate_callback, this);
    jack_set_buffer_size_callback(m_client, buffer_size_callback, this);

    if (jack_activate(m_client)) {
        jack_client_close(m_client);
        m_client = nullptr;
        return false;
    }

    scan_ports();
    return true;
}

void JackClient::disconnect() {
    if (m_client) {
        jack_deactivate(m_client);
        jack_client_close(m_client);
        m_client = nullptr;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    m_ports.clear();
}

std::vector<JackClient::PortInfo> JackClient::get_ports() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_ports;
}

std::vector<JackClient::ConnectionInfo> JackClient::get_connections() const {
    std::vector<ConnectionInfo> result;
    if (!m_client) return result;

    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& port : m_ports) {
        if (!port.is_output) continue;

        const char** connections = jack_port_get_all_connections(m_client,
            jack_port_by_name(m_client, port.name.c_str()));
        if (connections) {
            for (int i = 0; connections[i]; ++i) {
                ConnectionInfo conn;
                conn.source = port.name;
                conn.destination = connections[i];
                result.push_back(std::move(conn));
            }
            jack_free(connections);
        }
    }

    return result;
}

bool JackClient::connect_ports(const std::string& source, const std::string& dest) {
    if (!m_client) return false;
    int result = jack_connect(m_client, source.c_str(), dest.c_str());
    return (result == 0 || result == EEXIST);
}

bool JackClient::disconnect_ports(const std::string& source, const std::string& dest) {
    if (!m_client) return false;
    return jack_disconnect(m_client, source.c_str(), dest.c_str()) == 0;
}

jack_nframes_t JackClient::get_buffer_size() const {
    if (!m_client) return 0;
    return jack_get_buffer_size(m_client);
}

jack_nframes_t JackClient::get_sample_rate() const {
    if (!m_client) return 0;
    return jack_get_sample_rate(m_client);
}

std::string JackClient::get_actual_client_name() const {
    if (!m_client) return {};
    return jack_get_client_name(m_client);
}

void JackClient::scan_ports() {
    if (!m_client) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    m_ports.clear();

    const char** ports = jack_get_ports(m_client, nullptr, nullptr, 0);
    if (!ports) return;

    for (int i = 0; ports[i]; ++i) {
        jack_port_t* port = jack_port_by_name(m_client, ports[i]);
        if (!port) continue;

        PortInfo info;
        info.name = ports[i];

        auto colon = info.name.find(':');
        if (colon != std::string::npos) {
            info.client = info.name.substr(0, colon);
        } else {
            info.client = info.name;
        }

        int flags = jack_port_flags(port);
        info.is_input = (flags & JackPortIsInput);
        info.is_output = (flags & JackPortIsOutput);

        const char* type = jack_port_type(port);
        info.is_audio = (std::strcmp(type, JACK_DEFAULT_AUDIO_TYPE) == 0);
        info.is_midi = (std::strcmp(type, JACK_DEFAULT_MIDI_TYPE) == 0 ||
                       std::strcmp(type, "MIDI") == 0 ||
                       std::strcmp(type, "8 bit raw midi") == 0);

        if (info.is_audio || info.is_midi) {
            m_ports.push_back(std::move(info));
        }
    }

    jack_free(ports);
}

void JackClient::port_registration_callback(jack_port_id_t port_id, int reg, void* arg) {
    (void)port_id;
    (void)reg;
    auto* self = static_cast<JackClient*>(arg);
    if (self && self->m_client) {
        self->scan_ports();
        if (self->m_port_callback) {
            self->m_port_callback();
        }
    }
}

void JackClient::port_connect_callback(jack_port_id_t a, jack_port_id_t b, int connect, void* arg) {
    (void)a;
    (void)b;
    (void)connect;
    auto* self = static_cast<JackClient*>(arg);
    if (self && self->m_port_callback) {
        self->m_port_callback();
    }
}

int JackClient::sample_rate_callback(jack_nframes_t nframes, void* arg) {
    (void)nframes;
    auto* self = static_cast<JackClient*>(arg);
    if (self && self->m_port_callback) {
        self->m_port_callback();
    }
    return 0;
}

int JackClient::buffer_size_callback(jack_nframes_t nframes, void* arg) {
    (void)nframes;
    auto* self = static_cast<JackClient*>(arg);
    if (self && self->m_port_callback) {
        self->m_port_callback();
    }
    return 0;
}
