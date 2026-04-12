#pragma once

// Windows
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <shlwapi.h>

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

// foobar2000 SDK
#include <foobar2000.h>
#include <helpers/helpers.h>
#include <ATLHelpers/ATLHelpers.h>

// JSON (nlohmann – fetched via CMake FetchContent)
#include <nlohmann/json.hpp>
using json = nlohmann::json;
