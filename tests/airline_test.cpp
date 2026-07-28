#include "airline_test.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "core/airline.h"

namespace xa::test {
namespace {

int* g_checks = nullptr;
int* g_failed = nullptr;

void check(bool condition, const std::string& what) {
    ++*g_checks;
    if (condition) {
        std::cout << "   PASS " << what << "\n";
    } else {
        ++*g_failed;
        std::cout << "   FAIL " << what << "\n";
    }
}

}  // namespace

void runAirlineChecks(const char* airlinesFile, const char* liveryFile, int* checks, int* failed) {
    g_checks = checks;
    g_failed = failed;
    std::cout << "-- airline detection\n";

    core::AirlineIndex index;
    const int loaded = index.loadFile(airlinesFile);
    check(loaded > 5000, "the airline table loads (" + std::to_string(loaded) + " carriers)");
    if (loaded == 0) {
        return;
    }
    check(index.nameOf("SBI") == "S7 Airlines", "SBI is S7 Airlines");

    // No pack is owned here on purpose: recognising the airline and owning its
    // sounds are separate questions, and this is the one that must not depend on
    // the other. The S7 defect was exactly this confusion.
    const core::HasPack noPacks = [](const std::string&) { return false; };

    std::ifstream liveries(liveryFile);
    check(liveries.good(), std::string("the livery list opens (") + liveryFile + ")");

    std::vector<std::pair<std::string, std::string>> cases;
    std::string line;
    while (std::getline(liveries, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const std::size_t tab = line.find('\t');
        if (tab != std::string::npos) {
            cases.emplace_back(line.substr(0, tab), line.substr(tab + 1));
        }
    }

    std::vector<std::string> wrong;
    for (const auto& [folder, want] : cases) {
        const core::AirlineVerdict verdict =
            index.resolve("Aircraft/Test/liveries/" + folder + "/", "", "", "", noPacks);
        const std::string got = verdict.code == "Default" ? "DEFAULT" : verdict.code;
        if (got != want) {
            wrong.push_back(folder + " -> " + got + " (wanted " + want + ") via " + verdict.source);
        }
    }
    for (const std::string& item : wrong) {
        std::cout << "       " << item << "\n";
    }
    check(wrong.empty(), "all " + std::to_string(cases.size()) + " livery folders resolve correctly");

    // Same build, same answer. The name index used to be a hash table whose
    // ordering among equal-length names leaked into the verdict.
    core::AirlineIndex again;
    again.loadFile(airlinesFile);
    bool stable = true;
    for (const auto& [folder, _] : cases) {
        if (index.detect(folder, noPacks).code != again.detect(folder, noPacks).code) {
            stable = false;
        }
    }
    check(stable, "detection is deterministic across index rebuilds");

    // A three-letter word must never outrank the airline name next to it.
    for (const auto& [text, want] : std::vector<std::pair<std::string, std::string>>{
             {"Air China", "CCA"}, {"Red Wings Airlines RA-73329", "RWZ"},
             {"Thai Airways HS-TXS", "THA"}, {"A320 NEO Air Serbia", "ASL"}}) {
        const std::string got = index.detect(text, noPacks).code;
        check(got == want, "'" + text + "' is not hijacked by a stray code (got " + got + ")");
    }

    // "A320.acf" offers the word "acf", which is a real ICAO code.
    const core::AirlineVerdict fromFile = index.resolve("", "", "A320.acf", "", noPacks);
    check(fromFile.code == "Default", "an .acf filename is not read as the airline ACF");

    // Owning a pack promotes an explicit code above an unowned one, but still
    // never above a name. Both halves matter.
    const core::HasPack ownsTxs = [](const std::string& code) { return code == "TXS"; };
    check(index.detect("Thai Airways HS-TXS", ownsTxs).code == "THA",
          "owning a pack still does not let a registration beat the airline name");
    check(index.detect("SOMETHING XYZ RA-73000", ownsTxs).code != "TXS",
          "a code glued into a registration is not a standalone word");
}

}  // namespace xa::test
