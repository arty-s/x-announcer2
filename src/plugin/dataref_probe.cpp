#include "plugin/dataref_probe.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "XPLMDataAccess.h"

#include "plugin/xa_log.h"

namespace xa {
namespace {

// The probe watches by TOPIC, not by one keyword list, and each topic has a
// budget of its own. One list would have let a talkative aeroplane fill the
// whole quota with landing-gear animations and leave nothing for the cabin
// signs - and the topic that overflows is named in the log rather than silently
// truncated.
//
// The words are the ones add-on authors actually use: seatbelt_sign_pos,
// fasten_seat_belts, seatbeltLight, sign_belts, beaconLightSwitch, strobe_lts,
// position_light_pos, parkbrk, dist_dest. "seat" alone is not here on purpose -
// it matches every seat position in the cockpit; neither is bare "light", which
// matches half the panel; nor bare "landing", which is mostly the gear.
struct Topic {
    const char* id;
    const char* const* words;
    std::size_t wordCount;
    std::size_t budget;
};

const char* const kSignWords[] = {"belt", "fasten", "smok"};
const char* const kLightWords[] = {"beacon", "strobe", "taxi_light", "taxilight", "logo",
                                   "position_light", "nav_light", "navlight", "navlights",
                                   "land_light", "landing_light", "landinglight", "lights_on"};
const char* const kPowerWords[] = {"battery", "batt_on", "gpu", "avionics_on"};
const char* const kBrakeWords[] = {"parkbrake", "park_brake", "parkbrk", "parking_brake"};
const char* const kRouteWords[] = {"dist_dest", "dist_to_dest", "distance_dest", "todest"};

const Topic kTopics[] = {
    {"signs", kSignWords, sizeof(kSignWords) / sizeof(kSignWords[0]), 48},
    {"lights", kLightWords, sizeof(kLightWords) / sizeof(kLightWords[0]), 64},
    {"power", kPowerWords, sizeof(kPowerWords) / sizeof(kPowerWords[0]), 24},
    {"brake", kBrakeWords, sizeof(kBrakeWords) / sizeof(kBrakeWords[0]), 12},
    {"route", kRouteWords, sizeof(kRouteWords) / sizeof(kRouteWords[0]), 8},
};

constexpr std::size_t kTopicCount = sizeof(kTopics) / sizeof(kTopics[0]);

// Enough to hold every spelling on any one aeroplane, small enough that reading
// them all several times a second costs nothing. Overflow is reported, never
// silently dropped.
constexpr std::size_t kMaxWatched = 160;
// A whole session's budget. A dataref that is animated rather than switched can
// change every frame, and a log that scrolls past the interesting line is as
// useless as no log at all.
constexpr int kMaxLines = 200;
constexpr int kMaxChangesPerRef = 12;

// Which topic this name belongs to, or -1 for none. First match wins; the
// topics do not overlap in practice.
int topicOf(const char* name) {
    if (name == nullptr) {
        return -1;
    }
    std::string lower(name);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (std::size_t i = 0; i < kTopicCount; ++i) {
        for (std::size_t w = 0; w < kTopics[i].wordCount; ++w) {
            if (lower.find(kTopics[i].words[w]) != std::string::npos) {
                return static_cast<int>(i);
            }
        }
    }
    return -1;
}

// Scalars only. An array dataref would need a size and an index to mean
// anything, and no cabin sign in the wild is published as one.
bool typeIsReadable(XPLMDataTypeID type) {
    return (type & (xplmType_Int | xplmType_Float | xplmType_Double)) != 0 &&
           (type & (xplmType_IntArray | xplmType_FloatArray | xplmType_Data)) == 0;
}

std::string formatValue(double value) {
    char text[32];
    if (std::fabs(value - std::floor(value + 0.5)) < 0.001) {
        std::snprintf(text, sizeof(text), "%d", static_cast<int>(std::floor(value + 0.5)));
    } else {
        std::snprintf(text, sizeof(text), "%.3f", value);
    }
    return text;
}

}  // namespace

void DatarefProbe::setEnabled(bool on) {
    if (enabled_ == on) {
        return;
    }
    enabled_ = on;
    reset();
}

void DatarefProbe::reset() {
    watched_.clear();
    resetAt_ = -1.0;
    nextPoll_ = 0.0;
    builds_ = 0;
    lines_ = 0;
}

void DatarefProbe::poll(double wallSeconds) {
    if (!enabled_) {
        return;
    }
    if (resetAt_ < 0.0) {
        resetAt_ = wallSeconds;
    }
    const double since = wallSeconds - resetAt_;
    // Twice: once when the aeroplane has had a moment to register its own
    // datarefs, and once well after, because some register theirs on the first
    // frame the engines are asked for and not before.
    if ((builds_ == 0 && since > 3.0) || (builds_ == 1 && since > 30.0)) {
        build(wallSeconds);
    }
    if (wallSeconds < nextPoll_ || watched_.empty()) {
        return;
    }
    nextPoll_ = wallSeconds + 0.25;

    for (Watched& w : watched_) {
        if (w.dropped) {
            continue;
        }
        const double now = readValue(w);
        if (std::fabs(now - w.value) < 0.0005) {
            continue;
        }
        const double was = w.value;
        w.value = now;
        if (++w.changes > kMaxChangesPerRef) {
            w.dropped = true;
            log("probe: %s меняется непрерывно - снимаю с наблюдения", w.name.c_str());
            ++lines_;
            continue;
        }
        if (lines_ >= kMaxLines) {
            continue;
        }
        log("probe: %s %s -> %s", w.name.c_str(), formatValue(was).c_str(),
            formatValue(now).c_str());
        if (++lines_ == kMaxLines) {
            log("probe: строк набралось %d, дальше молчу - этого хватит, чтобы назвать датареф",
                kMaxLines);
        }
    }
}

void DatarefProbe::build(double wallSeconds) {
    ++builds_;
    nextPoll_ = wallSeconds;

    const int total = XPLMCountDataRefs();
    if (total <= 0) {
        return;
    }
    std::vector<XPLMDataRef> all(static_cast<std::size_t>(total), nullptr);
    XPLMGetDataRefsByIndex(0, total, all.data());

    int added = 0;
    int skipped = 0;
    std::size_t perTopic[kTopicCount] = {0};
    for (const Watched& w : watched_) {
        if (w.topic >= 0 && static_cast<std::size_t>(w.topic) < kTopicCount) {
            ++perTopic[w.topic];
        }
    }
    for (XPLMDataRef ref : all) {
        if (ref == nullptr) {
            continue;
        }
        XPLMDataRefInfo_t info;
        std::memset(&info, 0, sizeof(info));
        info.structSize = sizeof(info);
        XPLMGetDataRefInfo(ref, &info);
        const int topic = topicOf(info.name);
        if (topic < 0 || !typeIsReadable(info.type)) {
            continue;
        }
        const bool known = std::any_of(watched_.begin(), watched_.end(), [&](const Watched& w) {
            return w.ref == ref;
        });
        if (known) {
            continue;
        }
        if (watched_.size() >= kMaxWatched ||
            perTopic[topic] >= kTopics[topic].budget) {
            ++skipped;
            continue;
        }
        Watched w;
        w.ref = ref;
        w.name = info.name != nullptr ? info.name : "";
        w.type = static_cast<int>(info.type);
        w.topic = topic;
        w.value = readValue(w);
        watched_.push_back(w);
        ++perTopic[topic];
        ++added;
        // The starting values are printed too: half the reports arrive with the
        // switch already where the user left it, and then the only evidence is
        // what each candidate read at the start.
        if (lines_ < kMaxLines) {
            log("probe: %s = %s", w.name.c_str(), formatValue(w.value).c_str());
            ++lines_;
        }
    }
    if (added > 0 || skipped > 0) {
        std::string byTopic;
        for (std::size_t i = 0; i < kTopicCount; ++i) {
            if (perTopic[i] == 0) {
                continue;
            }
            if (!byTopic.empty()) {
                byTopic += ", ";
            }
            byTopic += std::string(kTopics[i].id) + " " + std::to_string(perTopic[i]);
        }
        log("probe: под наблюдением %d датарефов (%s)%s", static_cast<int>(watched_.size()),
            byTopic.c_str(), skipped > 0 ? "; список переполнен, часть не взята" : "");
    }
}

double DatarefProbe::readValue(const Watched& w) const {
    XPLMDataRef ref = static_cast<XPLMDataRef>(w.ref);
    if (ref == nullptr) {
        return 0.0;
    }
    if ((w.type & xplmType_Int) != 0) {
        return static_cast<double>(XPLMGetDatai(ref));
    }
    if ((w.type & xplmType_Double) != 0) {
        return XPLMGetDatad(ref);
    }
    return static_cast<double>(XPLMGetDataf(ref));
}

}  // namespace xa
