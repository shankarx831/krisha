// Copyright (C) Radioform / Original Authors
// Modified by Shankar (2026) for the KRISHA Architecture. Renamed namespaces and variables.
// Licensed under the GNU GPLv3.

/**
 * @file jni_bridge.cpp
 * @brief Android JNI Bridge spoke implementation.
 */

#include <jni.h>
#include <string.h>
#include <vector>
#include "krisha_dsp.h"
#include "krisha_universal.h"

extern "C" {

static krisha_dsp_engine_t* g_dsp_engine = nullptr;

static void ensure_dsp_engine() {
    if (!g_dsp_engine) {
        g_dsp_engine = krisha_dsp_create(48000);
        if (g_dsp_engine) {
            krisha_preset_t preset;
            krisha_dsp_preset_init_flat(&preset);
            krisha_dsp_apply_preset(g_dsp_engine, &preset);
        }
    }
}

/**
 * JNI method that parses a raw AutoEq preset text file and returns a flat float array.
 * 
 * Java Class Definition:
 * package com.krisha.spoke.android;
 * public class KrishaJNI {
 *     public static native float[] parseAutoEq(String autoEqText);
 * }
 * 
 * Returns flat array: [preamp, preamp_left, preamp_right, num_bands, f0, g0, q0, t0, ...]
 */
JNIEXPORT jfloatArray JNICALL
Java_com_krisha_spoke_android_KrishaJNI_parseAutoEq(
    JNIEnv env,
    jclass clazz,
    jstring jAutoEqText
) {
    if (!jAutoEqText) {
        return NULL;
    }

    // Extract raw UTF-8 string from the Java String object
    const char* text = env->GetStringUTFChars(jAutoEqText, NULL);
    if (!text) {
        return NULL;
    }

    // Parse the AutoEq text into a standard krisha_preset_t structure
    krisha_preset_t preset;
    krisha_error_t err = krisha_preset_parse_autoeq(text, &preset);

    // CRITICAL: Immediately release the Java String characters to prevent Dalvik/ART heap leaks
    env->ReleaseStringUTFChars(jAutoEqText, text);

    if (err != KRISHA_OK) {
        return NULL;
    }

    // Pack parsed values into a flat buffer to send across the JNI boundary
    uint32_t numBands = preset.num_bands;
    uint32_t totalSize = 4 + numBands * 4;

    std::vector<float> localBuf(totalSize);
    localBuf[0] = preset.preamp_db;
    localBuf[1] = preset.preamp_left_db;
    localBuf[2] = preset.preamp_right_db;
    localBuf[3] = (float)numBands;

    for (uint32_t i = 0; i < numBands; i++) {
        localBuf[4 + i * 4] = preset.bands[i].frequency_hz;
        localBuf[5 + i * 4] = preset.bands[i].gain_db;
        localBuf[6 + i * 4] = preset.bands[i].q_factor;
        localBuf[7 + i * 4] = (float)preset.bands[i].type;
    }

    // Allocate a new Java float array to hold our packed data
    jfloatArray result = env->NewFloatArray(totalSize);
    if (!result) {
        return NULL; // Out of memory
    }

    // Copy local vector memory directly into the Java float array region
    env->SetFloatArrayRegion(result, 0, totalSize, localBuf.data());

    return result;
}

JNIEXPORT jboolean JNICALL
Java_com_krisha_spoke_android_KrishaJNI_applyPreset(
    JNIEnv env,
    jclass clazz,
    jfloatArray jFlatPresetArray
) {
    if (!jFlatPresetArray) {
        return JNI_FALSE;
    }

    ensure_dsp_engine();
    if (!g_dsp_engine) {
        return JNI_FALSE;
    }

    jsize len = env->GetArrayLength(jFlatPresetArray);
    if (len < 4) {
        return JNI_FALSE;
    }

    std::vector<float> flatBuf(len);
    env->GetFloatArrayRegion(jFlatPresetArray, 0, len, flatBuf.data());

    krisha_preset_t preset;
    krisha_dsp_preset_init_flat(&preset);

    preset.preamp_db = flatBuf[0];
    preset.preamp_left_db = flatBuf[1];
    preset.preamp_right_db = flatBuf[2];
    uint32_t numBands = (uint32_t)flatBuf[3];
    if (numBands > KRISHA_MAX_BANDS) {
        numBands = KRISHA_MAX_BANDS;
    }
    preset.num_bands = numBands;

    for (uint32_t i = 0; i < numBands; i++) {
        uint32_t base = 4 + i * 4;
        if (base + 3 < (uint32_t)len) {
            preset.bands[i].frequency_hz = flatBuf[base];
            preset.bands[i].gain_db = flatBuf[base + 1];
            preset.bands[i].q_factor = flatBuf[base + 2];
            preset.bands[i].type = (krisha_filter_type_t)((int)flatBuf[base + 3]);
            preset.bands[i].enabled = true;
        }
    }

    krisha_error_t err = krisha_dsp_apply_preset(g_dsp_engine, &preset);
    return (err == KRISHA_OK) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_krisha_spoke_android_KrishaJNI_updatePreamp(
    JNIEnv env,
    jclass clazz,
    jfloat preampLeft,
    jfloat preampRight
) {
    ensure_dsp_engine();
    if (g_dsp_engine) {
        krisha_dsp_update_preamp_left(g_dsp_engine, preampLeft);
        krisha_dsp_update_preamp_right(g_dsp_engine, preampRight);
    }
}

JNIEXPORT jfloat JNICALL
Java_com_krisha_spoke_android_KrishaJNI_queryJniMagnitude(
    JNIEnv env,
    jclass clazz,
    jfloat frequencyHz,
    jboolean isLeft
) {
    ensure_dsp_engine();
    if (!g_dsp_engine) {
        return 0.0f;
    }
    return krisha_dsp_get_magnitude_at_frequency(g_dsp_engine, frequencyHz, isLeft);
}

JNIEXPORT jfloat JNICALL
Java_com_krisha_spoke_android_KrishaJNI_queryJniHarmanTarget(
    JNIEnv env,
    jclass clazz,
    jfloat frequencyHz
) {
    return krisha_dsp_get_harman_target_at_frequency(frequencyHz);
}

} // extern "C"
