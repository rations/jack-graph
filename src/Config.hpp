#pragma once

#include <string>

class Config {
public:
    Config();
    ~Config();
    
    void load();
    void save();
    
    int get_window_width() const;
    int get_window_height() const;
    void set_window_size(int w, int h);
    
    double get_zoom() const;
    void set_zoom(double z);
    
    int get_buffer_size() const;
    void set_buffer_size(int bs);
    
    int get_sample_rate() const;
    void set_sample_rate(int sr);

    std::string get_interface() const;
    void set_interface(const std::string& i);

    int get_frames_per_period() const;
    void set_frames_per_period(int fpp);

    int get_periods_per_buffer() const;
    void set_periods_per_buffer(int ppb);

    bool get_realtime() const;
    void set_realtime(bool rt);

    bool get_synchronous() const;
    void set_synchronous(bool sync);

    std::string get_midi_driver() const;
    void set_midi_driver(const std::string& md);

private:
    std::string config_dir() const;
    std::string config_file() const;

    int m_window_width;
    int m_window_height;
    double m_zoom;
    int m_buffer_size;
    int m_sample_rate;
    std::string m_interface;
    int m_frames_per_period;
    int m_periods_per_buffer;
    bool m_realtime;
    bool m_synchronous;
    std::string m_midi_driver;
};
