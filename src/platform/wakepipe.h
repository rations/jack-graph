// ONE SELF-PIPE, SO THAT SOMETHING HAPPENING ELSEWHERE CAN BE HANDLED HERE, ON THE MAIN LOOP.
//
// Two kinds of "elsewhere", and that there is one pipe for both is the point of the file:
//
//   * A SIGNAL. jack-graph starts jackd and needs to know the moment it dies: a jackd that cannot
//     open its device exits within a few hundred milliseconds, and its exit is what turns Start's
//     "Starting..." into jackd's own explanation. It is also what reaps it, so a jackd that exits
//     never lingers as a zombie. Replaces g_child_watch_add.
//
//   * ANOTHER THREAD. jack-graph's JACK client is called back on JACK's own threads -- a port
//     registered, an xrun, the server gone -- and none of those handlers may touch the client, a
//     panel or anything else. Replaces Glib::Dispatcher. See post() below.
//
// THE MECHANISM IS A SELF-PIPE, for both. A SIGCHLD handler, or another thread, writes one byte to
// a pipe; the pipe's read end is registered with X11Window::addFd; the handler on the main thread
// calls drain(), which reaps, dispatches and runs the post handler. This is what simple-login-gui
// already does for its session child, and it is the standard answer: almost nothing is legal
// inside a signal handler, but write() to a pipe is async-signal-safe, and it turns a signal --
// or a thread with something to say -- into a file descriptor the same select() is already
// waiting on. It is also why the JACK hop costs no second descriptor and no second mechanism.
//
// THE NAME IS THE PIPE, NOT EITHER JOB, deliberately. This was `childreaper` while reaping was all
// it did; it then grew the thread hop, and the name went on describing the less interesting half.
//
//------------------------------------------------------------------------------------------------
// THE ONE DESIGN DECISION THAT MATTERS: THIS ONLY EVER REAPS PIDS IT WAS ASKED TO WATCH.
//
// The obvious implementation of drain() is `while (waitpid(-1, &st, WNOHANG) > 0)`, and it is
// wrong in general: any code that forks a child and waits for it SYNCHRONOUSLY would sometimes
// find its child already reaped here, and its own waitpid would then fail with ECHILD and report a
// failure for a command that worked. It is a race, so it would not fail every time, which is worse.
// (jack-bridge, where this file comes from, runs such commands; jack-graph today does not, and the
// rule costs nothing to keep.)
//
// So drain() loops over the WATCH LIST and calls waitpid(pid, &st, WNOHANG) for each. A child
// nobody registered is nobody's business here.
//
// The pipe is also why this is not just SIGCHLD with SA_RESTART: the point is not to survive the
// signal, it is to handle the exit on the main thread, at a defined moment, where a callback may
// safely touch the panel and repaint.
//------------------------------------------------------------------------------------------------

#pragma once

#include <sys/types.h>

#include <functional>

namespace jackgraph
{
namespace wakepipe
{

// Installs the SIGCHLD handler and creates the self-pipe. Idempotent; returns false having warned
// if the pipe could not be created, in which case watch() still records callbacks and drain() can
// still be called from a timer -- a degradation, not a failure.
//
// Uses sigaction with SA_RESTART and SA_NOCLDSTOP: SA_RESTART because an unrestarted read() on the
// ALSA mixer's descriptors would start returning EINTR for no reason the caller can act on, and
// SA_NOCLDSTOP because a child that was merely stopped has not exited and waking the loop for it
// is noise.
bool install();

// The read end of the self-pipe, for X11Window::addFd, or -1 if install() failed.
int readFd();

// Call `onExit` on the main loop when `pid` exits, with the raw wait status. Replaces one
// g_child_watch_add. Watching a pid twice replaces the callback rather than adding a second.
void watch(pid_t pid, std::function<void(int status)> onExit);

// Stop watching, without waiting. For a child that has been handed to somebody else, or whose
// callback would outlive the object it captured -- which is the case this exists for: a panel that
// is being torn down must forget its children before its callbacks dangle.
void forget(pid_t pid);

// Drain the pipe, reap, and run the post handler if anything posted. Register this with
// X11Window::addFd(readFd(), drain).
//
// Reads until the pipe is empty, because several children can exit between two passes and SIGCHLD
// is not queued -- one byte may stand for any number of exits, and several may stand for one.
// Neither the count nor the content of the bytes means anything; what the pipe carries is "look
// again". So this then walks the watch list, and the number of waitpid calls is the number of
// children we care about rather than the number of bytes.
void drain();

// Turn one byte of the pipe into a callback on the main loop, with no child involved.
//
// THE ONE FUNCTION IN THIS FILE THAT MAY BE CALLED FROM ANOTHER THREAD, and the only thing any of
// jack-graph's JACK callbacks are allowed to do. JACK calls back on its own threads -- a port
// registered, an xrun, and the shutdown registered with jack_on_shutdown -- and JackClient's
// header is explicit that such a handler must not touch the client or any widget and must marshal
// to the main loop first. The GTK build used Glib::Dispatcher, which is a self-pipe with a C++
// wrapper on it; this is the same self-pipe, already in the same select().
//
// It carries NO PAYLOAD, on purpose: a caller with more than one thing to report sets its own
// atomic flags first and reads them in the post handler, which is what jack-graph's App does for
// its three callbacks. One byte cannot say which, and a queue here would be a second mechanism.
//
// ASYNC-SIGNAL-SAFE AND THREAD-SAFE BY THE SAME ARGUMENT: it sets one atomic and write()s one
// byte. The handler runs on the main thread, from drain().
void post();

// The callback post() will run, on the main loop. Set once at start-up; pass nullptr at shutdown,
// before anything the handler captured is destroyed.
void setPostHandler(std::function<void()> fn);

} // namespace wakepipe
} // namespace jackgraph
