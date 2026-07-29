#pragma once

namespace xa::test {

void runPackLayoutChecks(int* checks, int* failed);

// Which of several files for one announcement plays. Called by the above; it
// shares its counters, so it takes none.
void runVariantChecks();

}  // namespace xa::test
