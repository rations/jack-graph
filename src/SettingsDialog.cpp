#include "SettingsDialog.hpp"
#include <sstream>

SettingsDialog::SettingsDialog(Gtk::Window& parent, JackServerControl& server, Config& config)
    : Gtk::Dialog("JACK Settings", parent, true),
      m_server(server), m_config(config),
      m_content_box(Gtk::ORIENTATION_VERTICAL, 10),
      m_server_frame("JACK Server"),
      m_server_box(Gtk::ORIENTATION_HORIZONTAL, 10),
      m_start_btn("Start"),
      m_stop_btn("Stop"),
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
      m_close_btn("Close") {
    set_default_size(420, 500);

    add_action_widget(m_close_btn, Gtk::RESPONSE_CLOSE);

    build_ui();
    populate_devices();       // items must exist before load_current_settings sets active IDs
    load_current_settings();

    show_all_children();
}

SettingsDialog::~SettingsDialog() = default;

void SettingsDialog::build_ui() {
    auto* content_area = get_content_area();
    content_area->pack_start(m_content_box, true, true, 10);

    m_server_box.pack_start(m_server_status_label, true, true, 0);
    m_server_box.pack_start(m_start_btn, false, false, 0);
    m_server_box.pack_start(m_stop_btn, false, false, 0);
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

    m_button_box.pack_start(m_close_btn, false, false, 0);
    m_content_box.pack_start(m_button_box, false, false, 0);

    m_start_btn.signal_clicked().connect(sigc::mem_fun(*this, &SettingsDialog::on_start));
    m_stop_btn.signal_clicked().connect(sigc::mem_fun(*this, &SettingsDialog::on_stop));
}

void SettingsDialog::populate_devices() {
    m_interface_combo.remove_all();
    m_interface_combo.append("default", "default");

    std::string devices = m_server.list_audio_devices();
    std::istringstream stream(devices);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        auto pipe = line.find('|');
        if (pipe == std::string::npos) {
            m_interface_combo.append(line, line);
        } else {
            m_interface_combo.append(line.substr(0, pipe), line.substr(pipe + 1));
        }
    }
}

void SettingsDialog::update_server_status(bool running) {
    m_server_status_label.set_text(running ? "Status: Running" : "Status: Stopped");
    m_start_btn.set_sensitive(!running);
    m_stop_btn.set_sensitive(running);
}

void SettingsDialog::load_current_settings() {
    update_server_status(m_server.is_running());

    // Interface
    std::string iface = m_config.get_interface();
    if (!iface.empty())
        m_interface_combo.set_active_id(iface);

    // Sample rate — default 48000 on first run
    int sr = m_config.get_sample_rate();
    m_sample_rate_combo.set_active_id(sr > 0 ? std::to_string(sr) : "48000");

    // Frames/period — default 1024 on first run
    int fpp = m_config.get_frames_per_period();
    m_frames_combo.set_active_id(fpp > 0 ? std::to_string(fpp) : "1024");

    // Periods/buffer — default 2 on first run
    int ppb = m_config.get_periods_per_buffer();
    m_periods_combo.set_active_id(ppb > 0 ? std::to_string(ppb) : "2");

    // MIDI driver — default none on first run
    std::string midi = m_config.get_midi_driver();
    m_midi_combo.set_active_id(!midi.empty() ? midi : "none");

    // Checkboxes — correctly loaded from config
    m_realtime_check.set_active(m_config.get_realtime());
    m_sync_check.set_active(m_config.get_synchronous());
}

void SettingsDialog::on_start() {
    std::string iface   = m_interface_combo.get_active_id();
    std::string sr_str  = m_sample_rate_combo.get_active_id();
    std::string fpp_str = m_frames_combo.get_active_id();
    std::string ppb_str = m_periods_combo.get_active_id();

    if (sr_str.empty() || fpp_str.empty() || ppb_str.empty()) {
        Gtk::MessageDialog err(*this, "Please select Sample Rate, Frames/Period, and Periods/Buffer.",
                               false, Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
        err.run();
        return;
    }

    JackSettings settings;
    settings.interface        = iface;
    settings.sample_rate      = std::stoi(sr_str);
    settings.frames_per_period  = std::stoi(fpp_str);
    settings.periods_per_buffer = std::stoi(ppb_str);
    settings.realtime         = m_realtime_check.get_active();
    settings.synchronous      = m_sync_check.get_active();
    settings.midi_driver      = m_midi_combo.get_active_id();

    m_config.set_interface(settings.interface);
    m_config.set_sample_rate(settings.sample_rate);
    m_config.set_frames_per_period(settings.frames_per_period);
    m_config.set_periods_per_buffer(settings.periods_per_buffer);
    m_config.set_midi_driver(settings.midi_driver);
    m_config.set_realtime(settings.realtime);
    m_config.set_synchronous(settings.synchronous);
    m_config.save();

    m_server.start(settings);

    update_server_status(m_server.is_running());
}

void SettingsDialog::on_stop() {
    m_server.stop();

    usleep(200000);
    update_server_status(m_server.is_running());
}
