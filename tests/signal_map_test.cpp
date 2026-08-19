// signals.ini - the file a person edits after reading one probe line in a log,
// usually in a hurry and usually on a phone. Every way it can be wrong has to
// end in a complaint that names the line, never in a silently ignored setting:
// a binding that quietly did not happen looks exactly like the fault it was
// meant to fix.
#include "signal_map_test.h"

#include <iostream>
#include <string>
#include <vector>

#include "core/signal_map.h"

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

}  // namespace

void runSignalMapChecks(int* checks, int* failed) {
    g_checks = checks;
    g_failed = failed;
    std::cout << "signals.ini\n";

    {
        // The whole point, in the shape it will actually be written: one
        // aeroplane, three switches, one of them wired upside down.
        const char* const text =
            "# FlightFactor 777\n"
            "[B772]\n"
            "beacon = 1-sim/ckpt/beaconLightSwitch/anim\n"
            "strobe = 1-sim/ckpt/strobeLightSwitch/anim on<=0\n"
            "seatbelt = 1-sim/anim/seatbeltLight on>=2\n";
        std::vector<std::string> problems;
        const core::SignalOverrides map = core::parseSignalOverrides(text, &problems);
        check(problems.empty(), "a good file has nothing to complain about");
        check(map.count() == 3, "all three lines are kept");

        const std::vector<core::SignalOverride> mine = map.forAircraft("B772");
        check(mine.size() == 3, "and all three apply to that aeroplane");
        check(mine[0].dataref == "1-sim/ckpt/beaconLightSwitch/anim", "the dataref is read whole");
        check(mine[0].on == 1.0 && !mine[0].atMost, "no threshold means 1 and above");
        check(mine[1].atMost && mine[1].on == 0.0, "on<=0 is the inverted switch");
        check(mine[2].on == 2.0 && !mine[2].atMost, "on>=2 is a three-position knob");

        check(map.forAircraft("B738").empty(), "another aeroplane gets none of it");
    }

    {
        // Sections are optional. A file with one line in it is the commonest
        // one there will ever be, and it must not need ceremony.
        const char* const text = "logo = laminar/B738/toggle_switch/logo_light\n";
        std::vector<std::string> problems;
        const core::SignalOverrides map = core::parseSignalOverrides(text, &problems);
        check(problems.empty(), "a file with no section at all is valid");
        check(map.forAircraft("A20N").size() == 1, "and applies to every aeroplane");
    }

    {
        // Specific before general: the whole reason sections exist.
        const char* const text =
            "[*]\n"
            "beacon = common/beacon\n"
            "[B772]\n"
            "beacon = 1-sim/ckpt/beaconLightSwitch/anim\n";
        const core::SignalOverrides map = core::parseSignalOverrides(text, nullptr);
        const std::vector<core::SignalOverride> mine = map.forAircraft("B772");
        check(mine.size() == 2, "both lines are offered");
        check(mine[0].dataref == "1-sim/ckpt/beaconLightSwitch/anim",
              "the aeroplane's own line is tried first");
        check(map.forAircraft("B738")[0].dataref == "common/beacon",
              "everything else falls through to the shared section");
    }

    {
        // The aeroplane code is written as it is read off the panel; X-Plane
        // reports it in capitals. A file that only worked in one case would be
        // a trap nobody could see.
        const char* const text = "[b772]\nbeacon = a/b\n";
        const core::SignalOverrides map = core::parseSignalOverrides(text, nullptr);
        check(map.forAircraft("B772").size() == 1, "the section name is case-insensitive");
    }

    {
        std::vector<std::string> problems;
        const char* const text =
            "beacon\n"                       // no =
            "wheels = a/b\n"                 // no such signal
            "strobe = a/b on<>1\n"           // not a threshold
            "[unclosed\n"
            "taxi = c/d\n";                  // still good, after four bad ones
        const core::SignalOverrides map = core::parseSignalOverrides(text, &problems);
        check(mentions(problems, "нет знака ="), "a line with no = is named");
        check(mentions(problems, "wheels"), "an unknown signal is named, not guessed at");
        check(mentions(problems, "on<>1"), "a malformed threshold is quoted back");
        check(mentions(problems, "скобка"), "an unclosed section is named");
        check(map.count() == 1, "and the one good line still survives all four");
    }

    {
        // The sample is what a person sees first, so it has to parse as itself.
        std::vector<std::string> problems;
        const core::SignalOverrides map =
            core::parseSignalOverrides(core::sampleSignalOverrides(), &problems);
        check(problems.empty(), "the sample file we write out is itself valid");
        check(map.empty(), "and binds nothing until somebody uncomments a line");
    }
}

}  // namespace xa::test
