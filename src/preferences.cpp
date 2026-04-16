#include "stdafx.h"
#include "config.h"
#include "qobuz_api.h"
#include "resource.h"

// ---------------------------------------------------------------------------
// Format table (ASCII labels – safe to use as wide literals)
// ---------------------------------------------------------------------------
namespace {

static const struct { const wchar_t* label; uint32_t format_id; }
kFormats[] = {
    { L"FLAC 16-bit (CD)",            kFormatFLAC_16     },
    { L"FLAC 24-bit / up to 96 kHz",  kFormatFLAC_24_96  },
    { L"FLAC 24-bit / up to 192 kHz", kFormatFLAC_24_192 },
    { L"MP3 320 kbps",                kFormatMP3_320     },
};
static constexpr int kFormatCount =
    static_cast<int>(sizeof(kFormats) / sizeof(kFormats[0]));

// {C3F5A1B2-BEEF-4321-ABCD-000000000001}
static const GUID guid_prefs_page = {
    0xc3f5a1b2, 0xbeef, 0x4321,
    {0xab, 0xcd, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}
};

// ---------------------------------------------------------------------------
// Pure Win32 helpers – no foobar2000 uSetDlgItemText / uGetDlgItemText needed
// ---------------------------------------------------------------------------

static void dlgSetText(HWND dlg, int ctrl, const char* utf8) {
    pfc::stringcvt::string_wide_from_utf8 w(utf8);
    ::SetDlgItemTextW(dlg, ctrl, w);
}

static std::string dlgGetText(HWND dlg, int ctrl) {
    wchar_t buf[4096] = {};
    ::GetDlgItemTextW(dlg, ctrl, buf, 4096);
    pfc::stringcvt::string_utf8_from_wide conv(buf, wcslen(buf));
    return conv.get_ptr();
}

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
        dlgSetText(m_hWnd, IDC_EDIT_APP_ID,     "");
        dlgSetText(m_hWnd, IDC_EDIT_APP_SECRET, "");
        dlgSetText(m_hWnd, IDC_EDIT_EMAIL,      "");
        dlgSetText(m_hWnd, IDC_EDIT_PASSWORD,   "");
        setFormatCombo(kFormatFLAC_16);
        onChanged();
    }

    void apply() override {
        g_cfg_app_id     = getEditText(IDC_EDIT_APP_ID).c_str();
        g_cfg_app_secret = getEditText(IDC_EDIT_APP_SECRET).c_str();
        g_cfg_email      = getEditText(IDC_EDIT_EMAIL).c_str();
        g_cfg_password   = getEditText(IDC_EDIT_PASSWORD).c_str();

        int sel = (int)::SendDlgItemMessage(m_hWnd, IDC_COMBO_FORMAT,
                                            CB_GETCURSEL, 0, 0);
        if (sel >= 0 && sel < kFormatCount)
            g_cfg_format_id = kFormats[sel].format_id;

        g_cfg_auth_token = ""; // invalidate cached token
        onChanged();
    }

    HWND get_wnd() override { return m_hWnd; }

    BEGIN_MSG_MAP(CQobuzPreferences)
        MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
        COMMAND_HANDLER(IDC_EDIT_APP_ID,     EN_CHANGE,    OnEditChange)
        COMMAND_HANDLER(IDC_EDIT_APP_SECRET, EN_CHANGE,    OnEditChange)
        COMMAND_HANDLER(IDC_EDIT_EMAIL,      EN_CHANGE,    OnEditChange)
        COMMAND_HANDLER(IDC_EDIT_PASSWORD,   EN_CHANGE,    OnEditChange)
        COMMAND_HANDLER(IDC_COMBO_FORMAT,    CBN_SELCHANGE,OnComboChange)
        COMMAND_HANDLER(IDC_BTN_LOGIN,       BN_CLICKED,   OnLoginClicked)
    END_MSG_MAP()

private:
    preferences_page_callback::ptr m_callback;

    LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&) {
        dlgSetText(m_hWnd, IDC_EDIT_APP_ID,
                   static_cast<const char*>(g_cfg_app_id));
        dlgSetText(m_hWnd, IDC_EDIT_APP_SECRET,
                   static_cast<const char*>(g_cfg_app_secret));
        dlgSetText(m_hWnd, IDC_EDIT_EMAIL,
                   static_cast<const char*>(g_cfg_email));
        dlgSetText(m_hWnd, IDC_EDIT_PASSWORD,
                   static_cast<const char*>(g_cfg_password));

        HWND hCombo = GetDlgItem(IDC_COMBO_FORMAT);
        for (int i = 0; i < kFormatCount; ++i)
            ::SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)kFormats[i].label);
        setFormatCombo(static_cast<uint32_t>(g_cfg_format_id));

        ::SetDlgItemTextW(m_hWnd, IDC_STATIC_STATUS, L"");
        return TRUE;
    }

    LRESULT OnEditChange (WORD, WORD, HWND, BOOL&) { onChanged(); return 0; }
    LRESULT OnComboChange(WORD, WORD, HWND, BOOL&) { onChanged(); return 0; }

    LRESULT OnLoginClicked(WORD, WORD, HWND, BOOL&) {
        std::string appId     = getEditText(IDC_EDIT_APP_ID);
        std::string appSecret = getEditText(IDC_EDIT_APP_SECRET);
        std::string email     = getEditText(IDC_EDIT_EMAIL);
        std::string password  = getEditText(IDC_EDIT_PASSWORD);

        if (appId.empty() || appSecret.empty() ||
            email.empty() || password.empty()) {
            dlgSetText(m_hWnd, IDC_STATIC_STATUS,
                       "Please fill in all fields before testing login.");
            return 0;
        }

        dlgSetText(m_hWnd, IDC_STATIC_STATUS, "Logging in...");
        ::UpdateWindow(m_hWnd);

        QobuzAPI api(appId, appSecret);
        std::string token, err;
        if (api.login(email, password, token, err)) {
            std::string msg = "Login successful! Token: " +
                              token.substr(0, 8) + "...";
            dlgSetText(m_hWnd, IDC_STATIC_STATUS, msg.c_str());
            g_cfg_auth_token = token.c_str();
        } else {
            dlgSetText(m_hWnd, IDC_STATIC_STATUS,
                       ("Login failed: " + err).c_str());
        }
        return 0;
    }

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
        wchar_t buf[2048] = {};
        ::GetDlgItemTextW(m_hWnd, ctrl_id, buf, 2048);
        pfc::stringcvt::string_utf8_from_wide conv(buf, wcslen(buf));
        return conv.get_ptr();
    }

    uint32_t selectedFormatId() const {
        int sel = (int)::SendDlgItemMessage(m_hWnd, IDC_COMBO_FORMAT,
                                            CB_GETCURSEL, 0, 0);
        if (sel >= 0 && sel < kFormatCount)
            return kFormats[sel].format_id;
        return kFormatFLAC_16;
    }

    void setFormatCombo(uint32_t format_id) {
        for (int i = 0; i < kFormatCount; ++i) {
            if (kFormats[i].format_id == format_id) {
                ::SendDlgItemMessage(m_hWnd, IDC_COMBO_FORMAT,
                                     CB_SETCURSEL, i, 0);
                return;
            }
        }
        ::SendDlgItemMessage(m_hWnd, IDC_COMBO_FORMAT, CB_SETCURSEL, 0, 0);
    }
};

// ---------------------------------------------------------------------------
class CQobuzPreferencesImpl : public preferences_page_impl<CQobuzPreferences> {
public:
    const char* get_name() override { return "Qobuz"; }
    GUID        get_guid() override { return guid_prefs_page; }
    GUID        get_parent_guid() override { return preferences_page::guid_tools; }
};

static preferences_page_factory_t<CQobuzPreferencesImpl> g_prefs_factory;

} // anonymous namespace
