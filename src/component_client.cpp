#include "stdafx.h"

static HMODULE s_hModule      = nullptr;
static DWORD   s_mainThreadId = 0;
static bool    s_servicesAvailable = false;

extern "C" BOOL WINAPI DllMain(HINSTANCE hDll, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        s_hModule      = hDll;
        s_mainThreadId = GetCurrentThreadId();
        DisableThreadLibraryCalls(hDll);
    }
    return TRUE;
}

// ---------------------------------------------------------------------------
// core_api – every function declared in core_api.h that is NOT exported by
// shared.dll must be implemented here inside the component DLL.
// ---------------------------------------------------------------------------
namespace core_api {

// ---- Module identity -------------------------------------------------------

const char* get_my_full_path() {
    static pfc::string8 s_path;
    static bool s_init = false;
    if (!s_init) {
        s_init = true;
        wchar_t buf[2048] = {};
        GetModuleFileNameW(s_hModule, buf, 2048);
        s_path = pfc::stringcvt::string_utf8_from_wide(buf);
    }
    return s_path;
}

const char* get_my_file_name() {
    static pfc::string8 s_name;
    static bool s_init = false;
    if (!s_init) {
        s_init = true;
        const char* full  = get_my_full_path();
        const char* slash = strrchr(full, '\\');
        s_name = slash ? slash + 1 : full;
    }
    return s_name;
}

// ---- Threading -------------------------------------------------------------

bool is_main_thread() {
    return GetCurrentThreadId() == s_mainThreadId;
}

bool assert_main_thread() {
    return is_main_thread();
}

void ensure_main_thread() {
    if (!is_main_thread()) uBugCheck();
}

// ---- Service availability --------------------------------------------------

bool are_services_available() {
    return s_servicesAvailable;
}

// ---- UI / process ----------------------------------------------------------

HWND get_main_window() {
    // foobar2000's top-level window class has been "foo_bar" across all versions.
    return FindWindowW(L"foo_bar", nullptr);
}

// ---- Profile path ----------------------------------------------------------

const char* get_profile_path() {
    static pfc::string8 s_path;
    static bool s_init = false;
    if (!s_init) {
        s_init = true;

        // Get foobar2000.exe directory (GetModuleFileName with NULL = EXE)
        wchar_t exeDir[MAX_PATH] = {};
        GetModuleFileNameW(NULL, exeDir, MAX_PATH);
        wchar_t* sep = wcsrchr(exeDir, L'\\');
        if (sep) *sep = L'\0';

        // Portable mode: <exe dir>\profile\ exists
        wchar_t portDir[MAX_PATH];
        swprintf_s(portDir, L"%s\\profile", exeDir);
        DWORD attr = GetFileAttributesW(portDir);
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
            wchar_t buf[MAX_PATH];
            swprintf_s(buf, L"%s\\profile\\", exeDir);
            s_path = pfc::stringcvt::string_utf8_from_wide(buf);
        } else {
            // Standard install: %APPDATA%\foobar2000\
            wchar_t appData[MAX_PATH] = {};
            GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH);
            wchar_t buf[MAX_PATH];
            swprintf_s(buf, L"%s\\foobar2000\\", appData);
            s_path = pfc::stringcvt::string_utf8_from_wide(buf);
        }
    }
    return s_path;
}

} // namespace core_api

// ---------------------------------------------------------------------------
// Track service availability via initquit.
// ---------------------------------------------------------------------------
namespace {
class services_tracker : public initquit {
    void on_init() override { s_servicesAvailable = true;  }
    void on_quit() override { s_servicesAvailable = false; }
};
static initquit_factory_t<services_tracker> g_services_tracker;
} // namespace
