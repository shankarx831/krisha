/**
 * @file krisha_types.h
 * @brief Type definitions for Krisha DSP library
 *
 * This file contains POD (Plain Old Data) types that are safe to use across
 * C, C++, Objective-C, and Swift boundaries. No templates, no C++ classes,
 * no virtual functions.
 */

#ifndef KRISHA_TYPES_H
#define KRISHA_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximum number of EQ bands supported
 */
#define KRISHA_MAX_BANDS 10

/**
 * @brief Filter types for EQ bands
 */
typedef enum {
    KRISHA_FILTER_PEAK = 0,      // Parametric peak/dip (bell curve)
    KRISHA_FILTER_LOW_SHELF,     // Low shelf (boost/cut bass)
    KRISHA_FILTER_HIGH_SHELF,    // High shelf (boost/cut treble)
    KRISHA_FILTER_LOW_PASS,      // Low-pass filter
    KRISHA_FILTER_HIGH_PASS,     // High-pass filter
    KRISHA_FILTER_NOTCH,         // Notch filter (narrow rejection)
    KRISHA_FILTER_BAND_PASS      // Band-pass filter
} krisha_filter_type_t;

/**
 * @brief Configuration for a single EQ band
 */
typedef struct {
    float frequency_hz;             // Center frequency in Hz (20 - 20000)
    float gain_db;                  // Gain in dB (-12.0 to +12.0)
    float q_factor;                 // Q factor (0.1 to 10.0, default 1.0)
    krisha_filter_type_t type;   // Filter type
    bool enabled;                   // Band enabled/bypassed
} krisha_band_t;

/**
 * @brief Complete EQ preset configuration
 */
typedef struct {
    krisha_band_t bands[KRISHA_MAX_BANDS];  // Array of EQ bands
    uint32_t num_bands;             // Number of active bands (1-10)
    float preamp_db;                // Global preamp gain (-12.0 to +12.0)
    float preamp_left_db;           // Independent Left preamp gain (-12.0 to +12.0)
    float preamp_right_db;          // Independent Right preamp gain (-12.0 to +12.0)
    bool limiter_enabled;           // Enable soft limiter after EQ
    float limiter_threshold_db;     // Limiter threshold (-6.0 to 0.0)
    char name[64];                  // Preset name (null-terminated)
} krisha_preset_t;

/**
 * @brief Error codes returned by DSP functions
 */
typedef enum {
    KRISHA_OK = 0,               // Success
    KRISHA_ERROR_INVALID_PARAM,  // Invalid parameter value
    KRISHA_ERROR_NULL_POINTER,   // Null pointer passed
    KRISHA_ERROR_OUT_OF_MEMORY,  // Memory allocation failed
    KRISHA_ERROR_INVALID_STATE,  // Operation invalid in current state
    KRISHA_ERROR_UNSUPPORTED     // Feature not supported
} krisha_error_t;

/**
 * @brief DSP engine statistics (for diagnostics)
 */
typedef struct {
    uint64_t frames_processed;      // Total frames processed
    uint32_t underrun_count;        // Number of buffer underruns detected
    float cpu_load_percent;         // Estimated CPU load (0.0 - 100.0)
    bool bypass_active;             // Currently in bypass mode
    uint32_t sample_rate;           // Current sample rate
    float peak_left_db;             // Current peak level left channel (dBFS)
    float peak_right_db;            // Current peak level right channel (dBFS)
} krisha_stats_t;

#ifdef __cplusplus
}
#endif

#endif // KRISHA_TYPES_H
