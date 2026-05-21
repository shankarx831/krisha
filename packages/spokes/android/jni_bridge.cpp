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
#include "krisha_universal.h"

extern "C" {

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

} // extern "C"
