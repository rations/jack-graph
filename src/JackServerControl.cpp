#include "JackServerControl.hpp"
#include "platform/fs.h"

#include <jack/jack.h>
#include <alsa/asoundlib.h>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = jackgraph::fs;

namespace {

/* True if `pid` is a process named jackd that belongs to this user. That is
 * the whole test for adopting a PID file: a PID recorded last week may belong
 * to anything by now, and the old code would have SIGTERMed whatever it was. */
bool is_our_jackd(pid_t pid) {
    if (pid <= 0) return false;
    const std::string dir = "/proc/" + std::to_string(pid);
    struct stat st;
    if (stat(dir.c_str(), &st) != 0 || st.st_uid != getuid()) return false;
    std::string comm;
    if (!fs::readFile(dir + "/comm", comm)) return false;
    while (!comm.empty() && (comm.back() == '\n' || comm.back() == ' ')) comm.pop_back();
    return comm == "jackd";
}

/* Every descriptor above stderr, in the child between fork and exec. jackd
 * would otherwise inherit the X connection, the wake pipe and the ALSA
 * sequencer, and keep all three open for as long as it runs -- which is past
 * this program's exit. */
void close_inherited_fds() {
#ifdef SYS_close_range
    if (syscall(SYS_close_range, 3u, ~0u, 0u) == 0) return;
#endif
    long max = sysconf(_SC_OPEN_MAX);
    if (max < 0 || max > 65536) max = 65536;
    for (int fd = 3; fd < max; ++fd) close(fd);
}

bool contains_ci(const std::string& hay, const char* needle) {
    std::string h(hay), n(needle);
    for (auto& c : h) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (auto& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return h.find(n) != std::string::npos;
}

} // namespace

JackServerControl::JackServerControl() : m_child_pid(0), m_is_child(false) {
    m_state_dir = fs::runtimeDir() + "/jack-graph";
    m_pid_path = m_state_dir + "/jackd.pid";
    m_log_path = m_state_dir + "/jackd.log";

    std::string body;
    if (!fs::readFile(m_pid_path, body)) return;
    const pid_t pid = static_cast<pid_t>(std::strtol(body.c_str(), nullptr, 10));
    if (is_our_jackd(pid)) {
        m_child_pid = pid;
        m_is_child = false;
        fprintf(stderr, "jack-graph: adopted jackd PID %d from a previous session\n", pid);
    } else {
        fs::removeFile(m_pid_path);
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

void JackServerControl::write_pid_file() const {
    if (!fs::writeFileAtomic(m_pid_path, std::to_string(m_child_pid) + "\n"))
        fprintf(stderr, "jack-graph: could not record the jackd PID in %s\n", m_pid_path.c_str());
}

pid_t JackServerControl::spawn(const JackSettings& settings) {
    m_last_error.clear();

    // Build argv: jackd [-R] -d alsa [-d hw:X] -r rate -p period -n nperiods [-S] [-X seq|raw]
    // Everything after "-d alsa" is an ALSA driver option, which is why -S there
    // means 16-bit samples rather than the server's synchronous mode.
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
    if (settings.midi_driver == "seq" || settings.midi_driver == "raw") {
        args.push_back("-X");
        args.push_back(settings.midi_driver);
    }

    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);

    fprintf(stderr, "jack-graph: launching:");
    for (auto& a : args) fprintf(stderr, " %s", a.c_str());
    fprintf(stderr, "\n");

    // Everything the child needs is opened HERE, before the fork, so a failure
    // is reported to the user instead of vanishing inside a child that cannot
    // say anything. O_NOFOLLOW: the log's name is predictable, so it must never
    // be a symlink someone else planted.
    if (!fs::makeDirs(m_state_dir)) {
        m_last_error = "Cannot create " + m_state_dir + ": " + strerror(errno);
        return -1;
    }
    const int logfd = open(m_log_path.c_str(),
                           O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (logfd < 0) {
        m_last_error = "Cannot open " + m_log_path + ": " + strerror(errno);
        return -1;
    }
    const int nullfd = open("/dev/null", O_RDONLY | O_CLOEXEC);

    const pid_t pid = fork();
    if (pid < 0) {
        m_last_error = std::string("fork() failed: ") + strerror(errno);
        close(logfd);
        if (nullfd >= 0) close(nullfd);
        return -1;
    }

    if (pid == 0) {
        // The child. Only async-signal-safe calls from here to exec.
        //
        // Its own session, so a Ctrl-C in the terminal jack-graph was started
        // from does not reach jackd -- it is meant to outlive us.
        setsid();
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, nullptr);
        if (nullfd >= 0) dup2(nullfd, STDIN_FILENO);
        dup2(logfd, STDOUT_FILENO);
        dup2(logfd, STDERR_FILENO);
        close_inherited_fds();
        execvp("jackd", argv.data());
        const char* msg = "jack-graph: could not run jackd -- is it installed and on PATH?\n";
        ssize_t w = write(STDERR_FILENO, msg, strlen(msg));
        (void)w;
        _exit(127);
    }

    close(logfd);
    if (nullfd >= 0) close(nullfd);

    m_child_pid = pid;
    m_is_child = true;
    write_pid_file();
    return pid;
}

bool JackServerControl::owned_alive() const {
    if (m_child_pid <= 0) return false;
    // Our own child is reaped by the wake pipe, which calls child_exited() and
    // clears the PID, so while it is set the child has not been reaped. An
    // adopted one is init's to reap, so asking the kernel is the answer.
    if (m_is_child) return true;
    return kill(m_child_pid, 0) == 0 || errno == EPERM;
}

void JackServerControl::child_exited(int status) {
    if (WIFEXITED(status) && WEXITSTATUS(status) == 127) {
        m_last_error = "Could not run jackd. Is the jackd2 package installed?";
    } else {
        const bool signalled = WIFSIGNALED(status);
        const std::string tail = log_tail(!signalled);
        std::string how;
        if (signalled)
            how = std::string("jackd was killed by signal ") + std::to_string(WTERMSIG(status));
        else if (WIFEXITED(status))
            how = "jackd exited with status " + std::to_string(WEXITSTATUS(status));
        m_last_error = tail.empty() ? how + "." : how + ":\n" + tail;
    }
    forget_owned();
}

bool JackServerControl::request_stop() {
    if (m_child_pid <= 0) return false;
    fprintf(stderr, "jack-graph: stopping jackd (PID %d)\n", m_child_pid);
    return kill(m_child_pid, SIGTERM) == 0 || errno == ESRCH;
}

void JackServerControl::force_kill() {
    if (m_child_pid <= 0) return;
    fprintf(stderr, "jack-graph: jackd did not exit cleanly, sending SIGKILL\n");
    kill(m_child_pid, SIGKILL);
}

void JackServerControl::forget_owned() {
    m_child_pid = 0;
    m_is_child = false;
    fs::removeFile(m_pid_path);
}

/* jackd is verbose, and what it says about a failure is a few lines among
 * dozens of banners. The lines that carry the reason all say so in one of a
 * handful of words. The CAUSE comes first -- "the playback device is already in
 * use", and the process holding it -- and jackd's generic "Failed to open
 * server" last, so this keeps the first two such lines and the final one.
 *
 * With none of those, `fallback` decides: the last few lines for an exit status
 * (something went wrong and they are the best guess), nothing for a signal
 * (whoever sent it is the reason, and the log is only banners). */
std::string JackServerControl::log_tail(bool fallback) const {
    std::string body;
    if (!fs::readFile(m_log_path, body)) return std::string();

    std::vector<std::string> lines, telling;
    std::istringstream in(body);
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty()) continue;
        // jackd pads some sentences with double spaces ("applications  are using"),
        // which wraps as if there were a missing word.
        for (size_t d; (d = line.find("  ")) != std::string::npos;) line.erase(d, 1);
        // ALSA says this for every PCM without a control device -- "null",
        // "default" through a plugin -- on starts that then succeed.
        if (contains_ci(line, "Invalid CTL") || line.compare(0, 13, "control open ") == 0) continue;
        lines.push_back(line);
        for (const char* w : {"cannot", "can't", "fail", "error", "busy", "in use", "process id",
                              "unknown", "invalid", "not found", "no such", "could not",
                              "permission"}) {
            if (contains_ci(line, w)) {
                telling.push_back(line);
                break;
            }
        }
    }

    std::vector<std::string> pick;
    if (!telling.empty()) {
        for (size_t i = 0; i < telling.size() && i < 2; ++i) pick.push_back(telling[i]);
        if (telling.size() > 2) pick.push_back(telling.back());
    } else if (fallback) {
        const size_t n = lines.size() < 3 ? lines.size() : 3;
        pick.assign(lines.end() - static_cast<long>(n), lines.end());
    }

    std::string out;
    for (const std::string& l : pick) {
        if (!out.empty()) out += "\n";
        out += l;
    }
    return out;
}

std::string JackServerControl::migrate_device_id(const std::string& id) {
    if (id.compare(0, 3, "hw:") != 0) return id;
    const char* s = id.c_str() + 3;
    char* end = nullptr;
    const long card = std::strtol(s, &end, 10);
    if (end == s || card < 0) return id; // already hw:CARD=... or a name
    long dev = 0;
    if (*end == ',') {
        const char* d = end + 1;
        dev = std::strtol(d, &end, 10);
        if (end == d || dev < 0) return id;
    }
    if (*end != '\0') return id;

    char ctl_name[32];
    snprintf(ctl_name, sizeof(ctl_name), "hw:%ld", card);
    snd_ctl_t* ctl = nullptr;
    if (snd_ctl_open(&ctl, ctl_name, 0) < 0) return id;
    snd_ctl_card_info_t* info;
    snd_ctl_card_info_alloca(&info);
    std::string card_id;
    if (snd_ctl_card_info(ctl, info) == 0 && snd_ctl_card_info_get_id(info))
        card_id = snd_ctl_card_info_get_id(info);
    snd_ctl_close(ctl);
    if (card_id.empty()) return id;

    std::string out = "hw:CARD=" + card_id;
    if (dev != 0) out += ",DEV=" + std::to_string(dev);
    return out;
}

/* Enumerate playback-capable PCM devices through the ALSA control API.
 * Returns lines of the form "hw:CARD=id[,DEV=n]|Card Name - Device Name".
 *
 * The id half is deliberately CARD=/DEV= and never hw:0,0. It is saved to the
 * config and passed to jackd on the next Start, but card *numbers* move:
 * plugging in a USB interface can make it card 0 and push the internal card to
 * 1, and a numeric id would then silently point jackd at the wrong card. Card
 * ids are stable. migrate_device_id() converts a 1.x config's numeric id. */
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

        std::string card_id, card_name;
        if (snd_ctl_card_info(ctl, card_info) == 0) {
            const char* i = snd_ctl_card_info_get_id(card_info);
            const char* n = snd_ctl_card_info_get_name(card_info);
            if (i) card_id = i;
            if (n) card_name = n;
        }
        /* No id means no stable name to persist; skip rather than fall back to
         * a card number that will not survive a replug. */
        if (card_id.empty()) {
            snd_ctl_close(ctl);
            continue;
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

            /* The id, not the name. On an HDA/SOF card snd_pcm_info_get_name()
             * is empty for every PCM, so all six devices would render as the
             * bare card name and the dropdown would be no more use than the
             * hw:0,0 list it replaces. The id carries "HDA Analog", "HDMI1",
             * "HDMI2"... -- it is what `aplay -l` prints for the same reason. */
            const char* dev_id_raw = snd_pcm_info_get_id(pcm_info);
            const char* dev_name_raw = snd_pcm_info_get_name(pcm_info);
            std::string dev_name = dev_id_raw && *dev_id_raw ? dev_id_raw
                                 : (dev_name_raw ? dev_name_raw : "");

            /* DEV=0 is left off: "hw:CARD=x" already means device 0 to ALSA,
             * and it is the shorter form a user is likelier to have typed or
             * seen elsewhere. Devices past 0 (HDMI PCMs) need the suffix. */
            std::string id = "hw:CARD=" + card_id;
            if (dev != 0) id += ",DEV=" + std::to_string(dev);

            std::string display = card_name.empty() ? card_id : card_name;
            if (!dev_name.empty() && dev_name != card_name) {
                display += " - ";
                display += dev_name;
            }

            result += id + "|" + display + "\n";
        }

        snd_ctl_close(ctl);
    }

    return result;
}
