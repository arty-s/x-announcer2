// Everything the user is allowed to change, and the ini file it lives in.
//
// Parsing and rendering are pure string work on purpose: no filesystem, no
// XPLM. That keeps them inside xa_core, where the offline bench can hammer them
// with malformed files - which is exactly the input nobody tests by hand.
//
// The key names, their order and their meaning are 1.x's, so a user who knows
// one file knows the other. Two deliberate exceptions are documented at the
// audio bus fields below.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "core/config.h"

namespace xa::core {

struct Settings {
    // Where the packs are. Empty means "look in the usual places", which is
    // what autoFind then does.
    std::string library;
    std::string language = "en-us";
    bool autoFind = true;

    std::string airlineMode = "auto";  // auto | manual
    std::string airlineManual = "Default";

    // 1.x names its buses after FlyWithLua's mixer (interior|master|ui|com1).
    // The XPLM has no master bus, so v2 names them after the SDK enum instead:
    // interior | exterior | ui | com1 | com2 | ground. Music therefore defaults
    // to `exterior`, which is the bus it already played on before the file
    // existed - the default must never change what the user hears.
    std::string announceBus = "interior";
    std::string musicBus = "exterior";

    double volume = 0.85;
    double musicVolume = 0.35;
    double duck = 0.25;

    // What the state machine consults. Kept as 1.x's Config so the engine takes
    // it unchanged and the bench keeps comparing like with like.
    Config flight;

    std::string seatbeltDref;  // empty = find it automatically
    double windowScale = 1.0;

    // Was the panel open when X-Plane last shut down. v2-only: 1.x had no such
    // key because FlyWithLua reopened nothing by itself. Until this existed the
    // window forced itself open on every load - fine while the question was
    // "did the plugin start at all", rude once the answer was always yes.
    bool panelOpen = false;

    // Keys this build does not know: SimBrief and the widget are still to be
    // ported, and a file may also have been written by a newer version. They
    // are carried through a rewrite verbatim, because silently dropping a
    // setting the user typed is worse than not honouring it yet.
    std::map<std::string, std::string> unknown;

    bool autoAirline() const { return airlineMode != "manual"; }
};

// Reads ini text. Anything malformed keeps its default and is described in
// `problems` - a settings file must never fail silently, and it must never
// refuse to load either: one bad line cannot be allowed to cost the user every
// other setting.
Settings parseSettings(const std::string& text, std::vector<std::string>* problems = nullptr);

// Renders the whole file, comments and all, in 1.x's key order. Unknown keys
// follow at the end under their own heading.
std::string writeSettings(const Settings& settings);

// The buses this build accepts, for the panel to offer and the parser to check.
const std::vector<std::string>& audioBusNames();

}  // namespace xa::core
