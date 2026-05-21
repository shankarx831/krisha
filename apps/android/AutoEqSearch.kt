package com.radioform.spoke.android

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.net.URL

/**
 * JNI Bridge class mapping to packages/spokes/android/jni_bridge.cpp.
 */
object RadioformJNI {
    init {
        try {
            System.loadLibrary("radioform_jni")
        } catch (e: Exception) {
            e.printStackTrace()
        }
    }

    /**
     * Native C++ parser interface.
     * Returns flat array: [preamp, preamp_left, preamp_right, num_bands, f0, g0, q0, t0, ...]
     */
    external fun parseAutoEq(autoEqText: String): FloatArray?
}

class AutoEqSearch {

    /**
     * Asynchronously downloads ParametricEQ.txt from the jaakkopasanen/AutoEq master tree
     * and maps it to the JNI bridge parser.
     *
     * @param headphonePathName The relative Git path (e.g. "sennheiser/sennheiser_hd_600")
     */
    suspend fun fetchAndParsePreset(headphonePathName: String): AutoEqPresetResult? = withContext(Dispatchers.IO) {
        try {
            val urlString = "https://raw.githubusercontent.com/jaakkopasanen/AutoEq/master/results/$headphonePathName/ParametricEQ.txt"
            val rawText = URL(urlString).readText()

            if (rawText.isNotEmpty()) {
                val flatArray = RadioformJNI.parseAutoEq(rawText)
                if (flatArray != null && flatArray.size >= 4) {
                    val preampGlobal = flatArray[0]
                    val preampLeft = flatArray[1]
                    val preampRight = flatArray[2]
                    val numBands = flatArray[3].toInt()

                    val bands = ArrayList<AutoEqBand>()
                    for (i in 0 until numBands) {
                        val base = 4 + i * 4
                        if (base + 3 < flatArray.size) {
                            bands.add(
                                AutoEqBand(
                                    frequencyHz = flatArray[base],
                                    gainDb = flatArray[base + 1],
                                    qFactor = flatArray[base + 2],
                                    filterType = flatArray[base + 3].toInt()
                                )
                            )
                        }
                    }

                    return@withContext AutoEqPresetResult(
                        name = headphonePathName.substringAfterLast("/"),
                        preampDb = preampGlobal,
                        preampLeftDb = preampLeft,
                        preampRightDb = preampRight,
                        bands = bands
                    )
                }
            }
        } catch (e: Exception) {
            e.printStackTrace()
        }
        return@withContext null
    }
}

data class AutoEqBand(
    val frequencyHz: Float,
    val gainDb: Float,
    val qFactor: Float,
    val filterType: Int
)

data class AutoEqPresetResult(
    val name: String,
    val preampDb: Float,
    val preampLeftDb: Float,
    val preampRightDb: Float,
    val bands: List<AutoEqBand>
)
