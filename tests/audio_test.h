#pragma once

namespace xa::test {

// Decoder checks. `libraryDir` may be empty or missing - the synthetic checks
// still run, and the real-file checks report themselves as skipped rather than
// silently passing.
void runAudioChecks(const char* libraryDir, int* checks, int* failed);

}  // namespace xa::test
