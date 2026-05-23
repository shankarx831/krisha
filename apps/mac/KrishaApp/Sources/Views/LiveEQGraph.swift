import SwiftUI
import Combine
import CKrishaDSP

class LiveEQGraphViewModel: ObservableObject {
    @Published var activePathPoints: [CGPoint] = []
    @Published var harmanPathPoints: [CGPoint] = []
    
    private var cancellables = Set<AnyCancellable>()
    private let calculationQueue = DispatchQueue(label: "com.krisha.eq-graph-calc", qos: .userInteractive)
    
    init(presetManager: PresetManager) {
        // Observe all published values in PresetManager that affect the curve
        // Combine multiple publishers to trigger recalculation on any change
        Publishers.MergeMany(
            presetManager.$isEnabled.map { _ in () }.eraseToAnyPublisher(),
            presetManager.$currentBands.map { _ in () }.eraseToAnyPublisher(),
            presetManager.$currentQFactors.map { _ in () }.eraseToAnyPublisher(),
            presetManager.$currentFilterTypes.map { _ in () }.eraseToAnyPublisher(),
            presetManager.$currentFrequencies.map { _ in () }.eraseToAnyPublisher(),
            presetManager.$currentPreampDb.map { _ in () }.eraseToAnyPublisher(),
            presetManager.$currentPreampLeftDb.map { _ in () }.eraseToAnyPublisher(),
            presetManager.$currentPreampRightDb.map { _ in () }.eraseToAnyPublisher()
        )
        .debounce(for: .milliseconds(16), scheduler: RunLoop.main) // ~60fps throttle for ultra-efficiency
        .sink { [weak self, weak presetManager] _ in
            guard let self = self, let pm = presetManager else { return }
            self.recalculateCurve(presetManager: pm)
        }
        .store(in: &cancellables)
        
        // Initial calculation
        recalculateCurve(presetManager: presetManager)
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
            
            // Instantiate transient offline DSP engine to evaluate the mathematical curve
            guard let offlineEngine = krisha_dsp_create(48000) else { return }
            defer {
                krisha_dsp_destroy(offlineEngine)
            }
            
            var preset = krisha_preset_t()
            krisha_dsp_preset_init_flat(&preset)
            preset.num_bands = 10
            preset.preamp_db = preampDb
            preset.preamp_left_db = preampLeftDb
            preset.preamp_right_db = preampRightDb
            preset.limiter_enabled = limiterEnabled
            preset.limiter_threshold_db = limiterThresholdDb
            
            withUnsafeMutablePointer(to: &preset.bands) { bandsPtr in
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
            
            let applyResult = krisha_dsp_apply_preset(offlineEngine, &preset)
            guard applyResult == KRISHA_OK else { return }
            
            var activePoints: [CGPoint] = []
            var harmanPoints: [CGPoint] = []
            
            for i in 0..<120 {
                let freq = testFrequencies[i]
                
                // Vector A: Active signature (Left channel magnitude response)
                let activeDb = krisha_dsp_get_magnitude_at_frequency(offlineEngine, freq, true)
                // Vector B: Harman reference baseline magnitude
                let harmanDb = krisha_dsp_get_harman_target_at_frequency(freq)
                
                // x is standardized in [0, 1] range representing log frequency
                let x = CGFloat(i) / 119.0
                
                // Cap DB between -12dB and +12dB
                let capActiveDb = max(-12.0, min(12.0, activeDb))
                let capHarmanDb = max(-12.0, min(12.0, harmanDb))
                
                // y is standardized in [0, 1] range where 0 is +12dB and 1 is -12dB
                let yActive = CGFloat((12.0 - capActiveDb) / 24.0)
                let yHarman = CGFloat((12.0 - capHarmanDb) / 24.0)
                
                activePoints.append(CGPoint(x: x, y: yActive))
                harmanPoints.append(CGPoint(x: x, y: yHarman))
            }
            
            DispatchQueue.main.async {
                self.activePathPoints = activePoints
                self.harmanPathPoints = harmanPoints
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
            // Draw coordinate chart
            GeometryReader { geo in
                ZStack {
                    // Minimal grid background
                    gridBackground(size: geo.size)
                    
                    // Line B: Harman Reference Baseline (Muted Fine Dashed Translucent Gray/White)
                    curvePath(points: viewModel.harmanPathPoints, size: geo.size)
                        .stroke(Color.white.opacity(0.15), style: StrokeStyle(lineWidth: 1.5, dash: [4, 4]))
                    
                    // Line A: Active Sound Signature ( Crisp System Blue Accent)
                    curvePath(points: viewModel.activePathPoints, size: geo.size)
                        .stroke(Color.blue, lineWidth: 1.5)
                    
                    // Hover Tooltip Marker & Text Overlay
                    if let location = hoverLocation {
                        hoverOverlay(location: location, size: geo.size)
                    }
                }
                .background(
                    RoundedRectangle(cornerRadius: 8)
                        .fill(Color(white: 0.05))
                        .overlay(
                            RoundedRectangle(cornerRadius: 8)
                                .stroke(Color.white.opacity(0.08), lineWidth: 1)
                        )
                )
                .contentShape(Rectangle())
                .gesture(
                    DragGesture(minimumDistance: 0)
                        .onChanged { val in
                            if val.location.x >= 0 && val.location.x <= geo.size.width &&
                                val.location.y >= 0 && val.location.y <= geo.size.height {
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
            .frame(height: 130)
            
            // X-axis Legend labels (Frequency steps)
            HStack {
                Text("20Hz").font(.system(size: 9, weight: .regular)).foregroundColor(.white.opacity(0.4))
                Spacer()
                Text("100Hz").font(.system(size: 9, weight: .regular)).foregroundColor(.white.opacity(0.4))
                Spacer()
                Text("1kHz").font(.system(size: 9, weight: .regular)).foregroundColor(.white.opacity(0.4))
                Spacer()
                Text("10kHz").font(.system(size: 9, weight: .regular)).foregroundColor(.white.opacity(0.4))
                Spacer()
                Text("20kHz").font(.system(size: 9, weight: .regular)).foregroundColor(.white.opacity(0.4))
            }
            .padding(.horizontal, 4)
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
    
    // Sleek grid helper
    @ViewBuilder
    private func gridBackground(size: CGSize) -> some View {
        ZStack {
            // Horizontal lines (dB Steps: +12, +6, 0, -6, -12)
            ForEach([6, 0, -6], id: \.self) { db in
                let yRatio = CGFloat((12.0 - Double(db)) / 24.0)
                let yVal = yRatio * size.height
                
                Path { path in
                    path.move(to: CGPoint(x: 0, y: yVal))
                    path.addLine(to: CGPoint(x: size.width, y: yVal))
                }
                .stroke(db == 0 ? Color.white.opacity(0.2) : Color.white.opacity(0.05),
                        style: StrokeStyle(lineWidth: 1, dash: db == 0 ? [] : [2, 3]))
                .overlay(
                    Text("\(db > 0 ? "+" : "")\(db)dB")
                        .font(.system(size: 8, weight: .regular))
                        .foregroundColor(.white.opacity(0.3))
                        .position(x: 25, y: yVal - 6),
                    alignment: .topLeading
                )
            }
            
            // Vertical log lines (Freq steps)
            let keyFrequencies: [Float] = [50, 100, 200, 500, 1000, 2000, 5000, 10000]
            ForEach(keyFrequencies, id: \.self) { freq in
                let logMin = log10(20.0)
                let logMax = log10(20000.0)
                let ratio = CGFloat((log10(Double(freq)) - logMin) / (logMax - logMin))
                
                Path { path in
                    path.move(to: CGPoint(x: ratio * size.width, y: 0))
                    path.addLine(to: CGPoint(x: ratio * size.width, y: size.height))
                }
                .stroke(Color.white.opacity(0.04), style: StrokeStyle(lineWidth: 1, dash: [2, 3]))
            }
        }
    }
    
    // Interactive hover helper
    @ViewBuilder
    private func hoverOverlay(location: CGPoint, size: CGSize) -> some View {
        let xRatio = max(0, min(1.0, location.x / size.width))
        
        // Convert xRatio back to log frequency
        let logMin = log10(20.0)
        let logMax = log10(20000.0)
        let logFreq = Double(logMin) + Double(xRatio) * (Double(logMax) - Double(logMin))
        let freq = pow(10.0, logFreq)
        
        // Find nearest point index in viewModel
        let index = min(119, max(0, Int(xRatio * 119)))
        
        let activeDb = viewModel.activePathPoints.indices.contains(index) ? 12.0 - Double(viewModel.activePathPoints[index].y * 24.0) : 0.0
        let harmanDb = viewModel.harmanPathPoints.indices.contains(index) ? 12.0 - Double(viewModel.harmanPathPoints[index].y * 24.0) : 0.0
        
        ZStack {
            // vertical guide line
            Path { path in
                path.move(to: CGPoint(x: location.x, y: 0))
                path.addLine(to: CGPoint(x: location.x, y: size.height))
            }
            .stroke(Color.white.opacity(0.15), lineWidth: 1)
            
            // Dot for Harman reference
            if viewModel.harmanPathPoints.indices.contains(index) {
                Circle()
                    .fill(Color.white.opacity(0.4))
                    .frame(width: 5, height: 5)
                    .position(x: location.x, y: viewModel.harmanPathPoints[index].y * size.height)
            }
            
            // Dot for Active sound signature
            if viewModel.activePathPoints.indices.contains(index) {
                Circle()
                    .fill(Color.blue)
                    .frame(width: 5, height: 5)
                    .position(x: location.x, y: viewModel.activePathPoints[index].y * size.height)
            }
            
            // HUD panel displaying exact statistics comparing both
            VStack(alignment: .leading, spacing: 2) {
                Text("\(Int(freq)) Hz")
                    .font(.system(size: 10, weight: .bold))
                    .foregroundColor(.white)
                
                HStack(spacing: 6) {
                    Circle().fill(Color.blue).frame(width: 4, height: 4)
                    Text("Active: \(String(format: "%.1f", activeDb)) dB")
                        .font(.system(size: 9, weight: .regular))
                        .foregroundColor(Color.blue)
                }
                
                HStack(spacing: 6) {
                    Circle().fill(Color.white.opacity(0.5)).frame(width: 4, height: 4)
                    Text("Harman: \(String(format: "%.1f", harmanDb)) dB")
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
