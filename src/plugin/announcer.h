// Everything the plugin owns, in one place: the core engine plus the three
// adapters that give it a world - datarefs in, sounds out, files on disk.
//
// The rule this file exists to keep: the engine decides, the adapters obey. No
// decision about what to announce may be taken here, or the offline bench stops
// being able to prove anything about it.
#pragma once

#include <memory>
#include <string>

#include "core/airline.h"
#include "core/engine.h"
#include "plugin/audio_player.h"
#include "plugin/file_library.h"
#include "plugin/sim_state.h"

namespace xa {

class Announcer {
public:
    Announcer();

    void start();
    void onAircraftLoaded();

    // Called every frame from the flight loop.
    void frame();

    core::Engine& engine() { return *engine_; }
    FileSoundLibrary& library() { return library_; }
    SimState& simState() { return sim_; }
    const core::Snapshot& lastSnapshot() const { return snapshot_; }
    AudioPlayer& player() { return player_; }

    const core::AirlineVerdict& airline() const { return airline_; }
    const core::AirlineIndex& airlines() const { return airlines_; }
    // Re-runs detection now. Also used by the panel when the pack was chosen by
    // hand and the user wants automatic detection back.
    void resolveAirline();

    // Off means the pack stays where the user put it.
    bool autoAirline = true;

    float volume = 0.85f;
    float musicVolume = 0.35f;
    float duck = 0.25f;  // music gain multiplier while an announcement plays

private:
    void execute(const core::Intent& intent);
    std::string findLibrary() const;

    SimState sim_;
    FileSoundLibrary library_;
    AudioPlayer player_;
    core::AirlineIndex airlines_;
    std::unique_ptr<core::Engine> engine_;

    core::AirlineVerdict airline_;
    std::string lastLivery_;
    core::Snapshot snapshot_;
    double lastSimTime_ = -1.0;
    double lastWallTime_ = -1.0;
    double tickAccumulator_ = 0.0;
    bool duckApplied_ = false;
};

}  // namespace xa
