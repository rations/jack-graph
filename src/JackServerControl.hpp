#pragma once

#include <string>
#include <sys/types.h>

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
    std::string get_last_error() const;

private:
    pid_t m_child_pid;
    std::string m_last_error;
};
