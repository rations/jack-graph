#pragma once

#include <gtkmm.h>
#include <functional>
#include "JackServerControl.hpp"
#include "Config.hpp"

class SettingsDialog : public Gtk::Dialog {
public:
    SettingsDialog(Gtk::Window& parent, JackServerControl& server, Config& config);
    ~SettingsDialog() override;

    using ApplyCallback = std::function<void()>;
    void set_apply_callback(ApplyCallback cb) { m_apply_cb = std::move(cb); }

private:
    void build_ui();
    void populate_devices();
    void load_current_settings();
    void on_apply();
    void on_start_stop();

    JackServerControl& m_server;
    Config& m_config;

    Gtk::Box m_content_box;

    Gtk::Frame m_server_frame;
    Gtk::Box m_server_box;
    Gtk::Label m_server_status_label;
    Gtk::Button m_start_stop_btn;

    Gtk::Frame m_audio_frame;
    Gtk::Grid m_audio_grid;
    Gtk::Label m_interface_label;
    Gtk::ComboBoxText m_interface_combo;
    Gtk::Label m_sample_rate_label;
    Gtk::ComboBoxText m_sample_rate_combo;
    Gtk::Label m_frames_label;
    Gtk::ComboBoxText m_frames_combo;
    Gtk::Label m_periods_label;
    Gtk::ComboBoxText m_periods_combo;

    Gtk::Frame m_midi_frame;
    Gtk::Grid m_midi_grid;
    Gtk::Label m_midi_label;
    Gtk::ComboBoxText m_midi_combo;

    Gtk::Frame m_options_frame;
    Gtk::Box m_options_box;
    Gtk::CheckButton m_realtime_check;
    Gtk::CheckButton m_sync_check;

    Gtk::Box m_button_box;
    Gtk::Button m_apply_btn;
    Gtk::Button m_close_btn;

    ApplyCallback m_apply_cb;
};
