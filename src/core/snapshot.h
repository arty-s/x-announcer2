// What the core knows about the aeroplane at one instant.
//
// This is the only thing the state machine ever reads. The plugin fills it from
// datarefs; the test bench fills it from a scenario file. Nothing in this header
// may depend on XPLM - that separation is the whole reason the bench can fly a
// complete flight in milliseconds without starting the simulator.
#pragma once

namespace xa::core {

// Every cockpit switch the plugin reads is three-valued, and the third value is
// the one that matters: an aeroplane may simply not publish it.
//
// The seat belt sign taught this the expensive way - collapsing "unknown" into
// "off" made the sign appear to switch on the first time it was read and fired a
// PA nobody asked for. The same collapse on the exterior lights is worse and
// quieter: a study-level aeroplane that runs its own electrics publishes no
// stock beacon, X-Plane's dataref reads zero for the whole flight, and a
// condition written as "beacon on" can then never be met. The phase stops
// moving, no announcement is missing from the log because none was ever due, and
// the report says only "it is silent".
//
// So the rule, everywhere below: a signal we cannot read must not be allowed to
// FORBID anything. Unknown may never stand in for "off" in a condition that
// gates a phase.
enum class Tri { Unknown, Off, On };

inline bool isOn(Tri t) { return t == Tri::On; }
inline bool isOff(Tri t) { return t == Tri::Off; }
inline bool known(Tri t) { return t != Tri::Unknown; }

struct Snapshot {
    bool paused = false;
    bool replay = false;

    bool onGround = true;
    double gsKt = 0.0;
    double aglFt = 0.0;
    double altFt = 0.0;
    double vsFpm = 0.0;
    double gNormal = 1.0;

    // The exterior lights and the cabin signs, as the aeroplane publishes them -
    // or Unknown where it publishes nothing at all. Defaults are Unknown rather
    // than Off precisely because "nothing bound yet" is the state the plugin
    // starts in and must survive.
    Tri beacon = Tri::Unknown;
    Tri navLights = Tri::Unknown;
    Tri strobe = Tri::Unknown;
    Tri landingLight = Tri::Unknown;
    Tri taxiLight = Tri::Unknown;
    Tri logo = Tri::Unknown;

    Tri parkbrake = Tri::Unknown;
    Tri battery = Tri::Unknown;
    Tri seatbelt = Tri::Unknown;

    // "We have finished asking this aeroplane what it publishes." Not a fact
    // about the aeroplane at all - a fact about how much of it we have had time
    // to learn. The plugin hunts for the aircraft's own datarefs for two minutes
    // after it loads, because plugin load order is not guaranteed, so for those
    // two minutes every signal above can read Unknown on an aeroplane that will
    // publish all four a second later.
    //
    // The distinction matters in exactly one place: where Unknown is waved
    // through as "yes" (aircraftPowered). Waving it through while we are still
    // looking is how a cold and dark aeroplane starts boarding at the moment it
    // loads. Default true because the core is allowed to be used by something
    // that has no search to wait for - only a caller that HAS one can say no.
    bool signalsSettled = true;

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

    // How far the aeroplane's OWN flight computer says it is from the
    // destination. The stock FMS is empty on every add-on with a real FMC - a
    // Zibo's route lives in the Zibo - so where the aeroplane publishes the
    // remaining distance itself, that is the honest number and it wins over the
    // great-circle sum below.
    bool routeDistanceKnown = false;
    double routeDistanceNm = 0.0;

    bool anyEngine() const { return enginesRunning > 0; }
    bool allEnginesOff() const { return enginesRunning == 0; }
    bool isDark() const { return localHour >= 21 || localHour < 6; }
    bool frozen() const { return paused || replay; }

    // "There is a destination to measure against", whichever way we get it.
    bool haveRoute() const { return routeDistanceKnown || routeKnown; }
};

}  // namespace xa::core
