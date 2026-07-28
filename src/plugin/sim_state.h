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
    void bind();

    core::Snapshot read() const;

    // Who we are flying, as opposed to how we are flying it. Read separately
    // because it only changes when the aircraft or the livery does.
    struct Identity {
        std::string liveryPath;
        std::string tailNumber;
        std::string aircraftFile;
        std::string description;
    };
    Identity readIdentity() const;

    // Which seat belt dataref was found, for the panel to show. Empty means the
    // aircraft publishes none, which is information, not a fault.
    const char* seatbeltDataref() const { return seatbeltName_; }
    bool hasLogoDataref() const { return logo_ != nullptr; }

private:
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

    void* seatbelt_ = nullptr;
    const char* seatbeltName_ = "";
    int seatbeltOn_ = 1;
};

}  // namespace xa
