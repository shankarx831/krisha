// Copyright (C) Radioform / Original Authors
// Modified by Shankar (2026) for the KRISHA Architecture. Renamed namespaces and variables.
// Licensed under the GNU GPLv3.

/**
 * @file test_preset.cpp
 * @brief Tests for preset validation and initialization
 */

#include "test_utils.h"
#include "krisha_dsp.h"

TEST(preset_init_flat) {
    krisha_preset_t preset;
    krisha_dsp_preset_init_flat(&preset);

    // Check preset is initialized
    ASSERT_EQ(preset.num_bands, KRISHA_MAX_BANDS);
    ASSERT_EQ(preset.preamp_db, 0.0f);
    ASSERT(!preset.limiter_enabled); // Flat preset has limiter disabled for transparency

    // Check all bands are disabled and at 0dB
    for (uint32_t i = 0; i < KRISHA_MAX_BANDS; i++) {
        ASSERT_EQ(preset.bands[i].gain_db, 0.0f);
        ASSERT_EQ(preset.bands[i].enabled, false);
    }

    PASS();
}

TEST(preset_validate_valid) {
    krisha_preset_t preset;
    krisha_dsp_preset_init_flat(&preset);

    // Valid preset should pass
    ASSERT_EQ(krisha_dsp_preset_validate(&preset), KRISHA_OK);

    PASS();
}

TEST(preset_validate_invalid_frequency) {
    krisha_preset_t preset;
    krisha_dsp_preset_init_flat(&preset);

    // Invalid frequency (too low)
    preset.bands[0].frequency_hz = 10.0f; // Below 20 Hz
    ASSERT_EQ(krisha_dsp_preset_validate(&preset), KRISHA_ERROR_INVALID_PARAM);

    // Invalid frequency (too high)
    preset.bands[0].frequency_hz = 25000.0f; // Above 20 kHz
    ASSERT_EQ(krisha_dsp_preset_validate(&preset), KRISHA_ERROR_INVALID_PARAM);

    PASS();
}

TEST(preset_validate_invalid_gain) {
    krisha_preset_t preset;
    krisha_dsp_preset_init_flat(&preset);

    // Invalid gain (too negative)
    preset.bands[0].gain_db = -45.0f; // Below -36 dB
    ASSERT_EQ(krisha_dsp_preset_validate(&preset), KRISHA_ERROR_INVALID_PARAM);

    // Invalid gain (too positive)
    preset.bands[0].gain_db = 45.0f; // Above +36 dB
    ASSERT_EQ(krisha_dsp_preset_validate(&preset), KRISHA_ERROR_INVALID_PARAM);

    PASS();
}

TEST(preset_validate_invalid_q) {
    krisha_preset_t preset;
    krisha_dsp_preset_init_flat(&preset);

    // Invalid Q (too low)
    preset.bands[0].q_factor = 0.005f; // Below 0.01
    ASSERT_EQ(krisha_dsp_preset_validate(&preset), KRISHA_ERROR_INVALID_PARAM);

    // Invalid Q (too high)
    preset.bands[0].q_factor = 150.0f; // Above 100.0
    ASSERT_EQ(krisha_dsp_preset_validate(&preset), KRISHA_ERROR_INVALID_PARAM);

    PASS();
}
