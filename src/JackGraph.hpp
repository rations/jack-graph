#pragma once

#include <gtkmm.h>
#include <memory>
#include <vector>
#include <string>
#include "JackClient.hpp"
#include "AlsaClient.hpp"
#include "GraphCanvas.hpp"
#include "Node.hpp"
#include "Connection.hpp"
#include "Config.hpp"
#include "JackServerControl.hpp"
#include "SettingsDialog.hpp"

class JackGraph : public Gtk::Window {
public:
    JackGraph();
    ~JackGraph() override;

private:
    void setup_ui();
    void setup_menu();
    void refresh_ports();
    void sync_canvas_from_jack();
    void sync_canvas_from_alsa();

    void on_connect_jack(const std::string& source, const std::string& dest);
    void on_disconnect_jack(const std::string& source, const std::string& dest);
    void on_connect_alsa(const std::string& source, const std::string& dest);
    void on_disconnect_alsa(const std::string& source, const std::string& dest);

    void on_menu_refresh();
    void on_menu_zoom_in();
    void on_menu_zoom_out();
    void on_menu_zoom_normal();
    void on_menu_settings();
    void on_menu_about();
    void on_menu_quit();

    void update_status_bar();

    Gtk::Box m_main_box;
    Gtk::MenuBar m_menu_bar;
    Gtk::ScrolledWindow m_scrolled_window;
    GraphCanvas m_canvas;
    Gtk::Statusbar m_statusbar;
    guint m_status_context_id;

    JackClient m_jack;
    AlsaClient m_alsa;
    Config m_config;
    JackServerControl m_server;

    bool m_jack_connected;
    bool m_alsa_connected;
};
