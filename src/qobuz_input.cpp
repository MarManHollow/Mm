#include "stdafx.h"
#include "config.h"
#include "qobuz_api.h"

// ---------------------------------------------------------------------------
// foobar2000 input component for Qobuz
//
// URL scheme:  qobuz://track/<track_id>
//
// Strategy:
//   1. Resolve qobuz:// URL to an HTTPS CDN stream URL via the Qobuz API.
//   2. Delegate all decoding to foobar2000's built-in HTTP input pipeline.
// ---------------------------------------------------------------------------

namespace {

static std::string extractTrackId(const char* p_path) {
    static const char kPrefix[] = "qobuz://track/";
    constexpr size_t kPrefixLen = sizeof(kPrefix) - 1;
    if (strncmp(p_path, kPrefix, kPrefixLen) != 0) return {};
    std::string id = p_path + kPrefixLen;
    while (!id.empty() && (id.back() == '/' || id.back() == ' '))
        id.pop_back();
    return id;
}

static bool ensureAuthToken(std::string& out_token, std::string& out_error) {
    out_token = static_cast<const char*>(g_cfg_auth_token);
    if (!out_token.empty()) return true;

    const std::string email    = static_cast<const char*>(g_cfg_email);
    const std::string password = static_cast<const char*>(g_cfg_password);
    const std::string app_id   = static_cast<const char*>(g_cfg_app_id);
    const std::string app_sec  = static_cast<const char*>(g_cfg_app_secret);

    if (email.empty() || password.empty()) {
        out_error = "Qobuz: credentials not set. "
                    "Go to Preferences > Qobuz.";
        return false;
    }
    if (app_id.empty() || app_sec.empty()) {
        out_error = "Qobuz: developer app_id / app_secret not configured. "
                    "Go to Preferences > Qobuz.";
        return false;
    }

    QobuzAPI api(app_id, app_sec);
    if (!api.login(email, password, out_token, out_error))
        return false;

    g_cfg_auth_token = out_token.c_str();
    return true;
}

// ---------------------------------------------------------------------------
class input_qobuz : public input_stubs {
public:
    static bool g_is_our_path(const char* p_path, const char* /*p_extension*/) {
        return strncmp(p_path, "qobuz://", 8) == 0;
    }

    static bool g_is_our_content_type(const char* /*p_content_type*/) {
        return false;
    }

    static const char* g_get_name() { return "Qobuz"; }

    static GUID g_get_guid() {
        static const GUID guid = {
            0xb7e4f321, 0xaaaa, 0x4b1c,
            {0x9d, 0x2e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}
        };
        return guid;
    }

    static bool g_is_low_merit() { return false; }

    // ------------------------------------------------------------------
    void open(file::ptr p_filehint,
              const char* p_path,
              t_input_open_reason p_reason,
              abort_callback& p_abort) {
        m_track_id = extractTrackId(p_path);
        if (m_track_id.empty())
            throw exception_io_unsupported_format();

        if (p_reason == input_open_decode || p_reason == input_open_info_read) {
            std::string authToken, err;
            if (!ensureAuthToken(authToken, err))
                throw exception_io_data(err.c_str());

            const std::string app_id  = static_cast<const char*>(g_cfg_app_id);
            const std::string app_sec = static_cast<const char*>(g_cfg_app_secret);
            QobuzAPI api(app_id, app_sec);

            if (!api.getTrack(m_track_id, authToken, m_track, err))
                throw exception_io_data(err.c_str());

            if (!m_track.streamable)
                throw exception_io_data(
                    ("Qobuz: track " + m_track_id + " is not streamable.").c_str());

            if (p_reason == input_open_decode) {
                int bitDepth, sampleRate;
                if (!api.getTrackStreamUrl(m_track_id,
                                           static_cast<int>(g_cfg_format_id),
                                           authToken,
                                           m_stream_url,
                                           bitDepth,
                                           sampleRate,
                                           err))
                    throw exception_io_data(err.c_str());

                input_entry::g_open_for_decoding(m_inner_decoder,
                                                 nullptr,
                                                 m_stream_url.c_str(),
                                                 p_abort);
            }
        }
    }

    // ------------------------------------------------------------------
    // Note: 'override' removed – SDK 2.x changed some method signatures.
    // The methods still override the base class virtuals by name/args match.
    // ------------------------------------------------------------------
    t_uint32 get_subsong_count() { return 1; }

    void get_info(t_uint32 /*p_subsong*/,
                  file_info& p_info,
                  abort_callback& /*p_abort*/) {
        p_info.set_length(static_cast<double>(m_track.duration));
        p_info.info_set("TITLE",        m_track.title.c_str());
        p_info.info_set("ARTIST",       m_track.performer.name.c_str());
        p_info.info_set("ALBUM",        m_track.album_title.c_str());
        p_info.info_set("ALBUM ARTIST", m_track.album_artist.c_str());
        if (m_track.year > 0)
            p_info.info_set("DATE", std::to_string(m_track.year).c_str());
        if (m_track.track_number > 0)
            p_info.info_set_int("TRACKNUMBER", m_track.track_number);
        if (m_track.disc_number > 0)
            p_info.info_set_int("DISCNUMBER",  m_track.disc_number);
        if (!m_track.cover_url.empty())
            p_info.info_set("COVER_URL", m_track.cover_url.c_str());
        p_info.info_set("QOBUZ_TRACK_ID", m_track_id.c_str());
    }

    t_filestats get_file_stats(abort_callback& /*p_abort*/) {
        return filestats_invalid;
    }

    t_filestats2 get_stats2(uint32_t /*f*/, abort_callback& /*p_abort*/) {
        return t_filestats2();
    }

    void decode_initialize(t_uint32 p_subsong,
                           unsigned p_flags,
                           abort_callback& p_abort) {
        if (!m_inner_decoder.is_valid())
            throw exception_io_data("Qobuz: inner decoder not initialised");
        m_inner_decoder->initialize(p_subsong, p_flags, p_abort);
    }

    bool decode_run(audio_chunk& p_chunk, abort_callback& p_abort) {
        if (!m_inner_decoder.is_valid()) return false;
        return m_inner_decoder->run(p_chunk, p_abort);
    }

    void decode_seek(double p_seconds, abort_callback& p_abort) {
        if (m_inner_decoder.is_valid())
            m_inner_decoder->seek(p_seconds, p_abort);
    }

    bool decode_can_seek() {
        return m_inner_decoder.is_valid() && m_inner_decoder->can_seek();
    }

    void decode_on_idle(abort_callback& p_abort) {
        if (m_inner_decoder.is_valid())
            m_inner_decoder->on_idle(p_abort);
    }

    void retag_set_info(t_uint32, const file_info&, abort_callback&) {
        throw exception_tagging_unsupported();
    }
    void retag_commit(abort_callback&) {
        throw exception_tagging_unsupported();
    }
    void remove_tags(abort_callback&) {
        throw exception_tagging_unsupported();
    }

private:
    std::string                    m_track_id;
    std::string                    m_stream_url;
    QobuzTrack                     m_track;
    service_ptr_t<input_decoder>   m_inner_decoder;
};

} // anonymous namespace

static input_factory_t<input_qobuz> g_input_qobuz_factory;
