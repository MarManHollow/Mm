#include "stdafx.h"
#if __has_include(<SDK/cfg_var_legacy.h>)
#include <SDK/cfg_var_legacy.h>
#endif

// ---------------------------------------------------------------------------
// Globals set by foobar2000_get_interface() at component load time
// ---------------------------------------------------------------------------
static HINSTANCE             g_hIns              = nullptr;
static pfc::string_simple    g_name;
static pfc::string_simple    g_full_path;
static bool                  g_services_available = false;
static bool                  g_initialized        = false;

// ---------------------------------------------------------------------------
// core_api – all functions delegate to g_foobar2000_api (set by fb2k at init)
// ---------------------------------------------------------------------------
namespace core_api {

HINSTANCE get_my_instance()    { return g_hIns; }
const char* get_my_file_name() { return g_name; }
const char* get_my_full_path() { return g_full_path; }
bool are_services_available()  { return g_services_available; }

fb2k::hwnd_t get_main_window() {
    PFC_ASSERT(g_foobar2000_api != nullptr);
    return g_foobar2000_api->get_main_window();
}

bool assert_main_thread() {
    return (g_services_available && g_foobar2000_api)
        ? g_foobar2000_api->assert_main_thread() : true;
}

void ensure_main_thread() {
    if (!is_main_thread()) FB2K_BugCheck();
}

bool is_main_thread() {
    return (g_services_available && g_foobar2000_api)
        ? g_foobar2000_api->is_main_thread() : true;
}

const char* get_profile_path() {
    PFC_ASSERT(g_foobar2000_api != nullptr);
    return g_foobar2000_api->get_profile_path();
}

bool is_shutting_down() {
    return (g_services_available && g_foobar2000_api)
        ? g_foobar2000_api->is_shutting_down() : g_initialized;
}

bool is_initializing() {
    return (g_services_available && g_foobar2000_api)
        ? g_foobar2000_api->is_initializing() : !g_initialized;
}

bool is_portable_mode_enabled() {
    PFC_ASSERT(g_foobar2000_api != nullptr);
    return g_foobar2000_api->is_portable_mode_enabled();
}

bool is_quiet_mode_enabled() {
    PFC_ASSERT(g_foobar2000_api != nullptr);
    return g_foobar2000_api->is_quiet_mode_enabled();
}

} // namespace core_api

// ---------------------------------------------------------------------------
// foobar2000_client implementation – returned to fb2k by foobar2000_get_interface
// ---------------------------------------------------------------------------
namespace {
class foobar2000_client_impl
    : public foobar2000_client
    , private foobar2000_component_globals
{
public:
    t_uint32 get_version() override { return FOOBAR2000_CLIENT_VERSION; }

    pservice_factory_base get_service_list() override {
        return service_factory_base::__internal__list;
    }

    void get_config(stream_writer* p_stream, abort_callback& p_abort) override {
#ifdef FOOBAR2000_HAVE_CFG_VAR_LEGACY
        cfg_var_legacy::cfg_var::config_write_file(p_stream, p_abort);
#endif
    }

    void set_config(stream_reader* p_stream, abort_callback& p_abort) override {
#ifdef FOOBAR2000_HAVE_CFG_VAR_LEGACY
        cfg_var_legacy::cfg_var::config_read_file(p_stream, p_abort);
#endif
    }

    void set_library_path(const char* path, const char* name) override {
        g_full_path = path;
        g_name      = name;
    }

    void services_init(bool val) override {
        if (val) g_initialized = true;
        g_services_available = val;
    }

    bool is_debug() override { return PFC_DEBUG != 0; }
};
} // namespace

static foobar2000_client_impl g_client;

// ---------------------------------------------------------------------------
// DllMain – minimal; fb2k passes HINSTANCE via foobar2000_get_interface
// ---------------------------------------------------------------------------
extern "C" BOOL WINAPI DllMain(HINSTANCE hDll, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(hDll);
    return TRUE;
}

// ---------------------------------------------------------------------------
// Component entry point – foobar2000.exe calls this to initialize the plugin
// ---------------------------------------------------------------------------
extern "C" __declspec(dllexport)
foobar2000_client* _cdecl foobar2000_get_interface(foobar2000_api* p_api, HINSTANCE hIns) {
    g_hIns           = hIns;
    g_foobar2000_api = p_api;
    return &g_client;
}
