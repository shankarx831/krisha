/**
 * @file radioform_universal.h
 * @brief Public Cross-Platform C API for Radioform Universal DSP Engine
 *
 * This is the unified universal API for all targets (macOS, Windows, Linux, Android).
 */

#ifndef RADIOFORM_UNIVERSAL_H
#define RADIOFORM_UNIVERSAL_H

#include "radioform_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque handle to DSP engine instance
 */
typedef struct radioform_dsp_engine radioform_dsp_engine_t;

// ============================================================================
// Realtime Parameter Updates (Lock-free & Realtime-safe)
// ============================================================================

/**
 * @brief Update Left preamp gain in realtime
 *
 * @param engine Engine instance (must not be NULL)
 * @param gain_db New Left preamp gain in dB (-12.0 to +12.0)
 */
void radioform_dsp_update_preamp_left(
    radioform_dsp_engine_t* engine,
    float gain_db
);

/**
 * @brief Update Right preamp gain in realtime
 *
 * @param engine Engine instance (must not be NULL)
 * @param gain_db New Right preamp gain in dB (-12.0 to +12.0)
 */
void radioform_dsp_update_preamp_right(
    radioform_dsp_engine_t* engine,
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
float radioform_dsp_get_magnitude_at_frequency(
    const radioform_dsp_engine_t* engine,
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
 * @return RADIOFORM_OK on success, error code otherwise
 */
radioform_error_t radioform_preset_parse_autoeq(
    const char* text,
    radioform_preset_t* preset
);

#ifdef __cplusplus
}
#endif

#endif // RADIOFORM_UNIVERSAL_H
