#include "plugin/announcer.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "XPLMDataAccess.h"
#include "XPLMUtilities.h"

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

XPLMAudioBus busByName(const std::string& name) {
    if (name == "exterior") return xplm_AudioExteriorEnvironment;
    if (name == "ui") return xplm_AudioUI;
    if (name == "com1") return xplm_AudioRadioCom1;
    if (name == "com2") return xplm_AudioRadioCom2;
    if (name == "ground") return xplm_AudioGround;
    return xplm_AudioInterior;
}

}  // namespace

Announcer::Announcer() {
    engine_ = std::make_unique<core::Engine>(core::Config(), library_);
}

Announcer::~Announcer() {
    if (settingsDirty_) {
        saveSettings();
    }
}

std::vector<std::string> Announcer::libraryGuesses() const {
    // Where an MSFS Universal Announcer library usually ends up, same list as
    // 1.x. Only consulted when the configured folder holds no packs at all.
    char systemPath[1024] = {0};
    XPLMGetSystemPath(systemPath);
    std::vector<std::string> guesses = {R"(D:\UA_Sounds)", R"(C:\UA_Sounds)", R"(E:\UA_Sounds)"};
    if (systemPath[0] != '\0') {
        guesses.push_back(std::string(systemPath) + "UA_Sounds");
    }
    if (!pluginDir().empty()) {
        guesses.push_back(pluginDir() + "/Sounds");
    }
    return guesses;
}

void Announcer::loadSettings() {
    const std::string path = configPath();
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        // First run. Write the file out so there is something to edit, and say
        // where it is: a settings file nobody can find is a settings file
        // nobody uses.
        log("settings: no config.ini yet - writing defaults to '%s'", path.c_str());
        settings_ = core::Settings();
        saveSettings();
        return;
    }
    std::ostringstream text;
    text << file.rdbuf();

    std::vector<std::string> problems;
    settings_ = core::parseSettings(text.str(), &problems);
    log("settings: read '%s'", path.c_str());
    // Every complaint goes to the log on its own line. A settings file that is
    // half-understood must say which half.
    for (const std::string& problem : problems) {
        log("settings: %s", problem.c_str());
    }
}

void Announcer::saveSettings() {
    const std::string path = configPath();
    // Write beside the real file and move it into place, so a crash halfway
    // through cannot leave the user with an empty or half-written config.
    const std::string temporary = path + ".new";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            log("settings: cannot write '%s' - changes will be lost on exit", temporary.c_str());
            return;
        }
        file << core::writeSettings(settings_);
    }
    std::error_code ec;
    fs::rename(temporary, path, ec);
    if (ec) {
        log("settings: cannot replace '%s' (%s)", path.c_str(), ec.message().c_str());
        fs::remove(temporary, ec);
        return;
    }
    log("settings: saved");
}

void Announcer::applySettings() {
    engine_->config() = settings_.flight;
    player_.setBuses(busByName(settings_.announceBus), busByName(settings_.musicBus));
    player_.setMusicGain(static_cast<float>(settings_.musicVolume));
    duckApplied_ = false;  // recompute the ducking on the next frame
    // Only when it actually changed: this runs from a slider being dragged, and
    // re-looking-up every dataref per frame is work nobody asked for.
    if (settings_.seatbeltDref != appliedSeatbelt_) {
        appliedSeatbelt_ = settings_.seatbeltDref;
        sim_.bind(settings_.seatbeltDref);
    }
}

void Announcer::settingsChanged(bool rescanLibraryToo) {
    applySettings();
    if (rescanLibraryToo) {
        rescanLibrary();
        applyPackChoice();
    }
    settingsDirty_ = true;
    settingsDirtySince_ = wallSeconds();
}

void Announcer::rescanLibrary() {
    if (!settings_.library.empty()) {
        library_.scan(settings_.library, settings_.language);
    }
    if (library_.packs().empty() && settings_.autoFind) {
        for (const std::string& guess : libraryGuesses()) {
            std::error_code ec;
            if (!fs::is_directory(guess, ec)) {
                continue;
            }
            if (library_.scan(guess, settings_.language) > 0) {
                log("settings: found a sound library at '%s' - remembering it", guess.c_str());
                settings_.library = guess;
                saveSettings();
                break;
            }
        }
    }
    if (library_.packs().empty()) {
        log("library: no packs anywhere - set 'library' in config.ini to the folder "
            "that holds them");
    }
}

void Announcer::start() {
    loadSettings();
    // The first bind happens here rather than in applySettings(), which only
    // rebinds on change and would otherwise skip it when no override is set.
    appliedSeatbelt_ = settings_.seatbeltDref;
    sim_.bind(appliedSeatbelt_);
    applySettings();
    rescanLibrary();
    const int carriers = airlines_.loadFile(assetPath("airlines.txt"));
    if (carriers == 0) {
        log("airlines: table not found beside the plugin - detection will fall back to Default");
    } else {
        log("airlines: %d carriers loaded", carriers);
    }
    snapshot_ = sim_.read();
    applyPackChoice();
}

void Announcer::onAircraftLoaded() {
    // Add-on datarefs come and go with the aircraft, so the bindings are redone
    // rather than trusted. The seat belt sign in particular is aircraft-specific.
    sim_.bind(settings_.seatbeltDref);
    lastLivery_.clear();  // force a fresh verdict for the new aeroplane
}

void Announcer::applyPackChoice() {
    if (settings_.autoAirline()) {
        resolveAirline();
    } else {
        library_.selectPackForAirline(settings_.airlineManual);
    }
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
            player_.playAnnouncement(intent.event, path, static_cast<float>(settings_.volume));
            break;
        }
        case Kind::StopAnnouncement:
            player_.stopAnnouncement();
            break;
        case Kind::StartMusic: {
            const std::string path = library_.pathFor(intent.event);
            if (!path.empty()) {
                player_.playMusic(intent.event, path, static_cast<float>(settings_.musicVolume));
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
    if (settings_.autoAirline()) {
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

    // Settings touched in the panel are written once the user stops fiddling.
    if (settingsDirty_ && wallNow - settingsDirtySince_ > 2.0) {
        settingsDirty_ = false;
        saveSettings();
    }

    // Duck the background under a PA. Applied on change only: setting a channel
    // volume every frame is pointless traffic to FMOD.
    const bool shouldDuck = player_.announcementActive();
    if (shouldDuck != duckApplied_) {
        duckApplied_ = shouldDuck;
        const float level = static_cast<float>(settings_.musicVolume);
        player_.setMusicGain(shouldDuck ? level * static_cast<float>(settings_.duck) : level);
    }
}

}  // namespace xa
