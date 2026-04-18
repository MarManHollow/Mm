#pragma once

// ---- Must come before any Windows headers ----
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

// Windows core
#include <windows.h>
#include <timeapi.h>    // timeGetTime  (used by pfc/timers.h)
#include <winhttp.h>
#include <bcrypt.h>
#include <shlwapi.h>
#include <commctrl.h>

// ATL (ships with Visual Studio – must come before foobar2000 SDK)
#include <atlbase.h>
#include <atlwin.h>

// STL
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <stdexcept>
#include <ctime>
#include <cassert>
#include <thread>
#include <atomic>
#include <map>

// foobar2000 SDK
#include <foobar2000.h>
#include <helpers/helpers.h>
// ATLHelpers is optional – present in some SDK versions, absent in others
#if __has_include(<ATLHelpers/ATLHelpers.h>)
#include <ATLHelpers/ATLHelpers.h>
#endif

// JSON (nlohmann – fetched via CMake FetchContent)
#include <nlohmann/json.hpp>
using json = nlohmann::json;
