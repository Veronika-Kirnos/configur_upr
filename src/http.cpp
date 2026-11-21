#include "app.h"
#include <windows.h>
#include <winhttp.h>
#include <stdexcept>

std::wstring wide(const std::string& text) {
    if (text.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), text.size(), nullptr, 0);
    if (!size) throw std::runtime_error("Invalid UTF-8 text");
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), text.size(), result.data(), size);
    return result;
}

std::string utf8(const std::wstring& text) {
    int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), text.size(), nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), text.size(), result.data(), size, nullptr, nullptr);
    return result;
}

struct InternetHandle {
    HINTERNET value;
    explicit InternetHandle(HINTERNET handle) : value(handle) {
        if (!value) throw std::runtime_error("HTTP initialization failed: " + std::to_string(GetLastError()));
    }
    ~InternetHandle() { WinHttpCloseHandle(value); }
    InternetHandle(const InternetHandle&) = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;
};

std::string http_get(const std::string& url) {
    std::wstring address = wide(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(address.c_str(), 0, 0, &parts)) throw std::runtime_error("Invalid URL: " + url);
    std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    InternetHandle session(WinHttpOpen(L"Student-Maven-Graph/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    WinHttpSetTimeouts(session.value, 15000, 15000, 15000, 30000);
    InternetHandle connection(WinHttpConnect(session.value, host.c_str(), parts.nPort, 0));
    DWORD flags = parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    InternetHandle request(WinHttpOpenRequest(connection.value, L"GET", path.c_str(), nullptr,
                                            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0)
        || !WinHttpReceiveResponse(request.value, nullptr))
        throw std::runtime_error("HTTP request failed: " + url + " (" + std::to_string(GetLastError()) + ")");
    DWORD status = 0, size = sizeof(status);
    WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
    if (status != 200) throw std::runtime_error("HTTP " + std::to_string(status) + ": " + url);
    std::string result;
    char buffer[8192];
    DWORD received = 0;
    do {
        if (!WinHttpReadData(request.value, buffer, sizeof(buffer), &received))
            throw std::runtime_error("HTTP read failed: " + url);
        result.append(buffer, received);
    } while (received);
    return result;
}
