// What leaves the machine when the user presses "Отправить журнал".
//
// This is the one decision in the feature worth proving: the panel makes the
// user a promise about the contents, and a promise nobody checks is a promise
// that quietly stops being true. So the filtering, the scrubbing and the
// envelope live here - no files, no sockets, no XPLM - and the bench runs them.
// The plugin side only reads Log.txt and does the POST.
#pragma once

#include <cstddef>
#include <string>

namespace xa::core {

struct ReportMeta {
    std::string plugin;
    std::string xplane;
    std::string os;
    std::string aircraft;
    std::string pack;
    std::string settings;
};

// The service takes a megabyte; we stay far below it. A report is read by a
// person, and the useful part is always the tail.
inline constexpr std::size_t kReportMaxLogBytes = 256 * 1024;
inline constexpr std::size_t kReportMaxLines = 4000;

// Keeps only X-Announcer's own lines from a Log.txt, newest ones for certain,
// and trims the result to the caps above. Other plugins' lines are dropped -
// they are not ours to send.
std::string reportLogLines(const std::string& rawLog);

// Replaces the Windows/macOS user directory in a path with a placeholder. Log
// lines carry sound-file paths, and those are worth having; the user's name is
// not.
std::string scrubPaths(const std::string& text);

// The JSON body, ready to POST.
std::string buildReportBody(const ReportMeta& meta, const std::string& rawLog);

}  // namespace xa::core
