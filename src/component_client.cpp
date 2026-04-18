#include "stdafx.h"

// Module handle and main-thread ID captured at DLL_PROCESS_ATTACH.
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
// core_api – per-component implementations required by the foobar2000 SDK.
// ---------------------------------------------------------------------------
namespace core_api {

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

bool is_main_thread() {
    return GetCurrentThreadId() == s_mainThreadId;
}

bool are_services_available() {
    return s_servicesAvailable;
}

} // namespace core_api

// ---------------------------------------------------------------------------
// Track service availability via initquit so are_services_available() works.
// ---------------------------------------------------------------------------
namespace {
class services_tracker : public initquit {
    void on_init() override { s_servicesAvailable = true;  }
    void on_quit() override { s_servicesAvailable = false; }
};
static initquit_factory_t<services_tracker> g_services_tracker;
} // namespace
