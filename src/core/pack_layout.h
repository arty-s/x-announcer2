// How a sound pack is laid out on disk - the part of it that is a decision
// rather than a directory listing.
//
// A pack may keep its files straight inside its folder, or split them into
// language sub-folders (en-us, de-de, ru). Which one to read is a choice, and a
// choice belongs in the core where the bench can check it; walking the disk
// stays in the plugin.
#pragma once

#include <string>
#include <vector>

namespace xa::core {

// "en-us", "de_DE", "ru" - two letters, optionally a separator and two more.
// Anything else inside a pack is somebody's own folder and is left alone.
bool isLocaleFolder(const std::string& name);

// Picks the language folder to read, following 1.x: the one asked for if it is
// there, otherwise the only one there is. Returns empty when the pack has no
// language folders, or has several and none of them is the one wanted - reading
// a language the user did not ask for would be worse than reading none.
std::string chooseLocaleFolder(const std::vector<std::string>& folders,
                               const std::string& wanted);

// ---------------------------------------------------------------- variants --
//
// A pack may hold several files for the same announcement, and say in the file
// name when each of them fits: "AfterTakeoff[Night].ogg", "SafetyBriefing[A320]
// [2].ogg". Until now v2 took whichever came first off the disk, so a pack with
// a day and a night version played one of them at random - the announcement was
// right and the moment was wrong.
//
// Which tags mean what is 1.x's, unchanged: the packs on people's disks were
// written for it.

struct SoundVariant {
    std::string name;                    // the file name, for the panel and the log
    std::vector<std::string> aircraft;   // upper case: A320, B738, CL60
    std::vector<std::string> daypart;    // morning | afternoon | evening | night
    bool context = false;                // refuelling / deicing / delayed
    int number = 0;                      // [2] or a trailing digit; 0 when absent
};

// Where the aeroplane and the clock are, as far as choosing a file is concerned.
struct PlayContext {
    std::string aircraft;  // ICAO of the aircraft being flown, upper case
    std::string daypart;   // as daypartFor() returns
};

// 1.x's boundaries: morning 05-12, afternoon 12-17, evening 17-22, night
// otherwise. Deliberately NOT the same as Snapshot::isDark, which asks a
// different question - whether the cabin lights would be dimmed.
std::string daypartFor(int localHour);

// "AfterTakeoff[Night][2].ogg" -> "AfterTakeoff". Empty if the name is not one
// of the canonical events; unknown files are ignored, never guessed at.
std::string eventFromFilename(const std::string& filename);

// The same name, split into the event and what the tags say about it. Returns
// false when the file is not an announcement at all.
bool parseSoundFile(const std::string& filename, std::string* event, SoundVariant* variant);

// How well a file fits the moment. Returns false when it must NOT play here -
// the wrong aircraft or the wrong time of day are disqualifications, not
// penalties, because a night greeting at noon is worse than no greeting.
bool scoreVariant(const SoundVariant& variant, const PlayContext& context, int* score);

// Picks one of several files for the same event. Highest score wins; among
// equals `round` chooses, so a pack with three greetings does not play the same
// one every flight.
//
// 1.x drew that lot at random, on every enqueue. Here it is a counter, advanced
// once per flight, for two reasons: the core is contractually free of rand(),
// and the panel and the player have to name the SAME file - one of them asks
// while showing the library, the other while starting playback, and a die
// thrown between the two questions would make the panel a liar.
//
// Returns the index into `variants`, or -1 when none of them may play.
int chooseVariant(const std::vector<SoundVariant>& variants, const PlayContext& context,
                  unsigned round);

}  // namespace xa::core
