// Copyright (C) Radioform / Original Authors
// Modified by Shankar (2026) for the KRISHA Architecture. Renamed namespaces and variables.
// Licensed under the GNU GPLv3.

/**
 * @file version.cpp
 * @brief Version information for Krisha DSP library
 */

#include "krisha_dsp.h"

#ifndef KRISHA_DSP_VERSION
#define KRISHA_DSP_VERSION "1.0.0-dev"
#endif

const char* krisha_dsp_get_version(void) {
    return KRISHA_DSP_VERSION;
}
