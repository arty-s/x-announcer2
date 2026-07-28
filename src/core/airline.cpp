#include "core/airline.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>

namespace xa::core {
namespace {

// Common livery spellings the OpenFlights names do not cover directly.
const std::pair<const char*, const char*> kAliases[] = {
    {"aeroflot", "AFL"}, {"pobeda", "PBD"}, {"s7", "SBI"}, {"sevenairlines", "SBI"},
    {"rossiya", "SDM"}, {"utair", "UTA"}, {"uralairlines", "SVR"}, {"ural", "SVR"},
    {"nordwind", "NWS"}, {"redwings", "RWZ"}, {"azurair", "AZV"}, {"smartavia", "AUL"},
    {"american", "AAL"}, {"delta", "DAL"}, {"united", "UAL"}, {"southwest", "SWA"},
    {"jetblue", "JBU"}, {"alaska", "ASA"}, {"spirit", "NKS"}, {"frontier", "FFT"},
    {"british", "BAW"}, {"britishairways", "BAW"}, {"ba", "BAW"}, {"speedbird", "BAW"},
    {"lufthansa", "DLH"}, {"swiss", "SWR"}, {"austrian", "AUA"}, {"eurowings", "EWG"},
    {"airfrance", "AFR"}, {"klm", "KLM"}, {"transavia", "TRA"}, {"iberia", "IBE"},
    {"vueling", "VLG"}, {"airnostrum", "ANE"}, {"tap", "TAP"}, {"ita", "ITY"},
    {"alitalia", "AZA"}, {"turkish", "THY"}, {"pegasus", "PGT"}, {"aegean", "AEE"},
    {"ryanair", "RYR"}, {"easyjet", "EZY"}, {"wizz", "WZZ"}, {"wizzair", "WZZ"},
    {"jet2", "EXS"}, {"norwegian", "NAX"}, {"sas", "SAS"}, {"finnair", "FIN"},
    {"icelandair", "ICE"}, {"aerlingus", "EIN"}, {"tui", "TOM"}, {"condor", "CFG"},
    {"emirates", "UAE"}, {"etihad", "ETD"}, {"qatar", "QTR"}, {"flydubai", "FDB"},
    {"saudia", "SVA"}, {"elal", "ELY"}, {"royaljordanian", "RJA"}, {"oman", "OMA"},
    {"qantas", "QFA"}, {"qantaslink", "QLK"}, {"virginaustralia", "VOZ"}, {"jetstar", "JST"},
    {"airnewzealand", "ANZ"}, {"airasia", "AXM"}, {"singapore", "SIA"}, {"cathay", "CPA"},
    {"ana", "ANA"}, {"allnippon", "ANA"}, {"jal", "JAL"}, {"japanairlines", "JAL"},
    {"korean", "KAL"}, {"asiana", "AAR"}, {"china", "CCA"}, {"airchina", "CCA"},
    {"chinaeastern", "CES"}, {"chinasouthern", "CSN"}, {"eva", "EVA"}, {"chinaairlines", "CAL"},
    {"thai", "THA"}, {"vietnam", "HVN"}, {"garuda", "GIA"}, {"philippine", "PAL"},
    {"aircanada", "ACA"}, {"westjet", "WJA"}, {"aeromexico", "AMX"}, {"copa", "CMP"},
    {"latam", "LAN"}, {"avianca", "AVA"}, {"gol", "GLO"}, {"azul", "AZU"},
    {"ethiopian", "ETH"}, {"kenya", "KQA"}, {"southafrican", "SAA"}, {"egyptair", "MSR"},
    {"royalairmaroc", "RAM"}, {"airindia", "AIC"}, {"indigo", "IGO"}, {"vistara", "VTI"},
    {"swissair", "SWR"}, {"edelweiss", "EDW"}, {"airbaltic", "BTI"}, {"lot", "LOT"},
    {"czech", "CSA"}, {"croatia", "CTN"}, {"airserbia", "ASL"}, {"bulgaria", "LZB"},
    {"airastana", "KZR"}, {"uzbekistan", "UZB"}, {"azerbaijan", "AHY"}, {"flyone", "FIA"},
};

const char* const kStripWords[] = {
    "airlines", "airline", "airways", "airway", "aviation", "aircompany",
    "international", "virtual", "company", "limited", "group", "cargo", "air",
};

// Carriers whose whole name is a generic word: WAY is literally called
// "Airways", WHT is called "White". Indexed by name they hijack everything -
// "Thai Airways" became WAY, "full_white" became WHT. They stay reachable by
// their ICAO code, just not by name.
const std::set<std::string>& genericNames() {
    static const std::set<std::string> set = {
        "air", "airway", "airways", "airline", "airlines", "aviation", "cargo",
        "express", "charter", "jet", "jets", "transport", "white", "black",
        "blue", "green", "red", "silver", "gold", "star", "sky", "one", "house",
        "classic", "retro",
    };
    return set;
}

// Three-letter words that turn up in livery folder names and collide with real
// ICAO codes: engine variants, condition tags, filler words. Without this list
// "A320 NEO Air Serbia" resolves to whoever owns NEO or AIR.
const std::set<std::string>& tokenStoplist() {
    static const std::set<std::string> set = {
        "AIR", "NEO", "CEO", "CFM", "IAE", "PWG", "OLD", "NEW", "THE", "AND",
        "FOR", "VIP", "WIP", "HOF", "RED", "MAX", "XWB", "WIN",
    };
    return set;
}

std::string removeAll(std::string haystack, const std::string& needle) {
    std::size_t at = 0;
    while ((at = haystack.find(needle, at)) != std::string::npos) {
        haystack.erase(at, needle.size());
    }
    return haystack;
}

std::string trimTrailingSlashes(std::string s) {
    while (!s.empty() && (s.back() == '/' || s.back() == '\\')) {
        s.pop_back();
    }
    return s;
}

std::string leafOf(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string dropExtension(const std::string& name) {
    const std::size_t dot = name.find_last_of('.');
    if (dot == std::string::npos) {
        return name;
    }
    for (std::size_t i = dot + 1; i < name.size(); ++i) {
        if (std::isalpha(static_cast<unsigned char>(name[i])) == 0) {
            return name;
        }
    }
    return name.substr(0, dot);
}

}  // namespace

std::string normaliseName(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (const unsigned char c : s) {
        if (std::isalnum(c) != 0 && c < 0x80) {
            out.push_back(static_cast<char>(std::tolower(c)));
        }
    }
    return out;
}

std::vector<std::string> wordsOf(const std::string& text) {
    static const std::string separators = " \t\n\r[]()_,./\\";
    std::vector<std::string> out;
    std::string current;
    for (const char c : text) {
        if (separators.find(c) != std::string::npos) {
            if (!current.empty()) {
                out.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        out.push_back(current);
    }
    return out;
}

int AirlineIndex::load(std::istream& in) {
    airlines_.clear();
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const std::size_t tab = line.find('\t');
        if (tab == std::string::npos) {
            continue;
        }
        const std::string icao = line.substr(0, tab);
        if (icao.size() != 3) {
            continue;
        }
        airlines_.emplace(icao, line.substr(tab + 1));
    }
    buildIndex();
    return size();
}

int AirlineIndex::loadFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        airlines_.clear();
        buildIndex();
        return 0;
    }
    return load(file);
}

void AirlineIndex::buildIndex() {
    nameIndex_.clear();
    nameList_.clear();

    // Two carriers can want the same key: strip "airlines" from "Japan Airlines"
    // and "air" from "Air Japan" and both ask for "japan". Whoever gets it must
    // not be decided by iteration order - 1.x left that to a hash table and the
    // answer for a Japanese livery depended on the build.
    //
    // The rule: the key goes to the carrier whose full name STARTS with it. That
    // is what an abbreviation is - "Japan Airlines" shortens to "Japan", "Air
    // Japan" does not. When neither or both qualify, the lower ICAO wins, which
    // is arbitrary but at least identical everywhere.
    std::map<std::string, std::string> ownerFullName;
    const auto claim = [&](const std::string& key, const std::string& icao,
                           const std::string& full) {
        if (key.size() < 4 || genericNames().count(key) != 0) {
            return;
        }
        const auto owner = ownerFullName.find(key);
        if (owner == ownerFullName.end()) {
            nameIndex_[key] = icao;
            ownerFullName[key] = full;
            return;
        }
        const bool newcomerIsPrefix = full.compare(0, key.size(), key) == 0;
        const bool incumbentIsPrefix = owner->second.compare(0, key.size(), key) == 0;
        if (newcomerIsPrefix && !incumbentIsPrefix) {
            nameIndex_[key] = icao;
            owner->second = full;
        }
    };

    for (const auto& [icao, name] : airlines_) {
        const std::string full = normaliseName(name);
        claim(full, icao, full);
        std::string shortened = full;
        for (const char* word : kStripWords) {
            shortened = removeAll(shortened, word);
        }
        claim(shortened, icao, full);
    }
    for (const auto& [alias, icao] : kAliases) {
        nameIndex_[alias] = icao;
    }

    nameList_.assign(nameIndex_.begin(), nameIndex_.end());
    std::sort(nameList_.begin(), nameList_.end(),
              [](const auto& a, const auto& b) {
                  if (a.first.size() != b.first.size()) {
                      return a.first.size() > b.first.size();
                  }
                  return a.first < b.first;
              });
}

std::string AirlineIndex::nameOf(const std::string& icao) const {
    const auto it = airlines_.find(icao);
    return it == airlines_.end() ? std::string() : it->second;
}

AirlineMatch AirlineIndex::detect(const std::string& text, const HasPack& hasPack) const {
    AirlineMatch match;
    if (text.empty()) {
        return match;
    }
    const std::vector<std::string> words = wordsOf(text);

    // Pass 1: a run of consecutive words that is exactly an airline name. Word
    // boundaries make this safe for short names, so "Thai Airways HS-TXS" can
    // match "Thai" without "thai" being allowed to match anywhere at all. The
    // longest run wins: "Thai Lion" beats the "Thai" inside it.
    for (std::size_t start = 0; start < words.size(); ++start) {
        std::string run;
        for (std::size_t stop = start; stop < words.size(); ++stop) {
            run += normaliseName(words[stop]);
            const auto it = nameIndex_.find(run);
            if (it != nameIndex_.end() && run.size() >= 3 &&
                static_cast<int>(1000 + run.size()) > match.score) {
                match.code = it->second;
                match.how = "name";
                match.score = static_cast<int>(1000 + run.size());
            }
        }
    }

    // Pass 2: the name glued to something else - "AirAsiaOld", "AeroflotSkyteam".
    // nameList_ is sorted longest first, so the first hit is the most specific.
    const std::string norm = normaliseName(text);
    if (!norm.empty()) {
        for (const auto& [candidate, icao] : nameList_) {
            if (candidate.size() >= 5 && static_cast<int>(1000 + candidate.size()) > match.score &&
                norm.find(candidate) != std::string::npos) {
                match.code = icao;
                match.how = "name";
                match.score = static_cast<int>(1000 + candidate.size());
                break;
            }
        }
    }

    // Pass 3: the folder gives a shorter form of a longer official name -
    // "Scandinavian Airlines" for "Scandinavian Airlines System". Only for long
    // runs, and only when nothing better was found, so it stays cheap.
    if (match.score == 0) {
        std::size_t bestLength = 0;
        for (std::size_t start = 0; start < words.size(); ++start) {
            std::string run;
            for (std::size_t stop = start; stop < words.size(); ++stop) {
                run += normaliseName(words[stop]);
                // "airlines" is eight characters long and prefixes a whole
                // family of obscure carriers; generic runs are not evidence.
                if (run.size() < 8 || genericNames().count(run) != 0) {
                    continue;
                }
                for (const auto& [candidate, icao] : nameList_) {
                    if (candidate.size() > run.size() && candidate.compare(0, run.size(), run) == 0 &&
                        (bestLength == 0 || candidate.size() < bestLength)) {
                        bestLength = candidate.size();
                        match.code = icao;
                        match.how = "name (short form)";
                        match.score = static_cast<int>(900 + run.size());
                    }
                }
            }
        }
    }

    // An explicit ICAO code, but only as a standalone word, and only after the
    // name passes have had their say.
    for (const std::string& word : words) {
        if (word.size() != 3) {
            continue;
        }
        bool alpha = true;
        std::string upper;
        for (const unsigned char c : word) {
            if (std::isalpha(c) == 0 || c >= 0x80) {
                alpha = false;
                break;
            }
            upper.push_back(static_cast<char>(std::toupper(c)));
        }
        if (!alpha || tokenStoplist().count(upper) != 0) {
            continue;
        }
        const bool owned = hasPack && hasPack(upper);
        if (!owned && !known(upper)) {
            continue;
        }
        const int score = owned ? 700 : 500;
        if (score > match.score) {
            match.code = upper;
            match.how = "code " + upper;
            match.score = score;
        }
    }
    return match;
}

AirlineVerdict AirlineIndex::resolve(const std::string& liveryPath, const std::string& tailNumber,
                                     const std::string& aircraftFile,
                                     const std::string& aircraftDescription,
                                     const HasPack& hasPack) const {
    // Without dropping the extension "A320.acf" offers the word "acf", which is
    // a real ICAO code (a Canarian flight school) and won every time.
    const std::vector<std::pair<std::string, std::string>> candidates = {
        {leafOf(trimTrailingSlashes(liveryPath)), "livery"},
        {tailNumber, "tail number"},
        {dropExtension(aircraftFile), "aircraft file"},
        {aircraftDescription, "aircraft"},
    };

    for (const auto& [text, label] : candidates) {
        const AirlineMatch match = detect(text, hasPack);
        if (match.found()) {
            AirlineVerdict verdict;
            verdict.code = match.code;
            verdict.source = label + " (" + match.how + "): " + text;
            return verdict;
        }
    }
    return AirlineVerdict();
}

}  // namespace xa::core
