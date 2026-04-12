#include "stdafx.h"
#include "http_client.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
std::wstring HttpClient::widen(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring out(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), len);
    return out;
}

std::string HttpClient::narrow(const std::wstring& s) {
    if (s.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, out.data(), len, nullptr, nullptr);
    return out;
}

HttpClient::ParsedUrl HttpClient::parseUrl(const std::string& url) {
    URL_COMPONENTSW uc{};
    uc.dwStructSize = sizeof(uc);

    std::wstring wurl = widen(url);

    // Let WinHTTP do the parsing – we need the component lengths first
    uc.dwSchemeLength    = (DWORD)-1;
    uc.dwHostNameLength  = (DWORD)-1;
    uc.dwUrlPathLength   = (DWORD)-1;
    uc.dwExtraInfoLength = (DWORD)-1;

    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc))
        throw std::runtime_error("HttpClient: failed to parse URL: " + url);

    ParsedUrl result;
    result.scheme = std::wstring(uc.lpszScheme,    uc.dwSchemeLength);
    result.host   = std::wstring(uc.lpszHostName,  uc.dwHostNameLength);
    result.port   = uc.nPort;

    std::wstring path(uc.lpszUrlPath, uc.dwUrlPathLength);
    std::wstring extra(uc.lpszExtraInfo ? uc.lpszExtraInfo : L"",
                       uc.dwExtraInfoLength);
    result.path = path + extra;

    return result;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
HttpClient::HttpClient() {
    m_session = WinHttpOpen(
        L"foo_qobuz/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );
    if (!m_session)
        throw std::runtime_error("HttpClient: WinHttpOpen failed");
}

HttpClient::~HttpClient() {
    if (m_session)
        WinHttpCloseHandle(static_cast<HINTERNET>(m_session));
}

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------
HttpClient::Response HttpClient::get(const std::string& url, const Headers& hdrs) {
    return request(L"GET", url, {}, hdrs);
}

HttpClient::Response HttpClient::post(const std::string& url,
                                      const std::string& body,
                                      const Headers& hdrs) {
    return request(L"POST", url, body, hdrs);
}

// ---------------------------------------------------------------------------
// Core request implementation
// ---------------------------------------------------------------------------
HttpClient::Response HttpClient::request(const std::wstring& method,
                                         const std::string&  url,
                                         const std::string&  body,
                                         const Headers&      headers) {
    Response resp;

    ParsedUrl pu;
    try { pu = parseUrl(url); }
    catch (const std::exception& e) { resp.error = e.what(); return resp; }

    bool isHttps = (pu.scheme == L"https");

    HINTERNET hConnect = WinHttpConnect(
        static_cast<HINTERNET>(m_session),
        pu.host.c_str(),
        pu.port,
        0
    );
    if (!hConnect) {
        resp.error = "WinHttpConnect failed for host: " + narrow(pu.host);
        return resp;
    }

    DWORD openFlags = isHttps ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect,
        method.c_str(),
        pu.path.c_str(),
        nullptr,                    // HTTP/1.1
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        openFlags
    );
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        resp.error = "WinHttpOpenRequest failed";
        return resp;
    }

    // Build header string
    std::wstring extraHeaders;
    for (const auto& [k, v] : headers) {
        extraHeaders += widen(k) + L": " + widen(v) + L"\r\n";
    }
    // Default Content-Type for POST
    if (method == L"POST" && !body.empty()) {
        extraHeaders += L"Content-Type: application/x-www-form-urlencoded\r\n";
    }

    LPCWSTR lpszHeaders = extraHeaders.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS
                                               : extraHeaders.c_str();
    DWORD   headerLen   = extraHeaders.empty() ? 0
                                               : static_cast<DWORD>(extraHeaders.size());

    const void* bodyPtr = body.empty() ? WINHTTP_NO_REQUEST_DATA
                                       : static_cast<const void*>(body.c_str());
    DWORD bodyLen = static_cast<DWORD>(body.size());

    BOOL sent = WinHttpSendRequest(hRequest, lpszHeaders, headerLen,
                                   const_cast<void*>(bodyPtr), bodyLen,
                                   bodyLen, 0);
    if (!sent || !WinHttpReceiveResponse(hRequest, nullptr)) {
        resp.error = "WinHttpSendRequest/ReceiveResponse failed";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        return resp;
    }

    // Read HTTP status code
    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX,
                        &statusCode, &statusSize,
                        WINHTTP_NO_HEADER_INDEX);
    resp.status = static_cast<int>(statusCode);

    // Read body
    DWORD bytesAvail = 0;
    while (WinHttpQueryDataAvailable(hRequest, &bytesAvail) && bytesAvail > 0) {
        std::string chunk(bytesAvail, '\0');
        DWORD bytesRead = 0;
        WinHttpReadData(hRequest, chunk.data(), bytesAvail, &bytesRead);
        resp.body.append(chunk.data(), bytesRead);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    return resp;
}
