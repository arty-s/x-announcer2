#pragma once

namespace xa::core {

enum class Phase {
    Preflight,
    Boarding,
    Pushback,
    Takeoff,
    Climb,
    Cruise,
    Descent,
    Approach,
    TaxiIn,
    Disembark,
};

// The 1.x identifiers, kept verbatim: traces from both versions are compared
// character by character, so these strings are part of the contract.
inline const char* phaseId(Phase p) {
    switch (p) {
        case Phase::Preflight: return "PREFLIGHT";
        case Phase::Boarding:  return "BOARDING";
        case Phase::Pushback:  return "PUSHBACK";
        case Phase::Takeoff:   return "TAKEOFF";
        case Phase::Climb:     return "CLIMB";
        case Phase::Cruise:    return "CRUISE";
        case Phase::Descent:   return "DESCENT";
        case Phase::Approach:  return "APPROACH";
        case Phase::TaxiIn:    return "TAXI_IN";
        case Phase::Disembark: return "DISEMBARK";
    }
    return "?";
}

inline bool isAirbornePhase(Phase p) {
    return p == Phase::Takeoff || p == Phase::Climb || p == Phase::Cruise ||
           p == Phase::Descent || p == Phase::Approach;
}

inline bool isGroundPhase(Phase p) {
    return p == Phase::Preflight || p == Phase::Boarding || p == Phase::Pushback ||
           p == Phase::TaxiIn || p == Phase::Disembark;
}

}  // namespace xa::core
