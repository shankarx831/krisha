// Copyright (C) Radioform / Original Authors
// Modified by Shankar (2026) for the KRISHA Architecture. Renamed namespaces and variables.
// Licensed under the GNU GPLv3.

/**
 * @file krisha_universal.h
 * @brief Public Cross-Platform C API for Krisha Universal DSP Engine
 *
 * This is the unified universal API for all targets (macOS, Windows, Linux, Android).
 */

#ifndef KRISHA_UNIVERSAL_H
#define KRISHA_UNIVERSAL_H

#include "krisha_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque handle to DSP engine instance
 */
typedef struct krisha_dsp_engine krisha_dsp_engine_t;

// ============================================================================
// Realtime Parameter Updates (Lock-free & Realtime-safe)
// ============================================================================

/**
 * @brief Update Left preamp gain in realtime
 *
 * @param engine Engine instance (must not be NULL)
 * @param gain_db New Left preamp gain in dB (-12.0 to +12.0)
 */
void krisha_dsp_update_preamp_left(
    krisha_dsp_engine_t* engine,
    float gain_db
);

/**
 * @brief Update Right preamp gain in realtime
 *
 * @param engine Engine instance (must not be NULL)
 * @param gain_db New Right preamp gain in dB (-12.0 to +12.0)
 */
void krisha_dsp_update_preamp_right(
    krisha_dsp_engine_t* engine,
    float gain_db
);

// ============================================================================
// Live Graphing & Complex Analysis (Thread-safe)
// ============================================================================

/**
 * @brief Compute combined magnitude response at a specific frequency
 *
 * Calculates the exact combined response of the preamp and active EQ biquads.
 *
 * @param engine Engine instance (must not be NULL)
 * @param frequency_hz Frequency in Hz (e.g., 20.0 to 20000.0)
 * @param left_channel True for Left channel response, False for Right channel
 * @return Calculated gain in decibels (dB)
 */
float krisha_dsp_get_magnitude_at_frequency(
    const krisha_dsp_engine_t* engine,
    float frequency_hz,
    bool left_channel
);

// ============================================================================
// AutoEq Preset Parser (Cross-platform)
// ============================================================================

/**
 * @brief Parse a standard AutoEq ParametricEQ.txt file into a preset struct
 *
 * Parses lines matching:
 * - Preamp: -X.Y dB
 * - Filter N: ON PK Fc X.Y Hz Gain A.B dB Q Q.Q
 *
 * @param text The complete content of the AutoEq file (null-terminated)
 * @param preset Pointer to preset struct to fill (must not be NULL)
 * @return KRISHA_OK on success, error code otherwise
 */
krisha_error_t krisha_preset_parse_autoeq(
    const char* text,
    krisha_preset_t* preset
);

/**
 * @brief Get the Harman Target Curve baseline magnitude in dB at a specific frequency
 *
 * Calculates the static, precalculated mathematical model approximating the
 * industry-standard Harman Target Curve as a visual reference line.
 *
 * @param frequency_hz Frequency in Hz
 * @return Calculated baseline target gain in decibels (dB)
 */
float krisha_dsp_get_harman_target_at_frequency(
    float frequency_hz
);

#ifdef __cplusplus
}
#endif

#endif // KRISHA_UNIVERSAL_H
