#include "stdafx.h"
#include "config.h"

// ---------------------------------------------------------------------------
// Component identity
// ---------------------------------------------------------------------------
DECLARE_COMPONENT_VERSION(
    COMPONENT_NAME,
    COMPONENT_VERSION,
    "Qobuz streaming integration for foobar2000.\n"
    "Supports Hi-Res FLAC (up to 24-bit/192 kHz) and MP3 320 kbps.\n\n"
    "Configuration: Preferences \xbb Qobuz\n\n"
    "Source: https://github.com/marmanhollow/mm\n"
    "License: GPL-3.0"
)

// Ensure the DLL is named exactly foo_qobuz.dll
VALIDATE_COMPONENT_FILENAME("foo_qobuz.dll");
