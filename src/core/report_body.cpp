#include "core/report_body.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>
#include <vector>

namespace xa::core {
namespace {

constexpr const char* kPrefix = "X-Announcer2:";

std::string jsonEscape(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 16);
    for (const unsigned char c : in) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                // Control bytes would make the JSON invalid; everything else,
                // UTF-8 included, goes through untouched.
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

}  // namespace

std::string reportLogLines(const std::string& rawLog) {
    std::vector<std::string> kept;
    std::istringstream stream(rawLog);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.find(kPrefix) == std::string::npos) {
            continue;
        }
        // X-Plane's own timestamp at the head of the line is kept: it is how two
        // events get ordered against each other afterwards.
        kept.push_back(line);
        if (kept.size() > kReportMaxLines) {
            kept.erase(kept.begin());
        }
    }
    std::string out;
    for (const std::string& one : kept) {
        out += one;
        out += '\n';
    }
    if (out.size() > kReportMaxLogBytes) {
        out.erase(0, out.size() - kReportMaxLogBytes);
        const std::size_t newline = out.find('\n');
        if (newline != std::string::npos) {
            out.erase(0, newline + 1);  // never start mid-line
        }
        out.insert(0, "[начало журнала обрезано]\n");
    }
    return out;
}

// Whether the "users" folder found at `at` is really a home-directory root
// rather than a folder that merely shares the name. A sound pack in
// packs/USERS/AFL is not somebody's home, and scrubbing it would delete the pack
// name - which is exactly the sort of thing a report is read for.
static bool looksLikeHomeRoot(const std::string& text, std::size_t at, bool windowsForm) {
    if (windowsForm) {
        // "C:\Users\..." - a drive letter and a colon immediately before.
        return at >= 2 && text[at - 1] == ':' &&
               std::isalpha(static_cast<unsigned char>(text[at - 2])) != 0;
    }
    // "/Users/..." - the very start of the path, not a folder deeper in.
    if (at == 0) {
        return true;
    }
    const char before = text[at - 1];
    return before == ' ' || before == '"' || before == '\'' || before == '=' || before == '\t';
}

std::string scrubPaths(const std::string& text) {
    // Case-insensitive hunt for "\Users\<name>" and "/Users/<name>". The name is
    // the only part of a path that says who the person is; the rest of the path
    // is what makes a report worth reading.
    static const char* kMarkers[] = {"\\users\\", "/users/"};
    static const std::string kPlaceholder = "<user>";
    std::string out = text;
    std::string lower = out;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const char* marker : kMarkers) {
        const std::string needle(marker);
        const bool windowsForm = needle[0] == '\\';
        std::size_t at = 0;
        while ((at = lower.find(needle, at)) != std::string::npos) {
            if (!looksLikeHomeRoot(out, at, windowsForm)) {
                at += needle.size();
                continue;
            }
            const std::size_t nameStart = at + needle.size();
            std::size_t nameEnd = nameStart;
            while (nameEnd < out.size() && out[nameEnd] != '\\' && out[nameEnd] != '/' &&
                   out[nameEnd] != '"' && out[nameEnd] != ' ' && out[nameEnd] != '\n') {
                ++nameEnd;
            }
            if (nameEnd == nameStart) {
                at = nameStart;
                continue;
            }
            out.replace(nameStart, nameEnd - nameStart, kPlaceholder);
            lower.replace(nameStart, nameEnd - nameStart, kPlaceholder);
            at = nameStart + kPlaceholder.size();
        }
    }
    return out;
}

std::string buildReportBody(const ReportMeta& meta, const std::string& rawLog) {
    const std::string lines = scrubPaths(reportLogLines(rawLog));
    std::string json = "{";
    json += "\"plugin\":\"" + jsonEscape(meta.plugin) + "\",";
    json += "\"xplane\":\"" + jsonEscape(meta.xplane) + "\",";
    json += "\"os\":\"" + jsonEscape(meta.os) + "\",";
    json += "\"aircraft\":\"" + jsonEscape(meta.aircraft) + "\",";
    json += "\"pack\":\"" + jsonEscape(meta.pack) + "\",";
    json += "\"settings\":\"" + jsonEscape(scrubPaths(meta.settings)) + "\",";
    json += "\"log\":\"" + jsonEscape(lines) + "\"";
    json += "}";
    return json;
}

}  // namespace xa::core
