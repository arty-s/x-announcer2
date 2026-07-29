#include "core/settings.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace xa::core {
namespace {

std::string trim(const std::string& s) {
    const std::size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return std::string();
    }
    const std::size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

void complain(std::vector<std::string>* problems, const std::string& text) {
    if (problems != nullptr) {
        problems->push_back(text);
    }
}

// "true"/"1"/"yes"/"on" and their opposites, in any case. 1.x treated every
// other spelling as false, so `enabled = True` silently muted the whole plugin;
// here it keeps the default and says so.
bool parseBool(const std::string& key, const std::string& value, bool* out,
               std::vector<std::string>* problems) {
    const std::string v = lower(trim(value));
    if (v == "true" || v == "1" || v == "yes" || v == "on") {
        *out = true;
        return true;
    }
    if (v == "false" || v == "0" || v == "no" || v == "off") {
        *out = false;
        return true;
    }
    complain(problems, key + ": '" + value + "' is not true or false - keeping " +
                           (*out ? "true" : "false"));
    return false;
}

bool parseNumber(const std::string& key, const std::string& value, double lo, double hi,
                 double* out, std::vector<std::string>* problems) {
    const std::string v = trim(value);
    if (v.empty()) {
        complain(problems, key + ": no value - keeping the default");
        return false;
    }
    char* end = nullptr;
    const double parsed = std::strtod(v.c_str(), &end);
    if (end == nullptr || *end != '\0') {
        complain(problems, key + ": '" + value + "' is not a number - keeping the default");
        return false;
    }
    if (parsed < lo || parsed > hi) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%s: %g is outside %g..%g - clamped", key.c_str(), parsed,
                      lo, hi);
        complain(problems, buf);
        *out = parsed < lo ? lo : hi;
        return true;
    }
    *out = parsed;
    return true;
}

bool parseInt(const std::string& key, const std::string& value, int lo, int hi, int* out,
              std::vector<std::string>* problems) {
    double asDouble = static_cast<double>(*out);
    if (!parseNumber(key, value, lo, hi, &asDouble, problems)) {
        return false;
    }
    *out = static_cast<int>(asDouble);
    return true;
}

bool parseChoice(const std::string& key, const std::string& value,
                 const std::vector<std::string>& allowed, std::string* out,
                 std::vector<std::string>* problems) {
    const std::string v = lower(trim(value));
    if (std::find(allowed.begin(), allowed.end(), v) != allowed.end()) {
        *out = v;
        return true;
    }
    std::string list;
    for (const std::string& option : allowed) {
        list += (list.empty() ? "" : ", ") + option;
    }
    complain(problems,
             key + ": '" + value + "' is not one of " + list + " - keeping '" + *out + "'");
    return false;
}

// %g, so 0.85 stays 0.85 and 1.0 is written as 1 - the same shapes Lua's
// tostring() produced, which keeps the two files diffable by eye.
std::string number(double value) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%g", value);
    return buf;
}

std::string boolean(bool value) { return value ? "true" : "false"; }

struct Help {
    const char* key;
    const char* text;
};

// Russian, because this is the file a human opens in a text editor. The window
// is Russian too now (v2 has a real font), but the file has to explain itself
// on its own - it is read while X-Plane is not running.
const Help kHelp[] = {
    {"library", "папка со звуками: внутри по папке на авиакомпанию, как у MSFS Universal "
                "Announcer. Пусто - берётся Sound_packs рядом с плагином"},
    {"language", "языковая подпапка внутри пака, если она есть: en-us, de-de, ru"},
    {"airline_mode", "auto - определять авиакомпанию по ливрее и борту, manual - брать airline_manual"},
    {"airline_manual", "код ICAO пака, который использовать принудительно"},
    {"announce_bus", "шина X-Plane для объявлений: interior, exterior, ui, com1, com2, ground"},
    {"music_bus", "шина для фоновой музыки; может совпадать с announce_bus - каналы независимы"},
    {"volume", "громкость объявлений, 0.0 - 1.0"},
    {"music_volume", "громкость фоновой музыки, 0.0 - 1.0"},
    {"duck", "во сколько раз приглушать музыку на время объявления"},
    {"enabled", "false - объявления полностью выключены"},
    {"boarding_music", "играть музыку между приветствиями при посадке пассажиров"},
    {"cabin_noise", "фоновый шум салона в полёте, если файл CabinNoise есть в паке"},
    {"auto_boarding", "начинать посадку пассажиров самому по питанию и огням; false - только кнопкой"},
    {"boarding_repeat", "секунд между повторами приветствия при посадке"},
    {"pilot_welcome", "приветствие командира после приветствия бортпроводника"},
    {"door_calls", "объявления про двери: ArmDoors и DisarmDoors"},
    {"night_dim", "объявления про притушенный свет в салоне ночью"},
    {"landing_reaction", "реакция салона на посадку: LandingGreat или LandingTerrible"},
    {"seatbelt_dref", "свой датареф табло ремней; пусто - искать автоматически"},
    {"window_scale", "масштаб текста в окне, 1.0 - обычный; больше для VR"},
    {"panel_open", "было ли окно плагина открыто при выходе; так оно и откроется в следующий раз"},
};

const char* helpFor(const std::string& key) {
    for (const Help& help : kHelp) {
        if (key == help.key) {
            return help.text;
        }
    }
    return nullptr;
}

}  // namespace

const std::vector<std::string>& audioBusNames() {
    static const std::vector<std::string> names = {"interior", "exterior", "ui",
                                                   "com1",     "com2",     "ground"};
    return names;
}

Settings parseSettings(const std::string& text, std::vector<std::string>* problems) {
    Settings s;
    std::istringstream input(text);
    std::string line;
    int lineNumber = 0;

    while (std::getline(input, line)) {
        ++lineNumber;
        const std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') {
            continue;
        }
        const std::size_t equals = trimmed.find('=');
        if (equals == std::string::npos) {
            complain(problems, "line " + std::to_string(lineNumber) + ": '" + trimmed +
                                   "' is neither a comment nor key = value - ignored");
            continue;
        }
        const std::string key = lower(trim(trimmed.substr(0, equals)));
        const std::string value = trim(trimmed.substr(equals + 1));
        if (key.empty()) {
            complain(problems, "line " + std::to_string(lineNumber) + ": no key before '='");
            continue;
        }

        if (key == "library") {
            s.library = value;
        } else if (key == "language") {
            s.language = value;
        } else if (key == "auto_find") {
            // Dropped, not renamed. Saying so and letting it fall out of the
            // file on the next write beats keeping it as an unknown key that
            // looks like it still does something.
            complain(problems, "auto_find: больше не используется - пустой library "
                               "означает папку Sound_packs рядом с плагином");
        } else if (key == "airline_mode") {
            parseChoice(key, value, {"auto", "manual"}, &s.airlineMode, problems);
        } else if (key == "airline_manual") {
            s.airlineManual = value;
        } else if (key == "announce_bus") {
            parseChoice(key, value, audioBusNames(), &s.announceBus, problems);
        } else if (key == "music_bus") {
            parseChoice(key, value, audioBusNames(), &s.musicBus, problems);
        } else if (key == "volume") {
            parseNumber(key, value, 0.0, 1.0, &s.volume, problems);
        } else if (key == "music_volume") {
            parseNumber(key, value, 0.0, 1.0, &s.musicVolume, problems);
        } else if (key == "duck") {
            parseNumber(key, value, 0.0, 1.0, &s.duck, problems);
        } else if (key == "enabled") {
            parseBool(key, value, &s.flight.enabled, problems);
        } else if (key == "boarding_music") {
            parseBool(key, value, &s.flight.boardingMusic, problems);
        } else if (key == "cabin_noise") {
            parseBool(key, value, &s.flight.cabinNoise, problems);
        } else if (key == "auto_boarding") {
            parseBool(key, value, &s.flight.autoBoarding, problems);
        } else if (key == "boarding_repeat") {
            parseNumber(key, value, 0.0, 86400.0, &s.flight.boardingRepeat, problems);
        } else if (key == "pilot_welcome") {
            parseBool(key, value, &s.flight.pilotWelcome, problems);
        } else if (key == "door_calls") {
            parseBool(key, value, &s.flight.doorCalls, problems);
        } else if (key == "night_dim") {
            parseBool(key, value, &s.flight.nightDim, problems);
        } else if (key == "landing_reaction") {
            parseBool(key, value, &s.flight.landingReaction, problems);
        } else if (key == "music_max_loops") {
            // Dropped like auto_find, and for the same reason: a key that is
            // read and then ignored is worse than one that is gone. In 1.x the
            // cap bounded a FlyWithLua memory leak; v2 has no leak to bound, and
            // the audible effect was music stopping in the middle of boarding.
            complain(problems, "music_max_loops: больше не используется - фоновый трек "
                               "играет, пока идёт фаза, которая его завела");
        } else if (key == "seatbelt_dref") {
            s.seatbeltDref = value;
        } else if (key == "window_scale") {
            parseNumber(key, value, 0.5, 3.0, &s.windowScale, problems);
        } else if (key == "panel_open") {
            parseBool(key, value, &s.panelOpen, problems);
        } else {
            s.unknown[key] = value;
        }
    }
    return s;
}

std::string writeSettings(const Settings& s) {
    std::string out;
    out += "# Настройки X-Announcer 2 для X-Plane 12.\n";
    out += "# Файл переписывается плагином, когда вы меняете что-то в окне,\n";
    out += "# поэтому свои комментарии сюда добавлять бесполезно.\n";
    out += "# Правьте при выключенном симуляторе либо жмите «Перечитать» в окне.\n";

    const std::pair<const char*, std::string> entries[] = {
        {"library", s.library},
        {"language", s.language},
        {"airline_mode", s.airlineMode},
        {"airline_manual", s.airlineManual},
        {"announce_bus", s.announceBus},
        {"music_bus", s.musicBus},
        {"volume", number(s.volume)},
        {"music_volume", number(s.musicVolume)},
        {"duck", number(s.duck)},
        {"enabled", boolean(s.flight.enabled)},
        {"boarding_music", boolean(s.flight.boardingMusic)},
        {"cabin_noise", boolean(s.flight.cabinNoise)},
        {"auto_boarding", boolean(s.flight.autoBoarding)},
        {"boarding_repeat", number(s.flight.boardingRepeat)},
        {"pilot_welcome", boolean(s.flight.pilotWelcome)},
        {"door_calls", boolean(s.flight.doorCalls)},
        {"night_dim", boolean(s.flight.nightDim)},
        {"landing_reaction", boolean(s.flight.landingReaction)},
        {"seatbelt_dref", s.seatbeltDref},
        {"window_scale", number(s.windowScale)},
        {"panel_open", boolean(s.panelOpen)},
    };

    for (const auto& [key, value] : entries) {
        const char* help = helpFor(key);
        if (help != nullptr) {
            out += "\n# ";
            out += help;
            out += "\n";
        }
        out += key;
        out += " = ";
        out += value;
        out += "\n";
    }

    if (!s.unknown.empty()) {
        out += "\n# Эти ключи эта версия ещё не понимает (SimBrief, виджет) — они\n";
        out += "# сохраняются как есть, чтобы не пропасть при перезаписи файла.\n";
        for (const auto& [key, value] : s.unknown) {
            out += key;
            out += " = ";
            out += value;
            out += "\n";
        }
    }
    return out;
}

}  // namespace xa::core
