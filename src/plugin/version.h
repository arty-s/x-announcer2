// One place for the version string, so the log, the panel and a report all name
// the same build. 1.x is versioned separately and is at 1.2.4.
#pragma once

namespace xa {

// No "-dev" suffix any more: real people are flying this, and the SkunkCrafts
// updater compares this number against the one the module publishes.
//
// The -beta-xp1244 build is the 12.4.4 one, handed out from the site only and
// deliberately absent from the update channel. The suffix earns its length in
// the report metadata: a report from this build says so by itself, which is the
// whole point of shipping it to a handful of people. Everything that compares
// versions stops at the first non-digit, so this still reads as 2.1.0 - ahead of
// the 2.0.8 in the channel, which is what keeps the panel from telling a beta
// tester they are behind and offering them the stable build.
constexpr const char* kPluginVersion = "2.1.0-beta-xp1244";

}  // namespace xa
