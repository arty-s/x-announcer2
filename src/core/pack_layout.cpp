#include "core/pack_layout.h"

#include <algorithm>
#include <cctype>

namespace xa::core {
namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool isAlpha(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; }

}  // namespace

bool isLocaleFolder(const std::string& name) {
    if (name.size() == 2) {
        return isAlpha(name[0]) && isAlpha(name[1]);
    }
    if (name.size() == 5) {
        return isAlpha(name[0]) && isAlpha(name[1]) && (name[2] == '-' || name[2] == '_') &&
               isAlpha(name[3]) && isAlpha(name[4]);
    }
    return false;
}

std::string chooseLocaleFolder(const std::vector<std::string>& folders,
                               const std::string& wanted) {
    std::vector<std::string> locales;
    for (const std::string& name : folders) {
        if (isLocaleFolder(name)) {
            locales.push_back(name);
        }
    }
    if (locales.empty()) {
        return std::string();
    }

    const std::string want = lower(wanted);
    for (const std::string& name : locales) {
        if (lower(name) == want) {
            return name;
        }
    }
    // Several to choose from and none of them the one asked for: read none. A
    // pack that ships English and German must not start speaking German because
    // the filesystem happened to list it first.
    return locales.size() == 1 ? locales.front() : std::string();
}

}  // namespace xa::core
