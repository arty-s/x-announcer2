#include "plugin/xa_log.h"

#include <cstdarg>
#include <cstdio>
#include <deque>
#include <mutex>

#include "XPLMUtilities.h"

namespace xa {
namespace {

constexpr std::size_t kRingCapacity = 500;

std::mutex g_mutex;
std::deque<std::string> g_ring;

}  // namespace

void log(const char* fmt, ...) {
    char body[1024];
    va_list args;
    va_start(args, fmt);
    const int written = std::vsnprintf(body, sizeof(body), fmt, args);
    va_end(args);
    if (written < 0) {
        return;
    }

    char line[1100];
    std::snprintf(line, sizeof(line), "X-Announcer2: %s\n", body);
    XPLMDebugString(line);

    std::lock_guard<std::mutex> lock(g_mutex);
    g_ring.emplace_back(body);
    while (g_ring.size() > kRingCapacity) {
        g_ring.pop_front();
    }
}

std::vector<std::string> logTail(std::size_t maxLines) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const std::size_t count = g_ring.size() < maxLines ? g_ring.size() : maxLines;
    return std::vector<std::string>(g_ring.end() - static_cast<std::ptrdiff_t>(count), g_ring.end());
}

}  // namespace xa
