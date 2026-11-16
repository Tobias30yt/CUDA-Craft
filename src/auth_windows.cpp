#include "cudacraft/auth.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#include <algorithm>
#include <array>
#include <chrono>

namespace cudacraft::auth {
namespace {
struct Handle {
    HINTERNET value;
    explicit Handle(HINTERNET handle) : value(handle) { if (!value) throw protocol::Error("Authentication service unavailable"); }
    ~Handle() { WinHttpCloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
void check(BOOL ok) { if (!ok) throw protocol::Error("Authentication service unavailable"); }
}
Profile verify_mojang(const std::string& name, const std::string& hash) {
    if (!valid_username(name) || hash.empty() || hash.size() > 41) throw protocol::Error("Invalid authentication query");
    for (std::size_t i = 0; i < hash.size(); ++i) {
        const char c = hash[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (i == 0 && c == '-'))) throw protocol::Error("Invalid server hash");
    }
    const std::string path = "/session/minecraft/hasJoined?username=" + name + "&serverId=" + hash;
    const std::wstring wide_path(path.begin(), path.end());
    Handle session(WinHttpOpen(L"CUDA-Craft/0.2", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    check(WinHttpSetTimeouts(session.value, 2000, 2000, 2000, 2000));
    Handle connection(WinHttpConnect(session.value, L"sessionserver.mojang.com", INTERNET_DEFAULT_HTTPS_PORT, 0));
    Handle request(WinHttpOpenRequest(connection.value, L"GET", wide_path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    DWORD disabled = WINHTTP_DISABLE_REDIRECTS;
    check(WinHttpSetOption(request.value, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)));
    // Default certificate and hostname checks remain enabled. No configurable auth host.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    check(WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0));
    check(WinHttpReceiveResponse(request.value, nullptr));
    DWORD status = 0, size = sizeof(status);
    check(WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX));
    if (status == 204) throw protocol::Error("Minecraft session was not joined (HTTP 204)");
    if (status != 200) throw protocol::Error("Authentication service returned HTTP " + std::to_string(status));
    std::string body;
    std::array<char, 4096> buffer{};
    for (;;) {
        if (std::chrono::steady_clock::now() >= deadline) throw protocol::Error("Authentication request timed out");
        DWORD read = 0;
        check(WinHttpReadData(request.value, buffer.data(), static_cast<DWORD>(buffer.size()), &read));
        if (!read) break;
        if (body.size() + read > 65536) throw protocol::Error("Authentication response too large");
        body.append(buffer.data(), read);
    }
    return parse_profile(body, name);
}
}
