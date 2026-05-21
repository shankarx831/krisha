// Copyright (C) Radioform / Original Authors
// Modified by Shankar (2026) for the KRISHA Architecture. Renamed namespaces and variables.
// Licensed under the GNU GPLv3.

/**
 * @file parser.cpp
 * @brief AutoEq ParametricEQ parser implementation
 */

#include "krisha_dsp.h"
#include <string>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cmath>

static std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

static std::string to_upper(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    });
    return str;
}

// Find key and return the float value following it
static bool extract_float_after_key(const std::string& line, const std::string& key, float& out_value) {
    size_t pos = to_upper(line).find(to_upper(key));
    if (pos == std::string::npos) {
        return false;
    }

    // Skip the key and any spaces or separators
    pos += key.length();
    while (pos < line.length() && (std::isspace(static_cast<unsigned char>(line[pos])) || line[pos] == ':')) {
        pos++;
    }

    if (pos >= line.length()) {
        return false;
    }

    try {
        size_t idx = 0;
        out_value = std::stof(line.substr(pos), &idx);
        return true;
    } catch (...) {
        return false;
    }
}

krisha_error_t krisha_preset_parse_autoeq(
    const char* text,
    krisha_preset_t* preset
) {
    if (!text || !preset) {
        return KRISHA_ERROR_NULL_POINTER;
    }

    // First initialize to flat
    krisha_dsp_preset_init_flat(preset);
    std::strncpy(preset->name, "AutoEq Imported", sizeof(preset->name) - 1);
    preset->name[sizeof(preset->name) - 1] = '\0';

    std::string text_str(text);
    std::istringstream stream(text_str);
    std::string line;
    uint32_t parsed_bands_count = 0;

    while (std::getline(stream, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue; // Skip empty lines and comments
        }

        std::string upper_line = to_upper(line);

        // 1. Parse Preamp
        if (upper_line.find("PREAMP") != std::string::npos) {
            float preamp_val = 0.0f;
            if (extract_float_after_key(line, "Preamp", preamp_val)) {
                preset->preamp_db = preamp_val;
                preset->preamp_left_db = preamp_val;
                preset->preamp_right_db = preamp_val;
            }
            continue;
        }

        // 2. Parse Filter
        if (upper_line.find("FILTER") != std::string::npos && parsed_bands_count < KRISHA_MAX_BANDS) {
            // Find colon that separates "Filter N:" from params, or start from "Filter"
            size_t start_pos = line.find(':');
            std::string params_part = (start_pos != std::string::npos) ? line.substr(start_pos + 1) : line;
            params_part = trim(params_part);
            std::string upper_params = to_upper(params_part);

            krisha_band_t band;
            band.enabled = true; // Default to enabled
            band.gain_db = 0.0f;
            band.q_factor = 1.0f;
            band.frequency_hz = 1000.0f;
            band.type = KRISHA_FILTER_PEAK;

            // Check if ON or OFF
            if (upper_params.find("OFF") != std::string::npos) {
                band.enabled = false;
            }

            // Determine filter type
            if (upper_params.find(" PK ") != std::string::npos || upper_params.find(" PK") != std::string::npos || upper_params.find(" PEAK") != std::string::npos) {
                band.type = KRISHA_FILTER_PEAK;
            } else if (upper_params.find(" LSC ") != std::string::npos || upper_params.find(" LSC") != std::string::npos ||
                       upper_params.find(" LS ") != std::string::npos || upper_params.find(" LS") != std::string::npos ||
                       upper_params.find(" LOW_SHELF") != std::string::npos) {
                band.type = KRISHA_FILTER_LOW_SHELF;
            } else if (upper_params.find(" HSC ") != std::string::npos || upper_params.find(" HSC") != std::string::npos ||
                       upper_params.find(" HS ") != std::string::npos || upper_params.find(" HS") != std::string::npos ||
                       upper_params.find(" HIGH_SHELF") != std::string::npos) {
                band.type = KRISHA_FILTER_HIGH_SHELF;
            } else if (upper_params.find(" LP ") != std::string::npos || upper_params.find(" LP") != std::string::npos ||
                       upper_params.find(" LOW_PASS") != std::string::npos) {
                band.type = KRISHA_FILTER_LOW_PASS;
            } else if (upper_params.find(" HP ") != std::string::npos || upper_params.find(" HP") != std::string::npos ||
                       upper_params.find(" HIGH_PASS") != std::string::npos) {
                band.type = KRISHA_FILTER_HIGH_PASS;
            }

            // Extract Fc
            float fc_val = 0.0f;
            if (extract_float_after_key(params_part, "Fc", fc_val)) {
                band.frequency_hz = fc_val;
            }

            // Extract Gain
            float gain_val = 0.0f;
            if (extract_float_after_key(params_part, "Gain", gain_val)) {
                band.gain_db = gain_val;
            }

            // Extract Q
            float q_val = 1.0f;
            if (extract_float_after_key(params_part, "Q", q_val)) {
                band.q_factor = q_val;
            }

            // Store band in preset
            preset->bands[parsed_bands_count] = band;
            parsed_bands_count++;
        }
    }

    if (parsed_bands_count > 0) {
        preset->num_bands = parsed_bands_count;
        return KRISHA_OK;
    }

    return KRISHA_ERROR_INVALID_PARAM; // No valid filters parsed
}
