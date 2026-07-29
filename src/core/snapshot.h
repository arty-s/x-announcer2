// What the core knows about the aeroplane at one instant.
//
// This is the only thing the state machine ever reads. The plugin fills it from
// datarefs; the test bench fills it from a scenario file. Nothing in this header
// may depend on XPLM - that separation is the whole reason the bench can fly a
// complete flight in milliseconds without starting the simulator.
#pragma once

namespace xa::core {

// The seat belt sign is genuinely three-valued: an aircraft may not publish one
// at all. Collapsing "unknown" into "off" would make the sign appear to switch
// on the first time it is read, and fire a PA nobody asked for.
enum class Tri { Unknown, Off, On };

struct Snapshot {
    bool paused = false;
    bool replay = false;

    bool onGround = true;
    double gsKt = 0.0;
    double aglFt = 0.0;
    double altFt = 0.0;
    double vsFpm = 0.0;
    double gNormal = 1.0;

    bool beacon = false;
    bool navLights = false;
    bool strobe = false;
    bool landingLight = false;
    bool taxiLight = false;
    bool logo = false;
    bool logoDrefExists = false;  // only add-ons publish one; see powerSigns()

    bool parkbrake = false;
    bool battery = false;
    Tri seatbelt = Tri::Unknown;

    int enginesRunning = 0;
    int localHour = 12;

    // Where we are, and where the flight plan says we are going. The destination
    // is the last entry of the FMS route; routeKnown is false whenever there is
    // no plan loaded, which is most short hops and every flight flown by hand.
    // Two separate facts on purpose: a position is always available, a plan is
    // not, and the core must never treat a missing plan as "distance zero".
    double lat = 0.0;
    double lon = 0.0;
    bool routeKnown = false;
    double destLat = 0.0;
    double destLon = 0.0;

    bool anyEngine() const { return enginesRunning > 0; }
    bool allEnginesOff() const { return enginesRunning == 0; }
    bool isDark() const { return localHour >= 21 || localHour < 6; }
    bool frozen() const { return paused || replay; }
};

}  // namespace xa::core
