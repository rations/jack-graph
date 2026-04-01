#include "Config.hpp"
#include <fstream>
#include <filesystem>
#include <cstdlib>

namespace fs = std::filesystem;

Config::Config()
    : m_window_width(1200), m_window_height(800),
      m_zoom(1.0), m_buffer_size(0), m_sample_rate(0) {
}

Config::~Config() {
}

std::string Config::config_dir() const {
    const char* home = std::getenv("HOME");
    if (!home) return "/tmp";
    return std::string(home) + "/.config/jack-graph";
}

std::string Config::config_file() const {
    return config_dir() + "/config";
}

void Config::load() {
    std::ifstream file(config_file());
    if (!file.is_open()) return;
    
    std::string line;
    while (std::getline(file, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        
        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        
        if (key == "window_width") m_window_width = std::stoi(value);
        else if (key == "window_height") m_window_height = std::stoi(value);
        else if (key == "zoom") m_zoom = std::stod(value);
        else if (key == "buffer_size") m_buffer_size = std::stoi(value);
        else if (key == "sample_rate") m_sample_rate = std::stoi(value);
    }
}

void Config::save() {
    fs::create_directories(config_dir());
    std::ofstream file(config_file());
    file << "window_width=" << m_window_width << "\n";
    file << "window_height=" << m_window_height << "\n";
    file << "zoom=" << m_zoom << "\n";
    file << "buffer_size=" << m_buffer_size << "\n";
    file << "sample_rate=" << m_sample_rate << "\n";
}

int Config::get_window_width() const { return m_window_width; }
int Config::get_window_height() const { return m_window_height; }
void Config::set_window_size(int w, int h) { m_window_width = w; m_window_height = h; }
double Config::get_zoom() const { return m_zoom; }
void Config::set_zoom(double z) { m_zoom = z; }
int Config::get_buffer_size() const { return m_buffer_size; }
void Config::set_buffer_size(int bs) { m_buffer_size = bs; }
int Config::get_sample_rate() const { return m_sample_rate; }
void Config::set_sample_rate(int sr) { m_sample_rate = sr; }
