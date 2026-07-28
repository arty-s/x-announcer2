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

namespace xa {

class FileSoundLibrary : public core::SoundLibrary {
public:
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

    bool has(const std::string& event) const override;
    double duration(const std::string& event) const override;

    // Absolute path of the file that would play, or empty.
    std::string pathFor(const std::string& event) const;

    const std::string& root() const { return root_; }
    const std::string& pack() const { return pack_; }
    // Which language folder the current pack is reading, empty if none.
    std::string packLanguage() const;
    std::vector<std::string> packs() const;
    int eventCount() const;

private:
    struct Pack {
        std::map<std::string, std::string> events;  // event -> absolute path
        std::string language;                       // language folder read, if any
    };

    std::string root_;
    std::string language_;
    std::string pack_;
    std::map<std::string, Pack> packs_;

    // Probing opens the file, so the answer is remembered. Announcements are
    // asked for their length exactly when they start, and a disk read there is
    // a stutter in the frame.
    mutable std::map<std::string, double> durations_;
};

// "AfterTakeoff[Night][2].ogg" -> "AfterTakeoff". Returns empty if the name is
// not one of the canonical events; unknown files are ignored, never guessed at.
std::string eventFromFilename(const std::string& filename);

}  // namespace xa
