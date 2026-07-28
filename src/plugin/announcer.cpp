#include "plugin/announcer.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>

#include "XPLMDataAccess.h"

#include "plugin/xa_log.h"
#include "plugin/xa_paths.h"

namespace fs = std::filesystem;

namespace xa {
namespace {

double wallSeconds() {
    using clock = std::chrono::steady_clock;
    static const clock::time_point start = clock::now();
    return std::chrono::duration<double>(clock::now() - start).count();
}

double simSeconds() {
    static XPLMDataRef ref = XPLMFindDataRef("sim/network/misc/network_time_sec");
    return ref == nullptr ? 0.0 : XPLMGetDataf(ref);
}

}  // namespace

Announcer::Announcer() {
    engine_ = std::make_unique<core::Engine>(core::Config(), library_);
}

std::string Announcer::findLibrary() const {
    // Artyom's library first, then the folder beside the plugin. Guessing is
    // fine as long as the log says which guess won - a plugin that is silent
    // because it looked in the wrong place must not look like a broken plugin.
    const std::string beside = pluginDir().empty() ? std::string() : pluginDir() + "/Sounds";
    for (const std::string& guess : {std::string(R"(D:\UA_Sounds)"), beside}) {
        std::error_code ec;
        if (!guess.empty() && fs::is_directory(guess, ec)) {
            return guess;
        }
    }
    return beside;
}

void Announcer::start() {
    sim_.bind();
    library_.scan(findLibrary());
    const int carriers = airlines_.loadFile(assetPath("airlines.txt"));
    if (carriers == 0) {
        log("airlines: table not found beside the plugin - detection will fall back to Default");
    } else {
        log("airlines: %d carriers loaded", carriers);
    }
    snapshot_ = sim_.read();
    resolveAirline();
}

void Announcer::onAircraftLoaded() {
    // Add-on datarefs come and go with the aircraft, so the bindings are redone
    // rather than trusted. The seat belt sign in particular is aircraft-specific.
    sim_.bind();
    lastLivery_.clear();  // force a fresh verdict for the new aeroplane
}

void Announcer::resolveAirline() {
    const SimState::Identity identity = sim_.readIdentity();
    lastLivery_ = identity.liveryPath;

    // Owning a pack is allowed to promote an explicit three-letter code, and
    // nothing more: it must never decide whether the airline was recognised.
    const core::HasPack hasPack = [this](const std::string& code) {
        for (const std::string& name : library_.packs()) {
            if (name.size() == code.size() &&
                std::equal(name.begin(), name.end(), code.begin(), [](char a, char b) {
                    return std::tolower(static_cast<unsigned char>(a)) ==
                           std::tolower(static_cast<unsigned char>(b));
                })) {
                return true;
            }
        }
        return false;
    };

    airline_ = airlines_.resolve(identity.liveryPath, identity.tailNumber, identity.aircraftFile,
                                 identity.description, hasPack);
    const std::string name = airlines_.nameOf(airline_.code);
    log("airline: %s%s - %s", airline_.code.c_str(),
        name.empty() ? "" : (" (" + name + ")").c_str(), airline_.source.c_str());
    library_.selectPackForAirline(airline_.code);
}

void Announcer::execute(const core::Intent& intent) {
    using Kind = core::Intent::Kind;
    switch (intent.kind) {
        case Kind::PlayAnnouncement: {
            const std::string path = library_.pathFor(intent.event);
            if (path.empty()) {
                log("play %s: the file went missing since the scan", intent.event.c_str());
                break;
            }
            player_.playAnnouncement(intent.event, path, volume);
            break;
        }
        case Kind::StopAnnouncement:
            player_.stopAnnouncement();
            break;
        case Kind::StartMusic: {
            const std::string path = library_.pathFor(intent.event);
            if (!path.empty()) {
                player_.playMusic(intent.event, path, musicVolume);
            }
            break;
        }
        case Kind::StopMusic:
            player_.stopMusic();
            break;
        case Kind::PhaseChanged:
            log("phase -> %s (%s)", intent.event.c_str(), intent.detail.c_str());
            break;
        case Kind::FlightReset:
            log("flight reset (%s) -> %s", intent.detail.c_str(), intent.event.c_str());
            break;
        case Kind::Note:
            log("%s", intent.detail.c_str());
            break;
    }
}

void Announcer::frame() {
    snapshot_ = sim_.read();

    // A livery change is the one identity event X-Plane does not announce with a
    // message, so it is watched. Checked per frame because reading a string
    // dataref is cheap and a stale airline is heard immediately.
    if (autoAirline) {
        const std::string livery = sim_.readIdentity().liveryPath;
        if (livery != lastLivery_) {
            resolveAirline();
        }
    }

    const double simNow = simSeconds();
    const double wallNow = wallSeconds();
    const double simDt = lastSimTime_ < 0.0 ? 0.0 : simNow - lastSimTime_;
    const double wallDt = lastWallTime_ < 0.0 ? 0.0 : wallNow - lastWallTime_;
    lastSimTime_ = simNow;
    lastWallTime_ = wallNow;

    engine_->frame(snapshot_, simDt, wallDt);

    // The state machine runs at 1 Hz, as it did in 1.x. Running it per frame
    // would change nothing except the load: every condition it tests is measured
    // in seconds.
    if (!snapshot_.frozen()) {
        tickAccumulator_ += wallDt;
        if (tickAccumulator_ >= 1.0) {
            tickAccumulator_ = 0.0;
            engine_->tick(snapshot_);
        }
    }

    for (const core::Intent& intent : engine_->drainIntents()) {
        execute(intent);
    }

    player_.update();

    // Duck the background under a PA. Applied on change only: setting a channel
    // volume every frame is pointless traffic to FMOD.
    const bool shouldDuck = player_.announcementActive();
    if (shouldDuck != duckApplied_) {
        duckApplied_ = shouldDuck;
        player_.setMusicGain(shouldDuck ? musicVolume * duck : musicVolume);
    }
}

}  // namespace xa
