#include "JackServerControl.hpp"
#include <jack/jack.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cerrno>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

static const char* PID_FILE = "/tmp/jack-graph-jackd.pid";

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

    // Build argv as vector<string> to keep strings alive across execvp
    std::vector<std::string> args;
    args.push_back("jackd");
    if (settings.realtime)    args.push_back("-R");
    if (settings.synchronous) args.push_back("-S");
    if (settings.midi_driver == "seq") {
        args.push_back("-X");
        args.push_back("seq");
    }
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

    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);

    fprintf(stderr, "jack-graph: launching:");
    for (auto& a : args) fprintf(stderr, " %s", a.c_str());
    fprintf(stderr, "\n");

    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "jack-graph: fork() failed: %s\n", strerror(errno));
        return false;
    }

    if (pid == 0) {
        // Child: exec jackd (stderr stays on terminal for debugging)
        execvp("jackd", argv.data());
        fprintf(stderr, "jack-graph: execvp(jackd) failed: %s\n", strerror(errno));
        _exit(127);
    }

    // Parent: store PID
    m_child_pid = pid;
    std::ofstream pf(PID_FILE);
    if (pf.is_open()) {
        pf << pid << "\n";
        pf.close();
    }

    // Poll until jackd is ready (max 5 seconds)
    for (int i = 0; i < 10; i++) {
        usleep(500000);
        if (is_running()) return true;
    }

    // Timeout: jackd failed to start — clean up
    fprintf(stderr, "jack-graph: jackd did not start within 5 seconds\n");
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

    // Already dead?
    if (kill(m_child_pid, 0) == -1 && errno == ESRCH) {
        m_child_pid = 0;
        unlink(PID_FILE);
        return true;
    }

    fprintf(stderr, "jack-graph: stopping jackd (PID %d)\n", m_child_pid);
    kill(m_child_pid, SIGTERM);

    // Poll waitpid up to 10 seconds
    for (int i = 0; i < 20; i++) {
        usleep(500000);
        int status;
        pid_t result = waitpid(m_child_pid, &status, WNOHANG);
        if (result == m_child_pid || (result == -1 && errno == ECHILD))
            break;
    }

    // Force-kill if still alive
    if (kill(m_child_pid, 0) == 0) {
        fprintf(stderr, "jack-graph: jackd did not exit cleanly, sending SIGKILL\n");
        kill(m_child_pid, SIGKILL);
        waitpid(m_child_pid, nullptr, 0);
    }

    m_child_pid = 0;
    unlink(PID_FILE);
    return !is_running();
}

std::string JackServerControl::list_audio_devices() const {
    std::string result;

    // Parse aplay -l into "hw:X,Y|Card Name - Device Name" lines
    const char* cmd =
        "aplay -l 2>/dev/null | awk '"
        "/^card [0-9]+:/ {"
        "  line = $0;"
        "  match(line, /card ([0-9]+):/, a); card = a[1];"
        "  match(line, /device ([0-9]+):/, b); dev = b[1];"
        "  cnt = 0; rest = line;"
        "  while (match(rest, /\\[[^]]+\\]/)) {"
        "    cnt++;"
        "    bracket = substr(rest, RSTART+1, RLENGTH-2);"
        "    if (cnt==1) cname = bracket;"
        "    if (cnt==2) dname = bracket;"
        "    rest = substr(rest, RSTART+RLENGTH);"
        "  }"
        "  if (cnt >= 2) printf \"hw:%s,%s|%s - %s\\n\", card, dev, cname, dname;"
        "}'";

    FILE* fp = popen(cmd, "r");
    if (!fp) return result;

    char buf[512];
    while (fgets(buf, sizeof(buf), fp)) {
        std::string line(buf);
        line.erase(line.find_last_not_of(" \n\r\t") + 1);
        if (!line.empty()) result += line + "\n";
    }
    pclose(fp);
    return result;
}
