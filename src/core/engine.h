// The flight state machine and the announcement queue, ported from 1.x.
//
// Rules this file lives by:
//   * No XPLM, no FMOD, no filesystem, no networking, no clock of its own. Time
//     arrives as deltas; everything else arrives in a Snapshot.
//   * No std::rand, no wall-clock reads. Two runs of the same scenario must
//     produce byte-identical traces, or the bench is worthless.
//   * Ordered containers only. Iteration order is part of the output.
#pragma once

#include <deque>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/config.h"
#include "core/intent.h"
#include "core/library.h"
#include "core/phase.h"
#include "core/snapshot.h"

namespace xa::core {

// What the machine is waiting for, in the form the on-screen widget draws. It
// lives next to the machine on purpose: the bench flies a whole flight and
// checks the phase never advances while this still reports something unmet,
// which is what catches the widget drifting away from the logic.
struct Condition {
    std::string label;
    bool met = false;
    std::string value;
};

class Engine {
public:
    Engine(Config config, const SoundLibrary& library);

    // Per frame. Advances the clocks, samples touchdown rate and turbulence,
    // and services the audio queue on 1.x's cadence (every 15th frame).
    void frame(const Snapshot& s, double simDt, double wallDt);

    // ~1 Hz. Resynchronises the phase if the sim teleported the aeroplane, then
    // runs the state machine.
    void tick(const Snapshot& s);

    std::vector<Intent> drainIntents();

    Phase phase() const { return f_.phase; }
    double simClock() const { return simClock_; }
    double wallClock() const { return wallClock_; }

    bool isPlaying() const { return playing_.has_value(); }
    std::string playingEvent() const { return playing_ ? playing_->event : std::string(); }
    std::size_t queueSize() const { return queue_.size(); }
    bool musicPlaying() const { return music_.has_value(); }
    std::string musicEvent() const { return music_ ? music_->event : std::string(); }

    // Next phase name plus its unmet/met conditions, mirroring the machine.
    std::string nextPhaseLabel(const Snapshot& s) const;
    std::vector<Condition> phaseConditions(const Snapshot& s) const;

    Config& config() { return config_; }
    const Config& config() const { return config_; }

private:
    struct QueueItem {
        std::string event;
        std::string reason;
        bool hasExpiry = false;
        double expires = 0.0;
    };

    struct Playing {
        std::string event;
        double startedWall = 0.0;
        double duration = 0.0;
    };

    struct Music {
        std::string event;
        double startedWall = 0.0;
        double duration = 0.0;
        int loops = 0;
    };

    struct FlightState {
        Phase phase = Phase::Preflight;
        double phaseSince = 0.0;
        std::map<std::string, double> done;   // event -> sim time it was queued
        std::map<std::string, double> ended;  // event -> sim time playback ended
        bool boardingOpen = false;
        double lastWelcome = -1e9;
        Tri seatbeltPrev = Tri::Unknown;
        double lastSeatbelt = -1e9;
        std::optional<double> levelSince;
        std::optional<double> descentSince;
        std::optional<double> touchdownFpm;
        std::optional<double> touchdownAt;
        std::optional<double> liftoffAt;
        std::optional<double> resyncSince;
        double turbPeak = 0.0;
        bool wasAirborne = false;
        double lastVs = 0.0;
    };

    void emit(Intent::Kind kind, std::string event, std::string detail);
    void note(const char* fmt, ...);

    void resetFlight(const std::string& reason, Phase startPhase);
    void setPhase(Phase id, const std::string& reason);
    bool once(const std::string& event, const std::string& reason);
    bool finished(const std::string& event, double extraDelay = 0.0) const;
    bool enqueue(const std::string& event, const std::string& reason);
    void stopAnnouncement();
    void startMusic(const std::string& event);
    void stopMusic();
    void audioUpdate();
    void resyncPhase(const Snapshot& s);
    void stateMachine(const Snapshot& s);

    Config config_;
    const SoundLibrary& library_;

    double simClock_ = 0.0;
    double wallClock_ = 0.0;
    bool frozen_ = false;
    int frameAccum_ = 0;

    FlightState f_;
    std::deque<QueueItem> queue_;
    double gapUntil_ = 0.0;
    std::optional<Playing> playing_;
    std::optional<Music> music_;

    std::vector<Intent> intents_;
};

// "Is the aeroplane awake yet?" - any one of battery, nav, taxi or logo counts.
// Deliberately generous and deliberately not called cabin power: study-level
// add-ons run their own electrical system and many never drive X-Plane's generic
// battery dataref. On a ToLiss the battery reads off with the aircraft fully
// powered and the nav lights are what actually move.
bool aircraftPowered(const Snapshot& s, std::vector<std::string>* signsOn = nullptr);

}  // namespace xa::core
