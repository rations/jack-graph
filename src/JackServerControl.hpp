#pragma once

#include <string>
#include <sys/types.h>

struct JackSettings {
    std::string interface;
    int sample_rate;
    int frames_per_period;
    int periods_per_buffer;
    bool realtime;
    bool synchronous; /* the ALSA driver's -S: 16-bit samples ("shorts") */
    std::string midi_driver;
};

/* Starting and stopping the jackd this program owns.
 *
 * NOTHING HERE WAITS. The GTK build's start() slept in a loop for up to five
 * seconds waiting for the server, and stop() for up to ten waiting for it to
 * exit, with the whole window frozen. Now start is spawn() plus a readiness poll
 * and stop is request_stop() plus an exit watch, and App drives both from the
 * main loop's timers and the wake pipe -- see App::startServer / stopServer.
 *
 * jackd OUTLIVES THIS PROGRAM on purpose: closing the graph must not take the
 * audio down. Its PID is recorded so the next session can adopt it and still
 * offer Stop, and the adoption is checked (the process must be a jackd owned by
 * this user) so a recycled PID can never be SIGTERMed by mistake. */
class JackServerControl {
public:
    JackServerControl();
    ~JackServerControl();

    /* Whether a JACK server answers. Opens and closes a client named
     * "status_check" to find out -- so never call it while this program has a
     * JACK client of its own, which would see that registration and refresh
     * the graph in response. App::updateStatus() explains the loop that made. */
    bool is_running() const;
    std::string get_status() const;

    /* Fork and exec jackd with these settings. Returns its pid, or -1 with
     * last_error() set if it could not be started at all. Readiness is the
     * caller's to poll with is_running(); an early exit arrives through the
     * wake pipe and is reported with child_exited(). */
    pid_t spawn(const JackSettings& settings);

    /* The jackd this program started, in this session or an earlier one, or 0.
     * A server started any other way -- a system service, qjackctl, a shell --
     * is not ours to stop. */
    pid_t owned_pid() const { return m_child_pid; }
    /* True if owned_pid() is our own child, which the wake pipe reaps. False
     * for one adopted from an earlier session, which init reaps and we poll. */
    bool owned_is_child() const { return m_is_child; }
    /* Whether the owned jackd still exists. */
    bool owned_alive() const;

    /* Our child exited; `status` is the raw wait status. Records why, from the
     * status and the log, and forgets the PID. */
    void child_exited(int status);

    /* SIGTERM to the owned jackd. False if there is none. */
    bool request_stop();
    /* SIGKILL, for a jackd that ignored the SIGTERM. */
    void force_kill();
    /* The owned jackd is gone: drop the PID and its file. */
    void forget_owned();

    /* Why the last start failed, in a form fit to show: the lines of jackd's
     * own output that say what went wrong. */
    std::string last_error() const { return m_last_error; }
    void set_last_error(const std::string& e) { m_last_error = e; }
    /* The lines of the jackd log that say why it failed. With none, the last
     * few lines if `fallback`, else nothing. */
    std::string log_tail(bool fallback = true) const;
    const std::string& log_path() const { return m_log_path; }

    /* Playback devices as "id|display name" lines, ids in the stable
     * hw:CARD=<id>[,DEV=n] form. */
    std::string list_audio_devices() const;

    /* An interface id saved by jack-graph 1.x, "hw:N,M" or "hw:N", in the
     * hw:CARD= form -- or the input unchanged if it is not that shape or card N
     * does not exist. */
    static std::string migrate_device_id(const std::string& id);

private:
    void write_pid_file() const;

    pid_t m_child_pid;
    bool m_is_child;
    std::string m_state_dir;
    std::string m_pid_path;
    std::string m_log_path;
    std::string m_last_error;
};
