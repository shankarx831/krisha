import Foundation
import CoreAudio
import AudioToolbox
import CKrishaAudio

class AudioRenderer {
    private let memoryManager: SharedMemoryManager
    private let dspProcessor: DSPProcessor
    private let proxyManager: ProxyDeviceManager
    private var didLogRenderInfo = false
    private var debugRenderCount: Int = 0
    private var testTonePhase: Float = 0
    private let useTestTone: Bool
    private let bypassDSP: Bool

    // Pre-allocated raw pointer buffer to completely eliminate ARC and heap allocations in IOPROC
    private let tempBuffer: UnsafeMutablePointer<Float>
    private let tempBufferCapacity = 16384

    init(
        memoryManager: SharedMemoryManager,
        dspProcessor: DSPProcessor,
        proxyManager: ProxyDeviceManager
    ) {
        self.memoryManager = memoryManager
        self.dspProcessor = dspProcessor
        self.proxyManager = proxyManager
        self.useTestTone = (ProcessInfo.processInfo.environment["RF_TEST_TONE"] == "1")
        self.bypassDSP = (ProcessInfo.processInfo.environment["RF_BYPASS_DSP"] == "1")

        // Pre-allocate raw float array
        self.tempBuffer = UnsafeMutablePointer<Float>.allocate(capacity: tempBufferCapacity)
        self.tempBuffer.initialize(repeating: 0.0, count: tempBufferCapacity)
    }

    deinit {
        tempBuffer.deallocate()
    }

    func createRenderCallback() -> AURenderCallback {
        return { (
            inRefCon,
            ioActionFlags,
            inTimeStamp,
            inBusNumber,
            inNumberFrames,
            ioData
        ) -> OSStatus in
            guard let bufferList = ioData else {
                return noErr
            }

            let renderer = Unmanaged<AudioRenderer>.fromOpaque(inRefCon).takeUnretainedValue()
            renderer.render(bufferList: bufferList, frameCount: inNumberFrames)

            return noErr
        }
    }

    private func render(bufferList: UnsafeMutablePointer<AudioBufferList>, frameCount: UInt32) {
        if !didLogRenderInfo {
            didLogRenderInfo = true
            let numBuffers = Int(bufferList.pointee.mNumberBuffers)
            var sizes: [UInt32] = []
            for i in 0..<numBuffers {
                let buf = UnsafeMutableAudioBufferListPointer(bufferList)[i]
                sizes.append(buf.mDataByteSize)
            }
            print("[AudioRenderer] First render: frames=\(frameCount) buffers=\(numBuffers) sizes=\(sizes)")
        }

        let sharedMem: UnsafeMutablePointer<RFSharedAudio>?

        if let activeUID = proxyManager.activeProxyUID {
            sharedMem = memoryManager.getMemory(for: activeUID)
        } else {
            sharedMem = memoryManager.getFirstMemory()
        }

        guard let mem = sharedMem else {
            outputSilence(bufferList: bufferList, frameCount: frameCount)
            return
        }

        let needed = Int(frameCount) * 2
        if needed > tempBufferCapacity {
            // Safety fallback, should practically never happen since 16384 is massive.
            outputSilence(bufferList: bufferList, frameCount: frameCount)
            return;
        }

        // Clear only the active segment we are about to read into
        tempBuffer.initialize(repeating: 0.0, count: needed)

        let framesRead: UInt32

        if useTestTone {
            let sampleRate = Float(KrishaConfig.activeSampleRate)
            let freq: Float = 440.0
            let phaseInc = (2.0 * Float.pi * freq) / sampleRate
            for i in 0..<Int(frameCount) {
                let sample = sin(testTonePhase) * 0.2
                tempBuffer[i * 2] = sample
                tempBuffer[i * 2 + 1] = sample
                testTonePhase += phaseInc
                if testTonePhase > 2.0 * Float.pi {
                    testTonePhase -= 2.0 * Float.pi
                }
            }
            framesRead = frameCount
        } else {
            framesRead = rf_ring_read(mem, tempBuffer, frameCount)
        }

        if debugRenderCount < 5 {
            debugRenderCount += 1
            let sampleCount = Int(framesRead) * 2
            if sampleCount > 0 {
                var maxAbs: Float = 0
                for i in 0..<sampleCount {
                    let v = abs(tempBuffer[i])
                    if v > maxAbs { maxAbs = v }
                }
                print("[AudioRenderer] Debug: framesRead=\(framesRead) maxAbs=\(maxAbs)")
            } else {
                print("[AudioRenderer] Debug: framesRead=0")
            }
        }

        if !bypassDSP {
            dspProcessor.processInterleavedRaw(tempBuffer, output: tempBuffer, frameCount: framesRead)
        }

        deinterleave(
            source: tempBuffer,
            bufferList: bufferList,
            framesRead: framesRead,
            totalFrames: frameCount
        )
    }

    private func outputSilence(bufferList: UnsafeMutablePointer<AudioBufferList>, frameCount: UInt32) {
        let leftBuffer = bufferList.pointee.mBuffers.mData!.assumingMemoryBound(to: Float.self)
        let rightBuffer = UnsafeMutableAudioBufferListPointer(bufferList)[1].mData!.assumingMemoryBound(to: Float.self)

        for i in 0..<Int(frameCount) {
            leftBuffer[i] = 0
            rightBuffer[i] = 0
        }
    }

    private func deinterleave(
        source: UnsafePointer<Float>,
        bufferList: UnsafeMutablePointer<AudioBufferList>,
        framesRead: UInt32,
        totalFrames: UInt32
    ) {
        let leftBuffer = bufferList.pointee.mBuffers.mData!.assumingMemoryBound(to: Float.self)
        let rightBuffer = UnsafeMutableAudioBufferListPointer(bufferList)[1].mData!.assumingMemoryBound(to: Float.self)

        for i in 0..<Int(framesRead) {
            leftBuffer[i] = source[i * 2]
            rightBuffer[i] = source[i * 2 + 1]
        }

        for i in Int(framesRead)..<Int(totalFrames) {
            leftBuffer[i] = 0
            rightBuffer[i] = 0
        }
    }
}
