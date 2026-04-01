#include "AlsaClient.hpp"
#include <cstring>
#include <sstream>

AlsaClient::AlsaClient() : m_seq(nullptr), m_client_id(-1) {
}

AlsaClient::~AlsaClient() {
    disconnect();
}

bool AlsaClient::connect(const std::string& client_name) {
    if (m_seq) {
        disconnect();
    }

    int err = snd_seq_open(&m_seq, "default", SND_SEQ_OPEN_DUPLEX, 0);
    if (err < 0) {
        m_seq = nullptr;
        return false;
    }

    m_client_id = snd_seq_create_simple_port(m_seq, client_name.c_str(),
        SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ |
        SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    if (m_client_id < 0) {
        snd_seq_close(m_seq);
        m_seq = nullptr;
        return false;
    }

    return true;
}

void AlsaClient::disconnect() {
    if (m_seq) {
        snd_seq_close(m_seq);
        m_seq = nullptr;
        m_client_id = -1;
    }
}

std::vector<AlsaClient::PortInfo> AlsaClient::get_ports() const {
    std::vector<PortInfo> result;
    if (!m_seq) return result;

    std::lock_guard<std::mutex> lock(m_mutex);

    snd_seq_client_info_t* cinfo;
    snd_seq_port_info_t* pinfo;

    snd_seq_client_info_alloca(&cinfo);
    snd_seq_port_info_alloca(&pinfo);

    snd_seq_client_info_set_client(cinfo, -1);

    while (snd_seq_query_next_client(m_seq, cinfo) >= 0) {
        int client = snd_seq_client_info_get_client(cinfo);

        if (client == m_client_id) continue;

        snd_seq_port_info_set_client(pinfo, client);
        snd_seq_port_info_set_port(pinfo, -1);

        while (snd_seq_query_next_port(m_seq, pinfo) >= 0) {
            unsigned int caps = snd_seq_port_info_get_capability(pinfo);

            if (!(caps & SND_SEQ_PORT_CAP_READ) && !(caps & SND_SEQ_PORT_CAP_WRITE))
                continue;

            if (caps & SND_SEQ_PORT_CAP_NO_EXPORT)
                continue;

            unsigned int type = snd_seq_port_info_get_type(pinfo);
            if (!(type & SND_SEQ_PORT_TYPE_MIDI_GENERIC))
                continue;

            PortInfo info;
            info.client_id = client;
            info.port_id = snd_seq_port_info_get_port(pinfo);
            info.client = snd_seq_client_info_get_name(cinfo);
            info.name = snd_seq_port_info_get_name(pinfo);
            info.is_midi = true;

            bool can_read = (caps & SND_SEQ_PORT_CAP_READ);
            bool can_write = (caps & SND_SEQ_PORT_CAP_WRITE);

            info.is_input = can_write && !can_read;
            info.is_output = can_read && !can_write;

            if (!info.is_input && !info.is_output) {
                info.is_output = true;
            }

            result.push_back(std::move(info));
        }
    }

    return result;
}

std::vector<AlsaClient::ConnectionInfo> AlsaClient::get_connections() const {
    std::vector<ConnectionInfo> result;
    if (!m_seq) return result;

    std::lock_guard<std::mutex> lock(m_mutex);

    snd_seq_query_subscribe_t* subs;
    snd_seq_query_subscribe_alloca(&subs);

    snd_seq_client_info_t* cinfo;
    snd_seq_port_info_t* pinfo;
    snd_seq_client_info_alloca(&cinfo);
    snd_seq_port_info_alloca(&pinfo);

    snd_seq_client_info_set_client(cinfo, -1);

    while (snd_seq_query_next_client(m_seq, cinfo) >= 0) {
        int client = snd_seq_client_info_get_client(cinfo);
        if (client == m_client_id) continue;

        snd_seq_port_info_set_client(pinfo, client);
        snd_seq_port_info_set_port(pinfo, -1);

        while (snd_seq_query_next_port(m_seq, pinfo) >= 0) {
            int port = snd_seq_port_info_get_port(pinfo);

            snd_seq_addr_t addr;
            addr.client = static_cast<unsigned char>(client);
            addr.port = static_cast<unsigned char>(port);
            snd_seq_query_subscribe_set_root(subs, &addr);
            snd_seq_query_subscribe_set_type(subs,
                SND_SEQ_QUERY_SUBS_READ);
            snd_seq_query_subscribe_set_index(subs, 0);

            while (snd_seq_query_port_subscribers(m_seq, subs) >= 0) {
                const snd_seq_addr_t* dest =
                    snd_seq_query_subscribe_get_addr(subs);

                ConnectionInfo conn;
                conn.src_client = client;
                conn.src_port = port;
                conn.dst_client = dest->client;
                conn.dst_port = dest->port;

                snd_seq_port_info_t* src_pinfo;
                snd_seq_port_info_alloca(&src_pinfo);
                if (snd_seq_get_any_port_info(m_seq, client, port, src_pinfo) >= 0) {
                    conn.src_name = snd_seq_port_info_get_name(src_pinfo);
                }

                snd_seq_port_info_t* dst_pinfo;
                snd_seq_port_info_alloca(&dst_pinfo);
                if (snd_seq_get_any_port_info(m_seq, dest->client, dest->port, dst_pinfo) >= 0) {
                    conn.dst_name = snd_seq_port_info_get_name(dst_pinfo);
                }

                result.push_back(std::move(conn));

                snd_seq_query_subscribe_set_index(subs,
                    snd_seq_query_subscribe_get_index(subs) + 1);
            }
        }
    }

    return result;
}

bool AlsaClient::connect_ports(int src_client, int src_port, int dst_client, int dst_port) {
    if (!m_seq) return false;
    (void)src_client;
    return snd_seq_connect_to(m_seq, src_port, dst_client, dst_port) >= 0;
}

bool AlsaClient::disconnect_ports(int src_client, int src_port, int dst_client, int dst_port) {
    if (!m_seq) return false;
    (void)src_client;
    return snd_seq_disconnect_to(m_seq, src_port, dst_client, dst_port) >= 0;
}
