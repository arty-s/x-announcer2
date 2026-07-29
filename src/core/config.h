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

    // How long boarding may run before the cabin says the departure is delayed.
    // X-Plane knows nothing about schedules, so "we are still standing here" is
    // the only delay a plugin can honestly observe. 0 switches it off.
    //
    // Fifteen minutes is long enough that an ordinary turnaround never reaches
    // it, and short enough to be reachable by someone actually waiting - with
    // boarding_repeat at five minutes it lands after the third welcome.
    double delayAfter = 900.0;
};

}  // namespace xa::core
