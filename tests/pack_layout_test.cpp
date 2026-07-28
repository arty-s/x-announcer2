// Which language folder inside a pack gets read.
//
// This was the first thing the settings work turned up that was not a setting:
// v2 walked a pack recursively and merged every language it found, so a pack
// shipping en-us and de-de announced in whichever the filesystem listed first.
// 1.x had always read exactly one folder, and the bench never saw the
// difference because the bench does not touch the disk.
#include "pack_layout_test.h"

#include <iostream>
#include <string>
#include <vector>

#include "core/pack_layout.h"

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

void runPackLayoutChecks(int* checks, int* failed) {
    g_checks = checks;
    g_failed = failed;
    std::cout << "-- pack layout\n";

    check(core::isLocaleFolder("ru") && core::isLocaleFolder("en-us") &&
              core::isLocaleFolder("de_DE") && core::isLocaleFolder("PT-BR"),
          "language folders are recognised in every spelling packs use");
    check(!core::isLocaleFolder("Sounds") && !core::isLocaleFolder("") &&
              !core::isLocaleFolder("e1") && !core::isLocaleFolder("en-usa") &&
              !core::isLocaleFolder("extra"),
          "an ordinary folder inside a pack is not mistaken for a language");

    check(core::chooseLocaleFolder({}, "en-us").empty(), "a pack with no sub-folders reads none");
    check(core::chooseLocaleFolder({"Sounds", "Extra"}, "en-us").empty(),
          "sub-folders that are not languages are ignored");
    check(core::chooseLocaleFolder({"en-us"}, "ru") == "en-us",
          "the only language folder is read even when another was asked for");
    check(core::chooseLocaleFolder({"en-us", "de-de"}, "de-de") == "de-de",
          "the language asked for wins when the pack has several");
    check(core::chooseLocaleFolder({"en-us", "de-de"}, "ru").empty(),
          "several languages and none of them ours: read none, do not guess");
    check(core::chooseLocaleFolder({"EN-US"}, "en-us") == "EN-US",
          "the match ignores case but the folder keeps its own");
    check(core::chooseLocaleFolder({"de-de", "en-us"}, "en-us") ==
              core::chooseLocaleFolder({"en-us", "de-de"}, "en-us"),
          "the answer does not depend on the order the disk listed them");
}

}  // namespace xa::test
