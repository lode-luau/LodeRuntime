// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "HttpDownloader.hpp"

#include "PathUtil.hpp"

#include <fstream>
#include <system_error>

#if defined(_WIN32)
#include <Windows.h>
#include <winhttp.h>
#endif

namespace Lode::Package
{

namespace
{

namespace fs = std::filesystem;

void AddError(DownloadResult& result, std::string message)
{
    result.errors.push_back(std::move(message));
}

} // namespace

DownloadResult DownloadHttpsFile(const std::string& url, const fs::path& destination)
{
    DownloadResult result;

#if defined(_WIN32)
    const std::wstring wideUrl = Lode::Detail::PathFromUtf8(url).wstring();
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &components) ||
        components.nScheme != INTERNET_SCHEME_HTTPS)
    {
        AddError(result, "Release asset URL must be HTTPS: " + url);
        return result;
    }

    const std::wstring host(components.lpszHostName, components.dwHostNameLength);
    std::wstring path(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.dwExtraInfoLength != 0)
        path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    if (path.empty())
        path = L"/";

    HINTERNET session = WinHttpOpen(L"LodeRuntime/1.0",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session)
    {
        AddError(result, "WinHTTP session initialization failed with error " +
            std::to_string(GetLastError()) + ".");
        return result;
    }
    WinHttpSetTimeouts(session, 15000, 15000, 30000, 30000);

    HINTERNET connection = WinHttpConnect(session, host.c_str(), components.nPort, 0);
    if (!connection)
    {
        AddError(result, "Cannot connect to release host: " +
            std::to_string(GetLastError()) + ".");
        WinHttpCloseHandle(session);
        return result;
    }

    HINTERNET request = WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!request)
    {
        AddError(result, "Cannot create release asset request: " +
            std::to_string(GetLastError()) + ".");
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return result;
    }

    WinHttpSetTimeouts(request, 15000, 15000, 30000, 30000);
    const wchar_t headers[] = L"User-Agent: LodeRuntime/1.0\r\n";
    if (!WinHttpAddRequestHeaders(request, headers, static_cast<DWORD>(-1L),
                                  WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE) ||
        !WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, nullptr))
    {
        AddError(result, "Release asset request failed: " +
            std::to_string(GetLastError()) + ".");
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return result;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    if (!WinHttpQueryHeaders(request,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize,
            WINHTTP_NO_HEADER_INDEX) || statusCode != 200)
    {
        AddError(result, "Release asset request returned HTTP status " +
            std::to_string(statusCode) + ".");
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return result;
    }

    std::error_code ec;
    fs::create_directories(destination.parent_path(), ec);
    if (ec)
    {
        AddError(result, "Cannot create download directory: " + ec.message());
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return result;
    }

    std::ofstream output(destination, std::ios::binary | std::ios::trunc);
    if (!output.is_open())
    {
        AddError(result, "Cannot open download destination: " +
            Lode::Detail::PathToUtf8(destination));
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return result;
    }

    bool success = true;
    for (;;)
    {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available))
        {
            AddError(result, "Cannot read release asset response: " +
                std::to_string(GetLastError()) + ".");
            success = false;
            break;
        }
        if (available == 0)
            break;

        std::vector<char> buffer(available);
        DWORD received = 0;
        if (!WinHttpReadData(request, buffer.data(), available, &received) || received == 0)
        {
            AddError(result, "Cannot read release asset response: " +
                std::to_string(GetLastError()) + ".");
            success = false;
            break;
        }
        output.write(buffer.data(), static_cast<std::streamsize>(received));
        if (!output)
        {
            AddError(result, "Cannot write downloaded release asset.");
            success = false;
            break;
        }
    }
    output.close();

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);

    if (!success)
    {
        fs::remove(destination, ec);
        return result;
    }
    result.destination = destination;
#else
    (void)url;
    (void)destination;
    AddError(result, "GitHub Release artifact downloads are currently supported only on Windows.");
#endif
    return result;
}

} // namespace Lode::Package
