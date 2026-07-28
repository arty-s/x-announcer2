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

// The canonical events, exactly as 1.x knows them.
const char* const kEvents[] = {
    "BoardingWelcome", "BoardingWelcomePilot", "BoardingStarted", "BoardingMusic",
    "BoardingComplete", "DepartureDelayed", "ArmDoors", "PreSafetyBriefing",
    "SafetyBriefing", "CabinDimTakeoff", "CrewSeatsTakeoff", "CallCabinSecureTakeoff",
    "AfterTakeoff", "TopOfClimbPilot", "FastenSeatbelt", "Turbulence",
    "TopOfDescentPilot", "DescentSeatbelts", "CabinDimLanding", "BeforeLanding",
    "CrewSeatsLanding", "CallCabinSecureLanding", "AfterLanding", "AfterLandingMusic",
    "DisarmDoors", "DisembarkStarted", "LandingGreat", "LandingTerrible", "CabinNoise",
};

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string trim(const std::string& s) {
    const std::size_t first = s.find_first_not_of(" \t");
    if (first == std::string::npos) {
        return std::string();
    }
    const std::size_t last = s.find_last_not_of(" \t");
    return s.substr(first, last - first + 1);
}

const std::map<std::string, std::string>& eventsByKey() {
    static const std::map<std::string, std::string> table = [] {
        std::map<std::string, std::string> t;
        for (const char* name : kEvents) {
            t[lower(name)] = name;
        }
        // The spelling variants community packs actually ship, carried over
        // from 1.x where they were collected the hard way.
        t["fastenseatbelts"] = "FastenSeatbelt";
        t["cabindim"] = "CabinDimTakeoff";
        t["topofdecentpilot"] = "TopOfDescentPilot";
        t["welcomeaboard"] = "BoardingWelcome";
        return t;
    }();
    return table;
}

bool isAudioExtension(const std::string& ext) {
    const std::string e = lower(ext);
    return e == ".ogg" || e == ".mp3" || e == ".wav" || e == ".flac";
}

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
}

}  // namespace

std::string eventFromFilename(const std::string& filename) {
    const std::size_t dot = filename.find_last_of('.');
    if (dot == std::string::npos || !isAudioExtension(filename.substr(dot))) {
        return std::string();
    }
    std::string base = filename.substr(0, dot);

    // Strip [tags]: aircraft codes, time of day, variant numbers.
    std::string stripped;
    int depth = 0;
    for (const char c : base) {
        if (c == '[') {
            ++depth;
        } else if (c == ']') {
            if (depth > 0) {
                --depth;
            }
        } else if (depth == 0) {
            stripped.push_back(c);
        }
    }
    // Some packs write "AfterTakeoff.ogg[A359][2].ogg".
    const std::size_t stray = stripped.find_last_of('.');
    if (stray != std::string::npos && isAudioExtension(stripped.substr(stray))) {
        stripped = stripped.substr(0, stray);
    }
    stripped = trim(stripped);

    const auto& table = eventsByKey();
    auto it = table.find(lower(stripped));
    if (it != table.end()) {
        return it->second;
    }

    // "SafetyBriefing1" / "BoardingMusic 2" - a trailing variant number.
    std::size_t end = stripped.size();
    while (end > 0 && std::isdigit(static_cast<unsigned char>(stripped[end - 1]))) {
        --end;
    }
    if (end < stripped.size()) {
        it = table.find(lower(trim(stripped.substr(0, end))));
        if (it != table.end()) {
            return it->second;
        }
    }
    return std::string();
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
                const std::string event = eventFromFilename(fileEntry.path().filename().string());
                if (event.empty()) {
                    continue;
                }
                // First file wins, so a pack with variants is deterministic
                // between runs. Variant selection is 1.x behaviour still to be
                // ported.
                pack.events.emplace(event, fileEntry.path().string());
                ++files;
            }
        };
        absorb(packEntry.path());
        if (!locale.empty()) {
            absorb(packEntry.path() / locale);
            pack.language = locale;
            ++localised;
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

std::string FileSoundLibrary::pathFor(const std::string& event) const {
    const auto pack = packs_.find(pack_);
    if (pack == packs_.end()) {
        return std::string();
    }
    const auto file = pack->second.events.find(event);
    return file == pack->second.events.end() ? std::string() : file->second;
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
