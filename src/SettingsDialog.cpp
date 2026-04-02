#include "SettingsDialog.hpp"
#include <sstream>
#include <unistd.h>

SettingsDialog::SettingsDialog(Gtk::Window& parent, JackServerControl& server, Config& config)
    : Gtk::Dialog("JACK Settings", parent, true),
      m_server(server), m_config(config),
      m_content_box(Gtk::ORIENTATION_VERTICAL, 10),
      m_server_frame("JACK Server"),
      m_server_box(Gtk::ORIENTATION_HORIZONTAL, 10),
      m_start_stop_btn("Start"),
      m_audio_frame("Audio"),
      m_interface_label("Interface:"),
      m_sample_rate_label("Sample Rate:"),
      m_frames_label("Frames/Period:"),
      m_periods_label("Periods/Buffer:"),
      m_midi_frame("MIDI"),
      m_midi_label("MIDI Driver:"),
      m_options_frame("Options"),
      m_options_box(Gtk::ORIENTATION_VERTICAL, 4),
      m_realtime_check("Realtime"),
      m_sync_check("Use Server Synchronous Mode"),
      m_button_box(Gtk::ORIENTATION_HORIZONTAL, 10),
      m_apply_btn("Apply"),
      m_close_btn("Close") {
    set_default_size(420, 500);

    add_action_widget(m_close_btn, Gtk::RESPONSE_CLOSE);

    build_ui();
    load_current_settings();
    populate_devices();

    show_all_children();
}

SettingsDialog::~SettingsDialog() = default;

void SettingsDialog::build_ui() {
    auto* content_area = get_content_area();
    content_area->pack_start(m_content_box, true, true, 10);

    m_server_box.pack_start(m_server_status_label, true, true, 0);
    m_server_box.pack_start(m_start_stop_btn, false, false, 0);
    m_server_frame.add(m_server_box);
    m_content_box.pack_start(m_server_frame, false, false, 0);

    m_audio_grid.attach(m_interface_label, 0, 0, 1, 1);
    m_audio_grid.attach(m_interface_combo, 1, 0, 1, 1);
    m_audio_grid.attach(m_sample_rate_label, 0, 1, 1, 1);
    m_audio_grid.attach(m_sample_rate_combo, 1, 1, 1, 1);
    m_audio_grid.attach(m_frames_label, 0, 2, 1, 1);
    m_audio_grid.attach(m_frames_combo, 1, 2, 1, 1);
    m_audio_grid.attach(m_periods_label, 0, 3, 1, 1);
    m_audio_grid.attach(m_periods_combo, 1, 3, 1, 1);
    m_audio_grid.set_column_spacing(10);
    m_audio_grid.set_row_spacing(8);
    m_audio_frame.add(m_audio_grid);
    m_content_box.pack_start(m_audio_frame, false, false, 0);

    m_midi_grid.attach(m_midi_label, 0, 0, 1, 1);
    m_midi_grid.attach(m_midi_combo, 1, 0, 1, 1);
    m_midi_grid.set_column_spacing(10);
    m_midi_grid.set_row_spacing(8);
    m_midi_frame.add(m_midi_grid);
    m_content_box.pack_start(m_midi_frame, false, false, 0);

    m_options_box.pack_start(m_realtime_check, false, false, 0);
    m_options_box.pack_start(m_sync_check, false, false, 0);
    m_options_frame.add(m_options_box);
    m_content_box.pack_start(m_options_frame, false, false, 0);

    m_sample_rate_combo.append("44100", "44100");
    m_sample_rate_combo.append("48000", "48000");
    m_sample_rate_combo.append("88200", "88200");
    m_sample_rate_combo.append("96000", "96000");
    m_sample_rate_combo.append("192000", "192000");

    m_frames_combo.append("64", "64");
    m_frames_combo.append("128", "128");
    m_frames_combo.append("256", "256");
    m_frames_combo.append("512", "512");
    m_frames_combo.append("1024", "1024");
    m_frames_combo.append("2048", "2048");

    m_periods_combo.append("2", "2");
    m_periods_combo.append("3", "3");
    m_periods_combo.append("4", "4");
    m_periods_combo.append("5", "5");
    m_periods_combo.append("6", "6");
    m_periods_combo.append("7", "7");
    m_periods_combo.append("8", "8");

    m_midi_combo.append("none", "None");
    m_midi_combo.append("seq", "ALSA SEQ");

    m_button_box.pack_start(m_apply_btn, false, false, 0);
    m_button_box.pack_start(m_close_btn, false, false, 0);
    m_content_box.pack_start(m_button_box, false, false, 0);

    m_start_stop_btn.signal_clicked().connect(sigc::mem_fun(*this, &SettingsDialog::on_start_stop));
    m_apply_btn.signal_clicked().connect(sigc::mem_fun(*this, &SettingsDialog::on_apply));
}

void SettingsDialog::populate_devices() {
    m_interface_combo.remove_all();
    m_interface_combo.append("default", "default");

    std::string devices = m_server.list_audio_devices();
    std::istringstream stream(devices);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        m_interface_combo.append(line, line);
    }
}

void SettingsDialog::load_current_settings() {
    bool running = m_server.is_running();
    m_server_status_label.set_text("Status: " + m_server.get_status());
    m_start_stop_btn.set_label(running ? "Stop" : "Start");

    m_realtime_check.set_active(true);
    m_sync_check.set_active(false);

    m_sample_rate_combo.set_active_id("48000");
    m_frames_combo.set_active_id("128");
    m_periods_combo.set_active_id("2");
    m_midi_combo.set_active_id("none");
}

void SettingsDialog::on_apply() {
    JackSettings settings;
    settings.interface = m_interface_combo.get_active_id();
    settings.sample_rate = std::stoi(m_sample_rate_combo.get_active_id());
    settings.frames_per_period = std::stoi(m_frames_combo.get_active_id());
    settings.periods_per_buffer = std::stoi(m_periods_combo.get_active_id());
    settings.realtime = m_realtime_check.get_active();
    settings.synchronous = m_sync_check.get_active();
    settings.midi_driver = m_midi_combo.get_active_id();

    if (m_server.is_running()) {
        m_server.stop();
        usleep(200000);
    }

    bool started = m_server.start(settings);
    m_server_status_label.set_text("Status: " + m_server.get_status());
    m_start_stop_btn.set_label(started ? "Stop" : "Start");

    if (m_apply_cb) {
        m_apply_cb();
    }
}

void SettingsDialog::on_start_stop() {
    if (m_server.is_running()) {
        m_server.stop();
        m_server_status_label.set_text("Status: " + m_server.get_status());
        m_start_stop_btn.set_label("Start");
        if (m_apply_cb) {
            m_apply_cb();
        }
    } else {
        on_apply();
    }
}
