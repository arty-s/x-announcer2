// The offline bench: scenarios are DATA, not code.
//
// This is the single most important design decision in the project's testing.
// In 1.x the bench was Python driving Lua, and it only existed because the
// plugin was Lua. Writing the scenarios as a small text format instead means:
//
//   * the same file can be flown by this C++ core AND by the 1.x Lua plugin,
//     and the two traces diffed - which is the only real guard against the port
//     quietly changing behaviour;
//   * a new scenario costs a text file, not a rebuild;
//   * the trace is a stable artefact that can be committed and diffed.
//
// Format (one directive per line, '#' starts a comment):
//
//   config  key=value ...      auto_boarding=1 door_calls=0 boarding_repeat=90
//   library Event[:seconds]    which announcements exist, and how long they play
//   set     field=value ...    battery=1 nav=1 alt_ft=35000 seatbelt=1
//   advance <seconds> [rate=N] N is X-Plane's time acceleration (2x, 4x)
//   expect  phase PHASE_ID
//   expect  played Event
//   expect  not-played Event
//   expect  playing Event | idle
#pragma once

#include <string>
#include <vector>

namespace xa::test {

struct ScenarioResult {
    std::string name;
    std::vector<std::string> trace;
    std::vector<std::string> failures;
    bool ok() const { return failures.empty(); }
};

// Runs one scenario file. Never throws for scenario-level problems: a bad
// directive is reported as a failure so one broken file cannot hide the rest.
ScenarioResult runScenarioFile(const std::string& path);

// Same scenario text, used by the determinism check.
ScenarioResult runScenarioText(const std::string& name, const std::string& text);

}  // namespace xa::test
