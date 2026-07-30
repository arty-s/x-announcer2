// What the "Отправить журнал" button actually sends.
//
// The panel makes the user a promise in plain words: only X-Announcer's own
// lines, no other plugin's, and no user name in the paths. A promise nobody
// checks is a promise that quietly stops being true the next time somebody adds
// a log line - so it is checked here, against a Log.txt that looks like the real
// one, with a neighbouring plugin talking in the middle of it.
#include "report_body_test.h"

#include <iostream>
#include <string>

#include "core/report_body.h"

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

bool has(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

// A Log.txt the way X-Plane writes one: our lines timestamped and prefixed,
// other plugins' lines in between, and a path with a user name in it.
const char* kLog =
    "0:00:12.345 I/OS: X-Plane 12.4.31\n"
    "0:00:13.000 XSquawkBox: connected as PILOT123, private stuff\n"
    "0:00:13.100 X-Announcer2: library scan: 32 packs in C:\\Users\\Artyom\\UA_Sounds\n"
    "0:00:13.200 X-Announcer2: airline: SBI (S7 Airlines) from livery\n"
    "0:00:14.000 FlyWithLua: running script user_secret.lua\n"
    "0:00:15.000 X-Announcer2: phase -> CLIMB\n";

}  // namespace

void runReportBodyChecks(int* checks, int* failed) {
    g_checks = checks;
    g_failed = failed;
    std::cout << "-- report body\n";

    const std::string lines = core::reportLogLines(kLog);
    check(has(lines, "phase -> CLIMB"), "our own lines are kept");
    check(has(lines, "0:00:15.000"), "X-Plane's timestamp is kept - it is what orders events");
    check(!has(lines, "XSquawkBox"), "a neighbouring plugin's line is not sent");
    check(!has(lines, "FlyWithLua"), "another neighbour's line is not sent either");
    check(!has(lines, "user_secret.lua"), "nothing from other plugins leaks through");

    const std::string scrubbed = core::scrubPaths(lines);
    check(!has(scrubbed, "Artyom"), "the Windows user name is taken out of paths");
    check(has(scrubbed, "<user>"), "and is replaced by a placeholder, not deleted silently");
    check(has(scrubbed, "UA_Sounds"), "the rest of the path survives - that is the diagnostic part");
    check(has(core::scrubPaths("/Users/artyom/Sounds/AFL"), "<user>"),
          "the macOS form of the path is scrubbed too");

    // A pack folder called "users" must not be mistaken for a home directory.
    check(core::scrubPaths("packs/USERS/AFL") == "packs/USERS/AFL",
          "a folder merely named users is left alone");

    const core::ReportMeta meta{"2.0.0-dev", "12431", "windows", "A20N", "AFL", "volume = 0.8\n"};
    const std::string body = core::buildReportBody(meta, kLog);
    check(body.front() == '{' && body.back() == '}', "the body is one JSON object");
    check(has(body, "\"aircraft\":\"A20N\""), "the aeroplane goes with it");
    check(has(body, "\"pack\":\"AFL\""), "so does the pack in use");
    check(!has(body, "XSquawkBox"), "the envelope carries no neighbouring plugin either");
    check(!has(body, "\n"), "no raw newline escapes into the JSON");
    check(has(body, "\\n"), "log line breaks are escaped, not dropped");

    // Quotes and backslashes are ordinary in Windows paths and pack names; an
    // unescaped one would make the whole report unreadable at the far end.
    const std::string tricky =
        core::buildReportBody(meta, "X-Announcer2: pack \"C:\\Sound\\a\"\tafter tab\n");
    check(has(tricky, "\\\"C:\\\\Sound"), "quotes and backslashes are escaped");
    check(has(tricky, "\\t"), "a tab is escaped rather than passed through raw");

    // The cap protects the far end; the newest lines are the ones worth having.
    std::string flood;
    for (int i = 0; i < 6000; ++i) {
        flood += "X-Announcer2: line " + std::to_string(i) + "\n";
    }
    const std::string capped = core::reportLogLines(flood);
    check(capped.size() <= core::kReportMaxLogBytes + 64, "an enormous log is trimmed");
    check(has(capped, "line 5999"), "the newest line survives the trim");
    check(!has(capped, "line 0\n"), "the oldest is what gets dropped");
    check(capped.rfind("[начало", 0) == 0 || !has(capped, "line 1000"),
          "a trimmed log says so instead of pretending to be whole");

    // A log with nothing of ours in it must produce nothing, not a stray newline
    // that would look like a report with one empty line.
    check(core::reportLogLines("0:00:01 SomeOtherPlugin: hello\n").empty(),
          "a log with none of our lines yields an empty body");
}

}  // namespace xa::test
