#include "stdafx.h"
#include "config.h"

// ---------------------------------------------------------------------------
// GUIDs – generated once, never change
// ---------------------------------------------------------------------------
// {A1B2C3D4-1001-4000-8000-000000000001}
static const GUID guid_cfg_app_id = {
    0xa1b2c3d4, 0x1001, 0x4000, {0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x01}
};
// {A1B2C3D4-1001-4000-8000-000000000002}
static const GUID guid_cfg_app_secret = {
    0xa1b2c3d4, 0x1001, 0x4000, {0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x02}
};
// {A1B2C3D4-1001-4000-8000-000000000003}
static const GUID guid_cfg_email = {
    0xa1b2c3d4, 0x1001, 0x4000, {0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x03}
};
// {A1B2C3D4-1001-4000-8000-000000000004}
static const GUID guid_cfg_password = {
    0xa1b2c3d4, 0x1001, 0x4000, {0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x04}
};
// {A1B2C3D4-1001-4000-8000-000000000005}
static const GUID guid_cfg_auth_token = {
    0xa1b2c3d4, 0x1001, 0x4000, {0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x05}
};
// {A1B2C3D4-1001-4000-8000-000000000006}
static const GUID guid_cfg_format_id = {
    0xa1b2c3d4, 0x1001, 0x4000, {0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x06}
};

// ---------------------------------------------------------------------------
// Definitions
// ---------------------------------------------------------------------------
cfg_string g_cfg_app_id     (guid_cfg_app_id,      "");
cfg_string g_cfg_app_secret (guid_cfg_app_secret,  "");
cfg_string g_cfg_email      (guid_cfg_email,       "");
cfg_string g_cfg_password   (guid_cfg_password,    "");
cfg_string g_cfg_auth_token (guid_cfg_auth_token,  "");
cfg_uint   g_cfg_format_id  (guid_cfg_format_id,   kFormatFLAC_16);
