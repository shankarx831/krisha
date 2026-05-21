import SwiftUI
import Combine
import CRadioformDSP

class LiveEQGraphViewModel: ObservableObject {
    @Published var leftPathPoints: [CGPoint] = []
    @Published var rightPathPoints: [CGPoint] = []
    
    private var cancellables = Set<AnyCancellable>()
    private let calculationQueue = DispatchQueue(label: "com.radioform.eq-graph-calc", qos: .userInteractive)
    
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
            guard let offlineEngine = radioform_dsp_create(48000) else { return }
            defer {
                radioform_dsp_destroy(offlineEngine)
            }
            
            var preset = radioform_preset_t()
            radioform_dsp_preset_init_flat(&preset)
            preset.num_bands = 10
            preset.preamp_db = preampDb
            preset.preamp_left_db = preampLeftDb
            preset.preamp_right_db = preampRightDb
            preset.limiter_enabled = limiterEnabled
            preset.limiter_threshold_db = limiterThresholdDb
            
            for i in 0..<10 {
                let gain = isEnabled ? bands[i] : 0.0
                preset.bands[i].frequency_hz = frequencies[i]
                preset.bands[i].gain_db = gain
                preset.bands[i].q_factor = qFactors[i]
                preset.bands[i].type = radioform_filter_type_t(UInt32(filterTypes[i].rawValue))
                preset.bands[i].enabled = isEnabled && (abs(gain) > 0.01)
            }
            
            let applyResult = radioform_dsp_apply_preset(offlineEngine, &preset)
            guard applyResult == RADIOFORM_OK else { return }
            
            var leftPoints: [CGPoint] = []
            var rightPoints: [CGPoint] = []
            
            for i in 0..<120 {
                let freq = testFrequencies[i]
                let leftDb = radioform_dsp_get_magnitude_at_frequency(offlineEngine, freq, true)
                let rightDb = radioform_dsp_get_magnitude_at_frequency(offlineEngine, freq, false)
                
                // x is standardized in [0, 1] range representing log frequency
                let x = CGFloat(i) / 119.0
                
                // Cap DB between -12dB and +12dB
                let capLeftDb = max(-12.0, min(12.0, leftDb))
                let capRightDb = max(-12.0, min(12.0, rightDb))
                
                // y is standardized in [0, 1] range where 0 is +12dB and 1 is -12dB
                let yLeft = CGFloat((12.0 - capLeftDb) / 24.0)
                let yRight = CGFloat((12.0 - capRightDb) / 24.0)
                
                leftPoints.append(CGPoint(x: x, y: yLeft))
                rightPoints.append(CGPoint(x: x, y: yRight))
            }
            
            DispatchQueue.main.async {
                self.leftPathPoints = leftPoints
                self.rightPathPoints = rightPoints
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
                ZStack {
                    // Sleek grid background
                    gridBackground(size: geo.size)
                    
                    // Left Response Curve (Neon Cyan)
                    curvePath(points: viewModel.leftPathPoints, size: geo.size)
                        .stroke(
                            LinearGradient(
                                colors: [Color(.sRGB, red: 0.0, green: 0.9, blue: 0.9, alpha: 1.0), Color(.sRGB, red: 0.0, green: 0.5, blue: 0.9, alpha: 1.0)],
                                startPoint: .leading,
                                endPoint: .trailing
                            ),
                            lineWidth: 2
                        )
                        .shadow(color: Color(.sRGB, red: 0.0, green: 0.8, blue: 0.8, alpha: 0.4), radius: 3)
                    
                    // Right Response Curve (Neon Magenta)
                    curvePath(points: viewModel.rightPathPoints, size: geo.size)
                        .stroke(
                            LinearGradient(
                                colors: [Color(.sRGB, red: 1.0, green: 0.0, blue: 0.6, alpha: 1.0), Color(.sRGB, red: 0.8, green: 0.0, blue: 0.9, alpha: 1.0)],
                                startPoint: .leading,
                                endPoint: .trailing
                            ),
                            lineWidth: 2
                        )
                        .shadow(color: Color(.sRGB, red: 0.9, green: 0.0, blue: 0.7, alpha: 0.4), radius: 3)
                    
                    // Hover Tooltip Marker & Text Overlay
                    if let location = hoverLocation {
                        hoverOverlay(location: location, size: geo.size)
                    }
                }
                .background(
                    RoundedRectangle(cornerRadius: 12)
                        .fill(Color.black.opacity(0.45))
                        .overlay(
                            RoundedRectangle(cornerRadius: 12)
                                .stroke(Color.white.opacity(0.12), lineWidth: 1)
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
                Text("20Hz").font(.system(size: 9, weight: .semibold)).foregroundColor(.white.opacity(0.4))
                Spacer()
                Text("100Hz").font(.system(size: 9, weight: .semibold)).foregroundColor(.white.opacity(0.4))
                Spacer()
                Text("1kHz").font(.system(size: 9, weight: .semibold)).foregroundColor(.white.opacity(0.4))
                Spacer()
                Text("10kHz").font(.system(size: 9, weight: .semibold)).foregroundColor(.white.opacity(0.4))
                Spacer()
                Text("20kHz").font(.system(size: 9, weight: .semibold)).foregroundColor(.white.opacity(0.4))
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
            VStack {
                ForEach([12, 6, 0, -6, -12], id: \.self) { db in
                    if db != 12 && db != -12 {
                        let yRatio = CGFloat((12.0 - Double(db)) / 24.0)
                        Path { path in
                            path.move(to: CGPoint(x: 0, y: yRatio * size.height))
                            path.addLine(to: CGPoint(x: size.width, y: yRatio * size.height))
                        }
                        .stroke(db == 0 ? Color.white.opacity(0.25) : Color.white.opacity(0.08),
                                style: StrokeStyle(lineWidth: 1, dash: db == 0 ? [] : [2, 3]))
                        .overlay(
                            Text("\(db > 0 ? "+" : "")\(db)dB")
                                .font(.system(size: 8, weight: .bold))
                                .foregroundColor(.white.opacity(0.3))
                                .position(x: 20, y: yRatio * size.height - 6),
                            alignment: .topLeading
                        )
                    }
                }
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
                .stroke(Color.white.opacity(0.06), style: StrokeStyle(lineWidth: 1, dash: [2, 3]))
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
        
        let leftDb = viewModel.leftPathPoints.indices.contains(index) ? 12.0 - Double(viewModel.leftPathPoints[index].y * 24.0) : 0.0
        let rightDb = viewModel.rightPathPoints.indices.contains(index) ? 12.0 - Double(viewModel.rightPathPoints[index].y * 24.0) : 0.0
        
        ZStack {
            // vertical guide line
            Path { path in
                path.move(to: CGPoint(x: location.x, y: 0))
                path.addLine(to: CGPoint(x: location.x, y: size.height))
            }
            .stroke(Color.white.opacity(0.2), lineWidth: 1)
            
            // Neon Cyan dot for Left channel
            if viewModel.leftPathPoints.indices.contains(index) {
                Circle()
                    .fill(Color(.sRGB, red: 0.0, green: 0.9, blue: 0.9, alpha: 1.0))
                    .frame(width: 6, height: 6)
                    .position(x: location.x, y: viewModel.leftPathPoints[index].y * size.height)
            }
            
            // Neon Magenta dot for Right channel
            if viewModel.rightPathPoints.indices.contains(index) {
                Circle()
                    .fill(Color(.sRGB, red: 1.0, green: 0.0, blue: 0.6, alpha: 1.0))
                    .frame(width: 6, height: 6)
                    .position(x: location.x, y: viewModel.rightPathPoints[index].y * size.height)
            }
            
            // HUD panel displaying exact statistics
            VStack(alignment: .leading, spacing: 2) {
                Text("\(Int(freq)) Hz")
                    .font(.system(size: 10, weight: .bold))
                    .foregroundColor(.white)
                
                HStack(spacing: 6) {
                    Circle().fill(Color(.sRGB, red: 0.0, green: 0.8, blue: 0.8, alpha: 1.0)).frame(width: 4, height: 4)
                    Text("L: \(String(format: "%.1f", leftDb)) dB")
                        .font(.system(size: 9, weight: .semibold))
                        .foregroundColor(Color(.sRGB, red: 0.0, green: 0.85, blue: 0.85, alpha: 1.0))
                }
                
                HStack(spacing: 6) {
                    Circle().fill(Color(.sRGB, red: 0.9, green: 0.0, blue: 0.6, alpha: 1.0)).frame(width: 4, height: 4)
                    Text("R: \(String(format: "%.1f", rightDb)) dB")
                        .font(.system(size: 9, weight: .semibold))
                        .foregroundColor(Color(.sRGB, red: 0.95, green: 0.0, blue: 0.65, alpha: 1.0))
                }
            }
            .padding(6)
            .background(
                RoundedRectangle(cornerRadius: 6)
                    .fill(Color.black.opacity(0.85))
                    .overlay(RoundedRectangle(cornerRadius: 6).stroke(Color.white.opacity(0.15), lineWidth: 0.5))
            )
            .position(x: location.x + (location.x > size.width * 0.7 ? -50 : 50), y: 35)
        }
    }
}
