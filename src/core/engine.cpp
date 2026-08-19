#include "core/engine.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace xa::core {
namespace {

constexpr double kAnnouncementGap = 0.6;

// How long a queued announcement stays relevant. A "cabin secure" call still
// waiting behind a long safety briefing must not play in the climb.
const std::map<std::string, double>& eventTtl() {
    static const std::map<std::string, double> ttl = {
        {"BoardingWelcome", 240.0},        {"BoardingComplete", 300.0},
        {"CrewSeatsTakeoff", 150.0},       {"CallCabinSecureTakeoff", 150.0},
        {"CabinDimTakeoff", 240.0},        {"FastenSeatbelt", 120.0},
        {"Turbulence", 120.0},             {"BeforeLanding", 120.0},
        {"CrewSeatsLanding", 180.0},       {"CallCabinSecureLanding", 180.0},
    };
    return ttl;
}

// Great-circle distance in nautical miles. A flat-earth approximation would be
// off by tens of miles on the routes where "half way" is worth announcing at all,
// and the whole point of the announcement is that the number is believable.
double distanceNm(double lat1, double lon1, double lat2, double lon2) {
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kEarthRadiusNm = 3440.065;
    const double toRad = kPi / 180.0;
    const double dLat = (lat2 - lat1) * toRad;
    const double dLon = (lon2 - lon1) * toRad;
    const double a = std::sin(dLat / 2.0) * std::sin(dLat / 2.0) +
                     std::cos(lat1 * toRad) * std::cos(lat2 * toRad) * std::sin(dLon / 2.0) *
                         std::sin(dLon / 2.0);
    return 2.0 * kEarthRadiusNm * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
}

// Where the cabin marks the progress of the flight. These are not settings: the
// fractions are written into the file names the packs ship (Qatar's pack carries
// CruiseElapsed50Percent and CruiseElapsed75Percent), so a configurable fraction
// would make the file itself say something untrue.
constexpr double kCruiseMarkFirst = 0.50;
constexpr double kCruiseMarkSecond = 0.75;

// Below this the marks are meaningless: on a 40 nm hop half the route is reached
// while the gear is still coming up.
constexpr double kCruiseMinNm = 150.0;

// Fast enough on the ground that nothing but a take-off roll is happening. Well
// above any taxi speed and well below rotation, so the call lands where the
// lights would have put it rather than at V1.
constexpr double kRollingKt = 40.0;

// How far there is left to fly. The aeroplane's own figure first: an add-on with
// a real FMC keeps the route inside itself and leaves X-Plane's FMS empty, so
// the great-circle sum below has nothing to work from on precisely the aircraft
// people fly long routes in.
double remainingNm(const Snapshot& s) {
    if (s.routeDistanceKnown) {
        return s.routeDistanceNm;
    }
    return distanceNm(s.lat, s.lon, s.destLat, s.destLon);
}

std::string formatFeet(double v) {
    // A vertical speed of -0.4 fpm printed as "-0", which reads as a value the
    // sign of which someone is meant to act on. Anything that rounds to zero is
    // zero, and zero has no sign.
    if (v > -0.5 && v < 0.5) {
        v = 0.0;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.0f", v);
    return buf;
}

}  // namespace

const char* intentKindName(Intent::Kind kind) {
    switch (kind) {
        case Intent::Kind::PlayAnnouncement: return "play";
        case Intent::Kind::StopAnnouncement: return "stop";
        case Intent::Kind::StartMusic:       return "music";
        case Intent::Kind::StopMusic:        return "music-stop";
        case Intent::Kind::PhaseChanged:     return "phase";
        case Intent::Kind::FlightReset:      return "reset";
        case Intent::Kind::Note:             return "note";
    }
    return "?";
}

std::string formatIntent(const Intent& intent) {
    char head[32];
    std::snprintf(head, sizeof(head), "%8.1f ", intent.simClock);
    std::string line = head;
    line += intentKindName(intent.kind);
    if (!intent.event.empty()) {
        line += " ";
        line += intent.event;
    }
    if (!intent.detail.empty()) {
        line += " (";
        line += intent.detail;
        line += ")";
    }
    return line;
}

bool aircraftPowered(const Snapshot& s, std::vector<std::string>* signsOn, bool* blind) {
    bool any = false;
    bool anyKnown = false;
    const auto add = [&](Tri value, const char* name) {
        if (known(value)) {
            anyKnown = true;
        }
        if (isOn(value)) {
            any = true;
            if (signsOn != nullptr) {
                signsOn->emplace_back(name);
            }
        }
    };
    add(s.battery, "battery");
    add(s.navLights, "nav");
    add(s.logo, "logo");
    add(s.taxiLight, "taxi");
    if (blind != nullptr) {
        *blind = !anyKnown;
    }
    // Not one of the four exists on this aeroplane. That is not "the aeroplane
    // is dead" - it is "we have no way to ask", and the two must not produce the
    // same answer. Answering "no power" here is what keeps a FlightFactor 777
    // parked in PREFLIGHT for a whole flight: boarding never opens, so nothing
    // that follows boarding is ever due, and the log stays clean while the cabin
    // stays silent. The panel says which of the two answers this is.
    return any || !anyKnown;
}

Engine::Engine(Config config, const SoundLibrary& library)
    : config_(config), library_(library) {
    f_.phaseSince = 0.0;
}

void Engine::emit(Intent::Kind kind, std::string event, std::string detail) {
    Intent intent;
    intent.kind = kind;
    intent.event = std::move(event);
    intent.detail = std::move(detail);
    intent.simClock = simClock_;
    intents_.push_back(std::move(intent));
}

void Engine::note(const char* fmt, ...) {
    char body[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(body, sizeof(body), fmt, args);
    va_end(args);
    emit(Intent::Kind::Note, std::string(), body);
}

std::vector<Intent> Engine::drainIntents() {
    std::vector<Intent> out;
    out.swap(intents_);
    return out;
}

void Engine::restartFlight(const std::string& reason) {
    resetFlight(reason, Phase::Preflight);
}

void Engine::startBoarding(const std::string& reason) {
    // A full reset, not just a phase change: pressed on the ground before
    // departure there is nothing to lose, and pressed at any other moment the
    // person is plainly saying "we are starting a new flight, boarding now".
    // Half-starting - new phase over an old flight's memory - would leave every
    // once() of the previous leg marked as already said.
    resetFlight(reason, Phase::Boarding);
    // resetFlight rebuilds FlightState, so lastWelcome is back at -1e9 and the
    // welcome is due on the next tick rather than one boarding_repeat later.
    once("BoardingStarted", reason);
}

void Engine::skipAnnouncement() {
    stopAnnouncement();
    // Clearing the gap as well, or the queue sits idle for the usual pause after
    // something the person deliberately cut short.
    gapUntil_ = wallClock_;
}

void Engine::resetFlight(const std::string& reason, Phase startPhase) {
    f_ = FlightState();
    f_.phase = startPhase;
    f_.phaseSince = simClock_;
    queue_.clear();
    stopAnnouncement();
    stopMusic();
    emit(Intent::Kind::FlightReset, phaseId(startPhase), reason);
}

void Engine::setPhase(Phase id, const std::string& reason) {
    if (f_.phase == id) {
        return;
    }
    f_.phase = id;
    f_.phaseSince = simClock_;
    emit(Intent::Kind::PhaseChanged, phaseId(id), reason);
}

bool Engine::once(const std::string& event, const std::string& reason) {
    if (f_.done.count(event) != 0) {
        return false;
    }
    f_.done[event] = simClock_;
    if (enqueue(event, reason)) {
        return true;
    }
    // Nothing to play: unblock whatever was waiting on this event, or the
    // briefing chain stalls forever on a pack that simply lacks the file.
    f_.ended[event] = simClock_;
    return false;
}

bool Engine::finished(const std::string& event, double extraDelay) const {
    const auto it = f_.ended.find(event);
    if (it == f_.ended.end()) {
        return false;
    }
    return simClock_ >= it->second + extraDelay;
}

bool Engine::enqueue(const std::string& event, const std::string& reason) {
    if (!config_.enabled) {
        return false;
    }
    if (!library_.has(event)) {
        note("%s: no sound file (%s)", event.c_str(), reason.c_str());
        return false;
    }
    QueueItem item;
    item.event = event;
    item.reason = reason;
    const auto ttl = eventTtl().find(event);
    if (ttl != eventTtl().end()) {
        item.hasExpiry = true;
        item.expires = simClock_ + ttl->second;
    }
    queue_.push_back(std::move(item));
    return true;
}

void Engine::stopAnnouncement() {
    if (!playing_) {
        return;
    }
    // Mark it finished, or everything waiting on this event - the cabin secure
    // calls, the briefing chain - stays blocked forever.
    f_.ended[playing_->event] = simClock_;
    emit(Intent::Kind::StopAnnouncement, playing_->event, "stopped");
    playing_.reset();
}

void Engine::startMusic(const std::string& event) {
    if (music_ && music_->event == event) {
        return;
    }
    if (!library_.has(event)) {
        return;
    }
    stopMusic();
    Music m;
    m.event = event;
    m.startedWall = wallClock_;
    m.duration = library_.duration(event);
    music_ = m;
    emit(Intent::Kind::StartMusic, event, std::string());
}

void Engine::stopMusic() {
    if (!music_) {
        return;
    }
    emit(Intent::Kind::StopMusic, music_->event, std::string());
    music_.reset();
}

void Engine::audioUpdate() {
    if (playing_ && wallClock_ >= playing_->startedWall + playing_->duration) {
        f_.ended[playing_->event] = simClock_;
        playing_.reset();
        gapUntil_ = wallClock_ + kAnnouncementGap;
    }

    // Throw away announcements that waited too long to still make sense.
    for (auto it = queue_.begin(); it != queue_.end();) {
        if (it->hasExpiry && simClock_ > it->expires) {
            note("dropped %s (too late)", it->event.c_str());
            f_.ended[it->event] = simClock_;
            it = queue_.erase(it);
        } else {
            ++it;
        }
    }

    if (!playing_ && !queue_.empty() && wallClock_ >= gapUntil_) {
        const QueueItem item = queue_.front();
        queue_.pop_front();
        Playing p;
        p.event = item.event;
        p.startedWall = wallClock_;
        p.duration = library_.duration(item.event);
        playing_ = p;
        emit(Intent::Kind::PlayAnnouncement, item.event, item.reason);
    }

    // Background tracks loop for as long as the phase that started them wants
    // them. There used to be a cap of six, inherited from 1.x, where it existed
    // to bound a FlyWithLua leak rather than to say anything about boarding -
    // and its real effect was that music stopped mid-boarding for no reason the
    // listener could hear. Every track here is already stopped by something:
    // boarding music by the beacon, cabin noise by touching down.
    if (music_ && wallClock_ >= music_->startedWall + music_->duration) {
        music_->startedWall = wallClock_;
    }
}

void Engine::frame(const Snapshot& s, double simDt, double wallDt) {
    const bool nowFrozen = s.frozen();
    if (nowFrozen != frozen_) {
        note(nowFrozen ? "clock frozen" : "clock running again");
        frozen_ = nowFrozen;
    }
    if (frozen_) {
        return;
    }

    // The same sanity window 1.x used: a negative or huge delta means the sim
    // jumped (loading, teleport) and must not be integrated.
    if (simDt > 0.0 && simDt < 5.0) {
        simClock_ += simDt;
    }
    if (wallDt > 0.0 && wallDt < 30.0) {
        wallClock_ += wallDt;
    }

    // Touchdown vertical speed has to be sampled at frame rate: by the time the
    // 1 Hz tick notices the wheels are down, the number is long gone.
    if (!s.onGround) {
        f_.lastVs = s.vsFpm;
        const double g = std::fabs(s.gNormal - 1.0);
        if (g > f_.turbPeak) {
            f_.turbPeak = g;
        }
        f_.wasAirborne = true;
    } else if (f_.wasAirborne) {
        f_.wasAirborne = false;
        f_.touchdownFpm = f_.lastVs;
        f_.touchdownAt = simClock_;
        note("touchdown at %.0f fpm", f_.lastVs);
    }

    if (++frameAccum_ >= 15) {
        frameAccum_ = 0;
        audioUpdate();
    }
}

void Engine::resyncPhase(const Snapshot& s) {
    // The sim can drop the aircraft somewhere the machine cannot reach on its
    // own: teleport to a gate from the map, "start a flight here", a repaired
    // crash. Without this the announcer sits in Cruise forever and says nothing.
    const char* wrong = nullptr;
    if (isAirbornePhase(f_.phase) && s.onGround && s.allEnginesOff() && s.gsKt < 1.0) {
        wrong = "parked while the phase says airborne";
    } else if (isGroundPhase(f_.phase) && !s.onGround && s.aglFt > 3000.0) {
        wrong = "airborne while the phase says on the ground";
    }

    if (wrong == nullptr) {
        f_.resyncSince.reset();
        return;
    }
    if (!f_.resyncSince) {
        f_.resyncSince = simClock_;
    }
    if (simClock_ - *f_.resyncSince < 20.0) {
        return;
    }

    if (s.onGround) {
        resetFlight(wrong, Phase::Preflight);
    } else {
        resetFlight(wrong, Phase::Cruise);
        f_.done["BoardingWelcome"] = simClock_;
        f_.done["SafetyBriefing"] = simClock_;
        f_.done["AfterTakeoff"] = simClock_;
    }
}

void Engine::tick(const Snapshot& s) {
    if (frozen_) {
        return;
    }
    resyncPhase(s);
    if (config_.enabled) {
        stateMachine(s);
    }
}

void Engine::stateMachine(const Snapshot& s) {
    // ------------------------------------------------------------------ ground
    if (f_.phase == Phase::Preflight) {
        const bool powered = aircraftPowered(s);
        if (s.onGround && s.allEnginesOff() && !isOn(s.beacon) && s.gsKt < 1.0) {
            if (config_.autoBoarding && powered) {
                setPhase(Phase::Boarding, "cabin ready");
                once("BoardingStarted", "cabin ready");
            }
        }
        // Either sign of a departure, not both. This used to be an AND, and on
        // an aeroplane with no stock beacon that AND could never be true: the
        // engines could be running, the aeroplane taxiing, and the machine still
        // sat in PREFLIGHT waiting for a lamp nobody drives. The two conditions
        // mean the same thing anyway - somebody is leaving - and BOARDING has
        // read them as OR since 1.x.
        if (s.onGround && (s.anyEngine() || isOn(s.beacon))) {
            // loaded with the engines already running
            setPhase(Phase::Pushback, "engines already running");
        } else if (!s.onGround) {
            setPhase(Phase::Cruise, "loaded airborne");
            f_.done["BoardingWelcome"] = simClock_;
            f_.done["SafetyBriefing"] = simClock_;
            f_.done["AfterTakeoff"] = simClock_;
        }
    }

    if (f_.phase == Phase::Boarding) {
        if (simClock_ - f_.lastWelcome >= config_.boardingRepeat) {
            if (enqueue("BoardingWelcome", "boarding")) {
                f_.lastWelcome = simClock_;
                f_.done["BoardingWelcome"] = simClock_;
                if (config_.pilotWelcome && f_.done.count("BoardingWelcomePilot") == 0) {
                    f_.done["BoardingWelcomePilot"] = simClock_;
                    enqueue("BoardingWelcomePilot", "boarding");
                }
            } else {
                f_.lastWelcome = simClock_;  // do not retry every second
            }
        }

        // Standing here long enough that the cabin owes the passengers a word.
        // The clock is the phase's own, so a flight reset starts it over, and it
        // is the simulator's clock, so a paused sim does not tick towards it.
        if (config_.delayAfter > 0.0 && simClock_ - f_.phaseSince >= config_.delayAfter) {
            once("DepartureDelayed", "boarding running long");
        }

        if (config_.boardingMusic && !music_ && queue_.empty() && !playing_) {
            startMusic("BoardingMusic");
        }

        if (isOn(s.beacon) || s.anyEngine()) {
            stopMusic();
            // Say which of the two it was. "beacon on" printed over an aeroplane
            // that has no beacon is the kind of line that sends somebody looking
            // for a fault in the wrong place.
            const char* const why = isOn(s.beacon) ? "beacon on" : "engine started";
            once("BoardingComplete", why);
            setPhase(Phase::Pushback, why);
        }
    }

    if (f_.phase == Phase::Pushback) {
        if (config_.doorCalls && (s.anyEngine() || s.gsKt > 1.0)) {
            once("ArmDoors", "engines running");
        }
        if ((!config_.doorCalls || finished("ArmDoors")) && s.anyEngine()) {
            if (library_.has("PreSafetyBriefing")) {
                if (once("PreSafetyBriefing", "taxi out")) {
                    return;
                }
            }
            if (finished("PreSafetyBriefing") || !library_.has("PreSafetyBriefing")) {
                once("SafetyBriefing", "taxi out");
            }
        }
        if (config_.nightDim && s.isDark() && finished("SafetyBriefing", 10.0)) {
            once("CabinDimTakeoff", "night departure");
        }
        // Lights are how a crew says "we are going", and they are the earliest
        // signal there is - but they are also the signal an add-on is most
        // likely to keep to itself. The take-off roll is not: ground speed comes
        // from the flight model and exists on every aeroplane ever loaded. So
        // the lights stay as the nice early path and the roll is the guarantee,
        // otherwise "cabin crew, take your seats" is simply never said on a
        // FlightFactor.
        const bool linedUp = isOn(s.strobe) || isOn(s.landingLight);
        const bool rolling = s.gsKt > kRollingKt;
        if (s.onGround && s.anyEngine() && (linedUp || rolling)) {
            // The phase must move whether or not there is a file to play. Until
            // 2026-07-28 both this and the disembark transition were gated on
            // the announcement going on the air, so a pack missing one file
            // stalled the machine - harmless here, but fatal on the stand.
            const char* const why = linedUp ? "lined up" : "rolling";
            once("CrewSeatsTakeoff", why);
            setPhase(Phase::Takeoff, why);
        }
        if (!s.onGround) {
            setPhase(Phase::Takeoff, "airborne");
        }
    }

    if (f_.phase == Phase::Takeoff) {
        if (!s.onGround) {
            if (!f_.liftoffAt) {
                f_.liftoffAt = simClock_;
            }
            if (s.aglFt > 3000.0 || (simClock_ - *f_.liftoffAt) > 150.0) {
                once("AfterTakeoff", "airborne");
                setPhase(Phase::Climb, "airborne");
            }
        }
    }

    if (f_.phase == Phase::Climb) {
        if (std::fabs(s.vsFpm) < 350.0 && s.altFt > 15000.0) {
            if (!f_.levelSince) {
                f_.levelSince = simClock_;
            }
            if (simClock_ - *f_.levelSince > 25.0) {
                once("TopOfClimbPilot", "top of climb");
                setPhase(Phase::Cruise, "levelled off");
            }
        } else {
            f_.levelSince.reset();
        }
        if (s.altFt < 10000.0 && s.vsFpm < -400.0) {
            setPhase(Phase::Descent, "descending below 10000 ft");
        }
    }

    // The route is measured once, as soon as we are airborne and there is a plan
    // to measure. Measuring on the ground would be wrong for a taxi of any
    // length, and re-measuring in the air would let an edited plan move the
    // finish line under an announcement already made.
    if (!s.onGround && s.haveRoute() && !f_.routeTotalNm) {
        f_.routeTotalNm = remainingNm(s);
        note("route: %.0f nm to the last point of the plan%s", *f_.routeTotalNm,
             s.routeDistanceKnown ? " (по счислению самого борта)" : "");
    }

    if (f_.phase == Phase::Cruise) {
        // How much of the route is behind us. Both marks are guarded by once(),
        // so a flight that crosses three quarters while paused still says it
        // exactly one time - and a jump straight past both says only the later
        // one, because announcing "half way" after three quarters is a lie.
        if (s.haveRoute() && f_.routeTotalNm && *f_.routeTotalNm >= kCruiseMinNm) {
            const double remaining = remainingNm(s);
            const double flown = 1.0 - remaining / *f_.routeTotalNm;
            if (flown >= kCruiseMarkSecond) {
                once("CruiseElapsed75Percent", "three quarters of the route flown");
            } else if (flown >= kCruiseMarkFirst) {
                once("CruiseElapsed50Percent", "half the route flown");
            }
        } else if (f_.routeTotalNm && *f_.routeTotalNm < kCruiseMinNm && !f_.routeNoted) {
            f_.routeNoted = true;
            note("route: %.0f nm - too short to mark how much of it is flown",
                 *f_.routeTotalNm);
        }

        if (s.vsFpm < -500.0 && s.altFt > 20000.0) {
            if (!f_.descentSince) {
                f_.descentSince = simClock_;
            }
            if (simClock_ - *f_.descentSince > 25.0) {
                once("TopOfDescentPilot", "top of descent");
            }
        } else {
            f_.descentSince.reset();
        }
        if (s.altFt < 11000.0 && s.vsFpm < -300.0) {
            setPhase(Phase::Descent, "descending");
        }
    }

    // "Cabin secure" follow-ups are checked in every phase: the crew-seats call
    // can still be in the queue when the phase has already moved on.
    if (f_.done.count("CrewSeatsTakeoff") != 0 && finished("CrewSeatsTakeoff", 5.0)) {
        once("CallCabinSecureTakeoff", "cabin secure");
    }
    if (f_.done.count("CrewSeatsLanding") != 0 && finished("CrewSeatsLanding", 10.0)) {
        once("CallCabinSecureLanding", "cabin secure");
    }

    // Every move of the sign is written down, whatever comes of it. Without this
    // a report saying "I flipped the switch and nothing happened" cannot be told
    // apart from a wrong dataref: both look like a log with no seat belt line in
    // it. The rule below is untouched by this - it only gets explained.
    if (s.seatbelt != Tri::Unknown && s.seatbelt != f_.seatbeltNotePrev) {
        const Tri from = f_.seatbeltNotePrev;
        f_.seatbeltNotePrev = s.seatbelt;
        const char* const now = s.seatbelt == Tri::On ? "горит" : "погашено";
        if (from == Tri::Unknown) {
            note("табло ремней: %s (первое чтение)", now);
        } else if (s.seatbelt == Tri::Off) {
            note("табло ремней: погашено");
        } else if (s.onGround) {
            note("табло ремней: горит - объявления не будет, самолёт на земле");
        } else if (s.aglFt <= 5000.0) {
            note("табло ремней: горит - объявления не будет, %.0f футов над землёй из нужных 5000",
                 s.aglFt);
        } else if (simClock_ - f_.lastSeatbelt <= 180.0) {
            note("табло ремней: горит - объявления не будет, с прошлого прошло %.0f с из 180",
                 simClock_ - f_.lastSeatbelt);
        } else {
            note("табло ремней: горит");
        }
    }

    // The seat belt sign drives a PA in every airborne phase.
    if (!s.onGround && s.seatbelt != Tri::Unknown) {
        if (f_.seatbeltPrev == Tri::Off && s.seatbelt == Tri::On) {
            if (simClock_ - f_.lastSeatbelt > 180.0 && s.aglFt > 5000.0) {
                f_.lastSeatbelt = simClock_;
                const bool turbulent = f_.turbPeak > 0.35 && library_.has("Turbulence");
                enqueue(turbulent ? "Turbulence" : "FastenSeatbelt", "seatbelt sign");
                f_.turbPeak = 0.0;
            }
        }
        f_.seatbeltPrev = s.seatbelt;
    }

    if (f_.phase == Phase::Descent) {
        if (s.altFt < 10000.0) {
            once("DescentSeatbelts", "below 10000 ft");
            if (config_.nightDim && s.isDark()) {
                once("CabinDimLanding", "night arrival");
            }
        }
        if (s.aglFt < 5000.0 && s.vsFpm < -300.0) {
            once("BeforeLanding", "final");
        }
        if (s.aglFt < 3000.0 && s.vsFpm < -300.0) {
            once("CrewSeatsLanding", "approach");
            setPhase(Phase::Approach, "approach");
        }
    }

    if (f_.phase == Phase::Approach) {
        if (s.onGround && s.gsKt < 60.0) {
            once("AfterLanding", "vacated");
            setPhase(Phase::TaxiIn, "vacated");
        }
    }

    // The cabin's reaction to the touchdown, checked in any phase: a short
    // landing can put us in TAXI_IN well before the 8 second wait is over.
    if (config_.landingReaction && f_.touchdownAt && simClock_ - *f_.touchdownAt > 8.0) {
        const double fpm = std::fabs(f_.touchdownFpm.value_or(0.0));
        char reason[64];
        std::snprintf(reason, sizeof(reason), "touchdown %.0f fpm", f_.touchdownFpm.value_or(0.0));
        if (fpm < 180.0) {
            once("LandingGreat", reason);
        } else if (fpm > 400.0) {
            once("LandingTerrible", reason);
        }
    }

    if (f_.phase == Phase::TaxiIn) {
        // Parking brake OR simply standing still: not every operator sets the
        // brake on stand, some go straight to chocks.
        if (s.allEnginesOff() && (isOn(s.parkbrake) || s.gsKt < 1.0)) {
            if (config_.doorCalls) {
                once("DisarmDoors", "on stand");
            }
            if ((!config_.doorCalls || finished("DisarmDoors")) && !isOn(s.beacon)) {
                // This is the one that mattered: the turnaround reset lives in
                // DISEMBARK, so a pack without this file left every announcement
                // marked as already heard and the next flight silent.
                once("DisembarkStarted", "doors open");
                setPhase(Phase::Disembark, "doors open");
            }
        }
    }

    if (f_.phase == Phase::Disembark) {
        if (config_.boardingMusic && !music_ && queue_.empty() && !playing_ &&
            library_.has("AfterLandingMusic")) {
            startMusic("AfterLandingMusic");
        }
        if (simClock_ - f_.phaseSince > 120.0) {
            stopMusic();
            resetFlight("turnaround", Phase::Preflight);
        }
    }

    // Cabin ambience: airborne only, and it has to stop on the ground - otherwise
    // it keeps looping through the turnaround and blocks the arrival track.
    if (music_ && music_->event == "CabinNoise" && (s.onGround || !config_.cabinNoise)) {
        stopMusic();
    } else if (config_.cabinNoise && !s.onGround && !music_ && library_.has("CabinNoise")) {
        startMusic("CabinNoise");
    }
}

std::string Engine::nextPhaseLabel(const Snapshot&) const {
    switch (f_.phase) {
        case Phase::Preflight: return "Boarding";
        case Phase::Boarding:  return "Doors & safety";
        case Phase::Pushback:  return "Takeoff";
        case Phase::Takeoff:   return "Climb";
        case Phase::Climb:     return "Cruise";
        case Phase::Cruise:    return "Descent";
        case Phase::Descent:   return "Approach";
        case Phase::Approach:  return "After landing";
        case Phase::TaxiIn:    return "Disembarking";
        case Phase::Disembark: return "Preflight";
    }
    return "-";
}

std::vector<Condition> Engine::phaseConditions(const Snapshot& s) const {
    const auto yes = [](std::string label, bool met, std::string value = std::string()) {
        Condition c;
        c.label = std::move(label);
        c.met = met;
        c.value = std::move(value);
        return c;
    };

    switch (f_.phase) {
        case Phase::Preflight: {
            std::vector<std::string> on;
            bool blind = false;
            const bool powered = aircraftPowered(s, &on, &blind);
            std::string reading;
            if (blind) {
                // Met, but for a reason the person has to be told: this
                // aeroplane publishes none of the four, so the condition is
                // waved through rather than satisfied. Without the note the
                // panel would claim to have seen a battery that does not exist.
                reading = "this aircraft publishes none - not waiting for it";
            } else if (powered) {
                for (std::size_t i = 0; i < on.size(); ++i) {
                    reading += (i ? " + " : "") + on[i];
                }
            } else {
                // The value column carries the diagnosis: which of the four the
                // plugin can see, or - when it sees none - which ones it watches.
                // On an aircraft whose battery switch never reaches X-Plane that
                // is the difference between "flip the nav lights" and "the plugin
                // is broken".
                reading = "no battery/nav";
                if (known(s.logo)) {
                    reading += "/logo";
                }
                reading += "/taxi";
            }
            return {
                yes("on the ground", s.onGround),
                yes("engines off", s.allEnginesOff()),
                yes("beacon off", !isOn(s.beacon)),
                yes("battery or any light on", powered, reading),
            };
        }
        case Phase::Boarding:
            return {yes("beacon on or engine started", isOn(s.beacon) || s.anyEngine())};
        case Phase::Pushback:
            return {
                yes("engine running", s.anyEngine()),
                yes("strobes / landing lights or rolling",
                    isOn(s.strobe) || isOn(s.landingLight) || s.gsKt > kRollingKt,
                    formatFeet(s.gsKt) + " kt"),
            };
        case Phase::Takeoff:
            return {
                yes("airborne", !s.onGround),
                yes("3000 ft AGL", s.aglFt > 3000.0, formatFeet(s.aglFt)),
            };
        case Phase::Climb: {
            const double held = f_.levelSince ? (simClock_ - *f_.levelSince) : 0.0;
            char heldText[32];
            std::snprintf(heldText, sizeof(heldText), "%.0f s", held);
            return {
                yes("above 15 000 ft", s.altFt > 15000.0, formatFeet(s.altFt)),
                yes("levelling off", std::fabs(s.vsFpm) < 350.0, formatFeet(s.vsFpm) + " fpm"),
                yes("held 25 s", held > 25.0, heldText),
            };
        }
        case Phase::Cruise:
            return {
                yes("below 11 000 ft", s.altFt < 11000.0, formatFeet(s.altFt)),
                yes("descending", s.vsFpm < -300.0, formatFeet(s.vsFpm) + " fpm"),
            };
        case Phase::Descent:
            return {
                yes("below 3000 ft AGL", s.aglFt < 3000.0, formatFeet(s.aglFt)),
                yes("descending", s.vsFpm < -300.0, formatFeet(s.vsFpm) + " fpm"),
            };
        case Phase::Approach:
            return {
                yes("on the ground", s.onGround),
                yes("below 60 kt", s.gsKt < 60.0, formatFeet(s.gsKt) + " kt"),
            };
        case Phase::TaxiIn:
            return {
                yes("engines off", s.allEnginesOff()),
                yes("brake set or stopped", isOn(s.parkbrake) || s.gsKt < 1.0),
                yes("beacon off", !isOn(s.beacon)),
            };
        case Phase::Disembark: {
            const double left = 120.0 - (simClock_ - f_.phaseSince);
            char text[32];
            std::snprintf(text, sizeof(text), "%.0f s", left > 0.0 ? left : 0.0);
            return {yes("turnaround", left <= 0.0, text)};
        }
    }
    return {};
}

}  // namespace xa::core
