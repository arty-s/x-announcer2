// What the core wants done, as data rather than as a side effect.
//
// The engine never calls XPLM, never touches FMOD and never opens a file: it
// appends intents, and the plugin (or the bench) carries them out. That is what
// makes a whole flight replayable and diffable.
#pragma once

#include <string>
#include <vector>

namespace xa::core {

struct Intent {
    enum class Kind {
        PlayAnnouncement,  // event, detail = reason
        StopAnnouncement,
        StartMusic,
        StopMusic,
        PhaseChanged,      // event = phase id, detail = reason
        FlightReset,       // detail = reason
        Note,              // detail = human-readable line for the log
    };

    Kind kind = Kind::Note;
    std::string event;
    std::string detail;
    double simClock = 0.0;
};

const char* intentKindName(Intent::Kind kind);

// One canonical text line per intent. Both the C++ bench and the Lua bench emit
// this format, so a port that drifts shows up as a plain diff.
std::string formatIntent(const Intent& intent);

}  // namespace xa::core
