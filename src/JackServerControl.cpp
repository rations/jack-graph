#include "JackServerControl.hpp"
#include <jack/jack.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

JackServerControl::JackServerControl() {
}

JackServerControl::~JackServerControl() {
}

bool JackServerControl::is_running() const {
    jack_client_t *test = jack_client_open("status_check", JackNoStartServer, NULL);
    if (test) {
        jack_client_close(test);
        return true;
    }
    return false;
}

std::string JackServerControl::get_status() const {
    if (is_running()) {
        return "Running";
    }
    return "Stopped";
}

bool JackServerControl::start(const JackSettings& settings) {
    if (is_running()) {
        fprintf(stderr, "jack-graph: JACK is already running.\n");
        return true;
    }

    fprintf(stderr, "jack-graph: Starting jackd-rt service with new settings via pkexec...\n");
    
    // Use pkexec to run the helper script with root privileges
    // The helper updates /etc/default/jackd-rt and starts the service
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
        "pkexec /usr/local/lib/jack-bridge/jack-bridge-service-helper start "
        "\"%s\" %d %d %d \"%s\" 2>&1",
        settings.interface.c_str(),
        settings.sample_rate,
        settings.frames_per_period,
        settings.periods_per_buffer,
        settings.midi_driver.c_str());
    
    fprintf(stderr, "jack-graph: Running: %s\n", cmd);
    
    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "jack-graph: Failed to start jackd-rt service (exit code %d)\n", ret);
        return false;
    }
    
    usleep(2000000);
    return is_running();
}

bool JackServerControl::stop() {
    fprintf(stderr, "jack-graph: Stopping jackd-rt service via pkexec...\n");
    
    // Use pkexec to run the helper script with root privileges
    int ret = system("pkexec /usr/local/lib/jack-bridge/jack-bridge-service-helper stop 2>&1");
    if (ret != 0) {
        fprintf(stderr, "jack-graph: Failed to stop jackd-rt service (exit code %d)\n", ret);
        return false;
    }
    
    return true;
}

std::string JackServerControl::list_audio_devices() const {
    std::string result;
    FILE* fp = popen("aplay -l 2>/dev/null | grep 'card [0-9]' | sed 's/card \\([0-9]\\):.*device \\([0-9]\\):.*/hw:\\1,\\2/' | sort | uniq", "r");
    if (!fp) return result;

    char buf[256];
    while (fgets(buf, sizeof(buf), fp)) {
        std::string line(buf);
        line.erase(line.find_last_not_of(" \n\r\t") + 1);
        if (!line.empty()) {
            result += line + "\n";
        }
    }
    pclose(fp);
    return result;
}
