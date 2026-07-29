// The sound library on disk, as the core sees it.
//
// Layout is the one the MSFS Universal Announcer / Fenix packs already use, so
// Artyom's thirty packs work unchanged:
//
//     <library>/<PACK>/<Event>[tags].ogg|mp3|wav
//
// The pack normally follows the airline detected from the livery; the panel can
// pin it by hand instead. Until the first detection runs there is no airline to
// follow, so the first pack alphabetically stands in.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "core/library.h"
#include "core/pack_layout.h"

namespace xa {

class FileSoundLibrary : public core::SoundLibrary {
public:
    // What the panel needs to show one row of the library: how many files this
    // pack has for the event, which pack the sound would actually come from, and
    // the name of the file that would play.
    struct Coverage {
        std::string event;
        int own = 0;       // files in the selected pack
        int fallback = 0;  // files in Default
        std::string source;  // pack the file comes from, empty when there is none
        std::string file;    // file name that would play
    };

    // Scans `root` for packs. `language` names the language sub-folder to read
    // inside each pack when it has more than one. Returns the number of packs.
    int scan(const std::string& root, const std::string& language);

    // Chooses the pack to play from. An unknown name keeps the current one and
    // says so in the log rather than falling silent.
    bool selectPack(const std::string& pack);

    // Picks the pack for a detected airline: its own if we have one, otherwise
    // Default. Recognising the airline and owning its sounds are separate
    // questions, and conflating them is what once made a recognised S7 livery
    // report itself as undetected.
    void selectPackForAirline(const std::string& icao);

    // The aeroplane being flown and the time of day, which is what decides
    // between [Night] and [A320] variants of the same announcement. Changing
    // either re-picks the files, so the panel never shows one and plays another.
    void setPlayContext(const core::PlayContext& context);
    const core::PlayContext& playContext() const { return context_; }

    // Moves to the next variant of every event that has more than one. Called
    // once per flight rather than per announcement: within a flight the choice
    // has to hold still, because the length of the file is measured when the
    // announcement is scheduled and the file itself is opened a moment later.
    void nextVariantRound();

    // Whether an announcement missing from the selected pack is taken from
    // Default. Off means the flight simply has no such announcement, which the
    // state machine already knows how to survive.
    void setDefaultFallback(bool allowed);

    bool has(const std::string& event) const override;
    double duration(const std::string& event) const override;

    // Absolute path of the file that would play, or empty. Falls back to the
    // Default pack, as 1.x does, when the selected pack has nothing.
    std::string pathFor(const std::string& event) const;

    const std::string& root() const { return root_; }
    const std::string& pack() const { return pack_; }
    // Which language folder the current pack is reading, empty if none.
    std::string packLanguage() const;
    std::vector<std::string> packs() const;
    int eventCount() const;

    // Every canonical event with what the current pack can do about it, in the
    // order the flight goes through them. This is the library tab.
    std::vector<Coverage> coverage() const;

private:
    struct Entry {
        std::string path;
        core::SoundVariant variant;
    };
    struct Pack {
        std::map<std::string, std::vector<Entry>> events;  // event -> its files
        std::string language;                              // language folder read, if any
    };

    // Which pack and which file an event resolves to right now, or nulls.
    const Entry* resolve(const std::string& event, std::string* fromPack) const;
    const Entry* pickFrom(const Pack& pack, const std::string& event) const;

    std::string root_;
    std::string language_;
    std::string pack_;
    std::map<std::string, Pack> packs_;
    core::PlayContext context_;
    unsigned round_ = 0;
    bool defaultFallback_ = true;

    // Probing opens the file, so the answer is remembered. Announcements are
    // asked for their length exactly when they start, and a disk read there is
    // a stutter in the frame.
    mutable std::map<std::string, double> durations_;
};

// The canonical events in flight order, for the panel's library table.
const std::vector<std::string>& soundEventNames();

}  // namespace xa
