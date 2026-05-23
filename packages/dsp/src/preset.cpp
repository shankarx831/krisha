// Copyright (C) Radioform / Original Authors
// Modified by Shankar (2026) for the KRISHA Architecture. Renamed namespaces and variables.
// Licensed under the GNU GPLv3.

/**
 * @file preset.cpp
 * @brief Preset validation and initialization
 */

#include "krisha_dsp.h"
#include <cstring>
#include <cmath>

void krisha_dsp_preset_init_flat(krisha_preset_t* preset) {
    if (!preset) return;

    // Clear all memory
    std::memset(preset, 0, sizeof(krisha_preset_t));

    // Initialize with flat response (all bands disabled)
    preset->num_bands = KRISHA_MAX_BANDS;

    // Default 10-band EQ frequencies (standard graphic EQ)
    const float default_frequencies[KRISHA_MAX_BANDS] = {
        32.0f, 64.0f, 125.0f, 250.0f, 500.0f,
        1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f
    };

    for (uint32_t i = 0; i < KRISHA_MAX_BANDS; i++) {
        preset->bands[i].frequency_hz = default_frequencies[i];
        preset->bands[i].gain_db = 0.0f;
        preset->bands[i].q_factor = 1.0f; // Default Q
        preset->bands[i].type = KRISHA_FILTER_PEAK;
        preset->bands[i].enabled = false; // Disabled by default
    }

    // Initialize global settings
    preset->preamp_db = 0.0f;
    preset->preamp_left_db = 0.0f;
    preset->preamp_right_db = 0.0f;
    preset->limiter_enabled = false; // Disabled for flat preset (transparent testing)
    preset->limiter_threshold_db = -0.1f; // Just below 0dB

    // Set default name
    std::strncpy(preset->name, "Flat", sizeof(preset->name) - 1);
    preset->name[sizeof(preset->name) - 1] = '\0';
}

krisha_error_t krisha_dsp_preset_validate(const krisha_preset_t* preset) {
    if (!preset) {
        return KRISHA_ERROR_NULL_POINTER;
    }

    // Validate number of bands
    if (preset->num_bands == 0 || preset->num_bands > KRISHA_MAX_BANDS) {
        return KRISHA_ERROR_INVALID_PARAM;
    }

    // Validate each band
    for (uint32_t i = 0; i < preset->num_bands; i++) {
        const krisha_band_t* band = &preset->bands[i];

        // Check for NaN or infinity on band parameters
        if (!std::isfinite(band->frequency_hz) || !std::isfinite(band->gain_db) ||
            !std::isfinite(band->q_factor)) {
            return KRISHA_ERROR_INVALID_PARAM;
        }

        // Validate frequency (20 Hz to 20 kHz)
        if (band->frequency_hz < 20.0f || band->frequency_hz > 20000.0f) {
            return KRISHA_ERROR_INVALID_PARAM;
        }

        // Validate gain (-36 dB to +36 dB)
        if (band->gain_db < -36.0f || band->gain_db > 36.0f) {
            return KRISHA_ERROR_INVALID_PARAM;
        }

        // Validate Q factor (0.01 to 100.0)
        if (band->q_factor < 0.01f || band->q_factor > 100.0f) {
            return KRISHA_ERROR_INVALID_PARAM;
        }

        // Validate filter type
        if (band->type < KRISHA_FILTER_PEAK || band->type > KRISHA_FILTER_BAND_PASS) {
            return KRISHA_ERROR_INVALID_PARAM;
        }
    }

    // Check for NaN or infinity on global parameters
    if (!std::isfinite(preset->preamp_db) || !std::isfinite(preset->preamp_left_db) ||
        !std::isfinite(preset->preamp_right_db) || !std::isfinite(preset->limiter_threshold_db)) {
        return KRISHA_ERROR_INVALID_PARAM;
    }

    // Validate preamp (-36 dB to +36 dB)
    if (preset->preamp_db < -36.0f || preset->preamp_db > 36.0f ||
        preset->preamp_left_db < -36.0f || preset->preamp_left_db > 36.0f ||
        preset->preamp_right_db < -36.0f || preset->preamp_right_db > 36.0f) {
        return KRISHA_ERROR_INVALID_PARAM;
    }

    // Validate limiter threshold (-6 dB to 0 dB)
    if (preset->limiter_threshold_db < -6.0f || preset->limiter_threshold_db > 0.0f) {
        return KRISHA_ERROR_INVALID_PARAM;
    }

    return KRISHA_OK;
}
