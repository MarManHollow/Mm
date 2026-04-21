#include "stdafx.h"
#include "qobuz_api.h"
#include "http_client.h"

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
QobuzAPI::QobuzAPI(const std::string& app_id, const std::string& app_secret)
    : m_app_id(app_id), m_app_secret(app_secret) {}

// ---------------------------------------------------------------------------
// MD5 via Windows BCrypt
// ---------------------------------------------------------------------------
std::string QobuzAPI::md5hex(const std::string& input) {
    BCRYPT_ALG_HANDLE hAlg  = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    ULONG hashLen = 16, hashObjLen = 0, dummy = 0;
    std::string hexOut;

    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_MD5_ALGORITHM, nullptr, 0) != 0)
        return {};

    BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, (PUCHAR)&hashLen, sizeof(hashLen), &dummy, 0);

    ULONG objSize = 0;
    BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&objSize, sizeof(objSize), &dummy, 0);

    std::vector<BYTE> hashObj(objSize);
    if (BCryptCreateHash(hAlg, &hHash, hashObj.data(), objSize, nullptr, 0, 0) != 0) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return {};
    }

    BCryptHashData(hHash,
                   reinterpret_cast<PUCHAR>(const_cast<char*>(input.data())),
                   static_cast<ULONG>(input.size()), 0);

    std::vector<BYTE> digest(hashLen);
    BCryptFinishHash(hHash, digest.data(), hashLen, 0);

    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (BYTE b : digest) oss << std::setw(2) << (int)b;
    return oss.str();
}

// ---------------------------------------------------------------------------
// Request signature for /track/getFileUrl
//
// Qobuz signing algorithm:
//   sig = MD5( "trackgetFileUrl"
//              + "format_id" + format_id_str
//              + "intent"    + "stream"
//              + "track_id"  + track_id_str
//              + "request_ts"+ timestamp_str
//              + app_secret )
// ---------------------------------------------------------------------------
std::string QobuzAPI::computeRequestSig(const std::string& track_id,
                                         int                format_id,
                                         const std::string& timestamp) const {
    std::string s;
    s += "trackgetFileUrl";
    s += "format_id"  + std::to_string(format_id);
    s += "intent"     "stream";
    s += "track_id"   + track_id;
    s += "request_ts" + timestamp;
    s += m_app_secret;
    return md5hex(s);
}

// ---------------------------------------------------------------------------
// URL encoding
// ---------------------------------------------------------------------------
std::string QobuzAPI::urlEncode(const std::string& s) {
    std::ostringstream out;
    out << std::uppercase << std::hex;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out << c;
        } else {
            out << '%' << std::setw(2) << std::setfill('0') << (int)c;
        }
    }
    return out.str();
}

std::string QobuzAPI::buildQuery(
    const std::vector<std::pair<std::string,std::string>>& params) {
    std::string out;
    for (const auto& [k, v] : params) {
        if (!out.empty()) out += '&';
        out += urlEncode(k) + '=' + urlEncode(v);
    }
    return out;
}

// ---------------------------------------------------------------------------
// JSON helpers
// ---------------------------------------------------------------------------
std::string QobuzAPI::jsonString(const json& j, const char* key,
                                  const std::string& def) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return def;
    if (it->is_string()) return it->get<std::string>();
    if (it->is_number_integer()) return std::to_string(it->get<int64_t>());
    if (it->is_number()) return std::to_string(it->get<double>());
    return def;
}

int QobuzAPI::jsonInt(const json& j, const char* key, int def) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return def;
    if (it->is_number()) return it->get<int>();
    if (it->is_string()) {
        try { return std::stoi(it->get<std::string>()); } catch (...) {}
    }
    return def;
}

bool QobuzAPI::jsonBool(const json& j, const char* key, bool def) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return def;
    if (it->is_boolean()) return it->get<bool>();
    return def;
}

// ---------------------------------------------------------------------------
// Track / album parsers
// ---------------------------------------------------------------------------
QobuzTrack QobuzAPI::parseTrack(const json& jt) {
    QobuzTrack t;
    t.id           = jsonString(jt, "id");
    t.title        = jsonString(jt, "title");
    t.duration     = jsonInt   (jt, "duration");
    t.track_number = jsonInt   (jt, "track_number");
    t.disc_number  = jsonInt   (jt, "media_number", 1);
    t.streamable   = jsonBool  (jt, "streamable");
    t.hires        = jsonBool  (jt, "hires");
    t.bit_depth    = jsonInt   (jt, "maximum_bit_depth", 16);
    t.sampling_rate= jsonInt   (jt, "maximum_sampling_rate", 44100);

    if (jt.contains("performer") && jt["performer"].is_object()) {
        t.performer.id   = jsonString(jt["performer"], "id");
        t.performer.name = jsonString(jt["performer"], "name");
    }

    if (jt.contains("album") && jt["album"].is_object()) {
        const auto& ja = jt["album"];
        t.album_id     = jsonString(ja, "id");
        t.album_title  = jsonString(ja, "title");
        {
            std::string rd = jsonString(ja, "release_date_original");
            if (rd.size() >= 4) {
                try { t.year = std::stoi(rd.substr(0, 4)); } catch (...) {}
            }
        }

        if (ja.contains("artist") && ja["artist"].is_object())
            t.album_artist = jsonString(ja["artist"], "name");

        // Cover art – prefer 'large' size
        if (ja.contains("image") && ja["image"].is_object()) {
            const auto& img = ja["image"];
            t.cover_url = jsonString(img, "large",
                          jsonString(img, "small", ""));
        }
    }
    return t;
}

QobuzAlbum QobuzAPI::parseAlbum(const json& ja) {
    QobuzAlbum a;
    a.id          = jsonString(ja, "id");
    a.title       = jsonString(ja, "title");
    a.track_count = jsonInt   (ja, "tracks_count");

    std::string releaseDate = jsonString(ja, "release_date_original");
    if (releaseDate.size() >= 4)
        a.year = std::stoi(releaseDate.substr(0, 4));

    if (ja.contains("artist") && ja["artist"].is_object()) {
        a.artist.id   = jsonString(ja["artist"], "id");
        a.artist.name = jsonString(ja["artist"], "name");
    }
    if (ja.contains("image") && ja["image"].is_object()) {
        const auto& img = ja["image"];
        a.cover_url = jsonString(img, "large",
                      jsonString(img, "small", ""));
    }

    if (ja.contains("tracks") && ja["tracks"].is_object()) {
        const auto& jTracks = ja["tracks"];
        if (jTracks.contains("items") && jTracks["items"].is_array()) {
            for (const auto& jt : jTracks["items"])
                a.tracks.push_back(parseTrack(jt));
        }
    }
    return a;
}

// ---------------------------------------------------------------------------
// login()  –  POST /user/login
// ---------------------------------------------------------------------------
bool QobuzAPI::login(const std::string& email,
                     const std::string& password,
                     std::string&       out_token,
                     std::string&       out_error) {
    HttpClient http;

    std::string bodyStr = buildQuery({
        {"username", email},
        {"password", md5hex(password)},
        {"app_id",   m_app_id},
    });

    HttpClient::Headers hdrs = {
        {"X-App-Id", m_app_id},
    };

    auto resp = http.post(std::string(BASE_URL) + "/user/login", bodyStr, hdrs);
    if (!resp.ok()) {
        out_error = "Login failed (HTTP " + std::to_string(resp.status) + "): " + resp.body;
        return false;
    }

    try {
        auto j = json::parse(resp.body);
        if (!j.contains("user_auth_token") || j["user_auth_token"].is_null()) {
            out_error = jsonString(j, "message", "Login failed: no auth token in response");
            return false;
        }
        out_token = j["user_auth_token"].get<std::string>();
        return true;
    } catch (const std::exception& e) {
        out_error = std::string("Login JSON parse error: ") + e.what();
        return false;
    }
}

// ---------------------------------------------------------------------------
// getTrackStreamUrl()  –  GET /track/getFileUrl
// ---------------------------------------------------------------------------
bool QobuzAPI::getTrackStreamUrl(const std::string& track_id,
                                  int                format_id,
                                  const std::string& auth_token,
                                  std::string&       out_url,
                                  int&               out_bit_depth,
                                  int&               out_sample_rate,
                                  std::string&       out_error) {
    HttpClient http;

    std::string ts = std::to_string(static_cast<long long>(std::time(nullptr)));
    std::string sig = computeRequestSig(track_id, format_id, ts);

    std::string url = std::string(BASE_URL) + "/track/getFileUrl?" +
        buildQuery({
            {"track_id",    track_id},
            {"format_id",   std::to_string(format_id)},
            {"intent",      "stream"},
            {"request_ts",  ts},
            {"request_sig", sig},
        });

    HttpClient::Headers hdrs = {
        {"X-App-Id",          m_app_id},
        {"X-User-Auth-Token", auth_token},
    };

    auto resp = http.get(url, hdrs);
    if (!resp.ok()) {
        out_error = "getFileUrl failed (HTTP " + std::to_string(resp.status) + "): " + resp.body;
        return false;
    }

    try {
        auto j = json::parse(resp.body);
        out_url         = jsonString(j, "url");
        out_bit_depth   = jsonInt   (j, "bit_depth",    16);
        out_sample_rate = jsonInt   (j, "sampling_rate", 44100);

        if (out_url.empty()) {
            out_error = jsonString(j, "message", "getFileUrl: empty URL in response");
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        out_error = std::string("getFileUrl JSON parse error: ") + e.what();
        return false;
    }
}

// ---------------------------------------------------------------------------
// getTrack()  –  GET /track/get
// ---------------------------------------------------------------------------
bool QobuzAPI::getTrack(const std::string& track_id,
                         const std::string& auth_token,
                         QobuzTrack&        out_track,
                         std::string&       out_error) {
    HttpClient http;

    std::string url = std::string(BASE_URL) + "/track/get?" +
        buildQuery({{"track_id", track_id}});

    HttpClient::Headers hdrs = {
        {"X-App-Id",          m_app_id},
        {"X-User-Auth-Token", auth_token},
    };

    auto resp = http.get(url, hdrs);
    if (!resp.ok()) {
        out_error = "getTrack failed (HTTP " + std::to_string(resp.status) + ")";
        return false;
    }

    try {
        out_track = parseTrack(json::parse(resp.body));
        return true;
    } catch (const std::exception& e) {
        out_error = std::string("getTrack JSON parse error: ") + e.what();
        return false;
    }
}

// ---------------------------------------------------------------------------
// getAlbum()  –  GET /album/get
// ---------------------------------------------------------------------------
bool QobuzAPI::getAlbum(const std::string& album_id,
                         const std::string& auth_token,
                         QobuzAlbum&        out_album,
                         std::string&       out_error) {
    HttpClient http;

    std::string url = std::string(BASE_URL) + "/album/get?" +
        buildQuery({
            {"album_id", album_id},
            {"extra",    "tracks"},
        });

    HttpClient::Headers hdrs = {
        {"X-App-Id",          m_app_id},
        {"X-User-Auth-Token", auth_token},
    };

    auto resp = http.get(url, hdrs);
    if (!resp.ok()) {
        out_error = "getAlbum failed (HTTP " + std::to_string(resp.status) + ")";
        return false;
    }

    try {
        out_album = parseAlbum(json::parse(resp.body));
        return true;
    } catch (const std::exception& e) {
        out_error = std::string("getAlbum JSON parse error: ") + e.what();
        return false;
    }
}

// ---------------------------------------------------------------------------
// parsePlaylist() – internal helper
// ---------------------------------------------------------------------------
static QobuzPlaylist parsePlaylistHeader(const json& jp) {
    QobuzPlaylist p;
    p.id          = jp.contains("id") ? (jp["id"].is_number()
                        ? std::to_string(jp["id"].get<int64_t>())
                        : jp["id"].get<std::string>()) : "";
    p.name        = jp.contains("name")        ? jp["name"].get<std::string>() : "";
    p.description = jp.contains("description") && !jp["description"].is_null()
                        ? jp["description"].get<std::string>() : "";
    p.track_count = jp.contains("tracks_count") ? jp["tracks_count"].get<int>() : 0;
    p.is_public   = jp.contains("is_public")    ? jp["is_public"].get<bool>()   : false;
    if (jp.contains("owner") && jp["owner"].is_object())
        p.owner_name = jp["owner"].contains("name")
                           ? jp["owner"]["name"].get<std::string>() : "";
    return p;
}

// ---------------------------------------------------------------------------
// getFeaturedAlbums()  –  GET /catalog/getFeatured
// ---------------------------------------------------------------------------
bool QobuzAPI::getFeaturedAlbums(const std::string&       auth_token,
                                  std::vector<QobuzAlbum>& out_albums,
                                  std::string&             out_error) {
    HttpClient http;

    std::string url = std::string(BASE_URL) + "/catalog/getFeatured?" +
        buildQuery({
            {"type",   "new-releases"},
            {"limit",  "30"},
            {"offset", "0"},
        });

    HttpClient::Headers hdrs = {
        {"X-App-Id",          m_app_id},
        {"X-User-Auth-Token", auth_token},
    };

    auto resp = http.get(url, hdrs);
    if (!resp.ok()) {
        out_error = "getFeatured failed (HTTP " + std::to_string(resp.status) + ")";
        return false;
    }

    try {
        auto j = json::parse(resp.body);
        // Response: { "albums": { "items": [...] } }
        if (j.contains("albums") && j["albums"].is_object()) {
            const auto& ja = j["albums"];
            if (ja.contains("items") && ja["items"].is_array())
                for (const auto& item : ja["items"])
                    out_albums.push_back(parseAlbum(item));
        }
        return true;
    } catch (const std::exception& e) {
        out_error = std::string("getFeatured JSON parse error: ") + e.what();
        return false;
    }
}

// ---------------------------------------------------------------------------
// getUserPlaylists()  –  GET /playlist/getUserPlaylists
// ---------------------------------------------------------------------------
bool QobuzAPI::getUserPlaylists(const std::string&          auth_token,
                                 std::vector<QobuzPlaylist>& out_playlists,
                                 std::string&                out_error) {
    HttpClient http;

    std::string url = std::string(BASE_URL) + "/playlist/getUserPlaylists?" +
        buildQuery({{"limit", "100"}, {"offset", "0"}});

    HttpClient::Headers hdrs = {
        {"X-App-Id",          m_app_id},
        {"X-User-Auth-Token", auth_token},
    };

    auto resp = http.get(url, hdrs);
    if (!resp.ok()) {
        out_error = "getUserPlaylists failed (HTTP " + std::to_string(resp.status) + ")";
        return false;
    }

    try {
        auto j = json::parse(resp.body);
        // Response: { "playlists": { "items": [...] } }
        if (j.contains("playlists") && j["playlists"].is_object()) {
            const auto& jp = j["playlists"];
            if (jp.contains("items") && jp["items"].is_array())
                for (const auto& item : jp["items"])
                    out_playlists.push_back(parsePlaylistHeader(item));
        }
        return true;
    } catch (const std::exception& e) {
        out_error = std::string("getUserPlaylists JSON parse error: ") + e.what();
        return false;
    }
}

// ---------------------------------------------------------------------------
// getPlaylist()  –  GET /playlist/get  (includes tracks)
// ---------------------------------------------------------------------------
bool QobuzAPI::getPlaylist(const std::string& playlist_id,
                            const std::string& auth_token,
                            QobuzPlaylist&     out_playlist,
                            std::string&       out_error) {
    HttpClient http;

    std::string url = std::string(BASE_URL) + "/playlist/get?" +
        buildQuery({
            {"playlist_id", playlist_id},
            {"extra",       "tracks"},
            {"limit",       "500"},
            {"offset",      "0"},
        });

    HttpClient::Headers hdrs = {
        {"X-App-Id",          m_app_id},
        {"X-User-Auth-Token", auth_token},
    };

    auto resp = http.get(url, hdrs);
    if (!resp.ok()) {
        out_error = "getPlaylist failed (HTTP " + std::to_string(resp.status) + ")";
        return false;
    }

    try {
        auto j = json::parse(resp.body);
        out_playlist = parsePlaylistHeader(j);

        if (j.contains("tracks") && j["tracks"].is_object()) {
            const auto& jt = j["tracks"];
            if (jt.contains("items") && jt["items"].is_array())
                for (const auto& item : jt["items"])
                    out_playlist.tracks.push_back(parseTrack(item));
        }
        return true;
    } catch (const std::exception& e) {
        out_error = std::string("getPlaylist JSON parse error: ") + e.what();
        return false;
    }
}

// ---------------------------------------------------------------------------
// getUserFavorites()  –  GET /favorite/getUserFavorites
// ---------------------------------------------------------------------------
bool QobuzAPI::getUserFavorites(const std::string&       auth_token,
                                 const std::string&       type,
                                 std::vector<QobuzTrack>& out_tracks,
                                 std::vector<QobuzAlbum>& out_albums,
                                 std::string&             out_error) {
    HttpClient http;

    std::string url = std::string(BASE_URL) + "/favorite/getUserFavorites?" +
        buildQuery({
            {"type",   type},
            {"limit",  "200"},
            {"offset", "0"},
        });

    HttpClient::Headers hdrs = {
        {"X-App-Id",          m_app_id},
        {"X-User-Auth-Token", auth_token},
    };

    auto resp = http.get(url, hdrs);
    if (!resp.ok()) {
        out_error = "getUserFavorites failed (HTTP " + std::to_string(resp.status) + ")";
        return false;
    }

    try {
        auto j = json::parse(resp.body);

        if (j.contains("tracks") && j["tracks"].is_object()) {
            const auto& jt = j["tracks"];
            if (jt.contains("items") && jt["items"].is_array())
                for (const auto& item : jt["items"])
                    out_tracks.push_back(parseTrack(item));
        }
        if (j.contains("albums") && j["albums"].is_object()) {
            const auto& ja = j["albums"];
            if (ja.contains("items") && ja["items"].is_array())
                for (const auto& item : ja["items"])
                    out_albums.push_back(parseAlbum(item));
        }
        return true;
    } catch (const std::exception& e) {
        out_error = std::string("getUserFavorites JSON parse error: ") + e.what();
        return false;
    }
}

// ---------------------------------------------------------------------------
// search()  –  GET /search/getResults
// ---------------------------------------------------------------------------
bool QobuzAPI::search(const std::string&  query,
                       const std::string&  auth_token,
                       const std::string&  type,
                       QobuzSearchResults& out_results,
                       std::string&        out_error) {
    HttpClient http;

    std::string url = std::string(BASE_URL) + "/search/getResults?" +
        buildQuery({
            {"query",  query},
            {"type",   type},
            {"limit",  "50"},
            {"offset", "0"},
        });

    HttpClient::Headers hdrs = {
        {"X-App-Id",          m_app_id},
        {"X-User-Auth-Token", auth_token},
    };

    auto resp = http.get(url, hdrs);
    if (!resp.ok()) {
        out_error = "search failed (HTTP " + std::to_string(resp.status) + ")";
        return false;
    }

    try {
        auto j = json::parse(resp.body);

        if (j.contains("tracks") && j["tracks"].is_object()) {
            const auto& jt = j["tracks"];
            if (jt.contains("items") && jt["items"].is_array())
                for (const auto& item : jt["items"])
                    out_results.tracks.push_back(parseTrack(item));
        }
        if (j.contains("albums") && j["albums"].is_object()) {
            const auto& ja = j["albums"];
            if (ja.contains("items") && ja["items"].is_array())
                for (const auto& item : ja["items"])
                    out_results.albums.push_back(parseAlbum(item));
        }
        return true;
    } catch (const std::exception& e) {
        out_error = std::string("search JSON parse error: ") + e.what();
        return false;
    }
}
