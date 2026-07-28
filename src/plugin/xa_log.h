// Logging for X-Announcer 2.
//
// Everything goes to X-Plane's Log.txt through XPLMDebugString, prefixed so it
// can be grepped, and is mirrored into a ring buffer that the in-plugin Log tab
// reads. In 1.x every live diagnosis started from those Log.txt lines, so the
// rule stands: if a decision is not obvious from the log, the log is wrong.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace xa {

// Writes one line: "X-Announcer2: <formatted>\n".
void log(const char* fmt, ...);

// Newest-last copy of the ring buffer (for the UI).
std::vector<std::string> logTail(std::size_t maxLines = 200);

}  // namespace xa
