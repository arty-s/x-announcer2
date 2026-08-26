// Comparing the installed version against the one the update channel publishes.
//
// Pure string work, in the core and under the bench, because every way this can
// go wrong is silent. A comparison that reads "2.0.10" as older than "2.0.9"
// tells a person who is up to date that they are behind; a comparison that
// cannot read the channel's answer and guesses "behind" does the same. Both
// look like a working feature from the outside, which is why they are decided
// here rather than inside the thread that does the fetching.
#pragma once

#include <string>
#include <vector>

namespace xa::core {

// Reads "2.0.4", "v2.0.4", "2.0.4-beta" and "2.0.4 beta" into their numbers.
// Anything that does not begin with a number after the optional "v" is refused:
// a version nobody can read must not become a version everybody is compared to.
bool parseVersion(const std::string& text, std::vector<int>* out);

enum class VersionOrder {
    Same,
    Older,    // the first argument is behind the second
    Newer,    // ahead of it - a test build, or the channel rolled back
    Unknown,  // one of them could not be read; say nothing
};

// Numeric, component by component, missing components counted as zero, so
// "2.1" and "2.1.0" are the same build and "2.0.10" is newer than "2.0.9".
// A suffix after the numbers (`-beta`) is ignored: both entry points have
// carried "-beta" in the file name since 2.0.0 while the version itself never
// did, and a suffix that never varies cannot order anything.
VersionOrder compareVersions(const std::string& mine, const std::string& theirs);

// Pulls `version|v2.0.4` out of a skunkcrafts_updater.cfg. Empty if the file
// has no such line - the caller then reports a failed check, not a current one.
std::string versionFromUpdaterCfg(const std::string& cfg);

}  // namespace xa::core
