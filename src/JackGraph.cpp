#include "JackGraph.hpp"
#include <iostream>

JackGraph::JackGraph()
    : m_main_box(Gtk::ORIENTATION_VERTICAL),
      m_jack_connected(false), m_alsa_connected(false) {
    set_title("Jack Graph");
    set_default_size(1200, 800);

    m_config.load();

    setup_ui();
    setup_menu();

    m_jack_connected = m_jack.connect("jack-graph");
    if (m_jack_connected) {
        m_jack.set_port_callback([this]() {
            Glib::signal_idle().connect_once([this]() {
                refresh_ports();
            });
        });
    }

    m_alsa_connected = m_alsa.connect("jack-graph");

    refresh_ports();
    update_status_bar();

    show_all_children();
}

JackGraph::~JackGraph() {
    m_config.save();
}

void JackGraph::setup_ui() {
    add(m_main_box);

    m_main_box.pack_start(m_menu_bar, Gtk::PACK_SHRINK);

    m_scrolled_window.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    m_scrolled_window.add(m_canvas);
    m_main_box.pack_start(m_scrolled_window, Gtk::PACK_EXPAND_WIDGET);

    m_main_box.pack_start(m_statusbar, Gtk::PACK_SHRINK);

    m_canvas.set_connect_callback(
        [this](const std::string& src, const std::string& dst) {
            on_connect_jack(src, dst);
        });

    m_canvas.set_disconnect_callback(
        [this](const std::string& src, const std::string& dst) {
            on_disconnect_jack(src, dst);
        });
}

void JackGraph::setup_menu() {
    auto file_menu = Gtk::manage(new Gtk::Menu());
    auto file_item = Gtk::manage(new Gtk::MenuItem("_File", true));
    file_item->set_submenu(*file_menu);
    m_menu_bar.append(*file_item);

    auto refresh_item = Gtk::manage(new Gtk::MenuItem("_Refresh", true));
    refresh_item->signal_activate().connect(
        sigc::mem_fun(*this, &JackGraph::on_menu_refresh));
    file_menu->append(*refresh_item);

    auto sep = Gtk::manage(new Gtk::SeparatorMenuItem());
    file_menu->append(*sep);

    auto quit_item = Gtk::manage(new Gtk::MenuItem("_Quit", true));
    quit_item->signal_activate().connect(
        sigc::mem_fun(*this, &JackGraph::on_menu_quit));
    file_menu->append(*quit_item);

    auto view_menu = Gtk::manage(new Gtk::Menu());
    auto view_item = Gtk::manage(new Gtk::MenuItem("_View", true));
    view_item->set_submenu(*view_menu);
    m_menu_bar.append(*view_item);

    auto zoom_in_item = Gtk::manage(new Gtk::MenuItem("Zoom _In", true));
    zoom_in_item->signal_activate().connect(
        sigc::mem_fun(*this, &JackGraph::on_menu_zoom_in));
    view_menu->append(*zoom_in_item);

    auto zoom_out_item = Gtk::manage(new Gtk::MenuItem("Zoom _Out", true));
    zoom_out_item->signal_activate().connect(
        sigc::mem_fun(*this, &JackGraph::on_menu_zoom_out));
    view_menu->append(*zoom_out_item);

    auto zoom_normal_item = Gtk::manage(new Gtk::MenuItem("Zoom _Normal", true));
    zoom_normal_item->signal_activate().connect(
        sigc::mem_fun(*this, &JackGraph::on_menu_zoom_normal));
    view_menu->append(*zoom_normal_item);

    auto help_menu = Gtk::manage(new Gtk::Menu());
    auto help_item = Gtk::manage(new Gtk::MenuItem("_Help", true));
    help_item->set_submenu(*help_menu);
    m_menu_bar.append(*help_item);

    auto about_item = Gtk::manage(new Gtk::MenuItem("_About", true));
    about_item->signal_activate().connect(
        sigc::mem_fun(*this, &JackGraph::on_menu_about));
    help_menu->append(*about_item);

    m_status_context_id = m_statusbar.get_context_id("jack-graph");
}

void JackGraph::refresh_ports() {
    m_canvas.remove_all();

    if (m_jack_connected) {
        std::string our_client = m_jack.get_actual_client_name();
        std::cerr << "JACK client name: " << our_client << std::endl;
        auto ports = m_jack.get_ports();
        for (const auto& p : ports) {
            if (p.client == our_client) continue;
            auto node = std::make_shared<Node>(
                p.name,
                p.is_audio ? PortType::AUDIO : PortType::MIDI,
                p.is_output ? PortDirection::OUTPUT : PortDirection::INPUT);
            m_canvas.add_node(node);
        }

        auto conns = m_jack.get_connections();
        auto all_nodes = m_canvas.get_nodes();
        for (const auto& c : conns) {
            std::shared_ptr<Node> src_node, dst_node;
            for (auto& n : all_nodes) {
                if (n->full_name() == c.source) src_node = n;
                if (n->full_name() == c.destination) dst_node = n;
            }
            if (src_node && dst_node) {
                auto conn = std::make_shared<Connection>(src_node, dst_node, src_node->type);
                m_canvas.add_connection(conn);
            }
        }
    }

    if (m_alsa_connected) {
        auto ports = m_alsa.get_ports();
        for (const auto& p : ports) {
            std::string full_name = p.client + ":" + p.name;
            auto node = std::make_shared<Node>(
                full_name,
                PortType::MIDI,
                p.is_output ? PortDirection::OUTPUT : PortDirection::INPUT,
                true);
            m_canvas.add_node(node);
        }
    }

    m_canvas.layout(true);
    update_status_bar();
}

void JackGraph::on_connect_jack(const std::string& source, const std::string& dest) {
    if (m_jack_connected) {
        m_jack.connect_ports(source, dest);
    }
}

void JackGraph::on_disconnect_jack(const std::string& source, const std::string& dest) {
    if (m_jack_connected) {
        m_jack.disconnect_ports(source, dest);
    }
}

void JackGraph::on_connect_alsa(const std::string& source, const std::string& dest) {
    (void)source;
    (void)dest;
}

void JackGraph::on_disconnect_alsa(const std::string& source, const std::string& dest) {
    (void)source;
    (void)dest;
}

void JackGraph::on_menu_refresh() {
    refresh_ports();
}

void JackGraph::on_menu_zoom_in() {
    m_canvas.set_zoom(m_canvas.get_zoom() * 1.2);
}

void JackGraph::on_menu_zoom_out() {
    m_canvas.set_zoom(m_canvas.get_zoom() / 1.2);
}

void JackGraph::on_menu_zoom_normal() {
    m_canvas.set_zoom(1.0);
}

void JackGraph::on_menu_about() {
    Gtk::AboutDialog dialog;
    dialog.set_program_name("Jack Graph");
    dialog.set_version("0.1.0");
    dialog.set_comments("JACK and ALSA port connection manager");
    dialog.set_copyright("GPL-3.0+");
    dialog.set_transient_for(*this);
    dialog.run();
}

void JackGraph::on_menu_quit() {
    hide();
}

void JackGraph::update_status_bar() {
    std::string status;

    if (m_jack_connected) {
        status += "JACK: connected";
        status += " | Buffer: " + std::to_string(m_jack.get_buffer_size()) + " frames";
        status += " | Rate: " + std::to_string(m_jack.get_sample_rate()) + " Hz";
    } else {
        status += "JACK: not connected";
    }

    if (m_alsa_connected) {
        status += " | ALSA MIDI: connected";
    }

    m_statusbar.push(status, m_status_context_id);
}
