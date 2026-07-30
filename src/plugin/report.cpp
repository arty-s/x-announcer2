#include "plugin/report.h"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#include "plugin/xa_log.h"

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#endif

namespace xa::report {
namespace {

// The destination. One constant and no setting: a configurable address is one
// more way for a report to end up somewhere nobody reads, and the panel promises
// the user a specific place.
constexpr const wchar_t* kHost = L"xvatrus.ru";
constexpr const wchar_t* kPath = L"/xannouncer/report";

std::mutex g_mutex;
Status g_status;
std::thread g_worker;
bool g_busy = false;

void setStatus(State state, std::string ticket, std::string message) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_status.state = state;
    g_status.ticket = std::move(ticket);
    g_status.message = std::move(message);
}

std::string readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

#ifdef _WIN32
// POSTs the body. Returns the response text, or sets `error` and returns "".
std::string post(const std::string& body, std::string& error) {
    HINTERNET session = WinHttpOpen(L"X-Announcer/2", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr) {
        error = "не удалось открыть сетевую сессию";
        return {};
    }
    // Every stage is bounded: a simulator frozen behind a half-open socket would
    // be a worse bug than the one being reported.
    WinHttpSetTimeouts(session, 10000, 10000, 20000, 20000);

    HINTERNET connection = WinHttpConnect(session, kHost, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (connection == nullptr) {
        error = "не удалось соединиться с xvatrus.ru";
        WinHttpCloseHandle(session);
        return {};
    }
    HINTERNET request = WinHttpOpenRequest(connection, L"POST", kPath, nullptr,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           WINHTTP_FLAG_SECURE);
    if (request == nullptr) {
        error = "не удалось построить запрос";
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return {};
    }

    std::string response;
    const wchar_t* headers = L"Content-Type: application/json; charset=utf-8\r\n";
    BOOL ok = WinHttpSendRequest(request, headers, static_cast<DWORD>(-1),
                                 const_cast<char*>(body.data()),
                                 static_cast<DWORD>(body.size()),
                                 static_cast<DWORD>(body.size()), 0);
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
        while (response.size() < 64 * 1024 &&  // the answer is a ticket, not a document
               WinHttpQueryDataAvailable(request, &available) && available > 0) {
            std::string chunk(available, '\0');
            DWORD read = 0;
            if (!WinHttpReadData(request, chunk.data(), available, &read)) {
                break;
            }
            chunk.resize(read);
            response += chunk;
        }
        if (status != 200) {
            char buf[160];
            if (status == 429) {
                std::snprintf(buf, sizeof(buf),
                              "сервер просит подождать: сегодня уже принято несколько журналов");
            } else {
                std::snprintf(buf, sizeof(buf), "сервер ответил %lu",
                              static_cast<unsigned long>(status));
            }
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
std::string post(const std::string&, std::string& error) {
    error = "отправка журнала сейчас работает только под Windows";
    return {};
}
#endif

// Pulls the ticket out of {"id":"XA-..."} without dragging in a JSON parser for
// one field. Anything unexpected yields an empty string, and the caller says so
// rather than showing the user a "ticket" nobody can look up.
std::string ticketFrom(const std::string& response) {
    const std::string key = "\"id\"";
    std::size_t at = response.find(key);
    if (at == std::string::npos) {
        return {};
    }
    at = response.find('"', at + key.size());  // opening quote of the value
    if (at == std::string::npos) {
        return {};
    }
    const std::size_t end = response.find('"', at + 1);
    if (end == std::string::npos || end - at - 1 > 64) {
        return {};
    }
    const std::string id = response.substr(at + 1, end - at - 1);
    for (const char c : id) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-') {
            return {};
        }
    }
    return id;
}

}  // namespace

bool send(const Input& input) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_busy) {
            return false;
        }
        g_busy = true;
        g_status.state = State::Sending;
        g_status.ticket.clear();
        g_status.message = "собираю журнал…";
    }
    // A previous worker has finished but was never joined; joining here keeps
    // exactly one thread alive at a time.
    if (g_worker.joinable()) {
        g_worker.join();
    }
    g_worker = std::thread([input]() {
        const std::string raw = readFile(input.logPath);
        if (raw.empty()) {
            xa::log("report: не смог прочитать %s", input.logPath.c_str());
        }
        const std::string body = core::buildReportBody(input.meta, raw);
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            char buf[96];
            std::snprintf(buf, sizeof(buf), "отправляю %zu КБ…", body.size() / 1024 + 1);
            g_status.message = buf;
        }
        // The copy on disk is written before the send and regardless of it: when
        // the network is the broken thing, the user still has a file to hand
        // over by other means - and can read for themselves what was sent.
        if (!input.copyPath.empty()) {
            std::ofstream copy(input.copyPath, std::ios::binary | std::ios::trunc);
            if (copy) {
                copy << body;
            }
        }
        std::string error;
        const std::string response = post(body, error);
        const std::string ticket = ticketFrom(response);
        if (!error.empty() || ticket.empty()) {
            if (error.empty()) {
                error = "сервер ответил непонятным";
            }
            xa::log("report: не отправлено — %s", error.c_str());
            setStatus(State::Failed, "", error);
        } else {
            xa::log("report: отправлено, номер %s (%zu байт)", ticket.c_str(), body.size());
            setStatus(State::Sent, ticket, "");
        }
        std::lock_guard<std::mutex> lock(g_mutex);
        g_busy = false;
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

}  // namespace xa::report
