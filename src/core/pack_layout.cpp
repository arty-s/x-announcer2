#include "core/pack_layout.h"

#include <algorithm>
#include <cctype>
#include <map>

namespace xa::core {
namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

bool isAlpha(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; }

std::string trim(const std::string& s) {
    const std::size_t first = s.find_first_not_of(" \t");
    if (first == std::string::npos) {
        return std::string();
    }
    return s.substr(first, s.find_last_not_of(" \t") - first + 1);
}

bool isAudioExtension(const std::string& ext) {
    const std::string e = lower(ext);
    return e == ".ogg" || e == ".mp3" || e == ".wav" || e == ".flac";
}

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

bool isDaypartTag(const std::string& tag) {
    return tag == "morning" || tag == "afternoon" || tag == "evening" || tag == "night";
}

bool isContextTag(const std::string& tag) {
    return tag == "refueling" || tag == "refuelling" || tag == "deicing" || tag == "delayed";
}

bool allDigits(const std::string& s) {
    if (s.empty()) {
        return false;
    }
    return std::all_of(s.begin(), s.end(),
                       [](unsigned char c) { return std::isdigit(c) != 0; });
}

// Re-engining gives an aeroplane a new ICAO designator that shares nothing with
// the old one - an A320neo is A20N, not A320N - while the cabin, the doors and
// therefore the announcement stay exactly the same. Pack authors tag the cabin
// they recorded for ([A320], [B738]), so these pairs have to be read as one
// aeroplane or a neo pilot hears the generic Default file. Only re-engined
// variants of the same fuselage belong here: an A359 and an A35K are different
// cabins and must stay apart.
const char* const kSameCabin[][2] = {
    {"A19N", "A319"}, {"A20N", "A320"}, {"A21N", "A321"},
    {"B37M", "B737"}, {"B38M", "B738"}, {"B39M", "B739"},
    {"E290", "E190"}, {"E295", "E195"},
};

std::string sameCabinAs(const std::string& code) {
    for (const auto& pair : kSameCabin) {
        if (code == pair[0]) {
            return pair[1];
        }
        if (code == pair[1]) {
            return pair[0];
        }
    }
    return std::string();
}

// An aircraft tag matches when it is the aeroplane's code, or when one is the
// beginning of the other and both are at least three characters. That is how
// [A32] catches an A320, which is what packs in the wild expect - and the length
// floor is what stops [A] catching everything.
bool prefixMatches(const std::string& tag, const std::string& plane) {
    if (tag == plane) {
        return true;
    }
    if (tag.size() < 3 || plane.size() < 3) {
        return false;
    }
    return tag.rfind(plane, 0) == 0 || plane.rfind(tag, 0) == 0;
}

bool aircraftMatches(const std::string& tag, const std::string& plane) {
    if (plane.empty()) {
        return false;
    }
    if (prefixMatches(tag, plane)) {
        return true;
    }
    // Both directions, so that a pack tagged [A20N] is heard in a ceo as well.
    const std::string twin = sameCabinAs(plane);
    if (!twin.empty() && prefixMatches(tag, twin)) {
        return true;
    }
    const std::string tagTwin = sameCabinAs(tag);
    return !tagTwin.empty() && prefixMatches(tagTwin, plane);
}

}  // namespace

bool isLocaleFolder(const std::string& name) {
    if (name.size() == 2) {
        return isAlpha(name[0]) && isAlpha(name[1]);
    }
    if (name.size() == 5) {
        return isAlpha(name[0]) && isAlpha(name[1]) && (name[2] == '-' || name[2] == '_') &&
               isAlpha(name[3]) && isAlpha(name[4]);
    }
    return false;
}

std::string chooseLocaleFolder(const std::vector<std::string>& folders,
                               const std::string& wanted) {
    std::vector<std::string> locales;
    for (const std::string& name : folders) {
        if (isLocaleFolder(name)) {
            locales.push_back(name);
        }
    }
    if (locales.empty()) {
        return std::string();
    }

    const std::string want = lower(wanted);
    for (const std::string& name : locales) {
        if (lower(name) == want) {
            return name;
        }
    }
    // Several to choose from and none of them the one asked for: read none. A
    // pack that ships English and German must not start speaking German because
    // the filesystem happened to list it first.
    return locales.size() == 1 ? locales.front() : std::string();
}

std::string daypartFor(int localHour) {
    // Night is tested first because it is the range that wraps midnight. Asking
    // about morning first - which is how 1.x asked, and how this was first
    // ported - let 01:00 fall through into "before 17:00" and come out as
    // afternoon: between midnight and 05:00 a pack's [Night] greeting could
    // never play, and an [Afternoon] one played in its place. Fixed in both
    // branches the day the bench caught it.
    if (localHour < 5 || localHour >= 22) {
        return "night";
    }
    if (localHour < 12) {
        return "morning";
    }
    if (localHour < 17) {
        return "afternoon";
    }
    return "evening";
}

bool parseSoundFile(const std::string& filename, std::string* event, SoundVariant* variant) {
    const std::size_t dot = filename.find_last_of('.');
    if (dot == std::string::npos || !isAudioExtension(filename.substr(dot))) {
        return false;
    }
    const std::string base = filename.substr(0, dot);

    // Pull the [tags] out and keep what is left as the event name.
    std::vector<std::string> tags;
    std::string stem;
    std::string current;
    int depth = 0;
    for (const char c : base) {
        if (c == '[') {
            ++depth;
            current.clear();
        } else if (c == ']' && depth > 0) {
            --depth;
            tags.push_back(lower(trim(current)));
            current.clear();
        } else if (depth > 0) {
            current.push_back(c);
        } else {
            stem.push_back(c);
        }
    }
    // Some packs write "AfterTakeoff.ogg[A359][2].ogg".
    const std::size_t stray = stem.find_last_of('.');
    if (stray != std::string::npos && isAudioExtension(stem.substr(stray))) {
        stem = stem.substr(0, stray);
    }
    stem = trim(stem);

    const auto& table = eventsByKey();
    auto found = table.find(lower(stem));
    if (found == table.end()) {
        // "SafetyBriefing1" / "BoardingMusic 2" - a trailing variant number.
        std::size_t end = stem.size();
        while (end > 0 && std::isdigit(static_cast<unsigned char>(stem[end - 1])) != 0) {
            --end;
        }
        if (end == stem.size()) {
            return false;
        }
        found = table.find(lower(trim(stem.substr(0, end))));
        if (found == table.end()) {
            return false;
        }
        tags.push_back(stem.substr(end));
    }

    if (event != nullptr) {
        *event = found->second;
    }
    if (variant == nullptr) {
        return true;
    }

    *variant = SoundVariant();
    variant->name = filename;
    for (const std::string& tag : tags) {
        if (tag.empty()) {
            continue;
        }
        if (isDaypartTag(tag)) {
            variant->daypart.push_back(tag);
        } else if (isContextTag(tag)) {
            variant->context = true;
        } else if (allDigits(tag)) {
            variant->number = std::stoi(tag);
        } else {
            variant->aircraft.push_back(upper(tag));
        }
    }
    return true;
}

std::string eventFromFilename(const std::string& filename) {
    std::string event;
    return parseSoundFile(filename, &event, nullptr) ? event : std::string();
}

bool scoreVariant(const SoundVariant& variant, const PlayContext& context, int* score) {
    int total = 0;

    if (!variant.aircraft.empty()) {
        const std::string plane = upper(context.aircraft);
        const bool matched =
            std::any_of(variant.aircraft.begin(), variant.aircraft.end(),
                        [&](const std::string& tag) { return aircraftMatches(tag, plane); });
        if (!matched) {
            return false;  // wrong aeroplane, never play
        }
        total += 4;
    }

    if (!variant.daypart.empty()) {
        const bool matched = std::find(variant.daypart.begin(), variant.daypart.end(),
                                       context.daypart) != variant.daypart.end();
        if (!matched) {
            return false;  // wrong time of day, never play
        }
        total += 3;
    }

    if (variant.context) {
        // Refuelling and de-icing are not detected in X-Plane, so a file tagged
        // with one is not disqualified - it is merely the last resort. Dropping
        // it outright would silence a pack whose only greeting says [Delayed].
        total -= 1;
    }

    if (score != nullptr) {
        *score = total;
    }
    return true;
}

int chooseVariant(const std::vector<SoundVariant>& variants, const PlayContext& context,
                  unsigned round) {
    std::vector<int> best;
    int bestScore = 0;
    for (std::size_t i = 0; i < variants.size(); ++i) {
        int score = 0;
        if (!scoreVariant(variants[i], context, &score)) {
            continue;
        }
        if (best.empty() || score > bestScore) {
            best.assign(1, static_cast<int>(i));
            bestScore = score;
        } else if (score == bestScore) {
            best.push_back(static_cast<int>(i));
        }
    }
    if (best.empty()) {
        return -1;
    }
    return best[round % best.size()];
}

}  // namespace xa::core
