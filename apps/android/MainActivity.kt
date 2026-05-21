package com.krisha.spoke.android

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlin.math.log10
import kotlin.math.pow

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        
        // Load the Phase 2 JNI bridge containing the static C++ Core linking
        try {
            System.loadLibrary("krisha_jni")
        } catch (e: Exception) {
            e.printStackTrace()
        }

        setContent {
            MaterialTheme(
                colorScheme = darkColorScheme(
                    background = Color(0xFF0F0F11),
                    surface = Color(0xFF1E1E22),
                    primary = Color(0xFF00E5E5)
                )
            ) {
                Surface(
                    modifier = Modifier.fillMaxSize(),
                    color = MaterialTheme.colorScheme.background
                ) {
                    KrishaScreen()
                }
            }
        }
    }
}

@Composable
fun KrishaScreen() {
    var preampLeft by remember { mutableStateOf(0.0f) }
    var preampRight by remember { mutableStateOf(0.0f) }
    var isBypass by remember { mutableStateOf(false) }
    
    val coroutineScope = rememberCoroutineScope()
    val stepsCount = 120
    var leftMagnitudes by remember { mutableStateOf(FloatArray(stepsCount)) }
    var rightMagnitudes by remember { mutableStateOf(FloatArray(stepsCount)) }

    // Reactive off-thread JNI/DSP graph calculations using standard Coroutines
    LaunchedEffect(preampLeft, preampRight, isBypass) {
        coroutineScope.launch {
            val response = withContext(Dispatchers.Default) {
                calculateBiquadMagnitudes(preampLeft, preampRight, isBypass, stepsCount)
            }
            leftMagnitudes = response.first
            rightMagnitudes = response.second
        }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(16.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        // App HUD Header
        Text(
            text = "KRISHA UNIVERSAL",
            fontSize = 22.sp,
            color = Color.White,
            modifier = Modifier.padding(top = 16.dp)
        )

        // Sleek Logarithmic Canvas Graph Component
        EQResponseGraph(
            leftMags = leftMagnitudes,
            rightMags = rightMagnitudes,
            modifier = Modifier
                .fillMaxWidth()
                .height(200.dp)
                .background(Color(0xFF16161B), RoundedCornerShape(12.dp))
                .padding(8.dp)
        )

        // Balance & Preamp Slider Area
        Card(
            modifier = Modifier.fillMaxWidth(),
            colors = CardDefaults.cardColors(containerColor = Color(0xFF1E1E22))
        ) {
            Column(
                modifier = Modifier.padding(16.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                Text("Preamp Offset Balance", color = Color.White, fontSize = 16.sp)

                // Left Slider
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text("L", color = Color(0xFF00E5E5), modifier = Modifier.width(20.dp))
                    Slider(
                        value = preampLeft,
                        onValueChange = { preampLeft = it },
                        valueRange = -12.0f..12.0f,
                        modifier = Modifier.weight(1f),
                        colors = SliderDefaults.colors(
                            activeTrackColor = Color(0xFF00E5E5),
                            thumbColor = Color(0xFF00E5E5)
                        )
                    )
                    Text(String.format("%.1f dB", preampLeft), color = Color.Gray, modifier = Modifier.width(60.dp))
                }

                // Right Slider
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text("R", color = Color(0xFFFF0099), modifier = Modifier.width(20.dp))
                    Slider(
                        value = preampRight,
                        onValueChange = { preampRight = it },
                        valueRange = -12.0f..12.0f,
                        modifier = Modifier.weight(1f),
                        colors = SliderDefaults.colors(
                            activeTrackColor = Color(0xFFFF0099),
                            thumbColor = Color(0xFFFF0099)
                        )
                    )
                    Text(String.format("%.1f dB", preampRight), color = Color.Gray, modifier = Modifier.width(60.dp))
                }
            }
        }

        val activity = androidx.compose.ui.platform.LocalContext.current as? androidx.activity.ComponentActivity

        // Bypass switch
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text("Bypass DSP Processing", color = Color.White)
            Switch(
                checked = isBypass,
                onCheckedChange = { isBypass = it }
            )
        }

        Spacer(modifier = Modifier.height(16.dp))

        // Reset & Stop Panic Button
        Button(
            onClick = {
                preampLeft = 0.0f
                preampRight = 0.0f
                isBypass = true
                activity?.finishAndRemoveTask()
                kotlin.system.exitProcess(0)
            },
            colors = ButtonDefaults.buttonColors(containerColor = Color(0xFFFF3B30)),
            modifier = Modifier.fillMaxWidth()
        ) {
            Text("Reset & Stop KRISHA (Panic Button)", color = Color.White)
        }
    }
}

@Composable
fun EQResponseGraph(
    leftMags: FloatArray,
    rightMags: FloatArray,
    modifier: Modifier = Modifier
) {
    Canvas(modifier = modifier) {
        val width = size.width
        val height = size.height

        // Draw simple vertical logarithmic reference lines
        val refFreqs = floatArrayOf(20f, 100f, 1000f, 10000f, 20000f)
        val logMin = log10(20f)
        val logMax = log10(20000f)
        refFreqs.forEach { f ->
            val ratio = (log10(f) - logMin) / (logMax - logMin)
            val x = ratio * width
            drawLine(
                color = Color(0x33FFFFFF),
                start = Offset(x, 0f),
                end = Offset(x, height),
                strokeWidth = 1.dp.toPx()
            )
        }

        // Draw horizontal decibel grids (-12dB to +12dB)
        val refDbs = floatArrayOf(-12f, 0f, 12f)
        refDbs.forEach { db ->
            val ratio = 1f - (db + 12f) / 24f
            val y = ratio * height
            drawLine(
                color = Color(0x33FFFFFF),
                start = Offset(0f, y),
                end = Offset(width, y),
                strokeWidth = 1.dp.toPx()
            )
        }

        // Render Left Channel - Neon Cyan
        val leftPath = Path()
        for (i in leftMags.indices) {
            val x = (i.toFloat() / (leftMags.size - 1)) * width
            // Bound dB to structural limits
            val gainDb = leftMags[i].coerceIn(-12.0f, 12.0f)
            val y = (1.0f - (gainDb + 12.0f) / 24.0f) * height

            if (i == 0) leftPath.moveTo(x, y) else leftPath.lineTo(x, y)
        }
        drawPath(
            path = leftPath,
            color = Color(0xFF00E5E5),
            style = Stroke(width = 2.dp.toPx())
        )

        // Render Right Channel - Neon Magenta
        val rightPath = Path()
        for (i in rightMags.indices) {
            val x = (i.toFloat() / (rightMags.size - 1)) * width
            val gainDb = rightMags[i].coerceIn(-12.0f, 12.0f)
            val y = (1.0f - (gainDb + 12.0f) / 24.0f) * height

            if (i == 0) rightPath.moveTo(x, y) else rightPath.lineTo(x, y)
        }
        drawPath(
            path = rightPath,
            color = Color(0xFFFF0099),
            style = Stroke(width = 2.dp.toPx())
        )
    }
}

/**
 * Invokes the local native JNI bridge to query the underlying DSP magnitude responses.
 */
private fun calculateBiquadMagnitudes(
    preampLeft: Float,
    preampRight: Float,
    isBypass: Boolean,
    steps: Int
): Pair<FloatArray, FloatArray> {
    val left = FloatArray(steps)
    val right = FloatArray(steps)

    if (isBypass) return Pair(left, right)

    // Calculate response
    val logMin = log10(20.0)
    val logMax = log10(20000.0)
    val step = (logMax - logMin) / (steps - 1)

    for (i in 0 until steps) {
        val freq = 10.0.pow(logMin + i * step).toFloat()
        
        // P/Invoke-style JNI calls back to the static JNI lib exported in Phase 2
        try {
            left[i] = preampLeft + queryJniMagnitude(freq, true)
            right[i] = preampRight + queryJniMagnitude(freq, false)
        } catch (e: Exception) {
            // Fallback for simulation when JNI shared lib is offline
            left[i] = preampLeft
            right[i] = preampRight
        }
    }

    return Pair(left, right)
}

// Native functions provided by the C++ JNI Spoke
private external fun queryJniMagnitude(frequencyHz: Float, isLeft: Boolean): Float
