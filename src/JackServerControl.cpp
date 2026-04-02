#include "JackServerControl.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

JackServerControl::JackServerControl() : m_jack_pid(0) {
}

JackServerControl::~JackServerControl() {
    if (m_jack_pid > 0) {
        stop();
    }
}

bool JackServerControl::is_running() const {
    if (m_jack_pid > 0) {
        return kill(m_jack_pid, 0) == 0;
    }

    FILE* fp = popen("pgrep -x jackd 2>/dev/null", "r");
    if (!fp) return false;
    char buf[64] = {0};
    fgets(buf, sizeof(buf), fp);
    pclose(fp);
    return strlen(buf) > 0;
}

std::string JackServerControl::get_status() const {
    if (is_running()) {
        return "Running";
    }
    return "Stopped";
}

bool JackServerControl::start(const JackSettings& settings) {
    if (is_running()) {
        return true;
    }

    pid_t pid = fork();
    if (pid < 0) {
        return false;
    }

    if (pid == 0) {
        std::vector<std::string> args;
        args.push_back("jackd");
        args.push_back("-d");
        args.push_back("alsa");

        if (!settings.interface.empty()) {
            args.push_back("-d");
            args.push_back(settings.interface);
        }

        args.push_back("-r");
        args.push_back(std::to_string(settings.sample_rate));

        args.push_back("-p");
        args.push_back(std::to_string(settings.frames_per_period));

        args.push_back("-n");
        args.push_back(std::to_string(settings.periods_per_buffer));

        if (settings.realtime) {
            args.push_back("-R");
        }

        if (settings.synchronous) {
            args.push_back("-s");
        }

        if (settings.midi_driver == "seq") {
            args.push_back("-X");
            args.push_back("seq");
        } else if (settings.midi_driver == "none") {
            args.push_back("-X");
            args.push_back("none");
        }

        std::vector<char*> cargs;
        for (auto& a : args) {
            cargs.push_back(const_cast<char*>(a.c_str()));
        }
        cargs.push_back(nullptr);

        execvp("jackd", cargs.data());
        _exit(1);
    }

    m_jack_pid = pid;
    usleep(1000000);
    return is_running();
}

bool JackServerControl::stop() {
    if (m_jack_pid > 0) {
        kill(m_jack_pid, SIGTERM);
        for (int i = 0; i < 10; ++i) {
            if (kill(m_jack_pid, 0) != 0) break;
            usleep(100000);
        }
        m_jack_pid = 0;
    }

    FILE* fp = popen("pkill -x jackd 2>/dev/null", "r");
    if (fp) pclose(fp);

    usleep(500000);
    return !is_running();
}

std::string JackServerControl::list_audio_devices() const {
    std::string result;
    FILE* fp = popen("aplay -l 2>/dev/null | grep '^card' | sed 's/.*: //;s/:.*//'", "r");
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
