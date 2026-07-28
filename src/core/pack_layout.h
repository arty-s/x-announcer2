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

}  // namespace xa::core
