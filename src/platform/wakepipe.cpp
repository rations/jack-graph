// See wakepipe.h.

#include "wakepipe.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

namespace jackgraph
{
namespace wakepipe
{

namespace
{

struct Watch {
    pid_t pid;
    std::function<void(int status)> onExit;
};

// File scope rather than a class: there is one SIGCHLD disposition per process, so an object that
// could be constructed twice would be lying about what it owns.
int gPipe[2] = {-1, -1};
std::vector<Watch> gWatches;
std::function<void()> gPostHandler;
bool gInstalled = false;

// Set by post(), cleared by drain(). A plain bool would be a data race: the writer
// is another thread (JACK's) and the reader is the main loop. It is the flag and not the pipe byte
// that distinguishes a post from a SIGCHLD, because the two bytes are indistinguishable by the time
// they arrive and because running the post handler on every child exit would be a second thing to
// reason about.
std::atomic<bool> gPosted{false};

// THE ONLY THING THE SIGNAL HANDLER DOES. write() is async-signal-safe; nothing else here would
// be.
// errno is saved and restored because the interrupted code may be about to read it, and a failed
// write inside a handler that clobbered errno turns into a spurious error somewhere unrelated.
extern "C" void onSigchld(int)
{
    const int saved = errno;
    const char byte = 'c';
    ssize_t n;
    do {
        n = write(gPipe[1], &byte, 1);
    } while (n < 0 && errno == EINTR);
    // A full pipe means the main loop has not drained yet, which is fine: one byte already waiting
    // is enough to make it drain, and drain() reaps from the watch list rather than counting bytes.
    errno = saved;
}

} // namespace

//------------------------------------------------------------------------
bool install()
{
    if (gInstalled)
        return gPipe[0] >= 0;
    gInstalled = true;

    // O_CLOEXEC: a child must not inherit either end. Inheriting the write end would keep the pipe
    // open past the child's exit, which is harmless, but inheriting the read end in a
    // double-forked bridge that outlives the GUI is a descriptor leak that lasts as long as the
    // audio does.
    //
    // O_NONBLOCK on the read end so drain() can read until empty without blocking on the last
    // read; on the write end so the handler cannot block inside a signal handler if the pipe
    // filled up.
    if (pipe2(gPipe, O_CLOEXEC | O_NONBLOCK) != 0) {
        fprintf(stderr, "jack-graph: cannot create the wake pipe (%s); child processes and "
                        "cross-thread posts will not be noticed\n",
                strerror(errno));
        gPipe[0] = gPipe[1] = -1;
        return false;
    }

    struct sigaction sa = {};
    sa.sa_handler = onSigchld;
    sigemptyset(&sa.sa_mask);
    // SA_RESTART: the ALSA mixer's poll descriptors and the D-Bus socket are read from the main
    // loop, and an EINTR there is a failure the caller cannot do anything useful with.
    // SA_NOCLDSTOP: a stopped child has not exited, and waking the loop for it is noise.
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, nullptr) != 0) {
        fprintf(stderr, "jack-graph: cannot install the SIGCHLD handler (%s)\n", strerror(errno));
        return false;
    }
    return true;
}

int readFd()
{
    return gPipe[0];
}

//------------------------------------------------------------------------
void watch(pid_t pid, std::function<void(int status)> onExit)
{
    if (pid <= 0)
        return;
    for (Watch &w : gWatches) {
        if (w.pid == pid) {
            w.onExit = std::move(onExit);
            return;
        }
    }
    gWatches.push_back({pid, std::move(onExit)});
}

void forget(pid_t pid)
{
    for (size_t i = 0; i < gWatches.size(); ++i) {
        if (gWatches[i].pid == pid) {
            gWatches.erase(gWatches.begin() + static_cast<long>(i));
            return;
        }
    }
}

//------------------------------------------------------------------------
void drain()
{
    // Empty the pipe first. SIGCHLD is not queued, so one byte may stand for any number of exits
    // and several bytes may stand for one; neither the count nor the content means anything. What
    // matters is that the descriptor stops being readable, or select() would wake immediately for
    // ever.
    if (gPipe[0] >= 0) {
        char buf[64];
        ssize_t n;
        do {
            n = read(gPipe[0], buf, sizeof(buf));
        } while (n > 0 || (n < 0 && errno == EINTR));
    }

    // Then reap, FROM THE WATCH LIST ONLY. See the header: waitpid(-1, ...) here would race
    // a synchronous waitpid elsewhere and steal its child's status.
    //
    // Collected before dispatching, because a callback may watch a new child or forget an old one
    // and must not be doing that to the vector being walked. Firing the Steam toggle's callback,
    // for one, re-reads the JACK graph and can start another child.
    std::vector<std::pair<pid_t, int>> exited;
    for (const Watch &w : gWatches) {
        int status = 0;
        const pid_t r = waitpid(w.pid, &status, WNOHANG);
        if (r == w.pid)
            exited.emplace_back(w.pid, status);
        else if (r < 0 && errno == ECHILD)
            exited.emplace_back(w.pid, -1); // already gone: dispatch so the UI does not hang on it
    }

    for (const auto &e : exited) {
        std::function<void(int)> fn;
        for (const Watch &w : gWatches) {
            if (w.pid == e.first) {
                fn = w.onExit;
                break;
            }
        }
        forget(e.first);
        if (fn)
            fn(e.second);
    }

    // A post from another thread. Taken with exchange() so a second post arriving while the
    // handler runs is not lost: it sets the flag again and the next pass round the loop runs the
    // handler again.
    if (gPosted.exchange(false) && gPostHandler)
        gPostHandler();
}

//------------------------------------------------------------------------
void post()
{
    if (gPipe[1] < 0)
        return;
    gPosted.store(true);
    const char byte = 'p';
    ssize_t n;
    do {
        n = write(gPipe[1], &byte, 1);
    } while (n < 0 && errno == EINTR);
    (void)n;
}

void setPostHandler(std::function<void()> fn)
{
    gPostHandler = std::move(fn);
}

} // namespace wakepipe
} // namespace jackgraph
