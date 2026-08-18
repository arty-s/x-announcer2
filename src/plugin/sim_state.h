// Datarefs in, core Snapshot out. The only place in the plugin that knows the
// name of a dataref.
#pragma once

#include <string>

#include "core/snapshot.h"

namespace xa {

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

    // Looks for the seat belt sign again, and only for it. Plugin load order is
    // not guaranteed and an aeroplane's own plugin registers its datarefs when it
    // pleases, so the search that ran at XPLM_MSG_PLANE_LOADED can easily have
    // been a moment too early. Cheap: a handful of XPLMFindDataRef calls, and it
    // stops asking as soon as the aeroplane's own dataref turns up.
    // Returns true when the binding changed, so the caller can stop.
    bool retrySeatbelt(const std::string& seatbeltOverride);

    // True while the sign is being read from a stock dataref rather than from
    // one the aeroplane published itself. Stock names always resolve, so this is
    // NOT "nothing was found" - it is "nothing better was found yet".
    bool seatbeltIsFallback() const { return seatbeltFallback_; }

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

    // Which seat belt dataref was found, for the panel to show. Empty means the
    // aircraft publishes none, which is information, not a fault.
    const char* seatbeltDataref() const { return seatbeltName_.c_str(); }
    bool hasLogoDataref() const { return logo_ != nullptr; }

private:
    // Fills in the destination from the FMS route, or leaves routeKnown false.
    void readRoute(core::Snapshot* s) const;

    struct SeatbeltRef {
        const char* name;
        int onValue;
    };

    void* paused_ = nullptr;
    void* replay_ = nullptr;
    void* onGround_ = nullptr;
    void* groundSpeed_ = nullptr;
    void* agl_ = nullptr;
    void* altitude_ = nullptr;
    void* verticalSpeed_ = nullptr;
    void* gNormal_ = nullptr;
    void* beacon_ = nullptr;
    void* nav_ = nullptr;
    void* strobe_ = nullptr;
    void* landing_ = nullptr;
    void* taxi_ = nullptr;
    void* parkbrake_ = nullptr;
    void* battery_ = nullptr;
    void* logo_ = nullptr;
    void* engineCount_ = nullptr;
    void* enginesRunning_ = nullptr;
    void* localTime_ = nullptr;
    void* latitude_ = nullptr;
    void* longitude_ = nullptr;

    // How a switch sitting in its AUTO detent is turned into "is the sign lit".
    enum class AutoRule {
        None,          // the switch has no AUTO detent, or the dataref IS the lamp
        FlapsOrGear,   // what the 737 does itself: lit with flaps out or gear down
        SignThenFlaps  // believe the annunciator once it has been seen lit, else flaps/gear
    };

    // Finds the seat belt sign and fills in seatbelt*_. Split out of bind() so
    // that the retry can redo just this part.
    bool bindSeatbelt(const std::string& seatbeltOverride, bool announce);
    bool seatbeltLit() const;

    void* seatbelt_ = nullptr;
    std::string seatbeltName_;
    int seatbeltOn_ = 1;
    // The AUTO detent of a three-position switch, or -1. In AUTO the switch stops
    // reporting the sign, so the sign has to be worked out some other way.
    int seatbeltAuto_ = -1;
    AutoRule seatbeltRule_ = AutoRule::None;
    bool seatbeltFallback_ = true;
    void* seatbeltSign_ = nullptr;   // sim/cockpit2/annunciators/fasten_seatbelt
    void* seatbeltStock2_ = nullptr; // sim/cockpit2/switches/fasten_seat_belts
    void* seatbeltStock1_ = nullptr; // sim/cockpit/switches/fasten_seat_belts
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
