// Filesystem and path helpers.
//
// THE ATOMIC WRITE IS THE LOAD-BEARING ONE. The config used to be written with a plain
// std::ofstream, which truncates first: a crash or a full disc between the truncate and the write
// left an empty config and every setting back at its default.

#pragma once

#include <string>

namespace jackgraph
{
namespace fs
{

bool exists(const std::string &path);

// Whole-file read. Returns false if the file could not be opened.
bool readFile(const std::string &path, std::string &out);

// Write via a temporary in the same directory, fsync, then rename over the target. The rename is
// what makes a reader see either the old contents or the new ones and never a partial file.
// Creates parent directories as needed. Returns false having left the target untouched.
bool writeFileAtomic(const std::string &path, const std::string &body);

bool removeFile(const std::string &path);

// mkdir -p. Returns true if the directory exists afterwards.
bool makeDirs(const std::string &path);

// $HOME, or the passwd entry if it is unset.
const std::string &homeDir();

// $XDG_CONFIG_HOME, else ~/.config. The config file lives at $configHome/jack-graph/config.
const std::string &configHome();

// $XDG_RUNTIME_DIR, else $XDG_CACHE_HOME, else ~/.cache. For state that should not outlive the
// session: the PID and log of the jackd this program started.
const std::string &runtimeDir();

} // namespace fs
} // namespace jackgraph
