#pragma once
#include <string>
#include <vector>
#include <utility>

// ---------------------------------------------------------------------------
// Thin WinHTTP wrapper used by the Qobuz API client.
// All methods are synchronous and run on the calling thread.
// ---------------------------------------------------------------------------
class HttpClient {
public:
    HttpClient();
    ~HttpClient();

    // Prevent copies – HINTERNET handles are owned
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    struct Response {
        int         status = 0;   // HTTP status code, 0 on network error
        std::string body;
        std::string error;        // Non-empty on failure

        bool ok() const { return status >= 200 && status < 300; }
    };

    using Headers = std::vector<std::pair<std::string, std::string>>;

    Response get (const std::string& url, const Headers& headers = {});
    Response post(const std::string& url,
                  const std::string& body,
                  const Headers& headers = {});

private:
    void* m_session; // HINTERNET – opaque to callers

    Response request(const std::wstring& method,
                     const std::string&  url,
                     const std::string&  body,
                     const Headers&      headers);

    static std::wstring widen (const std::string& s);
    static std::string  narrow(const std::wstring& s);

    // URL helpers
    struct ParsedUrl {
        std::wstring scheme;    // "https"
        std::wstring host;
        INTERNET_PORT port;
        std::wstring path;      // includes query string
    };
    static ParsedUrl parseUrl(const std::string& url);
};
