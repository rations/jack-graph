#pragma once

#include <string>
#include <vector>
#include "Config.hpp"

struct JackSettings {
    std::string interface;
    int sample_rate;
    int frames_per_period;
    int periods_per_buffer;
    bool realtime;
    bool synchronous;
    std::string midi_driver;
};

class JackServerControl {
public:
    JackServerControl();
    ~JackServerControl();

    bool is_running() const;
    std::string get_status() const;

    bool start(const JackSettings& settings);
    bool stop();

    std::string list_audio_devices() const;

private:
    Config m_config;
};
