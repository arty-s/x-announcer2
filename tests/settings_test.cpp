// The settings file, hammered with the input nobody types by hand: values in
// the wrong units, booleans spelled the wrong way, keys from a future version.
//
// The file is the one part of the plugin a user edits directly, so every way it
// can be wrong is a way the plugin can go quiet for a reason that never reaches
// the log. That is what these checks are for.
#include "settings_test.h"

#include <iostream>
#include <string>
#include <vector>

#include "core/settings.h"

namespace xa::test {
namespace {

int* g_checks = nullptr;
int* g_failed = nullptr;

void check(bool condition, const std::string& what) {
    ++*g_checks;
    if (condition) {
        std::cout << "   PASS " << what << "\n";
    } else {
        ++*g_failed;
        std::cout << "   FAIL " << what << "\n";
    }
}

bool mentions(const std::vector<std::string>& problems, const std::string& needle) {
    for (const std::string& problem : problems) {
        if (problem.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// A file written by 1.x, with every value moved off its default so that a key
// silently ignored shows up as a default rather than blending in. The widget
// and SimBrief keys are the ones v2 does not implement yet.
const char* const kFileFrom1x = R"(# Настройки X-Announcer для X-Plane 12.

library = D:\UA_Sounds
language = ru
airline_mode = manual
airline_manual = DLH
announce_bus = ui
music_bus = com1
volume = 0.5
music_volume = 0.1
duck = 0.9
enabled = false
boarding_music = false
cabin_noise = true
auto_boarding = false
boarding_repeat = 120
pilot_welcome = true
door_calls = false
night_dim = false
landing_reaction = false
seatbelt_dref = my/own/seatbelt
window_scale = 1.5
auto_find = false
music_max_loops = 3
simbrief_id = 1167459
widget = true
widget_mode = full
widget_opacity = 0.55
widget_x = 20
widget_y = 60
)";

}  // namespace

void runSettingsChecks(int* checks, int* failed) {
    g_checks = checks;
    g_failed = failed;
    std::cout << "-- settings file\n";

    // Defaults must survive a round trip: whatever the plugin writes on first
    // run has to read back as exactly what it meant.
    {
        const core::Settings defaults;
        std::vector<std::string> problems;
        const core::Settings again = core::parseSettings(core::writeSettings(defaults), &problems);
        check(problems.empty(), "the file we write has nothing to complain about");
        check(again.library == defaults.library && again.language == defaults.language &&
                  again.airlineMode == defaults.airlineMode &&
                  again.airlineManual == defaults.airlineManual &&
                  again.announceBus == defaults.announceBus &&
                  again.musicBus == defaults.musicBus && again.volume == defaults.volume &&
                  again.musicVolume == defaults.musicVolume && again.duck == defaults.duck &&
                  again.defaultFallback == defaults.defaultFallback &&
                  again.seatbeltDref == defaults.seatbeltDref &&
                  again.windowScale == defaults.windowScale &&
                  again.panelOpen == defaults.panelOpen,
              "defaults survive write then read");
        check(again.flight.enabled == defaults.flight.enabled &&
                  again.flight.boardingMusic == defaults.flight.boardingMusic &&
                  again.flight.cabinNoise == defaults.flight.cabinNoise &&
                  again.flight.autoBoarding == defaults.flight.autoBoarding &&
                  again.flight.pilotWelcome == defaults.flight.pilotWelcome &&
                  again.flight.doorCalls == defaults.flight.doorCalls &&
                  again.flight.nightDim == defaults.flight.nightDim &&
                  again.flight.landingReaction == defaults.flight.landingReaction &&
                  again.flight.boardingRepeat == defaults.flight.boardingRepeat,
              "flight settings survive write then read");
    }

    // Defaults are 1.x's, value for value. The scenarios are flown through both
    // implementations, so a default that drifted here would either fail the
    // comparison or - worse - be adopted by it as the new truth.
    {
        const core::Settings s;
        check(s.language == "en-us" && s.airlineMode == "auto" && s.airlineManual == "Default",
              "library defaults match 1.x");
        check(s.library.empty(), "the sound folder defaults to the standard one, not a guess");
        // v2-only, and it starts as 1.x behaved: a new switch must never change
        // what somebody already hears, only offer to.
        check(s.defaultFallback, "gaps are filled from Default until told otherwise");
        check(s.volume == 0.85 && s.musicVolume == 0.35 && s.duck == 0.25 && s.windowScale == 1.0,
              "volume defaults match 1.x");
        check(!s.panelOpen, "the panel does not open itself on a fresh install");
        check(s.flight.enabled && s.flight.boardingMusic && !s.flight.cabinNoise &&
                  s.flight.autoBoarding && !s.flight.pilotWelcome && s.flight.doorCalls &&
                  s.flight.nightDim && s.flight.landingReaction &&
                  s.flight.boardingRepeat == 300.0,
              "flight defaults match 1.x");
    }

    // A whole file from 1.x, read key by key.
    {
        std::vector<std::string> problems;
        const core::Settings s = core::parseSettings(kFileFrom1x, &problems);
        // Two notes, and only two: auto_find and music_max_loops no longer
        // exist. Everything else in a 1.x file still means what it meant.
        check(problems.size() == 2 && mentions(problems, "auto_find") &&
                  mentions(problems, "music_max_loops"),
              "a 1.x file reads with notes about exactly the two dropped keys");
        check(s.library == R"(D:\UA_Sounds)" && s.language == "ru",
              "library and language come across");
        // A 1.x file cannot mention it, so reading one must leave it alone.
        check(s.defaultFallback, "a file written by 1.x leaves the Default stand-in switched on");
        // Neither may survive as an unknown key either, or a dead switch would
        // sit in the file looking like it still does something.
        check(s.unknown.count("auto_find") == 0 && s.unknown.count("music_max_loops") == 0,
              "and the dropped keys do not linger in the file");
        check(s.airlineMode == "manual" && s.airlineManual == "DLH" && !s.autoAirline(),
              "manual airline mode comes across");
        check(s.announceBus == "ui" && s.musicBus == "com1", "bus names come across");
        check(s.volume == 0.5 && s.musicVolume == 0.1 && s.duck == 0.9, "volumes come across");
        check(!s.flight.enabled && !s.flight.boardingMusic && s.flight.cabinNoise &&
                  !s.flight.autoBoarding && s.flight.pilotWelcome && !s.flight.doorCalls &&
                  !s.flight.nightDim && !s.flight.landingReaction,
              "every flight switch comes across");
        check(s.flight.boardingRepeat == 120.0, "boarding_repeat comes across");
        check(s.seatbeltDref == "my/own/seatbelt" && s.windowScale == 1.5,
              "seatbelt_dref and window_scale come across");

        // The keys v2 has no code for yet must not evaporate.
        check(s.unknown.count("simbrief_id") == 1 && s.unknown.at("simbrief_id") == "1167459" &&
                  s.unknown.count("widget_mode") == 1 && s.unknown.size() == 6,
              "SimBrief and widget keys are kept aside, not dropped");
        const core::Settings again = core::parseSettings(core::writeSettings(s));
        check(again.unknown == s.unknown, "and they survive a rewrite");
        check(again.airlineMode == "manual" && again.announceBus == "ui" && again.volume == 0.5 &&
                  again.flight.boardingRepeat == 120.0 &&
                  again.seatbeltDref == "my/own/seatbelt",
              "as does everything else in the file");
    }

    // Booleans. 1.x accepted only "true"/"1" and read everything else as false,
    // so `enabled = True` muted the plugin with no way to tell why.
    {
        std::vector<std::string> problems;
        const core::Settings s =
            core::parseSettings("enabled = True\ndoor_calls = OFF\nnight_dim = Yes\n", &problems);
        check(s.flight.enabled && !s.flight.doorCalls && s.flight.nightDim,
              "True / OFF / Yes are understood whatever the case");
        check(problems.empty(), "and they raise nothing");
    }
    {
        std::vector<std::string> problems;
        const core::Settings s = core::parseSettings("enabled = maybe\n", &problems);
        check(s.flight.enabled, "a boolean nobody can parse keeps its default");
        check(mentions(problems, "enabled"), "and it is reported, not swallowed");
    }

    // Numbers.
    {
        std::vector<std::string> problems;
        const core::Settings s =
            core::parseSettings("volume = 5\nduck = -1\nwindow_scale = 99\n", &problems);
        check(s.volume == 1.0 && s.duck == 0.0 && s.windowScale == 3.0,
              "out-of-range numbers are clamped, not obeyed");
        check(problems.size() == 3, "each clamp is reported");
    }
    {
        std::vector<std::string> problems;
        const core::Settings s = core::parseSettings("volume = громко\nmusic_volume = 0.4x\n",
                                                     &problems);
        check(s.volume == 0.85 && s.musicVolume == 0.35, "text where a number belongs is refused");
        check(problems.size() == 2, "both bad numbers are reported");
    }

    // One broken line must not cost the user the rest of the file - that is the
    // difference between "one setting is wrong" and "none of my settings work".
    {
        std::vector<std::string> problems;
        const core::Settings s = core::parseSettings(
            "# a comment\n; another\n\nthis line has no equals sign\nvolume = 0.4\n", &problems);
        check(s.volume == 0.4, "settings after a broken line are still read");
        check(mentions(problems, "line 4"), "the broken line is named by number");
    }

    // Paths are values, not expressions: no comment stripping, no case folding,
    // spaces kept. A folder called "D:\Sounds #2" is a real folder.
    {
        const core::Settings s = core::parseSettings("LIBRARY = D:\\My Sounds #2\n");
        check(s.library == "D:\\My Sounds #2", "a path keeps its spaces and its hash");
        check(!s.library.empty(), "and an upper-case key is still recognised");
    }

    // Choices are checked, because a typo in a bus name would otherwise send
    // every announcement to a bus that does not exist.
    {
        std::vector<std::string> problems;
        const core::Settings s = core::parseSettings("announce_bus = master\n", &problems);
        check(s.announceBus == "interior", "an unknown bus keeps the default");
        check(mentions(problems, "announce_bus"), "and is named in the problems");
    }
    {
        std::vector<std::string> problems;
        const core::Settings s = core::parseSettings("airline_mode = automatic\n", &problems);
        check(s.airlineMode == "auto" && s.autoAirline(), "an unknown airline_mode keeps auto");
        check(mentions(problems, "airline_mode"), "and is reported");
    }

    // An empty file is a valid file: it means "everything as it comes".
    {
        std::vector<std::string> problems;
        const core::Settings s = core::parseSettings("", &problems);
        check(problems.empty() && s.volume == 0.85, "an empty file is all defaults, no complaints");
    }
}

}  // namespace xa::test
