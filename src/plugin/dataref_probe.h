// Finds the cabin-sign trigger on an aeroplane nobody has ever tested.
//
// The candidate list in sim_state.cpp can only know the aeroplanes somebody has
// already looked at. For every other one the report from a user says "the sign
// does nothing" and there is no way to tell a wrong dataref from a switch that
// was flipped on the ground - the log is silent either way.
//
// So the plugin asks X-Plane for every dataref whose NAME could be about a
// trigger it needs - the cabin signs, the exterior lights, the battery, the
// park brake, the distance to destination - watches that shortlist, and writes
// down the ones that move. Signs alone were not enough: on an aeroplane that
// keeps its beacon to itself the whole departure goes quiet, and that shows up
// in a log exactly the same way as a wrong seat belt dataref did. The
// user flips the switch, sends the log, and the line
//
//     probe: laminar/B738/toggle_switch/seatbelt_sign_pos 0 -> 2
//
// names the dataref to add to the list. No guessing, no second round of
// questions, and it works the same on an aeroplane released tomorrow.
#pragma once

#include <string>
#include <vector>

namespace xa {

class DatarefProbe {
public:
    // Off writes nothing at all and keeps no list.
    void setEnabled(bool on);
    bool enabled() const { return enabled_; }

    // A new aeroplane: the old handles belong to it and are dropped.
    void reset();

    // Called every frame; does its own throttling. `wallSeconds` is wall time,
    // because this must keep working while the simulator is paused - that is
    // exactly when somebody sits and clicks the switch to see what happens.
    void poll(double wallSeconds);

private:
    struct Watched {
        void* ref = nullptr;
        std::string name;
        int type = 0;
        int topic = -1;
        double value = 0.0;
        int changes = 0;
        bool dropped = false;
    };

    void build(double wallSeconds);
    double readValue(const Watched& w) const;

    std::vector<Watched> watched_;
    bool enabled_ = true;
    double resetAt_ = -1.0;
    double nextPoll_ = 0.0;
    int builds_ = 0;
    int lines_ = 0;
};

}  // namespace xa
