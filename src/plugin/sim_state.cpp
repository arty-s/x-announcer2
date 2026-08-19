#include "plugin/sim_state.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
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

// Mirrors SimState::AutoRule, which is private to the class. The values line up
// and are converted at the single place this table is read.
enum class AutoRuleTag { None, FlapsOrGear, SignThenFlaps };

struct Candidate {
    Signal signal;
    const char* name;
    double on;
    bool atMost;
    AutoRuleTag rule;
    int autoPos;
};

constexpr AutoRuleTag kNoRule = AutoRuleTag::None;

// What an AEROPLANE publishes under its own name. Only names an add-on invents
// belong here: a stock name always resolves, so one stock entry in this list
// would end every search at itself and leave everything below unreachable. That
// is exactly how a Zibo 737 once ended up being read through an annunciator its
// own systems never write.
//
// The names are not guesses. Each was read out of the aeroplane's own files -
// the datarefs its objects animate, its published dataref list, or its switch
// table - so an entry here means the dataref exists, not that it probably does.
const Candidate kAircraftCandidates[] = {
    // ---------------------------------------------------------------- beacon
    {Signal::Beacon, "1-sim/ckpt/beaconLightSwitch/anim", 1.0, false, kNoRule, -1},
    {Signal::Beacon, "ckpt/oh/beaconLight/anim", 1.0, false, kNoRule, -1},            // ToLiss
    {Signal::Beacon, "Rotate/aircraft/controls/beacon_lts", 1.0, false, kNoRule, -1}, // MD-11
    {Signal::Beacon, "CL650/overhead/ext_lts/beacon", 1.0, false, kNoRule, -1},
    {Signal::Beacon, "KA350/ianim/pSubpanel/beaconLights", 1.0, false, kNoRule, -1},

    // ------------------------------------------------------------ navigation
    {Signal::NavLights, "laminar/B738/toggle_switch/position_light_pos", 1.0, false, kNoRule, -1},
    {Signal::NavLights, "1-sim/ckpt/navLightSwitch/anim", 1.0, false, kNoRule, -1},
    {Signal::NavLights, "Rotate/aircraft/controls/nav_lts", 1.0, false, kNoRule, -1},

    // --------------------------------------------------------------- strobes
    // The 777 wires these the other way up - on(0), off(1) in its own switch
    // table - and read the usual way round it would report the strobes lit for
    // exactly as long as they are dark, which is the whole taxi out.
    {Signal::Strobe, "1-sim/ckpt/strobeLightSwitch/anim", 0.0, true, kNoRule, -1},
    {Signal::Strobe, "ckpt/oh/strobeLight/anim", 1.0, false, kNoRule, -1},
    {Signal::Strobe, "Rotate/aircraft/controls/strobe_lts", 1.0, false, kNoRule, -1},
    {Signal::Strobe, "KA350/ianim/pSubpanel/strobeLights", 1.0, false, kNoRule, -1},

    // --------------------------------------------------------- landing lights
    {Signal::LandingLight, "1-sim/ckpt/landingLightNoseSwitch/anim", 0.0, true, kNoRule, -1},
    {Signal::LandingLight, "1-sim/ckpt/landingLightLeftSwitch/anim", 0.0, true, kNoRule, -1},
    {Signal::LandingLight, "laminar/B738/switch/land_lights_left_pos", 1.0, false, kNoRule, -1},

    // ------------------------------------------------------------ taxi lights
    {Signal::TaxiLight, "1-sim/ckpt/taxiLightSwitch/anim", 0.0, true, kNoRule, -1},
    {Signal::TaxiLight, "laminar/B738/toggle_switch/taxi_light_brightness_pos", 1.0, false,
     kNoRule, -1},

    // ------------------------------------------------------------ logo lights
    // X-Plane itself publishes no logo light; only add-ons do, which is why this
    // signal has no stock fallback at all.
    {Signal::Logo, "laminar/B738/toggle_switch/logo_light", 1.0, false, kNoRule, -1},
    {Signal::Logo, "Rotate/aircraft/controls/logo_lts", 1.0, false, kNoRule, -1},
    {Signal::Logo, "1-sim/ckpt/logoLightSwitch/anim", 1.0, false, kNoRule, -1},
    {Signal::Logo, "CL650/overhead/ext_lts/logo", 1.0, false, kNoRule, -1},

    // ---------------------------------------------------------------- battery
    {Signal::Battery, "1-sim/ckpt/batteryButton/anim", 1.0, false, kNoRule, -1},

    // ----------------------------------------------------------- park brake
    {Signal::Parkbrake, "1-sim/ckpt/parkbrake/anim", 1.0, false, kNoRule, -1},
    {Signal::Parkbrake, "laminar/B738/parking_brake_pos", 0.5, false, kNoRule, -1},
    {Signal::Parkbrake, "Rotate/aircraft/controls/park_brake", 0.5, false, kNoRule, -1},
    {Signal::Parkbrake, "ckpt/parkbrk", 0.5, false, kNoRule, -1},
    {Signal::Parkbrake, "CL650/pedestal/park_brake", 0.5, false, kNoRule, -1},

    // -------------------------------------------------------- seat belt sign
    // Zibo/LevelUp 737. The lamp comes first: the aeroplane's own script already
    // resolved AUTO (flaps out or gear down, and masks deployed override
    // everything), so reading it needs no rule of ours. It is published by the
    // FMOD sound pack script and older builds have no such thing, hence the
    // switch right after it. Note the case: B738 on the switch, b738 on the lamp.
    {Signal::Seatbelt, "laminar/b738/fmodpack/seatbelt_on_light", 1.0, false, kNoRule, -1},
    {Signal::Seatbelt, "laminar/B738/toggle_switch/seatbelt_sign_pos", 2.0, false,
     AutoRuleTag::FlapsOrGear, 1},
    {Signal::Seatbelt, "AirbusFBW/SeatBeltSignsOn", 1.0, false, kNoRule, -1},          // ToLiss lamp
    {Signal::Seatbelt, "ckpt/oh/seatbelts/anim", 1.0, false, kNoRule, -1},             // ToLiss switch
    {Signal::Seatbelt, "Rotate/aircraft/controls/seatbelts_lts", 1.0, false, kNoRule, -1},  // MD-11
    {Signal::Seatbelt, "Rotate/md80/systems/seatbelts_switch", 1.0, false, kNoRule, -1},    // MD-80
    // FlightFactor 777. Two names, both real: the first is what a community
    // script that supports two dozen aeroplanes reads on the 777-200ER v2, the
    // second is the switch itself. Both are animated by the aeroplane's own
    // objects, so neither is a guess any more; whichever exists wins and the log
    // says which.
    {Signal::Seatbelt, "1-sim/anim/seatbeltLight", 2.0, false, AutoRuleTag::SignThenFlaps, 1},
    {Signal::Seatbelt, "1-sim/ckpt/passSignsSeatbeltsSwitch/anim", 2.0, false,
     AutoRuleTag::SignThenFlaps, 1},
    {Signal::Seatbelt, "laminar/A333/switches/fasten_seatbelts", 2.0, false,
     AutoRuleTag::SignThenFlaps, 1},
    {Signal::Seatbelt, "laminar/B747/safety/seat_belts/sel_dial_pos", 2.0, false,
     AutoRuleTag::SignThenFlaps, 1},
    {Signal::Seatbelt, "CL650/overhead/signs/seatbelt_value", 1.0, false, kNoRule, -1},
    {Signal::Seatbelt, "CL650/overhead/signs/seatbelt", 1.0, false, kNoRule, -1},
    {Signal::Seatbelt, "SSG/EJET/SIGNS/fasten_belts_sw", 1.0, false, kNoRule, -1},
    {Signal::Seatbelt, "ssg/PASS/passenger_signal_sw", 1.0, false, kNoRule, -1},
    {Signal::Seatbelt, "aero787/cockpit/overhead/switches/seatbelts", 1.0, false, kNoRule, -1},
    {Signal::Seatbelt, "B742/OVHD/fasten_belts", 1.0, false, kNoRule, -1},
    {Signal::Seatbelt, "FJS/727/lights/FastenBeltsSwitch", 1.0, false, kNoRule, -1},
    {Signal::Seatbelt, "sim/custom/switchers/ovhd/sign_belts", 1.0, false, kNoRule, -1},  // Tu-154

    // ------------------------------------------------- distance to destination
    // The stock FMS is empty on every add-on with a real FMC: a Zibo's route
    // lives in the Zibo. Where the aeroplane publishes the remaining distance
    // itself, that is the only honest source for "half the route flown".
    {Signal::RouteDistance, "laminar/B738/FMS/dist_dest", 0.0, false, kNoRule, -1},
};

// What X-Plane publishes whatever is loaded. Kept apart from the list above on
// purpose, and marked provisional when bound: these names ALWAYS resolve, so
// finding one proves nothing about whether anything drives it.
const Candidate kStockCandidates[] = {
    {Signal::Beacon, "sim/cockpit2/switches/beacon_on", 1.0, false, kNoRule, -1},
    {Signal::NavLights, "sim/cockpit2/switches/navigation_lights_on", 1.0, false, kNoRule, -1},
    {Signal::Strobe, "sim/cockpit2/switches/strobe_lights_on", 1.0, false, kNoRule, -1},
    {Signal::LandingLight, "sim/cockpit2/switches/landing_lights_on", 1.0, false, kNoRule, -1},
    {Signal::TaxiLight, "sim/cockpit2/switches/taxi_light_on", 1.0, false, kNoRule, -1},
    {Signal::Battery, "sim/cockpit2/electrical/battery_on", 1.0, false, kNoRule, -1},
    {Signal::Parkbrake, "sim/flightmodel/controls/parkbrake", 0.5, false, kNoRule, -1},
    // Seat belt has two stock sources and a rule of its own; see seatbeltTri().
    {Signal::Seatbelt, kSeatbeltAnnunciator, 1.0, false, kNoRule, -1},
};

struct SignalName {
    const char* id;
    const char* title;
};

// Panel order, and the order of the lines in the log. Departure first, because
// that is the half that goes quiet when a trigger is missing.
const SignalName kNames[static_cast<int>(Signal::Count)] = {
    {"beacon", "Маяк"},
    {"nav", "АНО"},
    {"strobe", "Стробы"},
    {"landing", "Посадочные фары"},
    {"taxi", "Рулёжные фары"},
    {"logo", "Подсветка киля"},
    {"battery", "Батарея"},
    {"parkbrake", "Стояночный тормоз"},
    {"seatbelt", "Табло ремней"},
    {"route_distance", "До точки назначения"},
};

Signal signalById(const std::string& id) {
    for (int i = 0; i < static_cast<int>(Signal::Count); ++i) {
        if (id == kNames[i].id) {
            return static_cast<Signal>(i);
        }
    }
    return Signal::Count;
}

std::string formatNumber(double value) {
    char text[32];
    if (std::fabs(value - std::floor(value + 0.5)) < 0.001) {
        std::snprintf(text, sizeof(text), "%d", static_cast<int>(std::floor(value + 0.5)));
    } else {
        std::snprintf(text, sizeof(text), "%.2f", value);
    }
    return text;
}

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
    engineCount_ = find("sim/aircraft/engine/acf_num_engines");
    enginesRunning_ = find("sim/flightmodel/engine/ENGN_running");
    localTime_ = find("sim/time/local_time_sec");
    latitude_ = find("sim/flightmodel/position/latitude");
    longitude_ = find("sim/flightmodel/position/longitude");

    // What a switch in AUTO defers to, and what the stock seat belt fallback
    // reads. These are consulted by the AUTO rule whatever the sign is bound to.
    seatbeltSign_ = find(kSeatbeltAnnunciator);
    seatbeltStock2_ = find("sim/cockpit2/switches/fasten_seat_belts");
    seatbeltStock1_ = find("sim/cockpit/switches/fasten_seat_belts");
    // Commanded, not the surfaces: the crew's AUTO decision follows the lever.
    flapRatio_ = find("sim/cockpit2/controls/flap_ratio");
    gearHandle_ = find("sim/cockpit2/controls/gear_handle_down");
    paxOxygen_ = find("sim/operation/failures/rel_pass_o2_on");
    signSeenLit_ = false;
    loggedAuto_ = false;

    for (int i = 0; i < static_cast<int>(Signal::Count); ++i) {
        bindings_[i] = Binding();
    }
    for (int i = 0; i < static_cast<int>(Signal::Count); ++i) {
        const Signal signal = static_cast<Signal>(i);
        bindSignal(signal, signal == Signal::Seatbelt ? seatbeltOverride : std::string(), true);
    }
}

bool SimState::bindSignal(Signal signal, const std::string& override, bool announce) {
    Binding& b = slot(signal);
    const std::string was = b.name;
    const bool wasFromAircraft = b.fromAircraft;

    Binding fresh;

    // 1. What the user asked for by hand, and what signals.ini says for this
    //    aeroplane. Both are the person telling us the answer; they win.
    const auto tryName = [&](const char* name, double on, bool atMost, AutoRuleTag rule,
                             int autoPos, bool fromAircraft) {
        if (fresh.bound()) {
            return;
        }
        XPLMDataRef ref = find(name);
        if (ref == nullptr) {
            return;
        }
        fresh.ref = ref;
        fresh.name = name;
        fresh.on = on;
        fresh.atMost = atMost;
        fresh.rule = static_cast<AutoRule>(rule);
        fresh.autoPos = autoPos;
        fresh.fromAircraft = fromAircraft;
        fresh.provisional = !fromAircraft;
    };

    if (!override.empty()) {
        tryName(override.c_str(), 1.0, false, kNoRule, -1, true);
        if (!fresh.bound() && announce) {
            log("datarefs: this aircraft has no '%s' - falling back to the known ones",
                override.c_str());
        }
    }
    for (const core::SignalOverride& entry : overrides_.forAircraft(icao_)) {
        if (fresh.bound()) {
            break;
        }
        if (signalById(entry.signal) != signal) {
            continue;
        }
        tryName(entry.dataref.c_str(), entry.on, entry.atMost, kNoRule, -1, true);
        if (fresh.bound() && announce) {
            log("datarefs: %s взят из signals.ini - %s", kNames[static_cast<int>(signal)].id,
                entry.dataref.c_str());
        }
    }

    // 2. The aeroplane's own names, most specific first.
    for (const Candidate& c : kAircraftCandidates) {
        if (fresh.bound()) {
            break;
        }
        if (c.signal == signal) {
            tryName(c.name, c.on, c.atMost, c.rule, c.autoPos, true);
        }
    }

    // 3. Nothing of the aeroplane's own: fall back to what X-Plane publishes,
    //    and remember that this proves nothing until it is seen to move.
    for (const Candidate& c : kStockCandidates) {
        if (fresh.bound()) {
            break;
        }
        if (c.signal == signal) {
            tryName(c.name, c.on, c.atMost, c.rule, c.autoPos, false);
        }
    }

    const bool changed = fresh.name != was || fresh.fromAircraft != wasFromAircraft;
    if (changed) {
        b = fresh;
    }
    if (announce || changed) {
        const int index = static_cast<int>(signal);
        if (!b.bound()) {
            log("triggers: %s - этот борт ничего такого не публикует", kNames[index].id);
        } else {
            log("triggers: %s = %s%s", kNames[index].id, b.name.c_str(),
                b.fromAircraft ? " (датареф борта)" : " (штатный - жду, пока он шевельнётся)");
        }
    }
    return changed;
}

bool SimState::anySignalPending() const {
    for (int i = 0; i < static_cast<int>(Signal::Count); ++i) {
        const Binding& b = bindings_[i];
        if (!b.bound() || !b.fromAircraft) {
            return true;
        }
    }
    return false;
}

bool SimState::retryUnbound(const std::string& seatbeltOverride) {
    bool changed = false;
    for (int i = 0; i < static_cast<int>(Signal::Count); ++i) {
        const Binding& b = bindings_[i];
        // Already reading something the aeroplane published: nothing better
        // exists, stop asking for this one.
        if (b.bound() && b.fromAircraft) {
            continue;
        }
        const Signal signal = static_cast<Signal>(i);
        if (bindSignal(signal, signal == Signal::Seatbelt ? seatbeltOverride : std::string(),
                       false)) {
            changed = true;
        }
    }
    return changed;
}

bool SimState::seatbeltIsFallback() const {
    const Binding& b = slot(Signal::Seatbelt);
    return !b.bound() || !b.fromAircraft;
}

std::string SimState::seatbeltDataref() const {
    const Binding& b = slot(Signal::Seatbelt);
    if (b.bound()) {
        return b.name;
    }
    if (seatbeltSign_ != nullptr || seatbeltStock2_ != nullptr || seatbeltStock1_ != nullptr) {
        return "штатные датарефы X-Plane";
    }
    return std::string();
}

bool SimState::hasLogoDataref() const { return slot(Signal::Logo).bound(); }

double SimState::sample(const Binding& b) const {
    if (!b.bound()) {
        return 0.0;
    }
    XPLMDataRef ref = static_cast<XPLMDataRef>(b.ref);
    const XPLMDataTypeID type = XPLMGetDataRefTypes(ref);
    double value = 0.0;
    if ((type & xplmType_Double) != 0) {
        value = XPLMGetDatad(ref);
    } else if ((type & xplmType_Float) != 0) {
        value = static_cast<double>(XPLMGetDataf(ref));
    } else {
        value = static_cast<double>(XPLMGetDatai(ref));
        // Some aeroplanes publish a switch position as a float and nothing else.
        // Asking such a dataref for an int gives a confident zero.
        if (value == 0.0 && (type & xplmType_Int) == 0) {
            value = static_cast<double>(XPLMGetDataf(ref));
        }
    }

    const bool on = b.atMost ? value <= b.on + 0.001 : value >= b.on - 0.001;
    if (!b.haveLast) {
        b.haveLast = true;
        b.lastValue = value;
        // Already lit when we first looked is evidence too: an aeroplane loaded
        // with the beacon on says just as much as one that has it switched on
        // while we watch.
        if (on) {
            b.everMeaningful = true;
        }
    } else if (std::fabs(value - b.lastValue) > 0.0005) {
        b.lastValue = value;
        b.everMoved = true;
        b.everMeaningful = true;
    }
    return value;
}

core::Tri SimState::triOf(Signal signal) const {
    const Binding& b = slot(signal);
    if (!b.bound()) {
        return core::Tri::Unknown;
    }
    const double value = sample(b);
    if (b.provisional && !b.everMeaningful) {
        // A stock dataref nobody has been seen to drive. Reporting "off" here is
        // the mistake this whole file is arranged around: it reads as a switch
        // that is off rather than as a question we cannot ask, and a phase gated
        // on it then waits for ever.
        return core::Tri::Unknown;
    }
    const bool on = b.atMost ? value <= b.on + 0.001 : value >= b.on - 0.001;
    return on ? core::Tri::On : core::Tri::Off;
}

core::Tri SimState::seatbeltTri() const {
    const Binding& b = slot(Signal::Seatbelt);

    // Nothing of the aeroplane's own was found. X-Plane's annunciator is the
    // better answer where it is driven at all, and the stock switch is what an
    // aeroplane without its own systems moves; either one saying "on" is on.
    if (!b.bound() || !b.fromAircraft) {
        const bool anySource = seatbeltSign_ != nullptr || seatbeltStock2_ != nullptr ||
                               seatbeltStock1_ != nullptr;
        if (!anySource) {
            return core::Tri::Unknown;
        }
        const bool lit = readInt(seatbeltSign_, 0) == 1 || readInt(seatbeltStock2_, 0) == 1 ||
                         readInt(seatbeltStock1_, 0) == 1;
        if (lit) {
            signSeenLit_ = true;
        }
        // Same rule as everywhere else: a stock sign that has never been seen
        // lit is not a sign that is off, it is a sign we cannot read. Otherwise
        // the first flight on a Zibo starts by announcing that the belts have
        // just gone out.
        if (!signSeenLit_) {
            return core::Tri::Unknown;
        }
        return lit ? core::Tri::On : core::Tri::Off;
    }

    double value = sample(b);
    if (b.autoPos < 0 || std::fabs(value - static_cast<double>(b.autoPos)) > 0.4) {
        loggedAuto_ = false;
        const bool on = b.atMost ? value <= b.on + 0.001 : value >= b.on - 0.001;
        return on ? core::Tri::On : core::Tri::Off;
    }

    // A switch in AUTO is not a state of the sign - it is the crew saying
    // "aeroplane, you decide". Reading the detent then answers the wrong
    // question: on a 777 above ten thousand feet the sign goes out while the
    // switch stays exactly where it was, which is precisely the transition this
    // plugin exists to announce.
    //
    // Which way the aeroplane decides is not ours to invent. The annunciator is
    // used where it is genuinely driven - and "genuinely" means it has been seen
    // lit at least once this flight, because on a Zibo it exists, reads zero for
    // the whole flight and would report a sign that never lights. Everything
    // else follows the 737's own rule, taken from its cabin logic: the sign is
    // lit with flaps out or the gear down, which is departure and arrival.
    const bool signLit = readInt(seatbeltSign_, 0) == 1;
    if (signLit) {
        signSeenLit_ = true;
    }
    if (b.rule == AutoRule::SignThenFlaps && signSeenLit_) {
        if (!loggedAuto_) {
            loggedAuto_ = true;
            log("datarefs: seatbelt switch is in AUTO - the sign is read from %s",
                kSeatbeltAnnunciator);
        }
        return signLit ? core::Tri::On : core::Tri::Off;
    }

    // Masks out means the sign is on whatever the crew asked for.
    if (readInt(paxOxygen_, 0) == 6) {
        return core::Tri::On;
    }
    const bool flapsOut = readFloat(flapRatio_, 0.0f) > 0.01f;
    const bool gearDown = readInt(gearHandle_, 0) == 1;
    if (!loggedAuto_) {
        loggedAuto_ = true;
        log("datarefs: seatbelt switch is in AUTO - the aircraft decides, so the sign is taken "
            "from flaps and gear (%s never lit here)",
            kSeatbeltAnnunciator);
    }
    return (flapsOut || gearDown) ? core::Tri::On : core::Tri::Off;
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

std::vector<SignalReport> SimState::signalReports() const {
    std::vector<SignalReport> out;
    out.reserve(static_cast<std::size_t>(Signal::Count));
    for (int i = 0; i < static_cast<int>(Signal::Count); ++i) {
        const Signal signal = static_cast<Signal>(i);
        const Binding& b = bindings_[i];
        SignalReport row;
        row.id = kNames[i].id;
        row.title = kNames[i].title;
        row.bound = b.bound();
        row.fromAircraft = b.fromAircraft;
        row.dataref = b.name;
        if (b.bound()) {
            row.value = sample(b);
        }
        row.everMoved = b.everMoved;

        if (signal == Signal::Seatbelt) {
            row.reading = seatbeltTri();
            if (!b.bound() || !b.fromAircraft) {
                row.dataref = seatbeltDataref();
                row.note = signSeenLit_ ? "штатное табло, оно хотя бы раз загоралось"
                                        : "штатное табло, ни разу не загоралось - читаю как «не знаю»";
            } else if (b.autoPos >= 0) {
                row.note = "трёхпозиционный: 0 выкл, AUTO, вкл";
            }
        } else if (signal == Signal::RouteDistance) {
            row.reading = row.bound && row.value > 0.1 ? core::Tri::On : core::Tri::Unknown;
            row.note = row.bound ? "морских миль по счислению борта"
                                 : "борт не публикует - беру из штатного плана полёта";
        } else {
            row.reading = triOf(signal);
            if (b.bound() && !b.fromAircraft && !b.everMeaningful) {
                row.note = "штатный датареф, ни разу не двигался - считаю, что борт его не пишет";
            } else if (b.bound() && b.atMost) {
                row.note = "перевёрнутый: включено - это " + formatNumber(b.on) + " и ниже";
            }
        }
        out.push_back(std::move(row));
    }
    return out;
}

std::vector<std::string> SimState::signalLogLines() const {
    std::vector<std::string> out;
    for (const SignalReport& row : signalReports()) {
        std::string line = "triggers: ";
        line += row.id;
        line += " = ";
        line += row.dataref.empty() ? std::string("(нет)") : row.dataref;
        line += row.bound ? (row.fromAircraft ? " [борт]" : " [штатный]") : " [не найден]";
        if (row.bound) {
            line += " знач " + formatNumber(row.value);
            line += row.everMoved ? ", двигался" : ", не двигался";
        }
        line += " -> ";
        line += row.reading == core::Tri::On      ? "вкл"
                : row.reading == core::Tri::Off   ? "выкл"
                                                  : "не знаю";
        if (!row.note.empty()) {
            line += " (" + row.note + ")";
        }
        out.push_back(std::move(line));
    }
    return out;
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

    s.beacon = triOf(Signal::Beacon);
    s.navLights = triOf(Signal::NavLights);
    s.strobe = triOf(Signal::Strobe);
    s.landingLight = triOf(Signal::LandingLight);
    s.taxiLight = triOf(Signal::TaxiLight);
    s.logo = triOf(Signal::Logo);
    s.battery = triOf(Signal::Battery);
    s.parkbrake = triOf(Signal::Parkbrake);
    s.seatbelt = seatbeltTri();

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
//
// Before that, though: the aeroplane's own figure. An add-on with a real FMC
// leaves the stock FMS empty, so on exactly the aircraft people fly long routes
// in, the API below finds nothing at all.
void SimState::readRoute(core::Snapshot* s) const {
    const Binding& own = slot(Signal::RouteDistance);
    if (own.bound()) {
        const double nm = sample(own);
        // Zero is "no route entered", not "we have arrived": the FMC publishes
        // zero all the way through the turnaround.
        if (nm > 0.1) {
            s->routeDistanceKnown = true;
            s->routeDistanceNm = nm;
            return;
        }
    }

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
