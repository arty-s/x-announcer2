// One place for the version string, so the log, the panel and a report all name
// the same build. 1.x is versioned separately and is at 1.2.1.
#pragma once

namespace xa {

// No "-dev" suffix any more: real people are flying this, and the SkunkCrafts
// updater compares this number against the one the module publishes.
constexpr const char* kPluginVersion = "2.0.3";

}  // namespace xa
