#include "core/signal_map.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace xa::core {
namespace {

std::string trim(const std::string& text) {
    const auto space = [](unsigned char c) { return std::isspace(c) != 0; };
    std::size_t from = 0;
    std::size_t to = text.size();
    while (from < to && space(static_cast<unsigned char>(text[from]))) {
        ++from;
    }
    while (to > from && space(static_cast<unsigned char>(text[to - 1]))) {
        --to;
    }
    return text.substr(from, to - from);
}

std::string upper(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return text;
}

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// "on>=1", "on<=0", "on=2". Returns false when the word is not a threshold at
// all, so the caller can complain about it by name instead of guessing.
bool parseThreshold(const std::string& word, double* on, bool* atMost) {
    if (word.rfind("on", 0) != 0) {
        return false;
    }
    std::string rest = trim(word.substr(2));
    if (rest.rfind(">=", 0) == 0) {
        *atMost = false;
        rest = rest.substr(2);
    } else if (rest.rfind("<=", 0) == 0) {
        *atMost = true;
        rest = rest.substr(2);
    } else if (rest.rfind("=", 0) == 0) {
        *atMost = false;
        rest = rest.substr(1);
    } else {
        return false;
    }
    rest = trim(rest);
    if (rest.empty()) {
        return false;
    }
    char* end = nullptr;
    const double value = std::strtod(rest.c_str(), &end);
    if (end == rest.c_str() || *end != '\0') {
        return false;
    }
    *on = value;
    return true;
}

}  // namespace

const std::vector<std::string>& signalIds() {
    static const std::vector<std::string> ids = {
        "beacon", "nav",      "strobe",   "landing",  "taxi",
        "logo",   "battery",  "parkbrake", "seatbelt", "route_distance",
    };
    return ids;
}

std::vector<SignalOverride> SignalOverrides::forAircraft(const std::string& icao) const {
    std::vector<SignalOverride> out;
    const auto own = byAircraft.find(upper(icao));
    if (!icao.empty() && own != byAircraft.end()) {
        out.insert(out.end(), own->second.begin(), own->second.end());
    }
    const auto any = byAircraft.find("*");
    if (any != byAircraft.end()) {
        out.insert(out.end(), any->second.begin(), any->second.end());
    }
    return out;
}

int SignalOverrides::count() const {
    int total = 0;
    for (const auto& entry : byAircraft) {
        total += static_cast<int>(entry.second.size());
    }
    return total;
}

SignalOverrides parseSignalOverrides(const std::string& text, std::vector<std::string>* problems) {
    SignalOverrides out;
    const auto complain = [&](const std::string& message) {
        if (problems != nullptr) {
            problems->push_back(message);
        }
    };

    std::istringstream stream(text);
    std::string line;
    // No section yet means the file starts with plain lines. Those apply to
    // every aeroplane: the commonest file this will ever see is one line long
    // and written by somebody who has one aeroplane.
    std::string section = "*";
    int number = 0;
    while (std::getline(stream, line)) {
        ++number;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::string body = trim(line);
        if (body.empty() || body[0] == '#' || body[0] == ';') {
            continue;
        }
        if (body.front() == '[') {
            const std::size_t close = body.find(']');
            if (close == std::string::npos) {
                complain("строка " + std::to_string(number) + ": скобка [ не закрыта");
                continue;
            }
            section = upper(trim(body.substr(1, close - 1)));
            if (section.empty()) {
                section = "*";
            }
            continue;
        }
        const std::size_t eq = body.find('=');
        if (eq == std::string::npos) {
            complain("строка " + std::to_string(number) + ": нет знака =");
            continue;
        }
        SignalOverride entry;
        entry.signal = lower(trim(body.substr(0, eq)));
        const std::vector<std::string>& ids = signalIds();
        if (std::find(ids.begin(), ids.end(), entry.signal) == ids.end()) {
            complain("строка " + std::to_string(number) + ": '" + entry.signal +
                     "' - такого сигнала нет");
            continue;
        }
        // dataref, then optional threshold. Split on whitespace rather than on a
        // comma: a dataref never contains a space, and neither does "on<=0".
        std::istringstream rest(trim(body.substr(eq + 1)));
        std::string word;
        if (!(rest >> word)) {
            complain("строка " + std::to_string(number) + ": не назван датареф");
            continue;
        }
        entry.dataref = word;
        bool bad = false;
        while (rest >> word) {
            if (!parseThreshold(lower(word), &entry.on, &entry.atMost)) {
                complain("строка " + std::to_string(number) + ": '" + word +
                         "' - ожидалось on>=число или on<=число");
                bad = true;
                break;
            }
        }
        if (bad) {
            continue;
        }
        out.byAircraft[section].push_back(entry);
    }
    return out;
}

std::string sampleSignalOverrides() {
    return
        "# Здесь можно назвать датарефы борта, которых плагин ещё не знает.\n"
        "# Он ищет их сам, и для большинства самолётов этот файл не нужен вовсе.\n"
        "# Нужен он тогда, когда на вкладке «Триггеры» какая-то строка говорит\n"
        "# «борт не публикует» или читает не то, что видно в кабине.\n"
        "#\n"
        "# Что писать, подсказывает журнал: включите dataref_probe в config.ini,\n"
        "# щёлкните тумблером в кабине и найдите строку probe: с именем датарефа.\n"
        "#\n"
        "# Раздел - код борта из X-Plane (B738, B772, A20N) либо * для всех.\n"
        "# Строка - сигнал = датареф [on>=значение | on<=значение].\n"
        "# По умолчанию \"включено\" - это значение 1 и выше.\n"
        "#\n"
        "# Сигналы: beacon, nav, strobe, landing, taxi, logo, battery,\n"
        "#          parkbrake, seatbelt, route_distance.\n"
        "#\n"
        "# Пример - FlightFactor 777, у которого три тумблера перевёрнуты:\n"
        "# [B772]\n"
        "# strobe  = 1-sim/ckpt/strobeLightSwitch/anim on<=0\n"
        "# taxi    = 1-sim/ckpt/taxiLightSwitch/anim on<=0\n"
        "# landing = 1-sim/ckpt/landingLightNoseSwitch/anim on<=0\n";
}

}  // namespace xa::core
