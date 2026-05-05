#include "JackServerControl.hpp"
#include <jack/jack.h>
#include <alsa/asoundlib.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cerrno>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

static const char* PID_FILE = "/tmp/jack-graph-jackd.pid";
static const char* LOG_FILE = "/tmp/jack-graph-jackd.log";

JackServerControl::JackServerControl() : m_child_pid(0) {
    std::ifstream pf(PID_FILE);
    if (pf.is_open()) {
        pid_t pid = 0;
        pf >> pid;
        pf.close();
        if (pid > 0) {
            if (kill(pid, 0) == 0) {
                m_child_pid = pid;
                fprintf(stderr, "jack-graph: recovered jackd PID %d from previous session\n", pid);
            } else {
                unlink(PID_FILE);
            }
        }
    }
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
    return is_running() ? "Running" : "Stopped";
}

std::string JackServerControl::get_last_error() const {
    return m_last_error;
}

bool JackServerControl::start(const JackSettings& settings) {
    m_last_error.clear();

    if (is_running()) {
        fprintf(stderr, "jack-graph: JACK is already running.\n");
        return true;
    }

    // Build argv: jackd [-R] -d alsa [-d hw:X,Y] -r rate -p period -n nperiods [-S] [-X seq]
    std::vector<std::string> args;
    args.push_back("jackd");
    if (settings.realtime)
        args.push_back("-R");

    args.push_back("-d");
    args.push_back("alsa");

    if (!settings.interface.empty() && settings.interface != "default") {
        args.push_back("-d");
        args.push_back(settings.interface);
    }
    args.push_back("-r");
    args.push_back(std::to_string(settings.sample_rate));
    args.push_back("-p");
    args.push_back(std::to_string(settings.frames_per_period));
    args.push_back("-n");
    args.push_back(std::to_string(settings.periods_per_buffer));
    if (settings.synchronous)
        args.push_back("-S");
    if (settings.midi_driver == "seq") {
        args.push_back("-X");
        args.push_back("seq");
    }

    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);

    fprintf(stderr, "jack-graph: launching:");
    for (auto& a : args) fprintf(stderr, " %s", a.c_str());
    fprintf(stderr, "\n");

    pid_t pid = fork();
    if (pid < 0) {
        m_last_error = std::string("fork() failed: ") + strerror(errno);
        fprintf(stderr, "jack-graph: %s\n", m_last_error.c_str());
        return false;
    }

    if (pid == 0) {
        // Child: redirect jackd stderr to log file for error reporting
        int logfd = open(LOG_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (logfd >= 0) {
            dup2(logfd, STDERR_FILENO);
            close(logfd);
        }
        execvp("jackd", argv.data());
        // execvp only returns on failure — write to the log we just opened
        const char* msg = "execvp(jackd) failed\n";
        write(STDERR_FILENO, msg, strlen(msg));
        _exit(127);
    }

    // Parent: store PID
    m_child_pid = pid;
    {
        std::ofstream pf(PID_FILE);
        if (pf.is_open()) pf << pid << "\n";
    }

    // Poll until jackd is ready (max 5 seconds)
    for (int i = 0; i < 10; i++) {
        usleep(500000);
        if (is_running()) return true;
    }

    // Timeout — read jackd's log for the error message
    std::ifstream log(LOG_FILE);
    if (log.is_open()) {
        std::ostringstream ss;
        ss << log.rdbuf();
        m_last_error = ss.str();
    }
    if (m_last_error.empty())
        m_last_error = "jackd did not start within 5 seconds (no output captured).";

    fprintf(stderr, "jack-graph: jackd failed to start:\n%s\n", m_last_error.c_str());
    kill(m_child_pid, SIGTERM);
    waitpid(m_child_pid, nullptr, 0);
    m_child_pid = 0;
    unlink(PID_FILE);
    return false;
}

bool JackServerControl::stop() {
    if (m_child_pid == 0) {
        fprintf(stderr, "jack-graph: no jackd PID tracked — cannot stop\n");
        return false;
    }

    if (kill(m_child_pid, 0) == -1 && errno == ESRCH) {
        m_child_pid = 0;
        unlink(PID_FILE);
        return true;
    }

    fprintf(stderr, "jack-graph: stopping jackd (PID %d)\n", m_child_pid);
    kill(m_child_pid, SIGTERM);

    for (int i = 0; i < 20; i++) {
        usleep(500000);
        int status;
        pid_t result = waitpid(m_child_pid, &status, WNOHANG);
        if (result == m_child_pid || (result == -1 && errno == ECHILD))
            break;
    }

    if (kill(m_child_pid, 0) == 0) {
        fprintf(stderr, "jack-graph: jackd did not exit cleanly, sending SIGKILL\n");
        kill(m_child_pid, SIGKILL);
        waitpid(m_child_pid, nullptr, 0);
    }

    m_child_pid = 0;
    unlink(PID_FILE);
    return !is_running();
}

// Enumerate playback-capable PCM devices using the ALSA control API.
// Returns lines of the form "hw:X,Y|Card Name - Device Name\n".
std::string JackServerControl::list_audio_devices() const {
    std::string result;

    int card = -1;
    while (snd_card_next(&card) == 0 && card >= 0) {
        char ctl_name[16];
        snprintf(ctl_name, sizeof(ctl_name), "hw:%d", card);

        snd_ctl_t* ctl = nullptr;
        if (snd_ctl_open(&ctl, ctl_name, 0) < 0)
            continue;

        snd_ctl_card_info_t* card_info;
        snd_ctl_card_info_alloca(&card_info);

        std::string card_name;
        if (snd_ctl_card_info(ctl, card_info) == 0) {
            const char* n = snd_ctl_card_info_get_name(card_info);
            if (n) card_name = n;
        }

        int dev = -1;
        while (snd_ctl_pcm_next_device(ctl, &dev) == 0 && dev >= 0) {
            snd_pcm_info_t* pcm_info;
            snd_pcm_info_alloca(&pcm_info);
            snd_pcm_info_set_device(pcm_info, (unsigned int)dev);
            snd_pcm_info_set_subdevice(pcm_info, 0);
            snd_pcm_info_set_stream(pcm_info, SND_PCM_STREAM_PLAYBACK);

            if (snd_ctl_pcm_info(ctl, pcm_info) < 0)
                continue;  // no playback on this device

            const char* dev_name_raw = snd_pcm_info_get_name(pcm_info);
            std::string dev_name = dev_name_raw ? dev_name_raw : "";

            char id[24];
            snprintf(id, sizeof(id), "hw:%d,%d", card, dev);

            std::string display = card_name;
            if (!dev_name.empty() && dev_name != card_name) {
                display += " - ";
                display += dev_name;
            }

            result += std::string(id) + "|" + display + "\n";
        }

        snd_ctl_close(ctl);
    }

    return result;
}
