import SwiftUI
import Combine
import CKrishaDSP

class LiveEQGraphViewModel: ObservableObject {
    @Published var finalPathPoints: [CGPoint] = []
    @Published var harmanPathPoints: [CGPoint] = []
    @Published var rawPathPoints: [CGPoint] = []
    @Published var eqPathPoints: [CGPoint] = []
    
    private var cancellables = Set<AnyCancellable>()
    private let calculationQueue = DispatchQueue(label: "com.krisha.eq-graph-calc", qos: .userInteractive)
    
    init(presetManager: PresetManager) {
        // Observe all published values in PresetManager that affect the curve
        Publishers.MergeMany(
            presetManager.$isEnabled.map { _ in () }.eraseToAnyPublisher(),
            presetManager.$currentPreset.map { _ in () }.eraseToAnyPublisher(),
            presetManager.$currentBands.map { _ in () }.eraseToAnyPublisher(),
            presetManager.$currentQFactors.map { _ in () }.eraseToAnyPublisher(),
            presetManager.$currentFilterTypes.map { _ in () }.eraseToAnyPublisher(),
            presetManager.$currentFrequencies.map { _ in () }.eraseToAnyPublisher(),
            presetManager.$currentPreampDb.map { _ in () }.eraseToAnyPublisher(),
            presetManager.$currentPreampLeftDb.map { _ in () }.eraseToAnyPublisher(),
            presetManager.$currentPreampRightDb.map { _ in () }.eraseToAnyPublisher()
        )
        .debounce(for: .milliseconds(16), scheduler: RunLoop.main) // ~60fps throttle
        .sink { [weak self, weak presetManager] _ in
            guard let self = self, let pm = presetManager else { return }
            self.recalculateCurve(presetManager: pm)
        }
        .store(in: &cancellables)
        
        // Initial calculation
        recalculateCurve(presetManager: presetManager)
    }
    
    // Analog-style soft-clamping boundaries helper
    private func softClamp(_ db: Float) -> Float {
        let maxVal: Float = 12.0
        let minVal: Float = -12.0
        if db > maxVal {
            return maxVal + 2.0 * tanh((db - maxVal) / 2.0)
        } else if db < minVal {
            return minVal + 2.0 * tanh((db - minVal) / 2.0)
        }
        return db
    }
    
    func recalculateCurve(presetManager: PresetManager) {
        // Take copies of all parameters to prevent cross-thread access crashes
        let isEnabled = presetManager.isEnabled
        let bands = presetManager.currentBands
        let qFactors = presetManager.currentQFactors
        let filterTypes = presetManager.currentFilterTypes
        let frequencies = presetManager.currentFrequencies
        let preampDb = presetManager.currentPreampDb
        let preampLeftDb = presetManager.currentPreampLeftDb
        let preampRightDb = presetManager.currentPreampRightDb
        let limiterEnabled = presetManager.currentLimiterEnabled
        let limiterThresholdDb = presetManager.currentLimiterThresholdDb
        
        // Take a copy of the active preset to evaluate unmodified baseline targets
        let activePreset = presetManager.currentPreset
        
        calculationQueue.async { [weak self] in
            guard let self = self else { return }
            
            // Build the logarithmic list of 120 frequency steps from 20Hz to 20kHz
            let minFreq: Float = 20.0
            let maxFreq: Float = 20000.0
            let logMin = log10(minFreq)
            let logMax = log10(maxFreq)
            
            var testFrequencies: [Float] = []
            for i in 0..<120 {
                let ratio = Float(i) / 119.0
                let logFreq = logMin + ratio * (logMax - logMin)
                testFrequencies.append(pow(10.0, logFreq))
            }
            
            // Instantiate transient active and target engines
            guard let activeEngine = krisha_dsp_create(48000) else { return }
            guard let targetEngine = krisha_dsp_create(48000) else {
                krisha_dsp_destroy(activeEngine)
                return
            }
            defer {
                krisha_dsp_destroy(activeEngine)
                krisha_dsp_destroy(targetEngine)
            }
            
            // 1. Configure the Active EQ Engine
            var activeCPreset = krisha_preset_t()
            krisha_dsp_preset_init_flat(&activeCPreset)
            activeCPreset.num_bands = 10
            activeCPreset.preamp_db = preampDb
            activeCPreset.preamp_left_db = preampLeftDb
            activeCPreset.preamp_right_db = preampRightDb
            activeCPreset.limiter_enabled = limiterEnabled
            activeCPreset.limiter_threshold_db = limiterThresholdDb
            
            withUnsafeMutablePointer(to: &activeCPreset.bands) { bandsPtr in
                let rawPtr = UnsafeMutableRawPointer(bandsPtr)
                let bandsArray = rawPtr.assumingMemoryBound(to: krisha_band_t.self)
                for i in 0..<10 {
                    let gain = isEnabled ? bands[i] : 0.0
                    bandsArray[i].frequency_hz = frequencies[i]
                    bandsArray[i].gain_db = gain
                    bandsArray[i].q_factor = qFactors[i]
                    bandsArray[i].type = krisha_filter_type_t(UInt32(filterTypes[i].rawValue))
                    bandsArray[i].enabled = isEnabled && (abs(gain) > 0.01)
                }
            }
            krisha_dsp_apply_preset(activeEngine, &activeCPreset)
            
            // 2. Configure the Baseline Target EQ Engine (using optimal loaded configuration parameters)
            if let targetPreset = activePreset {
                var targetCPreset = krisha_preset_t()
                krisha_dsp_preset_init_flat(&targetCPreset)
                targetCPreset.num_bands = UInt32(targetPreset.bands.count)
                targetCPreset.preamp_db = targetPreset.preampDb
                targetCPreset.preamp_left_db = targetPreset.preampLeftDb
                targetCPreset.preamp_right_db = targetPreset.preampRightDb
                targetCPreset.limiter_enabled = targetPreset.limiterEnabled
                targetCPreset.limiter_threshold_db = targetPreset.limiterThresholdDb
                
                withUnsafeMutablePointer(to: &targetCPreset.bands) { bandsPtr in
                    let rawPtr = UnsafeMutableRawPointer(bandsPtr)
                    let bandsArray = rawPtr.assumingMemoryBound(to: krisha_band_t.self)
                    for i in 0..<Int(targetCPreset.num_bands) {
                        let band = targetPreset.bands[i]
                        bandsArray[i].frequency_hz = band.frequencyHz
                        bandsArray[i].gain_db = band.enabled ? band.gainDb : 0.0
                        bandsArray[i].q_factor = band.qFactor
                        bandsArray[i].type = krisha_filter_type_t(UInt32(band.filterType.rawValue))
                        bandsArray[i].enabled = band.enabled
                    }
                }
                krisha_dsp_apply_preset(targetEngine, &targetCPreset)
            }
            
            var finalPoints: [CGPoint] = []
            var harmanPoints: [CGPoint] = []
            var rawPoints: [CGPoint] = []
            var eqPoints: [CGPoint] = []
            
            for i in 0..<120 {
                let freq = testFrequencies[i]
                
                // Fetch the core baseline target
                let harmanDb = krisha_dsp_get_harman_target_at_frequency(freq)
                
                // Fetch active filter magnitude response
                let eqDb = krisha_dsp_get_magnitude_at_frequency(activeEngine, freq, true)
                
                // Fetch unmodified optimal target preset response
                let targetEqDb = (activePreset != nil) ? krisha_dsp_get_magnitude_at_frequency(targetEngine, freq, true) : 0.0
                
                // Predicted raw response of the headphone: Raw = Harman - EQ_target
                let rawDb = harmanDb - targetEqDb
                
                // Predicted equalized response of the headphone: Final = Raw + EQ_active
                let finalDb = rawDb + eqDb
                
                let x = CGFloat(i) / 119.0
                
                // Apply soft visual boundary clamping to prevent flatlining coordinates at extremes
                let yFinal = CGFloat((12.0 - self.softClamp(finalDb)) / 24.0)
                let yHarman = CGFloat((12.0 - self.softClamp(harmanDb)) / 24.0)
                let yRaw = CGFloat((12.0 - self.softClamp(rawDb)) / 24.0)
                let yEq = CGFloat((12.0 - self.softClamp(eqDb)) / 24.0)
                
                finalPoints.append(CGPoint(x: x, y: yFinal))
                harmanPoints.append(CGPoint(x: x, y: yHarman))
                rawPoints.append(CGPoint(x: x, y: yRaw))
                eqPoints.append(CGPoint(x: x, y: yEq))
            }
            
            DispatchQueue.main.async {
                self.finalPathPoints = finalPoints
                self.harmanPathPoints = harmanPoints
                self.rawPathPoints = rawPoints
                self.eqPathPoints = eqPoints
            }
        }
    }
}

struct LiveEQGraph: View {
    @ObservedObject var presetManager: PresetManager
    @StateObject private var viewModel: LiveEQGraphViewModel
    
    @State private var hoverLocation: CGPoint? = nil
    
    init(presetManager: PresetManager) {
        self.presetManager = presetManager
        self._viewModel = StateObject(wrappedValue: LiveEQGraphViewModel(presetManager: presetManager))
    }
    
    var body: some View {
        VStack(spacing: 4) {
            GeometryReader { geo in
                let size = geo.size
                let gridWidth = size.width - 45
                let gridHeight = size.height - 20
                
                ZStack(alignment: .topLeading) {
                    // 1. Grid Background & Curves (inside active area, clipped)
                    ZStack {
                        // Grid background lines
                        gridBackground(size: CGSize(width: gridWidth, height: gridHeight))
                        
                        // Line 3: Raw Response (Ultra-thin path in white with low-opacity)
                        curvePath(points: viewModel.rawPathPoints, size: CGSize(width: gridWidth, height: gridHeight))
                            .stroke(Color.white.opacity(0.20), lineWidth: 0.75)
                        
                        // Line 4: Equalizer Filter (Ultra-thin path in white with low-opacity)
                        curvePath(points: viewModel.eqPathPoints, size: CGSize(width: gridWidth, height: gridHeight))
                            .stroke(Color.white.opacity(0.20), lineWidth: 0.75)
                        
                        // Line 2: Target Curve (Thin, solid highly defined white reference line)
                        curvePath(points: viewModel.harmanPathPoints, size: CGSize(width: gridWidth, height: gridHeight))
                            .stroke(Color.white.opacity(0.60), lineWidth: 1.25)
                        
                        // Line 1: Final Equalized Result (Solid accent blue curve)
                        curvePath(points: viewModel.finalPathPoints, size: CGSize(width: gridWidth, height: gridHeight))
                            .stroke(Color(red: 0.0, green: 0.48, blue: 1.0), lineWidth: 2.0)
                    }
                    .frame(width: gridWidth, height: gridHeight)
                    .background(
                        RoundedRectangle(cornerRadius: 4)
                            .fill(Color(white: 0.05))
                            .overlay(
                                RoundedRectangle(cornerRadius: 4)
                                    .stroke(Color.white.opacity(0.12), lineWidth: 1)
                            )
                    )
                    .clipped()
                    .offset(x: 45, y: 0)
                    
                    // 2. Y-Axis labels in left gutter [0, 45]
                    yAxisLabels(size: size)
                    
                    // 3. X-Axis labels in bottom gutter [0, 20]
                    xAxisLabels(size: size)
                    
                    // Hover Tooltip Marker & Text Overlay
                    if let location = hoverLocation {
                        hoverOverlay(location: location, size: size)
                    }
                }
                .contentShape(Rectangle())
                .gesture(
                    DragGesture(minimumDistance: 0)
                        .onChanged { val in
                            if val.location.x >= 45 && val.location.x <= size.width &&
                                val.location.y >= 0 && val.location.y <= size.height - 20 {
                                hoverLocation = val.location
                            } else {
                                hoverLocation = nil
                            }
                        }
                        .onEnded { _ in
                            hoverLocation = nil
                        }
                )
            }
            .frame(height: 150)
            
            // Minimalist Instrument-Grade Legend HUD Row
            HStack(spacing: 16) {
                HStack(spacing: 5) {
                    Circle().fill(Color(red: 0.0, green: 0.48, blue: 1.0)).frame(width: 6, height: 6)
                    Text("Equalized")
                        .font(.system(size: 9, weight: .regular))
                        .tracking(1.0)
                        .foregroundColor(.white.opacity(0.8))
                }
                
                HStack(spacing: 5) {
                    Circle().fill(Color.white.opacity(0.60)).frame(width: 6, height: 6)
                    Text("Target")
                        .font(.system(size: 9, weight: .regular))
                        .tracking(1.0)
                        .foregroundColor(.white.opacity(0.5))
                }
                
                HStack(spacing: 5) {
                    Circle().stroke(Color.white.opacity(0.20), lineWidth: 1.0).frame(width: 6, height: 6)
                    Text("Raw / Filter")
                        .font(.system(size: 9, weight: .regular))
                        .tracking(1.0)
                        .foregroundColor(.white.opacity(0.5))
                }
            }
            .padding(.top, 4)
        }
    }
    
    // Draw vector path from standardized points
    private func curvePath(points: [CGPoint], size: CGSize) -> Path {
        var path = Path()
        guard points.count >= 2 else { return path }
        
        let first = CGPoint(x: points[0].x * size.width, y: points[0].y * size.height)
        path.move(to: first)
        
        for i in 1..<points.count {
            let pt = CGPoint(x: points[i].x * size.width, y: points[i].y * size.height)
            path.addLine(to: pt)
        }
        
        return path
    }
    
    // Sleek logarithmic vertical & horizontal grids (drawn inside gridWidth/gridHeight)
    @ViewBuilder
    private func gridBackground(size: CGSize) -> some View {
        ZStack {
            // Horizontal lines (dB Steps: +12, +6, 0, -6, -12)
            ForEach([12, 6, 0, -6, -12], id: \.self) { db in
                let yRatio = CGFloat((12.0 - Double(db)) / 24.0)
                let yVal = yRatio * size.height
                
                Path { path in
                    path.move(to: CGPoint(x: 0, y: yVal))
                    path.addLine(to: CGPoint(x: size.width, y: yVal))
                }
                .stroke(Color.white.opacity(0.12),
                        style: StrokeStyle(lineWidth: 1, dash: db == 0 ? [] : [2, 3]))
            }
            
            // Logarithmic vertical grid lines (20Hz, 100Hz, 1kHz, 10kHz, 20kHz are major lines)
            // (50Hz, 200Hz, 500Hz, 2kHz, 5kHz are intermediate lines)
            let keyFrequencies: [Float] = [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000]
            ForEach(keyFrequencies, id: \.self) { freq in
                let logMin = log10(20.0)
                let logMax = log10(20000.0)
                let ratio = CGFloat((log10(Double(freq)) - logMin) / (logMax - logMin))
                
                Path { path in
                    path.move(to: CGPoint(x: ratio * size.width, y: 0))
                    path.addLine(to: CGPoint(x: ratio * size.width, y: size.height))
                }
                .stroke(Color.white.opacity(0.12),
                        style: StrokeStyle(lineWidth: 1, dash: freq == 20 || freq == 100 || freq == 1000 || freq == 10000 || freq == 20000 ? [] : [4, 4]))
            }
        }
    }
    
    // Draw the Y-axis labels in the left gutter [0, 45] right-aligned to a clean axis line
    @ViewBuilder
    private func yAxisLabels(size: CGSize) -> some View {
        let gridHeight = size.height - 20
        ZStack {
            Path { path in
                path.move(to: CGPoint(x: 45, y: 0))
                path.addLine(to: CGPoint(x: 45, y: gridHeight))
            }
            .stroke(Color.white.opacity(0.12), lineWidth: 1)
            
            ForEach([12, 6, 0, -6, -12], id: \.self) { db in
                let yRatio = CGFloat((12.0 - Double(db)) / 24.0)
                let yVal = yRatio * gridHeight
                
                Text("\(db > 0 ? "+" : "")\(db)dB")
                    .font(.system(size: 8, weight: .regular))
                    .foregroundColor(.white.opacity(0.4))
                    .frame(width: 40, alignment: .trailing)
                    .position(x: 20, y: yVal)
            }
        }
    }
    
    // Draw the X-axis labels in the bottom gutter [0, 20] centered under grid lines
    @ViewBuilder
    private func xAxisLabels(size: CGSize) -> some View {
        let gridWidth = size.width - 45
        let majorFrequencies: [(Float, String)] = [
            (20, "20Hz"),
            (100, "100Hz"),
            (1000, "1kHz"),
            (10000, "10kHz"),
            (20000, "20kHz")
        ]
        
        ZStack {
            ForEach(majorFrequencies, id: \.0) { freq, label in
                let logMin = log10(20.0)
                let logMax = log10(20000.0)
                let ratio = CGFloat((log10(Double(freq)) - logMin) / (logMax - logMin))
                let xVal = 45 + ratio * gridWidth
                
                Text(label)
                    .font(.system(size: 8, weight: .regular))
                    .foregroundColor(.white.opacity(0.4))
                    .position(x: xVal, y: size.height - 10)
            }
        }
    }
    
    // Interactive hover helper
    @ViewBuilder
    private func hoverOverlay(location: CGPoint, size: CGSize) -> some View {
        let gridWidth = size.width - 45
        let gridHeight = size.height - 20
        let xRatio = max(0, min(1.0, (location.x - 45) / gridWidth))
        
        // Convert xRatio back to log frequency
        let logMin = log10(20.0)
        let logMax = log10(20000.0)
        let logFreq = Double(logMin) + Double(xRatio) * (Double(logMax) - Double(logMin))
        let freq = pow(10.0, logFreq)
        
        // Find nearest point index in viewModel
        let index = min(119, max(0, Int(xRatio * 119)))
        
        let finalDb = viewModel.finalPathPoints.indices.contains(index) ? 12.0 - Double(viewModel.finalPathPoints[index].y * 24.0) : 0.0
        let harmanDb = viewModel.harmanPathPoints.indices.contains(index) ? 12.0 - Double(viewModel.harmanPathPoints[index].y * 24.0) : 0.0
        
        ZStack {
            // vertical guide line inside the active area
            Path { path in
                path.move(to: CGPoint(x: location.x, y: 0))
                path.addLine(to: CGPoint(x: location.x, y: gridHeight))
            }
            .stroke(Color.white.opacity(0.15), lineWidth: 1)
            
            // Dot for Harman target (using mapped coordinate space)
            if viewModel.harmanPathPoints.indices.contains(index) {
                Circle()
                    .fill(Color.white.opacity(0.60))
                    .frame(width: 5, height: 5)
                    .position(x: location.x, y: viewModel.harmanPathPoints[index].y * gridHeight)
            }
            
            // Dot for Final sound signature
            if viewModel.finalPathPoints.indices.contains(index) {
                Circle()
                    .fill(Color(red: 0.0, green: 0.48, blue: 1.0))
                    .frame(width: 5, height: 5)
                    .position(x: location.x, y: viewModel.finalPathPoints[index].y * gridHeight)
            }
            
            // HUD panel displaying exact statistics
            VStack(alignment: .leading, spacing: 2) {
                Text("\(Int(freq)) Hz")
                    .font(.system(size: 10, weight: .bold))
                    .foregroundColor(.white)
                
                HStack(spacing: 6) {
                    Circle().fill(Color(red: 0.0, green: 0.48, blue: 1.0)).frame(width: 4, height: 4)
                    Text("Final: \(String(format: "%.1f", finalDb)) dB")
                        .font(.system(size: 9, weight: .regular))
                        .foregroundColor(Color(red: 0.0, green: 0.48, blue: 1.0))
                }
                
                HStack(spacing: 6) {
                    Circle().fill(Color.white.opacity(0.5)).frame(width: 4, height: 4)
                    Text("Target: \(String(format: "%.1f", harmanDb)) dB")
                        .font(.system(size: 9, weight: .regular))
                        .foregroundColor(.white.opacity(0.7))
                }
            }
            .padding(6)
            .background(
                RoundedRectangle(cornerRadius: 6)
                    .fill(Color(white: 0.05).opacity(0.95))
                    .overlay(RoundedRectangle(cornerRadius: 6).stroke(Color.white.opacity(0.1), lineWidth: 0.5))
            )
            .position(x: location.x + (location.x > size.width * 0.7 ? -50 : 50), y: 35)
        }
    }
}
