// See respath.h.

#include "respath.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <string>

#ifndef JACKGRAPH_RESOURCE_DIR_DEFAULT
#error "JACKGRAPH_RESOURCE_DIR_DEFAULT must be defined by the build (the installed share dir)"
#endif

namespace jackgraph
{
namespace
{

bool hasFonts(const std::string &dir)
{
    if (dir.empty())
        return false;
    struct stat st;
    return stat((dir + "/fonts").c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// The directory the running executable sits in, or empty.
std::string exeDir()
{
    char buf[4096];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0)
        return std::string();
    buf[n] = '\0';
    std::string path(buf);
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

// The install-tree tail of the compiled-in resource dir -- normally "share/jack-graph".
//
// DERIVED, NOT SPELLED OUT, so that changing SHAREDIR in the Makefile
// carries the relocatable lookup below with it. Spelling it out here is exactly
// how the two would drift apart, which is the bug this whole path exists to prevent.
std::string installTail()
{
    const std::string def(JACKGRAPH_RESOURCE_DIR_DEFAULT);
    const size_t name = def.find_last_of('/');       // ".../share|/jack-graph"
    if (name == std::string::npos || name == 0)
        return std::string();
    const size_t datadir = def.find_last_of('/', name - 1); // "...|/share/jack-graph"
    if (datadir == std::string::npos)
        return std::string();
    return def.substr(datadir + 1);
}

// The resource dir implied by where this executable actually sits: bin/../share/jack-graph.
//
// THIS IS WHAT MAKES THE BINARY RELOCATABLE, and it is checked before the compiled-in prefix
// on purpose. The compiled-in path is only right when the program is installed under the
// prefix it happened to be configured with; this one is right for /usr (the .deb), /usr/local
// (a system install.sh) and ~/.local (a per-user one) alike, from a single build. Packaging
// that ships a prebuilt binary cannot satisfy the compiled-in path for an arbitrary prefix,
// so without this the GUI silently falls back to a system font whose metrics are not the ones
// the layout was audited against.
std::string relocatedDir()
{
    const std::string exe = exeDir();
    const std::string tail = installTail();
    if (exe.empty() || tail.empty())
        return std::string();
    const size_t slash = exe.find_last_of('/');      // strip the trailing "bin"
    if (slash == std::string::npos || slash == 0)
        return std::string();
    return exe.substr(0, slash) + "/" + tail;
}

std::string resolve()
{
    if (const char *env = getenv("JACKGRAPH_RESOURCE_DIR")) {
        const std::string dir(env);
        if (hasFonts(dir))
            return dir;
    }

    const std::string relocated = relocatedDir();
    if (hasFonts(relocated))
        return relocated;

    const std::string installed(JACKGRAPH_RESOURCE_DIR_DEFAULT);
    if (hasFonts(installed))
        return installed;

    const std::string exe = exeDir();
    if (!exe.empty()) {
        // A build tree: ./jack-graph next to ./resources, or tools/uirender next to ../resources.
        for (const char *rel : {"/resources", "/../resources"}) {
            const std::string dir = exe + rel;
            if (hasFonts(dir))
                return dir;
        }
    }

    return std::string();
}

} // namespace

const std::string &resourceDir()
{
    static const std::string dir = resolve();
    return dir;
}

} // namespace jackgraph
