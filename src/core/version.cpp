#include "core/version.h"

#include <cctype>
#include <cstddef>
#include <sstream>

namespace xa::core {

bool parseVersion(const std::string& text, std::vector<int>* out) {
    std::size_t at = 0;
    while (at < text.size() && (text[at] == ' ' || text[at] == '\t')) {
        ++at;
    }
    if (at < text.size() && (text[at] == 'v' || text[at] == 'V')) {
        ++at;
    }
    std::vector<int> parts;
    while (at < text.size() && std::isdigit(static_cast<unsigned char>(text[at])) != 0) {
        int value = 0;
        while (at < text.size() && std::isdigit(static_cast<unsigned char>(text[at])) != 0) {
            // A version longer than any real one is somebody else's file, not a
            // number to overflow on.
            if (value < 1000000) {
                value = value * 10 + (text[at] - '0');
            }
            ++at;
        }
        parts.push_back(value);
        if (at < text.size() && text[at] == '.') {
            ++at;
            continue;
        }
        break;
    }
    if (parts.empty()) {
        return false;
    }
    if (out != nullptr) {
        *out = parts;
    }
    return true;
}

VersionOrder compareVersions(const std::string& mine, const std::string& theirs) {
    std::vector<int> a;
    std::vector<int> b;
    if (!parseVersion(mine, &a) || !parseVersion(theirs, &b)) {
        return VersionOrder::Unknown;
    }
    const std::size_t count = a.size() > b.size() ? a.size() : b.size();
    for (std::size_t i = 0; i < count; ++i) {
        const int one = i < a.size() ? a[i] : 0;
        const int two = i < b.size() ? b[i] : 0;
        if (one < two) {
            return VersionOrder::Older;
        }
        if (one > two) {
            return VersionOrder::Newer;
        }
    }
    return VersionOrder::Same;
}

std::string versionFromUpdaterCfg(const std::string& cfg) {
    std::istringstream lines(cfg);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::string key = "version|";
        if (line.rfind(key, 0) != 0) {
            continue;
        }
        std::string value = line.substr(key.size());
        while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
            value.pop_back();
        }
        return value;
    }
    return {};
}

}  // namespace xa::core
