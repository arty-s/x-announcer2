// The core's view of the sound library: does this announcement exist, and how
// long does it play?
//
// That is deliberately all. Decoding, packs, languages and file layout live on
// the plugin side; the bench substitutes a table of made-up durations and flies
// entire flights without touching a single audio file.
#pragma once

#include <string>

namespace xa::core {

class SoundLibrary {
public:
    virtual ~SoundLibrary() = default;

    virtual bool has(const std::string& event) const = 0;

    // Playback length in WALL-CLOCK seconds. Phases run on simulator time, but a
    // sentence takes as long as it takes: timing announcements on sim time cuts
    // them off mid-word at 2x and 4x.
    virtual double duration(const std::string& event) const = 0;
};

}  // namespace xa::core
