#include "Config.hpp"
#include "platform/fs.h"

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace {

/* One integer from the config, or `fallback` if the value is not a whole number
 * in [lo, hi]. std::stoi threw on a corrupt line, and nothing caught it, so a
 * config truncated mid-write stopped the program from starting at all. */
int parse_int(const std::string& value, int lo, int hi, int fallback) {
    const char* s = value.c_str();
    char* end = nullptr;
    errno = 0;
    const long v = std::strtol(s, &end, 10);
    if (end == s || *end != '\0' || errno == ERANGE || v < lo || v > hi) return fallback;
    return static_cast<int>(v);
}

double parse_double(const std::string& value, double lo, double hi, double fallback) {
    const char* s = value.c_str();
    char* end = nullptr;
    errno = 0;
    const double v = std::strtod(s, &end);
    if (end == s || *end != '\0' || errno == ERANGE || !(v >= lo && v <= hi)) return fallback;
    return v;
}

} // namespace

Config::Config()
    : m_window_width(1200), m_window_height(800),
      m_zoom(1.0), m_buffer_size(0), m_sample_rate(0),
      m_interface(""), m_frames_per_period(0), m_periods_per_buffer(0),
      m_realtime(false), m_synchronous(false), m_midi_driver("") {
}

Config::~Config() {
}

std::string Config::config_dir() const {
    return jackgraph::fs::configHome() + "/jack-graph";
}

std::string Config::config_file() const {
    return config_dir() + "/config";
}

void Config::load() {
    std::string body;
    if (!jackgraph::fs::readFile(config_file(), body)) return;

    std::istringstream file(body);
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);

        if (key == "window_width") m_window_width = parse_int(value, 100, 16384, m_window_width);
        else if (key == "window_height") m_window_height = parse_int(value, 100, 16384, m_window_height);
        else if (key == "zoom") m_zoom = parse_double(value, 0.01, 100.0, m_zoom);
        else if (key == "buffer_size") m_buffer_size = parse_int(value, 0, 1 << 20, m_buffer_size);
        else if (key == "sample_rate") m_sample_rate = parse_int(value, 0, 1 << 22, m_sample_rate);
        else if (key == "interface") m_interface = value;
        else if (key == "frames_per_period") m_frames_per_period = parse_int(value, 0, 1 << 20, m_frames_per_period);
        else if (key == "periods_per_buffer") m_periods_per_buffer = parse_int(value, 0, 64, m_periods_per_buffer);
        else if (key == "realtime") m_realtime = (value == "true");
        else if (key == "synchronous") m_synchronous = (value == "true");
        else if (key == "midi_driver") m_midi_driver = value;
    }
}

void Config::save() {
    std::ostringstream file;
    file << "window_width=" << m_window_width << "\n";
    file << "window_height=" << m_window_height << "\n";
    file << "zoom=" << m_zoom << "\n";
    file << "buffer_size=" << m_buffer_size << "\n";
    file << "sample_rate=" << m_sample_rate << "\n";
    file << "interface=" << m_interface << "\n";
    file << "frames_per_period=" << m_frames_per_period << "\n";
    file << "periods_per_buffer=" << m_periods_per_buffer << "\n";
    file << "realtime=" << (m_realtime ? "true" : "false") << "\n";
    /* The key says "synchronous" for compatibility with every config written
     * so far. What it holds is jackd's ALSA driver -S, which is 16-bit samples
     * ("shorts"), not synchronous mode -- see get_synchronous(). */
    file << "synchronous=" << (m_synchronous ? "true" : "false") << "\n";
    file << "midi_driver=" << m_midi_driver << "\n";

    /* Written to a temporary and renamed into place, so a crash mid-write
     * leaves the previous config rather than an empty one. */
    if (!jackgraph::fs::writeFileAtomic(config_file(), file.str()))
        fprintf(stderr, "jack-graph: could not save %s\n", config_file().c_str());
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

std::string Config::get_interface() const { return m_interface; }
void Config::set_interface(const std::string& i) { m_interface = i; }

int Config::get_frames_per_period() const { return m_frames_per_period; }
void Config::set_frames_per_period(int fpp) { m_frames_per_period = fpp; }

int Config::get_periods_per_buffer() const { return m_periods_per_buffer; }
void Config::set_periods_per_buffer(int ppb) { m_periods_per_buffer = ppb; }

bool Config::get_realtime() const { return m_realtime; }
void Config::set_realtime(bool rt) { m_realtime = rt; }

bool Config::get_synchronous() const { return m_synchronous; }
void Config::set_synchronous(bool sync) { m_synchronous = sync; }

std::string Config::get_midi_driver() const { return m_midi_driver; }
void Config::set_midi_driver(const std::string& md) { m_midi_driver = md; }
