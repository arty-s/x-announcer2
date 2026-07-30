// Sending the log to the author, on request.
//
// The trigger is a button a human presses, never a timer and never a crash
// handler: this plugin talks to the network only when its user has just decided
// it should. What goes out is decided in core/report_body.h, where the bench can
// check it against the promise the panel makes.
//
// The whole exchange happens on a worker thread. A send that stutters the frame
// is a send nobody will press twice.
#pragma once

#include <string>

#include "core/report_body.h"

namespace xa::report {

// Everything the sender needs, gathered on the main thread because most of it
// comes from XPLM calls, which are the simulator's to answer and not a worker's
// to ask.
struct Input {
    core::ReportMeta meta;
    std::string logPath;   // X-Plane's Log.txt
    std::string copyPath;  // where to leave a copy, next to the plugin
};

enum class State {
    Idle,
    Sending,
    Sent,
    Failed,
};

struct Status {
    State state = State::Idle;
    std::string ticket;   // "XA-20260730-CE380A" - what the user quotes
    std::string message;  // the reason, when it failed; progress, while sending
};

// Starts a send. Does nothing and returns false if one is already in flight.
bool send(const Input& input);

// A snapshot the UI can read every frame.
Status status();

// Joins the worker. Called from XPluginStop: a thread still writing into our
// statics while X-Plane unloads the DLL is the one bug here that would take the
// simulator with it.
void shutdown();

}  // namespace xa::report
