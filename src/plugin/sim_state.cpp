#include "plugin/sim_state.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "XPLMDataAccess.h"
#include "XPLMNavigation.h"
#include "XPLMPlanes.h"

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

// What X-Plane itself publishes about the sign, as opposed to the switch: "Seatbelt
// sign on, yes or no", read-only. This is the answer to the question the plugin is
// actually asking, and it is the only source that can be right while a switch sits
// in AUTO - there the crew has handed the decision to the aeroplane, and the switch
// stops reporting the sign.
const char* const kSeatbeltAnnunciator = "sim/cockpit2/annunciators/fasten_seatbelt";

// Seat belt sign, most specific first. `on` is the value that means "lit": the
// Zibo switch is a three-position knob (0 off, 1 auto, 2 on), the rest are plain
// on/off. Sourced from the aircraft in Artyom's hangar, verified in the sim.
struct SeatbeltCandidate {
    const char* name;
    int on;
    int autoPosition;  // the "AUTO" detent, or -1 where the switch has none
};

const SeatbeltCandidate kSeatbeltCandidates[] = {
    {"AirbusFBW/SeatBeltSignsOn", 1, -1},                            // ToLiss
    {"b737ng/equipment/alerts/crew/cabin/CRW_seatbelts_on", 1, -1},  // 737NG Series
    {"Rotate/aircraft/controls/seatbelts_lts", 1, -1},               // MD-11
    {"laminar/B738/toggle_switch/seatbelt_sign_pos", 2, 1},          // Zibo: off/auto/on
    // FlightFactor 777 v2. The widget is passSignsSeatbeltsSwitch with
    // off(0)/auto(1)/on(2), read from modules/idxData_B772.txt; the dataref name
    // follows the aircraft's own convention, 1-sim/ckpt/<widget>/anim, seen on
    // some fifty other switches. NOT verified in the sim - the string lives in
    // an encrypted module - so if it is wrong the log says which dataref was
    // bound instead, and the annunciator below still answers.
    {"1-sim/ckpt/passSignsSeatbeltsSwitch/anim", 2, 1},
    // X-Plane's own answer to the only question that matters - "is the sign
    // lit". Read-only, and an aircraft has to drive it deliberately, so it comes
    // after the add-ons that publish their own switch.
    {kSeatbeltAnnunciator, 1, -1},
    {"sim/cockpit2/switches/fasten_seat_belts", 1, -1},
    {"sim/cockpit/switches/fasten_seat_belts", 1, -1},
};

// X-Plane itself publishes no logo light; only add-ons do.
const char* const kLogoCandidates[] = {
    "laminar/B738/toggle_switch/logo_light",
    "Rotate/aircraft/controls/logo_lts",
};

}  // namespace

void SimState::bind(const std::string& seatbeltOverride) {
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
    latitude_ = find("sim/flightmodel/position/latitude");
    longitude_ = find("sim/flightmodel/position/longitude");

    logo_ = nullptr;
    for (const char* name : kLogoCandidates) {
        if ((logo_ = find(name)) != nullptr) {
            break;
        }
    }

    seatbelt_ = nullptr;
    seatbeltName_.clear();
    seatbeltAuto_ = -1;
    // Bound whatever else is found: it is what the switch is asked to defer to.
    seatbeltSign_ = find(kSeatbeltAnnunciator);
    if (!seatbeltOverride.empty()) {
        if ((seatbelt_ = find(seatbeltOverride.c_str())) != nullptr) {
            seatbeltName_ = seatbeltOverride;
            seatbeltOn_ = 1;
        } else {
            log("datarefs: this aircraft has no '%s' - falling back to the known ones",
                seatbeltOverride.c_str());
        }
    }
    if (seatbelt_ == nullptr) {
        for (const SeatbeltCandidate& candidate : kSeatbeltCandidates) {
            if ((seatbelt_ = find(candidate.name)) != nullptr) {
                seatbeltName_ = candidate.name;
                seatbeltOn_ = candidate.on;
                seatbeltAuto_ = candidate.autoPosition;
                break;
            }
        }
    }
    log("datarefs: seatbelt %s%s, logo %s",
        seatbeltName_.empty() ? "none published by this aircraft" : seatbeltName_.c_str(),
        seatbeltAuto_ >= 0 ? (seatbeltSign_ != nullptr ? " (has AUTO; the sign itself is readable)"
                                                       : " (has AUTO, but the sign is not readable)")
                           : "",
        logo_ != nullptr ? "found" : "none");
}

SimState::Identity SimState::readIdentity() const {
    // String datarefs come back as a byte array that is NOT guaranteed to be
    // terminated, so the length is asked for first and the result trimmed at the
    // first NUL rather than trusted.
    const auto readString = [](const char* name) {
        XPLMDataRef ref = XPLMFindDataRef(name);
        if (ref == nullptr) {
            return std::string();
        }
        const int size = XPLMGetDatab(ref, nullptr, 0, 0);
        if (size <= 0) {
            return std::string();
        }
        std::vector<char> buffer(static_cast<std::size_t>(size) + 1, '\0');
        XPLMGetDatab(ref, buffer.data(), 0, size);
        return std::string(buffer.data());
    };

    Identity identity;
    identity.liveryPath = readString("sim/aircraft/view/acf_livery_path");
    identity.tailNumber = readString("sim/aircraft/view/acf_tailnum");
    identity.description = readString("sim/aircraft/view/acf_descrip");
    // What the aeroplane calls itself: A320, B738, CL60. Packs tag files with
    // it, so it decides between SafetyBriefing[A320] and the plain one.
    identity.icao = readString("sim/aircraft/view/acf_ICAO");

    char fileName[256] = {0};
    char filePath[1024] = {0};
    XPLMGetNthAircraftModel(0, fileName, filePath);
    identity.aircraftFile = fileName;
    return identity;
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
        // A switch in AUTO is not a state of the sign - it is the crew saying
        // "aeroplane, you decide". Reading the detent then answers the wrong
        // question: on a 777 above ten thousand feet the sign goes out while the
        // switch stays exactly where it was, which is precisely the transition
        // this plugin exists to announce. Where the aeroplane publishes the sign
        // itself, that is what gets read.
        if (seatbeltAuto_ >= 0 && value == seatbeltAuto_ && seatbeltSign_ != nullptr) {
            const bool lit = XPLMGetDatai(static_cast<XPLMDataRef>(seatbeltSign_)) == 1;
            s.seatbelt = lit ? core::Tri::On : core::Tri::Off;
            if (!loggedAuto_) {
                loggedAuto_ = true;
                log("datarefs: the seatbelt switch is in AUTO - reading the sign from %s instead",
                    kSeatbeltAnnunciator);
            }
        } else {
            s.seatbelt = value >= seatbeltOn_ ? core::Tri::On : core::Tri::Off;
            loggedAuto_ = false;
        }
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

    s.lat = latitude_ != nullptr ? XPLMGetDatad(static_cast<XPLMDataRef>(latitude_)) : 0.0;
    s.lon = longitude_ != nullptr ? XPLMGetDatad(static_cast<XPLMDataRef>(longitude_)) : 0.0;
    readRoute(&s);
    return s;
}

// The destination is the last entry of the FMS route. X-Plane publishes no
// dataref for it - only relative bearings and DME to whatever is tuned - so the
// Navigation API is the only honest source, and a plan that is not there has to
// come back as "unknown" rather than as a distance of zero.
void SimState::readRoute(core::Snapshot* s) const {
    const int count = XPLMCountFMSEntries();
    if (count <= 0) {
        return;
    }
    // Walk back from the end: the tail of a plan can hold entries with no
    // navaid and no coordinates, and one of those would put the destination in
    // the Gulf of Guinea - latitude zero, longitude zero.
    for (int i = count - 1; i >= 0; --i) {
        XPLMNavType type = xplm_Nav_Unknown;
        char id[256] = {0};
        XPLMNavRef ref = XPLM_NAV_NOT_FOUND;
        int altitude = 0;
        float lat = 0.0f;
        float lon = 0.0f;
        XPLMGetFMSEntryInfo(i, &type, id, &ref, &altitude, &lat, &lon);
        if (type == xplm_Nav_Unknown || (lat == 0.0f && lon == 0.0f)) {
            continue;
        }
        s->routeKnown = true;
        s->destLat = lat;
        s->destLon = lon;
        return;
    }
}

}  // namespace xa
