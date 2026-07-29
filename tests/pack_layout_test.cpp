// Which language folder inside a pack gets read.
//
// This was the first thing the settings work turned up that was not a setting:
// v2 walked a pack recursively and merged every language it found, so a pack
// shipping en-us and de-de announced in whichever the filesystem listed first.
// 1.x had always read exactly one folder, and the bench never saw the
// difference because the bench does not touch the disk.
#include "pack_layout_test.h"

#include <iostream>
#include <string>
#include <vector>

#include "core/pack_layout.h"

namespace xa::test {
namespace {

int* g_checks = nullptr;
int* g_failed = nullptr;

void check(bool condition, const std::string& what) {
    ++*g_checks;
    if (condition) {
        std::cout << "   PASS " << what << "\n";
    } else {
        ++*g_failed;
        std::cout << "   FAIL " << what << "\n";
    }
}

}  // namespace

void runPackLayoutChecks(int* checks, int* failed) {
    g_checks = checks;
    g_failed = failed;
    std::cout << "-- pack layout\n";

    check(core::isLocaleFolder("ru") && core::isLocaleFolder("en-us") &&
              core::isLocaleFolder("de_DE") && core::isLocaleFolder("PT-BR"),
          "language folders are recognised in every spelling packs use");
    check(!core::isLocaleFolder("Sounds") && !core::isLocaleFolder("") &&
              !core::isLocaleFolder("e1") && !core::isLocaleFolder("en-usa") &&
              !core::isLocaleFolder("extra"),
          "an ordinary folder inside a pack is not mistaken for a language");

    check(core::chooseLocaleFolder({}, "en-us").empty(), "a pack with no sub-folders reads none");
    check(core::chooseLocaleFolder({"Sounds", "Extra"}, "en-us").empty(),
          "sub-folders that are not languages are ignored");
    check(core::chooseLocaleFolder({"en-us"}, "ru") == "en-us",
          "the only language folder is read even when another was asked for");
    check(core::chooseLocaleFolder({"en-us", "de-de"}, "de-de") == "de-de",
          "the language asked for wins when the pack has several");
    check(core::chooseLocaleFolder({"en-us", "de-de"}, "ru").empty(),
          "several languages and none of them ours: read none, do not guess");
    check(core::chooseLocaleFolder({"EN-US"}, "en-us") == "EN-US",
          "the match ignores case but the folder keeps its own");
    check(core::chooseLocaleFolder({"de-de", "en-us"}, "en-us") ==
              core::chooseLocaleFolder({"en-us", "de-de"}, "en-us"),
          "the answer does not depend on the order the disk listed them");

    runVariantChecks();
}

namespace {

// Shorthand for the tests: parse a real pack file name into its tags.
core::SoundVariant variantOf(const std::string& filename) {
    core::SoundVariant v;
    std::string event;
    core::parseSoundFile(filename, &event, &v);
    return v;
}

std::string eventOf(const std::string& filename) { return core::eventFromFilename(filename); }

bool plays(const std::string& filename, const std::string& aircraft, const std::string& daypart) {
    core::PlayContext ctx;
    ctx.aircraft = aircraft;
    ctx.daypart = daypart;
    return core::scoreVariant(variantOf(filename), ctx, nullptr);
}

int scoreOf(const std::string& filename, const std::string& aircraft,
            const std::string& daypart) {
    core::PlayContext ctx;
    ctx.aircraft = aircraft;
    ctx.daypart = daypart;
    int score = -1;
    core::scoreVariant(variantOf(filename), ctx, &score);
    return score;
}

std::string chosen(const std::vector<std::string>& files, const std::string& aircraft,
                   const std::string& daypart, unsigned round = 0) {
    core::PlayContext ctx;
    ctx.aircraft = aircraft;
    ctx.daypart = daypart;
    std::vector<core::SoundVariant> variants;
    for (const std::string& name : files) {
        variants.push_back(variantOf(name));
    }
    const int index = core::chooseVariant(variants, ctx, round);
    return index < 0 ? std::string() : files[static_cast<std::size_t>(index)];
}

}  // namespace

void runVariantChecks() {
    std::cout << "-- file variants\n";

    // Names as they actually appear in the packs on disk.
    check(eventOf("BoardingWelcome.ogg") == "BoardingWelcome" &&
              eventOf("AfterTakeoff[Night].mp3") == "AfterTakeoff" &&
              eventOf("SafetyBriefing[A320][2].wav") == "SafetyBriefing",
          "the event survives however many tags follow it");
    check(eventOf("AfterTakeoff.ogg[A359][2].ogg") == "AfterTakeoff",
          "and survives the packs that write the extension twice");
    check(eventOf("SafetyBriefing1.ogg") == "SafetyBriefing" &&
              variantOf("SafetyBriefing1.ogg").number == 1,
          "a trailing digit is a variant number, not part of the name");
    check(eventOf("readme.txt").empty() && eventOf("Untitled.ogg").empty(),
          "a file that is not an announcement is not guessed at");
    check(eventOf("welcomeaboard.OGG") == "BoardingWelcome",
          "the misspellings community packs ship are still understood");

    check(variantOf("AfterTakeoff[Night].ogg").daypart == std::vector<std::string>{"night"},
          "a time tag is read as a time");
    check(variantOf("SafetyBriefing[a320].ogg").aircraft == std::vector<std::string>{"A320"},
          "an aircraft tag is upper-cased, whatever the file says");
    check(variantOf("BoardingWelcome[Deicing].ogg").context,
          "de-icing is recognised as a context, not as an aeroplane");

    // The heart of it: a wrong tag disqualifies, it does not merely lose.
    check(!plays("AfterTakeoff[Night].ogg", "A320", "morning"),
          "a night file never plays in the morning");
    check(!plays("SafetyBriefing[B738].ogg", "A320", "night"),
          "another aeroplane's file never plays");
    check(plays("AfterTakeoff.ogg", "A320", "night") && plays("AfterTakeoff.ogg", "", ""),
          "an untagged file plays in any aeroplane at any hour");
    check(plays("SafetyBriefing[A320].ogg", "A320N", "day") &&
              plays("SafetyBriefing[A320N].ogg", "A320", "day"),
          "an aircraft tag matches by prefix in either direction");
    check(!plays("SafetyBriefing[A32].ogg", "B738", "day") &&
              !plays("SafetyBriefing[A].ogg", "A320", "day"),
          "but a one-letter tag matches nothing, or it would match everything");

    check(scoreOf("SafetyBriefing[A320][Night].ogg", "A320", "night") >
              scoreOf("SafetyBriefing[Night].ogg", "A320", "night"),
          "the more specific of two allowed files wins");
    check(scoreOf("BoardingWelcome[Delayed].ogg", "A320", "day") <
              scoreOf("BoardingWelcome.ogg", "A320", "day"),
          "a context we cannot detect is a last resort, not a disqualification");

    check(chosen({"AfterTakeoff.ogg", "AfterTakeoff[Night].ogg"}, "A320", "night") ==
              "AfterTakeoff[Night].ogg",
          "the night file is the one that plays at night");
    check(chosen({"AfterTakeoff.ogg", "AfterTakeoff[Night].ogg"}, "A320", "morning") ==
              "AfterTakeoff.ogg",
          "and the plain one in the morning, rather than nothing at all");
    check(chosen({"SafetyBriefing[Night].ogg"}, "A320", "morning").empty(),
          "a pack whose only file is wrong for the moment plays nothing");

    // The rotation. Same round, same answer - that is what lets the panel name
    // the file the player will open.
    const std::vector<std::string> three = {"BoardingWelcome.ogg", "BoardingWelcome[2].ogg",
                                            "BoardingWelcome[3].ogg"};
    check(chosen(three, "A320", "day", 0) == chosen(three, "A320", "day", 0),
          "asking twice in the same round gives the same file");
    check(chosen(three, "A320", "day", 0) != chosen(three, "A320", "day", 1) &&
              chosen(three, "A320", "day", 3) == chosen(three, "A320", "day", 0),
          "and the next flight takes the next greeting, round and round");
    check(chosen({"AfterTakeoff.ogg", "AfterTakeoff[Night].ogg"}, "A320", "night", 7) ==
              "AfterTakeoff[Night].ogg",
          "the rotation never overrules the fit - it only breaks ties");

    check(core::daypartFor(5) == "morning" && core::daypartFor(11) == "morning" &&
              core::daypartFor(12) == "afternoon" && core::daypartFor(16) == "afternoon" &&
              core::daypartFor(17) == "evening" && core::daypartFor(21) == "evening" &&
              core::daypartFor(22) == "night" && core::daypartFor(4) == "night",
          "the hours divide into times of day exactly where 1.x divides them");
}

}  // namespace xa::test
