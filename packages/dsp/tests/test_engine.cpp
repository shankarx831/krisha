/**
 * @file test_engine.cpp
 * @brief Tests for full DSP engine
 */

#include "test_utils.h"
#include "radioform_dsp.h"

using namespace dsp_test;

TEST(engine_create_destroy) {
    auto* engine = radioform_dsp_create(48000);
    ASSERT(engine != nullptr);

    radioform_dsp_destroy(engine);

    PASS();
}

TEST(engine_invalid_sample_rate) {
    // Too low
    auto* engine1 = radioform_dsp_create(1000);
    ASSERT(engine1 == nullptr);

    // Too high
    auto* engine2 = radioform_dsp_create(500000);
    ASSERT(engine2 == nullptr);

    PASS();
}

TEST(engine_bypass_is_bit_perfect) {
    auto* engine = radioform_dsp_create(48000);
    ASSERT(engine != nullptr);

    // Enable bypass
    radioform_dsp_set_bypass(engine, true);
    ASSERT_EQ(radioform_dsp_get_bypass(engine), true);

    // Generate test signal
    auto input_left = generate_sine(1000, 1000.0f, 48000.0f);
    auto input_right = input_left;
    std::vector<float> output_left(input_left.size());
    std::vector<float> output_right(input_right.size());

    // Process in bypass mode (planar)
    radioform_dsp_process_planar(
        engine,
        input_left.data(), input_right.data(),
        output_left.data(), output_right.data(),
        input_left.size()
    );

    // Output should be identical to input (bit-perfect)
    ASSERT(signals_identical(input_left, output_left));
    ASSERT(signals_identical(input_right, output_right));

    radioform_dsp_destroy(engine);
    PASS();
}

TEST(engine_bypass_interleaved_is_bit_perfect) {
    auto* engine = radioform_dsp_create(48000);
    ASSERT(engine != nullptr);

    radioform_dsp_set_bypass(engine, true);

    // Generate interleaved test signal
    std::vector<float> input(2000); // 1000 frames stereo
    for (size_t i = 0; i < 1000; i++) {
        float sample = std::sin(2.0f * M_PI * 1000.0f * i / 48000.0f);
        input[i * 2] = sample;     // Left
        input[i * 2 + 1] = sample; // Right
    }

    std::vector<float> output(input.size());

    // Process
    radioform_dsp_process_interleaved(
        engine,
        input.data(),
        output.data(),
        1000 // num_frames
    );

    // Output should be identical
    ASSERT(signals_identical(input, output));

    radioform_dsp_destroy(engine);
    PASS();
}

TEST(engine_apply_flat_preset) {
    auto* engine = radioform_dsp_create(48000);
    ASSERT(engine != nullptr);

    radioform_preset_t preset;
    radioform_dsp_preset_init_flat(&preset);

    auto result = radioform_dsp_apply_preset(engine, &preset);
    ASSERT_EQ(result, RADIOFORM_OK);

    radioform_dsp_destroy(engine);
    PASS();
}

TEST(engine_process_with_flat_preset) {
    auto* engine = radioform_dsp_create(48000);
    ASSERT(engine != nullptr);

    // Apply flat preset
    radioform_preset_t preset;
    radioform_dsp_preset_init_flat(&preset);
    radioform_dsp_apply_preset(engine, &preset);

    // Disable bypass
    radioform_dsp_set_bypass(engine, false);

    // Generate test signal
    auto input_left = generate_sine(4800, 1000.0f, 48000.0f);
    auto input_right = input_left;
    std::vector<float> output_left(input_left.size());
    std::vector<float> output_right(input_right.size());

    // Process
    radioform_dsp_process_planar(
        engine,
        input_left.data(), input_right.data(),
        output_left.data(), output_right.data(),
        input_left.size()
    );

    // With flat preset, output should be very similar to input
    // (might not be bit-perfect due to smoothing, but should be close)
    float input_rms = measure_rms(input_left);
    float output_rms = measure_rms(output_left);

    ASSERT_NEAR(output_rms, input_rms, 0.1f);

    radioform_dsp_destroy(engine);
    PASS();
}

TEST(engine_peak_filter_boosts_signal) {
    auto* engine = radioform_dsp_create(48000);
    ASSERT(engine != nullptr);

    // Create preset with +6dB peak at 1kHz
    radioform_preset_t preset;
    radioform_dsp_preset_init_flat(&preset);

    preset.num_bands = 1;
    preset.bands[0].enabled = true;
    preset.bands[0].frequency_hz = 1000.0f;
    preset.bands[0].gain_db = 6.0f;
    preset.bands[0].q_factor = 2.0f;
    preset.bands[0].type = RADIOFORM_FILTER_PEAK;

    radioform_dsp_apply_preset(engine, &preset);
    radioform_dsp_set_bypass(engine, false);

    // Generate 1kHz test signal
    auto input_left = generate_sine(4800, 1000.0f, 48000.0f);
    auto input_right = input_left;
    std::vector<float> output_left(input_left.size());
    std::vector<float> output_right(input_right.size());

    // Process
    radioform_dsp_process_planar(
        engine,
        input_left.data(), input_right.data(),
        output_left.data(), output_right.data(),
        input_left.size()
    );

    float input_rms = measure_rms(input_left);
    float output_rms = measure_rms(output_left);

    // Output should be louder (boosted)
    ASSERT(output_rms > input_rms * 1.5f); // At least +3.5dB

    radioform_dsp_destroy(engine);
    PASS();
}

TEST(engine_limiter_prevents_clipping) {
    auto* engine = radioform_dsp_create(48000);
    ASSERT(engine != nullptr);

    // Create preset with significant boost
    radioform_preset_t preset;
    radioform_dsp_preset_init_flat(&preset);

    preset.preamp_db = 12.0f; // +12dB preamp (will clip without limiter)
    preset.limiter_enabled = true;
    preset.limiter_threshold_db = -0.1f;

    radioform_dsp_apply_preset(engine, &preset);
    radioform_dsp_set_bypass(engine, false);

    // Generate loud test signal
    auto input_left = generate_sine(4800, 1000.0f, 48000.0f);
    auto input_right = input_left;
    std::vector<float> output_left(input_left.size());
    std::vector<float> output_right(input_right.size());

    // Process
    radioform_dsp_process_planar(
        engine,
        input_left.data(), input_right.data(),
        output_left.data(), output_right.data(),
        input_left.size()
    );

    // Peak should not exceed threshold
    float peak = measure_peak(output_left);
    ASSERT(peak <= 1.0f); // Should not clip

    radioform_dsp_destroy(engine);
    PASS();
}

TEST(engine_update_band_gain_realtime) {
    auto* engine = radioform_dsp_create(48000);
    ASSERT(engine != nullptr);

    // Create preset with one band
    radioform_preset_t preset;
    radioform_dsp_preset_init_flat(&preset);

    preset.num_bands = 1;
    preset.bands[0].enabled = true;
    preset.bands[0].frequency_hz = 1000.0f;
    preset.bands[0].gain_db = 0.0f; // Start at 0dB
    preset.bands[0].q_factor = 1.0f;
    preset.bands[0].type = RADIOFORM_FILTER_PEAK;

    radioform_dsp_apply_preset(engine, &preset);
    radioform_dsp_set_bypass(engine, false);

    // Update gain in realtime
    radioform_dsp_update_band_gain(engine, 0, 6.0f); // Change to +6dB

    // Generate test signal
    auto input_left = generate_sine(4800, 1000.0f, 48000.0f);
    auto input_right = input_left;
    std::vector<float> output_left(input_left.size());
    std::vector<float> output_right(input_right.size());

    // Process
    radioform_dsp_process_planar(
        engine,
        input_left.data(), input_right.data(),
        output_left.data(), output_right.data(),
        input_left.size()
    );

    // Check output is boosted
    float input_rms = measure_rms(input_left);
    float output_rms = measure_rms(output_left);

    ASSERT(output_rms > input_rms * 1.3f);

    radioform_dsp_destroy(engine);
    PASS();
}

TEST(engine_statistics_tracking) {
    auto* engine = radioform_dsp_create(48000);
    ASSERT(engine != nullptr);

    radioform_stats_t stats;
    radioform_dsp_get_stats(engine, &stats);

    // Initial stats
    ASSERT_EQ(stats.frames_processed, 0);
    ASSERT_EQ(stats.sample_rate, 48000);
    ASSERT_EQ(stats.bypass_active, false);

    // Process some audio
    auto input_left = generate_sine(1000, 1000.0f, 48000.0f);
    auto input_right = input_left;
    std::vector<float> output_left(input_left.size());
    std::vector<float> output_right(input_right.size());

    radioform_dsp_process_planar(
        engine,
        input_left.data(), input_right.data(),
        output_left.data(), output_right.data(),
        input_left.size()
    );

    // Check stats updated
    radioform_dsp_get_stats(engine, &stats);
    ASSERT_EQ(stats.frames_processed, 1000);

    radioform_dsp_destroy(engine);
    PASS();
}

TEST(engine_reset_clears_state) {
    auto* engine = radioform_dsp_create(48000);
    ASSERT(engine != nullptr);

    // Apply preset with filter
    radioform_preset_t preset;
    radioform_dsp_preset_init_flat(&preset);
    preset.num_bands = 1;
    preset.bands[0].enabled = true;
    preset.bands[0].frequency_hz = 1000.0f;
    preset.bands[0].gain_db = 6.0f;
    preset.bands[0].q_factor = 2.0f;
    preset.bands[0].type = RADIOFORM_FILTER_PEAK;

    radioform_dsp_apply_preset(engine, &preset);
    radioform_dsp_set_bypass(engine, false);

    // Process some audio to build up filter state
    auto signal_left = generate_sine(1000, 1000.0f, 48000.0f);
    auto signal_right = signal_left;
    std::vector<float> output_left(signal_left.size());
    std::vector<float> output_right(signal_right.size());

    radioform_dsp_process_planar(
        engine,
        signal_left.data(), signal_right.data(),
        output_left.data(), output_right.data(),
        signal_left.size()
    );

    // Call reset to clear stats and history
    radioform_dsp_reset(engine);

    // Stats should be cleared
    radioform_stats_t stats;
    radioform_dsp_get_stats(engine, &stats);
    ASSERT_EQ(stats.frames_processed, 0);

    radioform_dsp_destroy(engine);
    PASS();
}

TEST(engine_independent_lr_preamp) {
    auto* engine = radioform_dsp_create(48000);
    ASSERT(engine != nullptr);

    radioform_preset_t preset;
    radioform_dsp_preset_init_flat(&preset);
    preset.preamp_left_db = 6.0f;   // +6dB Left (~2.0x gain)
    preset.preamp_right_db = -6.0f; // -6dB Right (~0.5x gain)
    
    radioform_dsp_apply_preset(engine, &preset);
    radioform_dsp_set_bypass(engine, false);

    // Generate input signal
    auto input_left = generate_sine(5000, 1000.0f, 48000.0f);
    auto input_right = input_left;
    std::vector<float> output_left(input_left.size());
    std::vector<float> output_right(input_right.size());

    // Process
    radioform_dsp_process_planar(
        engine,
        input_left.data(), input_right.data(),
        output_left.data(), output_right.data(),
        input_left.size()
    );

    // Slice last 1000 samples to let smoothers reach steady state
    std::vector<float> in_slice(input_left.end() - 1000, input_left.end());
    std::vector<float> out_l_slice(output_left.end() - 1000, output_left.end());
    std::vector<float> out_r_slice(output_right.end() - 1000, output_right.end());

    float in_rms = measure_rms(in_slice);
    float out_l_rms = measure_rms(out_l_slice);
    float out_r_rms = measure_rms(out_r_slice);

    // Left should be boosted, Right should be cut
    ASSERT(out_l_rms > in_rms * 1.8f);
    ASSERT(out_r_rms < in_rms * 0.6f);

    // Realtime update testing
    radioform_dsp_update_preamp_left(engine, -12.0f);
    radioform_dsp_update_preamp_right(engine, 12.0f);

    // Process another block to let smoothers catch up
    radioform_dsp_process_planar(
        engine,
        input_left.data(), input_right.data(),
        output_left.data(), output_right.data(),
        input_left.size()
    );

    // Slice last 1000 samples to measure steady state
    std::vector<float> out_l_slice2(output_left.end() - 1000, output_left.end());
    std::vector<float> out_r_slice2(output_right.end() - 1000, output_right.end());

    out_l_rms = measure_rms(out_l_slice2);
    out_r_rms = measure_rms(out_r_slice2);

    // Now Left should be cut, Right boosted
    ASSERT(out_l_rms < in_rms * 0.4f);
    ASSERT(out_r_rms > in_rms * 1.8f);

    radioform_dsp_destroy(engine);
    PASS();
}

TEST(engine_mathematical_bypass) {
    auto* engine = radioform_dsp_create(48000);
    ASSERT(engine != nullptr);

    // Preset with 5 bands all set to 0.0dB gain
    radioform_preset_t preset;
    radioform_dsp_preset_init_flat(&preset);
    preset.num_bands = 5;
    for (int i = 0; i < 5; i++) {
        preset.bands[i].enabled = true;
        preset.bands[i].type = RADIOFORM_FILTER_PEAK;
        preset.bands[i].gain_db = 0.0f; // Bypassed
        preset.bands[i].frequency_hz = 1000.0f * (i + 1);
        preset.bands[i].q_factor = 1.0f;
    }

    radioform_dsp_apply_preset(engine, &preset);
    radioform_dsp_set_bypass(engine, false);

    // Generate signal
    auto input = generate_sine(1000, 1000.0f, 48000.0f);
    std::vector<float> output_left(input.size());
    std::vector<float> output_right(input.size());

    // Process
    radioform_dsp_process_planar(
        engine,
        input.data(), input.data(),
        output_left.data(), output_right.data(),
        input.size()
    );

    float in_rms = measure_rms(input);
    float out_l_rms = measure_rms(output_left);

    // Output should be transparent because all bands are mathematically bypassed
    ASSERT_NEAR(out_l_rms, in_rms, 1e-3f);

    radioform_dsp_destroy(engine);
    PASS();
}

TEST(engine_magnitude_response) {
    auto* engine = radioform_dsp_create(48000);
    ASSERT(engine != nullptr);

    // Create a +6dB boost preset at 1kHz
    radioform_preset_t preset;
    radioform_dsp_preset_init_flat(&preset);
    preset.num_bands = 1;
    preset.bands[0].enabled = true;
    preset.bands[0].frequency_hz = 1000.0f;
    preset.bands[0].gain_db = 6.0f;
    preset.bands[0].q_factor = 1.0f;
    preset.bands[0].type = RADIOFORM_FILTER_PEAK;

    radioform_dsp_apply_preset(engine, &preset);

    // Query magnitude response
    float mag_at_1k = radioform_dsp_get_magnitude_at_frequency(engine, 1000.0f, true);
    float mag_at_10k = radioform_dsp_get_magnitude_at_frequency(engine, 10000.0f, true);

    // 1kHz should be boosted by approx 6dB
    ASSERT_NEAR(mag_at_1k, 6.0f, 0.5f);
    
    // 10kHz should be near 0dB (outside the filter bandwidth)
    ASSERT_NEAR(mag_at_10k, 0.0f, 0.5f);

    radioform_dsp_destroy(engine);
    PASS();
}
