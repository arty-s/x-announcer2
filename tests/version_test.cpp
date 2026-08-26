// Whether the panel is allowed to tell somebody they are out of date.
//
// Both ways this fails are silent and both are worse than no feature at all: a
// lexicographic comparison calls 2.0.10 older than 2.0.9 and nags a person who
// is already current, and a garbage answer read as a version nags everybody.
// So the two rules under test are "compare numerically" and "when the answer
// cannot be read, say nothing" - the second is the same rule the trigger table
// runs on: a signal nobody can read must not forbid, or claim, anything.
#include "version_test.h"

#include <iostream>
#include <string>
#include <vector>

#include "core/version.h"

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

using core::VersionOrder;

VersionOrder order(const std::string& mine, const std::string& theirs) {
    return core::compareVersions(mine, theirs);
}

}  // namespace

void runVersionChecks(int* checks, int* failed) {
    g_checks = checks;
    g_failed = failed;
    std::cout << "-- version comparison\n";

    check(order("2.0.4", "2.0.4") == VersionOrder::Same, "the same version is the same version");
    check(order("2.0.1", "2.0.4") == VersionOrder::Older, "2.0.1 is behind 2.0.4");
    check(order("2.0.4", "2.0.1") == VersionOrder::Newer,
          "and a build ahead of the channel says so instead of claiming to be behind");

    // The one that a string comparison gets wrong, and the reason this lives
    // under the bench at all.
    check(order("2.0.9", "2.0.10") == VersionOrder::Older,
          "2.0.10 is newer than 2.0.9 - compared as numbers, not as text");
    check(order("2.0.10", "2.0.9") == VersionOrder::Newer, "and the same the other way round");
    check(order("2.10.0", "2.9.0") == VersionOrder::Newer, "the middle component counts too");

    // The channel writes "v2.0.4"; version.h writes "2.0.4". If the v were not
    // handled, every install would be told it is out of date forever.
    check(order("2.0.4", "v2.0.4") == VersionOrder::Same,
          "the channel's leading v is not a difference");
    check(order("v2.0.4", "2.0.4") == VersionOrder::Same, "and neither is ours");

    // Both entry points have carried "-beta" in the file name since 2.0.0.
    check(order("2.0.4", "2.0.4-beta") == VersionOrder::Same,
          "a suffix that never varies orders nothing");
    check(order("2.1", "2.1.0") == VersionOrder::Same, "a missing component counts as zero");
    check(order("2.1", "2.1.1") == VersionOrder::Older, "but a present one still counts");

    // Anything unreadable must produce Unknown, and the panel says nothing on
    // Unknown. A captive-portal login page is the realistic case: it answers
    // 200 with HTML, and "behind" would be a lie told to everyone on hotel wifi.
    check(order("2.0.4", "") == VersionOrder::Unknown, "an empty answer is not a version");
    check(order("2.0.4", "<!DOCTYPE html>") == VersionOrder::Unknown,
          "nor is a portal's login page");
    check(order("2.0.4", "unknown") == VersionOrder::Unknown, "nor is a word");
    check(order("", "2.0.4") == VersionOrder::Unknown, "and an unreadable OURS is unknown too");

    check(!core::parseVersion("beta", nullptr), "parseVersion refuses what has no number");
    std::vector<int> parts;
    check(core::parseVersion("v2.0.4-beta", &parts) && parts.size() == 3 && parts[0] == 2 &&
              parts[1] == 0 && parts[2] == 4,
          "and reads v2.0.4-beta as 2, 0, 4");

    // The cfg is what the channel actually serves, byte for byte.
    const std::string cfg =
        "name|X-Announcer 2\nversion|v2.0.4\nmodule|https://xvatrus.ru/xannouncer/update\n"
        "zone|custom\nlocked|false\ndisabled|false\n";
    check(core::versionFromUpdaterCfg(cfg) == "v2.0.4", "the version line is found in the cfg");
    check(core::versionFromUpdaterCfg("name|X-Announcer 2\r\nversion|v2.0.4\r\n") == "v2.0.4",
          "and CRLF does not leave a carriage return glued to the number");
    check(core::versionFromUpdaterCfg("name|X-Announcer 2\n").empty(),
          "a cfg with no version line yields nothing, so the check reports a failure");
    check(core::versionFromUpdaterCfg("<html>not the cfg</html>").empty(),
          "and so does something that is not a cfg at all");
}

}  // namespace xa::test
