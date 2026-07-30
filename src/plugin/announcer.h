// Everything the plugin owns, in one place: the core engine plus the three
// adapters that give it a world - datarefs in, sounds out, files on disk.
//
// The rule this file exists to keep: the engine decides, the adapters obey. No
// decision about what to announce may be taken here, or the offline bench stops
// being able to prove anything about it.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/airline.h"
#include "core/engine.h"
#include "core/settings.h"
#include "plugin/audio_player.h"
#include "plugin/file_library.h"
#include "plugin/sim_state.h"

namespace xa {

class Announcer {
public:
    Announcer();
    // Flushes settings the user changed in the last couple of seconds; without
    // it, quitting X-Plane right after moving a slider loses the change.
    ~Announcer();

    void start();
    void onAircraftLoaded();
    // The user's aeroplane was placed at a new airport (map teleport, "start a
    // flight here"). The flight in progress belongs to where it came from.
    void onRelocated();

    // Called every frame from the flight loop.
    void frame();

    core::Engine& engine() { return *engine_; }
    FileSoundLibrary& library() { return library_; }
    SimState& simState() { return sim_; }
    const core::Snapshot& lastSnapshot() const { return snapshot_; }
    AudioPlayer& player() { return player_; }

    // The ICAO type of the aeroplane in use, as X-Plane reports it (A20N, B738).
    const std::string& aircraftIcao() const { return aircraftIcao_; }

    const core::AirlineVerdict& airline() const { return airline_; }
    const core::AirlineIndex& airlines() const { return airlines_; }
    // Re-runs detection now. Also used by the panel when the pack was chosen by
    // hand and the user wants automatic detection back.
    void resolveAirline();

    core::Settings& settings() { return settings_; }
    const core::Settings& settings() const { return settings_; }

    // Re-reads config.ini from disk, throwing away unsaved changes.
    void loadSettings();
    // Hands the current settings to everything that acts on them: the engine,
    // the audio buses, the seat belt dataref, the library. Call after changing
    // anything in settings().
    void applySettings();
    void saveSettings();

    // The panel calls this after touching settings(): the change takes effect
    // now, the file is written a couple of seconds later. Writing on every frame
    // of a dragged slider would hammer the disk; waiting for the user to press a
    // button loses the change when the simulator goes down.
    void settingsChanged(bool rescanLibraryToo = false);

    // Scans the sound folder: the one named in the settings, or the standard
    // Sound_packs folder beside the plugin when that is left empty.
    void rescanLibrary();

    // The folder actually being read, and the standard one, for the panel to
    // show. Empty settings must still be able to say where the sounds go.
    std::string libraryDir() const;
    std::string defaultLibraryDir() const;

    // Points the library at the airline we detected, or at the pack the user
    // pinned by hand when airline_mode is manual.
    void applyPackChoice();

private:
    void execute(const core::Intent& intent);

    core::Settings settings_;
    SimState sim_;
    FileSoundLibrary library_;
    AudioPlayer player_;
    core::AirlineIndex airlines_;
    std::unique_ptr<core::Engine> engine_;

    core::AirlineVerdict airline_;
    std::string lastLivery_;
    // Read when the aeroplane loads rather than per frame: it cannot change
    // without a load, and it is a string dataref either way.
    std::string aircraftIcao_;
    core::Snapshot snapshot_;
    double lastSimTime_ = -1.0;
    double lastWallTime_ = -1.0;
    double tickAccumulator_ = 0.0;
    bool duckApplied_ = false;

    std::string appliedSeatbelt_;  // rebinding datarefs is only worth it on change
    bool settingsDirty_ = false;
    double settingsDirtySince_ = 0.0;
};

}  // namespace xa
