import Foundation
import CKrishaDSP

enum PresetError: Error {
    case invalidPreset
    case fileNotFound
    case encodingFailed
    case decodingFailed
}

/// Result of mapping a preset's bands to the standard 10-band UI frequencies
struct MappedBandResult {
    var gains: [Float]
    var qFactors: [Float]
    var filterTypes: [FilterType]
    var frequencies: [Float]
    var warnings: [String]
}

/// Manages loading, saving, and organizing presets
class PresetManager: ObservableObject {
    static let shared = PresetManager()
    static let customPresetName = "Custom"

    @Published var bundledPresets: [EQPreset] = []
    @Published var userPresets: [EQPreset] = []
    @Published var currentPreset: EQPreset?
    @Published var isEnabled: Bool = true
    @Published var currentBands: [Float] = Array(repeating: 0, count: 10)  // Current gain values for 10 bands

    // Per-band advanced settings
    @Published var currentQFactors: [Float] = Array(repeating: 1.0, count: 10)
    @Published var currentFilterTypes: [FilterType] = Array(repeating: .peak, count: 10)
    @Published var currentFrequencies: [Float] = [32, 64, 125, 250, 500, 1000, 2000, 4000, 8000, 16000]

    // Global settings
    @Published var currentPreampDb: Float = 0.0
    @Published var currentPreampLeftDb: Float = 0.0
    @Published var currentPreampRightDb: Float = 0.0
    @Published var currentLimiterEnabled: Bool = true
    @Published var currentLimiterThresholdDb: Float = -1.0

    // Focus mode state
    @Published var focusedBandIndex: Int? = nil   // nil = no focus, 0-9 = band, 10 = preamp

    // Custom preset state
    @Published var isCustomPreset: Bool = false
    @Published var isEditingPresetName: Bool = false
    @Published var isSavingPreset: Bool = false

    // Cached computed values (updated when presets change)
    private(set) var allPresets: [EQPreset] = []
    private(set) var userPresetIDs: Set<EQPreset.ID> = []

    private let userPresetsURL: URL
    private let standardFrequencies: [Float] = [
        32, 64, 125, 250, 500, 1000, 2000, 4000, 8000, 16000,
    ]

    // Timers
    private var audioApplyTimer: Timer?

    private init() {
        // Get user presets directory
        let appSupport = FileManager.default.urls(
            for: .applicationSupportDirectory,
            in: .userDomainMask
        ).first!

        userPresetsURL =
            appSupport
            .appendingPathComponent("Krisha")
            .appendingPathComponent("Presets")

        // Create directory if needed
        try? FileManager.default.createDirectory(
            at: userPresetsURL,
            withIntermediateDirectories: true
        )

        loadAllPresets()

        // Load current preset from IPC
        let loadedPreset = IPCController.shared.getCurrentPreset()

        // Apply the loaded preset (ensures UI sync), or default to "Flat"
        if let preset = loadedPreset {
            applyPreset(preset)
        } else if let flatPreset = bundledPresets.first(where: { $0.name == "Flat" }) {
            applyPreset(flatPreset)
        }
    }

    /// Load all presets (bundled + user)
    func loadAllPresets() {
        bundledPresets = loadBundledPresets()
        userPresets = loadUserPresets()
        // Update cached values
        allPresets = bundledPresets + userPresets
        userPresetIDs = Set(userPresets.map { $0.id })
    }

    /// Load bundled presets (Flat curve created programmatically)
    private func loadBundledPresets() -> [EQPreset] {
        return [EQPreset.flat()]
    }

    /// Load user presets from UserDefaults
    private func loadUserPresets() -> [EQPreset] {
        print("[PresetManager] Loading user presets from UserDefaults...")
        guard let data = UserDefaults.standard.data(forKey: "KrishaCustomPresets") else {
            print("[PresetManager] No user presets found in UserDefaults")
            return []
        }
        do {
            let presets = try JSONDecoder().decode([EQPreset].self, from: data)
            print("[PresetManager] Loaded \(presets.count) user presets from UserDefaults")
            return presets.sorted { $0.name < $1.name }
        } catch {
            print("[PresetManager] ERROR decoding user presets from UserDefaults: \(error)")
            return []
        }
    }

    /// Save user preset to UserDefaults
    func savePreset(_ preset: EQPreset) throws {
        guard preset.isValid() else {
            throw PresetError.invalidPreset
        }

        var currentPresets = loadUserPresets()
        // Replace existing preset with the same ID or name, or append new one
        if let idx = currentPresets.firstIndex(where: { $0.id == preset.id || $0.name == preset.name }) {
            currentPresets[idx] = preset
        } else {
            currentPresets.append(preset)
        }

        let encoder = JSONEncoder()
        let data = try encoder.encode(currentPresets)
        UserDefaults.standard.set(data, forKey: "KrishaCustomPresets")
        print("[PresetManager] Saved preset '\(preset.name)' to UserDefaults")
        loadAllPresets()
    }

    /// Delete user preset from UserDefaults
    func deletePreset(_ preset: EQPreset) throws {
        var currentPresets = loadUserPresets()
        currentPresets.removeAll(where: { $0.id == preset.id || $0.name == preset.name })

        let encoder = JSONEncoder()
        let data = try encoder.encode(currentPresets)
        UserDefaults.standard.set(data, forKey: "KrishaCustomPresets")
        print("[PresetManager] Deleted preset '\(preset.name)' from UserDefaults")
        loadAllPresets()
    }

    /// Map preset bands to standard 10-band UI frequencies
    private func mapPresetToStandardBands(_ preset: EQPreset) -> MappedBandResult {
        var gains: [Float] = Array(repeating: 0, count: 10)
        var qFactors: [Float] = Array(repeating: 1.0, count: 10)
        var filterTypes: [FilterType] = Array(repeating: .peak, count: 10)
        var frequencies: [Float] = standardFrequencies
        var warnings: [String] = []

        // If preset has exactly 10 bands, use them directly by index (preserves custom frequencies)
        if preset.bands.count == 10 {
            for i in 0..<10 {
                let band = preset.bands[i]
                gains[i] = band.enabled ? band.gainDb : 0
                qFactors[i] = band.qFactor
                filterTypes[i] = band.filterType
                frequencies[i] = band.frequencyHz
            }
            return MappedBandResult(gains: gains, qFactors: qFactors, filterTypes: filterTypes, frequencies: frequencies, warnings: warnings)
        }

        // For presets with fewer bands, use log-distance matching to map to standard slots
        var usedBandIndices = Set<Int>()

        func logDistance(_ f1: Float, _ f2: Float) -> Float {
            return abs(log10(f1) - log10(f2))
        }

        let allBands = preset.bands.enumerated().map { ($0.offset, $0.element) }

        for i in 0..<10 {
            let targetFreq = standardFrequencies[i]

            var bestMatch: (index: Int, band: EQBand, distance: Float)?

            for (presetIdx, band) in allBands {
                if usedBandIndices.contains(presetIdx) {
                    continue
                }

                let distance = logDistance(band.frequencyHz, targetFreq)

                if bestMatch == nil || distance < bestMatch!.distance {
                    bestMatch = (presetIdx, band, distance)
                }
            }

            if let match = bestMatch {
                let maxToleranceOctaves: Float = 0.5
                let maxLogDistance = maxToleranceOctaves * log10(2)

                if match.distance <= maxLogDistance {
                    gains[i] = match.band.enabled ? match.band.gainDb : 0
                    qFactors[i] = match.band.qFactor
                    filterTypes[i] = match.band.filterType
                    frequencies[i] = match.band.frequencyHz
                    usedBandIndices.insert(match.index)
                } else {
                    let octaveDiff = match.distance / log10(2)
                    warnings.append(
                        "Band at \(Int(match.band.frequencyHz))Hz is \(String(format: "%.1f", octaveDiff)) octaves from \(Int(targetFreq))Hz slider - setting to 0dB"
                    )
                }
            }
        }

        let enabledBands = allBands.filter { $0.1.enabled }
        for (presetIdx, band) in enabledBands {
            if !usedBandIndices.contains(presetIdx) {
                warnings.append(
                    "Band at \(Int(band.frequencyHz))Hz (\(String(format: "%.1f", band.gainDb))dB) has no matching slider - ignored"
                )
            }
        }

        return MappedBandResult(gains: gains, qFactors: qFactors, filterTypes: filterTypes, frequencies: frequencies, warnings: warnings)
    }

    /// Apply preset via IPC
    func applyPreset(_ preset: EQPreset) {
        do {
            try IPCController.shared.applyPreset(preset)
            currentPreset = preset

            // Reset custom preset state
            isCustomPreset = false
            isEditingPresetName = false

            // Update all tracked state from preset
            let result = mapPresetToStandardBands(preset)
            currentBands = result.gains
            currentQFactors = result.qFactors
            currentFilterTypes = result.filterTypes
            currentFrequencies = result.frequencies
            currentPreampDb = preset.preampDb
            currentPreampLeftDb = preset.preampLeftDb
            currentPreampRightDb = preset.preampRightDb
            currentLimiterEnabled = preset.limiterEnabled
            currentLimiterThresholdDb = preset.limiterThresholdDb

            // Log any mapping issues
            if !result.warnings.isEmpty {
                print("[PresetManager] Preset '\(preset.name)' mapping warnings:")
                for warning in result.warnings {
                    print("  - \(warning)")
                }
            }
        } catch {
            print("Failed to apply preset: \(error)")
        }
    }

    /// Update a single band and apply immediately (batched to avoid multiple redraws)
    func updateBand(index: Int, gainDb: Float) {
        guard index >= 0 && index < 10 else { return }

        // Update band value without triggering @Published (direct array mutation)
        // We'll manually notify once at the end
        let needsCustomUpdate = !isCustomPreset && isEnabled
        let needsPresetClear = currentPreset != nil && isEnabled

        // Batch all state changes together
        currentBands[index] = gainDb

        // Only update these if needed (avoids redundant publishes during drag)
        if needsPresetClear {
            currentPreset = nil
        }
        if needsCustomUpdate {
            isCustomPreset = true
        }

        // Reset inactivity timer if this band is focused
        if focusedBandIndex != nil {
            resetInactivityTimer()
        }

        // Apply to audio (doesn't trigger UI updates)
        applyCurrentStateToAudio()
    }

    /// Round dB level to 1 decimal place (reflecting how dB values are formatted in the UI)
    private func roundDb(_ db: Float) -> Float { (db * 10).rounded() / 10 }

    /// Apply current state to audio, throttled to prevent IPC flooding during rapid updates
    private func applyCurrentStateToAudio() {
        // Throttle: coalesce rapid calls (drag, scroll) into one IPC write per ~30ms
        audioApplyTimer?.invalidate()
        audioApplyTimer = Timer.scheduledTimer(withTimeInterval: 0.05, repeats: false) { [weak self] _ in
            self?.doApplyCurrentStateToAudio()
        }
    }

    private func doApplyCurrentStateToAudio() {
        let enabled = isEnabled
        let bands = currentFrequencies.enumerated().map { index, frequency in
            let gain = enabled ? currentBands[index] : 0.0
            return EQBand(
                frequencyHz: frequency,
                gainDb: roundDb(gain),
                qFactor: currentQFactors[index],
                filterType: currentFilterTypes[index],
                enabled: abs(gain) > 0.01
            )
        }

        // When disabled, force neutral global settings to ensure a true bypass.
        let preampDb = enabled ? currentPreampDb : 0.0
        let preampLeftDb = enabled ? currentPreampLeftDb : 0.0
        let preampRightDb = enabled ? currentPreampRightDb : 0.0
        let limiterEnabled = enabled ? currentLimiterEnabled : false
        let limiterThresholdDb = enabled ? currentLimiterThresholdDb : 0.0

        let customPreset = EQPreset(
            name: "Custom",
            bands: bands,
            preampDb: preampDb,
            preampLeftDb: preampLeftDb,
            preampRightDb: preampRightDb,
            limiterEnabled: limiterEnabled,
            limiterThresholdDb: limiterThresholdDb
        )

        do {
            try IPCController.shared.applyPreset(customPreset)
        } catch {
            print("Failed to apply current state: \(error)")
        }
    }

    /// Apply current state (either enabled with current bands, or disabled with all zeros)
    func applyCurrentState() {
        if isEnabled {
            currentPreset = nil
            isCustomPreset = true
        }
        applyCurrentStateToAudio()
    }

    /// Toggle EQ enabled state
    func toggleEnabled() {
        isEnabled.toggle()
        applyCurrentState()
    }

    // MARK: - Band Advanced Settings

    /// Update Q factor for a specific band
    func updateBandQ(index: Int, qFactor: Float) {
        guard index >= 0 && index < 10 else { return }
        currentQFactors[index] = max(0.1, min(10.0, qFactor))
        markCustomIfNeeded()
        resetInactivityTimer()
        applyCurrentStateToAudio()
    }

    /// Update filter type for a specific band
    func updateBandFilterType(index: Int, filterType: FilterType) {
        guard index >= 0 && index < 10 else { return }
        currentFilterTypes[index] = filterType
        markCustomIfNeeded()
        resetInactivityTimer()
        applyCurrentStateToAudio()
    }

    /// Update frequency for a specific band
    func updateBandFrequency(index: Int, frequencyHz: Float) {
        guard index >= 0 && index < 10 else { return }
        currentFrequencies[index] = max(20, min(20000, frequencyHz))
        markCustomIfNeeded()
        resetInactivityTimer()
        applyCurrentStateToAudio()
    }

    // MARK: - Global Settings

    /// Update preamp gain
    func updatePreamp(gainDb: Float) {
        currentPreampDb = max(-12.0, min(12.0, gainDb))
        currentPreampLeftDb = currentPreampDb
        currentPreampRightDb = currentPreampDb
        markCustomIfNeeded()
        if focusedBandIndex != nil {
            resetInactivityTimer()
        }
        applyCurrentStateToAudio()
    }

    /// Update left preamp gain
    func updatePreampLeft(gainDb: Float) {
        currentPreampLeftDb = max(-12.0, min(12.0, gainDb))
        markCustomIfNeeded()
        if focusedBandIndex != nil {
            resetInactivityTimer()
        }
        applyCurrentStateToAudio()
    }

    /// Update right preamp gain
    func updatePreampRight(gainDb: Float) {
        currentPreampRightDb = max(-12.0, min(12.0, gainDb))
        markCustomIfNeeded()
        if focusedBandIndex != nil {
            resetInactivityTimer()
        }
        applyCurrentStateToAudio()
    }

    /// Update limiter enabled state
    func updateLimiterEnabled(_ enabled: Bool) {
        currentLimiterEnabled = enabled
        markCustomIfNeeded()
        resetInactivityTimer()
        applyCurrentStateToAudio()
    }

    /// Update limiter threshold
    func updateLimiterThreshold(db: Float) {
        currentLimiterThresholdDb = max(-6.0, min(0.0, db))
        markCustomIfNeeded()
        resetInactivityTimer()
        applyCurrentStateToAudio()
    }

    // MARK: - Focus Mode

    /// Toggle focus on a band (double-click behavior)
    func toggleFocusedBand(_ index: Int) {
        if focusedBandIndex == index {
            focusedBandIndex = nil
            cancelInactivityTimer()
        } else {
            focusedBandIndex = index
            resetInactivityTimer()
        }
    }

    /// Set focused band directly (used for unfocusing on empty space click)
    func setFocusedBand(_ index: Int?) {
        focusedBandIndex = index
        if index != nil {
            resetInactivityTimer()
        } else {
            cancelInactivityTimer()
        }
    }

    /// Reset the inactivity timer (no-op, timeout removed)
    func resetInactivityTimer() {
        // No timeout - band stays focused until user clicks elsewhere
    }

    /// Cancel the inactivity timer (no-op, timeout removed)
    func cancelInactivityTimer() {
        // No timeout to cancel
    }


    // MARK: - Custom Preset Management

    /// Mark as custom preset if a saved preset was active
    private func markCustomIfNeeded() {
        if currentPreset != nil && isEnabled {
            currentPreset = nil
        }
        if !isCustomPreset && isEnabled {
            isCustomPreset = true
        }
    }

    /// Validate preset name for saving
    func validatePresetName(_ name: String) -> Bool {
        let trimmed = name.trimmingCharacters(in: .whitespaces)

        // Check not empty
        guard !trimmed.isEmpty else { return false }

        // Check not too long
        guard trimmed.count <= 64 else { return false }

        // Check not reserved name
        guard trimmed != Self.customPresetName else { return false }

        return true
    }

    /// Generate a unique preset name by appending numbers if needed
    func generateUniqueName(_ baseName: String) -> String {
        let allPresetNames = Set((bundledPresets + userPresets).map { $0.name })

        // If name doesn't exist, return as-is
        if !allPresetNames.contains(baseName) {
            return baseName
        }

        // Find next available number
        var counter = 2
        var candidateName = "\(baseName) \(counter)"

        while allPresetNames.contains(candidateName) {
            counter += 1
            candidateName = "\(baseName) \(counter)"
        }

        return candidateName
    }

    /// Save current EQ state as a custom preset
    @MainActor
    func saveCustomPreset(name: String) async throws {
        let trimmedName = name.trimmingCharacters(in: .whitespaces)

        // Validate
        guard validatePresetName(trimmedName) else {
            throw PresetError.invalidPreset
        }

        // Generate unique name if needed
        let finalName = generateUniqueName(trimmedName)

        // Build preset from current bands (preserving advanced settings)
        let bands: [EQBand] = currentFrequencies.enumerated().map { index, frequency in
            let gain = currentBands[index]
            return EQBand(
                frequencyHz: frequency,
                gainDb: gain,
                qFactor: currentQFactors[index],
                filterType: currentFilterTypes[index],
                enabled: abs(gain) > 0.01
            )
        }

        let newPreset = EQPreset(
            name: finalName,
            bands: bands,
            preampDb: currentPreampDb,
            preampLeftDb: currentPreampLeftDb,
            preampRightDb: currentPreampRightDb,
            limiterEnabled: currentLimiterEnabled,
            limiterThresholdDb: currentLimiterThresholdDb
        )

        // Save to disk (this reloads all presets, creating new instances)
        try savePreset(newPreset)

        // Find the newly loaded version of the preset from userPresets
        // (savePreset reloads all presets with new UUIDs, so we need to find by name)
        if let reloadedPreset = userPresets.first(where: { $0.name == finalName }) {
            currentPreset = reloadedPreset
        } else {
            currentPreset = newPreset  // Fallback (shouldn't happen)
        }

        isCustomPreset = false
        isEditingPresetName = false
    }

    /// Cancel editing mode
    func cancelEditing() {
        isEditingPresetName = false
    }

    /// Sanitize filename (remove invalid characters)
    private func sanitizeFilename(_ name: String) -> String {
        var safe = name.replacingOccurrences(of: "/", with: "-")
            .replacingOccurrences(of: ":", with: "-")
            .replacingOccurrences(of: "\\", with: "-")

        if safe.count > 64 {
            safe = String(safe.prefix(64))
        }

        return safe.trimmingCharacters(in: .whitespaces)
    }

    /// Parse a standard AutoEq ParametricEQ.txt stream in-memory using frozen C++ core parser
    func parseAutoEqText(_ text: String) -> EQPreset? {
        var cPreset = krisha_preset_t()
        krisha_dsp_preset_init_flat(&cPreset)

        let result = text.withCString { cStr in
            krisha_preset_parse_autoeq(cStr, &cPreset)
        }

        guard result == KRISHA_OK else {
            print("[PresetManager] C++ AutoEq parser failed with code: \(result.rawValue)")
            return nil
        }

        var bands: [EQBand] = []
        withUnsafePointer(to: cPreset.bands) { bandsPtr in
            let rawPtr = UnsafeRawPointer(bandsPtr)
            let bandsArray = rawPtr.assumingMemoryBound(to: krisha_band_t.self)
            for i in 0..<Int(cPreset.num_bands) {
                let band = bandsArray[i]
                let filterType = FilterType(rawValue: Int(band.type.rawValue)) ?? .peak
                bands.append(EQBand(
                    frequencyHz: band.frequency_hz,
                    gainDb: band.gain_db,
                    qFactor: band.q_factor,
                    filterType: filterType,
                    enabled: band.enabled
                ))
            }
        }

        let name = withUnsafePointer(to: cPreset.name) { ptr in
            ptr.withMemoryRebound(to: CChar.self, capacity: 64) { cStr in
                String(cString: cStr)
            }
        }

        return EQPreset(
            name: name.isEmpty ? "AutoEq Preset" : name,
            bands: bands,
            preampDb: cPreset.preamp_db,
            preampLeftDb: cPreset.preamp_left_db,
            preampRightDb: cPreset.preamp_right_db,
            limiterEnabled: cPreset.limiter_enabled,
            limiterThresholdDb: cPreset.limiter_threshold_db
        )
    }

    /// Compute magnitude response for Left or Right channel over a custom list of frequencies
    func getMagnitudeResponse(frequencies: [Float], leftChannel: Bool) -> [Float] {
        guard let offlineEngine = krisha_dsp_create(48000) else {
            return Array(repeating: 0.0, count: frequencies.count)
        }
        defer {
            krisha_dsp_destroy(offlineEngine)
        }

        var preset = krisha_preset_t()
        krisha_dsp_preset_init_flat(&preset)

        preset.num_bands = 10
        preset.preamp_db = currentPreampDb
        preset.preamp_left_db = currentPreampLeftDb
        preset.preamp_right_db = currentPreampRightDb
        preset.limiter_enabled = currentLimiterEnabled
        preset.limiter_threshold_db = currentLimiterThresholdDb

        withUnsafeMutablePointer(to: &preset.bands) { bandsPtr in
            let rawPtr = UnsafeMutableRawPointer(bandsPtr)
            let bands = rawPtr.assumingMemoryBound(to: krisha_band_t.self)
            for i in 0..<10 {
                let gain = isEnabled ? currentBands[i] : 0.0
                bands[i].frequency_hz = currentFrequencies[i]
                bands[i].gain_db = gain
                bands[i].q_factor = currentQFactors[i]
                bands[i].type = krisha_filter_type_t(UInt32(currentFilterTypes[i].rawValue))
                bands[i].enabled = isEnabled && (abs(gain) > 0.01)
            }
        }

        let applyResult = krisha_dsp_apply_preset(offlineEngine, &preset)
        guard applyResult == KRISHA_OK else {
            return Array(repeating: 0.0, count: frequencies.count)
        }

        return frequencies.map { freq in
            krisha_dsp_get_magnitude_at_frequency(offlineEngine, freq, leftChannel)
        }
    }
}
