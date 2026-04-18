cmake_minimum_required(VERSION 3.20)
project(foo_qobuz VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -----------------------------------------------------------------------
# foobar2000 SDK – download from https://www.foobar2000.org/SDK
# Pass -DFB2K_SDK_PATH=/path/to/sdk when configuring.
# -----------------------------------------------------------------------
set(FB2K_SDK_PATH "" CACHE PATH "Path to the foobar2000 SDK root directory")
if("${FB2K_SDK_PATH}" STREQUAL "")
    message(FATAL_ERROR
        "FB2K_SDK_PATH is not set.\n"
        "Download the SDK from https://www.foobar2000.org/SDK then run:\n"
        "  cmake -DFB2K_SDK_PATH=/path/to/sdk -B build .")
endif()

# -----------------------------------------------------------------------
# nlohmann/json (header-only)
# -----------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
set(JSON_BuildTests    OFF CACHE INTERNAL "")
set(JSON_Install       OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

# -----------------------------------------------------------------------
# Sources
# -----------------------------------------------------------------------
set(SOURCES
    src/main.cpp
    src/config.cpp
    src/http_client.cpp
    src/qobuz_api.cpp
    src/qobuz_input.cpp
    src/preferences.cpp
    src/qobuz_browser.cpp
    resources/resource.rc
)

add_library(foo_qobuz SHARED ${SOURCES})

target_include_directories(foo_qobuz PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/resources
    # foobar2000 SDK layout
    ${FB2K_SDK_PATH}
    ${FB2K_SDK_PATH}/foobar2000
    ${FB2K_SDK_PATH}/foobar2000/SDK
    ${FB2K_SDK_PATH}/foobar2000/helpers
    ${FB2K_SDK_PATH}/foobar2000/ATLHelpers
    ${FB2K_SDK_PATH}/WTL/include
    ${FB2K_SDK_PATH}/pfc
)

target_compile_definitions(foo_qobuz PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(foo_qobuz PRIVATE
    nlohmann_json::nlohmann_json
    winhttp
    bcrypt
    crypt32
    shlwapi
    comctl32
    winmm       # timeGetTime (used by pfc/timers.h)
)

# foobar2000 SDK static libs
# The SDK must be built with Visual Studio before linking.
# VS places libs under x64/Release (64-bit) or Release (32-bit).
function(fb2k_find_lib OUT_VAR LIB_NAME BASE_DIR)
    find_library(${OUT_VAR}
        NAMES "${LIB_NAME}"
        HINTS
            "${BASE_DIR}/x64/Release"
            "${BASE_DIR}/Release"
            "${BASE_DIR}/x64/Debug"
            "${BASE_DIR}/Debug"
            "${BASE_DIR}"
        NO_DEFAULT_PATH
    )
    if(NOT ${OUT_VAR})
        message(FATAL_ERROR
            "Cannot find ${LIB_NAME}.lib under ${BASE_DIR}.\n"
            "Build the foobar2000 SDK in Visual Studio first (Release|x64),\n"
            "then re-run cmake --build.")
    endif()
endfunction()

fb2k_find_lib(FB2K_SDK_LIB     foobar2000_SDK     "${FB2K_SDK_PATH}/foobar2000/SDK")
fb2k_find_lib(FB2K_HELPERS_LIB foobar2000_helpers "${FB2K_SDK_PATH}/foobar2000/helpers")
fb2k_find_lib(FB2K_PFC_LIB     pfc                "${FB2K_SDK_PATH}/pfc")

target_link_libraries(foo_qobuz PRIVATE
    "${FB2K_SDK_LIB}"
    "${FB2K_HELPERS_LIB}"
    "${FB2K_PFC_LIB}"
)

set_target_properties(foo_qobuz PROPERTIES
    OUTPUT_NAME "foo_qobuz"
    SUFFIX      ".dll"
    PREFIX      ""
)

# Rename to .fb2k-component so it can be drag-dropped into foobar2000
add_custom_command(TARGET foo_qobuz POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:foo_qobuz>
        $<TARGET_FILE_DIR:foo_qobuz>/foo_qobuz.fb2k-component
    COMMENT "Copying foo_qobuz.dll -> foo_qobuz.fb2k-component"
)

install(FILES $<TARGET_FILE_DIR:foo_qobuz>/foo_qobuz.fb2k-component
    DESTINATION ".")
