#include "stdafx.h"
#include "config.h"
#include "qobuz_api.h"
#include "resource.h"

// ---------------------------------------------------------------------------
// Preferences page – shown under Preferences > Qobuz
//
// Controls:
//   IDC_EDIT_APP_ID      – developer app_id
//   IDC_EDIT_APP_SECRET  – developer app_secret
//   IDC_EDIT_EMAIL       – account e-mail
//   IDC_EDIT_PASSWORD    – account password  (password style)
//   IDC_COMBO_FORMAT     – streaming quality selector
//   IDC_BTN_LOGIN        – test login button
//   IDC_STATIC_STATUS    – status label
// ---------------------------------------------------------------------------

namespace {

// Map combo-box index -> QobuzFormat value
static const struct { const char* label; uint32_t format_id; }
kFormats[] = {
    { "FLAC 16-bit (CD)",           kFormatFLAC_16     },
    { "FLAC 24-bit / up to 96 kHz", kFormatFLAC_24_96  },
    { "FLAC 24-bit / up to 192 kHz",kFormatFLAC_24_192 },
    { "MP3 320 kbps",               kFormatMP3_320     },
};
static constexpr int kFormatCount =
    static_cast<int>(sizeof(kFormats) / sizeof(kFormats[0]));

// GUID for the preferences page
// {C3F5A1B2-BEEF-4321-ABCD-000000000001}
static const GUID guid_prefs_page = {
    0xc3f5a1b2, 0xbeef, 0x4321,
    {0xab, 0xcd, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}
};

// ---------------------------------------------------------------------------
// Dialog class (ATL / WTL based, as used by the foobar2000 helpers layer)
// ---------------------------------------------------------------------------
class CQobuzPreferences
    : public CDialogImpl<CQobuzPreferences>,
      public preferences_page_instance {
public:
    enum { IDD = IDD_PREFS_QOBUZ };

    CQobuzPreferences(preferences_page_callback::ptr callback)
        : m_callback(callback) {}

    // --- preferences_page_instance ---
    t_uint32 get_state() override {
        t_uint32 state = preferences_state::resettable;
        if (hasChanges()) state |= preferences_state::changed;
        return state;
    }

    void reset() override {
        SetDlgItemText(IDC_EDIT_APP_ID,     "");
        SetDlgItemText(IDC_EDIT_APP_SECRET, "");
        SetDlgItemText(IDC_EDIT_EMAIL,      "");
        SetDlgItemText(IDC_EDIT_PASSWORD,   "");
        selectFormatCombo(kFormatFLAC_16);
        onChanged();
    }

    void apply() override {
        g_cfg_app_id     = getEditText(IDC_EDIT_APP_ID).c_str();
        g_cfg_app_secret = getEditText(IDC_EDIT_APP_SECRET).c_str();
        g_cfg_email      = getEditText(IDC_EDIT_EMAIL).c_str();
        g_cfg_password   = getEditText(IDC_EDIT_PASSWORD).c_str();

        int sel = SendDlgItemMessage(IDC_COMBO_FORMAT, CB_GETCURSEL, 0, 0);
        if (sel >= 0 && sel < kFormatCount)
            g_cfg_format_id = kFormats[sel].format_id;

        // Invalidate cached token so next playback re-authenticates if creds changed
        g_cfg_auth_token = "";

        onChanged();
    }

    HWND get_wnd() override { return m_hWnd; }

    // --- WTL message map ---
    BEGIN_MSG_MAP(CQobuzPreferences)
        MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
        COMMAND_HANDLER(IDC_EDIT_APP_ID,     EN_CHANGE, OnEditChange)
        COMMAND_HANDLER(IDC_EDIT_APP_SECRET, EN_CHANGE, OnEditChange)
        COMMAND_HANDLER(IDC_EDIT_EMAIL,      EN_CHANGE, OnEditChange)
        COMMAND_HANDLER(IDC_EDIT_PASSWORD,   EN_CHANGE, OnEditChange)
        COMMAND_HANDLER(IDC_COMBO_FORMAT,    CBN_SELCHANGE, OnComboChange)
        COMMAND_HANDLER(IDC_BTN_LOGIN,       BN_CLICKED, OnLoginClicked)
    END_MSG_MAP()

private:
    preferences_page_callback::ptr m_callback;

    LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&) {
        // Populate fields from saved config
        SetDlgItemText(IDC_EDIT_APP_ID,
                       static_cast<const char*>(g_cfg_app_id));
        SetDlgItemText(IDC_EDIT_APP_SECRET,
                       static_cast<const char*>(g_cfg_app_secret));
        SetDlgItemText(IDC_EDIT_EMAIL,
                       static_cast<const char*>(g_cfg_email));
        SetDlgItemText(IDC_EDIT_PASSWORD,
                       static_cast<const char*>(g_cfg_password));

        // Populate quality combo
        CComboBox combo = GetDlgItem(IDC_COMBO_FORMAT);
        for (int i = 0; i < kFormatCount; ++i)
            combo.AddString(pfc::stringcvt::string_wide_from_utf8(kFormats[i].label));
        selectFormatCombo(static_cast<uint32_t>(g_cfg_format_id));

        SetDlgItemText(IDC_STATIC_STATUS, "");
        return TRUE;
    }

    LRESULT OnEditChange(WORD, WORD, HWND, BOOL&) {
        onChanged();
        return 0;
    }
    LRESULT OnComboChange(WORD, WORD, HWND, BOOL&) {
        onChanged();
        return 0;
    }

    LRESULT OnLoginClicked(WORD, WORD, HWND, BOOL&) {
        std::string appId     = getEditText(IDC_EDIT_APP_ID);
        std::string appSecret = getEditText(IDC_EDIT_APP_SECRET);
        std::string email     = getEditText(IDC_EDIT_EMAIL);
        std::string password  = getEditText(IDC_EDIT_PASSWORD);

        if (appId.empty() || appSecret.empty() || email.empty() || password.empty()) {
            SetDlgItemText(IDC_STATIC_STATUS,
                           "Please fill in all fields before testing login.");
            return 0;
        }

        SetDlgItemText(IDC_STATIC_STATUS, "Logging in...");
        UpdateWindow();

        QobuzAPI api(appId, appSecret);
        std::string token, err;
        if (api.login(email, password, token, err)) {
            std::string msg = "Login successful! Token: " +
                              token.substr(0, 8) + "...";
            SetDlgItemText(IDC_STATIC_STATUS, msg.c_str());
            // Temporarily cache token
            g_cfg_auth_token = token.c_str();
        } else {
            SetDlgItemText(IDC_STATIC_STATUS, ("Login failed: " + err).c_str());
        }
        return 0;
    }

    // ---- helpers ----
    void onChanged() {
        if (m_callback.is_valid())
            m_callback->on_state_changed();
    }

    bool hasChanges() const {
        return getEditText(IDC_EDIT_APP_ID)     != (const char*)g_cfg_app_id     ||
               getEditText(IDC_EDIT_APP_SECRET) != (const char*)g_cfg_app_secret ||
               getEditText(IDC_EDIT_EMAIL)      != (const char*)g_cfg_email      ||
               getEditText(IDC_EDIT_PASSWORD)   != (const char*)g_cfg_password   ||
               selectedFormatId()               != (uint32_t)g_cfg_format_id;
    }

    std::string getEditText(int ctrl_id) const {
        pfc::string8 buf;
        uGetDlgItemText(m_hWnd, ctrl_id, buf);
        return buf.get_ptr();
    }

    uint32_t selectedFormatId() const {
        int sel = SendDlgItemMessage(IDC_COMBO_FORMAT, CB_GETCURSEL, 0, 0);
        if (sel >= 0 && sel < kFormatCount)
            return kFormats[sel].format_id;
        return kFormatFLAC_16;
    }

    void selectFormatCombo(uint32_t format_id) {
        for (int i = 0; i < kFormatCount; ++i) {
            if (kFormats[i].format_id == format_id) {
                SendDlgItemMessage(IDC_COMBO_FORMAT, CB_SETCURSEL, i, 0);
                return;
            }
        }
        SendDlgItemMessage(IDC_COMBO_FORMAT, CB_SETCURSEL, 0, 0);
    }
};

// ---------------------------------------------------------------------------
// preferences_page_factory_t helper
// ---------------------------------------------------------------------------
class CQobuzPreferencesImpl : public preferences_page_impl<CQobuzPreferences> {
public:
    const char* get_name() override { return "Qobuz"; }
    GUID        get_guid() override { return guid_prefs_page; }
    GUID        get_parent_guid() override { return preferences_page::guid_tools; }
};

static preferences_page_factory_t<CQobuzPreferencesImpl> g_prefs_factory;

} // anonymous namespace
