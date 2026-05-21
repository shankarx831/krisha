/**
 * @file test_parser.cpp
 * @brief Tests for the AutoEq ParametricEQ parser
 */

#include "test_utils.h"
#include "radioform_dsp.h"

TEST(parser_basic) {
    const char* autoeq_data = 
        "Preamp: -6.3 dB\n"
        "Filter 1: ON PK Fc 31.0 Hz Gain -3.4 dB Q 1.41\n"
        "Filter 2: ON PK Fc 62.0 Hz Gain -1.2 dB Q 1.41\n";

    radioform_preset_t preset;
    radioform_error_t err = radioform_preset_parse_autoeq(autoeq_data, &preset);

    ASSERT_EQ(err, RADIOFORM_OK);
    ASSERT_NEAR(preset.preamp_db, -6.3f, 1e-4);
    ASSERT_NEAR(preset.preamp_left_db, -6.3f, 1e-4);
    ASSERT_NEAR(preset.preamp_right_db, -6.3f, 1e-4);
    ASSERT_EQ(preset.num_bands, 2);

    // Band 1
    ASSERT(preset.bands[0].enabled);
    ASSERT_EQ(preset.bands[0].type, RADIOFORM_FILTER_PEAK);
    ASSERT_NEAR(preset.bands[0].frequency_hz, 31.0f, 1e-4);
    ASSERT_NEAR(preset.bands[0].gain_db, -3.4f, 1e-4);
    ASSERT_NEAR(preset.bands[0].q_factor, 1.41f, 1e-4);

    // Band 2
    ASSERT(preset.bands[1].enabled);
    ASSERT_EQ(preset.bands[1].type, RADIOFORM_FILTER_PEAK);
    ASSERT_NEAR(preset.bands[1].frequency_hz, 62.0f, 1e-4);
    ASSERT_NEAR(preset.bands[1].gain_db, -1.2f, 1e-4);
    ASSERT_NEAR(preset.bands[1].q_factor, 1.41f, 1e-4);

    PASS();
}

TEST(parser_varying_whitespace) {
    const char* autoeq_data = 
        "Preamp:   -4.5   dB\r\n"
        "\n"
        "Filter   1:   ON   PK   Fc   125.0   Hz   Gain   2.5   dB   Q   0.70\n"
        "# Comment line\n"
        "Filter 2: OFF LSC Fc 1000.0 Hz Gain -1.5 dB Q 1.0\r\n";

    radioform_preset_t preset;
    radioform_error_t err = radioform_preset_parse_autoeq(autoeq_data, &preset);

    ASSERT_EQ(err, RADIOFORM_OK);
    ASSERT_NEAR(preset.preamp_db, -4.5f, 1e-4);
    ASSERT_EQ(preset.num_bands, 2);

    // Band 1 (PK with varying whitespace)
    ASSERT(preset.bands[0].enabled);
    ASSERT_EQ(preset.bands[0].type, RADIOFORM_FILTER_PEAK);
    ASSERT_NEAR(preset.bands[0].frequency_hz, 125.0f, 1e-4);
    ASSERT_NEAR(preset.bands[0].gain_db, 2.5f, 1e-4);
    ASSERT_NEAR(preset.bands[0].q_factor, 0.70f, 1e-4);

    // Band 2 (Disabled LSC filter)
    ASSERT(!preset.bands[1].enabled);
    ASSERT_EQ(preset.bands[1].type, RADIOFORM_FILTER_LOW_SHELF);
    ASSERT_NEAR(preset.bands[1].frequency_hz, 1000.0f, 1e-4);
    ASSERT_NEAR(preset.bands[1].gain_db, -1.5f, 1e-4);
    ASSERT_NEAR(preset.bands[1].q_factor, 1.0f, 1e-4);

    PASS();
}

TEST(parser_missing_q_and_gain) {
    const char* autoeq_data = 
        "Filter 1: ON HP Fc 80 Hz\n"; // Missing gain and Q (defaults should apply)

    radioform_preset_t preset;
    radioform_error_t err = radioform_preset_parse_autoeq(autoeq_data, &preset);

    ASSERT_EQ(err, RADIOFORM_OK);
    ASSERT_EQ(preset.num_bands, 1);
    ASSERT_EQ(preset.bands[0].type, RADIOFORM_FILTER_HIGH_PASS);
    ASSERT_NEAR(preset.bands[0].frequency_hz, 80.0f, 1e-4);
    ASSERT_NEAR(preset.bands[0].gain_db, 0.0f, 1e-4); // Default gain
    ASSERT_NEAR(preset.bands[0].q_factor, 1.0f, 1e-4); // Default Q

    PASS();
}
