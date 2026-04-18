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
# foobar2000 SDK components – each built as its own static lib.
#
# NOTE: Do NOT define WIN32_LEAN_AND_MEAN on the SDK/pfc/helpers targets.
#       The SDK headers need COM types (interface, IUnknown, IDataObject)
#       and mmsystem.h (timeGetTime), all of which WIN32_LEAN_AND_MEAN strips.
# -----------------------------------------------------------------------
file(GLOB PFC_SOURCES          CONFIGURE_DEPENDS "${FB2K_SDK_PATH}/pfc/*.cpp")
file(GLOB FB2K_SDK_SOURCES     CONFIGURE_DEPENDS "${FB2K_SDK_PATH}/foobar2000/SDK/*.cpp")
file(GLOB FB2K_HELPERS_SOURCES CONFIGURE_DEPENDS "${FB2K_SDK_PATH}/foobar2000/helpers/*.cpp")

if(NOT FB2K_SDK_SOURCES)
    message(FATAL_ERROR
        "No .cpp files found under ${FB2K_SDK_PATH}/foobar2000/SDK\n"
        "Check that FB2K_SDK_PATH points to the SDK root.")
endif()

# pfc
add_library(pfc STATIC ${PFC_SOURCES})
target_include_directories(pfc PUBLIC
    "${FB2K_SDK_PATH}/pfc"
    "${FB2K_SDK_PATH}")
target_compile_definitions(pfc PRIVATE UNICODE _UNICODE NOMINMAX)
target_link_libraries(pfc PRIVATE winmm)
set_target_properties(pfc PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# foobar2000_SDK
add_library(foobar2000_SDK STATIC ${FB2K_SDK_SOURCES})
target_include_directories(foobar2000_SDK PUBLIC
    "${FB2K_SDK_PATH}"
    "${FB2K_SDK_PATH}/foobar2000"
    "${FB2K_SDK_PATH}/foobar2000/SDK"
    "${FB2K_SDK_PATH}/pfc")
target_compile_definitions(foobar2000_SDK PRIVATE UNICODE _UNICODE NOMINMAX)
target_link_libraries(foobar2000_SDK PUBLIC pfc)
set_target_properties(foobar2000_SDK PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# foobar2000_helpers  (needs WTL for atlapp.h et al.)
add_library(foobar2000_helpers STATIC ${FB2K_HELPERS_SOURCES})
target_include_directories(foobar2000_helpers PUBLIC
    "${FB2K_SDK_PATH}"
    "${FB2K_SDK_PATH}/foobar2000"
    "${FB2K_SDK_PATH}/foobar2000/SDK"
    "${FB2K_SDK_PATH}/foobar2000/helpers"
    "${FB2K_SDK_PATH}/WTL/include"
    "${FB2K_SDK_PATH}/pfc")
target_compile_definitions(foobar2000_helpers PRIVATE UNICODE _UNICODE NOMINMAX)
target_link_libraries(foobar2000_helpers PUBLIC foobar2000_SDK)
set_target_properties(foobar2000_helpers PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# -----------------------------------------------------------------------
# foo_qobuz plugin
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
    "${FB2K_SDK_PATH}"
    "${FB2K_SDK_PATH}/foobar2000"
    "${FB2K_SDK_PATH}/foobar2000/SDK"
    "${FB2K_SDK_PATH}/foobar2000/helpers"
    "${FB2K_SDK_PATH}/foobar2000/ATLHelpers"
    "${FB2K_SDK_PATH}/WTL/include"
    "${FB2K_SDK_PATH}/pfc"
)

target_compile_definitions(foo_qobuz PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(foo_qobuz PRIVATE
    foobar2000_SDK
    foobar2000_helpers
    pfc
    nlohmann_json::nlohmann_json
    winhttp
    bcrypt
    crypt32
    shlwapi
    comctl32
    winmm
)

set_target_properties(foo_qobuz PROPERTIES
    OUTPUT_NAME "foo_qobuz"
    SUFFIX      ".dll"
    PREFIX      ""
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
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
