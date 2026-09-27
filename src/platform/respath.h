// Finding this program's own resources (currently: the two fonts) at run time.
//
// Resolution order:
//   1. $JACKGRAPH_RESOURCE_DIR, if set -- the development and packaging override;
//   2. bin/../share/jack-graph relative to the running executable, so one prebuilt binary finds
//      its fonts under /usr (the .deb), /usr/local (install.sh) or any other prefix;
//   3. the compile-time install prefix, JACKGRAPH_RESOURCE_DIR_DEFAULT;
//   4. "resources" beside the executable, which is what a build tree looks like.
//
// Returns an empty string if none of those contains a fonts directory. Callers treat that as
// "no bundled fonts", which FontStack already degrades to a system toy face for.
//
// THE ENVIRONMENT OVERRIDE EXISTS ONLY IN THIS BINARY. A substituted font is a FreeType attack
// surface, so a resource path taken from the environment is only acceptable in a process that has
// no privilege to lose. jack-graph runs entirely unprivileged, and the jackd it starts links no
// font code at all, so this override reaches nothing that could be hurt by it.

#pragma once

#include <string>

namespace jackgraph
{

// Cached after the first call.
const std::string &resourceDir();

} // namespace jackgraph
