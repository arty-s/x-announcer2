#include "plugin/update_check.h"

#include <cstdio>
#include <mutex>
#include <string>
#include <thread>

#include "core/version.h"
#include "plugin/xa_log.h"

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#endif

namespace xa::update {
namespace {

// The same address the installed cfg names in its `module` line. Hardcoded for
// the reason report.cpp hardcodes its own: an address that can be configured is
// an address that can be pointed at nothing, and then the check answers "you
// are up to date" forever.
constexpr const wchar_t* kHost = L"xvatrus.ru";
constexpr const wchar_t* kPath = L"/xannouncer/update/skunkcrafts_updater.cfg";

std::mutex g_mutex;
Status g_status;
std::thread g_worker;
bool g_started = false;

void setStatus(State state, std::string latest, std::string message) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_status.state = state;
    g_status.latest = std::move(latest);
    g_status.message = std::move(message);
}

#ifdef _WIN32
// GETs the cfg. Returns the body, or sets `error` and returns "".
std::string get(std::string& error) {
    HINTERNET session = WinHttpOpen(L"X-Announcer/2", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr) {
        error = "не удалось открыть сетевую сессию";
        return {};
    }
    // Shorter than the report's timeouts on purpose: nobody is waiting for this
    // answer, and a check that hangs for a minute behind a captive portal would
    // hold a thread for a line of text.
    WinHttpSetTimeouts(session, 5000, 5000, 8000, 8000);

    HINTERNET connection = WinHttpConnect(session, kHost, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (connection == nullptr) {
        error = "не удалось соединиться с xvatrus.ru";
        WinHttpCloseHandle(session);
        return {};
    }
    HINTERNET request = WinHttpOpenRequest(connection, L"GET", kPath, nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (request == nullptr) {
        error = "не удалось построить запрос";
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return {};
    }

    std::string response;
    BOOL ok = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA,
                                 0, 0, 0);
    if (ok) {
        ok = WinHttpReceiveResponse(request, nullptr);
    }
    if (!ok) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "сеть не ответила (код %lu)",
                      static_cast<unsigned long>(GetLastError()));
        error = buf;
    } else {
        DWORD status = 0;
        DWORD size = sizeof(status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
        DWORD available = 0;
        // The cfg is six short lines. A captive portal will happily answer 200
        // with a megabyte of login page; this is where that stops.
        while (response.size() < 8 * 1024 && WinHttpQueryDataAvailable(request, &available) &&
               available > 0) {
            std::string chunk(available, '\0');
            DWORD read = 0;
            if (!WinHttpReadData(request, chunk.data(), available, &read)) {
                break;
            }
            chunk.resize(read);
            response += chunk;
        }
        if (status != 200) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "сервер ответил %lu",
                          static_cast<unsigned long>(status));
            error = buf;
            response.clear();
        }
    }
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return response;
}
#else
std::string get(std::string& error) {
    error = "проверка обновлений сейчас работает только под Windows";
    return {};
}
#endif

}  // namespace

bool start(const std::string& installedVersion) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_started) {
            return false;
        }
        g_started = true;
        g_status.state = State::Checking;
    }
    g_worker = std::thread([installedVersion]() {
        std::string error;
        const std::string body = get(error);
        const std::string latest = core::versionFromUpdaterCfg(body);
        if (!error.empty() || latest.empty()) {
            if (error.empty()) {
                error = "в ответе канала нет строки version";
            }
            xa::log("update: не спросил канал — %s", error.c_str());
            setStatus(State::Failed, "", error);
            return;
        }
        // Unknown is deliberately NOT Outdated. An answer nobody can read says
        // nothing about which build is newer, and a panel that guesses "behind"
        // would nag everyone behind a portal that answers 200 with HTML.
        const core::VersionOrder order = core::compareVersions(installedVersion, latest);
        if (order == core::VersionOrder::Unknown) {
            xa::log("update: канал ответил '%s' — не похоже на версию, молчу", latest.c_str());
            setStatus(State::Failed, latest, "ответ не похож на версию");
            return;
        }
        if (order == core::VersionOrder::Older) {
            // Into the log, so that a report from somebody running an old build
            // says so without anybody having to ask them.
            xa::log("update: установлена %s, в канале %s — УСТАРЕЛА",
                    installedVersion.c_str(), latest.c_str());
            setStatus(State::Outdated, latest, "");
            return;
        }
        xa::log("update: установлена %s, в канале %s — свежая", installedVersion.c_str(),
                latest.c_str());
        setStatus(State::Current, latest, "");
    });
    return true;
}

Status status() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_status;
}

void shutdown() {
    if (g_worker.joinable()) {
        g_worker.join();
    }
}

}  // namespace xa::update
