// Copyright (C) Radioform / Original Authors
// Modified by Shankar (2026) for the KRISHA Architecture. Renamed namespaces and variables.
// Licensed under the GNU GPLv3.

/**
 * @file engine.cpp
 * @brief Main DSP engine implementation
 *
 * Implements the public C API defined in krisha_dsp.h and krisha_universal.h.
 * The engine is self-contained and does not require external DSP libraries.
 */

#include "krisha_dsp.h"
#include "krisha_universal.h"
#include "biquad.h"
#include "smoothing.h"
#include "limiter.h"
#include "dc_blocker.h"
#include "cpu_util.h"

#include <cstring>
#include <cmath>
#include <atomic>
#include <array>
#include <chrono>

using namespace krisha;

// ============================================================================
// Engine Internal Structure
// ============================================================================

struct krisha_dsp_engine {
    // Sample rate
    uint32_t sample_rate;

    // EQ bands (each biquad handles stereo)
    std::array<Biquad, KRISHA_MAX_BANDS> bands;
    uint32_t num_active_bands;

    // Current preset configuration
    krisha_preset_t current_preset;

    // Precomputed effective active bands (0dB bypassed filters are excluded)
    std::array<uint32_t, KRISHA_MAX_BANDS> active_band_indices;
    uint32_t num_effective_bands;

    // Parameter smoothing for independent channels
    ParameterSmoother preamp_left_smoother;
    ParameterSmoother preamp_right_smoother;

    // Coefficient interpolation duration in samples (~10ms)
    int coeff_transition_samples;

    // Limiter
    SoftLimiter limiter;
    bool limiter_enabled;

    // DC Blocker (prevents DC offset buildup)
    StereoDCBlocker dc_blocker;

    // Bypass (atomic for lock-free realtime control)
    std::atomic<bool> bypass;

    // Statistics
    std::atomic<uint64_t> frames_processed;
    std::atomic<uint32_t> underrun_count;
    std::atomic<float> cpu_load_percent;  // CPU load as percentage (0-100)
    std::atomic<float> peak_left;         // Peak level left channel (linear, 0-1+)
    std::atomic<float> peak_right;        // Peak level right channel (linear, 0-1+)

    // Helper to precompute effective bands (for 0dB bypass optimization)
    void update_effective_bands() {
        num_effective_bands = 0;
        for (uint32_t i = 0; i < current_preset.num_bands; i++) {
            const auto& band = current_preset.bands[i];
            if (band.enabled) {
                bool is_gain_filter = (band.type == KRISHA_FILTER_PEAK ||
                                       band.type == KRISHA_FILTER_LOW_SHELF ||
                                       band.type == KRISHA_FILTER_HIGH_SHELF);
                // Bypass mathematically when gain is 0dB, provided we are not in the middle of coefficient smoothing
                if (is_gain_filter && std::abs(band.gain_db) < 1e-5f && !bands[i].isTransitioning()) {
                    continue;
                }
                active_band_indices[num_effective_bands++] = i;
            }
        }
    }

    // Constructor
    krisha_dsp_engine(uint32_t sr)
        : sample_rate(sr)
        , num_active_bands(0)
        , num_effective_bands(0)
        , limiter_enabled(true)
        , bypass(false)
        , frames_processed(0)
        , underrun_count(0)
        , cpu_load_percent(0.0f)
        , peak_left(0.0f)
        , peak_right(0.0f)
    {
        // Enable denormal suppression for performance
        enable_denormal_suppression();

        // Initialize with flat preset
        krisha_dsp_preset_init_flat(&current_preset);

        // Initialize all biquads
        for (auto& bq : bands) {
            bq.init();
        }

        // Initialize smoothers
        preamp_left_smoother.init(static_cast<float>(sample_rate), 10.0f); // 10ms ramp
        preamp_left_smoother.setValue(1.0f); // 0dB = gain of 1.0

        preamp_right_smoother.init(static_cast<float>(sample_rate), 10.0f);
        preamp_right_smoother.setValue(1.0f);

        // Coefficient transition duration: ~10ms worth of samples
        coeff_transition_samples = static_cast<int>(sample_rate * 0.01f);

        // Initialize limiter
        limiter.init(-0.1f); // -0.1 dB threshold

        // Initialize DC blocker (5Hz high-pass)
        dc_blocker.init(static_cast<float>(sample_rate), 5.0f);

        update_effective_bands();
    }
};

// ============================================================================
// Engine Lifecycle
// ============================================================================

krisha_dsp_engine_t* krisha_dsp_create(uint32_t sample_rate) {
    if (sample_rate < 8000 || sample_rate > 384000) {
        return nullptr; // Invalid sample rate
    }

    try {
        return new krisha_dsp_engine(sample_rate);
    } catch (...) {
        return nullptr;
    }
}

void krisha_dsp_destroy(krisha_dsp_engine_t* engine) {
    if (engine) {
        delete engine;
    }
}

void krisha_dsp_reset(krisha_dsp_engine_t* engine) {
    if (!engine) return;

    // Reset all filter state
    for (auto& bq : engine->bands) {
        bq.reset();
    }

    // Reset DC blocker
    engine->dc_blocker.reset();

    // Reset statistics
    engine->frames_processed.store(0);
    engine->underrun_count.store(0);
}

krisha_error_t krisha_dsp_set_sample_rate(
    krisha_dsp_engine_t* engine,
    uint32_t sample_rate
) {
    if (!engine) return KRISHA_ERROR_NULL_POINTER;
    if (sample_rate < 8000 || sample_rate > 384000) {
        return KRISHA_ERROR_INVALID_PARAM;
    }

    engine->sample_rate = sample_rate;

    // Reinitialize smoothers with new sample rate
    engine->preamp_left_smoother.init(static_cast<float>(sample_rate), 10.0f);
    engine->preamp_right_smoother.init(static_cast<float>(sample_rate), 10.0f);

    // Recalculate coefficient transition duration
    engine->coeff_transition_samples = static_cast<int>(sample_rate * 0.01f);

    // Reinitialize DC blocker with new sample rate
    engine->dc_blocker.init(static_cast<float>(sample_rate), 5.0f);

    // Recalculate filter coefficients
    return krisha_dsp_apply_preset(engine, &engine->current_preset);
}

// ============================================================================
// Audio Processing (REALTIME-SAFE)
// ============================================================================

void krisha_dsp_process_interleaved(
    krisha_dsp_engine_t* engine,
    const float* input,
    float* output,
    uint32_t num_frames
) {
    if (!engine || !input || !output || num_frames == 0) return;

    // Start CPU timing
    auto start_time = std::chrono::high_resolution_clock::now();

    // Check bypass
    if (engine->bypass.load(std::memory_order_relaxed)) {
        // Passthrough
        if (input != output) {
            std::memcpy(output, input, num_frames * 2 * sizeof(float));
        }

        // Decay peak meters so they don't hold stale values
        constexpr float peak_decay_time_ms = 300.0f;
        const float peak_decay_samples = peak_decay_time_ms * static_cast<float>(engine->sample_rate) / 1000.0f;
        const float peak_decay = std::exp(-static_cast<float>(num_frames) / peak_decay_samples);
        engine->peak_left.store(engine->peak_left.load(std::memory_order_relaxed) * peak_decay, std::memory_order_relaxed);
        engine->peak_right.store(engine->peak_right.load(std::memory_order_relaxed) * peak_decay, std::memory_order_relaxed);

        engine->frames_processed.fetch_add(num_frames, std::memory_order_relaxed);
        return;
    }

    // Peak detection
    float buffer_peak_left = 0.0f;
    float buffer_peak_right = 0.0f;

    // Check if preamp smoothers have settled (skip per-sample ticks when stable)
    const bool preamp_l_stable = engine->preamp_left_smoother.isStable();
    const bool preamp_r_stable = engine->preamp_right_smoother.isStable();
    const float preamp_l_gain_cached = preamp_l_stable ? engine->preamp_left_smoother.getCurrent() : 0.0f;
    const float preamp_r_gain_cached = preamp_r_stable ? engine->preamp_right_smoother.getCurrent() : 0.0f;

    // Process each frame
    for (uint32_t i = 0; i < num_frames; i++) {
        // Deinterleave
        float left = input[i * 2];
        float right = input[i * 2 + 1];

        // Apply preamps independently
        const float gain_l = preamp_l_stable ? preamp_l_gain_cached : engine->preamp_left_smoother.next();
        const float gain_r = preamp_r_stable ? preamp_r_gain_cached : engine->preamp_right_smoother.next();
        left *= gain_l;
        right *= gain_r;

        // Process through effective EQ bands (0dB bypassed filters are skipped)
        for (uint32_t b = 0; b < engine->num_effective_bands; b++) {
            uint32_t band = engine->active_band_indices[b];
            engine->bands[band].processSample(left, right, &left, &right);
        }

        // Remove DC offset
        engine->dc_blocker.processStereo(left, right, &left, &right);

        // Apply limiter if enabled
        if (engine->limiter_enabled) {
            engine->limiter.processSampleStereo(&left, &right);
        }

        // Track peak levels
        buffer_peak_left = std::max(buffer_peak_left, std::abs(left));
        buffer_peak_right = std::max(buffer_peak_right, std::abs(right));

        // Interleave output
        output[i * 2] = left;
        output[i * 2 + 1] = right;
    }

    // Update peak meters
    constexpr float peak_decay_time_ms = 300.0f;
    const float peak_decay_samples = peak_decay_time_ms * static_cast<float>(engine->sample_rate) / 1000.0f;
    const float peak_decay = std::exp(-static_cast<float>(num_frames) / peak_decay_samples);

    float new_peak_left = std::max(buffer_peak_left, engine->peak_left.load(std::memory_order_relaxed) * peak_decay);
    float new_peak_right = std::max(buffer_peak_right, engine->peak_right.load(std::memory_order_relaxed) * peak_decay);

    engine->peak_left.store(new_peak_left, std::memory_order_relaxed);
    engine->peak_right.store(new_peak_right, std::memory_order_relaxed);

    // End CPU timing
    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end_time - start_time;
    double available_time = static_cast<double>(num_frames) / static_cast<double>(engine->sample_rate);
    float instant_load = static_cast<float>((elapsed.count() / available_time) * 100.0);

    constexpr float cpu_smooth_time_ms = 500.0f;
    const float cpu_smooth_samples = cpu_smooth_time_ms * static_cast<float>(engine->sample_rate) / 1000.0f;
    const float cpu_alpha = 1.0f - std::exp(-static_cast<float>(num_frames) / cpu_smooth_samples);
    float current_load = engine->cpu_load_percent.load(std::memory_order_relaxed);
    engine->cpu_load_percent.store(current_load + cpu_alpha * (instant_load - current_load), std::memory_order_relaxed);

    engine->frames_processed.fetch_add(num_frames, std::memory_order_relaxed);
}

void krisha_dsp_process_planar(
    krisha_dsp_engine_t* engine,
    const float* input_left,
    const float* input_right,
    float* output_left,
    float* output_right,
    uint32_t num_frames
) {
    if (!engine || !input_left || !input_right || !output_left || !output_right || num_frames == 0) {
        return;
    }

    // Start CPU timing
    auto start_time = std::chrono::high_resolution_clock::now();

    // Check bypass
    if (engine->bypass.load(std::memory_order_relaxed)) {
        if (input_left != output_left) {
            std::memcpy(output_left, input_left, num_frames * sizeof(float));
        }
        if (input_right != output_right) {
            std::memcpy(output_right, input_right, num_frames * sizeof(float));
        }

        constexpr float peak_decay_time_ms = 300.0f;
        const float peak_decay_samples = peak_decay_time_ms * static_cast<float>(engine->sample_rate) / 1000.0f;
        const float peak_decay = std::exp(-static_cast<float>(num_frames) / peak_decay_samples);
        engine->peak_left.store(engine->peak_left.load(std::memory_order_relaxed) * peak_decay, std::memory_order_relaxed);
        engine->peak_right.store(engine->peak_right.load(std::memory_order_relaxed) * peak_decay, std::memory_order_relaxed);

        engine->frames_processed.fetch_add(num_frames, std::memory_order_relaxed);
        return;
    }

    if (input_left != output_left) {
        std::memcpy(output_left, input_left, num_frames * sizeof(float));
    }
    if (input_right != output_right) {
        std::memcpy(output_right, input_right, num_frames * sizeof(float));
    }

    // Apply preamps independently
    const bool preamp_l_stable = engine->preamp_left_smoother.isStable();
    if (preamp_l_stable) {
        const float gain = engine->preamp_left_smoother.getCurrent();
        for (uint32_t i = 0; i < num_frames; i++) {
            output_left[i] *= gain;
        }
    } else {
        for (uint32_t i = 0; i < num_frames; i++) {
            output_left[i] *= engine->preamp_left_smoother.next();
        }
    }

    const bool preamp_r_stable = engine->preamp_right_smoother.isStable();
    if (preamp_r_stable) {
        const float gain = engine->preamp_right_smoother.getCurrent();
        for (uint32_t i = 0; i < num_frames; i++) {
            output_right[i] *= gain;
        }
    } else {
        for (uint32_t i = 0; i < num_frames; i++) {
            output_right[i] *= engine->preamp_right_smoother.next();
        }
    }

    // Process through effective EQ bands (0dB bypassed filters are skipped)
    for (uint32_t b = 0; b < engine->num_effective_bands; b++) {
        uint32_t band = engine->active_band_indices[b];
        engine->bands[band].processBuffer(
            output_left, output_right,
            output_left, output_right,
            num_frames
        );
    }

    // Remove DC offset
    engine->dc_blocker.processBuffer(
        output_left, output_right,
        output_left, output_right,
        num_frames
    );

    // Apply limiter if enabled
    if (engine->limiter_enabled) {
        engine->limiter.processBuffer(output_left, output_right, num_frames);
    }

    // Peak detection
    float buffer_peak_left = 0.0f;
    float buffer_peak_right = 0.0f;
    for (uint32_t i = 0; i < num_frames; i++) {
        buffer_peak_left = std::max(buffer_peak_left, std::abs(output_left[i]));
        buffer_peak_right = std::max(buffer_peak_right, std::abs(output_right[i]));
    }

    constexpr float peak_decay_time_ms = 300.0f;
    const float peak_decay_samples = peak_decay_time_ms * static_cast<float>(engine->sample_rate) / 1000.0f;
    const float peak_decay = std::exp(-static_cast<float>(num_frames) / peak_decay_samples);

    engine->peak_left.store(std::max(buffer_peak_left, engine->peak_left.load(std::memory_order_relaxed) * peak_decay), std::memory_order_relaxed);
    engine->peak_right.store(std::max(buffer_peak_right, engine->peak_right.load(std::memory_order_relaxed) * peak_decay), std::memory_order_relaxed);

    // End CPU timing
    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end_time - start_time;
    double available_time = static_cast<double>(num_frames) / static_cast<double>(engine->sample_rate);
    float instant_load = static_cast<float>((elapsed.count() / available_time) * 100.0);

    constexpr float cpu_smooth_time_ms = 500.0f;
    const float cpu_smooth_samples = cpu_smooth_time_ms * static_cast<float>(engine->sample_rate) / 1000.0f;
    const float cpu_alpha = 1.0f - std::exp(-static_cast<float>(num_frames) / cpu_smooth_samples);
    float current_load = engine->cpu_load_percent.load(std::memory_order_relaxed);
    engine->cpu_load_percent.store(current_load + cpu_alpha * (instant_load - current_load), std::memory_order_relaxed);

    engine->frames_processed.fetch_add(num_frames, std::memory_order_relaxed);
}

// ============================================================================
// Preset Management (NOT realtime-safe)
// ============================================================================

krisha_error_t krisha_dsp_apply_preset(
    krisha_dsp_engine_t* engine,
    const krisha_preset_t* preset
) {
    if (!engine || !preset) {
        return KRISHA_ERROR_NULL_POINTER;
    }

    // Validate preset
    krisha_error_t err = krisha_dsp_preset_validate(preset);
    if (err != KRISHA_OK) {
        return err;
    }

    // Copy preset
    std::memcpy(&engine->current_preset, preset, sizeof(krisha_preset_t));
    engine->num_active_bands = preset->num_bands;

    // Update filter coefficients and smoothers for each band
    for (uint32_t i = 0; i < preset->num_bands; i++) {
        const krisha_band_t& band = preset->bands[i];

        if (band.enabled) {
            engine->bands[i].setCoeffs(band, static_cast<float>(engine->sample_rate));
        } else {
            engine->bands[i].setCoeffsFlat();
        }
    }

    // Precalculate effective active bands
    engine->update_effective_bands();

    // Update preamps
    float preamp_l_gain = db_to_gain(preset->preamp_left_db);
    float preamp_r_gain = db_to_gain(preset->preamp_right_db);
    engine->preamp_left_smoother.setTarget(preamp_l_gain);
    engine->preamp_right_smoother.setTarget(preamp_r_gain);

    // Update limiter
    engine->limiter_enabled = preset->limiter_enabled;
    if (preset->limiter_enabled) {
        engine->limiter.setThreshold(preset->limiter_threshold_db);
    }

    return KRISHA_OK;
}

krisha_error_t krisha_dsp_get_preset(
    krisha_dsp_engine_t* engine,
    krisha_preset_t* preset
) {
    if (!engine || !preset) {
        return KRISHA_ERROR_NULL_POINTER;
    }

    std::memcpy(preset, &engine->current_preset, sizeof(krisha_preset_t));
    return KRISHA_OK;
}

// ============================================================================
// Realtime Parameter Updates (Lock-free & Realtime-safe)
// ============================================================================

void krisha_dsp_set_bypass(krisha_dsp_engine_t* engine, bool bypass) {
    if (engine) {
        engine->bypass.store(bypass, std::memory_order_relaxed);
    }
}

bool krisha_dsp_get_bypass(const krisha_dsp_engine_t* engine) {
    return engine ? engine->bypass.load(std::memory_order_relaxed) : true;
}

void krisha_dsp_update_band_gain(
    krisha_dsp_engine_t* engine,
    uint32_t band_index,
    float gain_db
) {
    if (!engine || band_index >= engine->num_active_bands) return;

    gain_db = std::max(-12.0f, std::min(12.0f, gain_db));
    engine->current_preset.bands[band_index].gain_db = gain_db;

    const krisha_band_t& band = engine->current_preset.bands[band_index];
    engine->bands[band_index].setCoeffsSmooth(
        band, static_cast<float>(engine->sample_rate),
        engine->coeff_transition_samples
    );

    engine->update_effective_bands();
}

void krisha_dsp_update_preamp(
    krisha_dsp_engine_t* engine,
    float gain_db
) {
    if (!engine) return;

    gain_db = std::max(-12.0f, std::min(12.0f, gain_db));
    engine->current_preset.preamp_db = gain_db;
    engine->current_preset.preamp_left_db = gain_db;
    engine->current_preset.preamp_right_db = gain_db;

    float target_gain = db_to_gain(gain_db);
    engine->preamp_left_smoother.setTarget(target_gain);
    engine->preamp_right_smoother.setTarget(target_gain);
}

void krisha_dsp_update_preamp_left(
    krisha_dsp_engine_t* engine,
    float gain_db
) {
    if (!engine) return;

    gain_db = std::max(-12.0f, std::min(12.0f, gain_db));
    engine->current_preset.preamp_left_db = gain_db;

    float target_gain = db_to_gain(gain_db);
    engine->preamp_left_smoother.setTarget(target_gain);
}

void krisha_dsp_update_preamp_right(
    krisha_dsp_engine_t* engine,
    float gain_db
) {
    if (!engine) return;

    gain_db = std::max(-12.0f, std::min(12.0f, gain_db));
    engine->current_preset.preamp_right_db = gain_db;

    float target_gain = db_to_gain(gain_db);
    engine->preamp_right_smoother.setTarget(target_gain);
}

void krisha_dsp_update_band_frequency(
    krisha_dsp_engine_t* engine,
    uint32_t band_index,
    float frequency_hz
) {
    if (!engine || band_index >= engine->num_active_bands) return;

    frequency_hz = std::max(20.0f, std::min(20000.0f, frequency_hz));
    engine->current_preset.bands[band_index].frequency_hz = frequency_hz;

    const krisha_band_t& band = engine->current_preset.bands[band_index];
    engine->bands[band_index].setCoeffsSmooth(
        band, static_cast<float>(engine->sample_rate),
        engine->coeff_transition_samples
    );

    engine->update_effective_bands();
}

void krisha_dsp_update_band_q(
    krisha_dsp_engine_t* engine,
    uint32_t band_index,
    float q_factor
) {
    if (!engine || band_index >= engine->num_active_bands) return;

    q_factor = std::max(0.1f, std::min(10.0f, q_factor));
    engine->current_preset.bands[band_index].q_factor = q_factor;

    const krisha_band_t& band = engine->current_preset.bands[band_index];
    engine->bands[band_index].setCoeffsSmooth(
        band, static_cast<float>(engine->sample_rate),
        engine->coeff_transition_samples
    );

    engine->update_effective_bands();
}

// ============================================================================
// Diagnostics & Live Graphing
// ============================================================================

void krisha_dsp_get_stats(
    const krisha_dsp_engine_t* engine,
    krisha_stats_t* stats
) {
    if (!engine || !stats) return;

    stats->frames_processed = engine->frames_processed.load(std::memory_order_relaxed);
    stats->underrun_count = engine->underrun_count.load(std::memory_order_relaxed);
    stats->cpu_load_percent = engine->cpu_load_percent.load(std::memory_order_relaxed);
    stats->bypass_active = engine->bypass.load(std::memory_order_relaxed);
    stats->sample_rate = engine->sample_rate;

    float peak_left_linear = engine->peak_left.load(std::memory_order_relaxed);
    float peak_right_linear = engine->peak_right.load(std::memory_order_relaxed);

    constexpr float min_db = -120.0f;
    stats->peak_left_db = peak_left_linear > 0.0f
        ? std::max(20.0f * std::log10(peak_left_linear), min_db)
        : min_db;
    stats->peak_right_db = peak_right_linear > 0.0f
        ? std::max(20.0f * std::log10(peak_right_linear), min_db)
        : min_db;
}

const char* krisha_dsp_get_version(void) {
    return "1.0.0";
}

float krisha_dsp_get_magnitude_at_frequency(
    const krisha_dsp_engine_t* engine,
    float frequency_hz,
    bool left_channel
) {
    if (!engine) return 0.0f;

    float preamp_db = left_channel ? engine->current_preset.preamp_left_db : engine->current_preset.preamp_right_db;
    float total_gain_db = preamp_db;

    constexpr double PI_VAL = 3.14159265358979323846;
    double w0 = 2.0 * PI_VAL * static_cast<double>(frequency_hz) / static_cast<double>(engine->sample_rate);
    double cos_w = std::cos(w0);
    double sin_w = std::sin(w0);
    double cos_2w = std::cos(2.0 * w0);
    double sin_2w = std::sin(2.0 * w0);

    for (uint32_t i = 0; i < engine->current_preset.num_bands; i++) {
        const auto& band = engine->current_preset.bands[i];
        if (band.enabled) {
            BiquadCoeffs c = engine->bands[i].getCoeffs();

            double num_r = c.b0 + c.b1 * cos_w + c.b2 * cos_2w;
            double num_i = -(c.b1 * sin_w + c.b2 * sin_2w);
            double den_r = 1.0 + c.a1 * cos_w + c.a2 * cos_2w;
            double den_i = -(c.a1 * sin_w + c.a2 * sin_2w);

            double num_mag_sq = num_r * num_r + num_i * num_i;
            double den_mag_sq = den_r * den_r + den_i * den_i;

            if (den_mag_sq > 1e-12) {
                double mag = std::sqrt(num_mag_sq / den_mag_sq);
                if (mag > 1e-6) {
                    total_gain_db += static_cast<float>(20.0 * std::log10(mag));
                }
            }
        }
    }

    return total_gain_db;
}

// ============================================================================
// Performance Optimizations
// ============================================================================

void krisha_dsp_enable_denormal_suppression(void) {
    enable_denormal_suppression();
}
