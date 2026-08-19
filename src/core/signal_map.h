// signals.ini - a way to teach the plugin a new aeroplane without a new build.
//
// The built-in table in sim_state.cpp can only know the aeroplanes somebody has
// already opened. This file is how the person flying one that nobody has adds
// it: the probe in the log names the dataref that moved, and one line here binds
// it. Nothing else in the plugin has to change, and the same line can be sent
// back to be folded into the built-in table.
//
// Pure string work, no XPLM and no filesystem, so the bench can feed it the
// malformed files nobody writes by hand.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace xa::core {

// One line: which signal, which dataref, and what counts as "on".
struct SignalOverride {
    std::string signal;   // beacon, strobe, seatbelt - see signalIds()
    std::string dataref;
    // "On" as a threshold with a direction. The direction is not academic: the
    // FlightFactor 777 wires strobe, taxi and landing the other way up, so a
    // file that could only say "1 means on" would describe those three lights
    // exactly backwards.
    double on = 1.0;
    bool atMost = false;
};

struct SignalOverrides {
    // Section name - the aircraft ICAO as X-Plane reports it, or "*" for every
    // aeroplane - to the lines under it, in file order.
    std::map<std::string, std::vector<SignalOverride>> byAircraft;

    // The lines that apply to this aeroplane: its own section first, then "*".
    // Order is the search order, so a specific aeroplane always wins.
    std::vector<SignalOverride> forAircraft(const std::string& icao) const;

    bool empty() const { return byAircraft.empty(); }
    int count() const;
};

// The signal names this build understands, for the parser to check and the
// sample file to list.
const std::vector<std::string>& signalIds();

// Reads the file. Anything malformed is skipped and described in `problems`:
// a file that is half-understood must say which half, and one bad line must
// never cost the user the other twenty.
SignalOverrides parseSignalOverrides(const std::string& text,
                                     std::vector<std::string>* problems = nullptr);

// The commented sample written out when there is no file yet, so that the
// format is discoverable without the README.
std::string sampleSignalOverrides();

}  // namespace xa::core
