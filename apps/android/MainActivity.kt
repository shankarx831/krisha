package com.krisha.spoke.android

import android.content.ClipboardManager
import android.content.Context
import android.os.Bundle
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.platform.LocalClipboardManager
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import kotlin.math.*

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        
        try {
            System.loadLibrary("krisha_jni")
        } catch (e: Exception) {
            e.printStackTrace()
        }

        setContent {
            MaterialTheme(
                colorScheme = darkColorScheme(
                    background = Color(0xFF0F0F11),
                    surface = Color(0xFF16161A),
                    primary = Color(0xFF007AFF)
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

// Data class containing the four synchronized magnitude response curves
data class EQGraphData(
    val harman: FloatArray,
    val raw: FloatArray,
    val eq: FloatArray,
    val final: FloatArray
)

// Flat preset representational constant
private val FlatPreset = AutoEqPresetResult(
    name = "Flat",
    preampDb = 0.0f,
    preampLeftDb = 0.0f,
    preampRightDb = 0.0f,
    bands = emptyList()
)

// Helper: Convert preset to a packed flat float array for JNI
private fun presetToFlatArray(preset: AutoEqPresetResult): FloatArray {
    val numBands = preset.bands.size
    val flat = FloatArray(4 + numBands * 4)
    flat[0] = preset.preampDb
    flat[1] = preset.preampLeftDb
    flat[2] = preset.preampRightDb
    flat[3] = numBands.toFloat()
    
    for (i in 0 until numBands) {
        val base = 4 + i * 4
        val band = preset.bands[i]
        flat[base] = band.frequencyHz
        flat[base + 1] = band.gainDb
        flat[base + 2] = band.qFactor
        flat[base + 3] = band.filterType.toFloat()
    }
    return flat
}

// Helper: Serialise preset to JSON
private fun presetToJson(preset: AutoEqPresetResult): String {
    val json = JSONObject()
    json.put("name", preset.name)
    json.put("preampDb", preset.preampDb.toDouble())
    json.put("preampLeftDb", preset.preampLeftDb.toDouble())
    json.put("preampRightDb", preset.preampRightDb.toDouble())
    
    val bandsArray = JSONArray()
    preset.bands.forEach { band ->
        val bandJson = JSONObject()
        bandJson.put("frequencyHz", band.frequencyHz.toDouble())
        bandJson.put("gainDb", band.gainDb.toDouble())
        bandJson.put("qFactor", band.qFactor.toDouble())
        bandJson.put("filterType", band.filterType)
        bandsArray.put(bandJson)
    }
    json.put("bands", bandsArray)
    return json.toString()
}

// Helper: Deserialise preset from JSON
private fun jsonToPreset(jsonStr: String): AutoEqPresetResult {
    val json = JSONObject(jsonStr)
    val name = json.getString("name")
    val preampDb = json.getDouble("preampDb").toFloat()
    val preampLeftDb = json.getDouble("preampLeftDb").toFloat()
    val preampRightDb = json.getDouble("preampRightDb").toFloat()
    
    val bandsArray = json.getJSONArray("bands")
    val bands = ArrayList<AutoEqBand>()
    for (i in 0 until bandsArray.length()) {
        val bandJson = bandsArray.getJSONObject(i)
        bands.add(
            AutoEqBand(
                frequencyHz = bandJson.getDouble("frequencyHz").toFloat(),
                gainDb = bandJson.getDouble("gainDb").toFloat(),
                qFactor = bandJson.getDouble("qFactor").toFloat(),
                filterType = bandJson.getInt("filterType")
            )
        )
    }
    return AutoEqPresetResult(name, preampDb, preampLeftDb, preampRightDb, bands)
}

// SharedPreferences management
private const val PREFS_NAME = "KrishaPrefs"
private const val PRESETS_KEY = "KrishaCustomPresets"

private fun getSavedPresets(context: Context): List<AutoEqPresetResult> {
    val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
    val jsonStr = prefs.getString(PRESETS_KEY, null) ?: return emptyList()
    return try {
        val array = JSONArray(jsonStr)
        val list = ArrayList<AutoEqPresetResult>()
        for (i in 0 until array.length()) {
            list.add(jsonToPreset(array.getString(i)))
        }
        list
    } catch (e: Exception) {
        emptyList()
    }
}

private fun savePresets(context: Context, presets: List<AutoEqPresetResult>) {
    val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
    val array = JSONArray()
    presets.forEach { preset ->
        array.put(presetToJson(preset))
    }
    prefs.edit().putString(PRESETS_KEY, array.toString()).apply()
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun KrishaScreen() {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    
    // Preset state
    var savedPresetsList by remember { mutableStateOf(getSavedPresets(context)) }
    var activePreset by remember { mutableStateOf(FlatPreset) }
    
    // Audio control states
    var preampLeft by remember { mutableStateOf(0.0f) }
    var preampRight by remember { mutableStateOf(0.0f) }
    var isBypass by remember { mutableStateOf(false) }
    
    // AutoEq search & import states
    var autoEqInput by remember { mutableStateOf("") }
    var isFetching by remember { mutableStateOf(false) }
    var customPresetName by remember { mutableStateOf("") }
    var loadedDynamicPreset by remember { mutableStateOf<AutoEqPresetResult?>(null) }
    
    // Magnitude curves
    val stepsCount = 120
    var harmanMags by remember { mutableStateOf(FloatArray(stepsCount)) }
    var rawMags by remember { mutableStateOf(FloatArray(stepsCount)) }
    var eqMags by remember { mutableStateOf(FloatArray(stepsCount)) }
    var finalMags by remember { mutableStateOf(FloatArray(stepsCount)) }

    // Synchronize loaded preset coefficients into active DSP engine
    fun applyPresetToEngine(preset: AutoEqPresetResult) {
        activePreset = preset
        preampLeft = preset.preampLeftDb
        preampRight = preset.preampRightDb
        
        try {
            val flatArray = presetToFlatArray(preset)
            KrishaJNI.applyPreset(flatArray)
        } catch (e: Exception) {
            e.printStackTrace()
        }
    }

    // Recalculate curves when settings change (lock-free, off-thread)
    LaunchedEffect(preampLeft, preampRight, isBypass, activePreset) {
        scope.launch {
            val response = withContext(Dispatchers.Default) {
                calculateBiquadMagnitudes(preampLeft, preampRight, isBypass, stepsCount)
            }
            harmanMags = response.harman
            rawMags = response.raw
            eqMags = response.eq
            finalMags = response.final
        }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(16.dp)
            .verticalScroll(rememberScrollState()),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(14.dp)
    ) {
        // App HUD Header
        Text(
            text = "KRISHA UNIVERSAL",
            fontSize = 18.sp,
            fontWeight = FontWeight.Bold,
            color = Color.White,
            letterSpacing = 2.sp,
            modifier = Modifier.padding(top = 12.dp, bottom = 4.dp)
        )

        // Dual magnitude visual graph
        EQResponseGraph(
            finalMags = finalMags,
            harmanMags = harmanMags,
            rawMags = rawMags,
            eqMags = eqMags,
            modifier = Modifier
                .fillMaxWidth()
                .height(200.dp)
                .background(Color(0xFF16161A), RoundedCornerShape(12.dp))
                .padding(12.dp)
        )

        // Comparison Stats HUD overlay
        Card(
            modifier = Modifier.fillMaxWidth(),
            colors = CardDefaults.cardColors(containerColor = Color(0xFF16161A)),
            shape = RoundedCornerShape(12.dp)
        ) {
            Column(modifier = Modifier.padding(12.dp)) {
                Text(
                    text = "ACTIVE PRESET: ${activePreset.name.uppercase()}",
                    color = Color.White,
                    fontSize = 12.sp,
                    fontWeight = FontWeight.Bold,
                    letterSpacing = 1.sp
                )
                Spacer(modifier = Modifier.height(4.dp))
                
                // Calculate RMS deviation between Final response and Harman target
                var rmsDeviation = 0.0f
                var sumSq = 0.0f
                for (i in 0 until stepsCount) {
                    val activeDb = finalMags[i]
                    val harmanDb = harmanMags[i]
                    val diff = activeDb - harmanDb
                    sumSq += diff * diff
                }
                rmsDeviation = sqrt(sumSq / stepsCount)
                
                Text(
                    text = String.format("Harman Target RMS Deviation: %.2f dB", rmsDeviation),
                    color = if (rmsDeviation < 2.0f) Color(0xFF34C759) else Color(0xFFFF9500),
                    fontSize = 11.sp,
                    fontWeight = FontWeight.Medium
                )
            }
        }

        // Preset selector Card
        Card(
            modifier = Modifier.fillMaxWidth(),
            colors = CardDefaults.cardColors(containerColor = Color(0xFF16161A)),
            shape = RoundedCornerShape(12.dp)
        ) {
            Column(
                modifier = Modifier.padding(14.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Text(
                    text = "PRESETS",
                    color = Color.White.copy(alpha = 0.6f),
                    fontSize = 11.sp,
                    fontWeight = FontWeight.SemiBold,
                    letterSpacing = 1.sp
                )

                // Render presets selection
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .background(Color(0xFF222228), RoundedCornerShape(8.dp))
                        .clickable {
                            applyPresetToEngine(FlatPreset)
                            Toast.makeText(context, "Loaded Flat curve", Toast.LENGTH_SHORT).show()
                        }
                        .padding(12.dp),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text(
                        text = "Programmatic Flat Baseline",
                        color = if (activePreset.name == "Flat") Color(0xFF007AFF) else Color.White,
                        fontWeight = FontWeight.Medium,
                        fontSize = 13.sp
                    )
                }

                // Custom saved presets
                savedPresetsList.forEach { preset ->
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .background(Color(0xFF222228), RoundedCornerShape(8.dp))
                            .clickable {
                                applyPresetToEngine(preset)
                                Toast.makeText(context, "Loaded preset: ${preset.name}", Toast.LENGTH_SHORT).show()
                            }
                            .padding(horizontal = 12.dp, vertical = 8.dp),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text(
                            text = preset.name,
                            color = if (activePreset.name == preset.name) Color(0xFF007AFF) else Color.White,
                            fontWeight = FontWeight.Medium,
                            fontSize = 13.sp,
                            modifier = Modifier.weight(1f)
                        )
                        IconButton(
                            onClick = {
                                val updated = savedPresetsList.filter { it.name != preset.name }
                                savePresets(context, updated)
                                savedPresetsList = updated
                                if (activePreset.name == preset.name) {
                                    applyPresetToEngine(FlatPreset)
                                }
                                Toast.makeText(context, "Preset deleted", Toast.LENGTH_SHORT).show()
                            }
                        ) {
                            Icon(
                                imageVector = Icons.Default.Delete,
                                contentDescription = "Delete",
                                tint = Color(0xFFFF3B30)
                            )
                        }
                    }
                }
            }
        }

        // AutoEq Import Engine Card
        Card(
            modifier = Modifier.fillMaxWidth(),
            colors = CardDefaults.cardColors(containerColor = Color(0xFF16161A)),
            shape = RoundedCornerShape(12.dp)
        ) {
            Column(
                modifier = Modifier.padding(14.dp),
                verticalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                Text(
                    text = "AUTOEQ IMPORT ENGINE",
                    color = Color.White.copy(alpha = 0.6f),
                    fontSize = 11.sp,
                    fontWeight = FontWeight.SemiBold,
                    letterSpacing = 1.sp
                )

                OutlinedTextField(
                    value = autoEqInput,
                    onValueChange = { autoEqInput = it },
                    label = { Text("Headphone Search Path (e.g. sennheiser/hd_600)", fontSize = 12.sp) },
                    modifier = Modifier.fillMaxWidth(),
                    colors = OutlinedTextFieldDefaults.colors(
                        focusedTextColor = Color.White,
                        unfocusedTextColor = Color.White,
                        focusedBorderColor = Color(0xFF007AFF),
                        unfocusedBorderColor = Color(0xFF2C2C2E)
                    ),
                    singleLine = true
                )

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Button(
                        onClick = {
                            if (autoEqInput.isEmpty()) {
                                Toast.makeText(context, "Search path cannot be empty", Toast.LENGTH_SHORT).show()
                                return@Button
                            }
                            isFetching = true
                            scope.launch {
                                val search = AutoEqSearch()
                                val result = search.fetchAndParsePreset(autoEqInput)
                                isFetching = false
                                if (result != null) {
                                    loadedDynamicPreset = result
                                    customPresetName = result.name
                                    applyPresetToEngine(result)
                                    Toast.makeText(context, "Successfully loaded: ${result.name}", Toast.LENGTH_SHORT).show()
                                } else {
                                    Toast.makeText(context, "Failed to download from AutoEq", Toast.LENGTH_SHORT).show()
                                }
                            }
                        },
                        modifier = Modifier.weight(1f),
                        colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF007AFF)),
                        shape = RoundedCornerShape(8.dp)
                    ) {
                        if (isFetching) {
                            CircularProgressIndicator(color = Color.White, modifier = Modifier.size(16.dp))
                        } else {
                            Text("Fetch", fontSize = 12.sp, fontWeight = FontWeight.Bold)
                        }
                    }

                    // Clipboard import helper
                    Button(
                        onClick = {
                            try {
                                val clipboard = context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
                                val clip = clipboard.primaryClip
                                if (clip != null && clip.itemCount > 0) {
                                    val text = clip.getItemAt(0).text.toString()
                                    scope.launch(Dispatchers.Default) {
                                        val flatArray = KrishaJNI.parseAutoEq(text)
                                        withContext(Dispatchers.Main) {
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
                                                
                                                val importedPreset = AutoEqPresetResult(
                                                    name = "Imported",
                                                    preampDb = preampGlobal,
                                                    preampLeftDb = preampLeft,
                                                    preampRightDb = preampRight,
                                                    bands = bands
                                                )
                                                loadedDynamicPreset = importedPreset
                                                customPresetName = "Imported EQ"
                                                applyPresetToEngine(importedPreset)
                                                Toast.makeText(context, "Successfully loaded from Clipboard!", Toast.LENGTH_SHORT).show()
                                            } else {
                                                Toast.makeText(context, "Invalid ParametricEQ clipboard data", Toast.LENGTH_SHORT).show()
                                            }
                                        }
                                    }
                                } else {
                                    Toast.makeText(context, "Clipboard is empty", Toast.LENGTH_SHORT).show()
                                }
                            } catch (e: Exception) {
                                Toast.makeText(context, "Failed to read clipboard", Toast.LENGTH_SHORT).show()
                            }
                        },
                        modifier = Modifier.weight(1f),
                        colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF2C2C2E)),
                        shape = RoundedCornerShape(8.dp)
                    ) {
                        Text("Paste EQ", fontSize = 12.sp, fontWeight = FontWeight.Bold, color = Color.White)
                    }
                }

                // Custom preset name and save button
                loadedDynamicPreset?.let { preset ->
                    Spacer(modifier = Modifier.height(4.dp))
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        OutlinedTextField(
                            value = customPresetName,
                            onValueChange = { customPresetName = it },
                            label = { Text("Preset Label", fontSize = 11.sp) },
                            modifier = Modifier.weight(1.5f),
                            colors = OutlinedTextFieldDefaults.colors(
                                focusedTextColor = Color.White,
                                unfocusedTextColor = Color.White,
                                focusedBorderColor = Color(0xFF007AFF),
                                unfocusedBorderColor = Color(0xFF2C2C2E)
                            ),
                            singleLine = true
                        )

                        Button(
                            onClick = {
                                if (customPresetName.trim().isEmpty()) {
                                    Toast.makeText(context, "Preset name cannot be blank", Toast.LENGTH_SHORT).show()
                                    return@Button
                                }
                                val toSave = preset.copy(name = customPresetName.trim())
                                val currentList = savedPresetsList.filter { it.name != toSave.name }.toMutableList()
                                currentList.add(toSave)
                                savePresets(context, currentList)
                                savedPresetsList = currentList
                                applyPresetToEngine(toSave)
                                Toast.makeText(context, "Preset saved to persistent storage!", Toast.LENGTH_SHORT).show()
                            },
                            modifier = Modifier.weight(1f),
                            colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF34C759)),
                            shape = RoundedCornerShape(8.dp)
                        ) {
                            Text("Save", fontSize = 12.sp, fontWeight = FontWeight.Bold)
                        }
                    }
                }
            }
        }

        // Balance & Preamp Slider Area (Material 3 settings layout)
        Card(
            modifier = Modifier.fillMaxWidth(),
            colors = CardDefaults.cardColors(containerColor = Color(0xFF16161A)),
            shape = RoundedCornerShape(12.dp)
        ) {
            Column(
                modifier = Modifier.padding(14.dp),
                verticalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                Text(
                    text = "PREAMP BALANCE",
                    color = Color.White.copy(alpha = 0.6f),
                    fontSize = 11.sp,
                    fontWeight = FontWeight.SemiBold,
                    letterSpacing = 1.sp
                )

                // Left Slider
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Text(
                        text = "L",
                        color = Color.White.copy(alpha = 0.8f),
                        fontSize = 13.sp,
                        fontWeight = FontWeight.Bold,
                        modifier = Modifier.width(24.dp)
                    )
                    Slider(
                        value = preampLeft,
                        onValueChange = { 
                            preampLeft = it 
                            // Update JNI in real-time
                            try {
                                KrishaJNI.updatePreamp(preampLeft, preampRight)
                            } catch (e: Exception) {
                                e.printStackTrace()
                            }
                        },
                        valueRange = -12.0f..12.0f,
                        modifier = Modifier.weight(1f),
                        colors = SliderDefaults.colors(
                            activeTrackColor = Color(0xFF007AFF),
                            inactiveTrackColor = Color(0xFF2C2C2E),
                            thumbColor = Color(0xFF007AFF)
                        )
                    )
                    Text(
                        text = String.format("%.1f dB", preampLeft),
                        color = Color.White.copy(alpha = 0.7f),
                        fontSize = 12.sp,
                        modifier = Modifier.width(55.dp),
                        textAlign = TextAlign.End
                    )
                }

                // Right Slider
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Text(
                        text = "R",
                        color = Color.White.copy(alpha = 0.8f),
                        fontSize = 13.sp,
                        fontWeight = FontWeight.Bold,
                        modifier = Modifier.width(24.dp)
                    )
                    Slider(
                        value = preampRight,
                        onValueChange = { 
                            preampRight = it 
                            // Update JNI in real-time
                            try {
                                KrishaJNI.updatePreamp(preampLeft, preampRight)
                            } catch (e: Exception) {
                                e.printStackTrace()
                            }
                        },
                        valueRange = -12.0f..12.0f,
                        modifier = Modifier.weight(1f),
                        colors = SliderDefaults.colors(
                            activeTrackColor = Color(0xFF007AFF),
                            inactiveTrackColor = Color(0xFF2C2C2E),
                            thumbColor = Color(0xFF007AFF)
                        )
                    )
                    Text(
                        text = String.format("%.1f dB", preampRight),
                        color = Color.White.copy(alpha = 0.7f),
                        fontSize = 12.sp,
                        modifier = Modifier.width(55.dp),
                        textAlign = TextAlign.End
                    )
                }
            }
        }

        // Bypass Switch Card
        Card(
            modifier = Modifier.fillMaxWidth(),
            colors = CardDefaults.cardColors(containerColor = Color(0xFF16161A)),
            shape = RoundedCornerShape(12.dp)
        ) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 14.dp, vertical = 6.dp),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = "Bypass DSP Processing",
                    color = Color.White,
                    fontSize = 13.sp,
                    fontWeight = FontWeight.Medium
                )
                Switch(
                    checked = isBypass,
                    onCheckedChange = { isBypass = it },
                    colors = SwitchDefaults.colors(
                        checkedThumbColor = Color.White,
                        checkedTrackColor = Color(0xFF007AFF),
                        uncheckedThumbColor = Color.Gray,
                        uncheckedTrackColor = Color(0xFF2C2C2E)
                    )
                )
            }
        }

        Spacer(modifier = Modifier.height(10.dp))

        // Panic Button (Subtle, sleek, rounded)
        Button(
            onClick = {
                preampLeft = 0.0f
                preampRight = 0.0f
                isBypass = true
                applyPresetToEngine(FlatPreset)
                val activity = context as? ComponentActivity
                activity?.finishAndRemoveTask()
                kotlin.system.exitProcess(0)
            },
            colors = ButtonDefaults.buttonColors(containerColor = Color(0xFFFF3B30)),
            shape = RoundedCornerShape(8.dp),
            modifier = Modifier
                .fillMaxWidth()
                .padding(bottom = 12.dp)
        ) {
            Text(
                text = "Uninstall / Panic Stop Driver",
                color = Color.White,
                fontSize = 13.sp,
                fontWeight = FontWeight.Bold
            )
        }
    }
}

@Composable
fun EQResponseGraph(
    finalMags: FloatArray,
    harmanMags: FloatArray,
    rawMags: FloatArray,
    eqMags: FloatArray,
    modifier: Modifier = Modifier
) {
    Canvas(modifier = modifier) {
        val width = size.width
        val height = size.height

        // Draw standard & intermediate vertical logarithmic reference lines
        // 20Hz, 100Hz, 1kHz, 10kHz, 20kHz are major lines
        // 50Hz, 200Hz, 500Hz, 2kHz, 5kHz are intermediate lines
        val refFreqs = floatArrayOf(20f, 50f, 100f, 200f, 500f, 1000f, 2000f, 5000f, 10000f, 20000f)
        val logMin = log10(20f)
        val logMax = log10(20000f)
        
        refFreqs.forEach { f ->
            val ratio = (log10(f) - logMin) / (logMax - logMin)
            val x = ratio * width
            val isMajor = f == 20f || f == 100f || f == 1000f || f == 10000f || f == 20000f
            
            drawLine(
                color = if (isMajor) Color(0x26FFFFFF) else Color(0x0CFFFFFF),
                start = Offset(x, 0f),
                end = Offset(x, height),
                strokeWidth = 1.dp.toPx(),
                pathEffect = if (isMajor) null else PathEffect.dashPathEffect(floatArrayOf(4f, 4f), 0f)
            )
        }

        // Draw horizontal decibel grids (+12dB, +6dB, 0dB, -6dB, -12dB)
        val refDbs = floatArrayOf(-12f, -6f, 0f, 6f, 12f)
        refDbs.forEach { db ->
            val ratio = 1f - (db + 12f) / 24f
            val y = ratio * height
            drawLine(
                color = if (db == 0f) Color(0x33FFFFFF) else Color(0x13FFFFFF),
                start = Offset(0f, y),
                end = Offset(width, y),
                strokeWidth = 1.dp.toPx()
            )
        }

        // --- LAYER 1: Line 3 (Raw Response Curve) - Ultra-thin, 1.0dp path, muted low-opacity gray
        val rawPath = Path()
        for (i in rawMags.indices) {
            val x = (i.toFloat() / (rawMags.size - 1)) * width
            val y = (1.0f - (rawMags[i] + 12.0f) / 24.0f) * height

            if (i == 0) rawPath.moveTo(x, y) else rawPath.lineTo(x, y)
        }
        drawPath(
            path = rawPath,
            color = Color(0x6648484A),
            style = Stroke(width = 1.0f.dp.toPx())
        )

        // --- LAYER 2: Line 4 (Equalizer Filter Curve) - Ultra-thin, 1.0dp path, muted low-opacity gray
        val eqPath = Path()
        for (i in eqMags.indices) {
            val x = (i.toFloat() / (eqMags.size - 1)) * width
            val y = (1.0f - (eqMags[i] + 12.0f) / 24.0f) * height

            if (i == 0) eqPath.moveTo(x, y) else eqPath.lineTo(x, y)
        }
        drawPath(
            path = eqPath,
            color = Color(0x6648484A),
            style = Stroke(width = 1.0f.dp.toPx())
        )

        // --- LAYER 3: Line 2 (Target Curve - Harman Baseline) - Thin solid highly defined dark gray
        val harmanPath = Path()
        for (i in harmanMags.indices) {
            val x = (i.toFloat() / (harmanMags.size - 1)) * width
            val y = (1.0f - (harmanMags[i] + 12.0f) / 24.0f) * height

            if (i == 0) harmanPath.moveTo(x, y) else harmanPath.lineTo(x, y)
        }
        drawPath(
            path = harmanPath,
            color = Color(0xFF3A3A3C),
            style = Stroke(width = 1.5f.dp.toPx())
        )

        // --- LAYER 4: Line 1 (Final Equalized Result) - Solid, sharp 2.5dp primary accent blue
        val finalPath = Path()
        for (i in finalMags.indices) {
            val x = (i.toFloat() / (finalMags.size - 1)) * width
            val y = (1.0f - (finalMags[i] + 12.0f) / 24.0f) * height

            if (i == 0) finalPath.moveTo(x, y) else finalPath.lineTo(x, y)
        }
        drawPath(
            path = finalPath,
            color = Color(0xFF007AFF),
            style = Stroke(width = 2.5f.dp.toPx())
        )
    }
}

// Analog-style soft-clamping boundaries helper
private fun softClamp(db: Float): Float {
    val maxVal = 12.0f
    val minVal = -12.0f
    if (db > maxVal) {
        return maxVal + 2.0f * tanh((db - maxVal) / 2.0f)
    }
    if (db < minVal) {
        return minVal + 2.0f * tanh((db - minVal) / 2.0f)
    }
    return db
}

// Off-thread DSP calculations yielding 4 layered magnitude response curves
private fun calculateBiquadMagnitudes(
    preampLeft: Float,
    preampRight: Float,
    isBypass: Boolean,
    steps: Int
): EQGraphData {
    val harman = FloatArray(steps)
    val raw = FloatArray(steps)
    val eq = FloatArray(steps)
    val final = FloatArray(steps)

    val logMin = log10(20.0)
    val logMax = log10(20000.0)
    val step = (logMax - logMin) / (steps - 1)

    for (i in 0 until steps) {
        val freq = 10.0.pow(logMin + i * step).toFloat()
        
        // Harman Target Curve Baseline (Line 2)
        var harmanVal = 0.0f
        try {
            harmanVal = KrishaJNI.queryJniHarmanTarget(freq)
        } catch (e: Exception) {
            harmanVal = calculateHarmanFallback(freq)
        }
        harman[i] = softClamp(harmanVal)

        // Target EQ Response (optimal loaded preset configuration)
        var targetEqVal = 0.0f
        try {
            targetEqVal = KrishaJNI.queryJniTargetMagnitude(freq, true)
        } catch (e: Exception) {
            targetEqVal = 0.0f
        }

        // Predicted raw response of the headphone (Line 3): Raw = Harman - EQ_target
        val rawVal = harmanVal - targetEqVal
        raw[i] = softClamp(rawVal)

        // Equalizer Filter response (Line 4): EQ_active
        var eqVal = 0.0f
        if (isBypass) {
            eqVal = preampLeft
        } else {
            try {
                eqVal = KrishaJNI.queryJniMagnitude(freq, true)
            } catch (e: Exception) {
                eqVal = preampLeft
            }
        }
        eq[i] = softClamp(eqVal)

        // Equalized Final curve (Line 1): Final = Raw + EQ_active
        val finalVal = rawVal + eqVal
        final[i] = softClamp(finalVal)
    }

    return EQGraphData(harman, raw, eq, final)
}

private fun calculateHarmanFallback(frequencyHz: Float): Float {
    val f = frequencyHz.toDouble()
    val bassBoost = 5.0 / (1.0 + (f / 100.0).pow(2.0))
    val earGain = 8.0 * exp(-0.5 * (log10(f / 3000.0) / 0.2).pow(2.0))
    val treblePeak = 2.0 * exp(-0.5 * (log10(f / 6000.0) / 0.15).pow(2.0))
    return (bassBoost + earGain + treblePeak - 4.0).toFloat()
}
