#include "plugin/file_library.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

#include "audio/pcm.h"
#include "core/pack_layout.h"
#include "plugin/xa_log.h"

namespace fs = std::filesystem;

namespace xa {
namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
}

}  // namespace

const std::vector<std::string>& soundEventNames() {
    // Flight order, not alphabetical: the library table is read down the page
    // the way the flight is flown, so a gap in it is a gap in a leg.
    static const std::vector<std::string> names = {
        "BoardingStarted", "BoardingWelcome", "BoardingWelcomePilot", "BoardingMusic",
        "DepartureDelayed", "BoardingComplete", "ArmDoors", "PreSafetyBriefing",
        "SafetyBriefing", "CabinDimTakeoff", "CrewSeatsTakeoff", "CallCabinSecureTakeoff",
        "AfterTakeoff", "TopOfClimbPilot", "CruiseElapsed50Percent",
        "CruiseElapsed75Percent", "FastenSeatbelt", "Turbulence", "CabinNoise",
        "TopOfDescentPilot", "DescentSeatbelts", "CabinDimLanding", "BeforeLanding",
        "CrewSeatsLanding", "CallCabinSecureLanding", "LandingGreat", "LandingTerrible",
        "AfterLanding", "AfterLandingMusic", "DisarmDoors", "DisembarkStarted",
    };
    return names;
}

int FileSoundLibrary::scan(const std::string& root, const std::string& language) {
    packs_.clear();
    durations_.clear();
    root_ = root;
    language_ = language;

    std::error_code ec;
    if (!fs::is_directory(root, ec)) {
        log("library: '%s' is not a folder - no sounds will play", root.c_str());
        return 0;
    }

    int files = 0;
    int localised = 0;
    for (const auto& packEntry : fs::directory_iterator(root, ec)) {
        if (ec || !packEntry.is_directory()) {
            continue;
        }
        const std::string packName = packEntry.path().filename().string();

        // One level only, and only one language folder of it - as in 1.x. The
        // first scan here walked the whole tree, which quietly poured every
        // language of a multilingual pack into the same bucket and let the
        // filesystem decide which one was heard.
        std::vector<std::string> subfolders;
        for (const auto& entry : fs::directory_iterator(packEntry.path(), ec)) {
            if (!ec && entry.is_directory()) {
                subfolders.push_back(entry.path().filename().string());
            }
        }
        const std::string locale = core::chooseLocaleFolder(subfolders, language);

        Pack pack;
        // The pack's own folder first: with a variant of the same event in both
        // places the top-level file wins, which is what "first file wins" meant
        // before language folders existed.
        auto absorb = [&](const fs::path& dir) {
            for (const auto& fileEntry : fs::directory_iterator(dir, ec)) {
                if (ec || !fileEntry.is_regular_file()) {
                    continue;
                }
                const std::string name = fileEntry.path().filename().string();
                std::string event;
                core::SoundVariant variant;
                if (!core::parseSoundFile(name, &event, &variant)) {
                    continue;
                }
                // Every file is kept, tags and all. Which of them plays is
                // decided later, against the aeroplane and the clock.
                pack.events[event].push_back(Entry{fileEntry.path().string(), variant});
                ++files;
            }
        };
        absorb(packEntry.path());
        if (!locale.empty()) {
            absorb(packEntry.path() / locale);
            pack.language = locale;
            ++localised;
        }
        // Only now, and only if nothing was found: a pack that keeps its sounds
        // in a "Default" subfolder instead of its own folder. Reading it whenever
        // it existed would duplicate every announcement in the packs that carry
        // both, so an empty result is the whole condition.
        if (pack.events.empty()) {
            const std::string base = core::chooseBaseFolder(subfolders);
            if (!base.empty()) {
                absorb(packEntry.path() / base);
                if (!pack.events.empty()) {
                    log("library: пак '%s' держит звуки в подпапке '%s' - читаю её",
                        packName.c_str(), base.c_str());
                }
            }
        }

        if (!pack.events.empty()) {
            packs_.emplace(packName, std::move(pack));
        }
    }

    log("library: %zu packs, %d usable files in '%s'", packs_.size(), files, root.c_str());
    if (localised > 0) {
        log("library: %d packs have a '%s' language folder", localised, language.c_str());
    }
    if (!packs_.empty() && packs_.count(pack_) == 0) {
        pack_ = packs_.begin()->first;
        log("library: no pack chosen yet - standing on '%s' until the airline is known",
            pack_.c_str());
    }
    return static_cast<int>(packs_.size());
}

bool FileSoundLibrary::selectPack(const std::string& pack) {
    if (packs_.count(pack) == 0) {
        log("library: no pack called '%s'; still playing '%s'", pack.c_str(), pack_.c_str());
        return false;
    }
    pack_ = pack;
    durations_.clear();
    log("library: playing pack '%s' (%zu events)", pack_.c_str(), packs_.at(pack_).events.size());
    return true;
}

void FileSoundLibrary::selectPackForAirline(const std::string& icao) {
    // Pack folders are named after the airline, but nobody agrees on case.
    const std::string wanted = lower(icao);
    for (const auto& [name, _] : packs_) {
        if (lower(name) == wanted) {
            selectPack(name);
            return;
        }
    }
    for (const auto& [name, _] : packs_) {
        if (lower(name) == "default") {
            if (name != pack_) {
                log("library: no pack for %s - playing %s", icao.c_str(), name.c_str());
            }
            selectPack(name);
            return;
        }
    }
    log("library: no pack for %s and no Default pack either - staying on '%s'",
        icao.c_str(), pack_.c_str());
}

void FileSoundLibrary::setPlayContext(const core::PlayContext& context) {
    if (context.aircraft == context_.aircraft && context.daypart == context_.daypart) {
        return;
    }
    context_ = context;
    // The choice may now fall on a different file, so the remembered lengths
    // belong to nothing. Keeping them would time a night greeting by the length
    // of the day one.
    durations_.clear();
    log("library: choosing files for %s, %s",
        context_.aircraft.empty() ? "unknown aircraft" : context_.aircraft.c_str(),
        context_.daypart.c_str());
}

void FileSoundLibrary::nextVariantRound() {
    ++round_;
    durations_.clear();
}

void FileSoundLibrary::setDefaultFallback(bool allowed) {
    if (allowed == defaultFallback_) {
        return;
    }
    defaultFallback_ = allowed;
    // Half the events may now resolve to a different file, or to none.
    durations_.clear();
    log("library: недостающие объявления %s",
        allowed ? "берутся из Default" : "не подставляются - таких объявлений просто нет");
}

const FileSoundLibrary::Entry* FileSoundLibrary::pickFrom(const Pack& pack,
                                                          const std::string& event) const {
    const auto found = pack.events.find(event);
    if (found == pack.events.end() || found->second.empty()) {
        return nullptr;
    }
    std::vector<core::SoundVariant> variants;
    variants.reserve(found->second.size());
    for (const Entry& entry : found->second) {
        variants.push_back(entry.variant);
    }
    const int index = core::chooseVariant(variants, context_, round_);
    return index < 0 ? nullptr : &found->second[static_cast<std::size_t>(index)];
}

const FileSoundLibrary::Entry* FileSoundLibrary::resolve(const std::string& event,
                                                         std::string* fromPack) const {
    const auto pack = packs_.find(pack_);
    if (pack != packs_.end()) {
        if (const Entry* entry = pickFrom(pack->second, event)) {
            if (fromPack != nullptr) {
                *fromPack = pack->first;
            }
            return entry;
        }
    }
    if (!defaultFallback_) {
        return nullptr;
    }
    // The airline's own pack may cover only part of the flight, so Default
    // stands in for the rest - per event, as in 1.x, not per pack.
    for (const auto& [name, candidate] : packs_) {
        if (lower(name) != "default" || name == pack_) {
            continue;
        }
        if (const Entry* entry = pickFrom(candidate, event)) {
            if (fromPack != nullptr) {
                *fromPack = name;
            }
            return entry;
        }
    }
    return nullptr;
}

std::string FileSoundLibrary::pathFor(const std::string& event) const {
    const Entry* entry = resolve(event, nullptr);
    return entry == nullptr ? std::string() : entry->path;
}

std::vector<FileSoundLibrary::Coverage> FileSoundLibrary::coverage() const {
    std::vector<Coverage> rows;
    const auto selected = packs_.find(pack_);
    const Pack* fallbackPack = nullptr;
    for (const auto& [name, pack] : packs_) {
        if (lower(name) == "default") {
            fallbackPack = &pack;
            break;
        }
    }

    for (const std::string& event : soundEventNames()) {
        Coverage row;
        row.event = event;
        if (selected != packs_.end()) {
            const auto found = selected->second.events.find(event);
            if (found != selected->second.events.end()) {
                row.own = static_cast<int>(found->second.size());
            }
        }
        if (fallbackPack != nullptr) {
            const auto found = fallbackPack->events.find(event);
            if (found != fallbackPack->events.end()) {
                row.fallback = static_cast<int>(found->second.size());
            }
        }
        if (const Entry* entry = resolve(event, &row.source)) {
            row.file = entry->variant.name;
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

bool FileSoundLibrary::has(const std::string& event) const { return !pathFor(event).empty(); }

double FileSoundLibrary::duration(const std::string& event) const {
    const auto cached = durations_.find(event);
    if (cached != durations_.end()) {
        return cached->second;
    }
    const std::string path = pathFor(event);
    double seconds = 0.0;
    if (!path.empty()) {
        const std::vector<uint8_t> bytes = readFile(path);
        if (!audio::probeDuration(bytes.data(), bytes.size(), &seconds)) {
            log("library: cannot read the length of '%s' - treating it as silent", path.c_str());
            seconds = 0.0;
        }
    }
    durations_[event] = seconds;
    return seconds;
}

std::string FileSoundLibrary::packLanguage() const {
    const auto pack = packs_.find(pack_);
    return pack == packs_.end() ? std::string() : pack->second.language;
}

std::vector<std::string> FileSoundLibrary::packs() const {
    std::vector<std::string> names;
    names.reserve(packs_.size());
    for (const auto& [name, _] : packs_) {
        names.push_back(name);
    }
    return names;
}

int FileSoundLibrary::eventCount() const {
    const auto pack = packs_.find(pack_);
    return pack == packs_.end() ? 0 : static_cast<int>(pack->second.events.size());
}

}  // namespace xa
