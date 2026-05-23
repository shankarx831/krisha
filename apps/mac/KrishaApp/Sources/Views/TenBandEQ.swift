import SwiftUI
import AppKit

struct TenBandEQ: View {
    @ObservedObject private var presetManager = PresetManager.shared

    // Dynamically sort the active EQ band indices by frequency so they appear in perfect ascending order (bass to treble)
    var displayOrder: [Int] {
        let bandIndices = Array(0..<10)
        let sortedBandIndices = bandIndices.sorted { idx1, idx2 in
            presetManager.currentFrequencies[idx1] < presetManager.currentFrequencies[idx2]
        }
        return [10, 11] + sortedBandIndices
    }

    /// Format frequency value for band labels
    private func formatFrequency(_ hz: Float) -> String {
        if hz < 1000 {
            return "\(Int(hz))"
        } else if hz < 10000 {
            return String(format: "%.1fK", hz / 1000)
        } else {
            return "\(Int(hz / 1000))K"
        }
    }

    var body: some View {
        VStack(spacing: 8) {
            // Grid of vertical sliders with background grid lines
            ZStack(alignment: .top) {
                // Horizontal grid lines - aligned perfectly with slider track bounds [10, 90]
                GeometryReader { geometry in
                    let trackHeight: CGFloat = 80
                    let trackPadding: CGFloat = 10

                    // Draw grid lines every 3 dB (-12 to +12 = 9 lines)
                    ForEach(0..<9, id: \.self) { index in
                        let dbValue = 12 - (Float(index) * 3)
                        let yPosition = trackPadding + (trackHeight * CGFloat(index) / 8.0)
                        let isCenterLine = (dbValue == 0)

                        Rectangle()
                            .fill(Color(NSColor.separatorColor).opacity(isCenterLine ? 0.3 : 0.15))
                            .frame(height: 1)
                            .offset(y: yPosition)
                    }
                }
                .frame(height: 100)

                // Sliders on top of grid
                HStack(spacing: 0) {
                    ForEach(displayOrder, id: \.self) { index in
                        VStack(spacing: 4) {
                            VerticalSlider(
                                value: index < 10
                                    ? Binding(
                                        get: { presetManager.currentBands[index] },
                                        set: { newValue in
                                            presetManager.updateBand(index: index, gainDb: newValue)
                                        }
                                    )
                                    : (index == 10
                                        ? Binding(
                                            get: { presetManager.currentPreampLeftDb },
                                            set: { newValue in
                                                presetManager.updatePreampLeft(gainDb: newValue)
                                            }
                                        )
                                        : Binding(
                                            get: { presetManager.currentPreampRightDb },
                                            set: { newValue in
                                                presetManager.updatePreampRight(gainDb: newValue)
                                            }
                                        )),
                                range: -12...12,
                                isFocused: presetManager.focusedBandIndex == index,
                                onDoubleTap: {
                                    presetManager.toggleFocusedBand(index)
                                }
                            )
                            .frame(width: 20, height: 100)

                            // Frequency label
                            Text(index == 10 ? "Pre L" : (index == 11 ? "Pre R" : formatFrequency(presetManager.currentFrequencies[index])))
                            .font(.system(size: 9))
                            .foregroundColor(index >= 10 ? Color.blue.opacity(0.8) : Color.white.opacity(0.6))
                            .frame(minWidth: 18)
                        }
                        .padding(.horizontal, 1.5)

                        if index == 11 {
                            // Subtle separator after preamp knobs
                            Rectangle()
                                .fill(Color(NSColor.separatorColor).opacity(0.3))
                                .frame(width: 1, height: 80)
                                .padding(.horizontal, 2)
                        }
                    }
                }
            }
            .background(
                // Scroll wheel receiver for Q factor — active when a band (0-9) is focused
                Group {
                    if let focusedIndex = presetManager.focusedBandIndex, focusedIndex < 10 {
                        ScrollWheelReceiver { delta in
                            let currentQ = presetManager.currentQFactors[focusedIndex]
                            let newQ = currentQ + Float(delta) * 0.1
                            presetManager.updateBandQ(index: focusedIndex, qFactor: newQ)
                        }
                    }
                }
            )
            .contentShape(Rectangle())
            .onTapGesture {
                presetManager.setFocusedBand(nil)
            }
            .padding(.horizontal, 8)
            .padding(.vertical, 6)
        }
    }
}

struct VerticalSlider: View {
    @Binding var value: Float
    let range: ClosedRange<Float>
    let isFocused: Bool
    let onDoubleTap: () -> Void

    private let normalKnobSize: CGFloat = 16
    private let focusedKnobSize: CGFloat = 22

    // Bounds for the slider's physical track inside the height 100 view
    private let trackPadding: CGFloat = 10
    private let trackHeight: CGFloat = 80

    // While dragging, if the user moves the cursor horizontally beyond this threshold, the dB levels will be adjusted more precisely
    private let preciseThresholdX: Float = 50
    // Exit precise mode below this threshold (hysteresis prevents flickering near boundary)
    private let preciseExitThresholdX: Float = 35
    // Multiplier that allows for fine-grained dB adjustments
    private let preciseFactor: Float = 0.05

    // Remember the drag Y value so we can maintain the slider position when shifting to precise mode
    @State private var prevDragY: CGFloat? = nil

    // Used for the knob border to reflect the current dragging mode
    @State private var isDragging: Bool = false
    @State private var isPreciseDrag: Bool = false

    private var knobSize: CGFloat {
        isFocused ? focusedKnobSize : normalKnobSize
    }

    private var knobDisplayText: String {
        if value >= 0 {
            return String(format: "+%.1f", value)
        } else {
            return String(format: "%.1f", value)
        }
    }

    var body: some View {
        GeometryReader { geometry in
            ZStack(alignment: .bottom) {
                // Track background - constrained exactly to track bounds
                RoundedRectangle(cornerRadius: 2)
                    .fill(Color(NSColor.separatorColor))
                    .frame(width: 4, height: trackHeight)
                    .position(x: geometry.size.width / 2, y: geometry.size.height / 2)

                // Center line (0 dB) - positioned exactly at centerY
                let centerY = trackPadding + trackHeight / 2 // 50
                Rectangle()
                    .fill(Color(NSColor.tertiaryLabelColor))
                    .frame(width: 16, height: 1)
                    .position(x: geometry.size.width / 2, y: centerY)

                // Filled portion (from center to knob) - perfectly aligned
                let clampedValue = min(max(range.lowerBound, value), range.upperBound)
                let normalizedValue = CGFloat((clampedValue - range.lowerBound) / (range.upperBound - range.lowerBound))
                let knobY = trackPadding + trackHeight * (1 - normalizedValue)

                if value != 0 {
                    let fillHeight = abs(knobY - centerY)
                    let fillY = min(knobY, centerY)

                    RoundedRectangle(cornerRadius: 2)
                        .fill(Color.blue)
                        .frame(width: 4, height: fillHeight)
                        .position(x: geometry.size.width / 2, y: fillY + fillHeight / 2)
                }

                // Knob - perfectly centered on knobY, no focus jump because knobY doesn't depend on knobSize
                ZStack {
                    Circle()
                        .fill(Color(white: 0.15))
                        .overlay(
                            Circle()
                                .stroke(
                                    isFocused ? Color.blue : Color.white.opacity(0.2),
                                    lineWidth: isFocused ? 1.5 : 1.0
                                )
                        )

                    // dB text inside focused knob
                    if isFocused {
                        Text(knobDisplayText)
                            .font(.system(size: 7, weight: .bold, design: .default))
                            .foregroundColor(.white.opacity(0.9))
                            .lineLimit(1)
                            .minimumScaleFactor(0.7)
                            .monospacedDigit()
                    }
                }
                .frame(width: knobSize, height: knobSize)
                .position(
                    x: geometry.size.width / 2,
                    y: knobY
                )
                .onTapGesture(count: 2) {
                    onDoubleTap()
                }
                .gesture(
                    DragGesture(minimumDistance: 1)
                        .onChanged { gesture in
                            let rangeSpan = range.upperBound - range.lowerBound
                            let knobCenterX = Float(knobSize / 2)
                            let mouseDistanceX = abs(Float(gesture.location.x) - knobCenterX)

                            isDragging = true
                            if mouseDistanceX > preciseThresholdX {
                                isPreciseDrag = true
                            } else if mouseDistanceX < preciseExitThresholdX {
                                isPreciseDrag = false
                            }
                            let multiplier: Float = isPreciseDrag ? preciseFactor : 1

                            let deltaY = Float(gesture.translation.height - (prevDragY ?? gesture.translation.height))
                            let progress = -deltaY / Float(trackHeight) // Map directly to track bounds
                            let valueIncrement = progress * rangeSpan * multiplier
                            value = min(max(range.lowerBound, value + valueIncrement), range.upperBound)

                            prevDragY = gesture.translation.height
                        }
                        .onEnded { _ in
                            isDragging = false
                            isPreciseDrag = false
                            prevDragY = nil
                        }
                )
            }
        }
    }
}

// MARK: - Scroll Wheel Receiver (macOS)

struct ScrollWheelReceiver: NSViewRepresentable {
    let onScroll: (CGFloat) -> Void

    func makeNSView(context: Context) -> ScrollWheelNSView {
        ScrollWheelNSView(onScroll: onScroll)
    }

    func updateNSView(_ nsView: ScrollWheelNSView, context: Context) {
        nsView.onScroll = onScroll
    }

    class ScrollWheelNSView: NSView {
        var onScroll: (CGFloat) -> Void

        init(onScroll: @escaping (CGFloat) -> Void) {
            self.onScroll = onScroll
            super.init(frame: .zero)
        }

        required init?(coder: NSCoder) { fatalError() }

        override func scrollWheel(with event: NSEvent) {
            onScroll(event.deltaY)
        }
    }
}
