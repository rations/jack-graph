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

private:
    std::string config_dir() const;
    std::string config_file() const;
    
    int m_window_width;
    int m_window_height;
    double m_zoom;
    int m_buffer_size;
    int m_sample_rate;
};
