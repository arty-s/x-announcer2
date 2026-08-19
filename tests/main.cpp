// The offline bench.
//
//   xa_test [scenario-dir] [--update-golden]
//
// For every scenario it: flies it, checks the inline expectations, checks the
// widget-vs-machine invariant, flies it a SECOND time and demands a
// byte-identical trace, then diffs that trace against the golden file. The
// determinism pass is not ceremony - a bench whose output wanders cannot be
// diffed against 1.x, and diffing against 1.x is the only thing that proves the
// port did not change behaviour.
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "airline_test.h"
#include "audio_test.h"
#include "core/airline.h"
#include "core/settings.h"
#include "pack_layout_test.h"
#include "report_body_test.h"
#include "scenario.h"
#include "settings_test.h"
#include "signal_map_test.h"

namespace fs = std::filesystem;

namespace {

std::vector<std::string> readLines(const fs::path& path) {
    std::vector<std::string> lines;
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

void writeLines(const fs::path& path, const std::vector<std::string>& lines) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    for (const std::string& line : lines) {
        file << line << "\n";
    }
}

// A short diff: the first few differing lines say more than a full dump.
std::vector<std::string> describeDiff(const std::vector<std::string>& want,
                                      const std::vector<std::string>& got) {
    std::vector<std::string> out;
    const std::size_t n = std::max(want.size(), got.size());
    for (std::size_t i = 0; i < n && out.size() < 12; ++i) {
        const std::string a = i < want.size() ? want[i] : std::string("<missing>");
        const std::string b = i < got.size() ? got[i] : std::string("<missing>");
        if (a != b) {
            out.push_back("    golden: " + a);
            out.push_back("    actual: " + b);
        }
    }
    if (out.empty() && want.size() != got.size()) {
        out.push_back("    trace lengths differ");
    }
    return out;
}

}  // namespace

namespace {

// One line of "text<TAB>code" per input line. Used by the airline comparison
// against 1.x, which needs to ask the same question of both implementations
// without linking either into the other.
int detectFromFile(const std::string& path) {
    xa::core::AirlineIndex index;
    if (index.loadFile(XA_AIRLINES_FILE) == 0) {
        std::cerr << "cannot load " << XA_AIRLINES_FILE << "\n";
        return 2;
    }
    std::ifstream input(path);
    if (!input) {
        std::cerr << "cannot open " << path << "\n";
        return 2;
    }
    const xa::core::HasPack noPacks = [](const std::string&) { return false; };
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const auto verdict = index.resolve("Aircraft/Test/liveries/" + line + "/", "", "", "", noPacks);
        std::cout << line << "\t" << verdict.code << "\n";
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg.rfind("--detect-file=", 0) == 0) {
            return detectFromFile(arg.substr(14));
        }
        // The settings file exactly as a first run would write it, so the
        // comparison against 1.x can read our defaults without linking us in.
        if (arg == "--dump-settings") {
            std::cout << xa::core::writeSettings(xa::core::Settings());
            return 0;
        }
    }

    fs::path scenarioDir = XA_SCENARIO_DIR;
    std::string libraryDir = R"(D:\UA_Sounds)";
    bool updateGolden = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--update-golden") {
            updateGolden = true;
        } else if (arg.rfind("--library=", 0) == 0) {
            libraryDir = arg.substr(10);
        } else {
            scenarioDir = arg;
        }
    }
    const fs::path goldenDir = scenarioDir.parent_path() / "golden";

    if (!fs::exists(scenarioDir)) {
        std::cerr << "no scenario directory at " << scenarioDir.string() << "\n";
        return 2;
    }

    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(scenarioDir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".scn") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());

    int checks = 0;
    int failed = 0;

    xa::test::runAudioChecks(libraryDir.c_str(), &checks, &failed);
    std::cout << "\n";

    xa::test::runAirlineChecks(XA_AIRLINES_FILE, XA_LIVERY_FILE, &checks, &failed);
    std::cout << "\n";

    xa::test::runSettingsChecks(&checks, &failed);
    std::cout << "\n";

    xa::test::runSignalMapChecks(&checks, &failed);
    std::cout << "\n";

    xa::test::runPackLayoutChecks(&checks, &failed);
    std::cout << "\n";

    xa::test::runReportBodyChecks(&checks, &failed);
    std::cout << "\n";

    for (const fs::path& file : files) {
        const auto result = xa::test::runScenarioFile(file.string());
        std::cout << "-- " << result.name << " (" << result.trace.size() << " events)\n";

        ++checks;
        for (const std::string& failure : result.failures) {
            ++failed;
            std::cout << "   FAIL " << failure << "\n";
        }

        // Determinism: same input, same output, always.
        ++checks;
        const auto again = xa::test::runScenarioFile(file.string());
        if (again.trace != result.trace) {
            ++failed;
            std::cout << "   FAIL not deterministic - two runs produced different traces\n";
            for (const std::string& line : describeDiff(result.trace, again.trace)) {
                std::cout << line << "\n";
            }
        }

        const fs::path golden = goldenDir / (file.stem().string() + ".trace");
        ++checks;
        if (updateGolden) {
            writeLines(golden, result.trace);
            std::cout << "   golden updated\n";
        } else if (!fs::exists(golden)) {
            ++failed;
            std::cout << "   FAIL no golden trace; run with --update-golden once "
                         "the behaviour is believed correct\n";
        } else {
            const auto want = readLines(golden);
            if (want != result.trace) {
                ++failed;
                std::cout << "   FAIL trace differs from golden\n";
                for (const std::string& line : describeDiff(want, result.trace)) {
                    std::cout << line << "\n";
                }
            }
        }
    }

    std::cout << "\n" << (checks - failed) << "/" << checks << " checks passed across "
              << files.size() << " scenarios\n";
    return failed == 0 ? 0 : 1;
}
