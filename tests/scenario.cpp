#include "scenario.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>

#include "core/engine.h"

namespace xa::test {
namespace {

using namespace xa::core;

// A library that answers only what the scenario declared.
//
// The two worst bugs 1.x shipped both came from a stub that was more generous
// than reality - a separator X-Plane never produces, an ImGui binding that
// answered to any name at all. So this one refuses anything not listed, and the
// scenario has to say out loud which sounds a pack contains.
class ScriptedLibrary : public SoundLibrary {
public:
    void declare(const std::string& event, double seconds) { entries_[event] = seconds; }
    void clear() { entries_.clear(); }

    bool has(const std::string& event) const override { return entries_.count(event) != 0; }

    double duration(const std::string& event) const override {
        const auto it = entries_.find(event);
        return it == entries_.end() ? 0.0 : it->second;
    }

private:
    std::map<std::string, double> entries_;
};

std::vector<std::string> splitWords(const std::string& line) {
    std::vector<std::string> out;
    std::istringstream stream(line);
    std::string word;
    while (stream >> word) {
        out.push_back(word);
    }
    return out;
}

bool splitPair(const std::string& token, std::string* key, std::string* value) {
    const std::size_t eq = token.find('=');
    if (eq == std::string::npos) {
        return false;
    }
    *key = token.substr(0, eq);
    *value = token.substr(eq + 1);
    return true;
}

bool asBool(const std::string& v) { return v == "1" || v == "true" || v == "yes"; }

class Runner {
public:
    explicit Runner(std::string name) : name_(std::move(name)) {}

    ScenarioResult run(std::istream& input) {
        std::string line;
        int lineNo = 0;
        while (std::getline(input, line)) {
            ++lineNo;
            const std::size_t hash = line.find('#');
            if (hash != std::string::npos) {
                line = line.substr(0, hash);
            }
            const std::vector<std::string> words = splitWords(line);
            if (words.empty()) {
                continue;
            }
            handle(words, lineNo);
        }
        ScenarioResult result;
        result.name = name_;
        result.trace = trace_;
        result.failures = failures_;
        return result;
    }

private:
    void fail(int lineNo, const std::string& message) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "line %d: ", lineNo);
        failures_.push_back(buf + message);
    }

    Engine& engine() {
        if (!engine_) {
            engine_ = std::make_unique<Engine>(config_, library_);
        }
        return *engine_;
    }

    void handle(const std::vector<std::string>& words, int lineNo) {
        const std::string& verb = words[0];
        if (verb == "config") {
            if (engine_) {
                fail(lineNo, "config must come before the first advance");
                return;
            }
            for (std::size_t i = 1; i < words.size(); ++i) {
                std::string key;
                std::string value;
                if (!splitPair(words[i], &key, &value)) {
                    fail(lineNo, "expected key=value, got '" + words[i] + "'");
                    continue;
                }
                applyConfig(key, value, lineNo);
            }
        } else if (verb == "library") {
            for (std::size_t i = 1; i < words.size(); ++i) {
                const std::string& token = words[i];
                const std::size_t colon = token.find(':');
                if (colon == std::string::npos) {
                    library_.declare(token, 8.0);
                } else {
                    library_.declare(token.substr(0, colon), std::stod(token.substr(colon + 1)));
                }
            }
        } else if (verb == "set") {
            for (std::size_t i = 1; i < words.size(); ++i) {
                std::string key;
                std::string value;
                if (!splitPair(words[i], &key, &value)) {
                    fail(lineNo, "expected key=value, got '" + words[i] + "'");
                    continue;
                }
                applySet(key, value, lineNo);
            }
        } else if (verb == "advance") {
            if (words.size() < 2) {
                fail(lineNo, "advance needs a duration");
                return;
            }
            double rate = 1.0;
            for (std::size_t i = 2; i < words.size(); ++i) {
                std::string key;
                std::string value;
                if (splitPair(words[i], &key, &value) && key == "rate") {
                    rate = std::stod(value);
                }
            }
            advance(std::stod(words[1]), rate);
        } else if (verb == "event") {
            // Things the simulator tells the plugin rather than shows in a
            // dataref. Only one so far: the user swapped aeroplanes.
            if (words.size() < 2) {
                fail(lineNo, "event needs a name");
                return;
            }
            if (words[1] == "plane_loaded" || words[1] == "airport_loaded") {
                engine().restartFlight(words[1] == "plane_loaded" ? "new aircraft" : "new airport");
                trace_.push_back("event " + words[1]);
            } else {
                fail(lineNo, "unknown event '" + words[1] + "'");
            }
        } else if (verb == "expect") {
            expect(words, lineNo);
        } else {
            fail(lineNo, "unknown directive '" + verb + "'");
        }
    }

    void applyConfig(const std::string& key, const std::string& value, int lineNo) {
        if (key == "enabled") { config_.enabled = asBool(value); }
        else if (key == "boarding_music") { config_.boardingMusic = asBool(value); }
        else if (key == "cabin_noise") { config_.cabinNoise = asBool(value); }
        else if (key == "auto_boarding") { config_.autoBoarding = asBool(value); }
        else if (key == "pilot_welcome") { config_.pilotWelcome = asBool(value); }
        else if (key == "door_calls") { config_.doorCalls = asBool(value); }
        else if (key == "night_dim") { config_.nightDim = asBool(value); }
        else if (key == "landing_reaction") { config_.landingReaction = asBool(value); }
        else if (key == "boarding_repeat") { config_.boardingRepeat = std::stod(value); }
        else if (key == "music_max_loops") { config_.musicMaxLoops = std::stoi(value); }
        else { fail(lineNo, "unknown config key '" + key + "'"); }
    }

    void applySet(const std::string& key, const std::string& value, int lineNo) {
        Snapshot& s = sim_;
        if (key == "on_ground") { s.onGround = asBool(value); }
        else if (key == "gs_kt") { s.gsKt = std::stod(value); }
        else if (key == "agl_ft") { s.aglFt = std::stod(value); }
        else if (key == "alt_ft") { s.altFt = std::stod(value); }
        else if (key == "vs_fpm") { s.vsFpm = std::stod(value); }
        else if (key == "g") { s.gNormal = std::stod(value); }
        else if (key == "beacon") { s.beacon = asBool(value); }
        else if (key == "nav") { s.navLights = asBool(value); }
        else if (key == "strobe") { s.strobe = asBool(value); }
        else if (key == "landing") { s.landingLight = asBool(value); }
        else if (key == "taxi") { s.taxiLight = asBool(value); }
        else if (key == "logo") { s.logo = asBool(value); }
        else if (key == "logo_dref") { s.logoDrefExists = asBool(value); }
        else if (key == "parkbrake") { s.parkbrake = asBool(value); }
        else if (key == "battery") { s.battery = asBool(value); }
        else if (key == "engines") { s.enginesRunning = std::stoi(value); }
        else if (key == "hour") { s.localHour = std::stoi(value); }
        else if (key == "paused") { s.paused = asBool(value); }
        else if (key == "replay") { s.replay = asBool(value); }
        else if (key == "seatbelt") {
            // -1 is not "off": an aircraft that publishes no sign at all must not
            // look like one whose sign is switched off, or the first read fires
            // a PA on a transition that never happened.
            if (value == "-1" || value == "none") { s.seatbelt = Tri::Unknown; }
            else { s.seatbelt = asBool(value) ? Tri::On : Tri::Off; }
        } else {
            fail(lineNo, "unknown sim field '" + key + "'");
        }
    }

    // Mirrors 1.x's advance(): frame callback at 30 Hz, state tick at 1 Hz, and
    // the wall clock ticking in whole seconds exactly as os.time() does.
    void advance(double seconds, double rate) {
        Engine& e = engine();
        const int fps = 30;
        const double step = 1.0 / fps;
        const int frames = static_cast<int>(seconds * fps);
        for (int i = 0; i < frames; ++i) {
            const double wallDt = ((i + 1) % fps == 0) ? 1.0 : 0.0;
            e.frame(sim_, step * rate, wallDt);

            if (i % fps == 0) {
                const Phase before = e.phase();
                const bool allMetBefore = allConditionsMet(e);
                e.tick(sim_);
                checkWidgetAgreement(e, before, allMetBefore);
            }
            collect(e);
        }
    }

    bool allConditionsMet(const Engine& e) const {
        for (const Condition& c : e.phaseConditions(sim_)) {
            if (!c.met) {
                return false;
            }
        }
        return true;
    }

    // The invariant that catches drift between the widget and the machine: a
    // phase must never advance to its declared next phase while the condition
    // list still reports something unmet. If it does, the widget is lying to the
    // user about what the plugin is waiting for.
    void checkWidgetAgreement(const Engine& e, Phase before, bool allMetBefore) {
        if (e.phase() == before || allMetBefore) {
            return;
        }
        static const std::map<Phase, Phase> next = {
            {Phase::Preflight, Phase::Boarding}, {Phase::Boarding, Phase::Pushback},
            {Phase::Pushback, Phase::Takeoff},   {Phase::Takeoff, Phase::Climb},
            {Phase::Climb, Phase::Cruise},       {Phase::Cruise, Phase::Descent},
            {Phase::Descent, Phase::Approach},   {Phase::Approach, Phase::TaxiIn},
            {Phase::TaxiIn, Phase::Disembark},   {Phase::Disembark, Phase::Preflight},
        };
        const auto it = next.find(before);
        if (it == next.end() || it->second != e.phase()) {
            return;  // a shortcut (CLIMB->DESCENT) or a resync, not the widget's claim
        }
        failures_.push_back(std::string("widget disagrees with the machine: ") +
                            phaseId(before) + " -> " + phaseId(e.phase()) +
                            " happened while a condition was still unmet");
    }

    void collect(Engine& e) {
        for (const Intent& intent : e.drainIntents()) {
            if (intent.kind == Intent::Kind::PlayAnnouncement) {
                ++played_[intent.event];
            }
            if (intent.kind == Intent::Kind::Note) {
                continue;  // notes are diagnostics, not behaviour
            }
            trace_.push_back(formatIntent(intent));
        }
    }

    void expect(const std::vector<std::string>& words, int lineNo) {
        if (words.size() < 2) {
            fail(lineNo, "expect needs a subject");
            return;
        }
        Engine& e = engine();
        const std::string& what = words[1];
        if (what == "phase") {
            if (words.size() < 3) { fail(lineNo, "expect phase needs a phase id"); return; }
            const std::string actual = phaseId(e.phase());
            if (actual != words[2]) {
                fail(lineNo, "expected phase " + words[2] + ", got " + actual);
            }
        } else if (what == "played") {
            if (words.size() < 3) { fail(lineNo, "expect played needs an event"); return; }
            if (played_.count(words[2]) == 0) {
                fail(lineNo, words[2] + " never played");
            }
        } else if (what == "not-played") {
            if (words.size() < 3) { fail(lineNo, "expect not-played needs an event"); return; }
            if (played_.count(words[2]) != 0) {
                fail(lineNo, words[2] + " played but should not have");
            }
        } else if (what == "play-count") {
            if (words.size() < 4) {
                fail(lineNo, "expect play-count needs an event and a number");
                return;
            }
            const auto it = played_.find(words[2]);
            const int actual = it == played_.end() ? 0 : it->second;
            const int wanted = std::stoi(words[3]);
            if (actual != wanted) {
                fail(lineNo, words[2] + " played " + std::to_string(actual) + " times, expected " +
                                 std::to_string(wanted));
            }
        } else if (what == "playing") {
            if (words.size() < 3) { fail(lineNo, "expect playing needs an event or 'idle'"); return; }
            if (words[2] == "idle") {
                if (e.isPlaying()) { fail(lineNo, "expected silence, but " + e.playingEvent() + " is playing"); }
            } else if (e.playingEvent() != words[2]) {
                fail(lineNo, "expected " + words[2] + " playing, got " +
                             (e.isPlaying() ? e.playingEvent() : std::string("silence")));
            }
        } else {
            fail(lineNo, "unknown expectation '" + what + "'");
        }
    }

    std::string name_;
    Config config_;
    ScriptedLibrary library_;
    std::unique_ptr<Engine> engine_;
    Snapshot sim_;
    std::vector<std::string> trace_;
    std::vector<std::string> failures_;
    // Counted, not just remembered: "played again" is the only way a scenario
    // can tell that the flight was forgotten and started over, since the events
    // themselves look identical the second time round.
    std::map<std::string, int> played_;
};

}  // namespace

ScenarioResult runScenarioText(const std::string& name, const std::string& text) {
    std::istringstream stream(text);
    Runner runner(name);
    return runner.run(stream);
}

ScenarioResult runScenarioFile(const std::string& path) {
    std::ifstream file(path);
    ScenarioResult result;
    const std::size_t slash = path.find_last_of("/\\");
    result.name = slash == std::string::npos ? path : path.substr(slash + 1);
    if (!file) {
        result.failures.push_back("cannot open " + path);
        return result;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return runScenarioText(result.name, buffer.str());
}

}  // namespace xa::test
