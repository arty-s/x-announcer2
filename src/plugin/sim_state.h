// Datarefs in, core Snapshot out. The only place in the plugin that knows the
// name of a dataref.
//
// Every reading the state machine acts on goes through one table here, and that
// table answers two questions rather than one: what does the switch say, and did
// we have any way of asking. The second question is the whole point.
//
// X-Plane's own switch datarefs always exist. Ask for the beacon on an aeroplane
// that runs its own electrical system and you get a confident zero for the whole
// flight - not "no such thing", but "off", forever. A phase gated on "beacon on"
// then never fires, nothing is missing from the log, and the report reads "it is
// silent". So a stock dataref is treated as PROVISIONAL: it starts as Unknown
// and only becomes a real reading once it has been seen lit or seen to move. A
// dataref the aeroplane published under its own name needs no such proof - the
// name existing is the proof.
#pragma once

#include <string>
#include <vector>

#include "core/signal_map.h"
#include "core/snapshot.h"

namespace xa {

// Everything the plugin reads off the aeroplane that is not plain physics.
enum class Signal {
    Beacon,
    NavLights,
    Strobe,
    LandingLight,
    TaxiLight,
    Logo,
    Battery,
    Parkbrake,
    Seatbelt,
    RouteDistance,
    Count
};

// One row of the Triggers tab, and the thing a report has to contain for an
// aeroplane nobody has tested: what we bound to, where it came from, what it
// reads now and whether it has ever moved.
struct SignalReport {
    const char* id = "";        // beacon, strobe - stable, for the log
    const char* title = "";     // Маяк, Стробы - for the panel
    std::string dataref;        // what we are reading, empty when nothing was found
    bool fromAircraft = false;  // published by this aeroplane, not by X-Plane
    bool bound = false;
    bool everMoved = false;
    double value = 0.0;
    core::Tri reading = core::Tri::Unknown;
    std::string note;  // why the reading is what it is, when that needs saying
};

class SimState {
public:
    // Looks every dataref up once. Must be called after XPluginStart, when the
    // aircraft is loaded, and again whenever the aircraft changes: an add-on's
    // datarefs come and go with it.
    //
    // `seatbeltOverride` is the user's own seat belt dataref from the settings.
    // If it does not exist on this aircraft the search falls back to the known
    // ones and says so - a typo there must not leave the cabin sign dead.
    void bind(const std::string& seatbeltOverride = std::string());

    // The user's own signals.ini, and which aeroplane is loaded. Both only
    // matter at bind time; set them before it.
    void setOverrides(const core::SignalOverrides& overrides) { overrides_ = overrides; }
    void setAircraft(const std::string& icao) { icao_ = icao; }

    // Looks again for the signals that are still on a stock dataref or on
    // nothing at all. Plugin load order is not guaranteed and an aeroplane's own
    // plugin registers its datarefs when it pleases, so the search that ran at
    // XPLM_MSG_PLANE_LOADED can easily have been a moment too early. Cheap: a
    // handful of XPLMFindDataRef calls, and each signal stops asking as soon as
    // the aeroplane's own dataref turns up.
    // Returns true when at least one binding changed.
    bool retryUnbound(const std::string& seatbeltOverride);

    // Is anything still worth looking for. False stops the retry loop early.
    bool anySignalPending() const;

    // True while the sign is being read from a stock dataref rather than from
    // one the aeroplane published itself. Stock names always resolve, so this is
    // NOT "nothing was found" - it is "nothing better was found yet".
    bool seatbeltIsFallback() const;

    core::Snapshot read() const;

    // Who we are flying, as opposed to how we are flying it. Read separately
    // because it only changes when the aircraft or the livery does.
    struct Identity {
        std::string liveryPath;
        std::string tailNumber;
        std::string aircraftFile;
        std::string description;
        std::string icao;  // A320, B738 - what the pack's [tags] are matched against
    };
    Identity readIdentity() const;

    // The whole table, in panel order. Cheap enough to build per frame.
    std::vector<SignalReport> signalReports() const;

    // The same table as log lines. Written when the aeroplane settles and again
    // when the user sends a report, so a report from an untested aeroplane says
    // which of its triggers are alive without a second round of questions.
    std::vector<std::string> signalLogLines() const;

    // Which seat belt dataref was found, for the panel to show. Empty means the
    // aircraft publishes none, which is information, not a fault.
    std::string seatbeltDataref() const;
    bool hasLogoDataref() const;

private:
    // Fills in the destination from the FMS route, or leaves routeKnown false.
    void readRoute(core::Snapshot* s) const;

    // How a switch sitting in its AUTO detent is turned into "is the sign lit".
    enum class AutoRule {
        None,          // the switch has no AUTO detent, or the dataref IS the lamp
        FlapsOrGear,   // what the 737 does itself: lit with flaps out or gear down
        SignThenFlaps  // believe the annunciator once it has been seen lit, else flaps/gear
    };

    struct Binding {
        void* ref = nullptr;
        std::string name;
        // "On" is a threshold, not an equality, and it has a direction: the
        // FlightFactor 777 wires its strobe, taxi and landing switches the other
        // way up - on(0), off(1) in the aeroplane's own switch table - so a
        // table that could only say "1 means on" would report those lights lit
        // for exactly as long as they were dark.
        double on = 1.0;
        bool atMost = false;
        bool fromAircraft = false;
        // A stock dataref exists whatever is loaded, so finding one proves
        // nothing. It counts as an answer only once it has been seen lit or seen
        // to move.
        bool provisional = false;
        int autoPos = -1;  // seat belt three-position switch, or -1
        AutoRule rule = AutoRule::None;

        mutable bool haveLast = false;
        mutable double lastValue = 0.0;
        mutable bool everMoved = false;
        mutable bool everMeaningful = false;

        bool bound() const { return ref != nullptr; }
    };

    Binding& slot(Signal s) { return bindings_[static_cast<int>(s)]; }
    const Binding& slot(Signal s) const { return bindings_[static_cast<int>(s)]; }

    // Binds one signal from the tables plus any override. `announce` writes the
    // result to the log even when nothing changed.
    bool bindSignal(Signal signal, const std::string& override, bool announce);
    // Reads the raw number and keeps the diagnostics up to date.
    double sample(const Binding& b) const;
    // The reading as the engine sees it, Unknown included.
    core::Tri triOf(Signal signal) const;
    core::Tri seatbeltTri() const;

    core::SignalOverrides overrides_;
    std::string icao_;

    Binding bindings_[static_cast<int>(Signal::Count)];

    void* paused_ = nullptr;
    void* replay_ = nullptr;
    void* onGround_ = nullptr;
    void* groundSpeed_ = nullptr;
    void* agl_ = nullptr;
    void* altitude_ = nullptr;
    void* verticalSpeed_ = nullptr;
    void* gNormal_ = nullptr;
    void* engineCount_ = nullptr;
    void* enginesRunning_ = nullptr;
    void* localTime_ = nullptr;
    void* latitude_ = nullptr;
    void* longitude_ = nullptr;

    // What a switch in AUTO defers to, and what the stock seat belt fallback
    // reads. Kept apart from the table because the AUTO rule consults them
    // whatever the seat belt itself is bound to.
    void* seatbeltSign_ = nullptr;    // sim/cockpit2/annunciators/fasten_seatbelt
    void* seatbeltStock2_ = nullptr;  // sim/cockpit2/switches/fasten_seat_belts
    void* seatbeltStock1_ = nullptr;  // sim/cockpit/switches/fasten_seat_belts
    void* flapRatio_ = nullptr;
    void* gearHandle_ = nullptr;
    void* paxOxygen_ = nullptr;
    mutable bool loggedAuto_ = false;
    // The annunciator is only trusted once it has been seen lit: on aeroplanes
    // that run their own cabin signs it exists, reads zero for the whole flight,
    // and a switch in AUTO read through it would report a sign that never lights.
    mutable bool signSeenLit_ = false;
};

}  // namespace xa
