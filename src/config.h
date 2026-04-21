#pragma once
#include "stdafx.h"

// ---------------------------------------------------------------------------
// Build-time constants
// ---------------------------------------------------------------------------
#define COMPONENT_NAME    "Qobuz"
#define COMPONENT_VERSION "1.0.0"

// ---------------------------------------------------------------------------
// Qobuz audio format IDs
// ---------------------------------------------------------------------------
enum QobuzFormat : uint32_t {
    kFormatMP3_320     =  5,
    kFormatFLAC_16     =  6,
    kFormatFLAC_24_96  =  7,
    kFormatFLAC_24_192 = 27,
};

// ---------------------------------------------------------------------------
// Persisted configuration variables (defined in config.cpp)
// ---------------------------------------------------------------------------

// Your Qobuz developer app_id (register at https://www.qobuz.com/us-en/api)
extern cfg_string g_cfg_app_id;

// Your Qobuz developer app_secret
extern cfg_string g_cfg_app_secret;

// User credentials
extern cfg_string g_cfg_email;
extern cfg_string g_cfg_password;

// Auth token returned by login – cached between sessions
extern cfg_string g_cfg_auth_token;

// Preferred streaming format (QobuzFormat enum value)
extern cfg_uint g_cfg_format_id;
