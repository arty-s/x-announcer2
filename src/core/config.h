// Settings the state machine consults. Defaults are exactly 1.x's, because the
// differential bench flies the same scenario through both and any difference in
// defaults would show up as a false failure - or worse, mask a real one.
#pragma once

namespace xa::core {

struct Config {
    bool enabled = true;
    bool boardingMusic = true;
    bool cabinNoise = false;
    bool autoBoarding = true;
    bool pilotWelcome = false;
    bool doorCalls = true;
    bool nightDim = true;
    bool landingReaction = true;

    double boardingRepeat = 300.0;  // seconds between BoardingWelcome repeats
    int musicMaxLoops = 6;
};

}  // namespace xa::core
