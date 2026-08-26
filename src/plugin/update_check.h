// Asking the update channel whether a newer build exists.
//
// The whole point of this is the person on 2.0.1 whose seatbelt sign never
// worked: the fix had been published for four days, his install carried the
// updater's cfg, and nothing on his screen ever said so. SkunkCrafts finds that
// out only if the updater is actually installed in the X-Plane root, which is
// exactly what such a person does not have.
//
// So: one HTTPS GET of a 124-byte static file, started when the panel is opened
// for the first time in a session and never again. Not at start-up - somebody
// who never opens the window has not asked this plugin anything, and should not
// be made to talk to the network for an answer they will not read. Switchable
// off entirely with `update_check` in config.ini.
//
// The WinHTTP dance is spelled out again here rather than shared with report.cpp
// on purpose: that path sends a user's log, has no test the bench can run, and
// had just shipped. Two copies of forty lines is cheaper than one refactor that
// can only be verified inside a simulator.
#pragma once

#include <string>

namespace xa::update {

enum class State {
    Idle,      // not asked yet, or asking is switched off
    Checking,  // the GET is in flight
    Current,   // the channel's version is ours, or older than ours
    Outdated,  // the channel has something newer - the only state that is shown
    Failed,    // no answer, or an answer nobody can read; the panel stays quiet
};

struct Status {
    State state = State::Idle;
    std::string latest;   // what the channel said, when it said anything
    std::string message;  // why it failed, for the log
};

// Starts a check. Does nothing if one has already run or is running in this
// session: the answer does not change while X-Plane is up, and a panel that
// re-asked on every frame would be a small denial of service against its own
// author. Returns false if it did nothing.
bool start(const std::string& installedVersion);

// A snapshot the UI can read every frame.
Status status();

// Joins the worker. Called from XPluginStop for the same reason report:: does:
// a thread writing into our statics while X-Plane unloads the DLL takes the
// simulator with it.
void shutdown();

}  // namespace xa::update
