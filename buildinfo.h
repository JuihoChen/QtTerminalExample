#ifndef BUILDINFO_H
#define BUILDINFO_H

// Filled in at build time by buildinfo.cmake, so the About dialog always
// shows the build that is actually running
namespace BuildInfo {
    extern const char* const gitCommit;   // short hash, empty outside a git checkout
    extern const char* const buildTime;   // "YYYY-MM-DD HH:MM", local time
}

#endif // BUILDINFO_H
