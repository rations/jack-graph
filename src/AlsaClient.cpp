#include "AlsaClient.hpp"
#include <cerrno>
#include <poll.h>

AlsaClient::AlsaClient() : m_seq(nullptr), m_client_id(-1), m_port_id(-1) {
}

AlsaClient::~AlsaClient() {
    disconnect();
}

bool AlsaClient::connect(const std::string& client_name) {
    if (m_seq) {
        disconnect();
    }

    /* Non-blocking, so drain_events() can read until the queue is empty and
     * stop there instead of parking the main loop inside the sequencer. */
    if (snd_seq_open(&m_seq, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK) < 0) {
        m_seq = nullptr;
        return false;
    }
    snd_seq_set_client_name(m_seq, client_name.c_str());
    m_client_id = snd_seq_client_id(m_seq);

    /* One port, write-only, and only so the System:Announce port has somewhere
     * to deliver to. SND_SEQ_PORT_CAP_NO_EXPORT marks it internal: without it
     * JACK's ALSA MIDI bridge picks it up and creates extra
     * system:midi_capture_N / system:midi_playback_N ports that appear as
     * phantom entries in the graph. */
    m_port_id = snd_seq_create_simple_port(m_seq, client_name.c_str(),
        SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_NO_EXPORT,
        SND_SEQ_PORT_TYPE_APPLICATION);
    if (m_port_id < 0) {
        snd_seq_close(m_seq);
        m_seq = nullptr;
        m_client_id = -1;
        return false;
    }

    /* Clients and ports appearing and going, and subscriptions changing, are
     * all announced here. Failing to subscribe costs the live updates only:
     * Refresh still reads the current state. */
    if (snd_seq_connect_from(m_seq, m_port_id, SND_SEQ_CLIENT_SYSTEM,
                             SND_SEQ_PORT_SYSTEM_ANNOUNCE) < 0) {
        fprintf(stderr, "jack-graph: cannot subscribe to ALSA sequencer announcements; "
                        "the MIDI view will only update on Refresh\n");
    }

    return true;
}

void AlsaClient::disconnect() {
    if (m_seq) {
        snd_seq_close(m_seq);
        m_seq = nullptr;
        m_client_id = -1;
        m_port_id = -1;
    }
}

int AlsaClient::poll_fd() const {
    if (!m_seq) return -1;
    if (snd_seq_poll_descriptors_count(m_seq, POLLIN) < 1) return -1;
    struct pollfd pfd;
    if (snd_seq_poll_descriptors(m_seq, &pfd, 1, POLLIN) != 1) return -1;
    return pfd.fd;
}

bool AlsaClient::drain_events() {
    if (!m_seq) return false;

    bool changed = false;
    for (;;) {
        snd_seq_event_t* ev = nullptr;
        const int r = snd_seq_event_input(m_seq, &ev);
        if (r == -ENOSPC) {
            /* The input queue overran and events were lost. Whatever they said,
             * a full refresh reads the truth. */
            changed = true;
            continue;
        }
        if (r < 0 || !ev) break; /* -EAGAIN: the queue is empty */

        switch (ev->type) {
        case SND_SEQ_EVENT_CLIENT_START:
        case SND_SEQ_EVENT_CLIENT_EXIT:
        case SND_SEQ_EVENT_CLIENT_CHANGE:
        case SND_SEQ_EVENT_PORT_START:
        case SND_SEQ_EVENT_PORT_EXIT:
        case SND_SEQ_EVENT_PORT_CHANGE:
        case SND_SEQ_EVENT_PORT_SUBSCRIBED:
        case SND_SEQ_EVENT_PORT_UNSUBSCRIBED:
            changed = true;
            break;
        default:
            break;
        }
    }
    return changed;
}

std::vector<AlsaClient::PortInfo> AlsaClient::get_ports() const {
    std::vector<PortInfo> result;
    if (!m_seq) return result;

    snd_seq_client_info_t* cinfo;
    snd_seq_port_info_t* pinfo;
    snd_seq_client_info_alloca(&cinfo);
    snd_seq_port_info_alloca(&pinfo);

    snd_seq_client_info_set_client(cinfo, -1);
    while (snd_seq_query_next_client(m_seq, cinfo) >= 0) {
        const int client = snd_seq_client_info_get_client(cinfo);

        /* Ourselves, and System -- whose Timer and Announce ports are the
         * sequencer's own plumbing, not something to route MIDI through. */
        if (client == m_client_id || client == SND_SEQ_CLIENT_SYSTEM) continue;

        snd_seq_port_info_set_client(pinfo, client);
        snd_seq_port_info_set_port(pinfo, -1);
        while (snd_seq_query_next_port(m_seq, pinfo) >= 0) {
            const unsigned int caps = snd_seq_port_info_get_capability(pinfo);
            if (caps & SND_SEQ_PORT_CAP_NO_EXPORT) continue;

            /* What aconnect lists: a port others may subscribe FROM is an
             * output, one they may subscribe TO is an input. Filtering on
             * SND_SEQ_PORT_TYPE_MIDI_GENERIC instead, as this used to, hides
             * every application port that only declares TYPE_APPLICATION. */
            const bool out = (caps & SND_SEQ_PORT_CAP_READ) && (caps & SND_SEQ_PORT_CAP_SUBS_READ);
            const bool in = (caps & SND_SEQ_PORT_CAP_WRITE) && (caps & SND_SEQ_PORT_CAP_SUBS_WRITE);
            if (!out && !in) continue;

            PortInfo info;
            info.client_id = client;
            info.port_id = snd_seq_port_info_get_port(pinfo);
            info.client = snd_seq_client_info_get_name(cinfo);
            info.name = snd_seq_port_info_get_name(pinfo);
            info.is_input = in;
            info.is_output = out;
            result.push_back(std::move(info));
        }
    }

    return result;
}

std::vector<AlsaClient::ConnectionInfo> AlsaClient::get_connections() const {
    std::vector<ConnectionInfo> result;
    if (!m_seq) return result;

    snd_seq_query_subscribe_t* subs;
    snd_seq_client_info_t* cinfo;
    snd_seq_port_info_t* pinfo;
    snd_seq_query_subscribe_alloca(&subs);
    snd_seq_client_info_alloca(&cinfo);
    snd_seq_port_info_alloca(&pinfo);

    snd_seq_client_info_set_client(cinfo, -1);
    while (snd_seq_query_next_client(m_seq, cinfo) >= 0) {
        const int client = snd_seq_client_info_get_client(cinfo);
        if (client == m_client_id || client == SND_SEQ_CLIENT_SYSTEM) continue;

        snd_seq_port_info_set_client(pinfo, client);
        snd_seq_port_info_set_port(pinfo, -1);
        while (snd_seq_query_next_port(m_seq, pinfo) >= 0) {
            const int port = snd_seq_port_info_get_port(pinfo);

            snd_seq_addr_t root;
            root.client = static_cast<unsigned char>(client);
            root.port = static_cast<unsigned char>(port);
            snd_seq_query_subscribe_set_root(subs, &root);
            /* READ: the ports this one sends to. Every subscription has exactly
             * one sender, so walking senders lists each cable once. */
            snd_seq_query_subscribe_set_type(subs, SND_SEQ_QUERY_SUBS_READ);
            snd_seq_query_subscribe_set_index(subs, 0);

            while (snd_seq_query_port_subscribers(m_seq, subs) >= 0) {
                const snd_seq_addr_t* dest = snd_seq_query_subscribe_get_addr(subs);
                result.push_back({client, port, dest->client, dest->port});
                snd_seq_query_subscribe_set_index(subs, snd_seq_query_subscribe_get_index(subs) + 1);
            }
        }
    }

    return result;
}

/* A subscription between two OTHER clients' ports, which is what aconnect
 * makes. The old version called snd_seq_connect_to(), which subscribes one of
 * OUR ports to the destination and ignored the source entirely -- so every ALSA
 * cable the graph drew was connected to nothing. */
bool AlsaClient::connect_ports(int src_client, int src_port, int dst_client, int dst_port) {
    if (!m_seq) return false;

    snd_seq_port_subscribe_t* sub;
    snd_seq_port_subscribe_alloca(&sub);
    snd_seq_addr_t sender, dest;
    sender.client = static_cast<unsigned char>(src_client);
    sender.port = static_cast<unsigned char>(src_port);
    dest.client = static_cast<unsigned char>(dst_client);
    dest.port = static_cast<unsigned char>(dst_port);
    snd_seq_port_subscribe_set_sender(sub, &sender);
    snd_seq_port_subscribe_set_dest(sub, &dest);

    const int r = snd_seq_subscribe_port(m_seq, sub);
    return r >= 0 || r == -EBUSY; /* -EBUSY: already subscribed */
}

bool AlsaClient::disconnect_ports(int src_client, int src_port, int dst_client, int dst_port) {
    if (!m_seq) return false;

    snd_seq_port_subscribe_t* sub;
    snd_seq_port_subscribe_alloca(&sub);
    snd_seq_addr_t sender, dest;
    sender.client = static_cast<unsigned char>(src_client);
    sender.port = static_cast<unsigned char>(src_port);
    dest.client = static_cast<unsigned char>(dst_client);
    dest.port = static_cast<unsigned char>(dst_port);
    snd_seq_port_subscribe_set_sender(sub, &sender);
    snd_seq_port_subscribe_set_dest(sub, &dest);

    return snd_seq_unsubscribe_port(m_seq, sub) >= 0;
}
