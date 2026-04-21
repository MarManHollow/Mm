#include "stdafx.h"

// ---------------------------------------------------------------------------
// core_api functions that must be provided by each component DLL.
// These are called by SDK object files (metadb, config_object, etc.) that
// get pulled into the link via our use of metadb / cfg_var services.
// ---------------------------------------------------------------------------

namespace core_api {

HWND get_main_window() {
    return FindWindowW(L"foo_bar", nullptr);
}

bool assert_main_thread() {
    bool ok = (GetCurrentThreadId() == GetCurrentThreadId()); // placeholder
    (void)ok;
    return true;
}

void ensure_main_thread() {
    // no-op – called by SDK guards; we trust foobar2000 calls us correctly
}

const char* get_profile_path() {
    static char s_buf[MAX_PATH * 3] = {};
    static bool s_init = false;
    if (!s_init) {
        s_init = true;

        wchar_t exeDir[MAX_PATH] = {};
        GetModuleFileNameW(NULL, exeDir, MAX_PATH);
        wchar_t* sep = wcsrchr(exeDir, L'\\');
        if (sep) *sep = L'\0';

        // Portable: exe dir contains a "profile" subfolder
        wchar_t portDir[MAX_PATH];
        swprintf_s(portDir, L"%s\\profile", exeDir);
        DWORD attr = GetFileAttributesW(portDir);
        wchar_t result[MAX_PATH];
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
            swprintf_s(result, L"%s\\profile\\", exeDir);
        } else {
            wchar_t appData[MAX_PATH] = {};
            GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH);
            swprintf_s(result, L"%s\\foobar2000\\", appData);
        }

        WideCharToMultiByte(CP_UTF8, 0, result, -1, s_buf, sizeof(s_buf), nullptr, nullptr);
    }
    return s_buf;
}

} // namespace core_api
