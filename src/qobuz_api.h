#pragma once
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Plain data types returned by the Qobuz API client
// ---------------------------------------------------------------------------
struct QobuzArtist {
    std::string id;
    std::string name;
};

struct QobuzTrack {
    std::string   id;
    std::string   title;
    QobuzArtist   performer;
    std::string   album_id;
    std::string   album_title;
    std::string   album_artist;
    std::string   cover_url;     // album art (large)
    int           duration     = 0;
    int           track_number = 0;
    int           disc_number  = 0;
    int           year         = 0;
    bool          streamable   = false;
    bool          hires        = false;
    int           bit_depth    = 16;
    int           sampling_rate= 44100;
};

struct QobuzAlbum {
    std::string            id;
    std::string            title;
    QobuzArtist            artist;
    std::string            cover_url;
    int                    year      = 0;
    int                    track_count = 0;
    std::vector<QobuzTrack> tracks;
};

struct QobuzSearchResults {
    std::vector<QobuzTrack> tracks;
    std::vector<QobuzAlbum> albums;
};

struct QobuzPlaylist {
    std::string             id;
    std::string             name;
    std::string             description;
    std::string             owner_name;
    int                     track_count = 0;
    bool                    is_public   = false;
    std::vector<QobuzTrack> tracks;   // populated by getPlaylist()
};

// ---------------------------------------------------------------------------
// Qobuz REST API v0.2 client
// ---------------------------------------------------------------------------
class QobuzAPI {
public:
    static constexpr const char* BASE_URL = "https://www.qobuz.com/api.json/0.2";

    // Construct with developer credentials.
    // Register at https://www.qobuz.com/us-en/api to obtain app_id / app_secret.
    QobuzAPI(const std::string& app_id, const std::string& app_secret);

    // -----------------------------------------------------------------------
    // Authentication
    // POST /user/login
    // Returns the user_auth_token on success; leaves out_error empty.
    // -----------------------------------------------------------------------
    bool login(const std::string& email,
               const std::string& password,
               std::string& out_auth_token,
               std::string& out_error);

    // -----------------------------------------------------------------------
    // Streaming URL
    // GET /track/getFileUrl
    // format_id: see QobuzFormat in config.h
    // Returns the CDN stream URL for the track.
    // -----------------------------------------------------------------------
    bool getTrackStreamUrl(const std::string& track_id,
                           int                format_id,
                           const std::string& auth_token,
                           std::string&       out_url,
                           int&               out_bit_depth,
                           int&               out_sample_rate,
                           std::string&       out_error);

    // -----------------------------------------------------------------------
    // Metadata
    // -----------------------------------------------------------------------
    bool getTrack(const std::string& track_id,
                  const std::string& auth_token,
                  QobuzTrack&        out_track,
                  std::string&       out_error);

    bool getAlbum(const std::string& album_id,
                  const std::string& auth_token,
                  QobuzAlbum&        out_album,
                  std::string&       out_error);

    // -----------------------------------------------------------------------
    // Search
    // type: "tracks" | "albums" | "artists" (default "tracks")
    // -----------------------------------------------------------------------
    bool search(const std::string&   query,
                const std::string&   auth_token,
                const std::string&   type,
                QobuzSearchResults&  out_results,
                std::string&         out_error);

    // -----------------------------------------------------------------------
    // Discovery / library browsing
    // -----------------------------------------------------------------------

    // GET /catalog/getFeatured – editor-curated new releases
    bool getFeaturedAlbums(const std::string&        auth_token,
                           std::vector<QobuzAlbum>&  out_albums,
                           std::string&              out_error);

    // GET /playlist/getUserPlaylists – all playlists owned/followed by user
    bool getUserPlaylists(const std::string&           auth_token,
                          std::vector<QobuzPlaylist>&  out_playlists,
                          std::string&                 out_error);

    // GET /playlist/get – single playlist with its full track list
    bool getPlaylist(const std::string&  playlist_id,
                     const std::string&  auth_token,
                     QobuzPlaylist&      out_playlist,
                     std::string&        out_error);

    // GET /favorite/getUserFavorites
    // type: "tracks" or "albums"
    bool getUserFavorites(const std::string&        auth_token,
                          const std::string&        type,
                          std::vector<QobuzTrack>&  out_tracks,
                          std::vector<QobuzAlbum>&  out_albums,
                          std::string&              out_error);

private:
    std::string m_app_id;
    std::string m_app_secret;

    // Compute HMAC-MD5 request signature for /track/getFileUrl
    std::string computeRequestSig(const std::string& track_id,
                                  int                format_id,
                                  const std::string& timestamp) const;

    // MD5 of an arbitrary string (hex-encoded, lower-case)
    static std::string md5hex(const std::string& input);

    // URL-encode a UTF-8 string (percent-encoding)
    static std::string urlEncode(const std::string& s);

    // Build query-string from key/value pairs
    static std::string buildQuery(
        const std::vector<std::pair<std::string,std::string>>& params);

    // JSON helpers
    static std::string jsonString (const json& j, const char* key,
                                   const std::string& def = "");
    static int         jsonInt    (const json& j, const char* key, int def = 0);
    static bool        jsonBool   (const json& j, const char* key, bool def = false);

    static QobuzTrack  parseTrack (const json& jt);
    static QobuzAlbum  parseAlbum (const json& ja);
};
