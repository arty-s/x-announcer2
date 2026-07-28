#include "plugin/sim_state.h"

#include <algorithm>
#include <cmath>

#include "XPLMDataAccess.h"

#include "plugin/xa_log.h"

namespace xa {
namespace {

XPLMDataRef find(const char* name) { return XPLMFindDataRef(name); }

int readInt(void* ref, int fallback) {
    return ref == nullptr ? fallback : XPLMGetDatai(static_cast<XPLMDataRef>(ref));
}

float readFloat(void* ref, float fallback) {
    return ref == nullptr ? fallback : XPLMGetDataf(static_cast<XPLMDataRef>(ref));
}

// Seat belt sign, most specific first. `on` is the value that means "lit": the
// Zibo switch is a three-position knob (0 off, 1 auto, 2 on), the rest are plain
// on/off. Sourced from the aircraft in Artyom's hangar, verified in the sim.
struct SeatbeltCandidate {
    const char* name;
    int on;
};

const SeatbeltCandidate kSeatbeltCandidates[] = {
    {"AirbusFBW/SeatBeltSignsOn", 1},                            // ToLiss
    {"b737ng/equipment/alerts/crew/cabin/CRW_seatbelts_on", 1},   // 737NG Series
    {"Rotate/aircraft/controls/seatbelts_lts", 1},                // MD-11
    {"laminar/B738/toggle_switch/seatbelt_sign_pos", 2},          // Zibo
    {"sim/cockpit2/switches/fasten_seat_belts", 1},
    {"sim/cockpit/switches/fasten_seat_belts", 1},
};

// X-Plane itself publishes no logo light; only add-ons do.
const char* const kLogoCandidates[] = {
    "laminar/B738/toggle_switch/logo_light",
    "Rotate/aircraft/controls/logo_lts",
};

}  // namespace

void SimState::bind() {
    paused_ = find("sim/time/paused");
    replay_ = find("sim/time/is_in_replay");
    onGround_ = find("sim/flightmodel/failures/onground_any");
    groundSpeed_ = find("sim/flightmodel/position/groundspeed");
    agl_ = find("sim/flightmodel/position/y_agl");
    altitude_ = find("sim/flightmodel/misc/h_ind");
    verticalSpeed_ = find("sim/flightmodel/position/vh_ind_fpm");
    gNormal_ = find("sim/flightmodel/forces/g_nrml");
    beacon_ = find("sim/cockpit2/switches/beacon_on");
    nav_ = find("sim/cockpit2/switches/navigation_lights_on");
    strobe_ = find("sim/cockpit2/switches/strobe_lights_on");
    landing_ = find("sim/cockpit2/switches/landing_lights_on");
    taxi_ = find("sim/cockpit2/switches/taxi_light_on");
    parkbrake_ = find("sim/flightmodel/controls/parkbrake");
    battery_ = find("sim/cockpit2/electrical/battery_on");
    engineCount_ = find("sim/aircraft/engine/acf_num_engines");
    enginesRunning_ = find("sim/flightmodel/engine/ENGN_running");
    localTime_ = find("sim/time/local_time_sec");

    logo_ = nullptr;
    for (const char* name : kLogoCandidates) {
        if ((logo_ = find(name)) != nullptr) {
            break;
        }
    }

    seatbelt_ = nullptr;
    seatbeltName_ = "";
    for (const SeatbeltCandidate& candidate : kSeatbeltCandidates) {
        if ((seatbelt_ = find(candidate.name)) != nullptr) {
            seatbeltName_ = candidate.name;
            seatbeltOn_ = candidate.on;
            break;
        }
    }
    log("datarefs: seatbelt %s, logo %s",
        seatbeltName_[0] != '\0' ? seatbeltName_ : "none published by this aircraft",
        logo_ != nullptr ? "found" : "none");
}

core::Snapshot SimState::read() const {
    core::Snapshot s;
    s.paused = readInt(paused_, 0) == 1;
    s.replay = readInt(replay_, 0) == 1;
    s.onGround = readInt(onGround_, 1) == 1;
    s.gsKt = readFloat(groundSpeed_, 0.0f) * 1.94384;
    s.aglFt = readFloat(agl_, 0.0f) * 3.28084;
    s.altFt = readFloat(altitude_, 0.0f);
    s.vsFpm = readFloat(verticalSpeed_, 0.0f);
    s.gNormal = readFloat(gNormal_, 1.0f);
    s.beacon = readInt(beacon_, 0) == 1;
    s.navLights = readInt(nav_, 0) == 1;
    s.strobe = readInt(strobe_, 0) == 1;
    s.landingLight = readInt(landing_, 0) == 1;
    s.taxiLight = readInt(taxi_, 0) == 1;
    s.parkbrake = readFloat(parkbrake_, 0.0f) > 0.5f;
    s.battery = readInt(battery_, 0) == 1;
    s.logo = logo_ != nullptr && readInt(logo_, 0) == 1;
    s.logoDrefExists = logo_ != nullptr;

    if (seatbelt_ != nullptr) {
        // Some aircraft publish the sign as an int, some as a float switch
        // position, and reading the wrong one gives a confident zero.
        int value = XPLMGetDatai(static_cast<XPLMDataRef>(seatbelt_));
        if (value == 0) {
            const float asFloat = XPLMGetDataf(static_cast<XPLMDataRef>(seatbelt_));
            if (asFloat >= static_cast<float>(seatbeltOn_) - 0.5f) {
                value = seatbeltOn_;
            }
        }
        s.seatbelt = value >= seatbeltOn_ ? core::Tri::On : core::Tri::Off;
    }

    // ENGN_running is int[16] and the SDK warns that reading past the end of an
    // array dataref can take the simulator down without a word, so the count is
    // clamped to the documented size before it is used.
    int engines = readInt(engineCount_, 2);
    engines = std::max(1, std::min(engines, 16));
    s.enginesRunning = 0;
    if (enginesRunning_ != nullptr) {
        int running[16] = {0};
        const int got = XPLMGetDatavi(static_cast<XPLMDataRef>(enginesRunning_), running, 0, engines);
        for (int i = 0; i < got && i < 16; ++i) {
            if (running[i] == 1) {
                ++s.enginesRunning;
            }
        }
    }

    const float localSeconds = readFloat(localTime_, 43200.0f);
    s.localHour = static_cast<int>(std::fmod(localSeconds, 86400.0f) / 3600.0f);
    if (s.localHour < 0 || s.localHour > 23) {
        s.localHour = 12;
    }
    return s;
}

}  // namespace xa
